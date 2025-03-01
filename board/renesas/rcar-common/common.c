// SPDX-License-Identifier: GPL-2.0
/*
 * board/renesas/rcar-common/common.c
 *
 * Copyright (C) 2013 Renesas Electronics Corporation
 * Copyright (C) 2013 Nobuhiro Iwamatsu <nobuhiro.iwamatsu.yj@renesas.com>
 * Copyright (C) 2015 Nobuhiro Iwamatsu <iwamatsu@nigauri.org>
 */

#include <common.h>
#include <dm.h>
#include <init.h>
#include <dm/uclass-internal.h>
#include <asm/arch/rmobile.h>
#include <linux/libfdt.h>
#include <i2c.h>

#ifdef CONFIG_RCAR_GEN3

DECLARE_GLOBAL_DATA_PTR;

/*
 * If the firmware passed a device tree use it for U-Boot DRAM setup
 * and board identification
 */
extern u64 rcar_atf_boot_args[];

#ifdef CONFIG_MULTI_DTB_FIT
bool is_rcar_gen3_board(const char *board_name)
{
	void *atf_fdt_blob = (void *)(rcar_atf_boot_args[1]);
	bool ret = false;

	if ((fdt_magic(atf_fdt_blob) == FDT_MAGIC) &&
	    (fdt_node_check_compatible(atf_fdt_blob, 0, board_name) == 0))
		ret = true;

	return ret;
}
#endif

int fdtdec_board_setup(const void *fdt_blob)
{
	void *atf_fdt_blob = (void *)(rcar_atf_boot_args[1]);

	if (fdt_magic(atf_fdt_blob) == FDT_MAGIC)
		fdt_overlay_apply_node((void *)fdt_blob, 0, atf_fdt_blob, 0);

	return 0;
}

int dram_init(void)
{
	return fdtdec_setup_mem_size_base();
}

int dram_init_banksize(void)
{
	fdtdec_setup_memory_banksize();

	return 0;
}

#if defined(CONFIG_OF_BOARD_SETUP)
static int is_mem_overlap(void *blob, int first_mem_node, int curr_mem_node)
{
	struct fdt_resource first_mem_res, curr_mem_res;
	int curr_mem_reg, first_mem_reg = 0;
	int ret;

	for (;;) {
		ret = fdt_get_resource(blob, first_mem_node, "reg",
				       first_mem_reg++, &first_mem_res);
		if (ret) /* No more entries, no overlap found */
			return 0;

		curr_mem_reg = 0;
		for (;;) {
			ret = fdt_get_resource(blob, curr_mem_node, "reg",
					       curr_mem_reg++, &curr_mem_res);
			if (ret) /* No more entries, check next tuple */
				break;

			if (curr_mem_res.end < first_mem_res.start)
				continue;

			if (curr_mem_res.start >= first_mem_res.end)
				continue;

			pr_debug("Overlap found: 0x%llx..0x%llx / 0x%llx..0x%llx\n",
				 first_mem_res.start, first_mem_res.end,
				 curr_mem_res.start, curr_mem_res.end);

			return 1;
		}
	}

	return 0;
}

int ft_board_setup(void *blob, struct bd_info *bd)
{
	/*
	 * Scrub duplicate /memory@* node entries here. Some R-Car DTs might
	 * contain multiple /memory@* nodes, however fdt_fixup_memory_banks()
	 * either generates single /memory node or updates the first /memory
	 * node. Any remaining memory nodes are thus potential duplicates.
	 *
	 * However, it is not possible to delete all the memory nodes right
	 * away, since some of those might not be DRAM memory nodes, but some
	 * sort of other memory. Thus, delete only the memory nodes which are
	 * in the R-Car3 DBSC ranges.
	 */
	int mem = 0, first_mem_node = 0;

	for (;;) {
		mem = fdt_node_offset_by_prop_value(blob, mem,
						    "device_type", "memory", 7);
		if (mem < 0)
			break;
		if (!fdtdec_get_is_enabled(blob, mem))
			continue;

		/* First memory node, patched by U-Boot */
		if (!first_mem_node) {
			first_mem_node = mem;
			continue;
		}

		/* Check the remaining nodes and delete duplicates */
		if (!is_mem_overlap(blob, first_mem_node, mem))
			continue;

		/* Delete duplicate node, start again */
		fdt_del_node(blob, mem);
		first_mem_node = 0;
		mem = 0;
	}

	return 0;
}
#endif

#define BOARD_CODE_MASK		0xF8
#define BOARD_CODE_SHIFT	0x03
#define BOARD_ID_UNKNOWN	0xFF

static u8 rcar_get_board_type_by_chip(void)
{
	switch (rmobile_get_cpu_type()) {
	case RMOBILE_CPU_TYPE_R8A7795:
	case RMOBILE_CPU_TYPE_R8A7796:
	case RMOBILE_CPU_TYPE_R8A77965:
		return BOARD_TYPE_SALVATOR_X << BOARD_CODE_SHIFT;
	case RMOBILE_CPU_TYPE_R8A77970:
		return BOARD_TYPE_EAGLE << BOARD_CODE_SHIFT;
	case RMOBILE_CPU_TYPE_R8A77980:
		return BOARD_TYPE_CONDOR << BOARD_CODE_SHIFT;
	case RMOBILE_CPU_TYPE_R8A77990:
		return BOARD_TYPE_EBISU << BOARD_CODE_SHIFT;
	case RMOBILE_CPU_TYPE_R8A77995:
		return BOARD_TYPE_DRAAK << BOARD_CODE_SHIFT;
	default:
		return BOARD_TYPE_UNKNOWN << BOARD_CODE_SHIFT;
	}
}

int rcar_get_board_type(int busnum, int chip_addr, u32 *type)
{
	struct udevice *dev;
	u8 board_id = BOARD_ID_UNKNOWN;
	int ret;

	if (busnum == -1)
		goto get_type;

	ret = i2c_get_chip_for_busnum(busnum, chip_addr, 1, &dev);
	if (ret)
		return ret;

	ret = dm_i2c_read(dev, 0x70, &board_id, 1);
	if (ret)
		return ret;

get_type:
	if (board_id == BOARD_ID_UNKNOWN)
		board_id = rcar_get_board_type_by_chip();

	*type = ((u32)board_id & BOARD_CODE_MASK) >> BOARD_CODE_SHIFT;

	return ret;
}
#endif

void board_add_ram_info(int use_default)
{
	int i;

	printf("\nRAM Configuration:\n");
	for (i = 0; i < CONFIG_NR_DRAM_BANKS; i++) {
		if (!gd->bd->bi_dram[i].size)
			break;
		printf("Bank #%d: 0x%09llx - 0x%09llx, ", i,
		       (unsigned long long)(gd->bd->bi_dram[i].start),
		       (unsigned long long)(gd->bd->bi_dram[i].start
		       + gd->bd->bi_dram[i].size - 1));
		print_size(gd->bd->bi_dram[i].size, "\n");
	}
}
