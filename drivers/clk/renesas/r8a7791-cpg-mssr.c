// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R8A7791 CPG MSSR driver
 *
 * Copyright (C) 2018 Marek Vasut <marek.vasut@gmail.com>
 *
 * Based on the following driver from Linux kernel:
 * r8a7791 Clock Pulse Generator / Module Standby and Software Reset
 *
 * Copyright (C) 2015-2017 Glider bvba
 *
 * Based on clk-rcar-gen2.c
 *
 * Copyright (C) 2013 Ideas On Board SPRL
 */

#include <common.h>
#include <clk-uclass.h>
#include <dm.h>
#include <linux/bitops.h>

#include <dt-bindings/clock/r8a7791-cpg-mssr.h>

#include "renesas-cpg-mssr.h"
#include "rcar-gen2-cpg.h"

enum clk_ids {
	/* Core Clock Outputs exported to DT */
	LAST_DT_CORE_CLK = R8A7791_CLK_OSC,

	/* External Input Clocks */
	CLK_EXTAL,
	CLK_USB_EXTAL,

	/* Internal Core Clocks */
	CLK_MAIN,
	CLK_PLL0,
	CLK_PLL1,
	CLK_PLL3,
	CLK_PLL1_DIV2,

	/* Module Clocks */
	MOD_CLK_BASE
};

static const struct cpg_core_clk r8a7791_core_clks[] = {
	/* External Clock Inputs */
	DEF_INPUT("extal",     CLK_EXTAL),
	DEF_INPUT("usb_extal", CLK_USB_EXTAL),

	/* Internal Core Clocks */
	DEF_BASE(".main",       CLK_MAIN, CLK_TYPE_GEN2_MAIN, CLK_EXTAL),
	DEF_BASE(".pll0",       CLK_PLL0, CLK_TYPE_GEN2_PLL0, CLK_MAIN),
	DEF_BASE(".pll1",       CLK_PLL1, CLK_TYPE_GEN2_PLL1, CLK_MAIN),
	DEF_BASE(".pll3",       CLK_PLL3, CLK_TYPE_GEN2_PLL3, CLK_MAIN),

	DEF_FIXED(".pll1_div2", CLK_PLL1_DIV2, CLK_PLL1, 2, 1),

	/* Core Clock Outputs */
	DEF_BASE("z",    R8A7791_CLK_Z,    CLK_TYPE_GEN2_Z,    CLK_PLL0),
	DEF_BASE("adsp", R8A7791_CLK_ADSP, CLK_TYPE_GEN2_ADSP, CLK_PLL1),
	DEF_BASE("sdh",  R8A7791_CLK_SDH,  CLK_TYPE_GEN2_SDH,  CLK_PLL1),
	DEF_BASE("sd0",  R8A7791_CLK_SD0,  CLK_TYPE_GEN2_SD0,  CLK_PLL1),
	DEF_BASE("qspi", R8A7791_CLK_QSPI, CLK_TYPE_GEN2_QSPI, CLK_PLL1_DIV2),
	DEF_BASE("rcan", R8A7791_CLK_RCAN, CLK_TYPE_GEN2_RCAN, CLK_USB_EXTAL),

	DEF_FIXED("zg",     R8A7791_CLK_ZG,    CLK_PLL1,          3, 1),
	DEF_FIXED("zx",     R8A7791_CLK_ZX,    CLK_PLL1,          3, 1),
	DEF_FIXED("zs",     R8A7791_CLK_ZS,    CLK_PLL1,          6, 1),
	DEF_FIXED("hp",     R8A7791_CLK_HP,    CLK_PLL1,         12, 1),
	DEF_FIXED("i",      R8A7791_CLK_I,     CLK_PLL1,          2, 1),
	DEF_FIXED("b",      R8A7791_CLK_B,     CLK_PLL1,         12, 1),
	DEF_FIXED("lb",     R8A7791_CLK_LB,    CLK_PLL1,         24, 1),
	DEF_FIXED("p",      R8A7791_CLK_P,     CLK_PLL1,         24, 1),
	DEF_FIXED("cl",     R8A7791_CLK_CL,    CLK_PLL1,         48, 1),
	DEF_FIXED("m2",     R8A7791_CLK_M2,    CLK_PLL1,          8, 1),
	DEF_FIXED("zb3",    R8A7791_CLK_ZB3,   CLK_PLL3,          4, 1),
	DEF_FIXED("zb3d2",  R8A7791_CLK_ZB3D2, CLK_PLL3,          8, 1),
	DEF_FIXED("ddr",    R8A7791_CLK_DDR,   CLK_PLL3,          8, 1),
	DEF_FIXED("mp",     R8A7791_CLK_MP,    CLK_PLL1_DIV2,    15, 1),
	DEF_FIXED("cp",     R8A7791_CLK_CP,    CLK_EXTAL,         2, 1),
	DEF_FIXED("r",      R8A7791_CLK_R,     CLK_PLL1,      49152, 1),
	DEF_FIXED("osc",    R8A7791_CLK_OSC,   CLK_PLL1,      12288, 1),

	DEF_DIV6P1("sd2",   R8A7791_CLK_SD2,   CLK_PLL1_DIV2, 0x078),
	DEF_DIV6P1("sd3",   R8A7791_CLK_SD3,   CLK_PLL1_DIV2, 0x26c),
	DEF_DIV6P1("mmc0",  R8A7791_CLK_MMC0,  CLK_PLL1_DIV2, 0x240),
	DEF_DIV6P1("ssp",   R8A7791_CLK_SSP,   CLK_PLL1_DIV2, 0x248),
	DEF_DIV6P1("ssprs", R8A7791_CLK_SSPRS, CLK_PLL1_DIV2, 0x24c),
};

