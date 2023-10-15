#ifndef __RCAR4_XEN_H
#define __RCAR4_XEN_H

#include "rcar-gen4-common.h"

/* When booting over Xen we should not use this values */
#undef DRAM_RSV_SIZE
#undef CFG_SYS_SDRAM_BASE
#undef CFG_SYS_SDRAM_SIZE
#undef CFG_MAX_MEM_MAPPED

#define CONFIG_LNX_KRNL_IMG_TEXT_OFFSET_BASE    CONFIG_SYS_LOAD_ADDR

#endif /* __RCAR4_XEN_H */
