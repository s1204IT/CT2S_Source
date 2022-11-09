/*
 *  watchdog reset for pxa1L88 to set the cpu/ddr/gc/vpu to min freq.
 *  In order to make sure the voltage is at low level before next reboot.
 *  Copyright (c) 2014 Marvell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pm_qos.h>
#include <linux/slab.h>
#include <linux/reboot.h>
#include <linux/features.h>
#include <linux/clk.h>
#include <linux/delay.h>


#define MIN_FREQ	0x1

static struct pm_qos_request watchdog_reset_cpu_qos_max = {
	.name = "watchdog_reset",
};

static struct pm_qos_request watchdog_reset_ddr_qos_max = {
	.name = "watchdog_reset",
};

static struct pm_qos_request watchdog_reset_gc3d_qos_max = {
	.name = "watchdog_reset",
};

static struct pm_qos_request watchdog_reset_gc2d_qos_max = {
	.name = "watchdog_reset",
};

static struct pm_qos_request watchdog_reset_gcsh_qos_max = {
	.name = "watchdog_reset",
};

static struct pm_qos_request watchdog_reset_vpu_qos_max = {
	.name = "watchdog_reset",
};

static void watchdog_reset_setfreq(void)
{
	/* watchdog_reset cpu to min frequency */
	pm_qos_update_request(&watchdog_reset_cpu_qos_max,
		MIN_FREQ);

	/* watchdog_reset ddr to min frequency */
	pm_qos_update_request(&watchdog_reset_ddr_qos_max,
			MIN_FREQ);
	/* watchdog_reset gpu0(3D) to min frequency */
	pm_qos_update_request(&watchdog_reset_gc3d_qos_max,
			MIN_FREQ);

	/* watchdog_reset gpu1(2D) to min frequency */
	pm_qos_update_request(&watchdog_reset_gc2d_qos_max,
			MIN_FREQ);

	/* watchdog_reset gcsh to min frequency */
	pm_qos_update_request(&watchdog_reset_gcsh_qos_max,
			MIN_FREQ);

	/* watchdog_reset vpu to min frequency */
	pm_qos_update_request(&watchdog_reset_vpu_qos_max,
			MIN_FREQ);
}

static void watchdog_reset_qos_init(void)
{
	pm_qos_add_request(&watchdog_reset_cpu_qos_max,
		PM_QOS_CPUFREQ_MAX, PM_QOS_DEFAULT_VALUE);

	pm_qos_add_request(&watchdog_reset_ddr_qos_max,
		   PM_QOS_DDR_DEVFREQ_MAX, PM_QOS_DEFAULT_VALUE);
	pm_qos_add_request(&watchdog_reset_gc3d_qos_max,
		   PM_QOS_GPUFREQ_3D_MAX, PM_QOS_DEFAULT_VALUE);

	pm_qos_add_request(&watchdog_reset_gc2d_qos_max,
		   PM_QOS_GPUFREQ_2D_MAX, PM_QOS_DEFAULT_VALUE);

	pm_qos_add_request(&watchdog_reset_gcsh_qos_max,
		   PM_QOS_GPUFREQ_SH_MAX, PM_QOS_DEFAULT_VALUE);

	pm_qos_add_request(&watchdog_reset_vpu_qos_max,
		   PM_QOS_VPU_DEVFREQ_MAX, PM_QOS_DEFAULT_VALUE);
}

static int watchdog_reset_notify(struct notifier_block *nb,
	unsigned long event, void *dummy)
{
	struct clk *gc_clk, *gc_2d_clk, *gc_sh_clk,
				*vpu_clk, *cpu_clk, *ddr_clk;

	watchdog_reset_qos_init();
	watchdog_reset_setfreq();

	cpu_clk = clk_get_sys(NULL, "cpu");
	ddr_clk = clk_get_sys(NULL, "ddr");
	gc_clk = clk_get_sys(NULL, "GCCLK");
	gc_2d_clk = clk_get_sys(NULL, "GC2DCLK");
	gc_sh_clk = clk_get_sys(NULL, "GC_SHADER_CLK");
	vpu_clk = clk_get_sys(NULL, "VPUCLK");

	pr_info("cpu %lu,ddr %lu,gc %lu,gc2d %lu,gcsh %lu,vpu %lu\n",
			clk_get_rate(cpu_clk), clk_get_rate(ddr_clk),
			clk_get_rate(gc_clk), clk_get_rate(gc_2d_clk),
			clk_get_rate(gc_sh_clk), clk_get_rate(vpu_clk));

	return NOTIFY_DONE;
}

static struct notifier_block pxa_watchdog_reset_notifier = {
	.notifier_call = watchdog_reset_notify,
	.priority = 100,
};

static int __init watchdog_reset_init(void)
{
	if (has_feat_watchdog_reset())
		register_reboot_notifier(&pxa_watchdog_reset_notifier);
	return 0;
}

late_initcall(watchdog_reset_init);
