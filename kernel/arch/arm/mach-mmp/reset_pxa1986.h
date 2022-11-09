/*
 * linux/arch/arm/mach-mmp/include/mach/reset-pxa1986.h
 *
 * Copyright:	(C) 2012 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __RESET_PXA1986_H__
#define __RESET_PXA1986_H__

#define CORE_RST        (1 << 0)

#ifdef CONFIG_SMP
extern u32 pm_reserve_pa;
extern u32 reset_handler_pa;
extern u32 secondary_cpu_handler;
extern void pxa1986_secondary_startup(void);
extern void pxa1986_hotplug_handler(void);
extern void pxa1986_set_reset_handler(u32 fn, u32 cpu);
extern void pxa1986_cpu_reset_entry(void);
extern void pxa1986_cpu_reset(u32 cpu);

/* pxa1986_set_bxaddr() sets the jump address from RH for a specific physical
	cpu (cpu_logical_map(cpu) can be used to derive this.
	After this use gic_raise_softirq() to kick the CPU out of WFI.
	Set this only for the CPU you want to boot,
	because gic_raise_softirq() for a CPU
	that did not boot yet and did not call gic_secondary_init() maps
	to "all" in gic.c, so all CPU's will be kicked and will check
	their bxaddr[].
*/
extern void pxa1986_set_bxaddr(u32 physcpu, u32 val);
/*TODO: FIXME: need to remove this call to gic driver*/
extern void gic_raise_softirq(const struct cpumask *mask, unsigned int irq);
void pxa1986_secondary_init(u32 cpu);
#endif

#ifdef CONFIG_PM
extern u32 l2sram_shutdown;
extern u32 l2x0_regs_phys;
extern u32 l2x0_saved_regs_phys_addr;
extern void pxa1986_cpu_resume_handler(void);
#endif

#endif /* __RESET_PXA1986_H__ */
