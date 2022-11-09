/*
 * arch/arm64/mach/mmpx-dt.h
 *
 * Author:	Dongjiu Geng <djgeng@marvell.com>
 * Copyright:	(C) 2013 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __MACH_MMPX_DT_H__
#define __MACH_MMPX_DT_H__

extern void buck2_sleepmode_control_for_wifi(int on);
extern void __init pxa1928_clk_init(unsigned long, unsigned long,
						unsigned long, unsigned long);
extern void __init pxa1908_reserve(void);
extern void __init pxa1908_arch_timer_init(void);
extern void __init pxa1908_dt_init_machine(void);
extern void __init helan2_clk_init(void);
extern void __init init_pxa1928_sysset(void);
#ifdef CONFIG_MRVL_LOG
extern void __init pxa_reserve_logmem(void);
#endif

extern void restore_smmu(void);
#endif
