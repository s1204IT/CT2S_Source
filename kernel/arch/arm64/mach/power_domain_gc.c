#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/regs-addr.h>
#include <linux/clk.h>
#include <linux/clk/mmpdcstat.h>
#include <linux/cputype.h>

/* GC 3D/2D pwr function */
#define APMU_ISLD_GC3D_PWRCTRL	0x20c
#define APMU_ISLD_GC_CTRL	0x1b4
#define APMU_ISLD_GC2D_CTRL	0x1b8
#define APMU_GC3D_CKGT		0x3c
#define APMU_GC3D_CLKCTRL	0x174
#define APMU_GC3D_RSTCTRL	0x170

#define APMU_ISLD_GC2D_PWRCTRL	0x210
#define APMU_FABRIC1_CKGT	0x64
#define APMU_GC2D_CLKCTRL	0x17c
#define APMU_GC2D_RSTCTRL	0x178
#define APMU_AP_DEBUG3		0x394

#define KEEP_ACLK11_ON		(1u << 26)
#define HWMODE_EN	(1u << 0)
#define PWRUP		(1u << 1)
#define PWR_STATUS	(1u << 4)
#define REDUN_STATUS	(1u << 5)
#define INT_CLR		(1u << 6)
#define INT_MASK	(1u << 7)
#define INT_STATUS	(1u << 8)

#define GC2D_ACLK_EN	(1u << 0)
#define GC2D_CLK1X_DIV_MASK	(7 << 8)
#define GC2D_CLK1X_DIV_SHIFT	8

#define GC2D_CLK1X_CLKSRC_SEL_MASK	(7 << 12)
#define GC2D_CLK1X_CLKSRC_SEL_SHIFT	12

#define GC2D_CLK1X_EN_SHIFT	15
#define GC2D_CLK1X_EN_MASK	(1 << 15)

#define	GC2D_HCLK_EN	(1u << 24)
#define	GC2D_UPDATE_RTCWTC	(1u << 31)

#define GC2D_ACLK_RST	(1u << 0)
#define GC2D_CLK1X_RST	(1u << 1)
#define GC2D_HCLK_RST	(1u << 2)
#define GC2D_PWRON_RST	(1u << 7)
#define GC2D_FC_EN	(1u << 9)

#define X2H_CKGT_DISABLE	(1u << 0)

#define GC3D_ACLK_RST	(1u << 0)
#define GC3D_CLK1X_RST	(1u << 1)
#define GC3D_HCLK_RST	(1u << 2)
#define GC3D_PWRON_RST	(1u << 7)
#define GC3D_FC_EN	(1u << 9)

#define GC3D_ACLK_DIV_MASK	(7 << 0)
#define GC3D_ACLK_DIV_SHIFT	0

#define GC3D_ACLK_EN_SHIFT	7
#define GC3D_ACLK_EN_MASK	(1 << 7)

#define GC3D_ACLKSRC_SEL_MASK	(7 << 4)
#define GC3D_ACLKSRC_SEL_SHIFT	4

#define GC3D_CLK1X_DIV_MASK	(7 << 8)
#define GC3D_CLK1X_DIV_SHIFT	8

#define GC3D_CLK1X_EN_SHIFT	15
#define GC3D_CLK1X_EN_MASK	(1 << 15)

#define GC3D_CLK1X_CLKSRC_SEL_MASK	(7 << 12)
#define GC3D_CLK1X_CLKSRC_SEL_SHIFT	12

#define GC3D_CLKSH_DIV_MASK	(7 << 16)
#define GC3D_CLKSH_DIV_SHIFT	16

#define GC3D_CLKSH_EN_SHIFT	23
#define GC3D_CLKSH_EN_MASK	(1 << 23)

#define	GC3D_CLKSH_CLKSRC_SEL_MASK	(7 << 20)
#define GC3D_CLKSH_CLKSRC_SEL_SHIFT	20

#define GC3D_HCLK_EN	(1u << 24)
#define GC3D_UPDATE_RTCWTC	(1u << 31)

