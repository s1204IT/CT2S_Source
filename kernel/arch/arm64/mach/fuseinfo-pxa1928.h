/*
 * arch/arm64/mach/fuseinfo-pxa1928.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __MACH_FUSEINFO_PXA1928_H__
#define __MACH_FUSEINFO_PXA1928_H__

extern int smc_get_fuse_info(u64 function_id, void *arg);
extern void read_fuseinfo(void);
extern unsigned int get_chipprofile(void);
extern unsigned int get_iddq_105(void);
extern unsigned int get_iddq_130(void);

#endif
