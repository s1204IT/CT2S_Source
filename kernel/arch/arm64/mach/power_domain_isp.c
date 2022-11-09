#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/regs-addr.h>

#include <media/mv_sc2.h>

#define PMUA_ISLD_ISP_PWRCTRL 0x214
#define		ISP_HWMODE_EN (0x1 << 0)
#define		ISP_PWRUP (0x1 << 1)
#define		ISP_PWR_STATUS (0x1 << 4)
#define		ISP_REDUN_STATUS (0x1 << 5)
#define		ISP_INT_CLR (0x1 << 6)
#define		ISP_INT_MASK (0x1 << 7)
#define		ISP_INT_STATUS (0x1 << 8)
#define		IPE0_HWMODE_EN (0x1 << 9)
#define		IPE0_PWRUP (0x1 << 10)
#define		IPE0_PWR_STATUS (0x1 << 13)
#define		IPE0_REDUN_STATUS (0x1 << 14)
#define		IPE0_INT_CLR (0x1 << 15)
#define		IPE0_INT_MASK (0x1 << 16)
#define		IPE0_INT_STATUS (0x1 << 17)
#define		IPE1_HWMODE_EN (0x1 << 18)
#define		IPE1_PWRUP (0x1 << 19)
#define		IPE1_PWR_STATUS (0x1 << 22)
#define		IPE1_REDUN_STATUS (0x1 << 23)
#define		IPE1_INT_CLR (0x1 << 24)
#define		IPE1_INT_MASK (0x1 << 25)
#define		IPE1_INT_STATUS (0x1 << 26)

#define PMUA_ISLD_ISP_CTRL 0x1a4
#define		CMEM_DMMYCLK_EN (0x1 << 4)
#define PMUA_ISP_RSTCTRL 0x1e0
#define		ALL_RST_MASK (0xff)
#define		RST_DEASSERT (0xfb)
#define		RST_ASSERT (0x06)
#define		P_PWDN (0x1 << 8)
#define		ARSTN (0x1 << 1)

#define PMUA_ISP_CLKCTRL 0x1e4
#define PMUA_ISP_CLKCTRL2 0x1e8

#define ISP_CKGT 0x68
#define		MC_P3_CKGT_DISABLE 0x1

#define PMUA_DEBUG_REG 0x88
#define		MIPI_CSI_DVDD_VALID	(0x3 << 27)
#define		MIPI_CSI_AVDD_VALID	(0x3 << 25)

#define AXI_PHYS_BASE	0xd4200000
#define PMUM_SC2_ISIM_CLK_CR	0xd4051140
#define CCIC_CTRL_1	0xd420a040
#define CCIC_CTRL_2	0xd420a840

#define PMUA_AP_DEBUG2_REG 0x390
#define		KEEP_CI_CLKS_ON (0x1 << 11)

/* record reference count on sc2 power on/off */
static atomic_t sc2_pwr_refcnt;
static int fw_loaded;

static void __iomem *apmu_base;
static void __iomem *ciu_base;
static void __iomem *ccic_ctrl1;
static void __iomem *ccic_ctrl2;

/*
 * The below functions are used for sc2 power-up or
 * power-down.
 * The normal sequence for power up is:
 *   1. call sc2_power_switch(1)
 *   2. enable the clks by using clk-tree
 *   3. call sc2_deassert_reset(1)
 * The normal sequence for power down is:
 *   1. call sc2_deassert_reset(0)
 *   2. disalbe the clks by using clk-tree
 *   3. call sc2_power_switch(0)
 */