static const struct mssr_mod_clk r8a7791_mod_clks[] = {
	DEF_MOD("msiof0",		   0,	R8A7791_CLK_MP),
	DEF_MOD("vcp0",			 101,	R8A7791_CLK_ZS),
	DEF_MOD("vpc0",			 103,	R8A7791_CLK_ZS),
	DEF_MOD("jpu",			 106,	R8A7791_CLK_M2),
	DEF_MOD("ssp1",			 109,	R8A7791_CLK_ZS),
	DEF_MOD("tmu1",			 111,	R8A7791_CLK_P),
	DEF_MOD("3dg",			 112,	R8A7791_CLK_ZG),
	DEF_MOD("2d-dmac",		 115,	R8A7791_CLK_ZS),
	DEF_MOD("fdp1-1",		 118,	R8A7791_CLK_ZS),
	DEF_MOD("fdp1-0",		 119,	R8A7791_CLK_ZS),
	DEF_MOD("tmu3",			 121,	R8A7791_CLK_P),
	DEF_MOD("tmu2",			 122,	R8A7791_CLK_P),
	DEF_MOD("cmt0",			 124,	R8A7791_CLK_R),
	DEF_MOD("tmu0",			 125,	R8A7791_CLK_CP),
	DEF_MOD("vsp1du1",		 127,	R8A7791_CLK_ZS),
	DEF_MOD("vsp1du0",		 128,	R8A7791_CLK_ZS),
	DEF_MOD("vsps",			 131,	R8A7791_CLK_ZS),
	DEF_MOD("scifa2",		 202,	R8A7791_CLK_MP),
	DEF_MOD("scifa1",		 203,	R8A7791_CLK_MP),
	DEF_MOD("scifa0",		 204,	R8A7791_CLK_MP),
	DEF_MOD("msiof2",		 205,	R8A7791_CLK_MP),
	DEF_MOD("scifb0",		 206,	R8A7791_CLK_MP),
	DEF_MOD("scifb1",		 207,	R8A7791_CLK_MP),
	DEF_MOD("msiof1",		 208,	R8A7791_CLK_MP),
	DEF_MOD("scifb2",		 216,	R8A7791_CLK_MP),
	DEF_MOD("sys-dmac1",		 218,	R8A7791_CLK_ZS),
	DEF_MOD("sys-dmac0",		 219,	R8A7791_CLK_ZS),
	DEF_MOD("tpu0",			 304,	R8A7791_CLK_CP),
	DEF_MOD("sdhi3",		 311,	R8A7791_CLK_SD3),
	DEF_MOD("sdhi2",		 312,	R8A7791_CLK_SD2),
	DEF_MOD("sdhi0",		 314,	R8A7791_CLK_SD0),
	DEF_MOD("mmcif0",		 315,	R8A7791_CLK_MMC0),
	DEF_MOD("iic0",			 318,	R8A7791_CLK_HP),
	DEF_MOD("pciec",		 319,	R8A7791_CLK_MP),
	DEF_MOD("iic1",			 323,	R8A7791_CLK_HP),
	DEF_MOD("usb3.0",		 328,	R8A7791_CLK_MP),
	DEF_MOD("cmt1",			 329,	R8A7791_CLK_R),
	DEF_MOD("usbhs-dmac0",		 330,	R8A7791_CLK_HP),
	DEF_MOD("usbhs-dmac1",		 331,	R8A7791_CLK_HP),
	DEF_MOD("rwdt",			 402,	R8A7791_CLK_R),
	DEF_MOD("irqc",			 407,	R8A7791_CLK_CP),
	DEF_MOD("intc-sys",		 408,	R8A7791_CLK_ZS),
	DEF_MOD("audio-dmac1",		 501,	R8A7791_CLK_HP),
	DEF_MOD("audio-dmac0",		 502,	R8A7791_CLK_HP),
	DEF_MOD("adsp_mod",		 506,	R8A7791_CLK_ADSP),
	DEF_MOD("thermal",		 522,	CLK_EXTAL),
	DEF_MOD("pwm",			 523,	R8A7791_CLK_P),
	DEF_MOD("usb-ehci",		 703,	R8A7791_CLK_MP),
	DEF_MOD("usbhs",		 704,	R8A7791_CLK_HP),
	DEF_MOD("hscif2",		 713,	R8A7791_CLK_ZS),
	DEF_MOD("scif5",		 714,	R8A7791_CLK_P),
	DEF_MOD("scif4",		 715,	R8A7791_CLK_P),
	DEF_MOD("hscif1",		 716,	R8A7791_CLK_ZS),
	DEF_MOD("hscif0",		 717,	R8A7791_CLK_ZS),
	DEF_MOD("scif3",		 718,	R8A7791_CLK_P),
	DEF_MOD("scif2",		 719,	R8A7791_CLK_P),
	DEF_MOD("scif1",		 720,	R8A7791_CLK_P),
	DEF_MOD("scif0",		 721,	R8A7791_CLK_P),
	DEF_MOD("du1",			 723,	R8A7791_CLK_ZX),
	DEF_MOD("du0",			 724,	R8A7791_CLK_ZX),
	DEF_MOD("lvds0",		 726,	R8A7791_CLK_ZX),
	DEF_MOD("ipmmu-sgx",		 800,	R8A7791_CLK_ZX),
	DEF_MOD("mlb",			 802,	R8A7791_CLK_HP),
	DEF_MOD("vin2",			 809,	R8A7791_CLK_ZG),
	DEF_MOD("vin1",			 810,	R8A7791_CLK_ZG),
	DEF_MOD("vin0",			 811,	R8A7791_CLK_ZG),
	DEF_MOD("etheravb",		 812,	R8A7791_CLK_HP),
	DEF_MOD("ether",		 813,	R8A7791_CLK_P),
	DEF_MOD("sata1",		 814,	R8A7791_CLK_ZS),
	DEF_MOD("sata0",		 815,	R8A7791_CLK_ZS),
	DEF_MOD("gyro-adc",		 901,	R8A7791_CLK_P),
	DEF_MOD("gpio7",		 904,	R8A7791_CLK_CP),
	DEF_MOD("gpio6",		 905,	R8A7791_CLK_CP),
	DEF_MOD("gpio5",		 907,	R8A7791_CLK_CP),
	DEF_MOD("gpio4",		 908,	R8A7791_CLK_CP),
	DEF_MOD("gpio3",		 909,	R8A7791_CLK_CP),
	DEF_MOD("gpio2",		 910,	R8A7791_CLK_CP),
	DEF_MOD("gpio1",		 911,	R8A7791_CLK_CP),
	DEF_MOD("gpio0",		 912,	R8A7791_CLK_CP),
	DEF_MOD("can1",			 915,	R8A7791_CLK_P),
	DEF_MOD("can0",			 916,	R8A7791_CLK_P),
	DEF_MOD("qspi_mod",		 917,	R8A7791_CLK_QSPI),
	DEF_MOD("i2c5",			 925,	R8A7791_CLK_HP),
	DEF_MOD("iicdvfs",		 926,	R8A7791_CLK_CP),
	DEF_MOD("i2c4",			 927,	R8A7791_CLK_HP),
	DEF_MOD("i2c3",			 928,	R8A7791_CLK_HP),
	DEF_MOD("i2c2",			 929,	R8A7791_CLK_HP),
	DEF_MOD("i2c1",			 930,	R8A7791_CLK_HP),
	DEF_MOD("i2c0",			 931,	R8A7791_CLK_HP),
	DEF_MOD("ssi-all",		1005,	R8A7791_CLK_P),
	DEF_MOD("ssi9",			1006,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi8",			1007,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi7",			1008,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi6",			1009,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi5",			1010,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi4",			1011,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi3",			1012,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi2",			1013,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi1",			1014,	MOD_CLK_ID(1005)),
	DEF_MOD("ssi0",			1015,	MOD_CLK_ID(1005)),
	DEF_MOD("scu-all",		1017,	R8A7791_CLK_P),
	DEF_MOD("scu-dvc1",		1018,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-dvc0",		1019,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-ctu1-mix1",	1020,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-ctu0-mix0",	1021,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src9",		1022,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src8",		1023,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src7",		1024,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src6",		1025,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src5",		1026,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src4",		1027,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src3",		1028,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src2",		1029,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src1",		1030,	MOD_CLK_ID(1017)),
	DEF_MOD("scu-src0",		1031,	MOD_CLK_ID(1017)),
	DEF_MOD("scifa3",		1106,	R8A7791_CLK_MP),
	DEF_MOD("scifa4",		1107,	R8A7791_CLK_MP),
	DEF_MOD("scifa5",		1108,	R8A7791_CLK_MP),
};

