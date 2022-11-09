/*
 * linux/arch/arm/mach-mmp/pxa1928_lowpower.c
 *
 * Copyright:	(C) 2013 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/edge_wakeup_mmp.h>
#include <linux/cpu_pm.h>
#include <linux/regs-addr.h>
#include <linux/cputype.h>
#include <asm/cpuidle.h>
#include <asm/mach/map.h>
#include <asm/mcpm.h>
#include <mach/mmp_cpuidle.h>
#include <mach/pxa1928_lowpower.h>
#include <mach/addr-map.h>
#include <mach/pxa1928_ddrtable.h>

#ifdef CONFIG_TZ_HYPERVISOR
#include <linux/pxa_tzlc.h>
#endif

/*
 * Currently apmu and mpmu didn't use device tree.
 * here just use ioremap to get apmu and mpmu virtual
 * base address.
 */
static void __iomem *apmu_virt_addr;
static void __iomem *mpmu_virt_addr;
static void __iomem *icu_virt_addr;

static void __iomem *APMU_CORE_PWRMODE[4];
static void __iomem *APMU_CORE_RSTCTRL[4];
static void __iomem *APMU_APPS_PWRMODE;
static void __iomem *APMU_WKUP_MASK;
static void __iomem *ICU_AP_GLOBAL_IRQ_MSK[4];
static void __iomem *ICU_AP_GLOBAL_FIQ_MSK[4];

static struct cpuidle_state pxa1928_modes[] = {
	[0] = {
		.exit_latency		= 18,
		.target_residency	= 36,
		/*
		 * Use CPUIDLE_FLAG_TIMER_STOP flag to let the cpuidle
		 * framework handle the CLOCK_EVENT_NOTIFY_BROADCAST_
		 * ENTER/EXIT when entering idle states.
		 */
		.flags			= CPUIDLE_FLAG_TIME_VALID,
		.name			= "C1",
		.desc			= "C1: Core internal clock gated",
		.enter			= arm_cpuidle_simple_enter,
	},
	[1] = {
		.exit_latency		= 350,
		.target_residency	= 700,
		.flags			= CPUIDLE_FLAG_TIME_VALID |
					  CPUIDLE_FLAG_TIMER_STOP,
		.name			= "C2",
		.desc			= "C2: Core power down",
	},
	[2] = {
		.exit_latency		= 500,
		.target_residency	= 1000,
		.flags			= CPUIDLE_FLAG_TIME_VALID |
					  CPUIDLE_FLAG_TIMER_STOP,
		.name			= "D0CG-LCD",
		.desc			= "D0CG-LCD: AP idle state",
	},
	[3] = {
		.exit_latency		= 600,
		.target_residency	= 1200,
		.flags			= CPUIDLE_FLAG_TIME_VALID |
					  CPUIDLE_FLAG_TIMER_STOP,
		.name			= "D1",
		.desc			= "D1: Chip idle state",
	},
};

/*
 * edge wakeup
 *
 * To enable edge wakeup:
 * 1. Set mfp reg edge detection bits;
 * 2. Enable mfp ICU interrupt, but disable gic interrupt;
 * 3. Enable corrosponding wakeup port;
 */
#define ICU_IRQ_ENABLE	((1 << 6) | (1 << 0))
#define EDGE_WAKEUP_ICU	43	/* icu interrupt num for edge wakeup */

static void edge_wakeup_port_enable(void)
{
	u32 pad_wkup_mask;

	pad_wkup_mask = readl(APMU_WKUP_MASK);

	writel_relaxed(pad_wkup_mask | PMUA_GPIO, APMU_WKUP_MASK);
}

static void edge_wakeup_port_disable(void)
{
	u32 pad_wkup_mask;

	pad_wkup_mask = readl(APMU_WKUP_MASK);

	writel_relaxed(pad_wkup_mask & ~PMUA_GPIO, APMU_WKUP_MASK);
}
static void edge_wakeup_icu_enable(void)
{
	writel_relaxed(ICU_IRQ_ENABLE, icu_virt_addr + (EDGE_WAKEUP_ICU << 2));
}

