// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2025 Renesas Electronics Corp.
 * Copyright 2025 Niklas Söderlund <niklas.soderlund@ragnatech.se>
 *
 * Support library for Dreamchip HDR RPPX1 High Dynamic Range Real-time Pixel
 * Processor.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "rppx1.h"

/* RPP_HDR Base Addresses */
#define HDRREGS_BASE				0x0000
#define HDR_IRQ_BASE				0x0200
#define RPP_OUT_BASE				0x0800
#define RPP_RMAP_BASE				0x0c00
#define RPP_RMAP_MEAS_BASE			0x1000
#define RPP_MAIN_PRE1_BASE			0x2000
#define RPP_MAIN_PRE2_BASE			0x4000
#define RPP_MAIN_POST_BASE			0xa000
#define RPP_MVOUT_BASE				0xc000
#define RPP_FUSA_BASE				0xf000

#define RPP_HDRREGS_VERSION_REG			(HDRREGS_BASE + 0x0000)
#define RPP_HDR_UPD_REG				(HDRREGS_BASE + 0x0004)
#define RESERVED_3_REG				(HDRREGS_BASE + 0x0008)
#define RPP_HDR_INFORM_ENABLE_REG		(HDRREGS_BASE + 0x000c)
#define RPP_HDR_OUT_IF_ON_REG			(HDRREGS_BASE + 0x0010)
#define RPP_HDR_OUT_IF_OFF_REG			(HDRREGS_BASE + 0x0014)
#define RPP_HDR_SAFETY_ACCESS_PROTECTION_REG	(HDRREGS_BASE + 0x0018)

#define RPP_ISM					(HDR_IRQ_BASE + 0x00)
#define RPP_RIS					(HDR_IRQ_BASE + 0x04)
#define RPP_MIS					(HDR_IRQ_BASE + 0x08)
#define RPP_ISC					(HDR_IRQ_BASE + 0x0c)

/* RPP_OUT/MV_OUT Pipelines - Base Addresses */
#define GAMMA_OUT_BASE				0x0000 /* HV, MV */
#define IS_BASE					0x00c0 /* HV, MV */
#define CSM_BASE				0x0100 /* HV, MV */
#define OUT_IF_BASE				0x0200 /* HV, MV */
#define RPP_OUTREGS_BASE			0x02c0 /* HV, MV */
#define LUV_BASE				0x0300 /* MV */

/* PRE1/PRE2/POST Pipelines - Base Addresses */
#define ACQ_BASE				0x0080 /* PRE1, PRE2 */
#define BLS_BASE				0x0100 /* PRE1, PRE2 */
#define GAMMA_IN_BASE				0x0200 /* PRE1, PRE2 */
#define LSC_BASE				0x0400 /* PRE1, PRE2 */
#define AWB_GAIN_BASE				0x0500 /* PRE1, PRE2, POST */
#define DPCC_BASE				0x0600 /* PRE1, PRE2 */
#define DPF_BASE				0x0700 /* PRE1, PRE2 */
#define FILT_BASE				0x0800 /* POST */
#define CAC_BASE				0x0880 /* POST */
#define CCOR_BASE				0x0900 /* POST */
#define HIST_BASE				0x0a00 /* PRE1, PRE2, POST */
#define HIST256_BASE				0x0b00 /* PRE1 */
#define EXM_BASE				0x0c00 /* PRE1, PRE2 */
#define LTM_BASE				0x1000 /* POST */
#define LTM_MEAS_BASE				0x1200 /* POST */
#define WBMEAS_BASE				0x1700 /* POST */
#define BDRGB_BASE				0x1800 /* POST */
#define SHRP_BASE				0x1a00 /* POST */

/* Functional Safety Module Base Addresses */
#define FMU_BASE				0x0100

#define RPP_HDR_FMU_FSM				(RPP_FUSA_BASE + FMU_BASE + 0x00)
#define RPP_HDR_FMU_RFS				(RPP_FUSA_BASE + FMU_BASE + 0x04)
#define RPP_HDR_FMU_MFS				(RPP_FUSA_BASE + FMU_BASE + 0x08)
#define RPP_HDR_FMU_FSC				(RPP_FUSA_BASE + FMU_BASE + 0x0c)

void rppx1_write(struct rppx1 *rpp, u32 offset, u32 value)
{
	iowrite32(value, rpp->base + offset);
}

u32 rppx1_read(struct rppx1 *rpp, u32 offset)
{
	u32 ret = ioread32(rpp->base + offset);
	return ret;
}

