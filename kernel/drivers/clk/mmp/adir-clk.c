/*
 * Copyright (C) 2014 Marvell
 * Bill Zhou <zhoub@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * pxa1986 clock params & registration
 */

#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>
#ifdef CONFIG_SOUND
#include <linux/delay.h>
#endif

#include "adir-clk.h"

static void __iomem *clk_iomap_register(const char *reg_name)
{
	void __iomem *reg_virt_addr;
	struct device_node *node;

	BUG_ON(!reg_name);
	node = of_find_compatible_node(NULL, NULL, reg_name);
	BUG_ON(!node);
	reg_virt_addr = of_iomap(node, 0);
	BUG_ON(!reg_virt_addr);

	return reg_virt_addr;
}

static void __iomem *accu_base_va(void)
{
	static void __iomem *accu_virt_addr;
	if (unlikely(!accu_virt_addr))
		accu_virt_addr = clk_iomap_register("mrvl,mmp-ccu-accu");
	return accu_virt_addr;
}

static void __iomem *mccu_base_va(void)
{
	static void __iomem *mccu_virt_addr;
	if (unlikely(!mccu_virt_addr))
		mccu_virt_addr = clk_iomap_register("mrvl,mmp-ccu-mccu");
	return mccu_virt_addr;
}

static void __iomem *apmu_base_va(void)
{
	static void __iomem *apmu_virt_addr;
	if (unlikely(!apmu_virt_addr))
		apmu_virt_addr = clk_iomap_register("mrvl,mmp-pmu-apmu");
	return apmu_virt_addr;
}

static void __iomem *mpmu_base_va(void)
{
	static void __iomem *mpmu_virt_addr;
	if (unlikely(!mpmu_virt_addr))
		mpmu_virt_addr = clk_iomap_register("mrvl,mmp-pmu-mpmu");
	return mpmu_virt_addr;
}

static void __iomem *lcd_base_va(void)
{
	static void __iomem *lcd_virt_addr;
	if (unlikely(!lcd_virt_addr))
		lcd_virt_addr = clk_iomap_register("marvell,mmp-disp");
	return lcd_virt_addr;
}

static void __iomem *audio_base_va(void)
{
	static void __iomem *aud_virt_addr;
	if (unlikely(!aud_virt_addr))
		aud_virt_addr = clk_iomap_register("mrvl,mmp-aud-gp");
	return aud_virt_addr;
}

void __iomem *get_base_va(u32 reg_unit)
{
	switch (reg_unit) {
	case MMP_CLK_CTRL_REG_FROM_ACCU:
		return accu_base_va();
	case MMP_CLK_CTRL_REG_FROM_MCCU:
		return mccu_base_va();
	case MMP_CLK_CTRL_REG_FROM_APMU:
		return apmu_base_va();
	case MMP_CLK_CTRL_REG_FROM_MPMU:
		return mpmu_base_va();
	case MMP_CLK_CTRL_REG_FROM_LCD:
		return lcd_base_va();
	case MMP_CLK_CTRL_REG_FROM_AUDIO:
		return audio_base_va();
	default:
		BUG_ON("unknown register unit used in clock driver\n");
	}
	return NULL;
}

/*
 * params for fixed clock sources
 */
