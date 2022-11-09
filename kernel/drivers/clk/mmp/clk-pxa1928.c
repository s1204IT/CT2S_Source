/*
 * pxa1928 clock framework source file
 *
 * Copyright (C) 2013 Marvell
 * Guoqing <ligq@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/devfreq.h>
#include <linux/mmp_dspaux.h>
#include <linux/debugfs-pxa.h>
#include <linux/cputype.h>
#include <linux/clk/mmp.h>

#include "clk.h"

#define APBC_RTC	0x0
#define APBC_TWSI0	0x4
#define APBC_TWSI1	0x8
#define APBC_TWSI2	0xc
#define APBC_TWSI3	0x10
#define APBC_TWSI4	0x7c
#define APBC_TWSI5	0x80
#define APBC_KPC	0x18
#define APBC_UART0	0x2c
#define APBC_UART1	0x30
#define APBC_UART2	0x34
#define APBC_UART3	0x88
#define APBC_GPIO	0x38
#define APBC_PWM0	0x3c
#define APBC_PWM1	0x40
#define APBC_PWM2	0x44
#define APBC_PWM3	0x48
#define APBC_SSP0	0x4c
#define APBC_SSP1	0x50
#define APBC_SSP2	0x54
#define APBC_SSP3	0x58
#define APBC_SSP4	0x5c
#define APBC_SSP5	0x60
#define APBC_THSENS	0x90 /* Thermal Sensor */

#define APBC_RIPC_CLK_RST	0x8c
#define APBC_IPC_CP_CLK_RST	0xac

#define APMU_SDH0	0x54
#define APMU_SDH1	0x58
#define APMU_SDH2	0xe8
#define APMU_SDH3	0xec
#define APMU_SDH4	0x15c
#define APMU_USB	0x5c
#define APMU_HSIC	0xf8
#define APMU_DISP0	0x4c
#define APMU_DISP1	0x110
#define APMU_CCIC0	0x50
#define APMU_CCIC1	0xf4
#define APMU_GC		0xcc
#define APMU_GC2	0x27c
#define APMU_VPU	0xa4
#define APMU_COREAPSS	0x2e0
#define APMU_APDBG	0x340
#define APMU_AP_DEBUG1	0x38c
#define APMU_SMC	0xd4

#define MPMU_UART_PLL	0x14

#define POSR_PLL2_LOCK		(1 << 29)
#define POSR_PLL3_LOCK		(1 << 14)
#define POSR_PLL4_LOCK		(1 << 15)
#define POSR_PLL5_LOCK		(1 << 16)
#define POSR_PLL6_LOCK		(1 << 14)
#define POSR_PLL7_LOCK		(1 << 17)

#define APMU_GLB_CLK_CTRL	(0x0dc)
#define APMU_MC_CLK_RES_CTRL	(0x258)
#define MPMU_VRCR	(0x0018)
#define MPMU_PLL2CR	(0x0034)
#define MPMU_PLL3CR	(0x0050)
#define MPMU_PLL4CR	(0x1100)
#define MPMU_PLL5CR	(0x1114)
#define MPMU_PLL6CR	(0x1300)
#define MPMU_PLL7CR	(0x1400)
#define MPMU_POSR	(0x0010)
#define MPMU_POSR2	(0x0054)
#define MPMU_POSR3	(0x0074)
#define MPMU_PLL2_CTRL1	(0x0414)
#define MPMU_PLL3_CTRL1	(0x0058)
#define MPMU_PLL4_CTRL1	(0x1104)
#define MPMU_PLL5_CTRL1	(0x1118)
#define MPMU_PLL6_CTRL1	(0x1304)
#define MPMU_PLL7_CTRL1	(0x1404)
#define MPMU_PLL2_CTRL2	(0x0418)
#define MPMU_PLL3_CTRL2	(0x0060)
#define MPMU_PLL4_CTRL2	(0x1108)
#define MPMU_PLL5_CTRL2	(0x111c)
#define MPMU_PLL6_CTRL2	(0x1308)
#define MPMU_PLL7_CTRL2	(0x1408)
#define MPMU_PLL2_CTRL3	(0x041c)
#define MPMU_PLL3_CTRL3	(0x0064)
#define MPMU_PLL4_CTRL3	(0x110c)
#define MPMU_PLL5_CTRL3	(0x1120)
#define MPMU_PLL6_CTRL3	(0x130c)
#define MPMU_PLL7_CTRL3	(0x140c)
#define MPMU_PLL2_CTRL4	(0x0068)
#define MPMU_PLL3_CTRL4	(0x006c)
#define MPMU_PLL4_CTRL4	(0x1110)
#define MPMU_PLL5_CTRL4	(0x1124)
#define MPMU_PLL6_CTRL4	(0x1310)
#define MPMU_PLL7_CTRL4	(0x1410)
#define APMU_AP_PLL_CTRL	(0x0348)
#define GATE_CTRL_SHIFT_PLL2	(22)
#define GATE_CTRL_SHIFT_PLL3	(24)
#define GATE_CTRL_SHIFT_PLL4	(26)
#define GATE_CTRL_SHIFT_PLL5	(28)
#define GATE_CTRL_SHIFT_PLL6	(30)
#define GATE_CTRL_SHIFT_PLL7	(16)

#define SW_EN_SHIFT	(8)
#define SW_DISABLE	(2)
#define FBDIV_SHIFT	(10)
#define REFDIV_SHIFT	(19)
#define POSTDIV_SHIFT	(26)
#define PPOSTDIV_SHIFT	(5)
#define P_ENABLE_SHIFT	(9)
#define CTRL1_PLL_BIT (1 << 30)

#define APMU_DISP1_CLK_CTRL (0x004c)
#define APMU_DISP2_CLK_CTRL (0x0110)
#define LCD_PN_SCLK	(0xd420b1a8)

/* APMU of PXA1928 Ax */
#define APMU_DISP_RST_CTRL	(0x180)
#define APMU_DISP_CLK_CTRL	(0x184)
#define APMU_DISP_CLK_CTRL2	(0x188)
#define APMU_ISLD_LCD_CTRL	(0x1ac)
#define APMU_DISP_PWR_CTRL	(0x204)

#define APMU_ISLD_ISP_CTRL	(0x1a4)
#define APMU_ISP_RSTCTRL	(0x1E0)
#define APMU_ISP_CLKCTRL	(0x1E4)
#define APMU_ISP_CLKCTRL2	(0x1E8)
#define APMU_ISP_PWR_CTRL	(0x1fc)
#define APMU_ISLD_ISP_PWRCTRL	(0x214)

#define APMU_GC3D_RSTCTRL	(0x170)
#define APMU_GC3D_CLKCTRL	(0x174)
#define APMU_ISLD_GC_CTRL	(0x1b4)
#define APMU_ISLD_GC3D_PWRCTRL	(0x20c)

#define APMU_GC2D_RSTCTRL	(0x178)
#define APMU_GC2D_CLKCTRL	(0x17c)
#define APMU_ISLD_GC2D_CTRL	(0x1b8)
#define APMU_ISLD_GC2D_PWRCTRL	(0x210)
#define APMU_AP_DEBUG3		(0x394)

#define APMU_ISLD_VPU_CTRL	(0x1b0)
#define APMU_VPU_RSTCTRL	(0x1f0)
#define APMU_VPU_CLKCTRL	(0x1f4)
#define APMU_ISLD_VPU_PWRCTRL	(0x208)

#define APMU_AUDIO_PWR_UP		(3 << 9)
#define APMU_AUDIO_PWR_DOWN		(0 << 9)
#define APMU_AUDIO_ISO_DIS		(1 << 8)
#define APMU_AUDIO_CLK_ENA		(1 << 4)
#define APMU_AUDIO_RST_DIS		(1 << 1)

#define CIU_LCD_CKGT_CTRL	(0x38)
#define CIU_FABRIC1_CKGT_CTRL0	(0x64)
#define CIU_ISP_CKGT_CTRL	(0x68)
#define CIU_GC3D_CKGT_CTRL	(0x3c)
#define CIU_VPU_CKGT_CTRL	(0x6c)

struct pll_tbl {
	spinlock_t *lock;
	const char *vco_name;
	const char *out_name;
	const char *outp_name;
	unsigned long vco_flags;
	unsigned long out_flags;
	unsigned long outp_flags;
	struct pxa1928_clk_pll_vco_table  *vco_tbl;
	struct pxa1928_clk_pll_out_table  *out_tbl;
	struct pxa1928_clk_pll_out_table  *outp_tbl;
	struct pxa1928_clk_pll_vco_params vco_params;
	struct pxa1928_clk_pll_out_params out_params;
	struct pxa1928_clk_pll_out_params outp_params;
};

static DEFINE_SPINLOCK(clk_lock);
static DEFINE_SPINLOCK(pll2_lock);
static DEFINE_SPINLOCK(pll3_lock);
static DEFINE_SPINLOCK(pll4_lock);
static DEFINE_SPINLOCK(pll5_lock);
static DEFINE_SPINLOCK(pll6_lock);
static DEFINE_SPINLOCK(pll7_lock);
static DEFINE_SPINLOCK(disp_lock);
static DEFINE_SPINLOCK(gc3d_lock);
static DEFINE_SPINLOCK(gc2d_lock);
static DEFINE_SPINLOCK(ssp1_lock);
#ifdef CONFIG_UIO_HANTRO
static DEFINE_SPINLOCK(vpu_lock);
#endif

#define REG_WIDTH_1BIT	1
#define REG_WIDTH_2BIT	2
#define REG_WIDTH_3BIT	3
#define REG_WIDTH_4BIT	4
#define REG_WIDTH_5BIT	5
#define REG_WIDTH_16BIT	16


/* use to save some important clk ptr */
enum pxa1928x_clk {
	cpu = 0, ddr, axi, gc3d_aclk, gc2d_aclk,
	gc3d_1x, gc3d_2x, gc2d,
	vpu_aclk, vpu_dec, vpu_enc, isp,
	clk_max,
};
static struct clk *clks[clk_max];

static struct clk_factor_masks uart_factor_masks = {
	.factor = 2,
	.num_mask = 0x1fff,
	.den_mask = 0x1fff,
	.num_shift = 16,
	.den_shift = 0,
};

static struct clk_factor_tbl uart_factor_tbl[] = {
	{.num = 832, .den = 234},	/*58.5MHZ */
	{.num = 1, .den = 1},		/*26MHZ */
};

int pxa1928_is_zx;
int stepping_is_a0;
int pp_is_1926;

static const char *uart_parent[] = {"uart_pll", "vctcxo"};
static const char *disp_axi_sclk_parent[] = {"pll1_d2", "pll1_416", "pll1_624", "pll5"};
static const char *disp_axi_parent[] = {"disp_axi_sclk"};
static const char *ssp_parent[] = {"vctcxo_d4", "vctcxo_d2",
					"vctcxo", "pll1_d12"};
static const char *disp1_parent[] = {"pll1_624", "pll5p", "pll5", "pll1_416"};
static const char *disp1_parent_ax[] = {"pll1_624", "pll1_416"};
static const char *pnsclk_parent[] = {"disp1", "pll3"};
static const char *pnpath_parent[] = {"pn_sclk"};
static const char *dsi_parent[] = {"dsi_sclk"};
static const char *pnsclk_depend[] = {"LCDCIHCLK", "vdma_axi"};
static const char *pnsclk_depend_ax[] = {"LCDCIHCLK"};
static const char *rst_ctrl_depend[] = {"disp_axi", "vdma_axi",
	"disp_peri", "dsi_phy_slow"};
static const char *dsi_depend[] = {"dsi_phy_slow", "LCDCIHCLK"};
static const char *dsi_depend_ax[] = {"LCDCIHCLK"};
#ifdef CONFIG_VIDEO_MMP
static const char *ccic_fn_parent[] = {"pll1_624", "pll5p", "pll5", "pll1_416"};
static const char *ccic_sphy_parent[] = {"vctcxo"};
#endif
#ifdef CONFIG_VIDEO_MV_SC2_MMU
static const char *sc2_mclk_parent[] = {"pll1_624", "vctcxo"};
static const char *sc2_csi_parent[] = {"pll1_624", "pll7", "pll1_416", "pll5"};
/* sc2 axi first parent should be pll1_312*/
static const char *sc2_axi_parent[] = {"pll1_624", "pll7", "pll1_416", "pll5"};
static const char *sc2_4x_parent[] = {"pll1_624", "pll7", "pll1_416", "pll5"};
#endif
static u32 pnsclk_parent_tbl[] = {1, 7};
static u32 pnsclk_parent_tbl_ax[] = {1, 3};
#ifdef CONFIG_SOUND
static const char *sspa1_parent1[] = {
	"apll1_slow", "apll1_fast",
	"apll2_slow", "apll2_fast",
	"ext_pll_clk1", "ext_pll_clk2"
};
static const char *sspa1_parent2[] = {"sspa1_mn_div", "vctcxo"};
static const char *sspa1_parent3[] = {
	"i2s_ptk_apb_clk", "sspa1_clk_src"
};
static const char *sysclk_parent[] = {"sspa1_div_clk", "sspa2_div_clk"};

static const char * const pmu_sram_parent[] = {"apll1_mux", "vctcxo"};

static const char * const pmu_apb_parent[] = {"apll1_mux", "vctcxo"};
#endif
static const char *sdh_parent[] = {"pll1_624", "pll5p", "pll5", "pll1_416"};

static struct pxa1928_clk_pll_vco_table pll2_vco_table[] = {
	/* input  out  out_offsted refdiv fbdiv icp kvco offset_en*/
	{26000000, 1594666666, 1594666666, 3, 46, 3, 0xa, 0},
	{26000000, 2114000000, 2114000000, 3, 61, 3, 0xc, 0},
	{26000000, 2772000000U, 2772000000, 3, 80, 3, 0xf, 0},
	{0, 0, 0, 0, 0, 0, 0, 0}
};

static struct pxa1928_clk_pll_vco_table pll3_vco_table[] = {
	/* input  out  out_offsted refdiv fbdiv icp kvco offset_en*/
	{26000000, 1768000000, 1768000000, 3, 51, 3, 0xb, 0},
	{0, 0, 0, 0, 0, 0, 0, 0}
};

static struct pxa1928_clk_pll_vco_table pll4_vco_table[] = {
	/* input  out  out_offsted refdiv fbdiv icp kvco offset_en*/
	{26000000, 2114666666, 2133333333, 3, 61, 3, 0xc, 0},
	{0, 0, 0, 0, 0, 0, 0, 0}
};

static struct pxa1928_clk_pll_vco_table pll5_vco_table[] = {
	/* input  out  out_offsted refdiv fbdiv icp kvco offset_en*/
	{26000000, 1594666666, 1594666666, 3, 46, 3, 0xa, 0},
	{0, 0, 0, 0, 0, 0, 0, 0}
};

static struct pxa1928_clk_pll_vco_table pll6_vco_table[] = {
	/* input  out  out_offsted refdiv fbdiv icp kvco offset_en*/
	{26000000, 2114666666, 2133333333, 3, 61, 3, 0xc, 0},
	{0, 0, 0, 0, 0, 0, 0, 0}
};

static struct pxa1928_clk_pll_vco_table pll7_vco_table[] = {
	/* input  out  out_offsted refdiv fbdiv icp kvco offset_en*/
	{26000000, 2114666666, 2133333333, 3, 61, 3, 0xc, 0},
	{0, 0, 0, 0, 0, 0, 0, 0}
};

static struct pxa1928_clk_pll_out_table pll2_out_table[] = {
	/* input_rate output_rate div_sel */
	{1594666666, 797333333, 1},
	{2114000000, 1057000000, 1},
	{2772000000U, 1386000000, 1},
	{0, 0, 0},
};

static struct pxa1928_clk_pll_out_table pll2_outp_table[] = {
	/* input_rate output_rate div_sel */
	{1594666666, 531555555, PXA1928_PLL_DIV_3},
	{0, 0, 0},
};

static struct pxa1928_clk_pll_out_table pll3_out_table[] = {
	/* input_rate output_rate div_sel */
	{1768000000, 884000000, 1},
	{0, 0, 0},
};

static struct pxa1928_clk_pll_out_table pll3_outp_table[] = {
	/* input_rate output_rate div_sel */
	{1768000000, 442000000, 2},
	{0, 0, 0},
};

static struct pxa1928_clk_pll_out_table pll4_out_table[] = {
	/* input_rate output_rate div_sel */
	{2114666666, 1057333333, 1},
	{0, 0, 0},
};

static struct pxa1928_clk_pll_out_table pll4_outp_table[] = {
	/* input_rate output_rate div_sel */
	{2114666666, 528666666, 2},
	{0, 0, 0},
};

static struct pxa1928_clk_pll_out_table pll5_out_table[] = {
	/* input_rate output_rate div_sel */
	{1594666666, 797333333, 1},
	{0, 0, 0},
};

static struct pxa1928_clk_pll_out_table pll5_outp_table[] = {
	/* input_rate output_rate div_sel */
	{1594666666, 531555555, PXA1928_PLL_DIV_3},
	{0, 0, 0},
};

static struct pxa1928_clk_pll_out_table pll6_out_table[] = {
	/* input_rate output_rate div_sel */
	{2114666666, 528666666, 2},
	{0, 0, 0},
};

static struct pxa1928_clk_pll_out_table pll6_outp_table[] = {
	/* input_rate output_rate div_sel */
	{2114666666, 1057333333, 1},
	{0, 0, 0},
};

static struct pxa1928_clk_pll_out_table pll7_out_table[] = {
	/* input_rate output_rate div_sel */
	{2114666666, 528666666, 2},
	{0, 0, 0},
};

static struct pxa1928_clk_pll_out_table pll7_outp_table[] = {
	/* input_rate output_rate div_sel */
	{2114666666, 1057333333, 1},
	{0, 0, 0},
};

static struct pll_tbl pllx_tbl[] = {
	{
		.lock = &pll2_lock,
		.vco_name = "pll2_vco",
		.out_name = "pll2",
		.outp_name = "pll2p",
		.vco_tbl = pll2_vco_table,
		.out_tbl = pll2_out_table,
		.outp_tbl = pll2_outp_table,
		.out_flags = 0,
		.outp_flags = PXA1928_PLL_USE_DIV_3 |
				PXA1928_PLL_USE_ENABLE_BIT,
		.vco_flags = PXA1928_PLL_POST_ENABLE | PXA1928_PLL_PWR_CTRL,
		.vco_params = {
			1200000000, 3200000000UL, MPMU_PLL2CR, MPMU_PLL2_CTRL1,
			MPMU_PLL2_CTRL2, MPMU_PLL2_CTRL3, MPMU_PLL2_CTRL4,
			MPMU_POSR, POSR_PLL2_LOCK, APMU_AP_PLL_CTRL, 2,
			GATE_CTRL_SHIFT_PLL2, APMU_GLB_CLK_CTRL
		},
		.out_params = {
			1200000000, MPMU_PLL2_CTRL1, 3, 26, MPMU_PLL2_CTRL1
		},
		.outp_params = {
			1200000000, MPMU_PLL2_CTRL4, 3, 5, MPMU_PLL2_CTRL1
		}
	},
	{
		.lock = &pll3_lock,
		.vco_name = "pll3_vco",
		.out_name = "pll3",
		.outp_name = "pll3p",
		.vco_tbl = pll3_vco_table,
		.out_tbl = pll3_out_table,
		.outp_tbl = pll3_outp_table,
		.out_flags = 0,
		.outp_flags = PXA1928_PLL_USE_ENABLE_BIT,
		.vco_flags = 0,
		.vco_params = {
			1200000000, 3200000000UL, MPMU_PLL3CR, MPMU_PLL3_CTRL1,
			MPMU_PLL3_CTRL2, MPMU_PLL3_CTRL3, MPMU_PLL3_CTRL4,
			MPMU_POSR2, POSR_PLL3_LOCK, APMU_AP_PLL_CTRL, 2,
			GATE_CTRL_SHIFT_PLL3
		},
		.out_params = {
			1200000000, MPMU_PLL3_CTRL1, 3, 26, MPMU_PLL3_CTRL1
		},
		.outp_params = {
			1200000000, MPMU_PLL3_CTRL4, 3, 5, MPMU_PLL3_CTRL1
		}
	},
	{
		.lock = &pll4_lock,
		.vco_name = "pll4_vco",
		.out_name = "pll4",
		.outp_name = "pll4p",
		.vco_tbl = pll4_vco_table,
		.out_tbl = pll4_out_table,
		.outp_tbl = pll4_outp_table,
		.out_flags = PXA1928_PLL_USE_SYNC_DDR,
		.outp_flags = PXA1928_PLL_USE_ENABLE_BIT,
		.vco_flags = 0,
		.vco_params = {
			1200000000, 3200000000UL, MPMU_PLL4CR, MPMU_PLL4_CTRL1,
			MPMU_PLL4_CTRL2, MPMU_PLL4_CTRL3, MPMU_PLL4_CTRL4,
			MPMU_POSR2, POSR_PLL4_LOCK, APMU_AP_PLL_CTRL, 2,
			GATE_CTRL_SHIFT_PLL4
		},
		.out_params = {
			1200000000, APMU_MC_CLK_RES_CTRL, 3, 12, MPMU_PLL4_CTRL1
		},
		.outp_params = {
			1200000000, MPMU_PLL4_CTRL4, 3, 5, MPMU_PLL4_CTRL1
		}
	},
	{
		.lock = &pll5_lock,
		.vco_name = "pll5_vco",
		.out_name = "pll5",
		.outp_name = "pll5p",
		.vco_tbl = pll5_vco_table,
		.out_tbl = pll5_out_table,
		.outp_tbl = pll5_outp_table,
		.out_flags = 0,
		.outp_flags = PXA1928_PLL_USE_DIV_3 |
				PXA1928_PLL_USE_ENABLE_BIT,
		.vco_flags = 0,
		.vco_params = {
			1200000000, 3200000000UL, MPMU_PLL5CR, MPMU_PLL5_CTRL1,
			MPMU_PLL5_CTRL2, MPMU_PLL5_CTRL3, MPMU_PLL5_CTRL4,
			MPMU_POSR2, POSR_PLL5_LOCK, APMU_AP_PLL_CTRL, 2,
			GATE_CTRL_SHIFT_PLL5
		},
		.out_params = {
			1200000000, MPMU_PLL5_CTRL1, 3, 26, MPMU_PLL5_CTRL1
		},
		.outp_params = {
			1200000000, MPMU_PLL5_CTRL4, 3, 5, MPMU_PLL5_CTRL1
		}
	},
	{
		.lock = &pll6_lock,
		.vco_name = "pll6_vco",
		.out_name = "pll6",
		.outp_name = "pll6p",
		.vco_tbl = pll6_vco_table,
		.out_tbl = pll6_out_table,
		.outp_tbl = pll6_outp_table,
		.out_flags = 0,
		.outp_flags = PXA1928_PLL_USE_ENABLE_BIT,
		.vco_flags = 0,
		.vco_params = {
			1200000000, 3200000000UL, MPMU_PLL6CR, MPMU_PLL6_CTRL1,
			MPMU_PLL6_CTRL2, MPMU_PLL6_CTRL3, MPMU_PLL6_CTRL4,
			MPMU_POSR3, POSR_PLL6_LOCK, APMU_AP_PLL_CTRL, 2,
			GATE_CTRL_SHIFT_PLL6
		},
		.out_params = {
			1200000000, MPMU_PLL6_CTRL1, 3, 26, MPMU_PLL6_CTRL1
		},
		.outp_params = {
			1200000000, MPMU_PLL6_CTRL4, 3, 5, MPMU_PLL6_CTRL1
		}
	},
	{
		.lock = &pll7_lock,
		.vco_name = "pll7_vco",
		.out_name = "pll7",
		.outp_name = "pll7p",
		.vco_tbl = pll7_vco_table,
		.out_tbl = pll7_out_table,
		.outp_tbl = pll7_outp_table,
		.out_flags = 0,
		.outp_flags = PXA1928_PLL_USE_ENABLE_BIT,
		.vco_flags = 0,
		.vco_params = {
			1200000000, 3200000000UL, MPMU_PLL7CR, MPMU_PLL7_CTRL1,
			MPMU_PLL7_CTRL2, MPMU_PLL7_CTRL3, MPMU_PLL7_CTRL4,
			MPMU_POSR2, POSR_PLL7_LOCK, APMU_AP_PLL_CTRL, 2,
			GATE_CTRL_SHIFT_PLL7
		},
		.out_params = {
			1200000000, MPMU_PLL7_CTRL1, 3, 26, MPMU_PLL7_CTRL1
		},
		.outp_params = {
			1200000000, MPMU_PLL7_CTRL4, 3, 5, MPMU_PLL7_CTRL1
		}
	}
};