/*FIXME: will be replaced by clk-tree*/
static int isp_clk_enable(int on)
{
	u32 rstctrl, clkctrl, clkctrl2;

	if (apmu_base == NULL) {
		apmu_base = ioremap(AXI_PHYS_BASE + 0x82800, SZ_4K);
		if (apmu_base == NULL) {
			pr_err("error to ioremap APMU base\n");
			return -EINVAL;
		}
	}

	rstctrl = readl(apmu_base + PMUA_ISP_RSTCTRL);
	clkctrl = readl(apmu_base + PMUA_ISP_CLKCTRL);
	clkctrl2 = readl(apmu_base + PMUA_ISP_CLKCTRL2);

	if (on) {
		/*Disable frequency change*/
		rstctrl &= ~(1<<9);
		writel(rstctrl, apmu_base + PMUA_ISP_RSTCTRL);

		/*enable AXI clock*/
		clkctrl &= ~(0x7<<12);

		clkctrl &= ~(0x7<<8);
		clkctrl |= (0x1<<8);

		/*enable clk 4x clock*/
		clkctrl &= ~(0x7<<4);
		clkctrl |= (0x0<<4);

		clkctrl &= ~(0x7<<0);
		clkctrl |= (0x1<<0);

		/*enable OVTISP core clock*/
		clkctrl &= ~(0x7<<28);

		clkctrl &= ~(0xF<<24);
		clkctrl |= (0x2<<24);

		/*enable OVTISP peripheral clock*/
		clkctrl &= ~(0x7<<20);
/*		clkctrl |= (0x6<<20);	*/

		clkctrl &= ~(0x7<<16);
		clkctrl |= (0x1<<16);
		writel(clkctrl, apmu_base + PMUA_ISP_CLKCTRL);

		udelay(1000);

		/*enable CSI clock*/
		clkctrl2 &= ~(0x7<<4);
		clkctrl2 |= (0x0<<4);

		clkctrl2 &= ~(0x7<<0);
		clkctrl2 |= (0x1<<0);

		/*enable CSI TCLK PHYLANE4 clock*/
		clkctrl2 |= (0x1<<8);
		/*enable CSI TCLK PHYLANE2 clock*/
		clkctrl2 |= (0x1<<9);
		writel(clkctrl2, apmu_base + PMUA_ISP_CLKCTRL2);

		udelay(1000);

		rstctrl |= (1<<9);
		writel(rstctrl, apmu_base + PMUA_ISP_RSTCTRL);
	} else {
		/*Disable frequency change*/
		rstctrl &= ~(1<<9);
		writel(rstctrl, apmu_base + PMUA_ISP_RSTCTRL);

		/*disable AXI clock */
		clkctrl &= ~(0x7<<8);

		/*disable IPE clocks*/
		clkctrl &= ~(0x7<<0);

		/*disable OVTISP core clock*/
		clkctrl &= ~(0xF<<24);

		/*disable OVTISP peripheral clock*/
		clkctrl &= ~(0x7<<16);
		writel(clkctrl, apmu_base + PMUA_ISP_CLKCTRL);

		udelay(1000);
		/*disable CSI clock*/
		clkctrl2 &= ~(0x7<<0);

		/*
		 * disable CSI TCLK PHYLANE4 clock
		 * disable CSI TCLK PHYLANE2 clock
		 */
		clkctrl2 &= ~(0x1<<8);
		clkctrl2 &= ~(0x1<<9);
		writel(clkctrl2, apmu_base + PMUA_ISP_CLKCTRL2);

		udelay(1000);

		/*Enable frequency change*/
		rstctrl |= (1<<9);
		writel(rstctrl, apmu_base + PMUA_ISP_RSTCTRL);
	}

	return 1;
}

/*
 * sc2_deassert_reset: set the reset bits for sc2
 * This function should be called after clk enabled
 * or before power down
 */
static void sc2_deassert_reset(int on)
{
	unsigned int regval;

	if (apmu_base == NULL) {
		apmu_base = ioremap(AXI_PHYS_BASE + 0x82800, SZ_4K);
		if (apmu_base == NULL) {
			pr_err("error to ioremap APMU base\n");
			return;
		}
	}

	if (ciu_base == NULL) {
		ciu_base = ioremap(AXI_PHYS_BASE + 0x82c00, 0x100);
		if (ciu_base == NULL) {
			pr_err("error to ioremap CIU base\n");
			return;
		}
	}

	if (ccic_ctrl1 == NULL) {
		ccic_ctrl1 = ioremap(CCIC_CTRL_1, 4);
		if (ccic_ctrl1 == NULL) {
			pr_err("error to ioremap ccic_ctrl1 base\n");
			return;
		}
	}

	if (ccic_ctrl2 == NULL) {
		ccic_ctrl2 = ioremap(CCIC_CTRL_2, 4);
		if (ccic_ctrl2 == NULL) {
			pr_err("error to ioremap ccic_ctrl2 base\n");
			return;
		}
	}

	if (on) {
		regval = readl(apmu_base + PMUA_ISP_RSTCTRL);
		regval &= ~ALL_RST_MASK;
		regval |= RST_DEASSERT;
		writel(regval, apmu_base + PMUA_ISP_RSTCTRL);
		udelay(200);
		regval = readl(ciu_base + ISP_CKGT);
		regval &= ~(MC_P3_CKGT_DISABLE);
		writel(regval, ciu_base + ISP_CKGT);

/*Enable mclk*/
		writel(readl(ccic_ctrl1) & ~(1 << 28), ccic_ctrl1);
		writel(readl(ccic_ctrl2) & ~(1 << 28), ccic_ctrl2);
	} else {
/*Disable mclk*/
		writel(readl(ccic_ctrl1) | (1 << 28), ccic_ctrl1);
		writel(readl(ccic_ctrl2) | (1 << 28), ccic_ctrl2);

		regval = readl(apmu_base + PMUA_ISP_RSTCTRL);
		regval &= ~ALL_RST_MASK;
		regval |= RST_ASSERT;
		writel(regval, apmu_base + PMUA_ISP_RSTCTRL);
	}

	return;
}

