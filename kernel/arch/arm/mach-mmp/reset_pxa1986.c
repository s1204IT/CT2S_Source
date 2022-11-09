/*
 * linux/arch/arm/mach-mmp/reset_pxa1986.c
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
#include <asm/smp_plat.h>

#include <asm/io.h>
#include <asm/cacheflush.h>
#include <asm/mach/map.h>

#include <mach/regs-ciu.h>
#include <mach/regs-apmu.h>
#include "reset_pxa1986.h"
#include <mach/regs-coresight.h>

#ifdef CONFIG_SMP
static unsigned *bxaddr; /* Mapped into the RH BX_ADDR array */
void pxa1986_set_bxaddr(u32 physcpu, u32 val)
{
	int index = MPIDR_AFFINITY_LEVEL(physcpu, 0)
			+ 2*MPIDR_AFFINITY_LEVEL(physcpu, 1);
	bxaddr[index] = val;
	smp_wmb();
}

/*
 * This function is called from boot_secondary to bootup the secondary cpu.
 * It maybe called when system bootup or add a plugged cpu into system.
 */
void pxa1986_cpu_reset(u32 cpu)
{
	pxa1986_set_bxaddr(cpu_logical_map(cpu),
			__pa(pxa1986_secondary_startup));
	gic_raise_softirq(cpumask_of(cpu), 1);
}


/* This function is called from platform_secondary_init in platform.c */
void pxa1986_secondary_init(u32 cpu)
{
#ifdef CONFIG_CORESIGHT_SUPPORT
	static int bootup = 1;

	/* restore the coresight registers when hotplug in */
	if (!bootup)
		v7_coresight_restore();
	else
		bootup = 0;
#endif

#ifdef CONFIG_PM
	/* TODO: Set resume handler as the default handler when hotplugin */
#endif
}

static int __init pxa1986_cpu_reset_handler_init(void)
{
/* SRAM is accessible by secure only (once security is enabled),
	however we trap accesses to BX_ADDR block in HYP and invoke
	smc() there to read or write */
	bxaddr = ioremap_nocache(0xFFFF0080, 0x10);
	BUG_ON(bxaddr == NULL);
	return 0;
}
early_initcall(pxa1986_cpu_reset_handler_init);
#endif
