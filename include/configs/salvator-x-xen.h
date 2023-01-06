#ifndef __SALVATOR_X_XEN_H
#define __SALVATOR_X_XEN_H

#ifndef __ASSEMBLY__
#include <linux/types.h>
#endif

#define CONFIG_EXTRA_ENV_SETTINGS

#undef CONFIG_NR_DRAM_BANKS
#undef CONFIG_SYS_SDRAM_BASE

#define CONFIG_NR_DRAM_BANKS          1

/*
 * This can be any arbitrary address as we are using PIE, but
 * please note, that CONFIG_SYS_TEXT_BASE must match the below.
 */
#define CONFIG_SYS_LOAD_ADDR                    0x40080000
#define CONFIG_SYS_INIT_SP_ADDR                 0x42FFC000
#define CONFIG_LNX_KRNL_IMG_TEXT_OFFSET_BASE    CONFIG_SYS_LOAD_ADDR

/* Size of malloc() pool */
#define CONFIG_SYS_MALLOC_LEN         (1 * 1024 * 1024)

/* Monitor Command Prompt */
#define CONFIG_SYS_PROMPT_HUSH_PS2    "> "
#define CONFIG_SYS_CBSIZE             1024
#define CONFIG_SYS_MAXARGS            64
#define CONFIG_SYS_BARGSIZE           CONFIG_SYS_CBSIZE
#define CONFIG_SYS_PBSIZE             (CONFIG_SYS_CBSIZE + \
				      sizeof(CONFIG_SYS_PROMPT) + 16)

#define CONFIG_OF_SYSTEM_SETUP

#undef CONFIG_EXTRA_ENV_SETTINGS
#define CONFIG_EXTRA_ENV_SETTINGS	\
	"ethaddr=2E:09:0A:00:A0:4B\0 ipaddr=192.168.1.20\0 serverip=192.168.1.10\0" \
	"fdt_high=0xffffffffffffffff\0" \
	"bootargs_nfs=ignore_loglevel rw ip=192.168.1.20 rootfstype=nfs root=/dev/nfs " \
	"nfsroot=192.168.1.10:/srv/domd_weston_demo,nfsvers=3,vers=3,tcp console=ttySC0,115200\0" \
	"bootargs_mmc=ignore_loglevel rw ip=192.168.1.20 root=/dev/mmcblk1p2 rootwait console=ttySC0,115200\0"\
	"bootcmd_nfs=setenv bootargs ${bootargs_nfs}; tftp 0x42080000 domd_dumped.dtb;"\
	"tftp 0x43000000 domd_image; booti 0x43000000 - 0x42080000\0"\
	"bootcmd_mmc=setenv bootargs ${bootargs_mmc}; ext4load mmc 0:1 0x42080000 domd_dumped.dtb;"\
	"ext4load mmc 0:1 0x43000000 domd_image; booti 0x43000000 - 0x42080000\0"\
	"bootcmd=run bootcmd_mmc\0"

/* Ethernet RAVB */
#define CONFIG_BITBANGMII_MULTI
#define PHY_ANEG_TIMEOUT       20000

#endif /* __SALVATOR_X_XEN_H */
