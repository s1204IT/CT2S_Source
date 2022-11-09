/*
 * arch/arm/mach-mmp/power_domain_vpu.c
 *
 * Hantro video decoder engine UIO driver.
 *
 * Xiaolong Ye <yexl@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/regs-addr.h>
#include <linux/clk.h>
#include <linux/clk/mmpdcstat.h>
#include <linux/uio_coda7542.h>
#include "mmpx-dt.h"

#define APMU_VPU_CLK_RES_CTRL	0xa4
#define APMU_PWR_CTRL_REG       0xd8
#define APMU_PWR_BLK_TMR_REG    0xdc
#define APMU_PWR_STATUS_REG     0xf0

#define VPU_HW_MODE	(0x1 << 19)
#define VPU_AUTO_PWR_ON	(0x1 << 2)
#define VPU_PWR_STAT	(0x1 << 2)

int coda7542_pwr_refcnt;
static DEFINE_MUTEX(mlock);
static DEFINE_SPINLOCK(coda7542_pwr_lock);

static void coda7542_power_on(int flag)
{
	unsigned int val;
	int timeout = 2000;
	void __iomem *apmu_base;

	apmu_base = get_apmu_base_va();

	spin_lock(&coda7542_pwr_lock);
	/* set VPU HW on/off mode  */
	val = __raw_readl(apmu_base + APMU_VPU_CLK_RES_CTRL);
	val |= VPU_HW_MODE;
	__raw_writel(val, apmu_base + APMU_VPU_CLK_RES_CTRL);

	/* on1, on2, off timer */
	__raw_writel(0x20001fff, apmu_base + APMU_PWR_BLK_TMR_REG);

	/* VPU auto power on */
	val = __raw_readl(apmu_base + APMU_PWR_CTRL_REG);
	val |= VPU_AUTO_PWR_ON;
	__raw_writel(val, apmu_base + APMU_PWR_CTRL_REG);
	spin_unlock(&coda7542_pwr_lock);
	/*
	 * VPU power on takes 316us, usleep_range(280,290) takes about
	 * 300~320us, so it can reduce the duty cycle.
	 */
	usleep_range(280, 290);

	/* polling VPU_PWR_STAT bit */
	while (!(__raw_readl(apmu_base + APMU_PWR_STATUS_REG) & VPU_PWR_STAT)) {
		udelay(1);
		timeout -= 1;
		if (timeout < 0) {
			pr_err("%s: VPU power on timeout\n", __func__);
			return;
		}
	}
#ifdef CONFIG_ARM_SMMU
	/* restore smmu */
	if (flag)
		restore_smmu();
#endif
}

static void coda7542_power_off(void)
{
	unsigned int val;
	int timeout = 2000;
	void __iomem *apmu_base;

	apmu_base = get_apmu_base_va();

	spin_lock(&coda7542_pwr_lock);
	/* VPU auto power off */
	val = __raw_readl(apmu_base + APMU_PWR_CTRL_REG);
	val &= ~VPU_AUTO_PWR_ON;
	__raw_writel(val, apmu_base + APMU_PWR_CTRL_REG);
	spin_unlock(&coda7542_pwr_lock);
	/*
	 * VPU power off takes 23us, add a pre-delay to reduce the
	 * number of polling
	 */
	usleep_range(19, 22);

	/* polling VPU_PWR_STAT bit */
	while ((__raw_readl(apmu_base + APMU_PWR_STATUS_REG) & VPU_PWR_STAT)) {
		udelay(1);
		timeout -= 1;
		if (timeout < 0) {
			pr_err("%s: VPU power off timeout\n", __func__);
			return;
		}
	}
}

void coda7542_power_switch(int on, int flag)
{
	static struct clk *vpu_clk;

	if (unlikely(!vpu_clk))
		vpu_clk = clk_get(NULL, "VPUCLK");

	mutex_lock(&mlock);
	if (on) {
		if (coda7542_pwr_refcnt == 0) {
			clk_dcstat_event(vpu_clk, PWR_ON, 0);
			coda7542_power_on(flag);
		}
		coda7542_pwr_refcnt++;
	} else {
		if (WARN_ON(coda7542_pwr_refcnt == 0))
			goto out;

		if (--coda7542_pwr_refcnt > 0)
			goto out;
		mutex_lock(&vpu_pw_fc_lock);
		coda7542_power_off();
		mutex_unlock(&vpu_pw_fc_lock);
		clk_dcstat_event(vpu_clk, PWR_OFF, 0);
	}
out:
	mutex_unlock(&mlock);
}
EXPORT_SYMBOL_GPL(coda7542_power_switch);
