#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/regs-addr.h>
#include <linux/of_platform.h>

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
#define		ARSTN (0x1 << 1)
#define		P_PWDN (0x1 << 8)

#define		FC_EN (0x1 << 9)
#define PMUA_ISP_CLKCTRL 0x1e4
#define		ACLK_CLKSRC_SEL (0x7 << 12)
#define		ACLK_DIV (0x7 << 8)
#define		CLK4X_CLKSRC_SEL (0x7 << 4)
#define		CLK4X_DIV (0x7 << 0)
#define PMUA_ISP_CLKCTRL2 0x1e8
#define		CSI_TXCLK_ESC_2LN_EN (0x1 << 9)
#define		CSI_TXCLK_ESC_4LN_EN (0x1 << 8)
#define		CSI_CLK_CLKSRC_SEL (0x7 << 4)
#define		CSI_CLK_DIV (0x7 << 0)

#define ISP_CKGT 0x68
#define		MC_P3_CKGT_DISABLE 0x1

#define PMUA_DEBUG_REG 0x88
#define		MIPI_CSI_DVDD_VALID	(0x3 << 27)
#define		MIPI_CSI_AVDD_VALID	(0x3 << 25)

#define REG_CTRL1	0x40	/* CCIC Control 1 */

/*
 * The below functions are used for sc2 power-up or
 * power-down.
 * The normal sequence for power up is:
 *	 1. call sc2_power_switch(1)
 *	 2. enable the clks by using clk-tree
 *	 3. call sc2_deassert_reset(1)
 * The normal sequence for power down is:
 *	 1. call sc2_deassert_reset(0)
 *	 2. disalbe the clks by using clk-tree
 *	 3. call sc2_power_switch(0)
 */

/*
 * sc2_deassert_reset: set the reset bits for sc2
 * This function should be called after clk enabled
 * or before power down
 */
static void sc2_deassert_reset(int on)
{
	unsigned int regval;
	void __iomem *apmu_base;
	void __iomem *ciu_base;

	apmu_base = get_apmu_base_va();
	if (apmu_base == NULL) {
		pr_err("error to get APMU virt base\n");
		return;
	}

	ciu_base = get_ciu_base_va();
	if (ciu_base == NULL) {
		pr_err("error to get ciu virt base\n");
		return;
	}

	if (on) {
		regval = readl(apmu_base + PMUA_ISP_RSTCTRL);
		regval &= ~(ALL_RST_MASK);
		regval |= RST_DEASSERT;
		writel(regval, apmu_base + PMUA_ISP_RSTCTRL);
		udelay(200);
		regval = readl(ciu_base + ISP_CKGT);
		regval &= ~(MC_P3_CKGT_DISABLE);
		writel(regval, ciu_base + ISP_CKGT);
	} else {
		regval = readl(apmu_base + PMUA_ISP_RSTCTRL);
		regval &= ~(ALL_RST_MASK);
		regval |= RST_ASSERT;
		writel(regval, apmu_base + PMUA_ISP_RSTCTRL);
	}

	return;
}