bool rppx1_interrupt(struct rppx1 *rpp, u32 *isc)
{
	u32 status, raw, fault;

	fault = rppx1_read(rpp, RPP_HDR_FMU_MFS);
	if (fault) {
		pr_err("%s: fault 0x%08x\n", __func__, fault);
		rppx1_write(rpp, RPP_HDR_FMU_FSC, fault);
	}

	/* Read raw interrupt status. */
	raw = rppx1_read(rpp, RPP_RIS);
	status = rppx1_read(rpp, RPP_MIS);

	/* Propagate the isc status. */
	if (isc)
		*isc = status | raw;

	/* Clear enabled interrupts */
	rppx1_write(rpp, RPP_ISC, status);

	return !!(status & RPPX1_IRQ_ID_OUT_FRAME);
}
EXPORT_SYMBOL_GPL(rppx1_interrupt);

void rppx1_destroy(struct rppx1 *rpp)
{
	kfree(rpp);
}
EXPORT_SYMBOL_GPL(rppx1_destroy);

/**
 * Allocate the private data structure and verify the hardware is present.
 */
struct rppx1 *rppx1_create(void __iomem *base)
{
	struct rppx1 *rpp;
	u32 reg;

	/* Allocate library structure */
	rpp = kzalloc(sizeof(*rpp), GFP_KERNEL);
	if (!rpp)
		return NULL;

	rpp->base = base;

	/* Check communication with RPP and verify it truly is a X1. */
	reg = rppx1_read(rpp, RPP_HDRREGS_VERSION_REG);
	if (reg != 3) {
		pr_err("Unsupported HDR version (%u)\n", reg);
		rppx1_destroy(rpp);
		return NULL;
	}

	/* Probe the PRE1 pipeline. */
	if (rpp_module_probe(&rpp->pre1.acq, rpp, &rppx1_acq_ops,
			     RPP_MAIN_PRE1_BASE + ACQ_BASE) ||
	    rpp_module_probe(&rpp->pre1.bls, rpp, &rppx1_bls_ops,
			     RPP_MAIN_PRE1_BASE + BLS_BASE) ||
	    rpp_module_probe(&rpp->pre1.lin, rpp, &rppx1_lin_ops,
			     RPP_MAIN_PRE1_BASE + GAMMA_IN_BASE) ||
	    rpp_module_probe(&rpp->pre1.lsc, rpp, &rppx1_lsc_ops,
			     RPP_MAIN_PRE1_BASE + LSC_BASE) ||
	    rpp_module_probe(&rpp->pre1.awbg, rpp, &rppx1_awbg_ops,
			     RPP_MAIN_PRE1_BASE + AWB_GAIN_BASE) ||
	    rpp_module_probe(&rpp->pre1.dpcc, rpp, &rppx1_dpcc_ops,
			     RPP_MAIN_PRE1_BASE + DPCC_BASE) ||
	    rpp_module_probe(&rpp->pre1.bd, rpp, &rppx1_bd_ops,
			     RPP_MAIN_PRE1_BASE + DPF_BASE) ||
	    rpp_module_probe(&rpp->pre1.hist, rpp, &rppx1_hist_ops,
			     RPP_MAIN_PRE1_BASE + HIST_BASE) ||
	    rpp_module_probe(&rpp->pre1.hist256, rpp, &rppx1_hist256_ops,
			     RPP_MAIN_PRE1_BASE + HIST256_BASE) ||
	    rpp_module_probe(&rpp->pre1.exm, rpp, &rppx1_exm_ops,
			     RPP_MAIN_PRE1_BASE + EXM_BASE))
		goto err;

	/* Probe the PRE2 pipeline. */
	if (rpp_module_probe(&rpp->pre2.acq, rpp, &rppx1_acq_ops,
			     RPP_MAIN_PRE2_BASE + ACQ_BASE) ||
	    rpp_module_probe(&rpp->pre2.bls, rpp, &rppx1_bls_ops,
			     RPP_MAIN_PRE2_BASE + BLS_BASE) ||
	    rpp_module_probe(&rpp->pre2.lin, rpp, &rppx1_lin_ops,
			     RPP_MAIN_PRE2_BASE + GAMMA_IN_BASE) ||
	    rpp_module_probe(&rpp->pre2.lsc, rpp, &rppx1_lsc_ops,
			     RPP_MAIN_PRE2_BASE + LSC_BASE) ||
	    rpp_module_probe(&rpp->pre2.awbg, rpp, &rppx1_awbg_ops,
			     RPP_MAIN_PRE2_BASE + AWB_GAIN_BASE) ||
	    rpp_module_probe(&rpp->pre2.dpcc, rpp, &rppx1_dpcc_ops,
			     RPP_MAIN_PRE2_BASE + DPCC_BASE) ||
	    rpp_module_probe(&rpp->pre2.bd, rpp, &rppx1_bd_ops,
			     RPP_MAIN_PRE2_BASE + DPF_BASE) ||
	    rpp_module_probe(&rpp->pre2.hist, rpp, &rppx1_hist_ops,
			     RPP_MAIN_PRE2_BASE + HIST_BASE) ||
	    rpp_module_probe(&rpp->pre2.exm, rpp, &rppx1_exm_ops,
			     RPP_MAIN_PRE2_BASE + EXM_BASE))
		goto err;