#define GC3D_FAB_CKGT_DISABLE	(1u << 1)
#define MC_P4_CKGT_DISABLE	(1u << 0)

#define CMEM_DMMYCLK_EN		(1u << 4)

/* record reference count on gc2d/gc3d power on/off */
static int gc2d_pwr_refcnt;
static int gc3d_pwr_refcnt;

/* used to protect gc2d/gc3d power sequence */
static DEFINE_SPINLOCK(gc2d_pwr_lock);
static DEFINE_SPINLOCK(gc3d_pwr_lock);

bool gc3d_pwr_is_on(void)
{
	if (gc3d_pwr_refcnt)
		return true;
	else
		return false;
}

bool gcsh_pwr_is_on(void)
{
	return gc3d_pwr_is_on();
}

bool gc2d_pwr_is_on(void)
{
	if (gc2d_pwr_refcnt)
		return true;
	else
		return false;
}


static void gc2d_on(void)
{
	unsigned int regval, divval;
	unsigned int timeout;
	void __iomem *apmu_base;
	void __iomem *ciu_base;

	apmu_base = get_apmu_base_va();
	ciu_base = get_ciu_base_va();

	/* 1. Disable fabric1 x2h dynamic clock gating. */
	regval = readl(ciu_base + APMU_FABRIC1_CKGT);
	regval |= X2H_CKGT_DISABLE;
	writel(regval, ciu_base + APMU_FABRIC1_CKGT);

	/* 2. Assert GC2D ACLK reset */
	regval = readl(apmu_base + APMU_GC2D_RSTCTRL);
	regval &= ~GC2D_ACLK_RST;
	writel(regval, apmu_base + APMU_GC2D_RSTCTRL);

	/* 3. Enable HWMODE to power up the island and clear interrupt mask. */
	regval = readl(apmu_base + APMU_ISLD_GC2D_PWRCTRL);
	regval |= HWMODE_EN;
	writel(regval, apmu_base + APMU_ISLD_GC2D_PWRCTRL);

	regval = readl(apmu_base + APMU_ISLD_GC2D_PWRCTRL);
	regval &= ~INT_MASK;
	writel(regval, apmu_base + APMU_ISLD_GC2D_PWRCTRL);

	/* 4. Power up the island. */
	regval = readl(apmu_base + APMU_ISLD_GC2D_PWRCTRL);
	regval |= PWRUP;
	writel(regval, apmu_base + APMU_ISLD_GC2D_PWRCTRL);

	/*
	 * 5. Wait for active interrupt pending, indicating
	 * completion of island power up sequence.
	 * */
	timeout = 1000;
	do {
		if (--timeout == 0) {
			WARN(1, "GC2D: active interrupt pending!\n");
			return;
		}
		udelay(10);
		regval = readl(apmu_base + APMU_ISLD_GC2D_PWRCTRL);
	} while (!(regval & INT_STATUS));

	/*
	 * 6. The island is now powered up. Clear the interrupt and
	 *    set the interrupt masks.
	 */
	regval = readl(apmu_base + APMU_ISLD_GC2D_PWRCTRL);
	regval |= INT_CLR | INT_MASK;
	writel(regval, apmu_base + APMU_ISLD_GC2D_PWRCTRL);

	/* 7. Enable Dummy Clocks to SRAMs. */
	regval = readl(apmu_base + APMU_ISLD_GC2D_CTRL);
	regval |= CMEM_DMMYCLK_EN;
	writel(regval, apmu_base + APMU_ISLD_GC2D_CTRL);

	/* 8. Wait for 500ns. */
	udelay(1);

	/* 9. Disable Dummy Clocks to SRAMs. */
	regval = readl(apmu_base + APMU_ISLD_GC2D_CTRL);
	regval &= ~CMEM_DMMYCLK_EN;
	writel(regval, apmu_base + APMU_ISLD_GC2D_CTRL);

	/* 10. set FC_EN to 0. */
	regval = readl(apmu_base + APMU_GC2D_RSTCTRL);
	regval &= ~GC2D_FC_EN;
	writel(regval, apmu_base + APMU_GC2D_RSTCTRL);

	/* 11. Program PMUA_GC2D_CLKCTRL to enable clocks. */
	/* Enable GC2D HCLK clock. */
	regval = readl(apmu_base + APMU_GC2D_CLKCTRL);
	regval |= GC2D_HCLK_EN;
	writel(regval, apmu_base + APMU_GC2D_CLKCTRL);

	/* Enalbe GC2D CLK1X clock. */
	if (pxa1928_is_a0()) {
		regval = readl(apmu_base + APMU_GC2D_CLKCTRL);
		divval = (regval & GC2D_CLK1X_DIV_MASK) >> GC2D_CLK1X_DIV_SHIFT;
		if (!divval) {
			regval &= ~GC2D_CLK1X_DIV_MASK;
			regval |= (2 << GC2D_CLK1X_DIV_SHIFT);
			writel(regval, apmu_base + APMU_GC2D_CLKCTRL);
		}
	} else {
		regval = readl(apmu_base + APMU_GC2D_CLKCTRL);
		regval |= GC2D_CLK1X_EN_MASK;
		writel(regval, apmu_base + APMU_GC2D_CLKCTRL);
	}

	/* Enable GC2D ACLK clock. */
	regval = readl(apmu_base + APMU_GC2D_CLKCTRL);
	regval |= GC2D_ACLK_EN;
	writel(regval, apmu_base + APMU_GC2D_CLKCTRL);

	/* 12. Enable Frequency change. */
	regval = readl(apmu_base + APMU_GC2D_RSTCTRL);
	regval |= GC2D_FC_EN;
	writel(regval, apmu_base + APMU_GC2D_RSTCTRL);

	/* 13. Wait 32 cycles of the slowest clock(HCLK, clk 1x, ACLK. */
	udelay(5);

	/* 14. De-assert GC2D ACLK Reset, CLK1X Reset and HCLK Reset. */
	regval = readl(apmu_base + APMU_GC2D_RSTCTRL);
	regval |= (GC2D_ACLK_RST | GC2D_CLK1X_RST | GC2D_HCLK_RST);
	writel(regval, apmu_base + APMU_GC2D_RSTCTRL);

	/* 15. Wait 128 cycles of the slowest clock(HCLK, clk 1x, ACLK. */
	udelay(10);

	/* 16. Enable fabric1 x2h dynamic clock gating. */
	regval = readl(ciu_base + APMU_FABRIC1_CKGT);
	regval &= ~(X2H_CKGT_DISABLE);
	writel(regval, ciu_base + APMU_FABRIC1_CKGT);

	/* set gc2d's rtc/wtc values to zero */
	if (pxa1928_is_a0()) {
		regval = readl(apmu_base + APMU_ISLD_GC2D_CTRL);
		regval &= ~(0xffff0000);
		writel(regval, apmu_base + APMU_ISLD_GC2D_CTRL);
	}

	/* keep ACLK11 running in D0CG mode */
	regval = readl(apmu_base + APMU_AP_DEBUG3);
	regval |= KEEP_ACLK11_ON;
	writel(regval, apmu_base + APMU_AP_DEBUG3);
}

