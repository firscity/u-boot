// SPDX-License-Identifier: GPL-2.0+
/*
 * Renesas RCar Gen3 CPG MSSR driver
 *
 * Copyright (C) 2017 Marek Vasut <marek.vasut@gmail.com>
 *
 * Based on the following driver from Linux kernel:
 * r8a7796 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2016 Glider bvba
 */

#include <common.h>
#include <clk-uclass.h>
#include <dm.h>
#include <errno.h>
#include <log.h>
#include <wait_bit.h>
#include <asm/io.h>
#include <linux/bitops.h>

#include <dt-bindings/clock/renesas-cpg-mssr.h>

#include "renesas-cpg-mssr.h"
#include "rcar-gen3-cpg.h"

#define CPG_PLL0CR		0x00d8
#define CPG_PLL2CR		0x002c
#define CPG_PLL4CR		0x01f4

#define CPG_RPC_PREDIV_MASK	0x3
#define CPG_RPC_PREDIV_OFFSET	3
#define CPG_RPC_POSTDIV_MASK	0x7
#define CPG_RPC_POSTDIV_OFFSET	0

/*
 * SDn Clock
 */
#define CPG_SD_STP_HCK		BIT(9)
#define CPG_SD_STP_CK		BIT(8)

#define CPG_SD_STP_MASK		(CPG_SD_STP_HCK | CPG_SD_STP_CK)
#define CPG_SD_FC_MASK		(0x7 << 2 | 0x3 << 0)

#define CPG_SD_DIV_TABLE_DATA(stp_hck, stp_ck, sd_srcfc, sd_fc, sd_div) \
{ \
	.val = ((stp_hck) ? CPG_SD_STP_HCK : 0) | \
	       ((stp_ck) ? CPG_SD_STP_CK : 0) | \
	       ((sd_srcfc) << 2) | \
	       ((sd_fc) << 0), \
	.div = (sd_div), \
}

struct sd_div_table {
	u32 val;
	unsigned int div;
};

/* SDn divider
 *                     sd_srcfc   sd_fc   div
 * stp_hck   stp_ck    (div)      (div)     = sd_srcfc x sd_fc
 *-------------------------------------------------------------------
 *  0         0         0 (1)      1 (4)      4
 *  0         0         1 (2)      1 (4)      8
 *  1         0         2 (4)      1 (4)     16
 *  1         0         3 (8)      1 (4)     32
 *  1         0         4 (16)     1 (4)     64
 *  0         0         0 (1)      0 (2)      2
 *  0         0         1 (2)      0 (2)      4
 *  1         0         2 (4)      0 (2)      8
 *  1         0         3 (8)      0 (2)     16
 *  1         0         4 (16)     0 (2)     32
 */
static const struct sd_div_table cpg_sd_div_table[] = {
/*	CPG_SD_DIV_TABLE_DATA(stp_hck,  stp_ck,   sd_srcfc,   sd_fc,  sd_div) */
	CPG_SD_DIV_TABLE_DATA(0,        0,        0,          1,        4),
	CPG_SD_DIV_TABLE_DATA(0,        0,        1,          1,        8),
	CPG_SD_DIV_TABLE_DATA(1,        0,        2,          1,       16),
	CPG_SD_DIV_TABLE_DATA(1,        0,        3,          1,       32),
	CPG_SD_DIV_TABLE_DATA(1,        0,        4,          1,       64),
	CPG_SD_DIV_TABLE_DATA(0,        0,        0,          0,        2),
	CPG_SD_DIV_TABLE_DATA(0,        0,        1,          0,        4),
	CPG_SD_DIV_TABLE_DATA(1,        0,        2,          0,        8),
	CPG_SD_DIV_TABLE_DATA(1,        0,        3,          0,       16),
	CPG_SD_DIV_TABLE_DATA(1,        0,        4,          0,       32),
};

static int gen3_clk_get_parent(struct gen3_clk_priv *priv, struct clk *clk,
			       struct cpg_mssr_info *info, struct clk *parent)
{
	const struct cpg_core_clk *core;
	int ret;

