/*
 * linux/arch/arm/mach-mmp/include/mach/pxa1928_ddrtable.h
 *
 * Copyright:	(C) 2013 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */


#ifndef __PXA1928_DDRTABLE_H__
#define __PXA1928_DDRTABLE_H__

struct ddr_regtbl {
	u32 reg;
	u32 b2c;
	u32 b2s;
};

struct ddr_regtbl_info {
	u32 array_size;
	u32 timing_cnt;
};

enum ddr_regtbl_type {
	DDR_TYPE_MT42L256M = 0,
	DDR_TYPE_LPDDR3 = 1,
	DDR_TYPE_INVALID = 255,
};

extern struct ddr_regtbl *mt42l256m32d2lg18_array[];
extern struct ddr_regtbl *lpddr3_array[];

extern void pxa1928_ddrhwt_select_array(u32 ind);
extern int pxa1928_ddrhwt_get_freqnr(void);
extern int pxa1928_ddrhwt_get_timingcnt(void);
extern void pxa1928_ddrhwt_lpddr2_h2l(u32 *dmcu, struct ddr_regtbl *newtiming,
				u32 timing_cnt, u32 table_index);
extern void pxa1928_ddrhwt_lpddr2_l2h(u32 *dmcu, struct ddr_regtbl *newtiming,
				u32 timing_cnt, u32 table_index);
extern void pxa1928_register_table_lpddr2_dll_calibration(u32 *dmcu);
extern void pxa1928_enable_lpm_ddrdll_recalibration(void);
extern void pxa1928_ddrhwt_lpddr2_h2l_lowfreq(u32 *dmcu,
		struct ddr_regtbl *newtiming, u32 timing_cnt, u32 table_index);
extern void pxa1928_ddrhwt_lpddr2_l2h_lowfreq(u32 *dmcu,
		struct ddr_regtbl *newtiming, u32 timing_cnt, u32 table_index);
#endif