static void gc2d_off(void)
{
	unsigned int regval;
	void __iomem *apmu_base;

	apmu_base = get_apmu_base_va();

	/* 1. Assert GC2D CLK1X/HCLK Reset. */
	regval = readl(apmu_base + APMU_GC2D_RSTCTRL);
	regval &= ~(GC2D_CLK1X_RST | GC2D_HCLK_RST);
	writel(regval, apmu_base + APMU_GC2D_RSTCTRL);

	/* 2. Disable all clocks. */
	/* Disable GC2D HCLK clock. */
	regval = readl(apmu_base + APMU_GC2D_CLKCTRL);
	regval &= ~GC2D_HCLK_EN;
	writel(regval, apmu_base + APMU_GC2D_CLKCTRL);

	/* Disable GC2D CLK1X clock. */
	if (pxa1928_is_a0()) {
		regval = readl(apmu_base + APMU_GC2D_CLKCTRL);
		regval &= ~GC2D_CLK1X_DIV_MASK;
		writel(regval, apmu_base + APMU_GC2D_CLKCTRL);
	} else {
		regval = readl(apmu_base + APMU_GC2D_CLKCTRL);
		regval &= ~GC2D_CLK1X_EN_MASK;
		writel(regval, apmu_base + APMU_GC2D_CLKCTRL);
	}

	/* Disable GC2D AXI clock. */
	regval = readl(apmu_base + APMU_GC2D_CLKCTRL);
	regval &= ~GC2D_ACLK_EN;
	writel(regval, apmu_base + APMU_GC2D_CLKCTRL);

	/* 3. Power down the island. */
	regval = readl(apmu_base + APMU_ISLD_GC2D_PWRCTRL);
	regval &= ~PWRUP;
	writel(regval, apmu_base + APMU_ISLD_GC2D_PWRCTRL);
}

