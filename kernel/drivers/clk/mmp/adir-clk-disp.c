/*
 * Copyright (C) 2014 Marvell
 * Bill Zhou <zhoub@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * pxa1986 display clock params & registration
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "adir-clk.h"

DEFINE_SPINLOCK(disp_lock);

static u32 pnsclk_parent_tbl[] = {1, 7};

static const char const *disp1_parent[] = {
	PLL2_800_APPS,
	PLL1_624_APPS,
	PLL1_500_APPS,
	PLL1_416_APPS
};

static const char const *pnsclk_parent[] = {"disp1"};
static const char const *pnpath_parent[] = {"pn_sclk"};
static const char const *pnsclk_depend[] = {"LCDCIHCLK", "vdma_axi"};
static const char const *dsi_depend[] = {"dsi_phy", "LCDCIHCLK"};

static struct mmp_clk_disp vdma_axi = {
	.reg_rst = ACCU_VDMA_CLK_CNTRL_REG,
	.reg_rst_shadow = ACCU_FNCLK | ACCU_AHBCLK | ACCU_APBCLK,
	.reg_rst_mask = ACCU_FNCLK | ACCU_AHBCLK | ACCU_APBCLK,
	.lock = &disp_lock,
};

static struct mmp_clk_disp dsi_phy = {
	.reg_rst = ACCU_DISPLAY_UNIT_CLK_CNTRL_REG,
	.reg_rst_shadow = ACCU_APBCLK | ACCU_AHBCLK |
		ACCU_LCD_PHY_CAL_CKEN | ACCU_LCD_TX_ESC_CKEN,
	.reg_rst_mask = ACCU_APBCLK | ACCU_AHBCLK |
		ACCU_LCD_PHY_CAL_CKEN | ACCU_LCD_TX_ESC_CKEN,
	.lock = &disp_lock,
};

static struct mmp_clk_disp disp_axi = {
	.reg_rst = ACCU_DISPLAY1_CLK_CNTRL_REG,
	.reg_rst_shadow = ACCU_FNCLK,
	.reg_rst_mask = ACCU_FNCLK,
	.lock = &disp_lock,
};

static struct mmp_clk_disp disp1 = {
	.mux_ops = &clk_mux_ops,
	.mux.mask = 0x7,
	.mux.shift = 16,
	.mux.lock = &disp_lock,
	.reg_mux = ACCU_DISPLAY1_CLK_CNTRL_REG,
	.div_ops = &clk_divider_ops,
	.divider.width = 4,
	.divider.shift = 20,
	.divider.lock = &disp_lock,
	.reg_div = ACCU_DISPLAY1_CLK_CNTRL_REG,
};

static struct mmp_clk_disp pnsclk = {
	.mux_ops = &clk_mux_ops,
	.mux.mask = 0x7,
	.mux.shift = 29,
	.mux.lock = &disp_lock,
	.mux.table = pnsclk_parent_tbl,
	.dependence = pnsclk_depend,
	.num_dependence = ARRAY_SIZE(pnsclk_depend),
	.reg_mux_shadow = 0x20000000,
};

static struct mmp_clk_disp pnpath = {
	.div_ops = &clk_divider_ops,
	.divider.width = 8,
	.divider.shift = 0,
	.divider.lock = &disp_lock,
	.reg_rst_mask = (1 << 28),
	.dependence = pnsclk_depend,
	.num_dependence = ARRAY_SIZE(pnsclk_depend),
	.reg_div_shadow = 0x4,
};

static struct mmp_clk_disp dsi1 = {
	.div_ops = &clk_divider_ops,
	.divider.width = 4,
	.divider.shift = 8,
	.divider.lock = &disp_lock,
	.dependence = dsi_depend,
	.num_dependence = ARRAY_SIZE(dsi_depend),
	.reg_div_shadow = 0x100,
};

static struct clk *of_clk_get_parent(char *pname, struct clk *default_parent)
{
	struct device_node *np = of_find_node_by_name(NULL, pname);
	struct clk *parent = NULL;
	const char *str = NULL;

	if (np && !of_property_read_string(np, "clksrc", &str))
		parent = clk_get(NULL, str);

	return (IS_ERR_OR_NULL(parent)) ? default_parent : parent;
}

void __init pxa1986_disp_clk_init(void)
{
	void __iomem *apmu_base_va;
	void __iomem *accu_base_va;
	void __iomem *lcd_base_va;
	struct clk *clk, *clk_disp1;

	apmu_base_va = get_base_va(MMP_CLK_CTRL_REG_FROM_APMU);

	/* FIXME: need to add power on/off control to imaging power island */
	writel_relaxed(DISPLAY_VDMA_PWR_ON_OFF,
		apmu_base_va + APMU_IMG_PWR_CTRL);
	while (!(readl_relaxed(apmu_base_va + APMU_IMG_PWR_STATUS) &
		DISPLAY_VDMA_PWR_ON_OFF))
		/* wait for imaging power island power on stable */;

	accu_base_va = get_base_va(MMP_CLK_CTRL_REG_FROM_ACCU);

	clk = mmp_clk_register_disp("LCDCIHCLK", NULL, 0,
			MMP_DISP_BUS, CLK_IS_ROOT, accu_base_va, &disp_axi);
	clk_register_clkdev(clk, "LCDCIHCLK", NULL);

	clk = mmp_clk_register_disp("vdma_axi", NULL, 0,
			MMP_DISP_BUS, CLK_IS_ROOT, accu_base_va, &vdma_axi);
	clk_register_clkdev(clk, "vdma_axi", NULL);

	clk = mmp_clk_register_disp("dsi_phy", NULL, 0,
			MMP_DISP_BUS, CLK_IS_ROOT, accu_base_va, &dsi_phy);
	clk_register_clkdev(clk, "dsi_phy", NULL);

	clk_disp1 = mmp_clk_register_disp("disp1", disp1_parent,
			ARRAY_SIZE(disp1_parent), 0, CLK_SET_RATE_PARENT,
			accu_base_va, &disp1);
	clk_register_clkdev(clk_disp1, "disp1", NULL);

	clk = clk_get(NULL, PLL1_500_APPS);
	clk_set_parent(clk_disp1, of_clk_get_parent("disp1_clksrc", clk));

	lcd_base_va = get_base_va(MMP_CLK_CTRL_REG_FROM_LCD);

	pnsclk.mux.reg = lcd_base_va + LCD_SCLK_DIV;
	clk = mmp_clk_register_disp("pn_sclk", pnsclk_parent,
			ARRAY_SIZE(pnsclk_parent), MMP_DISP_MUX_ONLY,
			CLK_SET_RATE_PARENT, accu_base_va, &pnsclk);
	clk_register_clkdev(clk, "pn_sclk", NULL);
	clk_set_parent(clk, of_clk_get_parent("pn_sclk_clksrc", clk_disp1));

	pnpath.divider.reg = lcd_base_va + LCD_SCLK_DIV;
	clk = mmp_clk_register_disp("mmp_pnpath", pnpath_parent,
			ARRAY_SIZE(pnpath_parent), MMP_DISP_DIV_ONLY,
			0, accu_base_va, &pnpath);
	clk_register_clkdev(clk, "mmp_pnpath", NULL);

	dsi1.divider.reg = lcd_base_va + LCD_SCLK_DIV;
	clk = mmp_clk_register_disp("mmp_dsi1", pnpath_parent,
			ARRAY_SIZE(pnpath_parent), MMP_DISP_DIV_ONLY,
			CLK_SET_RATE_PARENT, accu_base_va, &dsi1);
	clk_register_clkdev(clk, "mmp_dsi1", NULL);
}