static struct mmp_clk_disp disp_rst_ctrl = {
	.reg_rst = APMU_DISP_RST_CTRL,
	.dependence = rst_ctrl_depend,
	.num_dependence = ARRAY_SIZE(rst_ctrl_depend),
	.lock = &disp_lock,
};

static struct mmp_clk_disp vdma_axi_ax = {
	.reg_rst = APMU_DISP_CLK_CTRL,
	.reg_rst_shadow = 1 << 24,
	.reg_rst_mask = (7 << 24) | (7 << 28),
	.lock = &disp_lock,
};

static struct mmp_clk_disp vdma_axi_b0 = {
	.reg_rst = APMU_DISP_CLK_CTRL,
	.reg_rst_shadow = (1 << 24) | (1 << 27),
	.reg_rst_mask = (7 << 24) | (7 << 28) | (1 << 27),
	.lock = &disp_lock,
};

static struct mmp_clk_disp disp_axi_ax = {
	.div_ops = &clk_divider_ops,
	.divider.width = 3,
	.divider.shift = 0,
	.reg_rst_mask = 7,
	.reg_div_shadow = 1,
	.divider.lock = &disp_lock,
};

static struct mmp_clk_disp disp_axi_b0 = {
	.div_ops = &clk_divider_ops,
	.divider.width = 3,
	.divider.shift = 0,
	.reg_rst_shadow = 1 << 7,
	.reg_rst_mask = 7 | (1 << 7),
	.reg_div_shadow = 1,
	.divider.lock = &disp_lock,
};

static struct mmp_clk_disp disp_peri = {
	.reg_rst = APMU_DISP_CLK_CTRL,
	.reg_rst_shadow = 1 << 8,
	.reg_rst_mask = (7 << 8) | (7 << 16),
	.lock = &disp_lock,
};

static struct mmp_clk_disp disp_peri_b0 = {
	.reg_rst = APMU_DISP_CLK_CTRL,
	.reg_rst_shadow = (1 << 8) | (1 << 15),
	.reg_rst_mask = (7 << 8) | (7 << 16) | (1 << 15) | (1 << 23),
	.lock = &disp_lock,
};

static struct mmp_clk_disp dsi_phy_slow_ax = {
	.reg_rst = APMU_DISP_CLK_CTRL,
	.reg_rst_shadow = 1 << 4,
	.reg_rst_mask = (1 << 4) | (3 << 5),
	.lock = &disp_lock,
};

static struct mmp_clk_disp disp_axi = {
	.reg_rst = APMU_DISP1_CLK_CTRL,
	.reg_rst_shadow = 1 | (1 << 1) | (1 << 3) | (1 << 4),
	.reg_rst_mask = (1 << 1) | (1 << 3) | (1 << 4),
	.lock = &disp_lock,
};

#define DSI_PHYSLOW_PRER_SHIFT  (15)
#define DSI_ESC_CLK_SEL_SHIFT   (22)
static struct mmp_clk_disp vdma_axi = {
	.reg_rst = APMU_DISP2_CLK_CTRL,
	.reg_rst_shadow = 1 | (1 << 3),
	.reg_rst_mask = (1 << 3),
	.lock = &disp_lock,
};

static struct mmp_clk_disp dsi_phy_slow = {
	.reg_rst = APMU_DISP1_CLK_CTRL,
	.reg_rst_shadow = (1 << 2) | (1 << 5) | (1 << 12) |
			 (0x1a << DSI_PHYSLOW_PRER_SHIFT) |
			 (0 << DSI_ESC_CLK_SEL_SHIFT),
	.reg_rst_mask = (1 << 2) | (1 << 5) | (1 << 12)  |
			(0x1f << DSI_PHYSLOW_PRER_SHIFT) |
			(3 << DSI_ESC_CLK_SEL_SHIFT),
	.lock = &disp_lock,
};

static struct mmp_clk_disp disp1 = {
	.mux_ops = &clk_mux_ops,
	.mux.mask = 3,
	.mux.shift = 6,
	.mux.lock = &disp_lock,
	.reg_mux = APMU_DISP1_CLK_CTRL,
	.div_ops = &clk_divider_ops,
	.divider.width = 4,
	.divider.shift = 8,
	.divider.lock = &disp_lock,
	.reg_div = APMU_DISP1_CLK_CTRL,
};

static struct mmp_clk_disp pnsclk = {
	.mux_ops = &clk_mux_ops,
	.mux.mask = 7,
	.mux.shift = 29,
	.mux.lock = &disp_lock,
	.mux.table = pnsclk_parent_tbl,
	.dependence = pnsclk_depend,
	.num_dependence = ARRAY_SIZE(pnsclk_depend),
	.reg_mux_shadow = 0x20000000,
};

static struct mmp_clk_disp dsisclk = {
	.mux_ops = &clk_mux_ops,
	.mux.mask = 3,
	.mux.shift = 12,
	.mux.lock = &disp_lock,
	.mux.table = pnsclk_parent_tbl_ax,
	.dependence = pnsclk_depend_ax,
	.num_dependence = ARRAY_SIZE(pnsclk_depend_ax),
	.reg_mux_shadow = 0x1000,
};

static struct mmp_clk_disp pnpath = {
	.div_ops = &clk_divider_ops,
	.divider.width = 8,
	.divider.shift = 0,
	.divider.lock = &disp_lock,
	.reg_rst_mask = (1 << 28),
	.dependence = pnsclk_depend,
	.num_dependence = ARRAY_SIZE(pnsclk_depend),
	.reg_div_shadow = 4,
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

static int __init __init_dcstat_debugfs_node(void);

static struct clk *get_parent(char *pname, struct clk *default_parent)
{
	struct device_node *np = of_find_node_by_name(NULL, pname);
	struct clk *parent = NULL;
	const char *str = NULL;

	if (np && !of_property_read_string(np, "clksrc", &str))
		parent = clk_get(NULL, str);