static void sc2_on(void)
{
	unsigned int regval;
	unsigned int timeout = 1000;
	void __iomem *apmu_base;
	void __iomem *ciu_base;
	void __iomem *mpmu_base;
	struct device_node *np = of_find_node_by_name(NULL, "ccic_couple");
	const u32 *tmp;
	int len, sync = 0;
	static void __iomem *ccic1_base;

	apmu_base = get_apmu_base_va();
	if (apmu_base == NULL) {
		pr_err("error get APMU virt base\n");
		return;
	}

	ciu_base = get_ciu_base_va();
	if (ciu_base == NULL) {
		pr_err("error to get ciu virt base\n");
		return;
	}

	mpmu_base = get_mpmu_base_va();
	if (mpmu_base == NULL) {
		pr_err("error to get APMU virt base\n");
		return;
	}

	if (np) {
		tmp = of_get_property(np, "ccic_coupled", &len);
		if (tmp)
			sync = be32_to_cpup(tmp);
	}

	/* 0. Keep SC2_ISLAND AXI RESET asserted */
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
			return;
		}
		udelay(10);
		regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	} while (!(regval & ISP_PWR_STATUS));
	/* 6. Wait for Active Interrupt pending */
	do {
		if (--timeout == 0) {
			WARN(1, "SC2: Active Interrupt timeout!\n");
			return;
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
			return;
		}
		udelay(10);
		regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	} while (!(regval & IPE0_PWR_STATUS));
	/* 12. Wait for Active Interrupt pending */
	do {
		if (--timeout == 0) {
			WARN(1, "SC2: Active Interrupt timeout!\n");
			return;
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
	/* clk enabling sequence */
	/* 16. Enable Dummy Clocks, wait for 500ns, Disable Dummy Clocks*/
	regval = readl(apmu_base + PMUA_ISLD_ISP_CTRL);
	regval |= CMEM_DMMYCLK_EN;
	writel(regval, apmu_base + PMUA_ISLD_ISP_CTRL);
	udelay(500);
	regval = readl(apmu_base + PMUA_ISLD_ISP_CTRL);
	regval &= ~(CMEM_DMMYCLK_EN);
	writel(regval, apmu_base + PMUA_ISLD_ISP_CTRL);
	/* 17. Disable Frequency Change */
	regval = readl(apmu_base + PMUA_ISP_RSTCTRL);
	regval &= ~(FC_EN);
	writel(regval, apmu_base + PMUA_ISP_RSTCTRL);
	/* 18. Enable Island AXI clock */
	regval = readl(apmu_base + PMUA_ISP_CLKCTRL);
	regval &= ~(ACLK_CLKSRC_SEL); /* select 312MHz as source */
	regval &= ~(ACLK_DIV);
	regval |= 0x100; /* set divider = 1 */
	writel(regval, apmu_base + PMUA_ISP_CLKCTRL);
	/* 19. Enable island clk4x */
	regval = readl(apmu_base + PMUA_ISP_CLKCTRL);
	regval &= ~(CLK4X_CLKSRC_SEL); /* select 624MHz as source */
	regval &= ~(CLK4X_DIV);
	regval |= 0x1; /* set divider = 1 */
	writel(regval, apmu_base + PMUA_ISP_CLKCTRL);
	/* 20. Enable OVISP Core clock */

	/* 21. Enable  OVISP pipeline clock*/

	/* 22. Enable CSI_CLK */
	regval = readl(apmu_base + PMUA_ISP_CLKCTRL2);
	regval &= ~(CSI_CLK_CLKSRC_SEL); /* select 624MHz as source */
	regval &= ~(CSI_CLK_DIV);
	regval |= 0x1; /* set divider = 1 */
	writel(regval, apmu_base + PMUA_ISP_CLKCTRL2);
	/* 23. Enable CSI TCLK DPHY4LN */
	regval = readl(apmu_base + PMUA_ISP_CLKCTRL2);
	regval |= CSI_TXCLK_ESC_4LN_EN;
	writel(regval, apmu_base + PMUA_ISP_CLKCTRL2);
	/* 24. Enable CSI TCLK DPHY2LN */
	regval = readl(apmu_base + PMUA_ISP_CLKCTRL2);
	regval |= CSI_TXCLK_ESC_2LN_EN;
	writel(regval, apmu_base + PMUA_ISP_CLKCTRL2);
	/* 25. Enable Frequency Change */
	regval = readl(apmu_base + PMUA_ISP_RSTCTRL);
	regval |= FC_EN;
	writel(regval, apmu_base + PMUA_ISP_RSTCTRL);
	/* 26. deassert reset */
	sc2_deassert_reset(1);

	/* reset mclk */
	iowrite32(0x7, mpmu_base + 0x1140);
	iowrite32(0x7, mpmu_base + 0x1144);

	if (sync) {
		/* always clear ccic1 pwrdnen bit */
		if (!ccic1_base)
			ccic1_base = ioremap(0xd420a000, 0x31c);
		regval = ioread32(ccic1_base + REG_CTRL1);
		regval &= ~0x10000000;
		iowrite32(regval, ccic1_base + REG_CTRL1);
	}

	return;
}

static void sc2_off(void)
{
	unsigned int regval;
	unsigned int timeout = 1000;
	void __iomem *apmu_base;

	apmu_base = get_apmu_base_va();
	if (apmu_base == NULL) {
		pr_err("error to ioremap APMU base\n");
		return;
	}

	/* 1. Assert the resets */
	sc2_deassert_reset(0);
	/* 2. Disable Frequency Change */
	regval = readl(apmu_base + PMUA_ISP_RSTCTRL);
	regval &= ~(FC_EN);
	writel(regval, apmu_base + PMUA_ISP_RSTCTRL);
	/* 3. Disable Island AXI clock */
	regval = readl(apmu_base + PMUA_ISP_CLKCTRL);
	regval &= ~(ACLK_DIV);
	writel(regval, apmu_base + PMUA_ISP_CLKCTRL);
	/* 4. Disable clk4x clocks */
	regval = readl(apmu_base + PMUA_ISP_CLKCTRL);
	regval &= ~(CLK4X_DIV);
	writel(regval, apmu_base + PMUA_ISP_CLKCTRL);
	/* 5. Disable OVISP Core clock */

	/* 6. Disable OVISP Peripheral clock */

	/* 7. wait for 50ns */
	udelay(50);
	/* 8. Disable CSI_CLK */
	regval = readl(apmu_base + PMUA_ISP_CLKCTRL2);
	regval &= ~(CSI_CLK_DIV);
	writel(regval, apmu_base + PMUA_ISP_CLKCTRL2);
	/* 9. Disable CSI TCLK DPHY4LN */
	regval = readl(apmu_base + PMUA_ISP_CLKCTRL2);
	regval &= ~(CSI_TXCLK_ESC_4LN_EN);
	writel(regval, apmu_base + PMUA_ISP_CLKCTRL2);
	/* 10. Disable CSI TCLK DPHY2LN */
	regval = readl(apmu_base + PMUA_ISP_CLKCTRL2);
	regval &= ~(CSI_TXCLK_ESC_2LN_EN);
	writel(regval, apmu_base + PMUA_ISP_CLKCTRL2);
	/* 11. wait for 50ns */
	udelay(50);
	/* 12. Enable Frequency Change */
	regval = readl(apmu_base + PMUA_ISP_RSTCTRL);
	regval |= FC_EN;
	writel(regval, apmu_base + PMUA_ISP_RSTCTRL);
	/* 13. Enable HWMODE to power down the island */
	regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	regval |= ISP_HWMODE_EN;
	writel(regval, apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	/* 14. Power down the Island */
	regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	regval &= ~(ISP_PWRUP);
	writel(regval, apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	/* 15. Wait for Island to be powered down */
	do {
		if (--timeout == 0) {
			WARN(1, "SC2: Island power down timeout!\n");
			return;
		}
		udelay(10);
		regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	} while (regval & ISP_PWR_STATUS);
	/* 16. Enable HWMODE to power down the OVT island */
	regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	regval |= IPE0_HWMODE_EN;
	writel(regval, apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	/* 17. Power down the OVT Island */
	regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	regval &= ~(IPE0_PWRUP);
	writel(regval, apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	/* 18. Wait for OVT Island to be powered down */
	do {
		if (--timeout == 0) {
			WARN(1, "SC2: OVT Island power up timeout!\n");
			return;
		}
		udelay(10);
		regval = readl(apmu_base + PMUA_ISLD_ISP_PWRCTRL);
	} while (regval & IPE0_PWR_STATUS);

	return;
}

void sc2_power_switch(unsigned int power_on)
{
	if (power_on)
		sc2_on();
	else
		sc2_off();
}
EXPORT_SYMBOL(sc2_power_switch);
