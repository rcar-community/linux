// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2025 Renesas Electronics Corp.
 * Copyright 2025 Niklas Söderlund <niklas.soderlund@ragnatech.se>
 */

#include "rppx1.h"

int rppx1_params_rkisp1(struct rppx1 *rpp, struct rkisp1_ext_params_cfg *cfg,
			rppx1_reg_write write, void *priv)
{
	size_t block_offset = 0;

	if (WARN_ON(!cfg))
		return -EINVAL;

	/* Walk the list of parameter blocks and process them. */
	while (block_offset < cfg->data_size) {
		const union rppx1_params_rkisp1_config *block =
			(const union rppx1_params_rkisp1_config *)&cfg->data[block_offset];
		struct rpp_module *module;

		block_offset += block->header.size;

		switch (block->header.type) {
		case RKISP1_EXT_PARAMS_BLOCK_TYPE_BLS:
			module = &rpp->pre1.bls;
			break;
		case RKISP1_EXT_PARAMS_BLOCK_TYPE_AWB_GAIN:
			module = &rpp->post.awbg;
			break;
		case RKISP1_EXT_PARAMS_BLOCK_TYPE_FLT:
		case RKISP1_EXT_PARAMS_BLOCK_TYPE_BDM:
			/* Both types handled by the same block. */
			module = &rpp->post.db;
			break;
		case RKISP1_EXT_PARAMS_BLOCK_TYPE_CTK:
			module = &rpp->post.ccor;
			break;
		case RKISP1_EXT_PARAMS_BLOCK_TYPE_GOC:
			module = &rpp->hv.ga;
			break;
		case RKISP1_EXT_PARAMS_BLOCK_TYPE_DPF:
		case RKISP1_EXT_PARAMS_BLOCK_TYPE_DPF_STRENGTH:
			/* Both types handled by the same block. */
			module = &rpp->pre1.bd;
			break;
		case RKISP1_EXT_PARAMS_BLOCK_TYPE_LSC:
			module = &rpp->pre1.lsc;
			break;
		case RKISP1_EXT_PARAMS_BLOCK_TYPE_AWB_MEAS:
			module = &rpp->post.wbmeas;
			break;
		case RKISP1_EXT_PARAMS_BLOCK_TYPE_HST_MEAS:
			module = &rpp->post.hist;
			break;
		case RKISP1_EXT_PARAMS_BLOCK_TYPE_AEC_MEAS:
			module = &rpp->pre1.exm;
			break;
		default:
			module = NULL;
			break;
		}

		if (!module) {
			pr_warn("Not handled RPPX1 block type: 0x%04x\n", block->header.type);
			continue;
		}

		rpp_module_call(module, param_rkisp1, block, write, priv);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rppx1_params_rkisp1);