	if (!renesas_clk_is_mod(clk)) {
		ret = renesas_clk_get_core(clk, info, &core);
		if (ret)
			return ret;

		if (core->type == CLK_TYPE_GEN3_MDSEL) {
			if (priv->cpg_mode & BIT(core->offset))
				parent->id = core->parent & 0xffff;
			else
				parent->id = core->parent >> 16;

			parent->dev = clk->dev;
			return 0;
		}
	}

	return renesas_clk_get_parent(clk, info, parent);
}

static int gen3_clk_setup_sdif_div(struct clk *clk, ulong rate)
{
	struct gen3_clk_priv *priv = dev_get_priv(clk->dev);
	struct cpg_mssr_info *info = priv->info;
	const struct cpg_core_clk *core;
	struct clk parent;
	int ret;

	ret = gen3_clk_get_parent(priv, clk, info, &parent);
	if (ret) {
		printf("%s[%i] parent fail, ret=%i\n", __func__, __LINE__, ret);
		return ret;
	}

	if (renesas_clk_is_mod(&parent))
		return 0;

	ret = renesas_clk_get_core(&parent, info, &core);
	if (ret)
		return ret;

	if (core->type != CLK_TYPE_GEN3_SD)
		return 0;

	debug("%s[%i] SDIF offset=%x\n", __func__, __LINE__, core->offset);

	writel((rate == 400000000) ? 0x4 : 0x1, priv->base + core->offset);

	return 0;
}

static int gen3_clk_enable(struct clk *clk)
{
	struct gen3_clk_priv *priv = dev_get_priv(clk->dev);

	return renesas_clk_endisable(clk, priv->base, priv->info, true);
}

static int gen3_clk_disable(struct clk *clk)
{
	struct gen3_clk_priv *priv = dev_get_priv(clk->dev);

	return renesas_clk_endisable(clk, priv->base, priv->info, false);
}

static u64 gen3_clk_get_rate64(struct clk *clk);

static u64 gen3_clk_get_rate64_pll_mul_reg(struct gen3_clk_priv *priv,
					   struct clk *parent,
					   const struct cpg_core_clk *core,
					   u32 mul_reg, u32 mult, u32 div,
					   char *name)
{
	u32 value;
	u64 rate;

	if (mul_reg) {
		value = readl(priv->base + mul_reg);
		mult = (((value >> 24) & 0x7f) + 1) * 2;
		div = 1;
	}

	rate = (gen3_clk_get_rate64(parent) * mult) / div;

	debug("%s[%i] %s clk: parent=%i mult=%u div=%u => rate=%llu\n",
	      __func__, __LINE__, name, core->parent, mult, div, rate);
	return rate;
}