static void edge_wakeup_icu_disable(void)
{
	writel_relaxed(0, icu_virt_addr + (EDGE_WAKEUP_ICU << 2));
}

static int need_restore_pad_wakeup;
static void pxa1928_edge_wakeup_enable(void)
{
	edge_wakeup_mfp_enable();
	edge_wakeup_icu_enable();
	edge_wakeup_port_enable();
	need_restore_pad_wakeup = 1;
}

static void pxa1928_edge_wakeup_disable(void)
{
	if (!need_restore_pad_wakeup)
		return;
	edge_wakeup_port_disable();
	edge_wakeup_icu_disable();
	edge_wakeup_mfp_disable();
	need_restore_pad_wakeup = 0;
}

static void pxa1928_lowpower_config(u32 cpu,
			u32 power_state, u32 lowpower_enable)
{
	u32 core_pwrmode[CONFIG_NR_CPUS], apcr, apps_pwrmode, core_rstctrl;
	u32 val;
	u32 cpu_idx;

	if (power_state < POWER_MODE_UDR || !lowpower_enable)
		core_pwrmode[cpu] = readl(APMU_CORE_PWRMODE[cpu]);
	else {
		for_each_possible_cpu(cpu_idx)
			core_pwrmode[cpu_idx] =
				readl(APMU_CORE_PWRMODE[cpu_idx]);
	}
	core_rstctrl = readl(APMU_CORE_RSTCTRL[cpu]);
	apps_pwrmode = readl(APMU_APPS_PWRMODE);

	/* keep bit 13 - 16 unchanged
	 * keept bit [31, 29..25] to 1 and others 0 */
	apcr = PMUM_APCR_BASE;
	apcr |= (readl_relaxed(mpmu_virt_addr + APCR) & PMUM_APCR_MASK);

	if (lowpower_enable) {
		switch (power_state) {
		case POWER_MODE_UDR:
			apcr |= PMUM_VCTCXOSD;
			for_each_possible_cpu(cpu_idx)
				core_pwrmode[cpu_idx] |= PMUA_L2SR_PWROFF_VT;
			/* fall through */
		case POWER_MODE_UDR_VCTCXO:
			/* fall through */
		case POWER_MODE_APPS_SLEEP:
			apps_pwrmode |= PMUA_APPS_D1D2;
			pxa1928_enable_lpm_ddrdll_recalibration();
			/* fall through */
		case POWER_MODE_APPS_IDLE:
			/* PMUA_APPS_D0CG = 0x1, PMUA_APPS_D1D2 = 0x2
			 * we need prevent them contaminating each other */
			if (power_state == POWER_MODE_APPS_IDLE) {
				apps_pwrmode |= PMUA_LCD_REFRESH_EN;
				apps_pwrmode |= PMUA_APPS_D0CG;
				/* For Z0 only. Will be fixed in A0.
				 * to keep VDMA functional in D0CG-LCD mode
				 * set PMUA_DEBUG2[26] to 0x1
				 * Set PMUA_DEBUG_REG[16] to 0x1 */
				val = readl_relaxed(APMU_REG(0x190));
				val |= (0x1 << 26);
				writel_relaxed(val, APMU_REG(0x190));
				val = readl_relaxed(APMU_REG(0x088));
				val |= (0x1 << 16);
				writel_relaxed(val, APMU_REG(0x088));
			}
			/* enable gpio edge for the modes need wakeup source */
			pxa1928_edge_wakeup_enable();
			/* fall through */
		case POWER_MODE_CORE_POWERDOWN:
			core_pwrmode[cpu] |= (1 << 30);
			core_pwrmode[cpu] |= PMUA_APCORESS_MP2;
			/* core and L1 SRAM power off */
			core_pwrmode[cpu] |= PMUA_CPU_PWRMODE_C2;
			/* fall through */
		case POWER_MODE_CORE_INTIDLE:
			core_rstctrl &= ~(PMUA_SW_WAKEUP);
			break;
		default:
			WARN(1, "Invalid power state!\n");
		}
	} else {
		core_pwrmode[cpu] &= ~(PMUA_CPU_PWRMODE_C2 |
					PMUA_L2SR_PWROFF_VT |
					PMUA_APCORESS_MP2);
		apcr &= ~PMUM_VCTCXOSD;
		apps_pwrmode &= ~(PMUA_LCD_REFRESH_EN | PMUA_APPS_PWRMODE_MSK);
		/* disable the gpio edge for cpu active states */
		pxa1928_edge_wakeup_disable();
	}

	if (power_state < POWER_MODE_UDR || !lowpower_enable) {
		writel_relaxed(core_pwrmode[cpu],
				APMU_CORE_PWRMODE[cpu]);
		/* according to PMU doc 0.7
		 * read back to ensure the previous write has taken effect */
		core_pwrmode[cpu] =
			readl_relaxed(APMU_CORE_PWRMODE[cpu]);
	} else {
		for_each_possible_cpu(cpu_idx) {
			writel_relaxed(core_pwrmode[cpu_idx],
					APMU_CORE_PWRMODE[cpu_idx]);
			core_pwrmode[cpu_idx] =
				readl_relaxed(APMU_CORE_PWRMODE[cpu_idx]);
		}

	}
		writel_relaxed(core_rstctrl, APMU_CORE_RSTCTRL[cpu]);
	core_rstctrl = readl_relaxed(APMU_CORE_RSTCTRL[cpu]);
	writel_relaxed(apps_pwrmode, APMU_APPS_PWRMODE);
	writel_relaxed(apcr, mpmu_virt_addr + APCR);
}