void gc2d_pwr(unsigned int power_on)
{
	static struct clk *gc2d_clk;

	if (!gc2d_clk)
		gc2d_clk = clk_get(NULL, "GC2D_CLK");

	spin_lock(&gc2d_pwr_lock);
	if (power_on) {
		if (gc2d_pwr_refcnt == 0) {
			gc2d_on();
			clk_dcstat_event(gc2d_clk, PWR_ON, 0);
		}
		gc2d_pwr_refcnt++;
	} else {
		/*
		 * If try to power off gc2d, but gc2d_pwr_refcnt is 0,
		 * print warning and return.
		 */
		if (WARN_ON(gc2d_pwr_refcnt == 0))
			goto out;

		if (--gc2d_pwr_refcnt > 0)
			goto out;
		gc2d_off();
		clk_dcstat_event(gc2d_clk, PWR_OFF, 0);
	}
out:
	spin_unlock(&gc2d_pwr_lock);
}
EXPORT_SYMBOL(gc2d_pwr);

static void gc3d_on(void)
{
	unsigned int regval, divval;
	unsigned int timeout;
	void __iomem *apmu_base;
	void __iomem *ciu_base;

	apmu_base = get_apmu_base_va();
	ciu_base = get_ciu_base_va();

	/* 1. Disable GC3D fabric dynamic clock gating. */
	regval = readl(ciu_base + APMU_GC3D_CKGT);
	regval |= (GC3D_FAB_CKGT_DISABLE | MC_P4_CKGT_DISABLE);
	writel(regval, ciu_base + APMU_GC3D_CKGT);

	/* 2. Assert GC3D ACLK reset. */
	regval = readl(apmu_base + APMU_GC3D_RSTCTRL);
	regval &= ~GC3D_ACLK_RST;
	writel(regval, apmu_base + APMU_GC3D_RSTCTRL);

	/* 3. Enable HWMODE to power up the island and clear interrupt mask */
	regval = readl(apmu_base + APMU_ISLD_GC3D_PWRCTRL);
	regval |= HWMODE_EN;
	writel(regval, apmu_base + APMU_ISLD_GC3D_PWRCTRL);

	regval = readl(apmu_base + APMU_ISLD_GC3D_PWRCTRL);
	regval &= ~INT_MASK;
	writel(regval, apmu_base + APMU_ISLD_GC3D_PWRCTRL);

	/* 4. Power up the island. */
	regval = readl(apmu_base + APMU_ISLD_GC3D_PWRCTRL);
	regval |= PWRUP;
	writel(regval, apmu_base + APMU_ISLD_GC3D_PWRCTRL);

	/*
	 * 5. Wait for active interrupt pending, indicating
	 * completion of island power up sequence.
	 * */
	timeout = 1000;
	do {
		if (--timeout == 0) {
			WARN(1, "GC3D: active interrupt pending!\n");
			return;
		}
		udelay(10);
		regval = readl(apmu_base + APMU_ISLD_GC3D_PWRCTRL);
	} while (!(regval & INT_STATUS));

	/*
	 * 6. The island is now powered up. Clear the interrupt and
	 *    set the interrupt masks.
	 */
	regval = readl(apmu_base + APMU_ISLD_GC3D_PWRCTRL);
	regval |= INT_CLR | INT_MASK;
	writel(regval, apmu_base + APMU_ISLD_GC3D_PWRCTRL);

	/* 7. Enable Dummy Clocks to SRAMs. */
	regval = readl(apmu_base + APMU_ISLD_GC_CTRL);
	regval |= CMEM_DMMYCLK_EN;
	writel(regval, apmu_base + APMU_ISLD_GC_CTRL);

	/* 8. Wait for 500ns. */
	udelay(1);

	/* 9. Disable Dummy Clocks to SRAMs. */
	regval = readl(apmu_base + APMU_ISLD_GC_CTRL);
	regval &= ~CMEM_DMMYCLK_EN;
	writel(regval, apmu_base + APMU_ISLD_GC_CTRL);

	/* 10. set FC_EN to 0. */
	regval = readl(apmu_base + APMU_GC3D_RSTCTRL);
	regval &= ~GC3D_FC_EN;
	writel(regval, apmu_base + APMU_GC3D_RSTCTRL);

	/* 11. Program PMUA_GC3D_CLKCTRL to enable clocks. */
	/* Enable GC3D AXI/CLK1X/CLKSH clock. */
	if (pxa1928_is_a0()) {
		regval = readl(apmu_base + APMU_GC3D_CLKCTRL);
		divval = (regval & GC3D_ACLK_DIV_MASK) >> GC3D_ACLK_DIV_SHIFT;
		if (!divval) {
			regval &= ~GC3D_ACLK_DIV_MASK;
			regval |= (2 << GC3D_ACLK_DIV_SHIFT);
			writel(regval, apmu_base + APMU_GC3D_CLKCTRL);
		}

		regval = readl(apmu_base + APMU_GC3D_CLKCTRL);
		divval = (regval & GC3D_CLK1X_DIV_MASK) >> GC3D_CLK1X_DIV_SHIFT;
		if (!divval) {
			regval &= ~GC3D_CLK1X_DIV_MASK;
			regval |= (2 << GC3D_CLK1X_DIV_SHIFT);
			writel(regval, apmu_base + APMU_GC3D_CLKCTRL);
		}
		regval = readl(apmu_base + APMU_GC3D_CLKCTRL);
		regval |= 1 << 15;
		writel(regval, apmu_base + APMU_GC3D_CLKCTRL);


		regval = readl(apmu_base + APMU_GC3D_CLKCTRL);
		divval = (regval & GC3D_CLKSH_DIV_MASK) >> GC3D_CLKSH_DIV_SHIFT;
		if (!divval) {
			regval &= ~GC3D_CLKSH_DIV_MASK;
			regval |= (2 << GC3D_CLKSH_DIV_SHIFT);
			writel(regval, apmu_base + APMU_GC3D_CLKCTRL);
		}
	} else {
		regval = readl(apmu_base + APMU_GC3D_CLKCTRL);
		regval |= GC3D_ACLK_EN_MASK | GC3D_CLK1X_EN_MASK | GC3D_CLKSH_EN_MASK;
		writel(regval, apmu_base + APMU_GC3D_CLKCTRL);
	}

	/* Enable GC3D HCLK clock. */
	regval = readl(apmu_base + APMU_GC3D_CLKCTRL);
	regval |= GC3D_HCLK_EN;
	writel(regval, apmu_base + APMU_GC3D_CLKCTRL);

	/* 12. Enable Frequency change. */
	regval = readl(apmu_base + APMU_GC3D_RSTCTRL);
	regval |= GC3D_FC_EN;
	writel(regval, apmu_base + APMU_GC3D_RSTCTRL);

	/* 13. Wait 32 cycles of the slowest clock. */
	udelay(1);

	/* 14. De-assert GC3D PWRON Reset. */
	regval = readl(apmu_base + APMU_GC3D_RSTCTRL);
	regval |= GC3D_PWRON_RST;
	writel(regval, apmu_base + APMU_GC3D_RSTCTRL);

	/* 15. De-assert GC3D ACLK Reset, CLK1X Reset and HCLK Reset. */
	regval = readl(apmu_base + APMU_GC3D_RSTCTRL);
	regval |= (GC3D_ACLK_RST | GC3D_CLK1X_RST | GC3D_HCLK_RST);
	writel(regval, apmu_base + APMU_GC3D_RSTCTRL);

	/* 16. Wait for 1000ns. */
	udelay(1);

	/* 16. Enable GC3D fabric dynamic clock gating. */
	regval = readl(ciu_base + APMU_GC3D_CKGT);
	regval &= ~(GC3D_FAB_CKGT_DISABLE | MC_P4_CKGT_DISABLE);
	writel(regval, ciu_base + APMU_GC3D_CKGT);
}

