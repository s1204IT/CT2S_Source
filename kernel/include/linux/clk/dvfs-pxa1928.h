/*
 *  linux/arch/arm/mach-mmp/include/mach/dvfs-pxa1928.h
 *
 *  Author: Xiaolong Ye yexl@marvell.com
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef __DVFS_PXA1928_H__
#define __DVFS_PXA1928_H__

#define PXA1928_DVC_LEVEL_NUM	16
enum {
	CORE = 0,
	AXI,
	DDR,
	GC2D,
	GC3D,
	VPUDE,
	VPUEN,
	SDH1,
	SDH2,
	SDH3,
	PXA1928_DVC_FACTOR_NUM,
};

int get_svc_freqs_cmb_tbl(unsigned long (**svc_tbl)[][PXA1928_DVC_LEVEL_NUM + 1]);
long get_dvc_freq(unsigned int factor, unsigned int level);
#endif /*__DVFS_PXA1928_H__*/
