#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/regs-addr.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <linux/clk/mmpdcstat.h>
#include <linux/uio_hantro.h>
#include <linux/cputype.h>
#include "mmpx-dt.h"

/* VPU pwr function */
#define APMU_ISLD_VPU_CTRL	0x1b0
#define APMU_VPU_RSTCTRL	0x1f0
#define APMU_VPU_CLKCTRL	0x1f4
#define APMU_ISLD_VPU_PWRCTRL	0x208
#define APMU_VPU_CKGT		0x6c

#define HWMODE_EN	(1u << 0)
#define PWRUP		(1u << 1)
#define PWR_STATUS	(1u << 4)
#define REDUN_STATUS	(1u << 5)
#define INT_CLR		(1u << 6)
#define INT_MASK	(1u << 7)
#define INT_STATUS	(1u << 8)
#define CMEM_DMMYCLK_EN		(1u << 4)

#define VPU_ACLK_RST	(1u << 0)
#define VPU_DEC_CLK_RST	(1u << 1)
#define VPU_ENC_CLK_RST	(1u << 2)
#define VPU_HCLK_RST	(1u << 3)
#define VPU_PWRON_RST	(1u << 7)
#define VPU_FC_EN	(1u << 9)

#define VPU_ACLK_DIV_MASK	(7 << 0)
#define VPU_ACLK_DIV_SHIFT	0

#define VPU_ACLK_EN_SHIFT	7
#define VPU_ACLK_EN_MASK	(1 << 7)

#define VPU_ACLKSRC_SEL_MASK	(7 << 4)
#define VPU_ACLKSRC_SEL_SHIFT	4

#define VPU_DCLK_DIV_MASK	(7 << 8)
#define	VPU_DCLK_DIV_SHIFT	8

#define VPU_DCLK_EN_SHIFT	15
#define VPU_DCLK_EN_MASK	(1 << 15)

#define	VPU_DCLKSRC_SEL_MASK	(7 << 12)
#define VPU_DCLKSRC_SEL_SHIFT	12

#define	VPU_ECLK_DIV_MASK	(7 << 16)
#define VPU_ECLK_DIV_SHIFT	16

#define VPU_ECLK_EN_SHIFT	23
#define VPU_ECLK_EN_MASK	(1 << 23)

#define VPU_ECLKSRC_SEL_MASK	(7 << 20)
#define VPU_ECLKSRC_SEL_SHIFT	20

#define VPU_UPDATE_RTCWTC	(1u << 31)

/* record reference count on hantro power on/off */
int hantro_pwr_refcnt;

/* used to protect gc2d/gc3d power sequence */
static DEFINE_SPINLOCK(hantro_pwr_lock);