	return (IS_ERR_OR_NULL(parent)) ? default_parent : parent;
}

static unsigned int get_pll1_freq(char *pll_name)
{
	struct device_node *np = of_find_node_by_name(NULL, pll_name);
	unsigned int freq;

	if (np && !of_property_read_u32(np, "freq", &freq))
		return freq;

	if (strcmp(pll_name, "pll1_416_freq"))
		return 624000000;

	return 416000000;
}

static int soc_is_zx(void)
{
	struct device_node *np = of_find_node_by_name(NULL, "pxa1928_apmu_ver");
	const char *str = NULL;

	if (np && !of_property_read_string(np, "version", &str)
			&& !strcmp(str, "zx"))
		return 1;

	return 0;
}

static int pp_is_pxa1926(void)
{
	struct device_node *np = of_find_node_by_name(NULL, "pp_version");
	const char *str = NULL;

	if (np && !of_property_read_string(np, "version", &str)) {
		if (!strcmp(str, "pxa1926"))
			return 1;
	}

	return 0;
}

static void __init pxa1928_pll_init(void __iomem *mpmu_base,
		void __iomem *apmu_base)
{
	struct clk *clk;
	int i, pll_num = ARRAY_SIZE(pllx_tbl);

	if (pxa1928_is_zx)
		/* last two PLLs(PLL6,PLL7) not support for PXA1928 Zx */
		pll_num -= 2;
	for (i = 0; i < pll_num; i++) {
		clk = pxa1928_clk_register_pll_vco(pllx_tbl[i].vco_name,
			"vctcxo", 0,
			pllx_tbl[i].vco_flags, pllx_tbl[i].lock,
			mpmu_base, apmu_base,
			&pllx_tbl[i].vco_params, pllx_tbl[i].vco_tbl);
		clk_register_clkdev(clk, pllx_tbl[i].vco_name, NULL);
		clk_set_rate(clk, pllx_tbl[i].vco_tbl[0].output_rate);

		clk = pxa1928_clk_register_pll_out(pllx_tbl[i].out_name,
			pllx_tbl[i].vco_name, pllx_tbl[i].out_flags,
			pllx_tbl[i].lock, mpmu_base, apmu_base,
			&pllx_tbl[i].out_params, pllx_tbl[i].out_tbl);
		clk_register_clkdev(clk, pllx_tbl[i].out_name, NULL);
		clk_set_rate(clk, pllx_tbl[i].out_tbl[0].output_rate);

		clk = pxa1928_clk_register_pll_out(pllx_tbl[i].outp_name,
			pllx_tbl[i].vco_name, pllx_tbl[i].outp_flags,
			pllx_tbl[i].lock, mpmu_base, apmu_base,
			&pllx_tbl[i].outp_params, pllx_tbl[i].outp_tbl);
		clk_register_clkdev(clk, pllx_tbl[i].outp_name, NULL);
		clk_set_rate(clk, pllx_tbl[i].outp_tbl[0].output_rate);
	}
}

static void __init pxa1928_disp_clk_init(void __iomem *apmu_base)
{
	struct clk *clk, *clk_disp1, *pll1_416;
	void __iomem *pn_sclk_reg = ioremap(LCD_PN_SCLK, 4);
	struct mmp_clk_disp *disp_axi_clk_info, *vdma_axi_info, *disp_peri_info;
	int is_a0 = pxa1928_is_a0();

	if (pn_sclk_reg == NULL) {
		pr_err("%s, pn_sclk_reg map error\n", __func__);
		return;
	}

	if (!pxa1928_is_zx) {
		/* Clock source select for ACLK (AXI clock) */
		clk = clk_register_mux(NULL, "disp_axi_sclk", disp_axi_sclk_parent,
				ARRAY_SIZE(disp_axi_sclk_parent), CLK_SET_RATE_PARENT,
				apmu_base + APMU_DISP_CLK_CTRL2, 4, 3, 0, &disp_lock);
		clk_register_clkdev(clk, "disp_axi_sclk", NULL);
		pll1_416 = clk_get(NULL, "pll1_416");
		clk_set_parent(clk, get_parent("disp_axi_clksrc", pll1_416));

		if (is_a0) {
			disp_axi_clk_info = &disp_axi_ax;
			vdma_axi_info = &vdma_axi_ax;
			disp_peri_info = &disp_peri;
		} else {
			disp_axi_clk_info = &disp_axi_b0;
			vdma_axi_info = &vdma_axi_b0;
			disp_peri_info = &disp_peri_b0;
		}
		/* Clock for ACLK (AXI clock) */
		disp_axi_clk_info->divider.reg =
			apmu_base + APMU_DISP_CLK_CTRL2;
		clk = mmp_clk_register_disp("disp_axi", disp_axi_parent,
			ARRAY_SIZE(disp_axi_parent),
			MMP_DISP_DIV_ONLY, CLK_SET_RATE_PARENT, apmu_base,
			disp_axi_clk_info);
		clk_set_rate(clk, 69333333);
		clk_register_clkdev(clk, "disp_axi", NULL);

		clk = mmp_clk_register_disp("vdma_axi", NULL, 0,
			MMP_DISP_BUS, CLK_IS_ROOT, apmu_base, vdma_axi_info);
		clk_register_clkdev(clk, "vdma_axi", NULL);

		clk = mmp_clk_register_disp("disp_peri", NULL, 0,
			MMP_DISP_BUS, CLK_IS_ROOT, apmu_base, disp_peri_info);
		clk_register_clkdev(clk, "disp_peri", NULL);

		clk = mmp_clk_register_disp("dsi_phy_slow", NULL, 0,
			MMP_DISP_BUS, CLK_IS_ROOT, apmu_base,
			&dsi_phy_slow_ax);
		clk_register_clkdev(clk, "dsi_phy_slow", NULL);

		/*
		 * FIXME: Because that the post divider from pnpath and dsi was
		 * enough, we not use disp1 post divider, only register it
		 * as mux.
		 */
		clk_disp1 = clk_register_mux(NULL, "disp1", disp1_parent_ax,
			ARRAY_SIZE(disp1_parent_ax), CLK_SET_RATE_PARENT,
			apmu_base + APMU_DISP_CLK_CTRL, 12, 3, 0, &disp_lock);
		clk_register_clkdev(clk_disp1, "disp1", NULL);
		pll1_416 = clk_get(NULL, "pll1_416");
		clk_set_parent(clk_disp1, get_parent("disp1_clksrc", pll1_416));

		clk = mmp_clk_register_disp("LCDCIHCLK", NULL, 0,
			MMP_DISP_RST_CTRL, CLK_IS_ROOT,
			apmu_base, &disp_rst_ctrl);
		clk_register_clkdev(clk, "LCDCIHCLK", NULL);

		dsisclk.mux.reg = pn_sclk_reg;
		clk = mmp_clk_register_disp("dsi_sclk", pnsclk_parent,
			ARRAY_SIZE(pnsclk_parent),
			MMP_DISP_MUX_ONLY, CLK_SET_RATE_PARENT, apmu_base,
			&dsisclk);
		clk_register_clkdev(clk, "dsi_sclk", NULL);
		clk_set_parent(clk, get_parent("pn_sclk_clksrc", clk_disp1));

		pnsclk.dependence = pnsclk_depend_ax;
		pnsclk.num_dependence = ARRAY_SIZE(pnsclk_depend_ax);
		pnsclk.mux.table = pnsclk_parent_tbl_ax,
		pnpath.dependence = pnsclk_depend_ax;
		pnpath.num_dependence = ARRAY_SIZE(pnsclk_depend_ax);
		dsi1.dependence = dsi_depend_ax;
		dsi1.num_dependence = ARRAY_SIZE(dsi_depend_ax);

		pnsclk.mux.reg = pn_sclk_reg;
		clk = mmp_clk_register_disp("pn_sclk", pnsclk_parent,
			ARRAY_SIZE(pnsclk_parent),
			MMP_DISP_MUX_ONLY, 0, apmu_base, &pnsclk);
		clk_register_clkdev(clk, "pn_sclk", NULL);
		clk_set_parent(clk, get_parent("pn_sclk_clksrc", clk_disp1));

		pnpath.divider.reg = pn_sclk_reg;
		clk = mmp_clk_register_disp("mmp_pnpath", pnpath_parent,
			ARRAY_SIZE(pnpath_parent),
			MMP_DISP_DIV_ONLY, 0, apmu_base, &pnpath);
		clk_register_clkdev(clk, "mmp_pnpath", NULL);

		dsi1.divider.reg = pn_sclk_reg;
		clk = mmp_clk_register_disp("mmp_dsi1", dsi_parent,
			ARRAY_SIZE(dsi_parent),
			MMP_DISP_DIV_ONLY, CLK_SET_RATE_PARENT, apmu_base,
			&dsi1);
		clk_register_clkdev(clk, "mmp_dsi1", NULL);
	} else {
		clk = mmp_clk_register_disp("LCDCIHCLK", NULL, 0,
			MMP_DISP_BUS, CLK_IS_ROOT,
			apmu_base, &disp_axi);
		clk_register_clkdev(clk, "LCDCIHCLK", NULL);

		clk = mmp_clk_register_disp("vdma_axi", NULL, 0,
			MMP_DISP_BUS, CLK_IS_ROOT, apmu_base, &vdma_axi);
		clk_register_clkdev(clk, "vdma_axi", NULL);

		clk = mmp_clk_register_disp("dsi_phy_slow", NULL, 0,
			MMP_DISP_BUS, CLK_IS_ROOT, apmu_base,
			&dsi_phy_slow);
		clk_register_clkdev(clk, "dsi_phy_slow", NULL);

		clk_disp1 = mmp_clk_register_disp("disp1", disp1_parent,
			ARRAY_SIZE(disp1_parent), 0, CLK_SET_RATE_PARENT,
			apmu_base, &disp1);
		clk_register_clkdev(clk_disp1, "disp1", NULL);
		pll1_416 = clk_get(NULL, "pll1_416");
		clk_set_parent(clk_disp1, get_parent("disp1_clksrc", pll1_416));

		pnsclk.mux.reg = pn_sclk_reg;
		clk = mmp_clk_register_disp("pn_sclk", pnsclk_parent,
			ARRAY_SIZE(pnsclk_parent),
			MMP_DISP_MUX_ONLY, CLK_SET_RATE_PARENT, apmu_base,
			&pnsclk);
		clk_register_clkdev(clk, "pn_sclk", NULL);
		clk_set_parent(clk, get_parent("pn_sclk_clksrc", clk_disp1));

		pnpath.divider.reg = pn_sclk_reg;
		clk = mmp_clk_register_disp("mmp_pnpath", pnpath_parent,
			ARRAY_SIZE(pnpath_parent),
			MMP_DISP_DIV_ONLY, 0, apmu_base, &pnpath);
		clk_register_clkdev(clk, "mmp_pnpath", NULL);

		dsi1.divider.reg = pn_sclk_reg;
		clk = mmp_clk_register_disp("mmp_dsi1", pnpath_parent,
			ARRAY_SIZE(pnpath_parent),
			MMP_DISP_DIV_ONLY, CLK_SET_RATE_PARENT, apmu_base,
			&dsi1);
		clk_register_clkdev(clk, "mmp_dsi1", NULL);
	}
}

#ifdef CONFIG_VIDEO_MMP
#define CCIC_AXI_CLK_EN		(1 << 3)
#define CCIC_AXI_RST	(1 << 0)
#define CCIC_FN_CLK_EN		(1 << 4)
#define CCIC_FN_RST		(1 << 1)
#define CCIC_PHY_RST	(1 << 2)
#define CCIC_PHY_CLK_EN	(1 << 5)
#define CCIC_SPHY_RST	(1 << 8)
#define CCIC_SPHY_CLK_EN	(1 << 9)
#define CCIC_SPHY_DIV_OFFSET	10
#define CCIC_SPHY_DIV_MASK	(MASK(5))

#define CCIC_AXI_ARB_CLK_EN	(1 << 15)
#define CCIC2_CCCI1_SYNC_EN	(1 << 15)
#define CCIC_AXI_ARB_RST	(1 << 16)

enum periph_ccic_fn_src {
	CCIC_PLL1_624 = 0x0,
	CCIC_PLL5P = 0x1,
	CCIC_PLL5 = 0x2,
	CCIC_PLL1_416 = 0x3,
};
/* ccic1 & ccic2 use the same ccic_fn_mux_sel */
static struct clk_mux_sel ccic_fn_mux_sel[] = {
	{.parent_name = "pll1_624", .value = CCIC_PLL1_624},
	{.parent_name = "pll5p", .value = CCIC_PLL5P},
	{.parent_name = "pll5", .value = CCIC_PLL5},
	{.parent_name = "pll1_416", .value = CCIC_PLL1_416},
};
static struct peri_params ccic1_func_params = {
	.inputs = ccic_fn_mux_sel,
	.inputs_size = ARRAY_SIZE(ccic_fn_mux_sel),
};
static struct peri_params ccic2_func_params = {
	.comclk_name = "CCIC_SYNC_EN",
	.inputs = ccic_fn_mux_sel,
	.inputs_size = ARRAY_SIZE(ccic_fn_mux_sel),
};
static struct peri_reg_info ccic1_func_reg = {
	.src_sel_shift = 6,
	.src_sel_mask = MASK(REG_WIDTH_2BIT),
	.div_shift = 17,
	.div_mask = MASK(REG_WIDTH_4BIT),
	.enable_val = (CCIC_FN_CLK_EN | CCIC_FN_RST),
	.disable_val = CCIC_FN_CLK_EN,
};
static struct peri_reg_info ccic2_func_reg = {
	.src_sel_shift = 6,
	.src_sel_mask = MASK(REG_WIDTH_2BIT),
	.div_shift = 16,
	.div_mask = MASK(REG_WIDTH_4BIT),
	.enable_val = (CCIC_FN_CLK_EN | CCIC_FN_RST),
	.disable_val = CCIC_FN_CLK_EN,
};

/* ccic1 & ccic2 use the same ccic_phy_reg */
static struct peri_reg_info ccic_phy_reg = {
	.src_sel_shift = 9,
	.src_sel_mask = MASK(REG_WIDTH_1BIT),
	.div_shift = 10,
	.div_mask = MASK(REG_WIDTH_5BIT),
	.enable_val = (CCIC_SPHY_CLK_EN | CCIC_SPHY_RST |
				   CCIC_PHY_CLK_EN | CCIC_PHY_RST),
	.disable_val = (CCIC_SPHY_CLK_EN | CCIC_PHY_CLK_EN),
};

static struct clk_mux_sel ccic_phy_mux_sel[] = {
	{.parent_name = "pll1_624", .value = CCIC_PLL1_624},
	{.parent_name = "vctcxo", .value = 1},
};

static struct peri_params ccic_phy_params = {
	.inputs = ccic_phy_mux_sel,
	.inputs_size =  ARRAY_SIZE(ccic_phy_mux_sel),
};

static void __init pxa1928_ccic_clk_init(void __iomem *apmu_base)
{
	struct clk *clk;
	struct device_node *np = of_find_node_by_name(NULL, "ccic_couple");
	const u32 *tmp;
	int len;
	int sync = 0;

	if (np) {
		tmp = of_get_property(np, "ccic_coupled", &len);
		if (tmp)
			sync = be32_to_cpup(tmp);
	}

	/* ccic1 & ccic2 will use the same axi arb clk */
	clk = mmp_clk_register_apmu("ccic_axi_arb_clk", "NULL",
			apmu_base + APMU_CCIC0, 0x18000, &clk_lock);
	clk_register_clkdev(clk, "CCICARBCLK", NULL);

	clk = mmp_clk_register_apmu("ccic1_axi_clk", "CCICARBCLK",
			apmu_base + APMU_CCIC0, 0x9, &clk_lock);
	clk_register_clkdev(clk, "CCICAXICLK", "mmp-camera.0");

	clk = mmp_clk_register_apmu("ccic2_axi_clk", "CCICARBCLK",
			apmu_base + APMU_CCIC1, 0x9, &clk_lock);
	clk_register_clkdev(clk, "CCICAXICLK", "mmp-camera.1");

	/* CCIC1 & CCIC2 sync enable clk */
	clk = mmp_clk_register_apmu("ccic1_ccic2_sync", "NULL",
			apmu_base + APMU_CCIC1, 0x8000, &clk_lock);
	clk_register_clkdev(clk, "CCIC_SYNC_EN", NULL);
	clk_prepare_enable(clk);
	clk_disable_unprepare(clk);

	if (sync) {
		/* ccic2 depends on this func clk:CCICFUNCLK_0 */
		clk = mmp_clk_register_peri("ccic1_func_clk", ccic_fn_parent,
			ARRAY_SIZE(ccic_fn_parent), 0, apmu_base + APMU_CCIC0,
			&clk_lock, &ccic1_func_params, &ccic1_func_reg);
		clk_register_clkdev(clk, "CCICFUNCLK", "mmp-camera.0");

		clk = mmp_clk_register_peri("ccic2_func_clk", ccic_fn_parent,
			ARRAY_SIZE(ccic_fn_parent), 0, apmu_base + APMU_CCIC0,
			&clk_lock, &ccic2_func_params, &ccic1_func_reg);
		clk_register_clkdev(clk, "CCICFUNCLK", "mmp-camera.1");
	} else {
		clk = mmp_clk_register_peri("ccic1_func_clk", ccic_fn_parent,
			ARRAY_SIZE(ccic_fn_parent), 0, apmu_base + APMU_CCIC0,
			&clk_lock, &ccic1_func_params, &ccic1_func_reg);
		clk_register_clkdev(clk, "CCICFUNCLK", "mmp-camera.0");

		clk = mmp_clk_register_peri("ccic2_func_clk", ccic_fn_parent,
			ARRAY_SIZE(ccic_fn_parent), 0, apmu_base + APMU_CCIC1,
			&clk_lock, &ccic1_func_params, &ccic2_func_reg);
		clk_register_clkdev(clk, "CCICFUNCLK", "mmp-camera.1");
	}

	clk = mmp_clk_register_peri("ccic1_phy_clk", ccic_sphy_parent,
		ARRAY_SIZE(ccic_sphy_parent), 0, apmu_base + APMU_CCIC0,
		&clk_lock, &ccic_phy_params, &ccic_phy_reg);
	clk_register_clkdev(clk, "CCICPHYCLK", "mmp-camera.0");

	clk = mmp_clk_register_peri("ccic2_phy_clk", ccic_sphy_parent,
		ARRAY_SIZE(ccic_sphy_parent), 0, apmu_base + APMU_CCIC1,
		&clk_lock, &ccic_phy_params, &ccic_phy_reg);
	clk_register_clkdev(clk, "CCICPHYCLK", "mmp-camera.1");

	return;
}
#endif

#ifdef CONFIG_VIDEO_MV_SC2_MMU
static DEFINE_SPINLOCK(sc2_lock);
#define PMUM_SC2_ISIM1_CLK_CR 0x1140
#define PMUM_SC2_ISIM2_CLK_CR 0x1140
#define		SC2_ISIM_RSTN (0x1 << 1)
/*SC2 MCLK has a hardware devider to make 624Mhz into 312Mhz
*In order to get a right register value, we have to multiply clk_rate by this divider
* reg_val = 312Mhz / clk_rate
* or
* reg_val = 624Mhz / clk_rate * MCLK_DIVIDER.
*/
#define MCLK_DIVIDER	2

/* sc2 mclk */
/*
 * Please notice the clk locates in MPMU
 * MCLK support fracional part. The code
 * does not support here. Will update later
 */
static struct peri_reg_info sc2_mclk_reg = {
	.src_sel_shift = 0,
	.src_sel_mask = MASK(REG_WIDTH_1BIT),
	.div_shift = 2,
	.div_mask = MASK(REG_WIDTH_16BIT),
	.enable_val = SC2_ISIM_RSTN,
	.disable_val = SC2_ISIM_RSTN,
	.flags = CLK_DIVIDER_ONE_BASED,
};

static struct periph_clk_tbl sc2_mclk_tbl[] = {
	{.clk_rate = 26000000, .parent_name = "vctcxo"},
	{.clk_rate = 24000000 * MCLK_DIVIDER, .parent_name = "pll1_624"},
};

static struct clk_mux_sel sc2_mclk_mux_sel[] = {
	{.parent_name = "pll1_624", .value = 0},
	{.parent_name = "vctcxo", .value = 1},
};

static struct peri_params sc2_mclk_params = {
	.clktbl = sc2_mclk_tbl,
	.clktblsize = ARRAY_SIZE(sc2_mclk_tbl),
	.inputs = sc2_mclk_mux_sel,
	.inputs_size = ARRAY_SIZE(sc2_mclk_mux_sel),
};

/* sc2 csi clk */
static struct peri_reg_info sc2_csi_reg = {
	.src_sel_shift = 4,
	.src_sel_mask = MASK(REG_WIDTH_3BIT),
	.div_shift = 0,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.fcreq_shift = 9,
	.fcreq_mask = MASK(REG_WIDTH_1BIT),
	.flags = CLK_DIVIDER_ONE_BASED,
};

static struct periph_clk_tbl sc2_csi_tbl[] = {
	{.clk_rate = 624000000, .parent_name = "pll1_624"},
	{.clk_rate = 416000000, .parent_name = "pll1_416"},
};

static struct clk_mux_sel sc2_csi_mux_sel[] = {
	{.parent_name = "pll1_624", .value = 0},
	/* prefer not to use pll7 */
	/* {.parent_name = "pll7", .value = 1}, */
	{.parent_name = "pll1_416", .value = 2},
	/* prefer not to use pll5 */
	/* {.parent_name = "pll5", .value = 6}, */
};

static struct peri_params sc2_csi_params = {
	.clktbl = sc2_csi_tbl,
	.clktblsize = ARRAY_SIZE(sc2_csi_tbl),
	.inputs = sc2_csi_mux_sel,
	.inputs_size =	ARRAY_SIZE(sc2_csi_mux_sel),
};

/* sc2 axi clk */
static struct peri_reg_info sc2_axi_reg = {
	.src_sel_shift = 12,
	.src_sel_mask = MASK(REG_WIDTH_3BIT),
	.div_shift = 8,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.fcreq_shift = 9,
	.fcreq_mask	= MASK(REG_WIDTH_1BIT),
	.flags = CLK_DIVIDER_ONE_BASED,
};

static struct periph_clk_tbl sc2_axi_tbl[] = {
	{.clk_rate = 312000000, .parent_name = "pll1_624"},
	{.clk_rate = 416000000, .parent_name = "pll1_416"},
};

static struct clk_mux_sel sc2_axi_mux_sel[] = {
	{.parent_name = "pll1_624", .value = 0},
	/* prefer not to use pll7 */
	/* {.parent_name = "pll7", .value = 1}, */
	{.parent_name = "pll1_416", .value = 2},
	/* prefer not to use pll5 */
	/* {.parent_name = "pll5", .value = 6}, */
};

static struct peri_params sc2_axi_params = {
	.clktbl = sc2_axi_tbl,
	.clktblsize = ARRAY_SIZE(sc2_axi_tbl),
	.inputs = sc2_axi_mux_sel,
	.inputs_size =	ARRAY_SIZE(sc2_axi_mux_sel),
};

/* sc2 4x clk */
static struct peri_reg_info sc2_4x_reg = {
	.src_sel_shift = 4,
	.src_sel_mask = MASK(REG_WIDTH_3BIT),
	.div_shift = 0,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.fcreq_shift = 9,
	.fcreq_mask	= MASK(REG_WIDTH_1BIT),
	.flags = CLK_DIVIDER_ONE_BASED,
};

static struct periph_clk_tbl sc2_4x_tbl[] = {
	{.clk_rate = 624000000, .parent_name = "pll1_624"},
	{.clk_rate = 416000000, .parent_name = "pll1_416"},
};

static struct clk_mux_sel sc2_4x_mux_sel[] = {
	{.parent_name = "pll1_624", .value = 0},
	/* prefer not to use pll7 */
	/* {.parent_name = "pll7", .value = 1}, */
	{.parent_name = "pll1_416", .value = 2},
	/* prefer not to use pll5 */
	/* {.parent_name = "pll5", .value = 6}, */
};

static struct peri_params sc2_4x_params = {
	.clktbl = sc2_4x_tbl,
	.clktblsize = ARRAY_SIZE(sc2_4x_tbl),
	.inputs = sc2_4x_mux_sel,
	.inputs_size =	ARRAY_SIZE(sc2_4x_mux_sel),
};

static void __init pxa1928_sc2_clk_init(void __iomem *apmu_base,
					void __iomem *mpmu_base)
{
	struct clk *clk;
	struct device_node *np = of_find_node_by_name(NULL, "ccic_couple");
	const u32 *tmp;
	int len;
	int sync = 0;

	if (np) {
		tmp = of_get_property(np, "ccic_coupled", &len);
		if (tmp)
			sync = be32_to_cpup(tmp);
	}

	/* de-asserted mclk */
	iowrite32(0x2, mpmu_base + 0x1140);
	iowrite32(0x2, mpmu_base + 0x1144);

	clk = mmp_clk_register_peri("sc2_mclk", sc2_mclk_parent,
		ARRAY_SIZE(sc2_mclk_parent), 0,
		mpmu_base + PMUM_SC2_ISIM1_CLK_CR,
		&sc2_lock, &sc2_mclk_params, &sc2_mclk_reg);
	clk_register_clkdev(clk, "SC2MCLK", NULL);
	if (!sync) {
		clk = mmp_clk_register_peri("sc2_mclk", sc2_mclk_parent,
			ARRAY_SIZE(sc2_mclk_parent), 0,
			mpmu_base + PMUM_SC2_ISIM2_CLK_CR,
			&sc2_lock, &sc2_mclk_params, &sc2_mclk_reg);
		clk_register_clkdev(clk, "SC2MCLK", "mv_sc2_ccic.1");
	}

	clk = pxa1928_clk_register_gc_vpu("sc2_csi_clk", sc2_csi_parent,
			ARRAY_SIZE(sc2_csi_parent), 0,
			apmu_base + APMU_ISP_CLKCTRL2,
			apmu_base + APMU_ISP_RSTCTRL, &sc2_lock,
			&sc2_csi_params, &sc2_csi_reg, NULL, 0);
	clk_register_clkdev(clk, "SC2CSICLK", NULL);

	clk = pxa1928_clk_register_gc_vpu("sc2_axi_clk", sc2_axi_parent,
			ARRAY_SIZE(sc2_axi_parent), 0,
			apmu_base + APMU_ISP_CLKCTRL,
			apmu_base + APMU_ISP_RSTCTRL, &sc2_lock,
			&sc2_axi_params, &sc2_axi_reg, NULL, 0);
	clk_register_clkdev(clk, "SC2AXICLK", NULL);

	clk = pxa1928_clk_register_gc_vpu("sc2_4x_clk", sc2_4x_parent,
			ARRAY_SIZE(sc2_4x_parent), 0,
			apmu_base + APMU_ISP_CLKCTRL,
			apmu_base + APMU_ISP_RSTCTRL, &sc2_lock,
			&sc2_4x_params, &sc2_4x_reg, NULL, 0);
	clk_register_clkdev(clk, "SC24XCLK", NULL);

	clk = mmp_clk_register_apmu("sc2_dphy_clk.0", "pll1_624",
			apmu_base + APMU_ISP_CLKCTRL2, 0x100, &sc2_lock);
	clk_register_clkdev(clk, "CCICDPHYCLK", "mv_sc2_ccic.0");

	clk = mmp_clk_register_apmu("sc2_dphy_clk.1", "pll1_624",
			apmu_base + APMU_ISP_CLKCTRL2, 0x200, &sc2_lock);
	clk_register_clkdev(clk, "CCICDPHYCLK", "mv_sc2_ccic.1");
	return;
}
#endif

#ifdef CONFIG_SOUND
static void __init pxa1928_audio_clk_init(void __iomem *apmu_base,
	void __iomem *audio_base, void __iomem *audio_aux_base,
	poweron_cb poweron)
{
	struct clk *apll1_slow, *sspa1_mn_div, *sspa1_clk_src, *sspa1_div_clk;
	struct device_node *np = of_find_node_by_name(NULL, "sound");
	struct clk *clk;
	int ret;
	u32 apll = 0;

	if (!np)
		return;

	if (!pxa1928_is_zx) {
		np = of_find_compatible_node(NULL, NULL, "marvell,mmp-map");
		if (!np)
			return;
		/*
		 * get apll settings in DT
		 * apll = 0: APLL_32K
		 * apll = 1: APLL_26M
		 */
		ret = of_property_read_u32(np, "marvell,apll", &apll);
		if (ret < 0)
			/* using default apll = 0, APLL_32K */
			pr_err("could not get marvell,apll in dt in clock init\n");

		audio_clk_init(apmu_base, audio_base, audio_aux_base, poweron, apll);
	} else {
		apll1_slow = mmp_clk_register_audio("apll1_slow", "vctcxo",
			apmu_base, audio_base, audio_aux_base,
			APMU_AUDIO_RST_DIS | APMU_AUDIO_ISO_DIS |
			APMU_AUDIO_CLK_ENA | APMU_AUDIO_PWR_UP, &clk_lock);
		clk_register_clkdev(apll1_slow, "mmp-audio", NULL);

		clk_prepare_enable(apll1_slow);
		clk_set_rate(apll1_slow, 22579200);

		clk = clk_register_mux(NULL, "sspa1_mux1", sspa1_parent1,
				ARRAY_SIZE(sspa1_parent1), 0,
				audio_aux_base + 0x84, 3, 3, 0, &clk_lock);
		clk_set_parent(clk, apll1_slow);
		clk_register_clkdev(clk, "sspa1_mux.1", NULL);

		sspa1_mn_div = clk_register_gate(NULL, "sspa1_mn_div",
					"sspa1_mux1", 0,
					audio_aux_base + 0x94, 30, 0,
					&clk_lock);
		clk_register_clkdev(clk, "mn_bypass.1", NULL);
		clk_prepare_enable(sspa1_mn_div);

		sspa1_clk_src = clk_register_mux(NULL, "sspa1_clk_src",
				sspa1_parent2,
				ARRAY_SIZE(sspa1_parent2), 0,
				audio_aux_base + 0x84, 9, 1, 0,
				&clk_lock);
		clk_set_parent(sspa1_clk_src, sspa1_mn_div);
		clk_register_clkdev(clk, "sspa1_mux.2", NULL);

		sspa1_div_clk = clk_register_mux(NULL, "sspa1_div_clk",
				sspa1_parent3,
				ARRAY_SIZE(sspa1_parent3), 0,
				audio_aux_base + 0x84, 11, 1, 0,
				&clk_lock);
		clk_set_parent(sspa1_div_clk, sspa1_clk_src);
		clk_register_clkdev(clk, "sspa1_mux.3", NULL);

		clk = clk_register_mux(NULL, "sysclk_mux", sysclk_parent,
				ARRAY_SIZE(sysclk_parent), 0,
				audio_aux_base + 0x84, 13, 1, 0, &clk_lock);
		clk_set_parent(clk, sspa1_div_clk);
		clk_register_clkdev(clk, "sysclk_mux.0", NULL);

		clk = clk_register_divider(NULL, "sspa1_div",
				"sspa1_div_clk", 0,
				audio_base + 0x34, 9, 6,
				CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
				&clk_lock);
		clk_set_rate(clk, 2822400);
		clk_register_clkdev(clk, "sspa1_div", NULL);

		clk = clk_register_divider(NULL, "sysclk_div",
				"sspa1_div_clk", 0,
				audio_base + 0x34, 1, 6,
				CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
				&clk_lock);
		clk_set_rate(clk, 11289600);
		clk_register_clkdev(clk, "sysclk_div", NULL);

		clk = clk_register_gate(NULL, "mmp-sspa-dai.0", "sspa1_div", 0,
					audio_base + 0x34, 8, 0, &clk_lock);

		clk_register_clkdev(clk, NULL, "mmp-sspa-dai.0");

		clk = clk_register_gate(NULL, "sysclk.0", "sysclk_div", 0,
					audio_base + 0x34, 8, 0, &clk_lock);

		clk_register_clkdev(clk, "mmp-sysclk", NULL);
	}
}
#endif

/*
 * pll5 initialization for pxa1926 pp
 */
void pxa1926_pll5_init(void)
{
	struct clk *pll5;

	pll5 = clk_get(NULL, "pll5");
	clk_set_rate(pll5, 1248000000);

	pr_debug("After clk_set_rate, pll5 rate is [%ld]\n", clk_get_rate(pll5));
}

#define MMP_PERI_GATE_FLAG	BIT(31)
/* For Zx */
#define GC2D_CLK_EN			(1u << 15)
#define GC2D_ACLK_EN			(1u << 19)
#define GC2D_ACLK_EN_A0			(1u << 0)
#define GC3D_CLK_EN			(1u << 3)
#define GC3D_ACLK_EN			(1u << 2)

/* For B0 */
#define GC3D_CLK2x_EN			(1 << 23)
#define GC3D_CLK1x_EN			(1 << 15)
#define GC3D_AXICLK_EN			(1 << 7)
#define GC2D_CLK1x_EN			(1 << 15)

#define DEFAULT_GC3D_MAX_FREQ	797333333
#define DEFAULT_GC2D_MAX_FREQ	416000000

static struct xpu_rtcwtc gc_rtcwtc_a0[] = {
	{.max_rate = 416000000, .rtcwtc = 0x550000,},
	{.max_rate = 797333333, .rtcwtc = 0x55550000,},
};

static struct xpu_rtcwtc gc_rtcwtc_b0[] = {
	{.max_rate = 208000000, .rtcwtc = 0x0,},
	{.max_rate = 797333333, .rtcwtc = 0x55550000,},
};

static struct xpu_rtcwtc gc2d_rtcwtc_a0[] = {
	{.max_rate = 416000000, .rtcwtc = 0x0,},
};

static struct xpu_rtcwtc gc2d_rtcwtc_b0[] = {
	{.max_rate = 208000000, .rtcwtc = 0x00900000,},
	{.max_rate = 416000000, .rtcwtc = 0x15950000,},
};

static struct clk_mux_sel gc3d_mux_pll[] = {
	{.parent_name = "pll1_624", .value = 0},
	{.parent_name = "pll2", .value = 1},
	{.parent_name = "pll5", .value = 2},
	{.parent_name = "pll2p", .value = 3},
};


static struct clk_mux_sel gc3d1x_mux_pll_ax[] = {
	{.parent_name = "pll1_624", .value = 0},
	{.parent_name = "pll2", .value = 1},
	{.parent_name = "pll5", .value = 2},
	{.parent_name = "pll5p", .value = 6},
};

static struct clk_mux_sel gc3d2x_mux_pll_ax[] = {
	{.parent_name = "pll1_624", .value = 0},
	{.parent_name = "pll2", .value = 1},
	{.parent_name = "pll5", .value = 2},
	{.parent_name = "pll5p", .value = 6},
};

static struct clk_mux_sel gc2d_mux_pll[] = {
	{.parent_name = "pll1_624", .value = 0},
	{.parent_name = "pll2", .value = 1},
	{.parent_name = "pll5", .value = 2},
	{.parent_name = "pll5p", .value = 3},
};

static struct clk_mux_sel gc2d_mux_pll_ax[] = {
	{.parent_name = "pll1_624", .value = 0},
	{.parent_name = "pll2", .value = 1},
	{.parent_name = "pll1_416", .value = 2},
	{.parent_name = "pll5p", .value = 6},
};

static struct clk_mux_sel gc_aclk_mux_pll[] = {
	{.parent_name = "pll1_624", .value = 0},
	{.parent_name = "pll2", .value = 1},
	{.parent_name = "pll5p", .value = 2},
	{.parent_name = "pll1_416", .value = 3},
};

static struct clk_mux_sel gc3d_aclk_mux_pll_ax[] = {
	{.parent_name = "pll1_624", .value = 0},
	{.parent_name = "pll2", .value = 1},
	{.parent_name = "pll1_416", .value = 2},
	{.parent_name = "pll5p", .value = 6},
};

static const char const *gc_aclk_parents[] = {"pll1_624", "pll2",
						"pll5p", "pll1_416",};
static const char const *gc3d_aclk_parents_ax[] = {"pll1_624", "pll2",
						"pll1_416", "pll5p",};
static const char const *gc3d_clk_parents[] = {"pll1_624", "pll2",
						"pll5", "pll2p",};
static const char const *gc3d_clk_parents_ax[] = {"pll1_624", "pll2",
						"pll5", "pll5p",};
static const char const *gc2d_clk_parents[] = {"pll1_624", "pll2",
						"pll5", "pll5p",};
static const char const *gc2d_clk_parents_ax[] = {"pll1_624", "pll2",
						"pll1_416", "pll5p",};

static struct periph_clk_tbl gc_aclk_tbl[] = {
	{.clk_rate = 156000000, .parent_name = "pll1_624"},
	{.clk_rate = 208000000, .parent_name = "pll1_416"},
	{.clk_rate = 312000000, .parent_name = "pll1_624"},
	{.clk_rate = 416000000, .parent_name = "pll1_416"},
};

static struct peri_reg_info gc3d_aclk_reg = {
	.src_sel_shift = 4,
	.src_sel_mask = MASK(REG_WIDTH_2BIT),
	.div_shift = 23,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.flags = CLK_DIVIDER_ONE_BASED,
	.reg_offset = APMU_GC,
	.enable_val = GC3D_ACLK_EN,
	.disable_val = GC3D_ACLK_EN,
};

static struct peri_reg_info gc3d_aclk_reg_ax = {
	.src_sel_shift = 4,
	.src_sel_mask = MASK(REG_WIDTH_3BIT),
	.div_shift = 0,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.fcreq_shift = 9,
	.fcreq_mask = MASK(REG_WIDTH_1BIT),
	.flags = CLK_DIVIDER_ONE_BASED,
	.enable_val = GC3D_AXICLK_EN,
	.disable_val = GC3D_AXICLK_EN,
};

static struct peri_reg_info gc2d_aclk_reg = {
	.src_sel_shift = 0,
	.src_sel_mask = MASK(REG_WIDTH_2BIT),
	.div_shift = 2,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.flags = CLK_DIVIDER_ONE_BASED,
	.reg_offset = APMU_GC2,
	.enable_val = GC2D_ACLK_EN,
	.disable_val = GC2D_ACLK_EN,
};

static struct peri_reg_info gc2d_aclk_reg_ax = {
	.enable_val = GC2D_ACLK_EN_A0,
	.disable_val = GC2D_ACLK_EN_A0,
};

static struct peri_params gc3d_aclk_params = {
	.clktbl = gc_aclk_tbl,
	.clktblsize = ARRAY_SIZE(gc_aclk_tbl),
	.inputs = gc_aclk_mux_pll,
	.inputs_size = ARRAY_SIZE(gc_aclk_mux_pll),
};

static struct peri_params gc2d_aclk_params = {
	.clktbl = gc_aclk_tbl,
	.clktblsize = ARRAY_SIZE(gc_aclk_tbl),
	.inputs = gc_aclk_mux_pll,
	.inputs_size = ARRAY_SIZE(gc_aclk_mux_pll),
};

static struct peri_params gc3d_aclk_params_ax = {
	.clktbl = gc_aclk_tbl,
	.clktblsize = ARRAY_SIZE(gc_aclk_tbl),
	.inputs = gc3d_aclk_mux_pll_ax,
	.inputs_size = ARRAY_SIZE(gc3d_aclk_mux_pll_ax),
};

static struct peri_params gc2d_aclk_params_ax = {
	.flags = MMP_PERI_GATE_FLAG,
};


static const char const *gc3d_clk1x_depend[] = {"GC3D_ACLK",};

static struct periph_clk_tbl gc3d_clk1x_tbl[] = {
	{.clk_rate = 156000000, .parent_name = "pll1_624"},
	{.clk_rate = 208000000, .parent_name = "pll1_624"},
	{.clk_rate = 312000000, .parent_name = "pll1_624"},
	{.clk_rate = 528666666, .parent_name = "pll5"},
	{.clk_rate = 624000000, .parent_name = "pll1_624"},
};

static unsigned long gc3d_max_freq = DEFAULT_GC3D_MAX_FREQ;
static struct periph_clk_tbl gc3d_clk1x_tbl_1928[] = {
	{
		.clk_rate = 156000000,
		.parent_name = "pll1_624",
		.comclk_rate = 156000000,
	},
	{
		.clk_rate = 208000000,
		.parent_name = "pll1_624",
		.comclk_rate = 208000000,
	},
	{
		.clk_rate = 312000000,
		.parent_name = "pll1_624",
		.comclk_rate = 312000000,
	},
	{
		.clk_rate = 531555555,
		.parent_name = "pll5p",
		.comclk_rate = 416000000,
	},
	{
		.clk_rate = 624000000,
		.parent_name = "pll1_624",
		.comclk_rate = 416000000,
	},
	{
		.clk_rate = 797333333,
		.parent_name = "pll5",
		.comclk_rate = 416000000,
	},
};

static struct periph_clk_tbl gc3d_clk1x_tbl_1926[] = {
	{
		.clk_rate = 208000000,
		.parent_name = "pll1_624",
		.comclk_rate = 208000000,
	},
	{
		.clk_rate = 312000000,
		.parent_name = "pll1_624",
		.comclk_rate = 312000000,
	},
	{
		.clk_rate = 416000000,
		.parent_name = "pll5",
		.comclk_rate = 312000000,
	},
	{
		.clk_rate = 624000000,
		.parent_name = "pll5",
		.comclk_rate = 312000000,
	},
};

static struct peri_reg_info gc3d_clk1x_reg = {
	.src_sel_shift = 6,
	.src_sel_mask = MASK(REG_WIDTH_2BIT),
	.div_shift = 26,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.flags = CLK_DIVIDER_ONE_BASED,
	.reg_offset = APMU_GC,
	.enable_val = GC3D_CLK_EN,
	.disable_val = GC3D_CLK_EN,
};

static struct peri_reg_info gc3d_clk1x_reg_ax = {
	.src_sel_shift = 12,
	.src_sel_mask = MASK(REG_WIDTH_3BIT),
	.div_shift = 8,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.fcreq_shift = 9,
	.fcreq_mask = MASK(REG_WIDTH_1BIT),
	.rtcwtc_shift = 31,
	.flags = CLK_DIVIDER_ONE_BASED,
	.enable_val = GC3D_CLK1x_EN,
	.disable_val = GC3D_CLK1x_EN,
};

static struct peri_params gc3d_clk1x_params = {
	.clktbl = gc3d_clk1x_tbl,
	.clktblsize = ARRAY_SIZE(gc3d_clk1x_tbl),
	.inputs = gc3d_mux_pll,
	.inputs_size = ARRAY_SIZE(gc3d_mux_pll),
	.dcstat_support = true,
};

static struct peri_params gc3d_clk1x_params_ax = {
	.comclk_name = "GC3D_ACLK",
	.inputs = gc3d1x_mux_pll_ax,
	.inputs_size = ARRAY_SIZE(gc3d1x_mux_pll_ax),
	.cutoff_freq = 416000000,
	.dcstat_support = true,
};

static const char const *gc3d_clk2x_depend[] = {"GC3D_CLK1X",};

static struct periph_clk_tbl gc3d_clk2x_tbl[] = {
	{.clk_rate = 156000000, .parent_name = "pll1_624"},
	{.clk_rate = 208000000, .parent_name = "pll1_624"},
	{.clk_rate = 312000000, .parent_name = "pll1_624"},
	{.clk_rate = 528666666, .parent_name = "pll5"},
	{.clk_rate = 624000000, .parent_name = "pll1_624"},
};

static struct periph_clk_tbl gc3d_clk2x_tbl_1928[] = {
	{
		.clk_rate = 156000000,
		.parent_name = "pll1_624",
		.comclk_rate = 156000000
	},
	{
		.clk_rate = 208000000,
		.parent_name = "pll1_624",
		.comclk_rate = 208000000,
	},
	{
		.clk_rate = 312000000,
		.parent_name = "pll1_624",
		.comclk_rate = 312000000,
	},
	{
		.clk_rate = 531555555,
		.parent_name = "pll5p",
		.comclk_rate = 416000000,
	},
	{
		.clk_rate = 624000000,
		.parent_name = "pll1_624",
		.comclk_rate = 416000000,
	},
	{
		.clk_rate = 797333333,
		.parent_name = "pll5",
		.comclk_rate =	416000000,
	},
};

static struct periph_clk_tbl gc3d_clk2x_tbl_1926[] = {
	{
		.clk_rate = 208000000,
		.parent_name = "pll1_624",
		.comclk_rate = 208000000,
	},
	{
		.clk_rate = 312000000,
		.parent_name = "pll1_624",
		.comclk_rate = 312000000,
	},
	{
		.clk_rate = 416000000,
		.parent_name = "pll5",
		.comclk_rate = 312000000,
	},
};

static struct peri_reg_info gc3d_clk2x_reg = {
	.src_sel_shift = 12,
	.src_sel_mask = MASK(REG_WIDTH_2BIT),
	.div_shift = 29,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.flags = CLK_DIVIDER_ONE_BASED,
	.reg_offset = APMU_GC,
	.enable_val = GC3D_CLK_EN,
	.disable_val = GC3D_CLK_EN,
};

static struct peri_reg_info gc3d_clk2x_reg_ax = {
	.src_sel_shift = 20,
	.src_sel_mask = MASK(REG_WIDTH_3BIT),
	.div_shift = 16,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.fcreq_shift = 9,
	.fcreq_mask = MASK(REG_WIDTH_1BIT),
	.rtcwtc_shift = 31,
	.flags = CLK_DIVIDER_ONE_BASED,
	.enable_val = GC3D_CLK2x_EN,
	.disable_val = GC3D_CLK2x_EN,
};

static struct peri_params gc3d_clk2x_params = {
	.clktbl = gc3d_clk2x_tbl,
	.clktblsize = ARRAY_SIZE(gc3d_clk2x_tbl),
	.inputs = gc3d_mux_pll,
	.inputs_size = ARRAY_SIZE(gc3d_mux_pll),
	.dcstat_support = true,
};

static struct peri_params gc3d_clk2x_params_ax = {
	.comclk_name = "GC3D_ACLK",
	.inputs = gc3d2x_mux_pll_ax,
	.inputs_size = ARRAY_SIZE(gc3d2x_mux_pll_ax),
	.cutoff_freq = 416000000,
	.dcstat_support = true,
};

static const char const *gc2d_clk_depend[] = {"GC3D_ACLK", "GC2D_ACLK",};

static struct periph_clk_tbl gc2d_clk_tbl[] = {
	{.clk_rate = 156000000, .parent_name = "pll1_624"},
	{.clk_rate = 208000000, .parent_name = "pll1_624"},
	{.clk_rate = 312000000, .parent_name = "pll1_624"},
	{.clk_rate = 528666666, .parent_name = "pll5"},
	{.clk_rate = 624000000, .parent_name = "pll1_624"},
};

static unsigned long gc2d_max_freq = DEFAULT_GC2D_MAX_FREQ;
static struct periph_clk_tbl gc2d_clk_tbl_1928[] = {
	{
		.clk_rate = 156000000,
		.parent_name = "pll1_624",
		.comclk_rate = 208000000,
	},
	{
		.clk_rate = 208000000,
		.parent_name = "pll1_416",
		.comclk_rate = 416000000,
	},
	{
		.clk_rate = 312000000,
		.parent_name = "pll1_624",
		.comclk_rate = 416000000,
	},
	{
		.clk_rate = 416000000,
		.parent_name = "pll1_416",
		.comclk_rate = 416000000,
	},
};

static struct periph_clk_tbl gc2d_clk_tbl_1926[] = {
	{
		.clk_rate = 208000000,
		.parent_name = "pll1_416",
		.comclk_rate = 416000000,
	},
	{
		.clk_rate = 312000000,
		.parent_name = "pll1_624",
		.comclk_rate = 416000000,
	},
};

static struct peri_reg_info gc2d_clk_reg = {
	.src_sel_shift = 16,
	.src_sel_mask = MASK(REG_WIDTH_2BIT),
	.div_shift = 20,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.flags = CLK_DIVIDER_ONE_BASED,
	.reg_offset = APMU_GC2,
	.enable_val = GC2D_CLK_EN,
	.disable_val = GC2D_CLK_EN,
};

static struct peri_reg_info gc2d_clk_reg_ax = {
	.src_sel_shift = 12,
	.src_sel_mask = MASK(REG_WIDTH_3BIT),
	.div_shift = 8,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.fcreq_shift = 9,
	.fcreq_mask = MASK(REG_WIDTH_1BIT),
	.flags = CLK_DIVIDER_ONE_BASED,
	.enable_val = GC2D_CLK1x_EN,
	.disable_val = GC2D_CLK1x_EN,
};

static struct peri_params gc2d_clk_params = {
	.clktbl = gc2d_clk_tbl,
	.clktblsize = ARRAY_SIZE(gc2d_clk_tbl),
	.inputs = gc2d_mux_pll,
	.inputs_size = ARRAY_SIZE(gc2d_mux_pll),
	.dcstat_support = true,
};

static struct peri_params gc2d_clk_params_ax = {
	.comclk_name = "axi11",
	.inputs = gc2d_mux_pll_ax,
	.inputs_size = ARRAY_SIZE(gc2d_mux_pll_ax),
	.cutoff_freq = 311000000,
	.dcstat_support = true,
};

struct plat_clk_list gc_clk_list[] = {
	{NULL, "GC3D_ACLK", 156000000},
	{NULL, "GC3D_CLK1X", 156000000},
	{NULL, "GC3D_CLK2X", 156000000},
	{NULL, "GC2D_CLK", 156000000},
};

static int __init gc3d_max_freq_set(char *str)
{
	int freq;

	if (!get_option(&str, &freq))
		return 0;

	if (freq < 156000) {
		pr_err("Too low gc3d max freq parameter!\n");
		return 0;
	}

	if (freq >= 797000)
		gc3d_max_freq = DEFAULT_GC3D_MAX_FREQ;
	else
		gc3d_max_freq = freq * 1000;

	return 1;
}
__setup("gc3d_max=", gc3d_max_freq_set);

static int __init gc2d_max_freq_set(char *str)
{
	int freq;

	if (!get_option(&str, &freq))
		return 0;

	if (freq < 156000) {
		pr_err("Too low gc2d max freq parameter!\n");
		return 0;
	}

	if (freq >= 416000)
		gc2d_max_freq = DEFAULT_GC3D_MAX_FREQ;
	else
		gc2d_max_freq = freq * 1000;

	return 1;
}
__setup("gc2d_max=", gc2d_max_freq_set);

static void __init peri_init_set_rate(struct plat_clk_list *peri_clk_list,
					int size)
{
	int i;
	struct clk *clk;
	for (i = 0; i < size; i++) {
		clk = clk_get_sys(peri_clk_list[i].dev_id,
				peri_clk_list[i].con_id);
		if (IS_ERR(clk)) {
			pr_err(" can't find clk %s\n",
				peri_clk_list[i].con_id);
			continue;
		}
		clk_set_rate(clk, peri_clk_list[i].initrate);
	}
}

/* interface used by GC driver to get avaliable GC frequencies, unit HZ */
/* Get GC3D core frequency */
int get_gc3d_freqs_table(unsigned long *gcu3d_freqs_table,
	unsigned int *item_counts, unsigned int max_item_counts)
{
	struct periph_clk_tbl *gc3d_tbl;
	unsigned int index, tbl_len;

	*item_counts = 0;

	gc3d_tbl = !pxa1928_is_zx ? (pp_is_1926 ? gc3d_clk1x_tbl_1926 :
				gc3d_clk1x_tbl_1928) : gc3d_clk1x_tbl;
	tbl_len = !pxa1928_is_zx ? (pp_is_1926 ? ARRAY_SIZE(gc3d_clk1x_tbl_1926) :
				ARRAY_SIZE(gc3d_clk1x_tbl_1928)) : ARRAY_SIZE(gc3d_clk1x_tbl);

	if (!gcu3d_freqs_table) {
		pr_err("%s NULL ptr!\n", __func__);
		return -EINVAL;
	}

	if (max_item_counts < tbl_len) {
		pr_err("%s Too many GC frequencies %u!\n", __func__,
			max_item_counts);
		return -EINVAL;
	}

	for (index = 0; index < tbl_len; index++) {
		if (gc3d_tbl[index].clk_rate > gc3d_max_freq)
			break;
		gcu3d_freqs_table[index] = gc3d_tbl[index].clk_rate;
	}
	*item_counts = index;
	return 0;
}
EXPORT_SYMBOL(get_gc3d_freqs_table);

/* Get GC3D shader frequency */
int get_gc3d_sh_freqs_table(unsigned long *gcu3d_sh_freqs_table,
	unsigned int *item_counts, unsigned int max_item_counts)
{
	struct periph_clk_tbl *gc3d_tbl;
	unsigned int index, tbl_len;

	*item_counts = 0;

	gc3d_tbl = !pxa1928_is_zx ? (pp_is_1926 ? gc3d_clk2x_tbl_1926 :
			gc3d_clk2x_tbl_1928) : gc3d_clk2x_tbl;
	tbl_len = !pxa1928_is_zx ? (pp_is_1926 ? ARRAY_SIZE(gc3d_clk2x_tbl_1926) :
			ARRAY_SIZE(gc3d_clk2x_tbl_1928)) : ARRAY_SIZE(gc3d_clk2x_tbl);

	if (!gcu3d_sh_freqs_table) {
		pr_err("%s NULL ptr!\n", __func__);
		return -EINVAL;
	}

	if (max_item_counts < tbl_len) {
		pr_err("%s Too many GC frequencies %u!\n", __func__,
			max_item_counts);
		return -EINVAL;
	}

	for (index = 0; index < tbl_len; index++) {
		if (gc3d_tbl[index].clk_rate > gc3d_max_freq)
			break;
		gcu3d_sh_freqs_table[index] = gc3d_tbl[index].clk_rate;
	}
	*item_counts = index;
	return 0;
}
EXPORT_SYMBOL(get_gc3d_sh_freqs_table);

/* Get GC2D frequency */
int get_gc2d_freqs_table(unsigned long *gcu2d_freqs_table,
	unsigned int *item_counts, unsigned int max_item_counts)
{
	struct periph_clk_tbl *gc2d_tbl;
	unsigned int index, tbl_len;

	*item_counts = 0;

	gc2d_tbl = !pxa1928_is_zx ? (pp_is_1926 ? gc2d_clk_tbl_1926 :
			gc2d_clk_tbl_1928) : gc2d_clk_tbl;
	tbl_len = !pxa1928_is_zx ? (pp_is_1926 ? ARRAY_SIZE(gc2d_clk_tbl_1926) :
			ARRAY_SIZE(gc2d_clk_tbl_1928)) : ARRAY_SIZE(gc2d_clk_tbl);

	if (!gcu2d_freqs_table) {
		pr_err("%s NULL ptr!\n", __func__);
		return -EINVAL;
	}

	if (max_item_counts < tbl_len) {
		pr_err("%s Too many GC frequencies %u!\n", __func__,
			max_item_counts);
		return -EINVAL;
	}

	for (index = 0; index < tbl_len; index++) {
		if (gc2d_tbl[index].clk_rate > gc2d_max_freq)
			break;
		gcu2d_freqs_table[index] = gc2d_tbl[index].clk_rate;
	}
	*item_counts = index;
	return 0;
}
EXPORT_SYMBOL(get_gc2d_freqs_table);

#ifdef CONFIG_UIO_HANTRO
/* For Zx */
#define VPU_DEC_CLK_EN			(1 << 27)
#define VPU_AXI_CLK_EN			(1 << 3)
#define VPU_ENC_CLK_EN			(1 << 4)

/* For B0 */
#define VPU_ECLK_EN			(1 << 23)
#define VPU_DCLK_EN			(1 << 15)
#define VPU_ACLK_EN			(1 << 7)

static struct xpu_rtcwtc vpu_rtcwtc_a0[] = {
	{.max_rate = 208000000, .rtcwtc = 0x55550000,},
	{.max_rate = 624000000, .rtcwtc = 0xa9aa0000,},
};

static struct xpu_rtcwtc vpu_rtcwtc_b0[] = {
	{.max_rate = 624000000, .rtcwtc = 0xa9aa0000,},
};

static struct periph_clk_tbl vpu_aclk_tbl[] = {
	{.clk_rate = 104000000, .parent_name = "pll1_624"},
	{.clk_rate = 156000000, .parent_name = "pll1_624"},
	{.clk_rate = 208000000, .parent_name = "pll1_624"},
	{.clk_rate = 264333333, .parent_name = "pll5"},
	{.clk_rate = 312000000, .parent_name = "pll1_624"},
	{.clk_rate = 416000000, .parent_name = "pll1_416"},
};

static struct periph_clk_tbl vpu_aclk_tbl_ax[] = {
	{.clk_rate = 104000000, .parent_name = "pll1_624"},
	{.clk_rate = 156000000, .parent_name = "pll1_624"},
	{.clk_rate = 208000000, .parent_name = "pll1_416"},
	{.clk_rate = 312000000, .parent_name = "pll1_624"},
	{.clk_rate = 416000000, .parent_name = "pll1_416"},
};

static const char const *vpu_parents[] = {"pll1_624", "pll5p",
					"pll5", "pll1_416",};

static const char const *vpu_parents_ax[] = {"pll1_624", "pll5p",
						"pll1_416", "pll7"};

static const char const *vpu_depend_clk[] = {"VPU_AXI_CLK",};
static struct clk_mux_sel vpu_mux_sel[] = {
	{.parent_name = "pll1_624", .value = 0},
	{.parent_name = "pll5p", .value = 1},
	{.parent_name = "pll5", .value = 2},
	{.parent_name = "pll1_416", .value = 3},
};

static struct clk_mux_sel vpu_mux_sel_ax[] = {
	{.parent_name = "pll1_624", .value = 0},
	{.parent_name = "pll5p", .value = 1},
	{.parent_name = "pll1_416", .value = 2},
	{.parent_name = "pll7", .value = 6},
};

static struct peri_reg_info vpu_aclk_reg = {
	.src_sel_shift = 12,
	.src_sel_mask = MASK(REG_WIDTH_2BIT),
	.div_shift = 19,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.flags = CLK_DIVIDER_ONE_BASED,
	.reg_offset = APMU_VPU,
	.enable_val = VPU_AXI_CLK_EN,
	.disable_val = VPU_AXI_CLK_EN,
};

static struct peri_reg_info vpu_aclk_reg_ax = {
	.src_sel_shift = 4,
	.src_sel_mask = MASK(REG_WIDTH_3BIT),
	.div_shift = 0,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.fcreq_shift = 9,
	.fcreq_mask = MASK(REG_WIDTH_1BIT),
	.flags = CLK_DIVIDER_ONE_BASED,
	.enable_val = VPU_ACLK_EN,
	.disable_val = VPU_ACLK_EN,
};

static struct peri_params vpu_aclk_params = {
	.clktbl = vpu_aclk_tbl,
	.clktblsize = ARRAY_SIZE(vpu_aclk_tbl),
	.inputs = vpu_mux_sel,
	.inputs_size = ARRAY_SIZE(vpu_mux_sel),
};

static struct peri_params vpu_aclk_params_ax = {
	.clktbl = vpu_aclk_tbl_ax,
	.clktblsize = ARRAY_SIZE(vpu_aclk_tbl_ax),
	.inputs = vpu_mux_sel_ax,
	.inputs_size = ARRAY_SIZE(vpu_mux_sel_ax),
	.cutoff_freq = 208000000,
};

static struct periph_clk_tbl vpu_dclk_tbl[] = {
	{
		.clk_rate = 104000000,
		.parent_name = "pll1_624",
		.comclk_rate = 104000000,
	},
	{
		.clk_rate = 132166666,
		.parent_name = "pll5",
		.comclk_rate = 156000000,
	},
	{
		.clk_rate = 156000000,
		.parent_name = "pll1_624",
		.comclk_rate = 156000000,
	},
	{
		.clk_rate = 208000000,
		.parent_name = "pll1_624",
		.comclk_rate = 208000000
	},
	{
		.clk_rate = 264333333,
		.parent_name = "pll5",
		.comclk_rate = 312000000,
	},
	{
		.clk_rate = 312000000,
		.parent_name = "pll1_624",
		.comclk_rate = 312000000,
	},
	{
		.clk_rate = 416000000,
		.parent_name = "pll1_416",
		.comclk_rate = 416000000,
	},
};

static struct periph_clk_tbl vpu_dclk_tbl_ax[] = {
	{
		.clk_rate = 104000000,
		.parent_name = "pll1_624",
		.comclk_rate = 104000000,
	},
	{
		.clk_rate = 156000000,
		.parent_name = "pll1_624",
		.comclk_rate = 156000000,
	},
	{
		.clk_rate = 208000000,
		.parent_name = "pll1_416",
		.comclk_rate = 208000000
	},
	{
		.clk_rate = 312000000,
		.parent_name = "pll1_624",
		.comclk_rate = 312000000,
	},
	{
		.clk_rate = 416000000,
		.parent_name = "pll1_416",
		.comclk_rate = 416000000,
	},
};

static struct peri_reg_info vpu_dclk_reg = {
	.src_sel_shift = 22,
	.src_sel_mask = MASK(REG_WIDTH_2BIT),
	.div_shift = 24,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.flags = CLK_DIVIDER_ONE_BASED,
	.reg_offset = APMU_VPU,
	.enable_val = VPU_DEC_CLK_EN,
	.disable_val = VPU_DEC_CLK_EN,
};

static struct peri_reg_info vpu_dclk_reg_ax = {
	.src_sel_shift = 12,
	.src_sel_mask = MASK(REG_WIDTH_3BIT),
	.div_shift = 8,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.fcreq_shift = 9,
	.fcreq_mask = MASK(REG_WIDTH_1BIT),
	.flags = CLK_DIVIDER_ONE_BASED,
	.enable_val = VPU_DCLK_EN,
	.disable_val = VPU_DCLK_EN,
};

static struct peri_params vpu_dclk_params = {
	.clktbl = vpu_dclk_tbl,
	.clktblsize = ARRAY_SIZE(vpu_dclk_tbl),
	.comclk_name = "VPU_AXI_CLK",
	.inputs = vpu_mux_sel,
	.inputs_size = ARRAY_SIZE(vpu_mux_sel),
	.dcstat_support = true,
};

static struct peri_params vpu_dclk_params_ax = {
	.clktbl = vpu_dclk_tbl_ax,
	.clktblsize = ARRAY_SIZE(vpu_dclk_tbl_ax),
	.comclk_name = "VPU_AXI_CLK",
	.inputs = vpu_mux_sel_ax,
	.inputs_size = ARRAY_SIZE(vpu_mux_sel_ax),
	.cutoff_freq = 208000000,
	.dcstat_support = true,
};

static struct periph_clk_tbl vpu_eclk_tbl[] = {
	{
		.clk_rate = 156000000,
		.parent_name = "pll1_624",
		.comclk_rate = 156000000,
	},
	{
		.clk_rate = 208000000,
		.parent_name = "pll1_624",
		.comclk_rate = 208000000
	},
	{
		.clk_rate = 264333333,
		.parent_name = "pll5",
		.comclk_rate = 264333333,
	},
	{
		.clk_rate = 312000000,
		.parent_name = "pll1_624",
		.comclk_rate = 312000000,
	},
	{
		.clk_rate = 416000000,
		.parent_name = "pll1_416",
		.comclk_rate = 416000000,
	},
};

static struct periph_clk_tbl vpu_eclk_tbl_1928[] = {
	{
		.clk_rate = 156000000,
		.parent_name = "pll1_624",
		.comclk_rate = 156000000,
	},
	{
		.clk_rate = 208000000,
		.parent_name = "pll1_416",
		.comclk_rate = 208000000
	},
	{
		.clk_rate = 312000000,
		.parent_name = "pll1_624",
		.comclk_rate = 312000000,
	},
	{
		.clk_rate = 416000000,
		.parent_name = "pll1_416",
		.comclk_rate = 416000000,
	},
	{
		.clk_rate = 531555555,
		.parent_name = "pll5p",
		.comclk_rate = 416000000,
	},
};

static struct periph_clk_tbl vpu_eclk_tbl_1926[] = {
	{
		.clk_rate = 156000000,
		.parent_name = "pll1_624",
		.comclk_rate = 156000000,
	},
	{
		.clk_rate = 208000000,
		.parent_name = "pll1_624",
		.comclk_rate = 208000000
	},
	{
		.clk_rate = 312000000,
		.parent_name = "pll1_624",
		.comclk_rate = 312000000,
	},
	{
		.clk_rate = 416000000,
		.parent_name = "pll1_416",
		.comclk_rate = 416000000,
	},
};

static struct peri_reg_info vpu_eclk_reg = {
	.src_sel_shift = 6,
	.src_sel_mask = MASK(REG_WIDTH_2BIT),
	.div_shift = 16,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.flags = CLK_DIVIDER_ONE_BASED,
	.reg_offset = APMU_VPU,
	.enable_val = VPU_ENC_CLK_EN,
	.disable_val = VPU_ENC_CLK_EN,
};

static struct peri_reg_info vpu_eclk_reg_ax = {
	.src_sel_shift = 20,
	.src_sel_mask = MASK(REG_WIDTH_3BIT),
	.div_shift = 16,
	.div_mask = MASK(REG_WIDTH_3BIT),
	.fcreq_shift = 9,
	.fcreq_mask = MASK(REG_WIDTH_1BIT),
	.flags = CLK_DIVIDER_ONE_BASED,
	.enable_val = VPU_ECLK_EN,
	.disable_val = VPU_ECLK_EN,
};

static struct peri_params vpu_eclk_params = {
	.clktbl = vpu_eclk_tbl,
	.clktblsize = ARRAY_SIZE(vpu_eclk_tbl),
	.comclk_name = "VPU_AXI_CLK",
	.inputs = vpu_mux_sel,
	.inputs_size = ARRAY_SIZE(vpu_mux_sel),
	.dcstat_support = true,
};

static struct peri_params vpu_eclk_params_ax = {
	.comclk_name = "VPU_AXI_CLK",
	.inputs = vpu_mux_sel_ax,
	.inputs_size = ARRAY_SIZE(vpu_mux_sel_ax),
	.cutoff_freq = 208000000,
	.dcstat_support = true,
};

struct plat_clk_list vpu_clk_list[] = {
	{NULL, "VPU_AXI_CLK", 208000000},
	{NULL, "VPU_DEC_CLK", 208000000},
	{NULL, "VPU_ENC_CLK", 208000000},
};

#define VPU_DEVFREQ_DEC	0
#define VPU_DEVFREQ_ENC	1

unsigned int pxa1928_get_vpu_op_num(unsigned int vpu_type)
{
	int dclk_tbl_len, eclk_tbl_len;

	dclk_tbl_len = !pxa1928_is_zx ? ARRAY_SIZE(vpu_dclk_tbl_ax) :
					ARRAY_SIZE(vpu_dclk_tbl);
	eclk_tbl_len = !pxa1928_is_zx ?
			((pp_is_1926) ? ARRAY_SIZE(vpu_eclk_tbl_1926) :
			 ARRAY_SIZE(vpu_eclk_tbl_1928)) : ARRAY_SIZE(vpu_eclk_tbl);

	switch (vpu_type) {
	case VPU_DEVFREQ_DEC:
		return dclk_tbl_len;
	case VPU_DEVFREQ_ENC:
		return eclk_tbl_len;
	default:
		pr_err("%s, unable to find the clock table!\n",
				__func__);
		return 0;
	}
}

unsigned int pxa1928_get_vpu_op_rate(unsigned int vpu_type, unsigned int index)
{
	struct periph_clk_tbl *clk_tbl;
	unsigned int tbl_size = 0;

	switch (vpu_type) {
	case VPU_DEVFREQ_DEC:
		clk_tbl = pxa1928_is_zx ? vpu_dclk_tbl : vpu_dclk_tbl_ax;
		tbl_size = pxa1928_is_zx ? ARRAY_SIZE(vpu_dclk_tbl) :
					ARRAY_SIZE(vpu_dclk_tbl_ax);
		break;
	case VPU_DEVFREQ_ENC:
		clk_tbl = !pxa1928_is_zx ? (pp_is_1926 ? vpu_eclk_tbl_1926 :
				vpu_eclk_tbl_1928) : vpu_eclk_tbl;
		tbl_size = !pxa1928_is_zx ? (pp_is_1926 ? ARRAY_SIZE(vpu_eclk_tbl_1926) :
				ARRAY_SIZE(vpu_eclk_tbl_1928)) : ARRAY_SIZE(vpu_eclk_tbl);
		break;
	default:
		pr_err("%s, unable to find the clock table!\n",
				__func__);
		return 0;
	}

	WARN_ON(index >= tbl_size);

	return clk_tbl[index].clk_rate / MHZ_TO_KHZ;
}

#ifdef CONFIG_VPU_DEVFREQ
static struct devfreq_frequency_table *vpu_devfreq_tbl;

static void __init_vpu_devfreq_table(unsigned int vpu_type)
{
	unsigned int vpu_freq_num = 0, i = 0;

	vpu_freq_num = pxa1928_get_vpu_op_num(vpu_type);
	vpu_devfreq_tbl =
		kmalloc(sizeof(struct devfreq_frequency_table)
				* (vpu_freq_num + 1), GFP_KERNEL);

	if (!vpu_devfreq_tbl)
		return;

	for (i = 0; i < vpu_freq_num; i++) {
		vpu_devfreq_tbl[i].index = i;
		vpu_devfreq_tbl[i].frequency =
			pxa1928_get_vpu_op_rate(vpu_type, i);
	}

	vpu_devfreq_tbl[i].index = i;
	vpu_devfreq_tbl[i].frequency = DEVFREQ_TABLE_END;

	devfreq_frequency_table_register(vpu_devfreq_tbl,
			DEVFREQ_VPU_0 + vpu_type);
}
#endif

static void __init pxa1928_vpu_clk_init(void __iomem *apmu_base)
{
	struct clk *clk;

	if (!pxa1928_is_zx) {
		clk = pxa1928_clk_register_gc_vpu("vpu_axi_clk", vpu_parents_ax,
				ARRAY_SIZE(vpu_parents_ax), 0,
				apmu_base + APMU_VPU_CLKCTRL,
				apmu_base + APMU_VPU_RSTCTRL, &vpu_lock,
				&vpu_aclk_params_ax, &vpu_aclk_reg_ax, NULL, 0);
		clk_register_clkdev(clk, "VPU_AXI_CLK", NULL);
		clks[vpu_aclk] = clk;

		vpu_dclk_reg_ax.reg_rtcwtc = apmu_base + APMU_ISLD_VPU_CTRL;
		vpu_dclk_params_ax.rwtctbl = stepping_is_a0 ? vpu_rtcwtc_a0 : vpu_rtcwtc_b0;
		vpu_dclk_params_ax.rwtctblsize = stepping_is_a0 ? ARRAY_SIZE(vpu_rtcwtc_a0) :
								ARRAY_SIZE(vpu_rtcwtc_b0);
		clk = pxa1928_clk_register_gc_vpu("vpu_dec_clk", vpu_parents_ax,
				ARRAY_SIZE(vpu_parents_ax), 0,
				apmu_base + APMU_VPU_CLKCTRL,
				apmu_base + APMU_VPU_RSTCTRL, &vpu_lock,
				&vpu_dclk_params_ax, &vpu_dclk_reg_ax, NULL, 0);
		clk_register_clkdev(clk, "VPU_DEC_CLK", NULL);
		clk_register_clkdev(clk, NULL, "devfreq-vpu.0");
		clks[vpu_dec] = clk;

		vpu_eclk_reg_ax.reg_rtcwtc = apmu_base + APMU_ISLD_VPU_CTRL;
		vpu_eclk_params_ax.clktbl = pp_is_1926 ?
				vpu_eclk_tbl_1926 : vpu_eclk_tbl_1928;
		vpu_eclk_params_ax.clktblsize = pp_is_1926 ?
				ARRAY_SIZE(vpu_eclk_tbl_1926) : ARRAY_SIZE(vpu_eclk_tbl_1928);
		vpu_eclk_params_ax.rwtctbl = stepping_is_a0 ? vpu_rtcwtc_a0 : vpu_rtcwtc_b0;
		vpu_eclk_params_ax.rwtctblsize = stepping_is_a0 ? ARRAY_SIZE(vpu_rtcwtc_a0) :
								ARRAY_SIZE(vpu_rtcwtc_b0);
		clk = pxa1928_clk_register_gc_vpu("vpu_enc_clk", vpu_parents_ax,
				ARRAY_SIZE(vpu_parents_ax), 0,
				apmu_base + APMU_VPU_CLKCTRL,
				apmu_base + APMU_VPU_RSTCTRL, &vpu_lock,
				&vpu_eclk_params_ax, &vpu_eclk_reg_ax, NULL, 0);
		clk_register_clkdev(clk, "VPU_ENC_CLK", NULL);
		clk_register_clkdev(clk, NULL, "devfreq-vpu.1");
		clks[vpu_enc] = clk;

		vpu_dclk_params_ax.sibling_clk = clks[vpu_enc];
		vpu_eclk_params_ax.sibling_clk = clks[vpu_dec];
	} else {
		clk = pxa1928_clk_register_gc_vpu("vpu_axi_clk", vpu_parents,
				ARRAY_SIZE(vpu_parents), 0,
				apmu_base + APMU_VPU, 0, &vpu_lock,
				&vpu_aclk_params, &vpu_aclk_reg, NULL, 0);
		clk_register_clkdev(clk, "VPU_AXI_CLK", NULL);
		clks[vpu_aclk] = clk;

		clk = pxa1928_clk_register_gc_vpu("vpu_dec_clk", vpu_parents,
				ARRAY_SIZE(vpu_parents), 0,
				apmu_base + APMU_VPU, 0, &vpu_lock,
				&vpu_dclk_params, &vpu_dclk_reg,
				vpu_depend_clk, ARRAY_SIZE(vpu_depend_clk));
		clk_register_clkdev(clk, "VPU_DEC_CLK", NULL);
		clk_register_clkdev(clk, NULL, "devfreq-vpu.0");
		clks[vpu_dec] = clk;

		clk = pxa1928_clk_register_gc_vpu("vpu_enc_clk", vpu_parents,
				ARRAY_SIZE(vpu_parents), 0,
				apmu_base + APMU_VPU, 0, &vpu_lock,
				&vpu_eclk_params, &vpu_eclk_reg,
				vpu_depend_clk, ARRAY_SIZE(vpu_depend_clk));
		clk_register_clkdev(clk, "VPU_ENC_CLK", NULL);
		clk_register_clkdev(clk, NULL, "devfreq-vpu.1");
		clks[vpu_enc] = clk;
	}

#ifdef CONFIG_VPU_DEVFREQ
	__init_vpu_devfreq_table(VPU_DEVFREQ_DEC);
	__init_vpu_devfreq_table(VPU_DEVFREQ_ENC);
#endif

	peri_init_set_rate(vpu_clk_list, ARRAY_SIZE(vpu_clk_list));
}
#endif

#define APMU_ACLK11_CLKCTRL	0x120
static void __init pxa1928_gc_clk_init(void __iomem *apmu_base)
{
	struct clk *clk;

	if (!pxa1928_is_zx) {
		clk = pxa1928_clk_register_gc_vpu("gc3d_aclk",
				gc3d_aclk_parents_ax,
				ARRAY_SIZE(gc3d_aclk_parents_ax), 0,
				apmu_base + APMU_GC3D_CLKCTRL,
				apmu_base + APMU_GC3D_RSTCTRL,
				&gc3d_lock, &gc3d_aclk_params_ax,
				&gc3d_aclk_reg_ax, NULL, 0);
		clk_register_clkdev(clk, "GC3D_ACLK", NULL);
		clks[gc3d_aclk] = clk;

		gc3d_clk1x_reg_ax.reg_rtcwtc = apmu_base + APMU_ISLD_GC_CTRL;
		gc3d_clk1x_params_ax.clktbl = pp_is_1926 ? gc3d_clk1x_tbl_1926 :
							gc3d_clk1x_tbl_1928;
		gc3d_clk1x_params_ax.clktblsize = pp_is_1926 ?
			ARRAY_SIZE(gc3d_clk1x_tbl_1926) : ARRAY_SIZE(gc3d_clk1x_tbl_1928);
		gc3d_clk1x_params_ax.rwtctbl = stepping_is_a0 ? gc_rtcwtc_a0 : gc_rtcwtc_b0;
		gc3d_clk1x_params_ax.rwtctblsize = stepping_is_a0 ? ARRAY_SIZE(gc_rtcwtc_a0) :
								ARRAY_SIZE(gc_rtcwtc_b0);
		clk = pxa1928_clk_register_gc_vpu("gc3d_clk1x",
				gc3d_clk_parents_ax,
				ARRAY_SIZE(gc3d_clk_parents_ax), 0,
				apmu_base + APMU_GC3D_CLKCTRL,
				apmu_base + APMU_GC3D_RSTCTRL,
				&gc3d_lock, &gc3d_clk1x_params_ax,
				&gc3d_clk1x_reg_ax, NULL, 0);
		clk_register_clkdev(clk, "GC3D_CLK1X", NULL);
		clks[gc3d_1x] = clk;

		gc3d_clk2x_reg_ax.reg_rtcwtc = apmu_base + APMU_ISLD_GC_CTRL;
		gc3d_clk2x_params_ax.clktbl = pp_is_1926 ? gc3d_clk2x_tbl_1926 :
						gc3d_clk2x_tbl_1928;
		gc3d_clk2x_params_ax.clktblsize = pp_is_1926 ?
			ARRAY_SIZE(gc3d_clk2x_tbl_1926) : ARRAY_SIZE(gc3d_clk2x_tbl_1928);
		gc3d_clk2x_params_ax.rwtctbl = stepping_is_a0 ? gc_rtcwtc_a0 : gc_rtcwtc_b0;
		gc3d_clk2x_params_ax.rwtctblsize = stepping_is_a0 ? ARRAY_SIZE(gc_rtcwtc_a0) :
								ARRAY_SIZE(gc_rtcwtc_b0);
		clk = pxa1928_clk_register_gc_vpu("gc3d_clk2x",
				gc3d_clk_parents_ax,
				ARRAY_SIZE(gc3d_clk_parents_ax), 0,
				apmu_base + APMU_GC3D_CLKCTRL,
				apmu_base + APMU_GC3D_RSTCTRL,
				&gc3d_lock, &gc3d_clk2x_params_ax,
				&gc3d_clk2x_reg_ax, NULL, 0);
		clk_register_clkdev(clk, "GC3D_CLK2X", NULL);
		clks[gc3d_2x] = clk;

		clk = pxa1928_clk_register_gc_vpu("gc2d_aclk",
				NULL, 0, 0,
				apmu_base + APMU_GC2D_CLKCTRL, NULL,
				&gc2d_lock, &gc2d_aclk_params_ax,
				&gc2d_aclk_reg_ax, NULL, 0);
		clk_register_clkdev(clk, "GC2D_ACLK", NULL);
		clks[gc2d_aclk] = clk;

		gc2d_clk_params_ax.clktbl = pp_is_1926 ? gc2d_clk_tbl_1926 :
							gc2d_clk_tbl_1928;
		gc2d_clk_params_ax.clktblsize = pp_is_1926 ?
				ARRAY_SIZE(gc2d_clk_tbl_1926) : ARRAY_SIZE(gc2d_clk_tbl_1928);
		gc2d_clk_reg_ax.reg_rtcwtc = apmu_base + APMU_ISLD_GC2D_CTRL;
		gc2d_clk_reg_ax.reg_debug = apmu_base + APMU_AP_DEBUG3;
		gc2d_clk_params_ax.rwtctbl = stepping_is_a0 ? gc2d_rtcwtc_a0 : gc2d_rtcwtc_b0;
		gc2d_clk_params_ax.rwtctblsize = stepping_is_a0 ? ARRAY_SIZE(gc2d_rtcwtc_a0) :
								ARRAY_SIZE(gc2d_rtcwtc_b0);
		clk = pxa1928_clk_register_gc_vpu("gc2d_clk",
				gc2d_clk_parents_ax,
				ARRAY_SIZE(gc2d_clk_parents_ax), 0,
				apmu_base + APMU_GC2D_CLKCTRL,
				apmu_base + APMU_GC2D_RSTCTRL,
				&gc2d_lock, &gc2d_clk_params_ax,
				&gc2d_clk_reg_ax, NULL, 0);
		clk_register_clkdev(clk, "GC2D_CLK", NULL);
		clks[gc2d] = clk;
	} else {
		clk = pxa1928_clk_register_gc_vpu("gc3d_aclk", gc_aclk_parents,
				ARRAY_SIZE(gc_aclk_parents), 0,
				apmu_base + APMU_GC, 0,
				&gc3d_lock, &gc3d_aclk_params,
				&gc3d_aclk_reg, NULL, 0);
		clk_register_clkdev(clk, "GC3D_ACLK", NULL);
		clks[gc3d_aclk] = clk;

		clk = pxa1928_clk_register_gc_vpu("gc3d_clk1x",
				gc3d_clk_parents,
				ARRAY_SIZE(gc3d_clk_parents), 0,
				apmu_base + APMU_GC, 0,
				&gc3d_lock, &gc3d_clk1x_params,
				&gc3d_clk1x_reg, gc3d_clk1x_depend,
				ARRAY_SIZE(gc3d_clk1x_depend));
		clk_register_clkdev(clk, "GC3D_CLK1X", NULL);
		clks[gc3d_1x] = clk;

		clk = pxa1928_clk_register_gc_vpu("gc3d_clk2x",
				gc3d_clk_parents,
				ARRAY_SIZE(gc3d_clk_parents), 0,
				apmu_base + APMU_GC, 0,
				&gc3d_lock, &gc3d_clk2x_params,
				&gc3d_clk2x_reg, gc3d_clk2x_depend,
				ARRAY_SIZE(gc3d_clk2x_depend));
		clk_register_clkdev(clk, "GC3D_CLK2X", NULL);
		clks[gc3d_2x] = clk;

		clk = pxa1928_clk_register_gc_vpu("gc2d_aclk", gc_aclk_parents,
				ARRAY_SIZE(gc_aclk_parents), 0,
				apmu_base + APMU_GC2, 0,
				&gc2d_lock, &gc2d_aclk_params,
				&gc2d_aclk_reg, NULL, 0);
		clk_register_clkdev(clk, "GC2D_ACLK", NULL);
		clks[gc2d_aclk] = clk;

		clk = pxa1928_clk_register_gc_vpu("gc2d_clk", gc2d_clk_parents,
				ARRAY_SIZE(gc2d_clk_parents), 0,
				apmu_base + APMU_GC2, 0,
				&gc2d_lock, &gc2d_clk_params, &gc2d_clk_reg,
				gc2d_clk_depend, ARRAY_SIZE(gc2d_clk_depend));
		clk_register_clkdev(clk, "GC2D_CLK", NULL);
		clks[gc2d] = clk;
	}

	peri_init_set_rate(gc_clk_list, ARRAY_SIZE(gc_clk_list));
}

#ifdef CONFIG_THERMAL
static void __init pxa1928_thermal_clk_init(void __iomem *apbc_base)
{
	struct clk *clk;

	clk = mmp_clk_register_apbc("thermal_g", "NULL",
				apbc_base + APBC_THSENS, 10, 0, &clk_lock);
	clk_register_clkdev(clk, "THERMALCLK_G", NULL);
	clk = mmp_clk_register_apbc("thermal_vpu", "NULL",
				apbc_base + APBC_THSENS + 0x8,
				10, 0, &clk_lock);
	clk_register_clkdev(clk, "THERMALCLK_VPU", NULL);
	clk = mmp_clk_register_apbc("thermal_cpu", "NULL",
				apbc_base + APBC_THSENS + 0xc,
				10, 0, &clk_lock);
	clk_register_clkdev(clk, "THERMALCLK_CPU", NULL);
	clk = mmp_clk_register_apbc("thermal_gc", "NULL",
				apbc_base + APBC_THSENS + 0x10,
				10, 0, &clk_lock);
	clk_register_clkdev(clk, "THERMALCLK_GC", NULL);
}
#endif

#ifdef CONFIG_CORESIGHT_SUPPORT
#define CORESIGHT_ETB_MEMORY_CLK_EN	(1 << 25)
#define USBPHYCLK_GATING_CTRL           (3 << 26)
static void __init pxa1928_coresight_clk_init(void __iomem *apmu_base)
{
	struct clk *clk;
	u32 val;

	clk = mmp_clk_register_apmu("DBGCLK", "NULL", apmu_base + APMU_APDBG,
				(0x3 << 8 | (1 << 4) | (0x3 << 0)), &clk_lock);
	clk_register_clkdev(clk, "DBGCLK", NULL);

	/* TMC clock */
	clk = mmp_clk_register_apmu("TRACECLK", "DBGCLK", apmu_base +
		APMU_AP_DEBUG1, CORESIGHT_ETB_MEMORY_CLK_EN, &clk_lock);
	clk_register_clkdev(clk, "TRACECLK", NULL);

	/* Disable the coresight related clock by default */
	val = readl(apmu_base + APMU_APDBG);
	val |= (0x1 << 4);
	writel(val, apmu_base + APMU_APDBG);
	val &= ~(0x1f << 8 | 0x7 << 0);
	writel(val, apmu_base + APMU_APDBG);

	val = readl(apmu_base +	APMU_AP_DEBUG1) & (~CORESIGHT_ETB_MEMORY_CLK_EN);
	val = val & ~USBPHYCLK_GATING_CTRL;
	writel(val, apmu_base +	APMU_AP_DEBUG1);
}
#endif

#ifdef CONFIG_SMC91X
static void __init pxa1928_smc91x_clk_init(void __iomem *apmu_base)
{
	struct device_node *np = of_find_node_by_name(NULL, "smc91x");
	const char *str = NULL;
	if (np && !of_property_read_string(np, "clksrc", &str))
		if (!strcmp(str, "smc91x")) {
			/* Enable clock to SMC Controller */
			writel(0x5b, apmu_base + APMU_SMC);
			/* Configure SMC Controller */
			/* Set CS0 to A\D type memory */
			writel(0x52880008, apmu_base + 0x1090);
		}
}
#endif

#ifdef CONFIG_MMC_SDHCI_PXAV3
static void __init pxa1928_sdh_clk_init(void __iomem *apmu_base)
{
	struct clk *clk;

	/* Select pll1_624 as sdh clock source by default */
	clk = clk_register_mux(NULL, "sdh_mux", sdh_parent,
			ARRAY_SIZE(sdh_parent), CLK_SET_RATE_PARENT,
			apmu_base + APMU_SDH0, 8, 2, 0, &clk_lock);

	if (pxa1928_is_a0())
		clk_set_parent(clk, clk_get(NULL, "pll1_416"));
	else
		clk_set_parent(clk, clk_get(NULL, "pll1_624"));
	clk_register_clkdev(clk, "sdh_mux", NULL);

	clk = clk_register_divider(NULL, "sdh_div", "sdh_mux", 0,
			apmu_base + APMU_SDH0, 10, 4,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO, &clk_lock);
	/* Set sdh base clock as 104Mhz for A0 while 156Mhz for B0 */
	if (pxa1928_is_a0())
		clk_set_rate(clk, 104000000);
	else
		clk_set_rate(clk, 156000000);
	clk_register_clkdev(clk, "sdh_div", NULL);

	/* Register clock device for sdh1/2/3 which share the same base clock */
	clk = mmp_clk_register_apmu("sdh1", "sdh_div",
			apmu_base + APMU_SDH0, 0x1b, &clk_lock);
	clk_register_clkdev(clk, "sdh-baseclk", "sdhci-pxav3.0");
	/* Register fake clock for sdh1/2/3 which will be used by dvfs if needed */
	clk = mmp_clk_register_dvfs_dummy("sdh1-fclk-tuned", NULL,
			0, DUMMY_VL_TO_HZ(0));
	clk_register_clkdev(clk, "sdh-fclk-tuned", "sdhci-pxav3.0");
	clk_register_clkdev(clk, "SDH1", NULL);

	clk = mmp_clk_register_apmu("sdh2", "sdh_div",
			apmu_base + APMU_SDH1, 0x1b, &clk_lock);
	clk_register_clkdev(clk, "sdh-baseclk", "sdhci-pxav3.1");
	clk = mmp_clk_register_dvfs_dummy("sdh2-fclk-tuned", NULL,
			0, DUMMY_VL_TO_HZ(0));
	clk_register_clkdev(clk, "sdh-fclk-tuned", "sdhci-pxav3.1");
	clk_register_clkdev(clk, "SDH2", NULL);

	clk = mmp_clk_register_apmu("sdh3", "sdh_div",
			apmu_base + APMU_SDH2, 0x1b, &clk_lock);
	clk_register_clkdev(clk, "sdh-baseclk", "sdhci-pxav3.2");
	if (pxa1928_is_a0()) {
		clk = clk_register_fixed_factor(NULL, "sdh3-fclk-fixed", "sdh_div", 0, 1, 1);
		clk_register_clkdev(clk, "sdh-fclk-fixed", "sdhci-pxav3.2");
	} else {
		clk = mmp_clk_register_dvfs_dummy("sdh3-fclk-tuned", NULL,
				0, DUMMY_VL_TO_HZ(0));
		clk_register_clkdev(clk, "sdh-fclk-tuned", "sdhci-pxav3.2");
	}
	clk_register_clkdev(clk, "SDH3", NULL);
}
#endif

#ifdef CONFIG_SOUND
#define PMUA_AUDIO_PWRCTRL		(0x010c)
#define PMUA_SRAM_PWRCTRL		(0x0240)
#define PMUA_ISLD_AUDIO_CTRL		(0x01A8)

#define DSP_AUDIO_SRAM_SEC_CTRL_REG	0xC0FFD000
#define WR_SEC_ENN			(1<<6)
#define RD_SEC_ENN			(1<<5)

#define DSP_AUDIO_SSPA1_CLK_REG		0xC014000C
#define SSPA1_CLK_EN			(1<<3)
#define SSPA1_DIV_ACTIVE		(1<<2)
#define SSPA1_DIV_BYPASS		(1<<31)
/*
 * check with clk guys, no need to add refercence cnt even both
 * audio pll call this power on/off func.
 */
void pxa1928_audio_subsystem_poweron(void __iomem *apmu_base,
		struct clk *puclk, int pwr_on)
{
	unsigned int pmua_audio_pwrctrl, pmua_audio_clk_res_ctrl;
	unsigned int pmua_isld_audio_ctrl, pmua_sram_pwrctrl;
	void __iomem *reg_sspa1_clk, *reg_sec_ctrl;
	unsigned int val, val_restore;