/*
 * CPG Clock Data
 */

/*
 *   MD		EXTAL		PLL0	PLL1	PLL3
 * 14 13 19	(MHz)		*1	*1
 *---------------------------------------------------
 * 0  0  0	15		x172/2	x208/2	x106
 * 0  0  1	15		x172/2	x208/2	x88
 * 0  1  0	20		x130/2	x156/2	x80
 * 0  1  1	20		x130/2	x156/2	x66
 * 1  0  0	26 / 2		x200/2	x240/2	x122
 * 1  0  1	26 / 2		x200/2	x240/2	x102
 * 1  1  0	30 / 2		x172/2	x208/2	x106
 * 1  1  1	30 / 2		x172/2	x208/2	x88
 *
 * *1 :	Table 7.5a indicates VCO output (PLLx = VCO/2)
 */
#define CPG_PLL_CONFIG_INDEX(md)	((((md) & BIT(14)) >> 12) | \
					 (((md) & BIT(13)) >> 12) | \
					 (((md) & BIT(19)) >> 19))
static const struct rcar_gen2_cpg_pll_config cpg_pll_configs[8] = {
	{ 1, 208, 106 }, { 1, 208,  88 }, { 1, 156,  80 }, { 1, 156,  66 },
	{ 2, 240, 122 }, { 2, 240, 102 }, { 2, 208, 106 }, { 2, 208,  88 },
};

