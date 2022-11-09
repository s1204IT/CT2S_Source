#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <mach/addr-map.h>

#include <mach/power_domain_isp.h>

#define APMU_ISP_CLK_RES_CTRL   0x038
#define APMU_PWR_CTRL_REG       0x0d8
#define APMU_PWR_BLK_TMR_REG    0x0dc
#define APMU_PWR_STATUS_REG     0x0f0

#define ISP_HW_MODE         (0x1 << 15)
#define ISP_AUTO_PWR_ON     (0x1 << 4)
#define ISP_PWR_STAT        (0x1 << 4)
#define APMU_CCIC_DBG		0x088

#define APMU_CSI_CCIC2_CLK_RES_CTRL	0x24
#define APMU_CCIC_CLK_RES_CTRL		0x50

#define read(r)		readl(apmu_base + (r))
#define write(v, r)	writel((v), apmu_base + (r))

static int isp_pwr_refcnt;
static void __iomem *apmu_base;

int isp_pwr_ctrl(void *dev,  int on)
{
	unsigned int val;
	int timeout = 5000;
	if (apmu_base == NULL) {
		apmu_base = ioremap(AXI_PHYS_BASE + 0x82800, SZ_4K);
		if (apmu_base == NULL) {
			pr_err("error to ioremap APMU base\n");
			return -EINVAL;
		}
	}
	/*  HW mode power on/off*/
	if (on) {
		if (isp_pwr_refcnt++ > 0)
			return 0;

		/* set isp HW mode*/
		val = readl(apmu_base + APMU_ISP_CLK_RES_CTRL);
		val |= ISP_HW_MODE;
		writel(val, apmu_base + APMU_ISP_CLK_RES_CTRL);

		spin_lock(&gc_vpu_isp_pwr_lock);
		/*  on1, on2, off timer */
		writel(0x20001fff, apmu_base + APMU_PWR_BLK_TMR_REG);
		/*  isp auto power on */
		val = readl(apmu_base + APMU_PWR_CTRL_REG);
		val |= ISP_AUTO_PWR_ON;
		writel(val, apmu_base + APMU_PWR_CTRL_REG);
		spin_unlock(&gc_vpu_isp_pwr_lock);

#ifdef CONFIG_VIDEO_MMPISP_AREA51
		writel(0x06000000 |
				readl(apmu_base + APMU_CCIC_DBG),
				apmu_base + APMU_CCIC_DBG);
#endif
		/*  polling ISP_PWR_STAT bit */
		while (!(readl(apmu_base + APMU_PWR_STATUS_REG)
					& ISP_PWR_STAT)) {
			udelay(500);
			timeout -= 500;
			if (timeout < 0) {
				pr_err("%s: isp power on timeout\n", __func__);
				return -ENODEV;
			}
		}
	} else {
		if (WARN_ON(isp_pwr_refcnt == 0))
			return -EINVAL;
		if (--isp_pwr_refcnt > 0)
			return 0;
#ifdef CONFIG_VIDEO_MMPISP_B52
		val = readl(apmu_base + APMU_ISP_CLK_RES_CTRL);
		val |= ISP_HW_MODE;
		writel(val, apmu_base + APMU_ISP_CLK_RES_CTRL);
#endif
		spin_lock(&gc_vpu_isp_pwr_lock);
#ifdef CONFIG_VIDEO_MMPISP_B52
		writel(0x20001fff, apmu_base + APMU_PWR_BLK_TMR_REG);
#endif
		/*  isp auto power off */
		val = readl(apmu_base + APMU_PWR_CTRL_REG);
		val &= ~ISP_AUTO_PWR_ON;
		writel(val, apmu_base + APMU_PWR_CTRL_REG);
		spin_unlock(&gc_vpu_isp_pwr_lock);
		/*  polling ISP_PWR_STAT bit */
		while ((readl(apmu_base + APMU_PWR_STATUS_REG)
					& ISP_PWR_STAT)) {
			udelay(500);
			timeout -= 500;
			if (timeout < 0) {
				pr_err("%s: ISP power off timeout\n", __func__);
				return -ENODEV;
			}
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(isp_pwr_ctrl);

int b52isp_clk_ctrl(u32 on)
{
	void __iomem *tmp;
	unsigned int reg;

	if (on) {
		if (isp_pwr_refcnt > 1)
			return 0;

		reg = read(APMU_ISP_CLK_RES_CTRL);
		write(reg | 1<<21 | 1<<18 | 1<<17 | 1<<16,
				APMU_ISP_CLK_RES_CTRL);
		do reg = read(APMU_ISP_CLK_RES_CTRL);
		while (reg & (1<<23));

		reg = read(APMU_ISP_CLK_RES_CTRL);
		write(reg | 1<<2 | 1<<4 | 1<<7 | 1<<1, APMU_ISP_CLK_RES_CTRL);
		do reg = read(APMU_ISP_CLK_RES_CTRL);
		while (reg & (1<<7));

		reg = read(APMU_ISP_CLK_RES_CTRL);
		reg |= (0x3 << 24);
		write(reg, APMU_ISP_CLK_RES_CTRL);
		reg = read(APMU_ISP_CLK_RES_CTRL);
		write(reg | 1<<0 | 1<<28 | 1<<27, APMU_ISP_CLK_RES_CTRL);

	/*	reg = read(APMU_CSI_CCIC2_CLK_RES_CTRL); */
		reg = 0x0;
		reg &= ~(0xf<<22);
		reg |= 0xb<<22;
		reg |= 0x1<<26;
		reg |= (0x1<<4);
		/*CSI clock*/
		reg &= ~(0x3<<16);
		reg |=  (0x1<<16);
		reg &= ~(0x7<<18);
		reg |=  (0x0<<18);
		reg |= (0x1<<1);
		reg |= (1<<7);
		reg |= (0x1<<15);
		write(reg, APMU_CSI_CCIC2_CLK_RES_CTRL);
		do reg = read(APMU_CSI_CCIC2_CLK_RES_CTRL);
		while (reg & (1<<15));

/*		reg = read(APMU_CCIC_CLK_RES_CTRL); */
		reg = 0x0;
		reg |= (0x3<<21);
		reg |= 0x1<<4;
		/*clk4x*/
		reg &= ~(0x3<<16);
		reg |= (0x1<<16);
		reg &= ~(0x7<<18);
		reg |= (0x1<<1);
		reg |= (0x1<<15);
		reg |= (1<<7);
		write(reg, APMU_CCIC_CLK_RES_CTRL);
		do reg = read(APMU_CCIC_CLK_RES_CTRL);
		while (reg & (1<<15));

		/* set phy */
		reg = read(APMU_CSI_CCIC2_CLK_RES_CTRL);
		reg |= (0x1<<5);
		reg |= (0x1<<7);
		reg |= (0x1<<2);
		reg |= (0x1<<1);
		write(reg, APMU_CSI_CCIC2_CLK_RES_CTRL);

		reg = read(APMU_CCIC_CLK_RES_CTRL);
		reg |= (0x1<<5);
		reg |= (0x1<<7);
		reg |= (0x1<<2);
		reg |= (0x1<<1);
		write(reg, APMU_CCIC_CLK_RES_CTRL);

		tmp  = ioremap(0xd401e1b0, 8);
		if (tmp == NULL) {
			pr_err("error to ioremap tmp base, %d\n", __LINE__);
			return -EINVAL;
		}
		writel(7 | readl(tmp + 0), tmp + 0);
		writel(7 | readl(tmp + 4), tmp + 4);
		iounmap(tmp);
	} else {
		if (isp_pwr_refcnt > 0)
			return 0;

		/*reset and disable ahb clock*/
		reg = read(APMU_CCIC_CLK_RES_CTRL);
		reg &= ~(0x3<<21);
		write(reg, APMU_CCIC_CLK_RES_CTRL);
		/*reset and clk4x clock*/
		reg = read(APMU_CCIC_CLK_RES_CTRL);
		reg &= ~(0x1<<1 | 0x1<<4);
		write(reg, APMU_CCIC_CLK_RES_CTRL);
		/*reset and dphy4x clock*/
		reg = read(APMU_CCIC_CLK_RES_CTRL);
		reg &= ~(0x1<<2 | 0x1<<5);
		write(reg, APMU_CCIC_CLK_RES_CTRL);

		/*reset and disable mcu clock*/
		reg = read(APMU_ISP_CLK_RES_CTRL);
		reg &= ~(0x3<<27);
		write(reg, APMU_ISP_CLK_RES_CTRL);
		/*reset and disable bus clock*/
		reg = read(APMU_ISP_CLK_RES_CTRL);
		reg &= ~(0x3<<16);
		write(reg, APMU_ISP_CLK_RES_CTRL);
		/*reset and disable isp clock*/
		reg = read(APMU_ISP_CLK_RES_CTRL);
		reg &= ~(0x3<<0);
		write(reg, APMU_ISP_CLK_RES_CTRL);

		/*reset and disable phy clock*/
		reg = read(APMU_CSI_CCIC2_CLK_RES_CTRL);
		reg &= ~(0x1<<2 | 0x1<<5);
		write(reg, APMU_CSI_CCIC2_CLK_RES_CTRL);
		/*reset and disable csi clock*/
		reg = read(APMU_CSI_CCIC2_CLK_RES_CTRL);
		reg &= ~(0x1<<1 | 0x1<<4);
		write(reg, APMU_CSI_CCIC2_CLK_RES_CTRL);
		/*disable vclk clock*/
		reg = read(APMU_CSI_CCIC2_CLK_RES_CTRL);
		reg &= ~(0x1<<26);
		write(reg, APMU_CSI_CCIC2_CLK_RES_CTRL);
	}

	return 0;
}

int b52isp_pwr_ctrl(u32 on)
{
	int ret;
	ret = isp_pwr_ctrl(NULL, on);
	if (ret < 0)
		return ret;

	return b52isp_clk_ctrl(on);
}
EXPORT_SYMBOL_GPL(b52isp_pwr_ctrl);

void set_b52fw_loaded(void)
{
	pr_err("TODO: add code to support firmware status tracking, refer to arch/arm64/mach/power_domain_isp.c\n");
	BUG_ON(1);
}
EXPORT_SYMBOL_GPL(set_b52fw_loaded);

int get_b52fw_loaded(void)
{
	pr_err("TODO: add code to support firmware status tracking, refer to arch/arm64/mach/power_domain_isp.c\n");
	BUG_ON(1);
}
EXPORT_SYMBOL_GPL(get_b52fw_loaded);