	if (pwr_on) {
		/* Audio island power on */
		pmua_audio_pwrctrl = readl_relaxed(apmu_base +
			PMUA_AUDIO_PWRCTRL);
		if ((pmua_audio_pwrctrl & (0x3 << 9)) == 3) {
			pr_info("%s audio domain has poewed on\n", __func__);
			return;
		}

		if (puclk)
			/* power up audio current ref */
			clk_prepare_enable(puclk);

		pmua_sram_pwrctrl = readl_relaxed(apmu_base +
			PMUA_SRAM_PWRCTRL);
		pmua_sram_pwrctrl |= (1 << 2);
		writel_relaxed(pmua_sram_pwrctrl,
			apmu_base + PMUA_SRAM_PWRCTRL);
		pmua_sram_pwrctrl = readl_relaxed(apmu_base +
			PMUA_SRAM_PWRCTRL);
		pmua_sram_pwrctrl |= (1 << 0);
		writel_relaxed(pmua_sram_pwrctrl,
			apmu_base + PMUA_SRAM_PWRCTRL);
		pmua_audio_pwrctrl = readl_relaxed(apmu_base +
			PMUA_AUDIO_PWRCTRL);
		pmua_audio_pwrctrl |= (1 << 9);
		writel_relaxed(pmua_audio_pwrctrl,
			apmu_base + PMUA_AUDIO_PWRCTRL);
		udelay(10);
		pmua_sram_pwrctrl = readl_relaxed(apmu_base +
			PMUA_SRAM_PWRCTRL);
		pmua_sram_pwrctrl |= (1 << 3);
		writel_relaxed(pmua_sram_pwrctrl,
			apmu_base + PMUA_SRAM_PWRCTRL);
		pmua_sram_pwrctrl = readl_relaxed(apmu_base +
			PMUA_SRAM_PWRCTRL);
		pmua_sram_pwrctrl |= (1 << 1);
		writel_relaxed(pmua_sram_pwrctrl,
			apmu_base + PMUA_SRAM_PWRCTRL);
		pmua_audio_pwrctrl = readl_relaxed(apmu_base +
			PMUA_AUDIO_PWRCTRL);
		pmua_audio_pwrctrl |= (1 << 10);
		writel_relaxed(pmua_audio_pwrctrl,
			apmu_base + PMUA_AUDIO_PWRCTRL);
		udelay(10);

		/* disable sram FW */
		pmua_isld_audio_ctrl = readl_relaxed(apmu_base +
			PMUA_ISLD_AUDIO_CTRL);
		pmua_isld_audio_ctrl &= ~(3 << 1);
		writel_relaxed(pmua_isld_audio_ctrl, apmu_base +
			PMUA_ISLD_AUDIO_CTRL);
		pmua_isld_audio_ctrl = readl_relaxed(apmu_base +
			PMUA_ISLD_AUDIO_CTRL);
		pmua_isld_audio_ctrl &= ~(1 << 0);
		writel_relaxed(pmua_isld_audio_ctrl, apmu_base +
			PMUA_ISLD_AUDIO_CTRL);

		/* Enable Dummy clocks to srams*/
		pmua_isld_audio_ctrl = readl_relaxed(apmu_base +
			PMUA_ISLD_AUDIO_CTRL);
		pmua_isld_audio_ctrl |= (1 << 4);
		writel_relaxed(pmua_isld_audio_ctrl, apmu_base +
			PMUA_ISLD_AUDIO_CTRL);
		udelay(1);

		/* disbale dummy clock to srams*/
		pmua_isld_audio_ctrl = readl_relaxed(apmu_base +
			PMUA_ISLD_AUDIO_CTRL);
		pmua_isld_audio_ctrl &= ~(1 << 4);
		writel_relaxed(pmua_isld_audio_ctrl, apmu_base +
			PMUA_ISLD_AUDIO_CTRL);

		/*
		 * program audio sram & apb clocks
		 * to source from 26MHz vctcxo
		 */
		pmua_audio_clk_res_ctrl = readl_relaxed(apmu_base +
			PMUA_AUDIO_CLK_RES_CTRL);
		pmua_audio_clk_res_ctrl |= (1 << 3);
		writel_relaxed(pmua_audio_clk_res_ctrl, apmu_base +
			PMUA_AUDIO_CLK_RES_CTRL);
		pmua_audio_clk_res_ctrl = readl_relaxed(apmu_base +
			PMUA_AUDIO_CLK_RES_CTRL);
		pmua_audio_clk_res_ctrl |= (1 << 1);
		writel_relaxed(pmua_audio_clk_res_ctrl, apmu_base +
			PMUA_AUDIO_CLK_RES_CTRL);

		/* disable island fw*/
		pmua_audio_pwrctrl = readl_relaxed(apmu_base +
			PMUA_AUDIO_PWRCTRL);
		pmua_audio_pwrctrl |= (1 << 8);
		writel_relaxed(pmua_audio_pwrctrl,
			apmu_base + PMUA_AUDIO_PWRCTRL);

		/* start sram repairt */
		pmua_audio_pwrctrl = readl_relaxed(apmu_base +
			PMUA_AUDIO_PWRCTRL);
		pmua_audio_pwrctrl |= (1 << 2);
		writel_relaxed(pmua_audio_pwrctrl,
			apmu_base + PMUA_AUDIO_PWRCTRL);

		/* wait for sram repair completion*/
		pmua_audio_pwrctrl = readl_relaxed(apmu_base +
			PMUA_AUDIO_PWRCTRL);
		while (pmua_audio_pwrctrl & (1 << 2)) {
			pmua_audio_pwrctrl = readl_relaxed(apmu_base +
				PMUA_AUDIO_PWRCTRL);
		}

		/* release sram and apb reset*/
		pmua_audio_clk_res_ctrl = readl_relaxed(apmu_base +
			PMUA_AUDIO_CLK_RES_CTRL);
		pmua_audio_clk_res_ctrl |= (1 << 2);
		writel_relaxed(pmua_audio_clk_res_ctrl, apmu_base +
			PMUA_AUDIO_CLK_RES_CTRL);
		pmua_audio_clk_res_ctrl = readl_relaxed(apmu_base +
			PMUA_AUDIO_CLK_RES_CTRL);
		pmua_audio_clk_res_ctrl |= (1 << 0);
		writel_relaxed(pmua_audio_clk_res_ctrl, apmu_base +
			PMUA_AUDIO_CLK_RES_CTRL);

		/*
		 * Enable SSPA1 clock to access DSP_AUDIO_SRAM_SEC_CONTROL_REG
		 * because SSPA1 and DSP_AUDIO_SRAM_SEC_CONTROL_REG
		 * register implemented in same clock domain.
		 */
		reg_sspa1_clk = ioremap(DSP_AUDIO_SSPA1_CLK_REG, 4);
		val = readl(reg_sspa1_clk);
		val_restore = val;
		val |= SSPA1_DIV_BYPASS | SSPA1_CLK_EN | SSPA1_DIV_ACTIVE;
		writel(val, reg_sspa1_clk);

		/* Disable audio SRAM read/write security */
		reg_sec_ctrl = ioremap(DSP_AUDIO_SRAM_SEC_CTRL_REG, 4);
		val = RD_SEC_ENN | WR_SEC_ENN;
		writel(val, reg_sec_ctrl);

		/* Disable SSPA1 clock*/
		writel(val_restore, reg_sspa1_clk);

		iounmap(reg_sspa1_clk);
		iounmap(reg_sec_ctrl);

		udelay(1);
	} else {
		/* Audio island power down */
		pmua_audio_pwrctrl = readl_relaxed(apmu_base +
			PMUA_AUDIO_PWRCTRL);
		/* Check if audio already power off, if yes then return */
		if ((pmua_audio_pwrctrl & ((0x3 << 9) | (0x1 << 8))) == 0x0) {
			/* Audio is powered off, return */
			return;
		}

		/* Enable Dummy clocks to srams*/
		pmua_isld_audio_ctrl = readl_relaxed(apmu_base +
			PMUA_ISLD_AUDIO_CTRL);
		pmua_isld_audio_ctrl |= (1 << 4);
		writel_relaxed(pmua_isld_audio_ctrl, apmu_base +
			PMUA_ISLD_AUDIO_CTRL);
		udelay(1);

		/* disbale dummy clock to srams*/
		pmua_isld_audio_ctrl = readl_relaxed(apmu_base +
			PMUA_ISLD_AUDIO_CTRL);
		pmua_isld_audio_ctrl &= ~(1 << 4);
		writel_relaxed(pmua_isld_audio_ctrl, apmu_base +
			PMUA_ISLD_AUDIO_CTRL);

		/* enable sram fw */
		pmua_isld_audio_ctrl = readl_relaxed(apmu_base +
			PMUA_ISLD_AUDIO_CTRL);
		pmua_isld_audio_ctrl &= ~(3 << 1);
		writel_relaxed(pmua_isld_audio_ctrl, apmu_base +
			PMUA_ISLD_AUDIO_CTRL);

		pmua_isld_audio_ctrl = readl_relaxed(apmu_base +
			PMUA_ISLD_AUDIO_CTRL);
		pmua_isld_audio_ctrl |= 1 << 0;
		writel_relaxed(pmua_isld_audio_ctrl, apmu_base +
			PMUA_ISLD_AUDIO_CTRL);

		/* enable audio fw */
		pmua_audio_pwrctrl = readl_relaxed(apmu_base +
			PMUA_AUDIO_PWRCTRL);
		pmua_audio_pwrctrl &= ~(1 << 8);
		writel_relaxed(pmua_audio_pwrctrl,
			apmu_base + PMUA_AUDIO_PWRCTRL);

		/* assert audio sram and apb reset */
		pmua_audio_clk_res_ctrl = readl_relaxed(apmu_base +
			PMUA_AUDIO_CLK_RES_CTRL);
		pmua_audio_clk_res_ctrl &= ~(1 << 2);
		writel_relaxed(pmua_audio_clk_res_ctrl, apmu_base +
			PMUA_AUDIO_CLK_RES_CTRL);
		pmua_audio_clk_res_ctrl = readl_relaxed(apmu_base +
			PMUA_AUDIO_CLK_RES_CTRL);
		pmua_audio_clk_res_ctrl &= ~(1 << 0);
		writel_relaxed(pmua_audio_clk_res_ctrl, apmu_base +
			PMUA_AUDIO_CLK_RES_CTRL);

		/* power off the island */
		pmua_audio_pwrctrl = readl_relaxed(apmu_base +
			PMUA_AUDIO_PWRCTRL);
		pmua_audio_pwrctrl &= ~(3 << 9);
		writel_relaxed(pmua_audio_pwrctrl,
			apmu_base + PMUA_AUDIO_PWRCTRL);

		/* if needed, power off the audio buffer sram */
		pmua_sram_pwrctrl = readl_relaxed(apmu_base +
			PMUA_SRAM_PWRCTRL);
		pmua_sram_pwrctrl &= ~(3 << 2);
		writel_relaxed(pmua_sram_pwrctrl,
			apmu_base + PMUA_SRAM_PWRCTRL);
		/*
		 * Fixme: if needed, power off the audio map code sram
		 * but, we keep it here due to we don't want to
		 * load map firmware again */
		pmua_sram_pwrctrl = readl_relaxed(apmu_base +
			PMUA_SRAM_PWRCTRL);
		pmua_sram_pwrctrl &= ~(3 << 0);
		writel_relaxed(pmua_sram_pwrctrl,
			apmu_base + PMUA_SRAM_PWRCTRL);

		if (puclk)
			/* power off audio current ref */
			clk_disable_unprepare(puclk);
	}
}
#endif

#define TDM_OWN_USB "tdm_own_usb"
int get_usb_ownship_from_cmdline(void)
{
	if (strstr(saved_command_line, TDM_OWN_USB))
		return 1;
	else
		return 0;
}

static void peripheral_phy_clk_gate(void __iomem *mpmu_base,
		void __iomem *apmu_base, int common, int defeature1,
		int defeature2, int defeature3, int release)
{
	u32 reg_val;
	if (common) {
		/* 0xD4282858 PMUA_SDH2_CLK_RES_CTRL release*/
		if (release) {
			reg_val = readl_relaxed(apmu_base + 0x0058);
			reg_val |= (0x1 << 0 | 0x1 << 1);
			writel_relaxed(reg_val, apmu_base + 0x0058);
		}
		/* 0xD4282858 PMUA_SDH2_CLK_RES_CTRL clk disable*/
		reg_val = readl_relaxed(apmu_base + 0x0058);
		reg_val &= ~(0x1 << 4 | 0x1 << 3);
		reg_val |= (0x0 << 4 | 0x0 << 3);
		writel_relaxed(reg_val, apmu_base + 0x0058);


		/* 0xD42828EC PMUA_SDH4_CLK_RES_CTRL release */
		if (release) {
			reg_val = readl_relaxed(apmu_base + 0x00ec);
			reg_val |= (0x1 << 0 | 0x1 << 1);
			writel_relaxed(reg_val, apmu_base + 0x00ec);
		}
		/* 0xD42828EC PMUA_SDH4_CLK_RES_CTRL clk disable */
		reg_val = readl_relaxed(apmu_base + 0x00ec);
		reg_val &= ~(0x1 << 4 | 0x1 << 3);
		reg_val |= (0x0 << 4 | 0x0 << 3);
		writel_relaxed(reg_val, apmu_base + 0x00ec);

		/* 0xD42828F8 PMUA_HSIC1_CLK_RES_CTRL release */
		if (release) {
			reg_val = readl_relaxed(apmu_base + 0x00f8);
			reg_val |= (0x1 << 0);
			writel_relaxed(reg_val, apmu_base + 0x00f8);
		}

		/* 0xD42828F8 PMUA_HSIC1_CLK_RES_CTRL clk disable */
		reg_val = readl_relaxed(apmu_base + 0x00f8);
		reg_val &= ~(0x1 << 3);
		reg_val |= (0x0 << 3);
		writel_relaxed(reg_val, apmu_base + 0x00f8);

		/* 0xD428295C PMUA_SDH5_CLK_RES_CTRL */
		if (release) {
			reg_val = readl_relaxed(apmu_base + 0x015c);
			reg_val |= (0x1 << 0 | 0x1 << 1);
			writel_relaxed(reg_val, apmu_base + 0x015c);
		}
		/* 0xD428295C PMUA_SDH5_CLK_RES_CTRL */
		reg_val = readl_relaxed(apmu_base + 0x015c);
		reg_val &= ~(0x1 << 4 | 0x1 << 3);
		reg_val |= (0x0 << 4 | 0x0 << 3);
		writel_relaxed(reg_val, apmu_base + 0x015c);

		/* 0xD4282960 PMUA_SDH6_CLK_RES_CTRL */
		if (release) {
			reg_val = readl_relaxed(apmu_base + 0x0160);
			reg_val |= (0x1 << 0 | 0x1 << 1);
			writel_relaxed(reg_val, apmu_base + 0x0160);
		}
		/* 0xD4282960 PMUA_SDH6_CLK_RES_CTRL */
		reg_val = readl_relaxed(apmu_base + 0x0160);
		reg_val &= ~(0x1 << 4 | 0x1 << 3);
		reg_val |= (0x0 << 4 | 0x0 << 3);
		writel_relaxed(reg_val, apmu_base + 0x0160);

		/* 0xD42828D4 PMUA_SMC_CLK_RES_CTRL */
		if (release) {
			reg_val = readl_relaxed(apmu_base + 0x00d4);
			reg_val |= (0x1 << 0 | 0x1 << 1);
			writel_relaxed(reg_val, apmu_base + 0x00d4);
		}
		/* 0xD42828D4 PMUA_SMC_CLK_RES_CTRL */
		reg_val = readl_relaxed(apmu_base + 0x00d4);
		reg_val &= ~(0x1f << 16 | 0x1 << 4 | 0x1 << 3);
		reg_val |= (0xf << 16 | 0x0 << 4 | 0x0 << 3);
		writel_relaxed(reg_val, apmu_base + 0x00d4);

		/* 0xD4282834 PMUA_USB_CLK_GATE_CTRL */
		reg_val = readl_relaxed(apmu_base + 0x0034);
		reg_val &= ~(0x3 << 8 | 0x3 << 6 | 0x3 << 2 | 0x3 << 0);
		reg_val |= (0x0 << 8 | 0x0 << 6 | 0x0 << 2 | 0x0 << 0);
		writel_relaxed(reg_val, apmu_base + 0x0034);

		/* 0xD4282860 PMUA_NF_CLK_RES_CTRL */
		if (release) {
			reg_val = readl_relaxed(apmu_base + 0x0060);
			reg_val |= (0x1 << 0 | 0x1 << 2);
			writel_relaxed(reg_val, apmu_base + 0x0060);
		}
		/* 0xD4282860 PMUA_NF_CLK_RES_CTRL */
		reg_val = readl_relaxed(apmu_base + 0x0060);
		reg_val &= ~(0x1 << 5 | 0x1 << 3);
		reg_val |= (0x0 << 5 | 0x0 << 3);
		writel_relaxed(reg_val, apmu_base + 0x0060);
	}

	if (defeature1) {
		/* 0xD4282854 PMUA_SDH1_CLK_RES_CTRL */
		if (release) {
			reg_val = readl_relaxed(apmu_base + 0x0054);
			reg_val |= (0x1 << 0 | 0x1 << 1);
			writel_relaxed(reg_val, apmu_base + 0x0054);
		}
		/* 0xD4282854 PMUA_SDH1_CLK_RES_CTRL */
		reg_val = readl_relaxed(apmu_base + 0x0054);
		reg_val &= ~(0xf << 10 | 0x3 << 8 | 0x1 << 4 | 0x1 << 3);
		reg_val |= (0xf << 10 | 0x3 << 8 | 0x0 << 4 | 0x0 << 3);
		writel_relaxed(reg_val, apmu_base + 0x0054);
	}

	if (defeature2) {
		if (!get_usb_ownship_from_cmdline()) {
			/* 0xD428285C PMUA_USB_CLK_RES_CTRL */
			if (release) {
				reg_val = readl_relaxed(apmu_base + 0x005c);
				reg_val |= (0x1 << 0);
				writel_relaxed(reg_val, apmu_base + 0x005c);
			}

			/* 0xD428285C PMUA_USB_CLK_RES_CTRL */
			reg_val = readl_relaxed(apmu_base + 0x005c);
			reg_val &= ~(0x1 << 3);
			reg_val |= (0x0 << 3);
			writel_relaxed(reg_val, apmu_base + 0x005c);
		}

		/* 0xD428286C PMUA_BUS_CLK_RES_CTRL */
		reg_val = readl_relaxed(apmu_base + 0x006c);
		reg_val &= ~(0x7 << 6);
		reg_val |= (0x4 << 6);
		writel_relaxed(reg_val, apmu_base + 0x006c);
	}

	if (defeature3) {
		/* 0xD42828E8 PMUA_SDH3_CLK_RES_CTRL */
		if (release) {
			reg_val = readl_relaxed(apmu_base + 0x00e8);
			reg_val |= (0x1 << 0 | 0x1 << 1);
			writel_relaxed(reg_val, apmu_base + 0x00e8);
		}

		/* 0xD42828E8 PMUA_SDH3_CLK_RES_CTRL */
		reg_val = readl_relaxed(apmu_base + 0x00e8);
		reg_val &= ~(0x1 << 4 | 0x1 << 3);
		reg_val |= (0x0 << 4 | 0x0 << 3);
		writel_relaxed(reg_val, apmu_base + 0x00e8);
	}
}
static void __iomem *mpmu_base;
static void __iomem *apmu_base;
static void __iomem *apbc_base;
static void __iomem *ciu_base;

void __init pxa1928_clk_init(unsigned long apb_phy_base,
		unsigned long axi_phy_base, unsigned long aud_phy_base,
		unsigned long aud_phy_base2)
{
	struct clk *clk;
	struct clk *vctcxo;
	struct clk *uart_pll;
	void __iomem *audio_base;
	void __iomem *audio_aux_base;
	void __iomem *ddrdfc_base;

	mpmu_base = ioremap(apb_phy_base + 0x50000, SZ_8K);
	if (mpmu_base == NULL) {
		pr_err("error to ioremap MPMU base\n");
		return;
	}

	apmu_base = ioremap(axi_phy_base + 0x82800, SZ_4K);
	if (apmu_base == NULL) {
		pr_err("error to ioremap APMU base\n");
		return;
	}

	ddrdfc_base = ioremap(axi_phy_base + 0x82600, SZ_512);
	if (ddrdfc_base == NULL) {
		pr_err("error to ioremap ddfdfc base\n");
		return;
	}

	apbc_base = ioremap(apb_phy_base + 0x15000, SZ_4K);
	if (apbc_base == NULL) {
		pr_err("error to ioremap APBC base\n");
		return;
	}

	ciu_base = ioremap(axi_phy_base + 0x82c00, SZ_256);
	if (ciu_base == NULL) {
		pr_err("error to ioremap APMU base\n");
		return;
	}

	pxa1928_is_zx = soc_is_zx();
	stepping_is_a0 = pxa1928_is_a0();
	pp_is_1926 = pp_is_pxa1926();
	if (!pxa1928_is_zx) {
		audio_base = ioremap(aud_phy_base, SZ_2K);
		if (audio_base == NULL) {
			pr_err("error to ioremap audio base\n");
			return;
		}

		audio_aux_base = ioremap(aud_phy_base2, SZ_2K);
		if (audio_aux_base == NULL) {
			pr_err("error to ioremap audio aux base\n");
			return;
		}
	} else {
		audio_base = ioremap(aud_phy_base + 0x400, SZ_2K);
		if (audio_base == NULL) {
			pr_err("error to ioremap audio base\n");
			return;
		}

		audio_aux_base = ioremap(aud_phy_base2, SZ_256);
		if (audio_aux_base == NULL) {
			pr_err("error to ioremap audio aux base\n");
			return;
		}
	}
	peripheral_phy_clk_gate(mpmu_base, apmu_base, 1, 1, 1, 1, 1);
	clk = clk_register_fixed_rate(NULL, "clk32", NULL, CLK_IS_ROOT, 32768);
	clk_register_clkdev(clk, "clk32", NULL);

	vctcxo = clk_register_fixed_rate(NULL, "vctcxo", NULL, CLK_IS_ROOT,
				26000000);
	clk_register_clkdev(vctcxo, "vctcxo", NULL);

	clk = clk_register_gate(NULL, "vcxo_out", "vctcxo", 0,
				mpmu_base + MPMU_VRCR, 8, 0, NULL);
	clk_register_clkdev(clk, "vcxo_out", NULL);

	clk = clk_register_fixed_rate(NULL, "pll1_416", NULL,
				CLK_IS_ROOT, get_pll1_freq("pll1_416_freq"));
	clk_register_clkdev(clk, "pll1_416", NULL);

	clk = clk_register_fixed_rate(NULL, "pll1_624", NULL,
				CLK_IS_ROOT, get_pll1_freq("pll1_624_freq"));
	clk_register_clkdev(clk, "pll1_624", NULL);

	clk = clk_register_fixed_rate(NULL, "usb_pll", NULL, CLK_IS_ROOT,
				480000000);
	clk_register_clkdev(clk, "usb_pll", NULL);

	pxa1928_pll_init(mpmu_base, apmu_base);

	clk = clk_register_fixed_factor(NULL, "vctcxo_d2", "vctcxo", 0, 1, 2);
	clk_register_clkdev(clk, "vctcxo_d2", NULL);

	clk = clk_register_fixed_factor(NULL, "vctcxo_d4", "vctcxo", 0, 1, 4);
	clk_register_clkdev(clk, "vctcxo_d4", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_d2", "pll1_624", 0, 1, 2);
	clk_register_clkdev(clk, "pll1_d2", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_d9", "pll1_624", 0, 1, 9);
	clk_register_clkdev(clk, "pll1_d9", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_d12", "pll1_624", 0, 1, 12);
	clk_register_clkdev(clk, "pll1_d12", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_d16", "pll1_624", 0, 1, 16);
	clk_register_clkdev(clk, "pll1_d16", NULL);

	clk = clk_register_fixed_factor(NULL, "pll1_d20", "pll1_624", 0, 1, 20);
	clk_register_clkdev(clk, "pll1_d20", NULL);

	uart_pll = mmp_clk_register_factor("uart_pll", "pll1_416", 0,
				mpmu_base + MPMU_UART_PLL,
				&uart_factor_masks, uart_factor_tbl,
				ARRAY_SIZE(uart_factor_tbl));
	clk_set_rate(uart_pll, 58593749);
	clk_register_clkdev(uart_pll, "uart_pll", NULL);

	clk = mmp_clk_register_apbc("twsi0", "vctcxo",
				apbc_base + APBC_TWSI0, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-i2c.0");

	clk = mmp_clk_register_apbc("twsi1", "vctcxo",
				apbc_base + APBC_TWSI1, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-i2c.1");

	clk = mmp_clk_register_apbc("twsi2", "vctcxo",
				apbc_base + APBC_TWSI2, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-i2c.2");

	clk = mmp_clk_register_apbc("twsi3", "vctcxo",
				apbc_base + APBC_TWSI3, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-i2c.3");

	clk = mmp_clk_register_apbc("twsi4", "vctcxo",
				apbc_base + APBC_TWSI4, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-i2c.4");

	clk = mmp_clk_register_apbc("twsi5", "vctcxo",
				apbc_base + APBC_TWSI5, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-i2c.5");

	clk = mmp_clk_register_apbc("gpio", "vctcxo",
				apbc_base + APBC_GPIO, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa1928-gpio");

	clk = mmp_clk_register_apbc("kpc", "clk32",
				apbc_base + APBC_KPC, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa27x-keypad");

	clk = mmp_clk_register_apbc("rtc", "clk32",
				apbc_base + APBC_RTC, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "sa1100-rtc");

	clk = mmp_clk_register_apbc("pwm0", "vctcxo",
				apbc_base + APBC_PWM0, 10, APBC_PWM, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-pwm.0");

	clk = mmp_clk_register_apbc("pwm1", "vctcxo",
				apbc_base + APBC_PWM1, 10, APBC_PWM, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-pwm.1");

	clk = mmp_clk_register_apbc("pwm2", "vctcxo",
				apbc_base + APBC_PWM2, 10, APBC_PWM, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-pwm.2");

	clk = mmp_clk_register_apbc("pwm3", "vctcxo",
				apbc_base + APBC_PWM3, 10, APBC_PWM, &clk_lock);
	clk_register_clkdev(clk, NULL, "mmp-pwm.3");

	clk = clk_register_mux(NULL, "uart0_mux", uart_parent,
				ARRAY_SIZE(uart_parent), CLK_SET_RATE_PARENT,
				apbc_base + APBC_UART0, 4, 3, 0, &clk_lock);
	clk_set_parent(clk, get_parent("uart0_clksrc", uart_pll));
	clk_register_clkdev(clk, "uart_mux.0", NULL);

	clk = mmp_clk_register_apbc("uart0", "uart0_mux",
				apbc_base + APBC_UART0, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-uart.0");

	clk = clk_register_mux(NULL, "uart1_mux", uart_parent,
				ARRAY_SIZE(uart_parent), CLK_SET_RATE_PARENT,
				apbc_base + APBC_UART1, 4, 3, 0, &clk_lock);
	clk_set_parent(clk, vctcxo);
	clk_register_clkdev(clk, "uart_mux.1", NULL);

	clk = mmp_clk_register_apbc("uart1", "uart1_mux",
				apbc_base + APBC_UART1, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-uart.1");

	clk = clk_register_mux(NULL, "uart2_mux", uart_parent,
				ARRAY_SIZE(uart_parent), CLK_SET_RATE_PARENT,
				apbc_base + APBC_UART2, 4, 3, 0, &clk_lock);
	clk_set_parent(clk, vctcxo);
	clk_register_clkdev(clk, "uart_mux.2", NULL);

	clk = mmp_clk_register_apbc("uart2", "uart2_mux",
				apbc_base + APBC_UART2, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-uart.2");

	clk = clk_register_mux(NULL, "uart3_mux", uart_parent,
				ARRAY_SIZE(uart_parent), CLK_SET_RATE_PARENT,
				apbc_base + APBC_UART3, 4, 3, 0, &clk_lock);
	clk_set_parent(clk, get_parent("uart3_clksrc", uart_pll));
	clk_register_clkdev(clk, "uart_mux.3", NULL);

	clk = mmp_clk_register_apbc("uart3", "uart3_mux",
				apbc_base + APBC_UART3, 10, 0, &clk_lock);
	clk_register_clkdev(clk, NULL, "pxa2xx-uart.3");

	clk = clk_register_mux(NULL, "ssp1_mux", ssp_parent,
				ARRAY_SIZE(ssp_parent), CLK_SET_RATE_PARENT,
				apbc_base + APBC_SSP1, 4, 3, 0, &ssp1_lock);
	clk_set_parent(clk, clk_get(NULL, "pll1_d12"));
	clk_register_clkdev(clk, "ssp1_mux", NULL);

	clk = mmp_clk_register_apbc("ssp1", "ssp1_mux",
				apbc_base + APBC_SSP1, 10, 0, &ssp1_lock);
	clk_register_clkdev(clk, NULL, "pxa910-ssp.1");

	clk = mmp_clk_register_apmu("usb", "usb_pll", apmu_base + APMU_USB,
				0x9, &clk_lock);
	clk_register_clkdev(clk, "usb_clk", NULL);
	clk_register_clkdev(clk, NULL, "mv-otg");
	clk_register_clkdev(clk, NULL, "mv-udc");
	clk_register_clkdev(clk, NULL, "pxa-u2oehci");
	clk_register_clkdev(clk, NULL, "pxa1928-usb-phy");

	clk = mmp_clk_register_apmu("hsic", "usb_pll", apmu_base + APMU_HSIC,
				0x9, &clk_lock);
	clk_register_clkdev(clk, "hsic_clk", NULL);
	clk_register_clkdev(clk, NULL, "pxa-u2hhsic");
	clk_register_clkdev(clk, NULL, "pxa1928-hsic-phy");

	pxa1928_axi11_clk_init(apmu_base);
#ifdef CONFIG_CORESIGHT_SUPPORT
	pxa1928_coresight_clk_init(apmu_base);
#endif

#ifdef CONFIG_THERMAL
	pxa1928_thermal_clk_init(apbc_base);
#endif

	if (pp_is_1926)
		pxa1926_pll5_init();
#ifdef	CONFIG_UIO_HANTRO
	pxa1928_vpu_clk_init(apmu_base);
#endif
	pxa1928_gc_clk_init(apmu_base);
	pxa1928_disp_clk_init(apmu_base);
#ifdef CONFIG_VIDEO_MMP
	pxa1928_ccic_clk_init(apmu_base);
#endif
#ifdef CONFIG_VIDEO_MV_SC2_CCIC
	pxa1928_sc2_clk_init(apmu_base, mpmu_base);
#endif
#ifdef CONFIG_SOUND
	/* sram_mux: choose apll1_mux or vctcxo */
	clk = clk_register_mux(NULL, "sram_mux", (const char **)pmu_sram_parent,
			       ARRAY_SIZE(pmu_sram_parent), 0,
			       apmu_base + PMUA_AUDIO_CLK_RES_CTRL, 1, 1, 0,
			       &clk_lock);
	clk_register_clkdev(clk, "sram_mux", NULL);

	/* map_apb_mux: choose apll1_mux or vctcxo */
	clk =
	    clk_register_mux(NULL, "map_apb_mux", (const char **)pmu_apb_parent,
			     ARRAY_SIZE(pmu_apb_parent), 0,
			     apmu_base + PMUA_AUDIO_CLK_RES_CTRL, 3, 1, 0,
			     &clk_lock);
	clk_register_clkdev(clk, "map_apb_mux", NULL);

	clk = clk_register_gate(NULL, "32kpu", NULL, 0,
			mpmu_base + ANAGRP_CTRL1, CURR_REF_PU_IDX, 0, NULL);
	clk_register_clkdev(clk, "32kpu", NULL);

	pxa1928_audio_clk_init(apmu_base, audio_base, audio_aux_base,
			       pxa1928_audio_subsystem_poweron);
#endif

#ifdef CONFIG_SMC91X
	pxa1928_smc91x_clk_init(apmu_base);
#endif

#ifdef CONFIG_MMC_SDHCI_PXAV3
	pxa1928_sdh_clk_init(apmu_base);
#endif
	pxa1928_core_clk_init(apmu_base);
	pxa1928_axi_clk_init(apmu_base);
	pxa1928_ddr_clk_init(apmu_base, ddrdfc_base, ciu_base);

	clk = mmp_clk_register_apbc("ipc_cp", NULL,
				apbc_base + APBC_IPC_CP_CLK_RST, 10,
				0, NULL);
	clk_register_clkdev(clk, "ipc_clk", NULL);
	clk_prepare_enable(clk);

	clk = mmp_clk_register_apbc("ripc", NULL,
				apbc_base + APBC_RIPC_CLK_RST, 10,
				0, NULL);
	clk_register_clkdev(clk, "ripc_clk", NULL);
	clk_prepare_enable(clk);
#ifdef CONFIG_DEBUG_FS
	__init_dcstat_debugfs_node();
#endif
}

#ifdef CONFIG_DEBUG_FS
static struct dentry *stat;
CLK_DCSTAT_OPS(clks[cpu], cpu)
CLK_DCSTAT_OPS(clks[ddr], ddr)
CLK_DCSTAT_OPS(clks[axi], axi)
CLK_DCSTAT_OPS(clks[gc3d_1x], gc3d_1x);
CLK_DCSTAT_OPS(clks[gc3d_2x], gc3d_2x);
CLK_DCSTAT_OPS(clks[gc2d], gc2d);
CLK_DCSTAT_OPS(clks[vpu_dec], vpu_dec);
CLK_DCSTAT_OPS(clks[vpu_enc], vpu_enc);

static ssize_t pxa1928_clk_stats_read(struct file *filp,
	char __user *buffer, size_t count, loff_t *ppos)
{
	char *buf;
	int len = 0, ret = 0, i, pll_max = 4, ver_flag = 0;
	u32 reg, size = PAGE_SIZE - 1;
	u32 pll_cr, pll_ctrl1, pll_ctrl2, pll_ctrl3, pll_ctrl4;
	u32 is_enabled, rate, post_div, ppost_div;
	u32 posr, posr2, posr3, ap_pll_ctrl;
	u32 pll_cr_reg_shift[] = {
		MPMU_PLL2CR, MPMU_PLL3CR, MPMU_PLL4CR,
		MPMU_PLL5CR, MPMU_PLL6CR, MPMU_PLL7CR
	};
	u32 pll_ctrl1_reg_shift[] = {
		MPMU_PLL2_CTRL1, MPMU_PLL3_CTRL1, MPMU_PLL4_CTRL1,
		MPMU_PLL5_CTRL1, MPMU_PLL6_CTRL1, MPMU_PLL7_CTRL1
	};
	u32 pll_ctrl2_reg_shift[] = {
		MPMU_PLL2_CTRL2, MPMU_PLL3_CTRL2, MPMU_PLL4_CTRL2,
		MPMU_PLL5_CTRL2, MPMU_PLL6_CTRL2, MPMU_PLL7_CTRL2
	};
	u32 pll_ctrl3_reg_shift[] = {
		MPMU_PLL2_CTRL3, MPMU_PLL3_CTRL3, MPMU_PLL4_CTRL3,
		MPMU_PLL5_CTRL3, MPMU_PLL6_CTRL3, MPMU_PLL7_CTRL3
	};
	u32 pll_ctrl4_reg_shift[] = {
		MPMU_PLL2_CTRL4, MPMU_PLL3_CTRL4, MPMU_PLL4_CTRL4,
		MPMU_PLL5_CTRL4, MPMU_PLL6_CTRL4, MPMU_PLL7_CTRL4
	};
	char *apb_devices[] = {
		"TWSI1", "TWSI2", "TWSI3", "TWSI4", "TWSI5", "TWSI6",
		"PWM1", "PWM2", "PWM3", "PWM4",
		"UART1", "UART2", "UART3", "UART4",
		"SSP1", "SSP2", "SSP3",
		"THSENS", "THSENS0", "THSENS1", "THSENS2",
		"RTC", "KPC", "GPIO", "RIPC", "IPC"
	};
	u32 apb_shift[] = {
		APBC_TWSI0, APBC_TWSI1, APBC_TWSI2,
		APBC_TWSI3, APBC_TWSI4, APBC_TWSI5,
		APBC_PWM0, APBC_PWM1, APBC_PWM2, APBC_PWM3,
		APBC_UART0, APBC_UART1, APBC_UART2, APBC_UART3,
		APBC_SSP1, APBC_SSP2, APBC_SSP3,
		APBC_THSENS, APBC_THSENS + 8,
		APBC_THSENS + 0xc, APBC_THSENS + 0x10,
		APBC_RTC, APBC_KPC, APBC_GPIO,
		APBC_RIPC_CLK_RST, APBC_IPC_CP_CLK_RST
	};
	char *apmu_devices[] = {
		"SDH1", "SDH2", "SDH3", "SDH4", "SDH5", "USB",
	};
	u32 apmu_shift[] = {
		APMU_SDH0, APMU_SDH1, APMU_SDH2, APMU_SDH3, APMU_SDH4, APMU_USB
	};
	char *media_devices[] = {
		"CCIC1", "CCIC2", "DISP1", "DISP2", "GC", "GC2", "VPU"
	};
	u32 media_shift[] = {
		APMU_CCIC0, APMU_CCIC1, APMU_DISP0, APMU_DISP1, APMU_GC,
		APMU_GC2, APMU_VPU
	};
	char *media_devices_ax[] = {
		"ISLD_ISP_CTRL", "ISP_RSTCTRL", "ISP_CLKCTRL",
		"ISP_CLKCTRL2", "ISP_PWR_CTRL", "ISLD_ISP_PWRCTRL",
		"DISP_RSTCTRL", "DISP_CLKCTRL", "DISP_CLKCTRL2",
		"ISLD_LCD_CTRL", "ISLD_LCD_PWRCTRL",
		"GC3D_RSTCTRL", "GC3D_CLKCTRL",
		"ISLD_GC3D_CTRL", "ISLD_GC3D_PWRCTRL",
		"GC2D_RSTCTRL", "GC2D_CLKCTRL",
		"ISLD_GC2D_CTRL", "ISLD_GC2D_PWRCTRL",
		"ISLD_VPU_CTRL", "VPU_RSTCTRL",
		"VPU_CLKCTRL", "ISLD_VPU_PWRCTRL"
	};
	u32 media_shift_ax[] = {
		APMU_ISLD_ISP_CTRL, APMU_ISP_RSTCTRL, APMU_ISP_CLKCTRL,
		APMU_ISP_CLKCTRL2, APMU_ISP_PWR_CTRL, APMU_ISLD_ISP_PWRCTRL,
		APMU_DISP_RST_CTRL, APMU_DISP_CLK_CTRL, APMU_DISP_CLK_CTRL2,
		APMU_ISLD_LCD_CTRL, APMU_DISP_PWR_CTRL,
		APMU_GC3D_RSTCTRL, APMU_GC3D_CLKCTRL,
		APMU_ISLD_GC_CTRL, APMU_ISLD_GC3D_PWRCTRL,
		APMU_GC2D_RSTCTRL, APMU_GC2D_CLKCTRL,
		APMU_ISLD_GC2D_CTRL, APMU_ISLD_GC2D_PWRCTRL,
		APMU_ISLD_VPU_CTRL, APMU_VPU_RSTCTRL,
		APMU_VPU_CLKCTRL, APMU_ISLD_VPU_PWRCTRL
	};

	char *ciu_devices_ax[] = {
		"LCD_CKGT_CTRL", "FABRIC1_CKGT_CTRL0", "ISP_CKGT_CTRL",
		"GC3D_CKGT_CTRL", "VPU_CKGT_CTRL"
	};
	u32 ciu_shift_ax[] = {
		CIU_LCD_CKGT_CTRL, CIU_FABRIC1_CKGT_CTRL0, CIU_ISP_CKGT_CTRL,
		CIU_GC3D_CKGT_CTRL, CIU_VPU_CKGT_CTRL
	};

	buf = (char *)__get_free_pages(GFP_NOIO, 0);
	if (!buf)
		return -ENOMEM;

	if (!pxa1928_is_zx) {
		ver_flag = 1;
		pll_max = 6;
	}

	/* PLL registers status */
	posr = readl_relaxed(mpmu_base + MPMU_POSR);
	posr2 = readl_relaxed(mpmu_base + MPMU_POSR2);
	posr3 = readl_relaxed(mpmu_base + MPMU_POSR3);
	ap_pll_ctrl = readl_relaxed(apmu_base + APMU_AP_PLL_CTRL);
	len += snprintf(buf + len, size,
			"[PLL SOURCE]\n\tPOSR:0x%x\tPOSR2:0x%x\tPOSR3:0x%x"
			"\tAP_PLL_CTRL:0x%x\n",
			posr, posr2, posr3, ap_pll_ctrl);

	for (i = 0; i < pll_max; i++) {
		pll_cr = readl_relaxed(mpmu_base + pll_cr_reg_shift[i]);
		pll_ctrl1 = readl_relaxed(mpmu_base + pll_ctrl1_reg_shift[i]);
		pll_ctrl2 = readl_relaxed(mpmu_base + pll_ctrl2_reg_shift[i]);
		pll_ctrl3 = readl_relaxed(mpmu_base + pll_ctrl3_reg_shift[i]);
		pll_ctrl4 = readl_relaxed(mpmu_base + pll_ctrl4_reg_shift[i]);

		post_div = 1 << ((pll_ctrl1 >> POSTDIV_SHIFT) & 0x7);
		ppost_div = 1 << ((pll_ctrl4 >> PPOSTDIV_SHIFT) & 0x7);
		if (pll_ctrl1 & CTRL1_PLL_BIT) {
			if (i == 2)
				/* for PLL4 post div from mck */
				post_div = 1 << ((readl_relaxed(apmu_base +
					APMU_MC_CLK_RES_CTRL) >> 12) & 3);
		} else if (i == 0 || i == 3)
			/* for PLL2 and PLL5 select postdiv as 3 */
			ppost_div = 3;

		is_enabled =
			(((pll_cr >> SW_EN_SHIFT) & 3) == SW_DISABLE) ? 0 : 1;
		rate = (26 << 2) * ((pll_cr >> FBDIV_SHIFT) & 0x1ff);
		rate /= (pll_cr >> REFDIV_SHIFT) & 0x1ff;
		len += snprintf(buf + len, size,
			"\tPLL%d status:%d\tPLL%dP status:%d\t"
			"VCO@%dMHz\tOUT@%dMHz\tOUTP@%dMHz\n", i + 2,
			is_enabled, i + 2,
			is_enabled && (pll_ctrl4 & (1 << P_ENABLE_SHIFT)),
			rate, rate / post_div, rate / ppost_div);
		len += snprintf(buf + len, size,
			"\tPLL_CR:0x%x\tPLL_CTRL1:0x%x\tPLL_CTRL2:0x%x"
			"\tPLL_CTRL3:0x%x\tPLL_CTRL4:0x%x\n", pll_cr,
			pll_ctrl1, pll_ctrl2, pll_ctrl3, pll_ctrl4);
	}

	len += snprintf(buf + len, size, "[APB DEVICES]\n");
	for (i = 0; i < ARRAY_SIZE(apb_devices); i++) {
		reg = readl_relaxed(apbc_base + apb_shift[i]);
		if (apb_shift[i] >= APBC_PWM0 && apb_shift[i] <= APBC_PWM3)
			len += snprintf(buf + len, size,
				"\tAPBC_%s:0x%x\tstatus:%d\n", apb_devices[i],
				reg, !!(reg & 2));
		else
			len += snprintf(buf + len, size,
				"\tAPBC_%s:0x%x\tstatus:%d\n", apb_devices[i],
				reg, reg & 1);
	}

	len += snprintf(buf + len, size, "[APMU DEVICES]\n");
	for (i = 0; i < ARRAY_SIZE(apmu_devices); i++) {
		reg = readl_relaxed(apmu_base + apmu_shift[i]);
		len += snprintf(buf + len, size,
			"\tAPMU_%s:0x%x\tstatus:%d\n", apmu_devices[i],
			reg, !!(reg & 8));
	}

	if (ver_flag) {
		for (i = 0; i < ARRAY_SIZE(media_devices_ax); i++)
			len += snprintf(buf + len, size,
				"\tAPMU_%s:0x%x\n", media_devices_ax[i],
				readl_relaxed(apmu_base + media_shift_ax[i]));
		for (i = 0; i < ARRAY_SIZE(ciu_devices_ax); i++)
			len += snprintf(buf + len, size,
				"\tCIU_%s:0x%x\n", ciu_devices_ax[i],
				readl_relaxed(ciu_base + ciu_shift_ax[i]));
	} else {
		for (i = 0; i < ARRAY_SIZE(media_devices); i++)
			len += snprintf(buf + len, size,
				"\tAPMU_%s:0x%x\n", media_devices[i],
				readl_relaxed(apmu_base + media_shift[i]));
	}

	ret = simple_read_from_buffer(buffer, count, ppos, buf, len);
	free_pages((unsigned long)buf, 0);

	return ret;
}

static const struct file_operations pxa1928_clk_stats_ops = {
	.owner = THIS_MODULE,
	.read = pxa1928_clk_stats_read,
};

static int __init __init_dcstat_debugfs_node(void)
{
	struct dentry *cpu_dc_stat = NULL, *ddr_dc_stat = NULL;
	struct dentry *axi_dc_stat = NULL;
	struct dentry *gc2d_dc_stat = NULL;
	struct dentry *vpu_dec_dc_stat = NULL, *vpu_enc_dc_stat = NULL;
	struct dentry *gc3d_1x_dc_stat = NULL, *gc3d_2x_dc_stat = NULL;
	struct dentry *clock_status = NULL;

	stat = debugfs_create_dir("stat", pxa);
	if (!stat)
		return -ENOENT;

	cpu_dc_stat = cpu_dcstat_file_create("cpu_dc_stat", stat);
	if (!cpu_dc_stat)
		goto err_cpu_dc_stat;

	clks[ddr] = clk_get_sys("devfreq-ddr", NULL);
	ddr_dc_stat = clk_dcstat_file_create("ddr_dc_stat", stat,
			&ddr_dc_ops);
	if (!ddr_dc_stat)
		goto err_ddr_dc_stat;

	axi_dc_stat = clk_dcstat_file_create("axi_dc_stat", stat,
			&axi_dc_ops);
	if (!axi_dc_stat)
		goto err_axi_dc_stat;

	gc3d_1x_dc_stat = clk_dcstat_file_create("gc3d_core0_dc_stat",
		stat, &gc3d_1x_dc_ops);
	if (!gc3d_1x_dc_stat)
		goto err_gc3d_1x_dc_stat;

	gc3d_2x_dc_stat = clk_dcstat_file_create("gcsh_core0_dc_stat",
		stat, &gc3d_2x_dc_ops);
	if (!gc3d_2x_dc_stat)
		goto err_gc3d_2x_dc_stat;

	gc2d_dc_stat = clk_dcstat_file_create("gc2d_core0_dc_stat",
		stat, &gc2d_dc_ops);
	if (!gc2d_dc_stat)
		goto err_gc2d_dc_stat;

	vpu_dec_dc_stat = clk_dcstat_file_create("vpu_dec_dc_stat",
		stat, &vpu_dec_dc_ops);
	if (!vpu_dec_dc_stat)
		goto err_vpu_dec_dc_stat;

	vpu_enc_dc_stat = clk_dcstat_file_create("vpu_enc_dc_stat",
		stat, &vpu_enc_dc_ops);
	if (!vpu_enc_dc_stat)
		goto err_vpu_enc_dc_stat;

	clock_status = debugfs_create_file("clock_status", 0444,
					   pxa, NULL, &pxa1928_clk_stats_ops);
	if (!clock_status)
		goto err_clk_stats;

	return 0;

err_clk_stats:
	debugfs_remove(vpu_enc_dc_stat);
err_vpu_enc_dc_stat:
	debugfs_remove(vpu_dec_dc_stat);
err_vpu_dec_dc_stat:
	debugfs_remove(gc2d_dc_stat);
err_gc2d_dc_stat:
	debugfs_remove(gc3d_2x_dc_stat);
err_gc3d_2x_dc_stat:
	debugfs_remove(gc3d_1x_dc_stat);
err_gc3d_1x_dc_stat:
	debugfs_remove(axi_dc_stat);
err_axi_dc_stat:
	debugfs_remove(ddr_dc_stat);
err_ddr_dc_stat:
	debugfs_remove(cpu_dc_stat);
err_cpu_dc_stat:
	debugfs_remove(stat);
	return -ENOENT;
}
#endif
