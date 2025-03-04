#include <common.h>
#include <cpu_func.h>
#include <dm.h>
#include <errno.h>
#include <malloc.h>
#include <xen.h>

#include <asm/io.h>
#include <asm/armv8/mmu.h>
#include <asm/xen.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/system.h>

#include <linux/compiler.h>

#include <xen/gnttab.h>
#include <xen/hvm.h>

#include <mach/rmobile.h>

DECLARE_GLOBAL_DATA_PTR;

/* TODO: fix this HACK */
u32 rmobile_get_cpu_type(void) {
	return RMOBILE_CPU_TYPE_R8A7795;
}

int board_init(void)
{
	return 0;
}

int board_early_init_f(void)
{
	return 0;
}

int arch_misc_init(void)
{
	return 0;
}

/*
 * Use fdt provided by Xen: according to
 * https://www.kernel.org/doc/Documentation/arm64/booting.txt
 * x0 is the physical address of the device tree blob (dtb) in system RAM.
 * This is stored in rom_pointer during low level init.
 */
void *board_fdt_blob_setup(void)
{
	if (fdt_magic(rom_pointer[0]) != FDT_MAGIC)
		return NULL;
	return (void *)rom_pointer[0];
}

#define MAX_MEM_MAP_REGIONS 7
static struct mm_region xen_mem_map[MAX_MEM_MAP_REGIONS];
struct mm_region *mem_map = xen_mem_map;

static int get_next_memory_node(const void *blob, int mem)
{
	do {
		mem = fdt_node_offset_by_prop_value(blob, mem,
						    "device_type", "memory", 7);
	} while (!fdtdec_get_is_enabled(blob, mem));

	return mem;
}

static int setup_mem_map(void)
{
	int i = 0, ret, mem, reg = 0;
	struct fdt_resource res;
	const void *blob = gd->fdt_blob;
	u64 gfn;

	phys_addr_t gnttab_base;
	phys_size_t gnttab_sz;

	memset(xen_mem_map, 0, sizeof(xen_mem_map));
	/*
	 * Add "magic" region which is used by Xen to provide some essentials
	 * for the guest: we need console and xenstore.
	 */
	ret = hvm_get_parameter_maintain_dcache(HVM_PARAM_CONSOLE_PFN, &gfn);
	if (ret < 0) {
		printf("%s: Can't get HVM_PARAM_CONSOLE_PFN, ret %d\n",
		       __func__, ret);
		return -EINVAL;
	}

	xen_mem_map[i].virt = PFN_PHYS(gfn);
	xen_mem_map[i].phys = PFN_PHYS(gfn);
	xen_mem_map[i].size = PAGE_SIZE;
	xen_mem_map[i].attrs = (PTE_BLOCK_MEMTYPE(MT_NORMAL) |
				PTE_BLOCK_INNER_SHARE);
	i++;

	ret = hvm_get_parameter_maintain_dcache(HVM_PARAM_STORE_PFN, &gfn);
	if (ret < 0) {
		printf("%s: Can't get HVM_PARAM_STORE_PFN, ret %d\n",
		       __func__, ret);
		return -EINVAL;
	}

	xen_mem_map[i].virt = PFN_PHYS(gfn);
	xen_mem_map[i].phys = PFN_PHYS(gfn);
	xen_mem_map[i].size = PAGE_SIZE;
	xen_mem_map[i].attrs = (PTE_BLOCK_MEMTYPE(MT_NORMAL) |
				PTE_BLOCK_INNER_SHARE);
	i++;

	/* TODO: fix gnttab region in Zephyr Dom0 */
	/* Get Xen's suggested physical page assignments for the grant table. */
	get_gnttab_base(&gnttab_base, &gnttab_sz);

	xen_mem_map[i].virt = gnttab_base;
	xen_mem_map[i].phys = gnttab_base;
	xen_mem_map[i].size = gnttab_sz;
	xen_mem_map[i].attrs = (PTE_BLOCK_MEMTYPE(MT_NORMAL) |
				PTE_BLOCK_INNER_SHARE);
	i++;

	xen_mem_map[i].virt = 0xe6000000;
	xen_mem_map[i].phys = 0xe6000000;
	xen_mem_map[i].size = 0x1000000;
	xen_mem_map[i].attrs = PTE_BLOCK_MEMTYPE(MT_DEVICE_NGNRNE) | PTE_BLOCK_NON_SHARE | PTE_BLOCK_PXN | PTE_BLOCK_UXN;
	i++;

	mem = get_next_memory_node(blob, -1);
	if (mem < 0) {
		printf("%s: Missing /memory node\n", __func__);
		return -EINVAL;
	}

	for (; i < MAX_MEM_MAP_REGIONS; i++) {
		ret = fdt_get_resource(blob, mem, "reg", reg++, &res);
		if (ret == -FDT_ERR_NOTFOUND) {
			reg = 0;
			mem = get_next_memory_node(blob, mem);
			if (mem == -FDT_ERR_NOTFOUND)
				break;

			ret = fdt_get_resource(blob, mem, "reg", reg++, &res);
			if (ret == -FDT_ERR_NOTFOUND)
				break;
		}
		if (ret != 0) {
			printf("No reg property for memory node\n");
			return -EINVAL;
		}

		xen_mem_map[i].virt = (phys_addr_t)res.start;
		xen_mem_map[i].phys = (phys_addr_t)res.start;
		xen_mem_map[i].size = (phys_size_t)(res.end - res.start + 1);
		xen_mem_map[i].attrs = (PTE_BLOCK_MEMTYPE(MT_NORMAL) |
					PTE_BLOCK_INNER_SHARE);
	}
	return 0;
}

void enable_caches(void)
{
	/* Re-setup the memory map as BSS gets cleared after relocation. */
	setup_mem_map();
	icache_enable();
	dcache_enable();
}

/* Read memory settings from the Xen provided device tree. */
int dram_init(void)
{
	int ret;

	ret = fdtdec_setup_mem_size_base();
	if (ret < 0)
		return ret;
	/* Setup memory map, so MMU page table size can be estimated. */
	return setup_mem_map();
}

int dram_init_banksize(void)
{
	return fdtdec_setup_memory_banksize();
}

/*
 * Board specific reset that is system reset.
 */
void reset_cpu(ulong addr)
{
}

int ft_system_setup(void *blob, struct bd_info *bd)
{
	return 0;
}

int ft_board_setup(void *blob, struct bd_info *bd)
{
	return 0;
}

int print_cpuinfo(void)
{
	printf("Xen virtual CPU\n");
	return 0;
}

void board_cleanup_before_linux(void)
{
	xen_fini();
}