static void hantro_on(int flag)
{
	unsigned int regval, divval;
	unsigned int timeout;
	void __iomem *apmu_base;
	void __iomem *ciu_base;

	apmu_base = get_apmu_base_va();
	ciu_base = get_ciu_base_va();

	/* 1. Assert AXI Reset. */
	regval = readl(apmu_base + APMU_VPU_RSTCTRL);
	regval &= ~VPU_ACLK_RST;
	writel(regval, apmu_base + APMU_VPU_RSTCTRL);

	/* 2. Enable HWMODE to power up the island and clear interrupt mask. */
	regval = readl(apmu_base + APMU_ISLD_VPU_PWRCTRL);
	regval |= HWMODE_EN;
	writel(regval, apmu_base + APMU_ISLD_VPU_PWRCTRL);

	regval = readl(apmu_base + APMU_ISLD_VPU_PWRCTRL);
	regval &= ~INT_MASK;
	writel(regval, apmu_base + APMU_ISLD_VPU_PWRCTRL);

	/* 3. Power up the island. */
	regval = readl(apmu_base + APMU_ISLD_VPU_PWRCTRL);
	regval |= PWRUP;
	writel(regval, apmu_base + APMU_ISLD_VPU_PWRCTRL);

	/*
	 * 4. Wait for active interrupt pending, indicating
	 * completion of island power up sequence.
	 * */
	timeout = 1000;
	do {
		if (--timeout == 0) {
			WARN(1, "VPU: active interrupt pending!\n");
			return;
		}
		udelay(10);
		regval = readl(apmu_base + APMU_ISLD_VPU_PWRCTRL);
	} while (!(regval & INT_STATUS));

	/*
	 * 5. The island is now powered up. Clear the interrupt and
	 *    set the interrupt masks.
	 */
	regval = readl(apmu_base + APMU_ISLD_VPU_PWRCTRL);
	regval |= INT_CLR | INT_MASK;
	writel(regval, apmu_base + APMU_ISLD_VPU_PWRCTRL);

	/* 6. Enable Dummy Clocks to SRAMs. */
	regval = readl(apmu_base + APMU_ISLD_VPU_CTRL);
	regval |= CMEM_DMMYCLK_EN;
	writel(regval, apmu_base + APMU_ISLD_VPU_CTRL);

	/* 7. Wait for 500ns. */
	udelay(1);

	/* 8. Disable Dummy Clocks to SRAMs. */
	regval = readl(apmu_base + APMU_ISLD_VPU_CTRL);
	regval &= ~CMEM_DMMYCLK_EN;
	writel(regval, apmu_base + APMU_ISLD_VPU_CTRL);

	/* 9. Disable VPU fabric Dynamic Clock gating. */
	regval = readl(ciu_base + APMU_VPU_CKGT);
	regval |= 0x1f;
	writel(regval, ciu_base + APMU_VPU_CKGT);

	/* 10. De-assert VPU HCLK Reset. */
	regval = readl(apmu_base + APMU_VPU_RSTCTRL);
	regval |= VPU_HCLK_RST;
	writel(regval, apmu_base + APMU_VPU_RSTCTRL);

	/* 11. Set PMUA_VPU_RSTCTRL[9] = 0x0. */
	regval = readl(apmu_base + APMU_VPU_RSTCTRL);
	regval &= ~VPU_FC_EN;
	writel(regval, apmu_base + APMU_VPU_RSTCTRL);

	/* 12. program PMUA_VPU_CLKCTRL to enable clks*/
	/* Enable VPU AXI/DECODER/ENCODER Clock. */
	if (pxa1928_is_a0()) {
		regval = readl(apmu_base + APMU_VPU_CLKCTRL);
		divval = (regval & VPU_ACLK_DIV_MASK) >> VPU_ACLK_DIV_SHIFT;
		if (!divval) {
			regval &= ~VPU_ACLK_DIV_MASK;
			regval |= (2 << VPU_ACLK_DIV_SHIFT);
			writel(regval, apmu_base + APMU_VPU_CLKCTRL);
		}

		regval = readl(apmu_base + APMU_VPU_CLKCTRL);
		divval = (regval & VPU_ECLK_DIV_MASK) >> VPU_ECLK_DIV_SHIFT;
		if (!divval) {
			regval &= ~VPU_ECLK_DIV_MASK;
			regval |= (2 << VPU_ECLK_DIV_SHIFT);
			writel(regval, apmu_base + APMU_VPU_CLKCTRL);
		}

		regval = readl(apmu_base + APMU_VPU_CLKCTRL);
		divval = (regval & VPU_DCLK_DIV_MASK) >> VPU_DCLK_DIV_SHIFT;
		if (!divval) {
			regval &= ~VPU_DCLK_DIV_MASK;
			regval |= (2 << VPU_DCLK_DIV_SHIFT);
			writel(regval, apmu_base + APMU_VPU_CLKCTRL);
		}
	} else {
		regval = readl(apmu_base + APMU_VPU_CLKCTRL);
		regval |= VPU_ACLK_EN_MASK | VPU_DCLK_EN_MASK | VPU_ECLK_EN_MASK;
		writel(regval, apmu_base + APMU_VPU_CLKCTRL);
	}

	/* 13. Enable VPU Frequency Change. */
	regval = readl(apmu_base + APMU_VPU_RSTCTRL);
	regval |= VPU_FC_EN;
	writel(regval, apmu_base + APMU_VPU_RSTCTRL);

	/* 14. De-assert VPU ACLK/DCLK/ECLK Reset. */
	regval = readl(apmu_base + APMU_VPU_RSTCTRL);
	regval |= VPU_ACLK_RST | VPU_DEC_CLK_RST | VPU_ENC_CLK_RST;
	writel(regval, apmu_base + APMU_VPU_RSTCTRL);

	/* 15. Enable VPU fabric Dynamic Clock gating. */
	regval = readl(ciu_base + APMU_VPU_CKGT);
	regval &= ~0x1f;
	writel(regval, ciu_base + APMU_VPU_CKGT);

	/* Disable Peripheral Clock. */
	if (pxa1928_is_a0()) {
		regval = readl(apmu_base + APMU_VPU_CLKCTRL);
		regval &= ~(VPU_DCLK_DIV_MASK | VPU_ECLK_DIV_MASK);
		writel(regval, apmu_base + APMU_VPU_CLKCTRL);
	} else {
		regval = readl(apmu_base + APMU_VPU_CLKCTRL);
		regval &= ~(VPU_DCLK_EN_MASK | VPU_ECLK_EN_MASK);
		writel(regval, apmu_base + APMU_VPU_CLKCTRL);
	}

	/* Restore smmu */
	if (flag)
		restore_smmu();
}

