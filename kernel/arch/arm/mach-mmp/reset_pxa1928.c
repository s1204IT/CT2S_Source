/*
 * linux/arch/arm/mach-mmp/reset.c
 *
 * Author:	Neil Zhang <zhangwm@marvell.com>
 * Copyright:	(C) 2012 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/cputype.h>

#include <asm/io.h>
#include <asm/cacheflush.h>
#include <asm/mach/map.h>
#include <asm/mcpm.h>

#include <mach/addr-map.h>

#include "reset_pxa1928.h"

#define PMU_CC2_AP			APMU_REG(0x0100)
#define CIU_CA9_WARM_RESET_VECTOR	CIU_REG(0x00d8)
#define SW_SCRATCH			CIU_REG(0x24)

/*
 * This function is called from boot_secondary to bootup the secondary cpu.
 */
void mmp_cpu_power_up(unsigned int cpu, unsigned int cluster)
{

	BUG_ON(cpu == 0);
}

void mmp_set_entry_vector(u32 cpu, u32 addr)
{
	BUG_ON(cpu >= CONFIG_NR_CPUS);

	mmp_entry_vectors[cpu] = addr;
	smp_wmb();
	__cpuc_flush_dcache_area((void *)&mmp_entry_vectors[cpu],
				sizeof(mmp_entry_vectors[cpu]));
	outer_clean_range(__pa(&mmp_entry_vectors[cpu]),
				__pa(&mmp_entry_vectors[cpu + 1]));
}

void __init mmp_entry_vector_init(void)
{
	/* We will reset from DDR directly by default */
	writel(__pa(mcpm_entry_point), SW_SCRATCH);
}