#define __DECLARE_MPMU_PLL_PARAMS(SUBSYS, subsys, PLL, pll)		\
	struct clk_fixed_src_params mpmu_##subsys##_##pll##_params = {	\
	.ctrl_reg_offset = MPMU_##SUBSYS##_PLLREQ,			\
	.request_bit = MPMU_##SUBSYS##_##PLL##_REQ,			\
	.req_mode_mask_bits = ~(MPMU_SW_CLK_REQ | MPMU_HW_CLK_REQ),	\
	.req_mode_set_bits = MPMU_HW_CLK_REQ,				\
	.status_reg_offset = MPMU_PLLSTAT,				\
	.on_bit = MPMU_PLL_ON(MPMU_SYS_##PLL),				\
	.locked_bit = MPMU_PLL_IS_LOCKED(MPMU_SYS_##PLL),		\
};

#define __DECLARE_MCCU_CLK_PARAMS(SUBSYS, subsys, CLK, clk)		\
	struct clk_fixed_src_params mccu_##subsys##_##clk##_params = {	\
	.ctrl_reg_offset = MCCU_##SUBSYS##_CKREQ,			\
	.request_bit = MCCU_CLK_REQ(MCCU_##CLK),			\
	.status_reg_offset = MCCU_##SUBSYS##_CKSTAT,			\
	.on_bit = MCCU_CLK_REQ(MCCU_##CLK),				\
};

static DEFINE_SPINLOCK(mpmu_ap_clk_req_lock);
static DEFINE_SPINLOCK(mpmu_srd_clk_req_lock);
static DEFINE_SPINLOCK(mpmu_aud_clk_req_lock);

__DECLARE_MPMU_PLL_PARAMS(AP, ap, PLL1, pll1)
__DECLARE_MPMU_PLL_PARAMS(AP, ap, PLL2, pll2)
__DECLARE_MPMU_PLL_PARAMS(SRD, srd, PLL1, pll1)
__DECLARE_MPMU_PLL_PARAMS(SRD, srd, PLL2, pll2)
__DECLARE_MPMU_PLL_PARAMS(AUD, aud, PLL1, pll1)

static DEFINE_SPINLOCK(mccu_apps_clk_req_lock);
static DEFINE_SPINLOCK(mccu_aud_clk_req_lock);

__DECLARE_MCCU_CLK_PARAMS(AP, ap, PLL1_416, pll1_416)
__DECLARE_MCCU_CLK_PARAMS(AP, ap, PLL1_500, pll1_500)
__DECLARE_MCCU_CLK_PARAMS(AP, ap, PLL1_624, pll1_624)
__DECLARE_MCCU_CLK_PARAMS(AP, ap, PLL2_800, pll2_800)
__DECLARE_MCCU_CLK_PARAMS(AUD, aud, PLL1_416, pll1_416)
__DECLARE_MCCU_CLK_PARAMS(AUD, aud, PLL1_500, pll1_500)
__DECLARE_MCCU_CLK_PARAMS(AUD, aud, PLL1_624, pll1_624)

/*
 * params for configurable clock sources
 */
struct reg_desc core_pll1_cfg_regs[] = {
	{APMU_CORE_PLL1_CTL_1, NULL, 0x5310A001},
	{APMU_CORE_PLL1_CTL_2, NULL, 0x64600000},
	{APMU_CORE_PLL1_CTL_3, NULL, 0x00011100},
	{APMU_CORE_PLL1_CTL_4, NULL, 0x00000000},
};

struct reg_desc core_pll2_cfg_regs[] = {
	{APMU_CORE_PLL2_CTL_1, NULL, 0x5310A001},
	{APMU_CORE_PLL2_CTL_2, NULL, 0x64600000},
	{APMU_CORE_PLL2_CTL_3, NULL, 0x00011100},
	{APMU_CORE_PLL2_CTL_4, NULL, 0x00000000},
};

struct reg_desc pll3_cfg_regs[] = {
	{APMU_PLL3_CTL_1, NULL, 0x5310A001},
	{APMU_PLL3_CTL_2, NULL, 0x64600000},
	{APMU_PLL3_CTL_3, NULL, 0x00011100},
	{APMU_PLL3_CTL_4, NULL, 0x00000000},
};

struct reg_desc pll4_cfg_regs[] = {
	{APMU_PLL4_CTL_1, NULL, 0x5310A001},
	{APMU_PLL4_CTL_2, NULL, 0x64600000},
	{APMU_PLL4_CTL_3, NULL, 0x00011100},
	{APMU_PLL4_CTL_4, NULL, 0x00000000},
};

#define __DECLARE_APMU_PLL_PARAMS(PLL, pll)				\
	struct clk_config_src_params pll##_params = {			\
	.cfg_reg1_offset = APMU_##PLL##_CTL_0,				\
	.fbdiv_mask_bits = PLL_FBDIV_MASK,				\
	.fbdiv_shift = APMU_PLL_FBDIV_SHIFT,				\
	.refdiv_mask_bits = PLL_REFDIV_MASK,				\
	.refdiv_shift = APMU_PLL_REFDIV_SHIFT,				\
	.vcodiv_mask_bits = PLL_VCODIV_MASK,				\
	.vcodiv_shift = APMU_PLL_VCODIV_SHIFT,				\
	.kvco_mask_bits = PLL_KVCO_MASK,				\
	.kvco_shift = APMU_PLL_KVCO_SHIFT,				\
	.clear_bits = APMU_PLL_FC_GO,					\
	.cfg_regs = pll##_cfg_regs,					\
	.nr_cfg_regs = ARRAY_SIZE(pll##_cfg_regs),			\
	.ctrl_reg_offset = APMU_AP_CLK_REQ,				\
	.request_bit = BIT(APMU_##PLL),					\
	.req_mode_mask_bits = ~(APMU_SW_CLK_REQ | APMU_HW_CLK_REQ),	\
	.req_mode_set_bits = APMU_HW_CLK_REQ | APMU_PLL_STABLE_LOCK,	\
	.status_reg_offset = APMU_AP_CLK_ST,				\
	.on_bit = BIT(APMU_##PLL),					\
};

struct reg_desc plld_cfg_regs[] = {
	{MPMU_PLLD_CF2, NULL, 0x00010301},
	{MPMU_PLLD_CF3, NULL, 0x64600000},
	{MPMU_PLLD_CF4, NULL, 0x00000000},
};

struct clk_config_src_params ddr_pll_params = {
	.cfg_reg1_offset = MPMU_PLLD_CF1,
	.fbdiv_mask_bits = PLL_FBDIV_MASK,
	.fbdiv_shift = PLLD_FBDIV_SHIFT,
	.refdiv_mask_bits = PLL_REFDIV_MASK,
	.refdiv_shift = PLLD_REFDIV_SHIFT,
	.vcodiv_mask_bits = PLL_VCODIV_MASK,
	.vcodiv_shift = PLLD_VCODIV_SHIFT,
	.kvco_mask_bits = PLL_KVCO_MASK,
	.kvco_shift = PLLD_KVCO_SHIFT,
	.clear_bits = PLLD_CKOUT_GATING_EN,
	.cfg_regs = plld_cfg_regs,
	.nr_cfg_regs = ARRAY_SIZE(plld_cfg_regs),
	.ctrl_reg_offset = MPMU_SRD_PLLREQ,
	.request_bit = MPMU_SRD_PLLD_REQ,
	.req_mode_mask_bits = ~(MPMU_SW_CLK_REQ | MPMU_HW_CLK_REQ),
	.req_mode_set_bits = MPMU_HW_CLK_REQ,
	.status_reg_offset = MPMU_PLLSTAT,
	.on_bit = MPMU_PLL_ON(MPMU_DDR_PLL),
	.locked_bit = MPMU_PLL_IS_LOCKED(MPMU_DDR_PLL),
};

static DEFINE_SPINLOCK(apmu_pll_req_lock);

__DECLARE_APMU_PLL_PARAMS(CORE_PLL1, core_pll1)
__DECLARE_APMU_PLL_PARAMS(CORE_PLL2, core_pll2)
__DECLARE_APMU_PLL_PARAMS(PLL3, pll3)
__DECLARE_APMU_PLL_PARAMS(PLL4, pll4)

/*
 * params for peripheral clocks
 */
/* FABRIC_N1 */
/* APPS_MDMA */
/* APPS_STM */
/* APPS_IC */
/* WTM_MAIN */
/* WTM_CORE */
/* WTM_CORE_BUS */

/* FABRIC_N2, VID_DEC, VID_ENC */
static struct clk_peri_pp vpu_pp_table[] = {
	{
		.clk_rate = 312000000,
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
		.div = 2,
	},
};

static struct clk_parent vpu_parents[] = {
	{
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
	},
};

static const char const *vpu_parents_name[] = {
	PLL1_624_APPS,
};

static struct clk_peri_params axi2_params = {
	.pp_table = vpu_pp_table,
	.nr_pp = ARRAY_SIZE(vpu_pp_table),
	.parents = vpu_parents,
	.nr_parents = ARRAY_SIZE(vpu_parents),
	.ctrl_reg_offset = ACCU_FABRIC_N2_CLK_CNTRL_REG,
	.enable_value = ACCU_FNCLK | ACCU_APBCLK | ACCU_AHBCLK,
	.src_mask_bits = ACCU_SOURCE_MASK,
	.src_shift = ACCU_SRC_SHIFT,
	.div_mask_bits = ACCU_RATIO_MASK,
	.div_shift = ACCU_RATIO_SHIFT,
};

static char *vpudec_depend_clk[] = {
	"AXI2_CLK",
};

static struct clk_peri_params vpudec_params = {
	.pp_table = vpu_pp_table,
	.nr_pp = ARRAY_SIZE(vpu_pp_table),
	.parents = vpu_parents,
	.nr_parents = ARRAY_SIZE(vpu_parents),
	.depend_clks_name = vpudec_depend_clk,
	.nr_depend_clks = ARRAY_SIZE(vpudec_depend_clk),
	.ctrl_reg_offset = ACCU_VID_DEC_CLK_CNTRL_REG,
	.enable_value = ACCU_FNCLK | ACCU_APBCLK | ACCU_AHBCLK,
	.src_mask_bits = ACCU_SOURCE_MASK,
	.src_shift = ACCU_SRC_SHIFT,
	.div_mask_bits = ACCU_RATIO_MASK,
	.div_shift = ACCU_RATIO_SHIFT,
};

static struct clk_peri_params vpuenc_params = {
	.pp_table = vpu_pp_table,
	.nr_pp = ARRAY_SIZE(vpu_pp_table),
	.parents = vpu_parents,
	.nr_parents = ARRAY_SIZE(vpu_parents),
	.ctrl_reg_offset = ACCU_FABRIC_N2_CLK_CNTRL_REG,
	.ctrl_reg2_offset = ACCU_VID_DEC_CLK_CNTRL_REG,
	.enable_value = ACCU_FNCLK | ACCU_APBCLK | ACCU_AHBCLK,
	.src_mask_bits = ACCU_SOURCE_MASK,
	.src_shift = ACCU_SRC_SHIFT,
	.div_mask_bits = ACCU_RATIO_MASK,
	.div_shift = ACCU_RATIO_SHIFT,
};

/* FABRIC_N3 */

/* USB: FIXME: how to configure USB control register */

/* HSIC */
/* NAND */
/* FABRIC_N4 */
/* APPS_INT_SRAM */
/* VDMA */
/* CI1_CCIC */
/* CI2_CCIC */
/* ISP */

/* DISPLAY1, 2, DISPLAY_UNIT are implemented in adir-clk-disp.c */

/* FABRIC_N5 */
/* FABRIC_N6 */

/* GC_2D, GC1_3D, GC2_3D */
DEFINE_SPINLOCK(gc2d_lock);
DEFINE_SPINLOCK(gc3d_lock);

static struct clk_peri_pp gc2d_pp_table[] = {
	{
		.clk_rate = 104000000,
		.parent_name = PLL1_416_APPS,
		.src = SYS_PLL1_416,
		.div = 4,
	},
	{
		.clk_rate = 156000000,
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
		.div = 4,
	},
	{
		.clk_rate = 208000000,
		.parent_name = PLL1_416_APPS,
		.src = SYS_PLL1_416,
		.div = 2,
	},
	{
		.clk_rate = 312000000,
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
		.div = 2,
	},
};

static struct clk_parent gc2d_parents[] = {
	{
		.parent_name = PLL1_416_APPS,
		.src = SYS_PLL1_416,
	},
	{
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
	},
};

static const char const *gc2d_parents_name[] = {
	PLL1_416_APPS,
	PLL1_624_APPS,
};

static struct clk_peri_params gc2d_params = {
	.pp_table = gc2d_pp_table,
	.nr_pp = ARRAY_SIZE(gc2d_pp_table),
	.parents = gc2d_parents,
	.nr_parents = ARRAY_SIZE(gc2d_parents),
	.ctrl_reg_offset = ACCU_GC_2D_CLK_CNTRL_REG,
	.enable_value = ACCU_FNCLK | ACCU_APBCLK | ACCU_AHBCLK,
	.src_mask_bits = ACCU_SOURCE_MASK,
	.src_shift = ACCU_SRC_SHIFT,
	.div_mask_bits = ACCU_RATIO_MASK,
	.div_shift = ACCU_RATIO_SHIFT,
};

static struct clk_peri_pp gc3d_pp_table[] = {
	{
		.clk_rate = 104000000,
		.parent_name = PLL1_416_APPS,
		.src = SYS_PLL1_416,
		.div = 4,
	},
	{
		.clk_rate = 156000000,
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
		.div = 4,
	},
	{
		.clk_rate = 208000000,
		.parent_name = PLL1_416_APPS,
		.src = SYS_PLL1_416,
		.div = 2,
	},
	{
		.clk_rate = 312000000,
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
		.div = 2,
	},
		{
		.clk_rate = 416000000,
		.parent_name = PLL1_416_APPS,
		.src = SYS_PLL1_416,
		.div = 1,
	},
	{
		.clk_rate = 500000000,
		.parent_name = PLL1_500_APPS,
		.src = SYS_PLL1_500,
		.div = 1,
	},
	{
		.clk_rate = 624000000,
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
		.div = 1,
	},
};

static struct clk_parent gc3d_parents[] = {
	{
		.parent_name = PLL1_416_APPS,
		.src = SYS_PLL1_416,
	},
	{
		.parent_name = PLL1_500_APPS,
		.src = SYS_PLL1_500,
	},
	{
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
	},
};

static const char const *gc3d_parents_name[] = {
	PLL1_416_APPS,
	PLL1_500_APPS,
	PLL1_624_APPS,
};

static struct clk_peri_params gc3d_fn_share_params = {
	.ctrl_reg_offset = ACCU_GC1_3D_CLK_CNTRL_REG,
	.enable_value = ACCU_GC3D_INTLCLK,
};

static struct clk_peri_params gc3d_sh_share_params = {
	.ctrl_reg_offset = ACCU_GC2_3D_CLK_CNTRL_REG,
	.enable_value = ACCU_GC3D_INTLCLK,
};

static char *gc3d_fn_depend_clk[] = {
	"GC3D_FN_SHARE",
};

static struct clk_peri_params gc3d_fn_params = {
	.pp_table = gc3d_pp_table,
	.nr_pp = ARRAY_SIZE(gc3d_pp_table),
	.parents = gc3d_parents,
	.nr_parents = ARRAY_SIZE(gc3d_parents),
	.depend_clks_name = gc3d_fn_depend_clk,
	.nr_depend_clks = ARRAY_SIZE(gc3d_fn_depend_clk),
	.enable_value = ACCU_GC3D_FNCLK | ACCU_GC3D_APBCLK | ACCU_GC3D_AHBCLK,
	.src_mask_bits = ACCU_SOURCE_MASK,
	.src_shift = ACCU_SRC_SHIFT,
	.div_mask_bits = ACCU_RATIO_MASK,
	.div_shift = ACCU_RATIO_SHIFT,
};

static char *gc3d_sh_depend_clk[] = {
	"GC3D_SH_SHARE",
};

static struct clk_peri_params gc3d_sh_params = {
	.pp_table = gc3d_pp_table,
	.nr_pp = ARRAY_SIZE(gc3d_pp_table),
	.parents = gc3d_parents,
	.nr_parents = ARRAY_SIZE(gc3d_parents),
	.depend_clks_name = gc3d_sh_depend_clk,
	.nr_depend_clks = ARRAY_SIZE(gc3d_sh_depend_clk),
	.enable_value = ACCU_GC3D_SHADERCLK,
	.src_mask_bits = ACCU_SOURCE_MASK,
	.src_shift = ACCU_SRC_SHIFT,
	.div_mask_bits = ACCU_RATIO_MASK,
	.div_shift = ACCU_RATIO_SHIFT,
};

/* APPS_LSP_APB */
/* APPS_OW */
/* APPS_PWM01 */
/* APPS_PWM23 */
/* APPS_TIMERS1 */
/* APPS_TIMERS2 */
/* APPS_TIMERS3 */

/* APPS_KP: FIXME: find a proper way to access the control register */

/* APPS_RTC: FIXME: find a proper way to access the control register */

/* APPS_SSP1 */
/* APPS_SSP2 */

/* APPS_I2C1, 2, 3, 4, 5 */
static struct clk_peri_pp i2c_pp_table[] = {
	{
		.clk_rate = 32840000,
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
		.div = 0,
	},
	{
		.clk_rate = 69330000,
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
		.div = 1,
	},
};

static struct clk_parent i2c_parents[] = {
	{
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
	},
};

static const char const *i2c_parents_name[] = {
	PLL1_624_APPS,
};

static struct clk_peri_params i2c_params = {
	.pp_table = i2c_pp_table,
	.nr_pp = ARRAY_SIZE(i2c_pp_table),
	.parents = i2c_parents,
	.nr_parents = ARRAY_SIZE(i2c_parents),
	.fixed_div_shift = 20,
};

/* APPS_UART1, 2, 3 */
static struct clk_peri_pp uart_pp_table[] = {
	{
		.clk_rate = 14857000,
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
		.div = 0,
	},
	{
		.clk_rate = 59428000,
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
		.div = 1,
	},
};

static struct clk_parent uart_parents[] = {
	{
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
	},
};

static const char const *uart_parents_name[] = {
	PLL1_624_APPS,
};

static struct clk_peri_params uart_params = {
	.pp_table = uart_pp_table,
	.nr_pp = ARRAY_SIZE(uart_pp_table),
	.parents = uart_parents,
	.nr_parents = ARRAY_SIZE(uart_parents),
	.enable_value = ACCU_FNCLK | ACCU_APBCLK,
	.fixed_div_shift = 20,
};

/* APPS_TEMP_S1, 2, 3 */
static struct clk_peri_params therm_params = {
	.enable_value = ACCU_FNCLK | ACCU_APBCLK |
		ACCU_TEMP_ANALOG_CKEN | ACCU_TEMP_CTL32K_CKEN,
};

/* APPS_CORE */
/* APPS_CORE_TOP */
/* APPS_CORE_CS */

/* SDH, SDH_BUS */
/* FIXME:
 * 1. find a proper way to access the ctrl register for clock 'sdh-card'.
 * 2. add clock for SDH_BUS (AXI7).
 */
DEFINE_SPINLOCK(sdh_lock);
static struct clk_peri_pp sdh_pp_table[] = {
	{
		.clk_rate = 78000000,
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
		.div = 8,
	},
	{
		.clk_rate = 104000000,
		.parent_name = PLL1_416_APPS,
		.src = SYS_PLL1_416,
		.div = 4,
	},
	{
		.clk_rate = 156000000,
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
		.div = 4,
	},
	{
		.clk_rate = 200000000,
		.parent_name = PLL2_800_APPS,
		.src = SYS_PLL2_800,
		.div = 4,
	},
	{
		.clk_rate = 208000000,
		.parent_name = PLL1_416_APPS,
		.src = SYS_PLL1_416,
		.div = 2,
	},
};

static struct clk_parent sdh_parents[] = {
	{
		.parent_name = PLL1_416_APPS,
		.src = SYS_PLL1_416,
	},
	{
		.parent_name = PLL1_624_APPS,
		.src = SYS_PLL1_624,
	},
	{
		.parent_name = PLL2_800_APPS,
		.src = SYS_PLL2_800,
	},
};

static const char const *sdh_parents_name[] = {
	PLL1_416_APPS,
	PLL1_624_APPS,
	PLL2_800_APPS,
};

static char *sdh_depend_clk[] = {
	"SDH-INTERNAL",
};

static struct clk_peri_params sdh_params = {
	.pp_table = sdh_pp_table,
	.nr_pp = ARRAY_SIZE(sdh_pp_table),
	.parents = sdh_parents,
	.nr_parents = ARRAY_SIZE(sdh_parents),
	.depend_clks_name = sdh_depend_clk,
	.nr_depend_clks = ARRAY_SIZE(sdh_depend_clk),
	.ctrl_reg_offset = ACCU_SDH_CLK_CNTRL_REG,
	.src_mask_bits = ACCU_SOURCE_MASK,
	.src_shift = ACCU_SRC_SHIFT,
	.div_mask_bits = ACCU_RATIO_MASK,
	.div_shift = ACCU_RATIO_SHIFT,
};

#ifdef CONFIG_SOUND
static struct clk_audio_params audio_params = {
	.ctrl_offset = MPMU_PLLA_CF1,
	.postdiv_mask = PLLA_POSTDIV_AUDIO_MASK,
	.postdiv = PLLA_POSTDIV_AUDIO(0x8),
	.power_offset = MPMU_AUDSSCR,
	.axi_on_bit = AUD_AXI_EN,
	.power_on_bit = AUD_PWR_ON,
};

static const char const *sspa1_parent[] = {
	PLL1_416_APPS,
	PLL1_500_APPS, PLL1_624_APPS, PLLA_56_AUD
};

static struct clk_factor_masks sspa1_factor_masks = {
	.factor = 1,
	.den_mask = 0xfff,
	.num_mask = 0x7fff,
	.den_shift = 0,
	.num_shift = 15,
};

static struct clk_factor_tbl sspa1_factor_tbl[] = {
	{.num = 0xa, .den = 0x2},	/*44.1kHZ */
};
#endif

/* HSI */
#ifdef CONFIG_SOUND
void __init pxa1986_audio_clk_init(void)
{
	struct clk *clk, *apll_clk;

	clk = clk_register_fixed_rate(NULL, PLLA_451_AUD,
					NULL, CLK_IS_ROOT, 451584000);
	BUG_ON(!clk);
	clk_register_clkdev(clk, PLLA_451_AUD, NULL);

	apll_clk = clk_register_audio_pll(PLLA_56_AUD, PLLA_451_AUD,
		CLK_SET_RATE_PARENT, MMP_CLK_CTRL_REG_FROM_MPMU,
		56448000UL, &mpmu_aud_clk_req_lock,
		&audio_params);
	BUG_ON(!apll_clk);
	clk_register_clkdev(apll_clk, PLLA_56_AUD, NULL);
	clk_prepare_enable(apll_clk);
	usleep_range(2000, 3000);

	clk = clk_register_mux(NULL, "sspa1_mux", sspa1_parent,
			ARRAY_SIZE(sspa1_parent), 0,
			get_base_va(MMP_CLK_CTRL_REG_FROM_MPMU) +
			MPMU_AUD_PLLREQ,
			17, 1, 0, &mpmu_aud_clk_req_lock);
	clk_set_parent(clk, apll_clk);
	clk_register_clkdev(clk, "sspa1_mux.1", NULL);
	usleep_range(2000, 3000);

	clk =  mmp_clk_register_factor(SSPA1_MN_DIV, PLLA_56_AUD, 0,
				get_base_va(MMP_CLK_CTRL_REG_FROM_AUDIO) +
				0x1008,
				&sspa1_factor_masks, sspa1_factor_tbl,
				ARRAY_SIZE(sspa1_factor_tbl));
	clk_set_rate(clk, 11289600);
	clk_register_clkdev(clk, SSPA1_MN_DIV, NULL);

	clk = clk_register_gate(NULL, "mmp-sspa-dai.0", SSPA1_MN_DIV, 0,
				get_base_va(MMP_CLK_CTRL_REG_FROM_AUDIO) +
				0x1028, 2,
				0, &mpmu_aud_clk_req_lock);
	clk_register_clkdev(clk, NULL, "mmp-sspa-dai.0");

}
#endif

/* func to register clock sources and peripheral clocks */
void __init pxa1986_clk_init(void)
{
	int i;
	u32 reg_val;
	unsigned long mmp_flags;
	struct clk *clk;

	/* slave config registers for APMU PLLs */
	for (i = 0; i < 4; i++) {
		core_pll1_cfg_regs[i].addr = apmu_base_va() +
			core_pll1_cfg_regs[i].offset;
		core_pll2_cfg_regs[i].addr = apmu_base_va() +
			core_pll2_cfg_regs[i].offset;
		pll3_cfg_regs[i].addr = apmu_base_va() +
			pll3_cfg_regs[i].offset;
		pll4_cfg_regs[i].addr = apmu_base_va() +
			pll4_cfg_regs[i].offset;
	}

	/* slave config registers for DDR PLL */
	for (i = 0; i < 3; i++)
		plld_cfg_regs[i].addr = mpmu_base_va() +
			plld_cfg_regs[i].offset;

	clk = clk_register_fixed_rate(NULL, CLK_32K, NULL,
		CLK_IS_ROOT, CLK_32K_RATE);
	clk_register_clkdev(clk, CLK_32K, NULL);

	clk = clk_register_fixed_rate(NULL, CLK_VCXO, NULL,
		CLK_IS_ROOT, CLK_VCXO_RATE);
	clk_register_clkdev(clk, CLK_VCXO, NULL);

	clk = clk_register_fixed_rate(NULL, USB_PLL, NULL,
		CLK_IS_ROOT, USB_PLL_RATE);
	clk_register_clkdev(clk, USB_PLL, NULL);

	/* fixed clock source */
	mmp_flags = MMP_CLK_CTRL_REG_FROM_MPMU |
		MMP_CLK_PLL_CHECK_REQ_MODE | MMP_CLK_PLL_CHECK_LOCKED;

	clk = clk_register_fixed_src(MPMU_APPS_PLL1, NULL, CLK_IS_ROOT,
		mmp_flags | MMP_CLK_PLL_ALWAYS_ON, PLL1_RATE,
		&mpmu_ap_clk_req_lock, &mpmu_ap_pll1_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, MPMU_APPS_PLL1, NULL);

	clk = clk_register_fixed_src(MPMU_APPS_PLL2, NULL, CLK_IS_ROOT,
		mmp_flags, PLL2_RATE, &mpmu_ap_clk_req_lock,
		&mpmu_ap_pll2_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, MPMU_APPS_PLL2, NULL);

	clk = clk_register_fixed_src(MPMU_SRD_PLL1, NULL, CLK_IS_ROOT,
		mmp_flags, PLL1_RATE, &mpmu_srd_clk_req_lock,
		&mpmu_srd_pll1_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, MPMU_SRD_PLL1, NULL);

	clk = clk_register_fixed_src(MPMU_SRD_PLL2, NULL, CLK_IS_ROOT,
		mmp_flags, PLL2_RATE, &mpmu_srd_clk_req_lock,
		&mpmu_srd_pll2_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, MPMU_SRD_PLL2, NULL);

	clk = clk_register_fixed_src(MPMU_AUD_PLL1, NULL, CLK_IS_ROOT,
		mmp_flags, PLL1_RATE, &mpmu_aud_clk_req_lock,
		&mpmu_aud_pll1_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, MPMU_AUD_PLL1, NULL);

	clk = clk_register_fixed_src(PLL1_416_APPS, MPMU_APPS_PLL1, 0,
		MMP_CLK_CTRL_REG_FROM_MCCU, PLL1_416_RATE,
		&mccu_apps_clk_req_lock, &mccu_ap_pll1_416_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, PLL1_416_APPS, NULL);

	clk = clk_register_fixed_src(PLL1_500_APPS, MPMU_APPS_PLL1, 0,
		MMP_CLK_CTRL_REG_FROM_MCCU, PLL1_500_RATE,
		&mccu_apps_clk_req_lock, &mccu_ap_pll1_500_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, PLL1_500_APPS, NULL);

	clk = clk_register_fixed_src(PLL1_624_APPS, MPMU_APPS_PLL1, 0,
		MMP_CLK_CTRL_REG_FROM_MCCU, PLL1_624_RATE,
		&mccu_apps_clk_req_lock, &mccu_ap_pll1_624_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, PLL1_624_APPS, NULL);

	clk = clk_register_fixed_src(PLL2_800_APPS, MPMU_APPS_PLL2, 0,
		MMP_CLK_CTRL_REG_FROM_MCCU, PLL2_RATE,
		&mccu_apps_clk_req_lock, &mccu_ap_pll2_800_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, PLL2_800_APPS, NULL);

	clk = clk_register_fixed_src(PLL1_416_SRD, MPMU_SRD_PLL1, 0, 0,
		PLL1_416_RATE, NULL, NULL);
	BUG_ON(!clk);
	clk_register_clkdev(clk, PLL1_416_SRD, NULL);

	clk = clk_register_fixed_src(PLL1_500_SRD, MPMU_SRD_PLL1, 0, 0,
		PLL1_500_RATE, NULL, NULL);
	BUG_ON(!clk);
	clk_register_clkdev(clk, PLL1_500_SRD, NULL);

	clk = clk_register_fixed_src(PLL1_624_SRD, MPMU_SRD_PLL1, 0, 0,
		PLL1_624_RATE, NULL, NULL);
	BUG_ON(!clk);
	clk_register_clkdev(clk, PLL1_624_SRD, NULL);

	clk = clk_register_fixed_src(PLL2_800_SRD, MPMU_SRD_PLL2, 0, 0,
		PLL2_RATE, NULL, NULL);
	BUG_ON(!clk);
	clk_register_clkdev(clk, PLL2_800_SRD, NULL);

	clk = clk_register_fixed_src(PLL1_416_AUD, MPMU_AUD_PLL1, 0,
		MMP_CLK_CTRL_REG_FROM_MCCU, PLL1_416_RATE,
		&mccu_aud_clk_req_lock, &mccu_aud_pll1_416_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, PLL1_416_AUD, NULL);

	clk = clk_register_fixed_src(PLL1_500_AUD, MPMU_AUD_PLL1, 0,
		MMP_CLK_CTRL_REG_FROM_MCCU, PLL1_500_RATE,
		&mccu_aud_clk_req_lock, &mccu_aud_pll1_500_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, PLL1_500_AUD, NULL);

	clk = clk_register_fixed_src(PLL1_624_AUD, MPMU_AUD_PLL1, 0,
		MMP_CLK_CTRL_REG_FROM_MCCU, PLL1_624_RATE,
		&mccu_aud_clk_req_lock, &mccu_aud_pll1_624_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, PLL1_624_AUD, NULL);

	/* configurable clock source */
	mmp_flags = MMP_CLK_CTRL_REG_FROM_APMU | MMP_CLK_PLL_CHECK_REQ_MODE;

	clk = clk_register_config_src(CORE_PLL1, CLK_IS_ROOT, mmp_flags,
		&apmu_pll_req_lock, &core_pll1_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, CORE_PLL1, NULL);

	clk = clk_register_config_src(CORE_PLL2, CLK_IS_ROOT, mmp_flags,
		&apmu_pll_req_lock, &core_pll2_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, CORE_PLL2, NULL);

	clk = clk_register_config_src(PLL3, CLK_IS_ROOT, mmp_flags,
		&apmu_pll_req_lock, &pll3_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, PLL3, NULL);

	clk = clk_register_config_src(PLL4, CLK_IS_ROOT, mmp_flags,
		&apmu_pll_req_lock, &pll4_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, PLL4, NULL);

	clk = clk_register_config_src(DDR_PLL, CLK_IS_ROOT,
		MMP_CLK_CTRL_REG_FROM_MPMU | MMP_CLK_PLL_CHECK_LOCKED,
		&mpmu_srd_clk_req_lock, &ddr_pll_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, DDR_PLL, NULL);

	/* register peripheral clocks */
	mmp_flags = MMP_CLK_CTRL_REG_FROM_MCCU | MMP_CLK_SRC_FROM_SRD_SS;

	i2c_params.ctrl_reg_offset = MCCU_I2CCR;
	i2c_params.enable_value = MCCU_I2C_FNC_CKEN | MCCU_I2C_BUS_CKEN;
	clk = clk_register_peri("twsi0", i2c_parents_name, 1, 0,
		mmp_flags | MMP_CLK_USE_FIXED_DIV, NULL, &i2c_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "pxa910-i2c.0");

	mmp_flags = MMP_CLK_CTRL_REG_FROM_ACCU | MMP_CLK_SRC_FROM_APPS_SS;

	i2c_params.ctrl_reg_offset = ACCU_APPS_I2C1_CLK_CNTRL_REG;
	i2c_params.enable_value = ACCU_FNCLK | ACCU_APBCLK;
	clk = clk_register_peri("twsi1", i2c_parents_name, 1, 0,
		mmp_flags | MMP_CLK_USE_FIXED_DIV, NULL, &i2c_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "pxa910-i2c.1");

	i2c_params.ctrl_reg_offset = ACCU_APPS_I2C2_CLK_CNTRL_REG;
	clk = clk_register_peri("twsi2", i2c_parents_name, 1, 0,
		mmp_flags | MMP_CLK_USE_FIXED_DIV, NULL, &i2c_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "pxa910-i2c.2");

	i2c_params.ctrl_reg_offset = ACCU_APPS_I2C3_CLK_CNTRL_REG;
	clk = clk_register_peri("twsi3", i2c_parents_name, 1, 0,
		mmp_flags | MMP_CLK_USE_FIXED_DIV, NULL, &i2c_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "pxa910-i2c.3");

	i2c_params.ctrl_reg_offset = ACCU_APPS_I2C4_CLK_CNTRL_REG;
	clk = clk_register_peri("twsi4", i2c_parents_name, 1, 0,
		mmp_flags | MMP_CLK_USE_FIXED_DIV, NULL, &i2c_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "pxa910-i2c.4");

	i2c_params.ctrl_reg_offset = ACCU_APPS_I2C5_CLK_CNTRL_REG;
	clk = clk_register_peri("twsi5", i2c_parents_name, 1, 0,
		mmp_flags | MMP_CLK_USE_FIXED_DIV, NULL, &i2c_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "pxa910-i2c.5");

	/* FIXME: need control AXI5 & AXI6 by clock framework */
	reg_val = readl_relaxed(accu_base_va() + ACCU_FABRIC_N5_CLK_CNTRL_REG);
	writel_relaxed(reg_val | ACCU_FNCLK | ACCU_APBCLK,
		accu_base_va() + ACCU_FABRIC_N5_CLK_CNTRL_REG);

	reg_val = readl_relaxed(accu_base_va() + ACCU_FABRIC_N6_CLK_CNTRL_REG);
	writel_relaxed(reg_val | ACCU_FNCLK,
		accu_base_va() + ACCU_FABRIC_N6_CLK_CNTRL_REG);

	clk = clk_register_peri("gc2d", gc2d_parents_name,
		ARRAY_SIZE(gc2d_parents_name), 0, mmp_flags,
		&gc2d_lock, &gc2d_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, "GC2D_CLK", NULL);

	clk = clk_register_peri("gc3d_fn_share", NULL, 0, 0,
		MMP_CLK_CTRL_REG_FROM_ACCU | MMP_CLK_ONLY_ON_OFF_OPS,
		&gc3d_lock, &gc3d_fn_share_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, "GC3D_FN_SHARE", NULL);

	clk = clk_register_peri("gc3d_sh_share", NULL, 0, 0,
		MMP_CLK_CTRL_REG_FROM_ACCU | MMP_CLK_ONLY_ON_OFF_OPS,
		&gc3d_lock, &gc3d_sh_share_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, "GC3D_SH_SHARE", NULL);

	gc3d_fn_params.ctrl_reg_offset = ACCU_GC1_3D_CLK_CNTRL_REG,
	clk = clk_register_peri("gc3d1_fn", gc3d_parents_name,
		ARRAY_SIZE(gc3d_parents_name), 0, mmp_flags, NULL,
		&gc3d_fn_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, "GC3D1_CLK", NULL);

	gc3d_fn_params.ctrl_reg_offset = ACCU_GC2_3D_CLK_CNTRL_REG,
	clk = clk_register_peri("gc3d2_fn", gc3d_parents_name,
		ARRAY_SIZE(gc3d_parents_name), 0, mmp_flags, NULL,
		&gc3d_fn_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, "GC3D2_CLK", NULL);

	gc3d_sh_params.ctrl_reg_offset = ACCU_GC1_3D_CLK_CNTRL_REG,
	clk = clk_register_peri("gc3d1_sh", gc3d_parents_name,
		ARRAY_SIZE(gc3d_parents_name), 0, mmp_flags, NULL,
		&gc3d_sh_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, "GC3D1_SHADER_CLK", NULL);

	gc3d_fn_params.ctrl_reg_offset = ACCU_GC2_3D_CLK_CNTRL_REG,
	clk = clk_register_peri("gc3d2_sh", gc3d_parents_name,
		ARRAY_SIZE(gc3d_parents_name), 0, mmp_flags,
		NULL, &gc3d_sh_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, "GC3D2_SHADER_CLK", NULL);

	gc3d_fn_params.ctrl_reg_offset = ACCU_GC1_3D_CLK_CNTRL_REG,
	gc3d_fn_params.ctrl_reg2_offset = ACCU_GC2_3D_CLK_CNTRL_REG,
	clk = clk_register_peri("gc3d_fn", gc3d_parents_name,
		ARRAY_SIZE(gc3d_parents_name), 0,
		mmp_flags | MMP_CLK_HAS_TWO_CTRL_REGS,
		NULL, &gc3d_fn_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, "GC3D_CLK", NULL);

	gc3d_sh_params.ctrl_reg_offset = ACCU_GC1_3D_CLK_CNTRL_REG,
	gc3d_sh_params.ctrl_reg2_offset = ACCU_GC2_3D_CLK_CNTRL_REG,
	clk = clk_register_peri("gc3d_sh", gc3d_parents_name,
		ARRAY_SIZE(gc3d_parents_name), 0,
		mmp_flags | MMP_CLK_HAS_TWO_CTRL_REGS,
		NULL, &gc3d_sh_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, "GC3D_SHADER_CLK", NULL);

	pxa1986_disp_clk_init();

	clk = clk_register_fixed_rate(NULL, "rtc", CLK_32K, 0, CLK_32K_RATE);
	clk_register_clkdev(clk, NULL, "sa1100-rtc");

	clk = clk_register_fixed_rate(NULL, "kpc", CLK_32K, 0, CLK_32K_RATE);
	clk_register_clkdev(clk, NULL, "pxa27x-keypad");

	uart_params.ctrl_reg_offset = ACCU_APPS_UART1_CLK_CNTRL_REG;
	clk = clk_register_peri("uart0", uart_parents_name, 1, 0,
		mmp_flags | MMP_CLK_USE_FIXED_DIV, NULL, &uart_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "pxa2xx-uart.0");

	uart_params.ctrl_reg_offset = ACCU_APPS_UART2_CLK_CNTRL_REG;
	clk = clk_register_peri("uart1", uart_parents_name, 1, 0,
		mmp_flags | MMP_CLK_USE_FIXED_DIV, NULL, &uart_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "pxa2xx-uart.1");

	uart_params.ctrl_reg_offset = ACCU_APPS_UART3_CLK_CNTRL_REG;
	clk = clk_register_peri("uart2", uart_parents_name, 1, 0,
		mmp_flags | MMP_CLK_USE_FIXED_DIV, NULL, &uart_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "pxa2xx-uart.2");

	clk = clk_register_gate(NULL, "sdh-internal", NULL, 0,
		accu_base_va() + ACCU_SDH_CLK_CNTRL_REG, ACCU_SDH_INT_CKEN,
		0, &sdh_lock);
	BUG_ON(!clk);
	clk_register_clkdev(clk, "SDH-INTERNAL", NULL);

	clk = clk_register_gate(NULL, "sdh-card", CLK_32K, 0,
		accu_base_va() + ACCU_SDH_CLK_CNTRL_REG, ACCU_SDH_CARD_INS_CKEN,
		0, &sdh_lock);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "SDH-CARD");

	sdh_params.enable_value = ACCU_SDH_MASTER_BUS_CKEN |
		ACCU_SDH_SLAVE_BUS_CKEN;
	clk = clk_register_peri("sdh-mst-slv", sdh_parents_name,
		ARRAY_SIZE(sdh_parents_name), 0, mmp_flags,
		&sdh_lock, &sdh_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, "SDH-MASTER-SLAVE", NULL);

	sdh_params.enable_value = ACCU_SDH_CKEN(1);
	clk = clk_register_peri("sdh1", sdh_parents_name,
		ARRAY_SIZE(sdh_parents_name), 0, mmp_flags,
		&sdh_lock, &sdh_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "sdhci-pxav3.0");

	sdh_params.enable_value = ACCU_SDH_CKEN(2);
	clk = clk_register_peri("sdh2", sdh_parents_name,
		ARRAY_SIZE(sdh_parents_name), 0, mmp_flags,
		&sdh_lock, &sdh_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "sdhci-pxav3.1");

	sdh_params.enable_value = ACCU_SDH_CKEN(3);
	clk = clk_register_peri("sdh3", sdh_parents_name,
		ARRAY_SIZE(sdh_parents_name), 0, mmp_flags,
		&sdh_lock, &sdh_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "sdhci-pxav3.2");

	sdh_params.enable_value = ACCU_SDH_CKEN(4);
	clk = clk_register_peri("sdh4", sdh_parents_name,
		ARRAY_SIZE(sdh_parents_name), 0, mmp_flags,
		&sdh_lock, &sdh_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "sdhci-pxav3.3");

	clk_register_clkdev(clk, NULL, "mv-udc");
	clk_register_clkdev(clk, NULL, "mv-otg");
	clk_register_clkdev(clk, NULL, "pxa1986-usb-phy");

	therm_params.ctrl_reg_offset = ACCU_APPS_TEMP_S1_CLK_CNTRL_REG;
	clk = clk_register_peri("thermal0", NULL, 0, 0,
		MMP_CLK_CTRL_REG_FROM_ACCU | MMP_CLK_ONLY_ON_OFF_OPS,
		NULL, &therm_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "pxa1986-thermal.0");

	therm_params.ctrl_reg_offset = ACCU_APPS_TEMP_S2_CLK_CNTRL_REG;
	clk = clk_register_peri("thermal1", NULL, 0, 0,
		MMP_CLK_CTRL_REG_FROM_ACCU | MMP_CLK_ONLY_ON_OFF_OPS,
		NULL, &therm_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "pxa1986-thermal.1");

	therm_params.ctrl_reg_offset = ACCU_APPS_TEMP_S3_CLK_CNTRL_REG;
	clk = clk_register_peri("thermal2", NULL, 0, 0,
		MMP_CLK_CTRL_REG_FROM_ACCU | MMP_CLK_ONLY_ON_OFF_OPS,
		NULL, &therm_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, NULL, "pxa1986-thermal.2");

	clk = clk_register_peri("axi2", vpu_parents_name,
		ARRAY_SIZE(vpu_parents_name), 0,
		mmp_flags, NULL, &axi2_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, "AXI2_CLK", NULL);

	clk = clk_register_peri("vpu_dec", vpu_parents_name,
		ARRAY_SIZE(vpu_parents_name), 0,
		mmp_flags, NULL, &vpudec_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, "VPU_DEC_CLK", NULL);

	clk = clk_register_peri("vpu_enc", vpu_parents_name,
		ARRAY_SIZE(vpu_parents_name), 0,
		mmp_flags | MMP_CLK_HAS_TWO_CTRL_REGS,
		NULL, &vpuenc_params);
	BUG_ON(!clk);
	clk_register_clkdev(clk, "VPU_ENC_CLK", NULL);

#ifdef CONFIG_SOUND
	pxa1986_audio_clk_init();
#endif
}

void hantro_power_switch(int on)
{
	/* FIXME: currently always enable the power of video subsystem */
	writel_relaxed(0x1, apmu_base_va() + APMU_VIDEO_PWR_CTRL);
	while (!(readl_relaxed(apmu_base_va() + APMU_VIDEO_PWR_STATUS)&
		VIDEO_SS_PWR_ON))
		;
}
EXPORT_SYMBOL_GPL(hantro_power_switch);
