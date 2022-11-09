/*
 * arch/arm/mach-mmp/pm-pxa1928.c
 *
 * Author:	Timothy Xia <wlxia@marvell.com>
 * Copyright:	(C) 2013 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/io.h>
#include <linux/regs-addr.h>
#include <linux/cputype.h>
#include <mach/pxa1928_lowpower.h>
#include <mach/irqs.h>
#include <mach/pm.h>
#include <linux/pm_qos.h>
#include <mach/regs-icu.h>

/* FIXME:
 * Please use Global API to replace the following local mapping
 */
static void __iomem *apmu_virt_addr;
static void __iomem *mpmu_virt_addr;
static void __iomem *APMU_WKUP_MASK;

static void __init pmu_mapping(void)
{
	apmu_virt_addr = get_apmu_base_va();
	mpmu_virt_addr = get_mpmu_base_va();

	APMU_WKUP_MASK = apmu_virt_addr + WKUP_MASK;
}

#define IRQ_PXA1928_START			32
#define IRQ_PXA1928_KEYPAD			(IRQ_PXA1928_START + 9)
#define IRQ_PXA1928_TIMER1			(IRQ_PXA1928_START + 13)
#define IRQ_PXA1928_TIMER2			(IRQ_PXA1928_START + 14)
#define IRQ_PXA1928_MMC			(IRQ_PXA1928_START + 39)
#define IRQ_PXA1928_USB_OTG		(IRQ_PXA1928_START + 44)
#define IRQ_PXA1928_GPIO			(IRQ_PXA1928_START + 49)
#define IRQ_PXA1928_MMC2			(IRQ_PXA1928_START + 52)
#define IRQ_PXA1928_MMC3			(IRQ_PXA1928_START + 53)
#define IRQ_PXA1928_MMC4			(IRQ_PXA1928_START + 54)
#define IRQ_PXA1928_PMIC			(IRQ_PXA1928_START + 77)
#define IRQ_PXA1928_RTC_ALARM		(IRQ_PXA1928_START + 79)
static void pxa1928_set_wake(int irq, unsigned int on)
{
	uint32_t wkup_mask = 0;

	/* setting wakeup sources */
	switch (irq) {
	case IRQ_PXA1928_PMIC:
		wkup_mask = PMUA_PMIC;
		break;
	case IRQ_PXA1928_USB_OTG:
		wkup_mask = PMUA_USB;
		break;
	case IRQ_PXA1928_GPIO:
		wkup_mask = PMUA_GPIO;
		break;
	case IRQ_PXA1928_MMC:
	case IRQ_PXA1928_MMC2:
		wkup_mask = PMUA_SDH1;
		break;
	case IRQ_PXA1928_MMC3:
	case IRQ_PXA1928_MMC4:
		wkup_mask = PMUA_SDH2;
		break;
	case IRQ_PXA1928_KEYPAD:
		wkup_mask = PMUA_KEYPAD;
		break;
	case IRQ_PXA1928_RTC_ALARM:
		wkup_mask = PMUA_RTC_ALARM;
		break;
	case IRQ_PXA1928_TIMER1:
		wkup_mask = PMUA_TIMER_1_1;
		break;
	case IRQ_PXA1928_TIMER2:
		wkup_mask = PMUA_TIMER_2_1;
	default:
		/* do nothing */
		break;
	}

	if (on) {
		if (wkup_mask) {
			wkup_mask |= __raw_readl(APMU_WKUP_MASK);
			__raw_writel(wkup_mask, APMU_WKUP_MASK);
		}
	} else {
		if (wkup_mask) {
			wkup_mask = ~wkup_mask & __raw_readl(APMU_WKUP_MASK);
			__raw_writel(wkup_mask, APMU_WKUP_MASK);
		}
	}
}

static void pxa1928_plt_suspend_init(void)
{
	pmu_mapping();
}

static int pxa1928_pm_check_constraint(void)
{
	int ret = 0;
	struct pm_qos_object *idle_qos;
	struct list_head *list;
	struct plist_node *node;
	struct pm_qos_request *req;

	idle_qos = pm_qos_array[PM_QOS_CPUIDLE_BLOCK];
	list = &idle_qos->constraints->list.node_list;

	/* local irq disabled here, not need any lock */
	list_for_each_entry(node, list, node_list) {
		req = container_of(node, struct pm_qos_request, node);
		/*
		 * If here is alive LPM constraint, this function's
		 * return value will cause System to repeat suspend
		 * entry and exit until other wakeup events wakeup
		 * system to full awake or the held LPM constraint
		 * is released by the user then finally entering
		 * the Suspend + Chip sleep mode.
		 */
		if ((node->prio != PM_QOS_CPUIDLE_BLOCK_DEFAULT_VALUE)) {
			pr_info("****************************************\n");
			pr_err("%s lpm constraint alive before Suspend",
				req->name);
			pr_info("*****************************************\n");
			ret = -EBUSY;
		}
	}

	return ret;
}

static int pxa1928_suspend_check(void)
{
	u32 ret = 0;

	ret = pxa1928_pm_check_constraint();

	return ret;
}


static u32 pxa1928_post_chk_wakeup(void)
{
	return 0;
}

static struct suspend_ops pxa1928_suspend_ops = {
	.pre_suspend_check = pxa1928_suspend_check,
	.post_chk_wakeup = pxa1928_post_chk_wakeup,
	.post_clr_wakeup = NULL,
	.set_wake = pxa1928_set_wake,
	.plt_suspend_init = pxa1928_plt_suspend_init,
};

static struct platform_suspend pxa1928_suspend = {
	.suspend_state	= POWER_MODE_UDR,
	.ops		= &pxa1928_suspend_ops,
};
static int __init pxa1928_suspend_init(void)
{
	int ret;
	ret = mmp_platform_suspend_register(&pxa1928_suspend);

	if (ret)
		WARN_ON("PXA1928 Suspend Register fails!!");

	return 0;
}
late_initcall(pxa1928_suspend_init);
