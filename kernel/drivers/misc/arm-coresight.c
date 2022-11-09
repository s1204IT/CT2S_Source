/*
 * linux/drivers/misc/arm-coresight.c
 *
 * Author:	Neil Zhang <zhangwm@marvell.com>
 * Copyright:	(C) 2012 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/cpu.h>
#include <linux/clk.h>
#include <linux/init.h>
#include <linux/cpu_pm.h>
#include <linux/notifier.h>
#include <linux/arm-coresight.h>

static struct clk *dbgclk;

static inline int enable_debug_clock(u32 cpu)
{
	if (!dbgclk) {
		pr_emerg("No DBGCLK is found!\n");
		return -1;
	}

	if (clk_enable(dbgclk)) {
		pr_emerg("No DBGCLK is found!\n");
		return -1;
	}

	return 0;
}

void coresight_dump_pcsr(u32 cpu)
{
	if (enable_debug_clock(cpu))
		return;

	arch_enable_access(cpu);
	arch_dump_pcsr(cpu);
}

void coresight_trigger_panic(int cpu)
{
	if (enable_debug_clock(cpu))
		return;

	arch_enable_access(cpu);
	arch_dump_pcsr(cpu);

	/* Halt the dest cpu */
	pr_emerg("Going to halt cpu%d\n", cpu);
	if (arch_halt_cpu(cpu)) {
		panic("Cannot halt cpu%d, Let's panic directly\n", cpu);
		return;
	}

	pr_emerg("Going to insert inst on cpu%d\n", cpu);
	arch_insert_inst(cpu);

	/* Restart target cpu */
	pr_emerg("Going to restart cpu%d\n", cpu);
	arch_restart_cpu(cpu);
}

static int coresight_core_notifier(struct notifier_block *nfb,
				      unsigned long action, void *hcpu)
{
	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_STARTING:
		arch_restore_coreinfo();
		return NOTIFY_OK;

	case CPU_DYING:
		arch_save_coreinfo();
		return NOTIFY_OK;

	default:
		return NOTIFY_DONE;
	}
}

static int coresight_notifier(struct notifier_block *self,
				unsigned long cmd, void *v)
{
	switch (cmd) {
	case CPU_PM_ENTER:
		arch_save_coreinfo();
		break;
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		arch_restore_coreinfo();
		break;
	case CPU_CLUSTER_PM_ENTER:
		arch_save_mpinfo();
		break;
	case CPU_CLUSTER_PM_ENTER_FAILED:
	case CPU_CLUSTER_PM_EXIT:
		arch_restore_mpinfo();
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block coresight_notifier_block = {
	.notifier_call = coresight_notifier,
};

static int __init arm_coresight_init(void)
{
	dbgclk = clk_get(NULL, "DBGCLK");
	if (IS_ERR(dbgclk)) {
		pr_warn("No DBGCLK is defined...\n");
		dbgclk = NULL;
	}

	if (dbgclk)
		clk_prepare(dbgclk);

	arch_coresight_init();

	cpu_notifier(coresight_core_notifier, 0);
	cpu_pm_register_notifier(&coresight_notifier_block);

	return 0;
}
arch_initcall(arm_coresight_init);