static void sc2_on(void)
{
	unsigned int regval;
	unsigned int timeout = 1000;

	if (apmu_base == NULL) {
		apmu_base = ioremap(AXI_PHYS_BASE + 0x82800, SZ_4K);
		if (apmu_base == NULL) {
			pr_err("error to ioremap APMU base\n");
			return;
		}
	}

	if (ciu_base == NULL) {
		ciu_base = ioremap(AXI_PHYS_BASE + 0x82c00, 0x100);
		if (ciu_base == NULL) {
			pr_err("error to ioremap CIU base\n");
			return;
		}
	}

	/* 0. Keep SC2_ISLAND AXI RESET asserted */
	/* suppose clk setting will set this bit */
	regval = readl(apmu_base + PMUA_ISP_RSTCTRL);
	regval &= ~(ARSTN);
	writel(regval, apmu_base + PMUA_ISP_RSTCTRL);

	/* 1. Disabling Dynamic Clock Gating before power up */
	regval = readl(ciu_base + ISP_CKGT);
	regval |= MC_P3_CKGT_DISABLE;
	writel(regval, ciu_base + ISP_CKGT);
	/* 2. Enable HWMODE to power up the island */
	regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	regval |= ISP_HWMODE_EN;
	writel(regval, apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	/* 3. Remove Interrupt Mask */
	regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	regval &= ~(ISP_INT_MASK);
	writel(regval, apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	/* 4. Power up the Island */
	regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	regval |= ISP_PWRUP;
	writel(regval, apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	/* 5. Wait for Island to be powered up */
	do {
		if (--timeout == 0) {
			WARN(1, "SC2: Island power up timeout!\n");
			goto out;
		}
		udelay(10);
		regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	} while (!(regval & ISP_PWR_STATUS));
	/* 6. Wait for Active Interrupt pending */
	do {
		if (--timeout == 0) {
			WARN(1, "SC2: Active Interrupt timeout!\n");
			goto out;
		}
		udelay(10);
		regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	} while (!(regval & ISP_INT_STATUS));
	/* 7. Clear the interrupt and set the interrupt mask */
	regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	regval |= (ISP_INT_CLR | ISP_INT_MASK);
	writel(regval, apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	/* 8. Enable OVT HW mode */
	regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	regval |= IPE0_HWMODE_EN;
	writel(regval, apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	/* 9. Remove OVT interrupt mask */
	regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	regval &= ~(IPE0_INT_MASK);
	writel(regval, apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	/* 10. Power up OVT */
	regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	regval |= IPE0_PWRUP;
	writel(regval, apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	/* 11. Wait for OVT Island to be powered up */
	do {
		if (--timeout == 0) {
			WARN(1, "SC2: OVT Island power up timeout!\n");
			goto out;
		}
		udelay(10);
		regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	} while (!(regval & IPE0_PWR_STATUS));
	/* 12. Wait for Active Interrupt pending */
	do {
		if (--timeout == 0) {
			WARN(1, "SC2: Active Interrupt timeout!\n");
			goto out;
		}
		udelay(10);
		regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	} while (!(regval & IPE0_INT_STATUS));
	/* 13. Clear the interrupt and set the interrupt mask */
	regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	regval |= (IPE0_INT_CLR | IPE0_INT_MASK);
	writel(regval, apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	/* 14. Enable CSI Digital VDD and Anolog VDD*/
	regval = readl(apmu_base + PMUA_DEBUG_REG);
	regval |= (MIPI_CSI_DVDD_VALID | MIPI_CSI_AVDD_VALID);
	writel(regval, apmu_base + PMUA_DEBUG_REG);
	/* 15. Enable OVTISP Power */
	regval = readl(apmu_base + PMUA_ISP_RSTCTRL);
	regval &= ~(P_PWDN);
	writel(regval, apmu_base + PMUA_ISP_RSTCTRL);
	/* 16. Enable Dummy Clocks, wait for 500us, Disable Dummy Clocks*/
	regval = readl(apmu_base + PMUA_ISLD_ISP_CTRL);
	regval |= CMEM_DMMYCLK_EN;
	writel(regval, apmu_base + PMUA_ISLD_ISP_CTRL);
	udelay(500);
	regval = readl(apmu_base + PMUA_ISLD_ISP_CTRL);
	regval &= ~(CMEM_DMMYCLK_EN);
	writel(regval, apmu_base + PMUA_ISLD_ISP_CTRL);
	/* 17. Enable KEEP_CI_CLKS_ON option for entering D0CG mode*/
	regval = readl(apmu_base + PMUA_AP_DEBUG2_REG);
	regval |= KEEP_CI_CLKS_ON;
	writel(regval, apmu_base + PMUA_AP_DEBUG2_REG);

out:
	return;
}

static void sc2_off(void)
{
	unsigned int regval;
	unsigned int timeout = 1000;

	if (apmu_base == NULL) {
		apmu_base = ioremap(AXI_PHYS_BASE + 0x82800, SZ_4K);
		if (apmu_base == NULL) {
			pr_err("error to ioremap APMU base\n");
			return;
		}
	}

	/* 1. Assert the resets */

	/* 2. Disable Frequency Change */

	/* 3. Disable Island AXI clock */

	/* 4. Disable IPE1 & IPE2 Peripheral clocks */

	/* 5. Disable OVISP Core clock */

	/* 6. Disable OVISP Peripheral clock */

	/* 7. wait for 50ns */

	/* 8. Disable CSI_CLK */

	/* 9. Disable CSI TCLK DPHY4LN */

	/* 10. Disable CSI TCLK DPHY2LN */
	/* 11. Disable KEEP_CI_CLKS_ON option when exiting from D0CG mode*/
	regval = readl(apmu_base + PMUA_AP_DEBUG2_REG);
	regval &= ~(KEEP_CI_CLKS_ON);
	writel(regval, apmu_base + PMUA_AP_DEBUG2_REG);

	/* 12. wait for 50ns */

	/* 13. Enable Frequency Change */

	/* 14. Enable HWMODE to power down the island */
	regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	regval |= ISP_HWMODE_EN;
	writel(regval, apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	/* 15. Power down the Island */
	regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	regval &= ~(ISP_PWRUP);
	writel(regval, apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	/* 16. Wait for Island to be powered down */
	do {
		if (--timeout == 0) {
			WARN(1, "SC2: Island power down timeout!\n");
			goto out;
		}
		udelay(10);
		regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	} while (regval & ISP_PWR_STATUS);
	/* 17. Enable HWMODE to power down the OVT island */
	regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	regval |= IPE0_HWMODE_EN;
	writel(regval, apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	/* 18. Power down the OVT Island */
	regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	regval &= ~(IPE0_PWRUP);
	writel(regval, apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	/* 19. Wait for OVT Island to be powered down */
	do {
		if (--timeout == 0) {
			WARN(1, "SC2: OVT Island power up timeout!\n");
			goto out;
		}
		udelay(10);
		regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	} while (regval & IPE0_PWR_STATUS);
out:
	return;
}

static int b52isp_pwr_ctrl(u32 on)
{
	if (on == 0xDEAD) {
		sc2_deassert_reset(0);
		isp_clk_enable(0);
		sc2_off();
		sc2_on();
		isp_clk_enable(1);
		sc2_deassert_reset(1);
		pr_info("----------------------reset B52ISP power\n");
		return 0;
	}

	if (on) {
		sc2_on();
		isp_clk_enable(1);
		sc2_deassert_reset(1);
	} else {
		sc2_deassert_reset(0);
		isp_clk_enable(0);
		sc2_off();
		fw_loaded = 0;
	}

	return 0;
}

int sc2_pwr_ctrl(u32 on, unsigned int mod_id)
{
	if (on) {
		if (atomic_inc_return(&sc2_pwr_refcnt) == 1) {
			if (mod_id == SC2_MOD_CCIC)
				sc2_power_switch(on);
			else if (mod_id == SC2_MOD_B52ISP)
				b52isp_pwr_ctrl(on);
		}
	} else {
		/*
		 * If try to power off sc2, but sc2_pwr_refcnt is 0,
		 * print warning and return.
		 */
		if (WARN_ON(atomic_read(&sc2_pwr_refcnt) == 0))
			return -EINVAL;

		if (atomic_dec_return(&sc2_pwr_refcnt) > 0)
			return 0;

		if (mod_id == SC2_MOD_CCIC)
			sc2_power_switch(0);
		else if (mod_id == SC2_MOD_B52ISP)
			b52isp_pwr_ctrl(0);
	}
	return 0;
}
EXPORT_SYMBOL(sc2_pwr_ctrl);

void set_b52fw_loaded(void)
{
	fw_loaded = 1;
	return;
}
EXPORT_SYMBOL(set_b52fw_loaded);

int get_b52fw_loaded(void)
{
	return fw_loaded;
}
EXPORT_SYMBOL(get_b52fw_loaded);