static u64 gen3_clk_get_rate64(struct clk *clk)
{
	struct gen3_clk_priv *priv = dev_get_priv(clk->dev);
	struct cpg_mssr_info *info = priv->info;
	struct clk parent;
	const struct cpg_core_clk *core;
	const struct rcar_gen3_cpg_pll_config *pll_config =
					priv->cpg_pll_config;
	u32 value, div, prediv, postdiv, parent_id;
	u64 rate = 0;
	int i, ret;

	debug("%s[%i] Clock: id=%lu\n", __func__, __LINE__, clk->id);

	ret = gen3_clk_get_parent(priv, clk, info, &parent);
	if (ret) {
		printf("%s[%i] parent fail, ret=%i\n", __func__, __LINE__, ret);
		return ret;
	}

	if (renesas_clk_is_mod(clk)) {
		rate = gen3_clk_get_rate64(&parent);
		debug("%s[%i] MOD clk: parent=%lu => rate=%llu\n",
		      __func__, __LINE__, parent.id, rate);
		return rate;
	}

	ret = renesas_clk_get_core(clk, info, &core);
	if (ret)
		return ret;

	switch (core->type) {
	case CLK_TYPE_IN:
		if (core->id == info->clk_extal_id) {
			rate = clk_get_rate(&priv->clk_extal);
			debug("%s[%i] EXTAL clk: rate=%llu\n",
			      __func__, __LINE__, rate);
			return rate;
		}

		if (core->id == info->clk_extalr_id) {
			rate = clk_get_rate(&priv->clk_extalr);
			debug("%s[%i] EXTALR clk: rate=%llu\n",
			      __func__, __LINE__, rate);
			return rate;
		}

		return -EINVAL;

	case CLK_TYPE_GEN3_MAIN:
		return gen3_clk_get_rate64_pll_mul_reg(priv, &parent, core,
						0, 1, pll_config->extal_div,
						"MAIN");

	case CLK_TYPE_GEN3_PLL0:
		return gen3_clk_get_rate64_pll_mul_reg(priv, &parent, core,
						CPG_PLL0CR, 0, 0, "PLL0");

	case CLK_TYPE_GEN3_PLL1:
		return gen3_clk_get_rate64_pll_mul_reg(priv, &parent, core,
						0, pll_config->pll1_mult,
						pll_config->pll1_div, "PLL1");

	case CLK_TYPE_GEN3_PLL2:
		return gen3_clk_get_rate64_pll_mul_reg(priv, &parent, core,
						CPG_PLL2CR, 0, 0, "PLL2");

	case CLK_TYPE_GEN3_PLL3:
		return gen3_clk_get_rate64_pll_mul_reg(priv, &parent, core,
						0, pll_config->pll3_mult,
						pll_config->pll3_div, "PLL3");

	case CLK_TYPE_GEN3_PLL4:
		return gen3_clk_get_rate64_pll_mul_reg(priv, &parent, core,
						CPG_PLL4CR, 0, 0, "PLL4");

	case CLK_TYPE_R8A779A0_MAIN:
		return gen3_clk_get_rate64_pll_mul_reg(priv, &parent, core,
						0, 1, pll_config->extal_div,
						"V3U_MAIN");

	case CLK_TYPE_R8A779A0_PLL1:
		return gen3_clk_get_rate64_pll_mul_reg(priv, &parent, core,
						0, pll_config->pll1_mult,
						pll_config->pll1_div,
						"V3U_PLL1");

	case CLK_TYPE_R8A779A0_PLL2X_3X:
		return gen3_clk_get_rate64_pll_mul_reg(priv, &parent, core,
						core->offset, 0, 0,
						"V3U_PLL2X_3X");

	case CLK_TYPE_R8A779A0_PLL5:
		return gen3_clk_get_rate64_pll_mul_reg(priv, &parent, core,
						0, pll_config->pll5_mult,
						pll_config->pll5_div,
						"V3U_PLL5");

	case CLK_TYPE_FF:
		return gen3_clk_get_rate64_pll_mul_reg(priv, &parent, core,
						0, core->mult, core->div,
						"FIXED");

	case CLK_TYPE_GEN3_MDSEL:
		if (priv->cpg_mode & BIT(core->offset)) {
			parent_id = core->parent & 0xffff;
			div = core->div & 0xffff;
		} else {
			parent_id = core->parent >> 16;
			div = core->div >> 16;
		}
		rate = gen3_clk_get_rate64(&parent) / div;
		debug("%s[%i] MDSEL clk: parent=%i div=%u => rate=%llu\n",
		      __func__, __LINE__,
		      parent_id, div, rate);
		return rate;

	case CLK_TYPE_GEN3_SD:		/* FIXME */
		fallthrough;
	case CLK_TYPE_R8A779A0_SD:
		value = readl(priv->base + core->offset);
		value &= CPG_SD_STP_MASK | CPG_SD_FC_MASK;

		for (i = 0; i < ARRAY_SIZE(cpg_sd_div_table); i++) {
			if (cpg_sd_div_table[i].val != value)
				continue;

			rate = gen3_clk_get_rate64(&parent) /
			       cpg_sd_div_table[i].div;
			debug("%s[%i] SD clk: parent=%i div=%i => rate=%llu\n",
			      __func__, __LINE__,
			      core->parent, cpg_sd_div_table[i].div, rate);

			return rate;
		}

		return -EINVAL;

	case CLK_TYPE_GEN3_RPC:
	case CLK_TYPE_GEN3_RPCD2:
		rate = gen3_clk_get_rate64(&parent);

		value = readl(priv->base + core->offset);

		prediv = (value >> CPG_RPC_PREDIV_OFFSET) &
			 CPG_RPC_PREDIV_MASK;
		if (prediv == 2)
			rate /= 5;
		else if (prediv == 3)
			rate /= 6;
		else
			return -EINVAL;

		postdiv = (value >> CPG_RPC_POSTDIV_OFFSET) &
			  CPG_RPC_POSTDIV_MASK;

		if (postdiv % 2 != 0) {
			rate /= postdiv + 1;

			if (core->type == CLK_TYPE_GEN3_RPCD2)
				rate /= 2;

			debug("%s[%i] RPC clk: parent=%i prediv=%i postdiv=%i => rate=%llu\n",
			      __func__, __LINE__,
			      core->parent, prediv, postdiv, rate);

			return rate;
		}

		return -EINVAL;

	}

	printf("%s[%i] unknown fail\n", __func__, __LINE__);

	return -ENOENT;
}