/* Here we don't enable CP wakeup sources since CP will enable them */
#define ENABLE_AP_WAKEUP_SOURCES	\
	(PMUA_CP_IPC | PMUA_PMIC | PMUA_AUDIO | PMUA_RTC_ALARM |\
	 PMUA_KEYPAD | PMUA_GPIO | PMUA_TIMER_2_1 | PMUA_SDH1)
static u32 s_wkup_mask;
/*
 * Enable AP wakeup sources and ports. To enalbe wakeup
 * ports, it needs both AP side to configure MPMU_APCR
 * and CP side to configure MPMU_CPCR to really enable
 * it. To enable wakeup sources, either AP side to set
 * MPMU_AWUCRM or CP side to set MPMU_CWRCRM can really
 * enable it.
 */
static void pxa1928_save_wakeup(void)
{
	s_wkup_mask = readl_relaxed(APMU_WKUP_MASK);
	writel(s_wkup_mask | ENABLE_AP_WAKEUP_SOURCES, APMU_WKUP_MASK);
}

static void pxa1928_restore_wakeup(void)
{
	writel(s_wkup_mask, APMU_WKUP_MASK);
}

static void pxa1928_gic_global_mask(u32 cpu, u32 mask)
{
	u32 core_pwrmode;

	core_pwrmode = readl(APMU_CORE_PWRMODE[cpu]);

	if (mask) {
		core_pwrmode |= PMUA_GIC_IRQ_GLOBAL_MASK;
		core_pwrmode |= PMUA_GIC_FIQ_GLOBAL_MASK;
	} else {
		core_pwrmode &= ~(PMUA_GIC_IRQ_GLOBAL_MASK |
					PMUA_GIC_FIQ_GLOBAL_MASK);
	}
	writel(core_pwrmode, APMU_CORE_PWRMODE[cpu]);
}

static void pxa1928_icu_global_mask(u32 cpu, u32 mask)
{
	u32 irq_msk;
	u32 fiq_msk;

	irq_msk = readl(ICU_AP_GLOBAL_IRQ_MSK[cpu]);
	fiq_msk = readl(ICU_AP_GLOBAL_FIQ_MSK[cpu]);

	if (mask) {
		irq_msk |= ICU_MASK;
		fiq_msk |= ICU_MASK;
	} else {
		irq_msk &= ~(ICU_MASK);
		fiq_msk &= ~(ICU_MASK);
	}
	writel(irq_msk, ICU_AP_GLOBAL_IRQ_MSK[cpu]);
	writel(fiq_msk, ICU_AP_GLOBAL_FIQ_MSK[cpu]);
}

