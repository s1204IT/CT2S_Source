/*
 * PXA1928 CP related
 *
 * This software program is licensed subject to the GNU General Public License
 * (GPL).Version 2,June 1991, available at http://www.fsf.org/copyleft/gpl.html

 * (C) Copyright 2013 Marvell International Ltd.
 * All Rights Reserved
 */
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>

#include "pxa_cp_load.h"
#include "common_regs.h"

/**
 * CP release procedure
 * 1. clear bit SGR of register APRR to release CP
 * 2. wait until read back zero value after clear bit CPR
 */
void cp1928_releasecp(void)
{
	int value;

	writel(((unsigned long)arbel_bin_phys_addr >> 16) | 0x10000,
		MPMU_CP_REMAP_REG0);

	pr_info("%s: MPMU_CPSR before releasing Seagull %08x\n",
		__func__, __raw_readl(MPMU_CPSR));

	/* Seagull clocks */
	__raw_writel(0xffffffff, MPMU_ACGR);
	__raw_writel(0x0, CIU_FABRIC_CKGT_CTRL0);
	__raw_writel(0x0, CIU_FABRIC_CKGT_CTRL1);

	/* Fabric reset */
	value = __raw_readl(MPMU_CPRR);
	value &= ~MPMU_CPRR_BBR;
	__raw_writel(value, MPMU_CPRR);

	pr_info("release cp...\n");

	value = __raw_readl(MPMU_APRR);
	pr_info("the PMUM_APRR value is 0x%p\n",
		MPMU_APRR);
	pr_info("the value is 0x%08x\n", value);
	/* set SGR(bit[7]) bit of APRR register low */
	value &= ~MPMU_APRR_SGR;
	__raw_writel(value, MPMU_APRR);
	pr_info("done!\n");
	udelay(100);

	pr_info("%s: MPMU_CPSR after releasing Seagull %08x\n",
		__func__, __raw_readl(MPMU_CPSR));

	/* clear MPMU_CPPMU_REG4 bit 31 */
	value = __raw_readl(MPMU_CPPMU_REG4);
	value &= ~(1 << 31);
	__raw_writel(value, MPMU_CPPMU_REG4);

	pr_info("%s: clear MPMU_CPPMU_REG4 bit 31: %08x\n",
		__func__, __raw_readl(MPMU_CPPMU_REG4));
}

/**
 * CP hold procedure
 * 1. stop watchdog
 * 2. set bit SGR of register APRR to hold CP in reset
 * 3. set bits DSPR, BBR and DSRAMINT of register CPRR to hold MSA in reset
 * 4. wait several us
 */
void cp1928_holdcp(void)
{
	int value;
	int timeout = 1000000;

	if (watchdog_count_stop_fp)
		watchdog_count_stop_fp();

	pr_info("hold cp...");
	/* set CPR(bit[0]) bit of APRR register high */
	value = __raw_readl(MPMU_APRR) | MPMU_APRR_SGR;
	/* set timeout (bit 9 ~ 29) to 0 */
	value &= ~(((1 << (29 - 9 + 1)) - 1) << 9);
	__raw_writel(value, MPMU_APRR);
	/* wait reset done */
	while (((__raw_readl(MPMU_APRR) & MPMU_APRR_RST_DONE) == 0x0)
			&& timeout)
		timeout--;
	if (!timeout) {
		pr_info("read back RST_DONE failed!\n");
		pr_info("%s: MPMU_APRR %08x\n", __func__, __raw_readl(MPMU_APRR));
		pr_info("%s: MPMU_CPSR %08x\n", __func__, __raw_readl(MPMU_CPSR));
		/* set MPMU_CPPMU_REG4 bit 31 to do force reset */
		value = __raw_readl(MPMU_CPPMU_REG4);
		value |= (1 << 31);
		__raw_writel(value, MPMU_CPPMU_REG4);
		pr_info("%s: set MPMU_CPPMU_REG4 bit 31 to trigger force reset: %08x\n",
			__func__, __raw_readl(MPMU_CPPMU_REG4));

		/* wait Seagull to reset */
		udelay(10);

		pr_info("%s: MPMU_APRR after setting REG4 bit 31 %08x\n",
			__func__, __raw_readl(MPMU_APRR));
		pr_info("%s: MPMU_CPSR after setting REG4 bit 31 %08x\n",
			__func__, __raw_readl(MPMU_CPSR));
	}
	else
		pr_info("done!\n");

	pr_info("hold msa...");
	value = __raw_readl(MPMU_CPRR);
	/* set DSPR(bit[2]), BBR(bit[3]) and DSRAMINT(bit[5]) bits of CPRR
	   register high */
	value |= MPMU_CPRR_DSPR | MPMU_CPRR_BBR | MPMU_CPRR_DSRAMINT;
	__raw_writel(value, MPMU_CPRR);
	pr_info("done!\n");

	udelay(100);
}

bool cp1928_get_status(void)
{
	return !(readl(MPMU_APRR) & MPMU_APRR_SGR);
}