static ulong gen3_clk_get_rate(struct clk *clk)
{
	return gen3_clk_get_rate64(clk);
}

static ulong gen3_clk_set_rate(struct clk *clk, ulong rate)
{
	/* Force correct SD-IF divider configuration if applicable */
	gen3_clk_setup_sdif_div(clk, rate);
	return gen3_clk_get_rate64(clk);
}

static int gen3_clk_of_xlate(struct clk *clk, struct ofnode_phandle_args *args)
{
	if (args->args_count != 2) {
		debug("Invaild args_count: %d\n", args->args_count);
		return -EINVAL;
	}

	clk->id = (args->args[0] << 16) | args->args[1];

	return 0;
}

const struct clk_ops gen3_clk_ops = {
	.enable		= gen3_clk_enable,
	.disable	= gen3_clk_disable,
	.get_rate	= gen3_clk_get_rate,
	.set_rate	= gen3_clk_set_rate,
	.of_xlate	= gen3_clk_of_xlate,
};

int gen3_clk_probe(struct udevice *dev)
{
	struct gen3_clk_priv *priv = dev_get_priv(dev);
	struct cpg_mssr_info *info =
		(struct cpg_mssr_info *)dev_get_driver_data(dev);
	fdt_addr_t rst_base;
	u32 cpg_mode;
	int ret;

	priv->base = dev_read_addr_ptr(dev);
	if (!priv->base)
		return -EINVAL;

	priv->info = info;
	ret = fdt_node_offset_by_compatible(gd->fdt_blob, -1, info->reset_node);
	if (ret < 0)
		return ret;

	rst_base = fdtdec_get_addr(gd->fdt_blob, ret, "reg");
	if (rst_base == FDT_ADDR_T_NONE)
		return -EINVAL;

	cpg_mode = readl(rst_base + info->reset_modemr_offset);

	priv->cpg_pll_config =
		(struct rcar_gen3_cpg_pll_config *)info->get_pll_config(cpg_mode);
	if (!priv->cpg_pll_config->extal_div)
		return -EINVAL;

	priv->cpg_mode = cpg_mode;

	if (info->reg_layout == CLK_REG_LAYOUT_RCAR_GEN2_AND_GEN3) {
		priv->info->status_regs = mstpsr;
		priv->info->control_regs = smstpcr;
		priv->info->reset_regs = srcr;
		priv->info->reset_clear_regs = srstclr;
	} else if (info->reg_layout == CLK_REG_LAYOUT_RCAR_V3U) {
		priv->info->status_regs = mstpsr_for_v3u;
		priv->info->control_regs = mstpcr_for_v3u;
		priv->info->reset_regs = srcr_for_v3u;
		priv->info->reset_clear_regs = srstclr_for_v3u;
	} else {
		return -EINVAL;
	}

	ret = clk_get_by_name(dev, "extal", &priv->clk_extal);
	if (ret < 0)
		return ret;

	if (info->extalr_node) {
		ret = clk_get_by_name(dev, info->extalr_node, &priv->clk_extalr);
		if (ret < 0)
			return ret;
	}

	return 0;
}

int gen3_clk_remove(struct udevice *dev)
{
	struct gen3_clk_priv *priv = dev_get_priv(dev);

	return renesas_clk_remove(priv->base, priv->info);
}