static void pxa1928_set_pmu(u32 cpu, u32 power_mode)
{
	pxa1928_lowpower_config(cpu, power_mode, 1);
	/* Mask GIC global interrupt */
	pxa1928_gic_global_mask(cpu, 1);
	/* Mask ICU global interrupt */
	pxa1928_icu_global_mask(cpu, 1);
}

static void pxa1928_clr_pmu(u32 cpu)
{
	/* Unmask GIC interrtup */
	pxa1928_gic_global_mask(cpu, 0);
	/* Mask ICU global interrupt */
	pxa1928_icu_global_mask(cpu, 1);

	pxa1928_lowpower_config(cpu, -1, 0);
}
static struct platform_power_ops pxa1928_power_ops = {
	.set_pmu	= pxa1928_set_pmu,
	.clr_pmu	= pxa1928_clr_pmu,
	.save_wakeup	= pxa1928_save_wakeup,
	.restore_wakeup	= pxa1928_restore_wakeup,
	.power_up_setup = ca7_power_up_setup,
};

static struct platform_idle pxa1928_idle = {
	.cpudown_state	= POWER_MODE_CORE_POWERDOWN,
	.wakeup_state	= POWER_MODE_APPS_SLEEP,
	.hotplug_state	= POWER_MODE_UDR,
	.l2_flush_state	= POWER_MODE_UDR,
	.ops		= &pxa1928_power_ops,
	.states		= pxa1928_modes,
	.state_count	= ARRAY_SIZE(pxa1928_modes),
};
static void __init pxa1928_pmu_mapping(void)
{
	apmu_virt_addr = get_apmu_base_va();
	mpmu_virt_addr = get_mpmu_base_va();
	icu_virt_addr  = pxa1928_icu_get_base_addr();

	APMU_CORE_PWRMODE[0] = apmu_virt_addr + CORE0_PWRMODE;
	APMU_CORE_PWRMODE[1] = apmu_virt_addr + CORE1_PWRMODE;
	APMU_CORE_PWRMODE[2] = apmu_virt_addr + CORE2_PWRMODE;
	APMU_CORE_PWRMODE[3] = apmu_virt_addr + CORE3_PWRMODE;
	APMU_CORE_RSTCTRL[0] = apmu_virt_addr + CORE0_RSTCTRL;
	APMU_CORE_RSTCTRL[1] = apmu_virt_addr + CORE1_RSTCTRL;
	APMU_CORE_RSTCTRL[2] = apmu_virt_addr + CORE2_RSTCTRL;
	APMU_CORE_RSTCTRL[3] = apmu_virt_addr + CORE3_RSTCTRL;

	APMU_APPS_PWRMODE = apmu_virt_addr + APPS_PWRMODE;
	APMU_WKUP_MASK = apmu_virt_addr + WKUP_MASK;

	ICU_AP_GLOBAL_IRQ_MSK[0] = icu_virt_addr  + CORE0_CA7_GLB_IRQ_MASK;
	ICU_AP_GLOBAL_IRQ_MSK[1] = icu_virt_addr  + CORE1_CA7_GLB_IRQ_MASK;
	ICU_AP_GLOBAL_IRQ_MSK[2] = icu_virt_addr  + CORE2_CA7_GLB_IRQ_MASK;
	ICU_AP_GLOBAL_IRQ_MSK[3] = icu_virt_addr  + CORE3_CA7_GLB_IRQ_MASK;
	ICU_AP_GLOBAL_FIQ_MSK[0] = icu_virt_addr  + CORE0_CA7_GLB_FIQ_MASK;
	ICU_AP_GLOBAL_FIQ_MSK[1] = icu_virt_addr  + CORE1_CA7_GLB_FIQ_MASK;
	ICU_AP_GLOBAL_FIQ_MSK[2] = icu_virt_addr  + CORE2_CA7_GLB_FIQ_MASK;
	ICU_AP_GLOBAL_FIQ_MSK[3] = icu_virt_addr  + CORE3_CA7_GLB_FIQ_MASK;
}

static int __init pxa1928_lowpower_init(void)
{
	pxa1928_pmu_mapping();
	mmp_platform_power_register(&pxa1928_idle);

	return 0;
}

early_initcall(pxa1928_lowpower_init);