static void gc3d_off(void)
{
	unsigned int regval;
	void __iomem *apmu_base;

	apmu_base = get_apmu_base_va();

	/* 1. Disable all clocks. */
	/* Disable GC3D HCLK clock. */
	regval = readl(apmu_base + APMU_GC3D_CLKCTRL);
	regval &= ~GC3D_HCLK_EN;
	writel(regval, apmu_base + APMU_GC3D_CLKCTRL);

	/* Disable GC3D SHADER/CLK1X/AXI clock */
	if (pxa1928_is_a0()) {
		regval = readl(apmu_base + APMU_GC3D_CLKCTRL);
		regval &= ~GC3D_CLKSH_DIV_MASK;
		writel(regval, apmu_base + APMU_GC3D_CLKCTRL);

		regval = readl(apmu_base + APMU_GC3D_CLKCTRL);
		regval &= ~GC3D_CLK1X_DIV_MASK;
		writel(regval, apmu_base + APMU_GC3D_CLKCTRL);

		regval = readl(apmu_base + APMU_GC3D_CLKCTRL);
		regval &= ~GC3D_ACLK_DIV_MASK;
		writel(regval, apmu_base + APMU_GC3D_CLKCTRL);
	} else {
		regval = readl(apmu_base + APMU_GC3D_CLKCTRL);
		regval &= ~(GC3D_ACLK_EN_MASK | GC3D_CLK1X_EN_MASK | GC3D_CLKSH_EN_MASK);
		writel(regval, apmu_base + APMU_GC3D_CLKCTRL);
	}


	/* 2. Assert HCLK and CLK1X resets. */
	regval = readl(apmu_base + APMU_GC3D_RSTCTRL);
	regval &= ~(GC3D_CLK1X_RST | GC3D_HCLK_RST);
	writel(regval, apmu_base + APMU_GC3D_RSTCTRL);

	/* 3. Power down the island. */
	regval = readl(apmu_base + APMU_ISLD_GC3D_PWRCTRL);
	regval &= ~PWRUP;
	writel(regval, apmu_base + APMU_ISLD_GC3D_PWRCTRL);
}