	/* Probe the POST pipeline. */
	if (rpp_module_probe(&rpp->post.awbg, rpp, &rppx1_awbg_ops,
			     RPP_MAIN_POST_BASE + AWB_GAIN_BASE) ||
	    rpp_module_probe(&rpp->post.ccor, rpp, &rppx1_ccor_ops,
			     RPP_MAIN_POST_BASE + CCOR_BASE) ||
	    rpp_module_probe(&rpp->post.hist, rpp, &rppx1_hist_ops,
			     RPP_MAIN_POST_BASE + HIST_BASE) ||
	    rpp_module_probe(&rpp->post.db, rpp, &rppx1_db_ops,
			     RPP_MAIN_POST_BASE + FILT_BASE) ||
	    rpp_module_probe(&rpp->post.cac, rpp, &rppx1_cac_ops,
			     RPP_MAIN_POST_BASE + CAC_BASE) ||
	    rpp_module_probe(&rpp->post.ltm, rpp, &rppx1_ltm_ops,
			     RPP_MAIN_POST_BASE + LTM_BASE) ||
	    rpp_module_probe(&rpp->post.ltmmeas, rpp, &rppx1_ltmmeas_ops,
			     RPP_MAIN_POST_BASE + LTM_MEAS_BASE) ||
	    rpp_module_probe(&rpp->post.wbmeas, rpp, &rppx1_wbmeas_ops,
			     RPP_MAIN_POST_BASE + WBMEAS_BASE) ||
	    rpp_module_probe(&rpp->post.bdrgb, rpp, &rppx1_bdrgb_ops,
			     RPP_MAIN_POST_BASE + BDRGB_BASE) ||
	    rpp_module_probe(&rpp->post.shrp, rpp, &rppx1_shrp_ops,
			     RPP_MAIN_POST_BASE + SHRP_BASE))
		goto err;

	/* Probe the Human Vision pipeline. */
	if (rpp_module_probe(&rpp->hv.ga, rpp, &rppx1_ga_ops,
			     RPP_OUT_BASE + GAMMA_OUT_BASE) ||
	    rpp_module_probe(&rpp->hv.is, rpp, &rppx1_is_ops,
			     RPP_OUT_BASE + IS_BASE) ||
	    rpp_module_probe(&rpp->hv.ccor, rpp, &rppx1_ccor_csm_ops,
			     RPP_OUT_BASE + CSM_BASE) ||
	    rpp_module_probe(&rpp->hv.outif, rpp, &rppx1_outif_ops,
			     RPP_OUT_BASE + OUT_IF_BASE) ||
	    rpp_module_probe(&rpp->hv.outregs, rpp, &rppx1_outregs_ops,
			     RPP_OUT_BASE + RPP_OUTREGS_BASE))
		goto err;

	/* Probe the Machine Vision pipeline. */
	if (rpp_module_probe(&rpp->mv.ga, rpp, &rppx1_ga_ops,
			     RPP_MVOUT_BASE + GAMMA_OUT_BASE) ||
	    rpp_module_probe(&rpp->mv.is, rpp, &rppx1_is_ops,
			     RPP_MVOUT_BASE + IS_BASE) ||
	    rpp_module_probe(&rpp->mv.ccor, rpp, &rppx1_ccor_csm_ops,
			     RPP_MVOUT_BASE + CSM_BASE) ||
	    rpp_module_probe(&rpp->mv.outif, rpp, &rppx1_outif_ops,
			     RPP_MVOUT_BASE + OUT_IF_BASE) ||
	    rpp_module_probe(&rpp->mv.outregs, rpp, &rppx1_outregs_ops,
			     RPP_MVOUT_BASE + RPP_OUTREGS_BASE) ||
	    rpp_module_probe(&rpp->mv.xyz2luv, rpp, &rppx1_xyz2luv_ops,
			     RPP_MVOUT_BASE + LUV_BASE))
		goto err;

	/* Probe the standalone Radiance Mapping modules. */
	if (rpp_module_probe(&rpp->rmap, rpp, &rppx1_rmap_ops,
			     RPP_RMAP_BASE) ||
	    rpp_module_probe(&rpp->rmapmeas, rpp, &rppx1_rmapmeas_ops,
			     RPP_RMAP_MEAS_BASE))
		goto err;

	return rpp;
err:
	rppx1_destroy(rpp);