static const struct mstp_stop_table r8a7791_mstp_table[] = {
	{ 0x00640801, 0x400000, 0x00640801, 0x0 },
	{ 0x9B6C9B5A, 0x0, 0x9B6C9B5A, 0x0 },
	{ 0x100D21FC, 0x2000, 0x100D21FC, 0x0 },
	{ 0xF08CD810, 0x0, 0xF08CD810, 0x0 },
	{ 0x800001C4, 0x180, 0x800001C4, 0x0 },
	{ 0x44C00046, 0x0, 0x44C00046, 0x0 },
	{ 0x0, 0x0, 0x0, 0x0 },	/* SMSTP6 is not present on Gen2 */
	{ 0x05BFE618, 0x200000, 0x05BFE618, 0x0 },
	{ 0x40C0FE85, 0x0, 0x40C0FE85, 0x0 },
	{ 0xFF979FFF, 0x0, 0xFF979FFF, 0x0 },
	{ 0xFFFEFFE0, 0x0, 0xFFFEFFE0, 0x0 },
	{ 0x000001C0, 0x0, 0x000001C0, 0x0 },
};

static const void *r8a7791_get_pll_config(const u32 cpg_mode)
{
	return &cpg_pll_configs[CPG_PLL_CONFIG_INDEX(cpg_mode)];
}

static const struct cpg_mssr_info r8a7791_cpg_mssr_info = {
	.core_clk		= r8a7791_core_clks,
	.core_clk_size		= ARRAY_SIZE(r8a7791_core_clks),
	.mod_clk		= r8a7791_mod_clks,
	.mod_clk_size		= ARRAY_SIZE(r8a7791_mod_clks),
	.mstp_table		= r8a7791_mstp_table,
	.mstp_table_size	= ARRAY_SIZE(r8a7791_mstp_table),
	.reset_node		= "renesas,r8a7791-rst",
	.reset_modemr_offset	= CPG_RST_MODEMR,
	.extal_usb_node		= "usb_extal",
	.mod_clk_base		= MOD_CLK_BASE,
	.clk_extal_id		= CLK_EXTAL,
	.clk_extal_usb_id	= CLK_USB_EXTAL,
	.pll0_div		= 2,
	.get_pll_config		= r8a7791_get_pll_config,
};

static const struct udevice_id r8a7791_clk_ids[] = {
	{
		.compatible	= "renesas,r8a7791-cpg-mssr",
		.data		= (ulong)&r8a7791_cpg_mssr_info
	},
	{
		.compatible	= "renesas,r8a7793-cpg-mssr",
		.data		= (ulong)&r8a7791_cpg_mssr_info
	},
	{ }
};

U_BOOT_DRIVER(clk_r8a7791) = {
	.name		= "clk_r8a7791",
	.id		= UCLASS_CLK,
	.of_match	= r8a7791_clk_ids,
	.priv_auto_alloc_size = sizeof(struct gen2_clk_priv),
	.ops		= &gen2_clk_ops,
	.probe		= gen2_clk_probe,
	.remove		= gen2_clk_remove,
};