void gc3d_pwr(unsigned int power_on)
{
	static struct clk *gc3d_clk, *gcsh_clk;

	if (!gc3d_clk)
		gc3d_clk = clk_get(NULL, "GC3D_CLK1X");
	if (!gcsh_clk)
		gcsh_clk = clk_get(NULL, "GC3D_CLK2X");

	spin_lock(&gc3d_pwr_lock);
	if (power_on) {
		if (gc3d_pwr_refcnt == 0) {
			gc3d_on();
			clk_dcstat_event(gc3d_clk, PWR_ON, 0);
			clk_dcstat_event(gcsh_clk, PWR_ON, 0);
		}
		gc3d_pwr_refcnt++;
	} else {
		/*
		 * If try to power off gc3d, but gc3d_pwr_refcnt is 0,
		 * print warning and return.
		 */
		if (WARN_ON(gc3d_pwr_refcnt == 0))
			goto out;

		if (--gc3d_pwr_refcnt > 0)
			goto out;

		gc3d_off();
		clk_dcstat_event(gc3d_clk, PWR_OFF, 0);
		clk_dcstat_event(gcsh_clk, PWR_OFF, 0);
	}
out:
	spin_unlock(&gc3d_pwr_lock);
}
EXPORT_SYMBOL(gc3d_pwr);