	return NULL;
}
EXPORT_SYMBOL_GPL(rppx1_create);

int rppx1_start(struct rppx1 *rpp,
		const struct v4l2_mbus_framefmt *input,
		const struct v4l2_mbus_framefmt *hv,
		const struct v4l2_mbus_framefmt *mv)
{
	if (rpp_module_call(&rpp->pre1.acq, start, input) ||
	    rpp_module_call(&rpp->pre1.bls, start, input) ||
	    rpp_module_call(&rpp->pre1.lin, start, input) ||
	    rpp_module_call(&rpp->pre1.lsc, start, input) ||
	    rpp_module_call(&rpp->pre1.awbg, start, input) ||
	    rpp_module_call(&rpp->pre1.dpcc, start, input) ||
	    rpp_module_call(&rpp->pre1.bd, start, input) ||
	    rpp_module_call(&rpp->pre1.hist, start, input) ||
	    rpp_module_call(&rpp->pre1.exm, start, input) ||
	    rpp_module_call(&rpp->pre1.hist256, start, input))
		return -EINVAL;

	if (rpp_module_call(&rpp->rmap, start, NULL) ||
	    rpp_module_call(&rpp->rmapmeas, start, NULL))
		return -EINVAL;

	if (rpp_module_call(&rpp->post.awbg, start, input) ||
	    rpp_module_call(&rpp->post.db, start, input) ||
	    rpp_module_call(&rpp->post.cac, start, input) ||
	    rpp_module_call(&rpp->post.ccor, start, input) ||
	    rpp_module_call(&rpp->post.ltm, start, input) ||
	    rpp_module_call(&rpp->post.bdrgb, start, input) ||
	    rpp_module_call(&rpp->post.shrp, start, input) ||
	    rpp_module_call(&rpp->post.ltmmeas, start, input) ||
	    rpp_module_call(&rpp->post.wbmeas, start, input) ||
	    rpp_module_call(&rpp->post.hist, start, input))
		return -EINVAL;

	if (hv && (rpp_module_call(&rpp->hv.ga, start, hv) ||
		   rpp_module_call(&rpp->hv.ccor, start, hv) ||
		   rpp_module_call(&rpp->hv.outregs, start, hv) ||
		   rpp_module_call(&rpp->hv.is, start, hv) ||
		   rpp_module_call(&rpp->hv.outif, start, hv)))
		return -EINVAL;

	if (mv && (rpp_module_call(&rpp->mv.ga, start, mv) ||
		   rpp_module_call(&rpp->mv.ccor, start, mv) ||
		   rpp_module_call(&rpp->mv.xyz2luv, start, mv) ||
		   rpp_module_call(&rpp->mv.outregs, start, mv) ||
		   rpp_module_call(&rpp->mv.is, start, mv) ||
		   rpp_module_call(&rpp->mv.outif, start, mv)))
		return -EINVAL;

	rppx1_write(rpp, RPP_HDR_UPD_REG, 0x00000001);

	/* Clear fault interrupts. */
	rppx1_write(rpp, RPP_HDR_SAFETY_ACCESS_PROTECTION_REG, 0x00000001);
	rppx1_write(rpp, RPP_HDR_FMU_FSM, 0x000001c0);
	rppx1_write(rpp, RPP_HDR_FMU_FSC, rppx1_read(rpp, RPP_HDR_FMU_MFS));
	rppx1_write(rpp, RPP_HDR_SAFETY_ACCESS_PROTECTION_REG, 0x00000000);

	/* Set interrupt mask. */
	rppx1_write(rpp, RPP_ISM, RPPX1_IRQ_ID_OUT_FRAME);

	rppx1_write(rpp, RPP_HDR_UPD_REG, 0x00000001);
	rppx1_write(rpp, RPP_HDR_UPD_REG, 0x00000002);

	/* Clear any pending interrupts. */
	rppx1_interrupt(rpp, NULL);

	/* Enable input formatters. */
	rppx1_write(rpp, RPP_HDR_INFORM_ENABLE_REG, 1);

	return 0;
}
EXPORT_SYMBOL_GPL(rppx1_start);

int rppx1_stop(struct rppx1 *rpp)
{
	/* Disable input formatters. */
	rppx1_write(rpp, RPP_HDR_INFORM_ENABLE_REG, 0);

	/* Clear any pending interrupts. */
	rppx1_interrupt(rpp, NULL);

	return 0;
}
EXPORT_SYMBOL_GPL(rppx1_stop);

MODULE_AUTHOR("Niklas Söderlund <niklas.soderlund@ragnatech.se>");
MODULE_DESCRIPTION("Dreamchip HDR RPPX1 support library");
MODULE_LICENSE("GPL");
