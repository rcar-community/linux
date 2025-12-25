// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2025 Renesas Electronics Corp.
 * Copyright 2025 Niklas Söderlund <niklas.soderlund@ragnatech.se>
 */

#include <linux/bug.h>

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
		int ret;

		block_offset += block->header.size;

		switch (block->header.type) {
		default:
			module = NULL;
			break;
		}

		if (!module) {
			pr_warn("Not handled RPPX1 block type: 0x%04x\n", block->header.type);
			continue;
		}

		ret = rpp_module_call(module, param_rkisp1, block, write, priv);
		if (ret) {
			pr_err("Error processing RPPX1 block type: 0x%04x\n", block->header.type);
			return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(rppx1_params_rkisp1);