static void hantro_off(void)
{
	unsigned int regval;
	void __iomem *apmu_base;

	apmu_base = get_apmu_base_va();

	/* 1. Assert Bus Reset and Perpheral Reset. */
	regval = readl(apmu_base + APMU_VPU_RSTCTRL);
	regval &= ~(VPU_DEC_CLK_RST | VPU_ENC_CLK_RST
			| VPU_HCLK_RST);
	writel(regval, apmu_base + APMU_VPU_RSTCTRL);

	/* 2. Disable Bus and Peripheral Clock. */
	if (pxa1928_is_a0()) {
		regval = readl(apmu_base + APMU_VPU_CLKCTRL);
		regval &= ~(VPU_ACLK_DIV_MASK | VPU_DCLK_DIV_MASK | VPU_ECLK_DIV_MASK);
		writel(regval, apmu_base + APMU_VPU_CLKCTRL);
	} else {
		regval = readl(apmu_base + APMU_VPU_CLKCTRL);
		regval &= ~(VPU_ACLK_EN_MASK | VPU_DCLK_EN_MASK | VPU_ECLK_EN_MASK);
		writel(regval, apmu_base + APMU_VPU_CLKCTRL);
	}

	/* 3. Power down the island. */
	regval = readl(apmu_base + APMU_ISLD_VPU_PWRCTRL);
	regval &= ~PWRUP;
	writel(regval, apmu_base + APMU_ISLD_VPU_PWRCTRL);
}

void hantro_power_switch(unsigned int power_on, int flag)
{
	static struct clk *vpu_enc_clk, *vpu_dec_clk;

	if (!vpu_enc_clk)
		vpu_enc_clk = clk_get(NULL, "VPU_ENC_CLK");
	if (!vpu_dec_clk)
		vpu_dec_clk = clk_get(NULL, "VPU_DEC_CLK");

	mutex_lock(&vpu_pw_fc_lock);
	spin_lock(&hantro_pwr_lock);
	if (power_on) {
		if (hantro_pwr_refcnt == 0) {
			hantro_on(flag);
			clk_dcstat_event(vpu_enc_clk, PWR_ON, 0);
			clk_dcstat_event(vpu_dec_clk, PWR_ON, 0);
		}
		hantro_pwr_refcnt++;
	} else {
		/*
		 * If try to power off hantro, but hantro_pwr_refcnt is 0,
		 * print warning and return.
		 */
		if (WARN_ON(hantro_pwr_refcnt == 0))
			goto out;

		if (--hantro_pwr_refcnt > 0)
			goto out;

		hantro_off();
		clk_dcstat_event(vpu_enc_clk, PWR_OFF, 0);
		clk_dcstat_event(vpu_dec_clk, PWR_OFF, 0);
	}
out:
	spin_unlock(&hantro_pwr_lock);
	mutex_unlock(&vpu_pw_fc_lock);
}
EXPORT_SYMBOL(hantro_power_switch);
