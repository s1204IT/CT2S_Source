/*
 * This software program is licensed subject to the GNU General Public License
 * (GPL).Version 2,June 1991, available at http://www.fsf.org/copyleft/gpl.html
 *
 *	Yifan Zhang <zhangyf@marvell.com>
 *	Xiangzhan Meng <mengxzh@marvell.com>
 *
 * (C) Copyright 2013 Marvell International Ltd.
 * All Rights Reserved
 */
#include <linux/module.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/cpumask.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/cputype.h>

#define VCXO 26000
#define ON "on"
#define OFF "off"
#define SLOW "slow_ramp"
#define UNKNOWN "unknown"

#define DFC_BASE	0xd4282600
#define DFC_RANGE	0x200
#define APMU_BASE	0xd4282600
#define APMU_RANGE	0x600
#define MPMU_BASE	0xd4050000
#define MPMU_RANGE	0x1410
#define MCU_BASE	0xd0000000
#define MCU_RANGE	0x510
#define ICU_BASE	0xd4284000
#define ICU_RANGE	0x300
#define AUD_BASE	0x510
#define AUD_RANGE	0x9c

static unsigned long apmu_virt_base;
static unsigned long mpmu_virt_base;
static unsigned long mcu_virt_base;
static unsigned long icu_virt_base;
static unsigned long aud_virt_base;
static unsigned long dfc_virt_base;

#define APMU_REG(x)		 (void __iomem *)(apmu_virt_base + (x))
#define MPMU_REG(x)		 (void __iomem *)(mpmu_virt_base + (x))
#define MCU_REG(x)		 (void __iomem *)(mcu_virt_base + (x))
#define ICU_REG(x)		 (void __iomem *)(icu_virt_base + (x))
#define DFC_REG(x)		 (void __iomem *)(dfc_virt_base + (x))
#define DSP_AUDIO_REG(x)	 (void __iomem *)(aud_virt_base + (x))

#define DDRDFC_CTRL		DFC_REG(0x0)
#define DDRDFC_FP_LWR	DFC_REG(0x10)
#define DDRDFC_FP_UPR	DFC_REG(0x14)

#define MPMU_PLL2CR		MPMU_REG(0x0034)
#define MPMU_PLL2_CTRL1		MPMU_REG(0x0414)
#define MPMU_PLL2_CTRL2		MPMU_REG(0x0418)
#define MPMU_PLL2_CTRL3		MPMU_REG(0x041c)
#define MPMU_PLL2_DIFF_CTRL	MPMU_REG(0x0068)

#define MPMU_PLL3CR		MPMU_REG(0x0050)
#define MPMU_PLL3_CTRL1		MPMU_REG(0x0058)
#define MPMU_PLL3_CTRL2		MPMU_REG(0x0060)
#define MPMU_PLL3_CTRL3		MPMU_REG(0x0064)
#define MPMU_PLL3_DIFF_CTRL	MPMU_REG(0x006c)

#define MPMU_PLL4CR		MPMU_REG(0x1100)
#define MPMU_PLL4_CTRL1		MPMU_REG(0x1104)
#define MPMU_PLL4_CTRL2		MPMU_REG(0x1108)
#define MPMU_PLL4_CTRL3		MPMU_REG(0x110c)
#define MPMU_PLL4_DIFF_CTRL	MPMU_REG(0x1110)

#define MPMU_PLL5CR		MPMU_REG(0x1114)
#define MPMU_PLL5_CTRL1		MPMU_REG(0x1118)
#define MPMU_PLL5_CTRL2		MPMU_REG(0x111c)
#define MPMU_PLL5_CTRL3		MPMU_REG(0x1120)
#define MPMU_PLL5_DIFF_CTRL	MPMU_REG(0x1124)

#ifdef CONFIG_ARM64
#define MPMU_PLL6CR		MPMU_REG(0x1300)
#define MPMU_PLL6_CTRL1		MPMU_REG(0x1304)
#define MPMU_PLL6_CTRL2		MPMU_REG(0x1308)
#define MPMU_PLL6_CTRL3		MPMU_REG(0x130c)
#define MPMU_PLL6_DIFF_CTRL	MPMU_REG(0x1310)

#define MPMU_PLL7CR		MPMU_REG(0x1400)
#define MPMU_PLL7_CTRL1		MPMU_REG(0x1404)
#define MPMU_PLL7_CTRL2		MPMU_REG(0x1408)
#define MPMU_PLL7_CTRL3		MPMU_REG(0x140c)
#define MPMU_PLL7_DIFF_CTRL	MPMU_REG(0x1410)
#endif

/* PXA1928 AP PLL control registers */
#define APMU_AP_PLL_CTRL		APMU_REG(0x0348)
/*
 * pll1_gating_ctrl: 21:20, id = 1
 * pll2_gating_ctrl: 23:22, id = 2
 * pll3_gating_ctrl: 25:24, id = 3
 * pll4_gating_ctrl: 27:26, id = 4
 * pll5_gating_ctrl: 29:28, id = 5
 *
 * pll6 & pll7 for Ax
 * pll6_gating_ctrl: 31:30, id = 6
 * pll7_gating_ctrl: 17:16, id = 7
 */
#define PLL_GATING_CTRL_SHIFT(id) ((id < 7) ? ((id << 1) + 18) : 16)

#define APMU_CORE0_PWRMODE	APMU_REG(0x0280)
#define APMU_CORE1_PWRMODE	APMU_REG(0x0284)
#define APMU_CORE2_PWRMODE	APMU_REG(0x0288)
#define APMU_CORE3_PWRMODE	APMU_REG(0x028C)

#define APMU_MC_CLK_STAT	APMU_REG(0x264)
#ifndef CONFIG_ARM64
#define APMU_DM_CC_PJ		APMU_REG(0x00c)
#endif
#define APMU_DM2_CC		APMU_REG(0x154)
#define APMU_PLL_SEL_STATUS	APMU_REG(0x00c4)
#define APMU_COREAPSS_CLKCTRL	APMU_REG(0x2e0)
#define APMU_CORESP_PWRMODE	APMU_REG(0x2d0)

#ifdef CONFIG_ARM64
#define APMU_ISLD_VPU_PWRCTRL	APMU_REG(0x0208)
#define APMU_ISLD_GC3D_PWRCTRL	APMU_REG(0x020c)
#define APMU_ISLD_GC2D_PWRCTRL	APMU_REG(0x0210)

#define APMU_VPU_CLK_CTRL	APMU_REG(0x01f4)
#define APMU_GC3D_CLK_CTRL	APMU_REG(0x0174)
#define APMU_GC2D_CLK_CTRL	APMU_REG(0x017c)
#define APMU_ACLK11_CLK_CTRL	APMU_REG(0x0120)

#define APMU_ISP_PWR		APMU_REG(0x1FC)
#define APMU_ISP_CLK		APMU_REG(0X1E4)
#define APMU_ISP_CLK2		APMU_REG(0X1E8)

#define APMU_AUDIO_PWR_CTRL		APMU_REG(0x10c)

#else

#define APMU_VPU_CLK_RES_CTRL	APMU_REG(0x00a4)
#define APMU_GC_CLK_RES_CTRL	APMU_REG(0x00cc)
#define APMU_GC_CLK_RES_CTRL2	APMU_REG(0x027c)

#define APMU_ISPPWR		APMU_REG(0x1FC)
#define APMU_ISPCLK		APMU_REG(0x224)
#define APMU_CCIC_RST		APMU_REG(0x050)
#define APMU_CCIC2_RST		APMU_REG(0x0f4)

#define APMU_AUDIO_CLK_RES_CTRL		APMU_REG(0x10c)
#define DSP_AUDIO_PLL1_CONFIG_REG1      DSP_AUDIO_REG(0x68)
#define DSP_AUDIO_PLL1_CONFIG_REG2      DSP_AUDIO_REG(0x6c)
#define DSP_AUDIO_PLL1_CONFIG_REG3      DSP_AUDIO_REG(0x70)
#endif

#define APMU_DVC_APCORESS		APMU_REG(0x350)
#define APMU_DVC_APSS			APMU_REG(0x354)
#define APMU_DVC_STAT			APMU_REG(0x36c)



#define PMUM_READ_AND_PRINT(start, end)	\
	do {				\
		for (i = start; i <= end; i += 4) \
			s += sprintf(buf + s, "MPMU[0x%x]: 0x%x\n", i,\
					readl(MPMU_REG(i)));\
	} while (0)

#define PMUA_READ_AND_PRINT(start, end)	\
	do {				\
		for (i = start; i <= end; i += 4) \
			s += sprintf(buf + s, "APMU[0x%x]: 0x%x\n", i,\
					readl(APMU_REG(i)));\
	} while (0)

#define MCU_READ_AND_PRINT(start, end)	\
	do {				\
		for (i = start; i <= end; i += 4) \
			s += sprintf(buf + s, "MCU[0x%x]: 0x%x\n", i,\
					readl(DMCU_REG(i)));\
	} while (0)

#define ICU_READ_AND_PRINT(start, end)	\
	do {				\
		for (i = start; i <= end; i += 4) \
			s += sprintf(buf + s, "ICU[0x%x]: 0x%x\n", i,\
					readl(ICU_REG(i)));\
	} while (0)

#define on_off_judge(flag, char_p) \
	do {					\
		tmp = flag;			\
		if (!tmp)			\
			*char_p = OFF;		\
		else				\
			*char_p = ON;		\
	} while (0)

#define on_off_slow_judge(flag, char_p) \
	do {					\
		tmp = flag;			\
		switch (tmp) {			\
		case 0:				\
			*char_p = OFF;		\
			break;			\
		case 1:				\
			*char_p = SLOW;		\
			break;			\
		case 3:				\
			*char_p = ON;		\
			break;			\
		default:			\
			*char_p = UNKNOWN;	\
		}				\
	} while (0)


#define cal_pll_vco(_name_) \
	do {							\
		if (__pll_vco_is_enabled(_name_))		\
			pll##_name_##_vco = 4 *			\
			mpmu_pll##_name_##cr.b.fbdiv *		\
			VCXO / mpmu_pll##_name_##cr.b.refdiv;	\
		else						\
			pll##_name_##_vco = 0;			\
	} while (0)

#define PXA1928_SYSSET_OPS(name)					\
static ssize_t name##_show(struct file *filp,			\
	char __user *buffer, size_t count, loff_t *ppos)		\
{									\
	char *p;							\
	int len = 0;							\
	size_t ret, size = PAGE_SIZE * 2 - 1;				\
	p = (char *)__get_free_pages(GFP_NOIO, 0);			\
	if (!p)								\
		return -ENOMEM;						\
	len = name##_read(p, size);			\
	if (len < 0) {						\
		free_pages((unsigned long)p, 0);\
		return -EINVAL;					\
	}							\
	if (len == size)						\
		pr_warn("%s The dump buf is not large enough!\n",	\
			 __func__);					\
	ret = simple_read_from_buffer(buffer, count, ppos, p, len);	\
	free_pages((unsigned long)p, 0);				\
	return ret;							\
}									\
static const struct file_operations name##_ops = {			\
	.owner = THIS_MODULE,						\
	.read = name##_show,						\
};


enum pll_index {
	PLL1_312 = 0,
	PLL1_416,
	PLL1_624,
	PLL1_1248,
	PLL2_CLKOUT,
	PLL2_CLKOUTP,
	PLL3_CLKOUT,
	PLL3_CLKOUTP,
	PLL4_CLKOUT,
	PLL4_CLKOUTP,
	PLL5_CLKOUT,
	PLL5_CLKOUTP,
#ifdef CONFIG_ARM64
	PLL6_CLKOUT,
	PLL6_CLKOUTP,
	PLL7_CLKOUT,
	PLL7_CLKOUTP,
#endif
	PLL_NUM,
};

enum reg_type {
	MPMU = 0,
	APMU,
	MCU,
	ICU,
	REG_TYPE_NUM,
};

/* PLL1 is fixed */
static u32 pll_array[PLL_NUM] = {312000, 416000, 624000, 1248000};
static u32 pll2_vco, pll3_vco, pll4_vco, pll5_vco;
#ifdef CONFIG_ARM64
static u32 pll6_vco, pll7_vco;
#endif

static union mpmu_cr_pll {
	struct {
		unsigned int reserved1:8;
		unsigned int sw_en:1;
		unsigned int ctrl:1;
		unsigned int fbdiv:9;
		unsigned int refdiv:9;
		unsigned int reserved0:4;
	} b;
	unsigned int v;
#ifdef CONFIG_ARM64
} mpmu_pll2cr, mpmu_pll3cr, mpmu_pll4cr, mpmu_pll5cr, mpmu_pll6cr, mpmu_pll7cr;
#else
} mpmu_pll2cr, mpmu_pll3cr, mpmu_pll4cr, mpmu_pll5cr;
#endif

static union mpmu_ctrl1_pll {
	struct {
		unsigned int vddl:3;
		unsigned int bw_sel:1;
		unsigned int vddm:2;
		unsigned int reserved1:9;
		unsigned int ctune:2;
		unsigned int reserved0:1;
		unsigned int kvco:4;
		unsigned int icp:4;
		unsigned int clkout_se_div_sel:3;
		unsigned int pll_rst:1;
		unsigned int ctrl1_pll_bit_30:1;
		unsigned int bypass_en:1;
	} b;
	unsigned int v;
#ifdef CONFIG_ARM64
} mpmu_pll2_ctrl1, mpmu_pll3_ctrl1, mpmu_pll4_ctrl1, mpmu_pll5_ctrl1,
	mpmu_pll6_ctrl1, mpmu_pll7_ctrl1;
#else
} mpmu_pll2_ctrl1, mpmu_pll3_ctrl1, mpmu_pll4_ctrl1, mpmu_pll5_ctrl1;
#endif

static union mpmu_ctrl4_pll {
	struct {
		unsigned int reserved3:5;
		unsigned int clkout_diff_div_sel:3;
		unsigned int reserved2:1;
		unsigned int clkout_diff_en:1;
		unsigned int reserved1:6;
		unsigned int fd:3;
		unsigned int intpr:3;
		unsigned int pi_loop_mode:1;
		unsigned int avdd1815_sel:1;
		unsigned int reserved0:8;
	} b;
	unsigned int v;
#ifdef CONFIG_ARM64
} mpmu_pll2_ctrl4, mpmu_pll3_ctrl4, mpmu_pll4_ctrl4, mpmu_pll5_ctrl4,
	mpmu_pll6_ctrl4, mpmu_pll7_ctrl4;
#else
} mpmu_pll2_ctrl4, mpmu_pll3_ctrl4, mpmu_pll4_ctrl4, mpmu_pll5_ctrl4;
#endif

static union apmu_ddrdfc_ctrl {
	struct {
		unsigned int dfc_hw_en:1;
		unsigned int ddr_type:1;
		unsigned int ddrpll_ctrl:1;
		unsigned int dfc_freq_tbl_en:1;
		unsigned int reserved0:4;
		unsigned int dfc_fp_idx:5;
		unsigned int dfc_pllpark_idx:5;
		unsigned int reserved1:2;
		unsigned int dfc_fp_idx_cur:5;
		unsigned int reserved2:3;
		unsigned int dfc_in_progress:1;
		unsigned int dfc_int_clr:1;
		unsigned int dfc_int_msk:1;
		unsigned int dfc_int:1;
	} b;
	unsigned int v;
} apmu_ddrdfc_ctrl;

union ddrdfc_fp_lwr {
	struct {
		unsigned int ddr_vl:4;
		unsigned int mc_reg_tbl_sel:5;
		unsigned int rtcwtc_sel:1;
		unsigned int sel_4xmode:1;
		unsigned int sel_ddrpll:1;
		unsigned int reserved:20;
	} b;
	unsigned int v;
} ddrdfc_fp_lwr;

union ddrdfc_fp_upr {
	struct {
		unsigned int fp_upr0:3;
		unsigned int fp_upr1:5;
		unsigned int fp_upr2:7;
		unsigned int fp_upr3:3;
		unsigned int fp_upr4:1;
		unsigned int fp_upr5:3;
		unsigned int reserved:10;
	} b;
	unsigned int v;
} ddrdfc_fp_upr;

static union apmu_mc_clk_stat {
	struct {
		unsigned int reserved2:1;
		unsigned int pmu_ddr_clk_sel:2;
		unsigned int pmu_ddr_clk_div:3;
		unsigned int reserved1:6;
		unsigned int pll4_vcodiv_sel_se:3;
		unsigned int reserved0:15;
		unsigned int mc_clk_src_sel:1;
		unsigned int mc_4x_mode:1;
	} b;
	unsigned int v;
} apmu_mc_clk_stat;

#ifndef CONFIG_ARM64
static union apmu_dm_cc_pj {
	struct {
		unsigned int reserved2:15;
		unsigned int soc_axi_clk_div:3;
		unsigned int reserved1:6;
		unsigned int sp_hw_semaphore:1;
		unsigned int pj_hw_semaphore:1;
		unsigned int reserved0:6;
	} b;
	unsigned int v;
} apmu_dm_cc_pj;
#endif

static union apmu_dm2_cc {
	struct {
		unsigned int soc_axi_clk2_div:3;
		unsigned int soc_axi_clk3_div:3;
		unsigned int soc_axi_clk4_div:3;
		unsigned int reserved0:23;
	} b;
	unsigned int v;
} apmu_dm2_cc;

static union apmu_pll_sel_status {
	struct {
		unsigned int sp_pll_sel:3;
		unsigned int reserved1:6;
		unsigned int soc_axi_pll_sel:3;
		unsigned int reserved0:20;
	} b;
	unsigned int v;
} apmu_pll_sel_status;

static union apmu_coreapss_clkctrl {
	struct {
		unsigned int coreap_clk_div:3;
		unsigned int reserved5:5;
		unsigned int coreap_axim_clk_div:3;
		unsigned int reserved4:1;
		unsigned int coreap_periphclk_div:3;
		unsigned int reserved3:1;
		unsigned int coreap_atclk_div:3;
		unsigned int reserved2:1;
		unsigned int coreap_apbclk_div:5;
		unsigned int reserved1:2;
		unsigned int coreap_rtcwtc_sel:1;
#ifdef CONFIG_ARM64
		unsigned int reserved0:1;
		unsigned int coreap_clksrc_sel:3;
#else
		unsigned int reserved0:2;
		unsigned int coreap_clksrc_sel:2;
#endif
	} b;
	unsigned int v;
} apmu_coreapss_clkctrl;

static union apmu_core_pwrmode {
	struct {
		unsigned int cpu_pwrmode:2;
		unsigned int reserved:2;
		unsigned int l2sr_pwroff_vt:1;
		unsigned int reserved1:3;
		unsigned int coress_pwrmode_vt:2;
		unsigned int reserved2:2;
		unsigned int msk_cpu_irq:1;
		unsigned int msk_cpu_fiq:1;
		unsigned int reserved3:10;
		unsigned int wfi_status:1;
		unsigned int lpm_status:1;
		unsigned int pwr_status:1;
		unsigned int reserved4:3;
		unsigned int clr_c2_status:1;
		unsigned int c2_status:1;
	} b;
	unsigned int v;
} apmu_core_pwrmode[4];

#ifdef CONFIG_ARM64
static union apmu_isld_pwrctrl {
	struct {
		unsigned int hwmode_en:1;
		unsigned int pwrup:1;
		unsigned int reserved0:2;
		unsigned int pwr_status:1;
		unsigned int redun_status:1;
		unsigned int int_clr:1;
		unsigned int int_msk:1;
		unsigned int int_status:1;
		unsigned int reserved1:23;
	} b;
	unsigned int v;
} apmu_isld_vpu_pwrctrl, apmu_isld_gc3d_pwrctrl, apmu_isld_gc2d_pwrctrl;

static union {
	struct {
		unsigned int axi_clk_div:3;
		unsigned int reserved0:1;
		unsigned int axi_clksrc_sel:3;
		unsigned int axi_clk_en:1;
		unsigned int dec_clk_div:3;
		unsigned int reserved2:1;
		unsigned int dec_clksrc_sel:3;
		unsigned int dec_clk_en:1;
		unsigned int enc_clk_div:3;
		unsigned int reserved4:1;
		unsigned int enc_clksrc_sel:3;
		unsigned int enc_clk_en:1;
		unsigned int reserverd5:7;
		unsigned int update_rtcwrc:1;
	} b;
	unsigned int v;
} apmu_vpu_clk_ctrl;

static union {
	struct {
		unsigned int axi_clk_div:3;
		unsigned int reserved0:1;
		unsigned int axi_clksrc_sel:3;
		unsigned int axi_clk_en:1;
		unsigned int clk1x_div:3;
		unsigned int reserved2:1;
		unsigned int clk1x_clksrc_sel:3;
		unsigned int clk1x_en:1;
		unsigned int clksh_div:3;
		unsigned int reserved4:1;
		unsigned int clksh_clksrc_sel:3;
		unsigned int clksh_en:1;
		unsigned int hclk_en:1;
		unsigned int reserved6:6;
		unsigned int update_rtcwtc;
	} b;
	unsigned int v;
} apmu_gc3d_clk_ctrl;

static union {
	struct {
		unsigned int axi_clk_en:1;
		unsigned int reserved0:7;
		unsigned int clk1x_div:3;
		unsigned int reserved1:1;
		unsigned int clk1x_clksrc_sel:3;
		unsigned int clk1x_en:1;
		unsigned int reserved2:8;
		unsigned int hclk_en:1;
		unsigned int reserved3:6;
		unsigned int update_rtcwtc:1;
	} b;
	unsigned int v;
} apmu_gc2d_clk_ctrl;

static union {
	struct {
		unsigned int axi_clk_div:3;
		unsigned int reserved0:1;
		unsigned int axi_clksrc_sel:3;
		unsigned int reserved1:2;
		unsigned int fc_en:1;
		unsigned int reserved2:22;
	} b;
	unsigned int v;
} apmu_aclk11_clk_ctrl;

static union {
	struct {
		unsigned int clk4x_div:3;
		unsigned int reserved0:1;
		unsigned int clk4x_clksrc_sel:3;
		unsigned int reserved1:1;
		unsigned int aclk_div:3;
		unsigned int reserved2:1;
		unsigned int aclk_clksrc_sel:3;
		unsigned int reserved3:1;
		unsigned int p_sclk_div:3;
		unsigned int reserved4:1;
		unsigned int p_sclk_clksrc_sel:3;
		unsigned int reserved5:1;
		unsigned int wb_clk_div:4;
		unsigned int wb_clk_clksrc_sel:3;
		unsigned int update_rtcwtc:1;
	} b;
	unsigned int v;
} apmu_isp_clk_ctrl;

static union {
	struct {
		unsigned int csi_clk_div:3;
		unsigned int reserved0:1;
		unsigned int csi_clk_clksrc_sel:3;
		unsigned int reserved1:1;
		unsigned int csi_txclk_esc_4ln_en:1;
		unsigned int csi_txclk_esc_2ln_en:1;
		unsigned int reserved2:22;
	} b;
	unsigned int v;
} apmu_isp_clk2_ctrl;

static union {
	struct {
		unsigned int mp0_vl:4;
		unsigned int reserved0:4;
		unsigned int mp1_vl:4;
		unsigned int mp2l2on_vl:4;
		unsigned int mp2l2off_vl:4;
		unsigned int reserved1:12;
	} b;
	unsigned int v;
} apmu_dvc_apcoress;

static union {
	struct {
		unsigned int d0_vl:4;
		unsigned int reserved0:4;
		unsigned int d0cg_vl:4;
		unsigned int reserved1:4;
		unsigned int d1_vl:4;
		unsigned int d2_vl:4;
		unsigned int reserved3:8;
	} b;
	unsigned int v;
} apmu_dvc_apss;

static union {
	struct {
		unsigned int vl_cur:4;
		unsigned int vl_pntr:4;
		unsigned int vl_cp:4;
		unsigned int reserved:20;
	} b;
	unsigned int v;
} apmu_dvc_stat;

static union {
	struct {
		unsigned int unused2:9;
		unsigned int audio_pwrup:2;
		unsigned int unused0:21;
	} b;
	unsigned int v;
} apmu_audio_pwr_ctrl;

#else

static union {
	struct {
		unsigned int unused0:3;
		unsigned int vpu_axi_clk_en:1;
		unsigned int vpu_encoder_clk_en:1;
		unsigned int reserved0:1;
		unsigned int vpu_encoder_clk_sel:2;
		unsigned int vpu_isb:1;
		unsigned int vpu_pwrup:2;
		unsigned int reserved1:1;
		unsigned int vpu_axi_clk_sel:2;
		unsigned int reserved2:2;
		unsigned int vpu_encoder_clk_div:3;
		unsigned int vpu_axi_clk_div:3;
		unsigned int vpu_decoder_clk_sel:2;
		unsigned int vpu_decoder_clk_div:3;
		unsigned int vpu_decoder_clk_en:1;
		unsigned int unused3:4;
	} b;
	unsigned int v;
} apmu_vpu_clk_res_ctrl;

static union {
	struct {
		unsigned int unused0:2;
		unsigned int gc2200_axiclk_en:1;
		unsigned int gc2200_clk1x_clk2x_en:1;
		unsigned int gc2200_aclk_sel:2;
		unsigned int gc2200_clk1x_src_sel:2;
		unsigned int gc_isb:1;
		unsigned int gc_pwrup:2;
		unsigned int unused1:1;
		unsigned int gc2200_clk2x_src_sel:2;
		unsigned int unused2:9;
		unsigned int gc2200_axi_clk_div:3;
		unsigned int gc2200_clk_1x_div:3;
		unsigned int gc2200_clk_2x_div:3;
	} b;
	unsigned int v;
} apmu_gc_clk_res_ctrl;

static union {
	struct {
		unsigned int gc320_axi_clk_sel:2;
		unsigned int gc320_axi_clk_div:3;
		unsigned int unused0:10;
		unsigned int gc320_clk_en:1;
		unsigned int gc320_clk_src_sel:2;
		unsigned int unused1:1;
		unsigned int gc320_axiclk_en:1;
		unsigned int gc320_clk_div:3;
		unsigned int unused2:9;
	} b;
	unsigned int v;
} apmu_gc_clk_res_ctrl2;

static union {
	struct {
		unsigned int unused0:4;
		unsigned int isp_clk_enable:1;
		unsigned int unused1:27;
	} b;
	unsigned int v;
} apmu_isp_clk_res_ctrl;

static union {
	struct {
		unsigned int mp0_vl:3;
		unsigned int reserved0:5;
		unsigned int mp1_vl:3;
		unsigned int reserved1:1;
		unsigned int mp2l2on_vl:3;
		unsigned int reserved2:1;
		unsigned int mp2l2off_vl:3;
		unsigned int reserved3:13;
	} b;
	unsigned int v;
} apmu_dvc_apcoress;

static union {
	struct {
		unsigned int d0_vl:3;
		unsigned int reserved0:5;
		unsigned int d0cg_vl:3;
		unsigned int reserved1:5;
		unsigned int d1_vl:3;
		unsigned int reserved2:1;
		unsigned int d2_vl:3;
		unsigned int reserved3:9;
	} b;
	unsigned int v;
} apmu_dvc_apss;

static union {
	struct {
		unsigned int unused2:3;
		unsigned int ccic_axiclk_en:1;
		unsigned int ccic_clk_en:1;
		unsigned int ccic_phyclk_en:1;
		unsigned int unused1:9;
		unsigned int ccic_axi_arb_clk_en:1;
		unsigned int unused0:16;
	} b;
	unsigned int v;
} apmu_ccic_clk_res_ctrl;

static union {
	struct {
		unsigned int unused0:3;
		unsigned int ccic2_axiclk_en:1;
		unsigned int ccic2_clk_en:1;
		unsigned int ccic2_phyclk_en:1;
		unsigned int unused1:3;
		unsigned int ccic2_physlowclk_en:1;
		unsigned int unused2:22;
	} b;
	unsigned int v;
} apmu_ccic2_clk_res_ctrl;

static union {
	struct {
		unsigned int unused2:4;
		unsigned int audio_clk_en:1;
		unsigned int unused1:4;
		unsigned int audio_pwrup:2;
		unsigned int unused0:21;
	} b;
	unsigned int v;
} apmu_audio_clk_res_ctrl;

static union {
	struct {
		unsigned int unused0:9;
		unsigned int refdiv:9;
		unsigned int fbdiv:9;
		unsigned int unused1:5;
	} b;
	unsigned int v;
} dsp_audio_pll1_config_reg1;

static union {
	struct {
		unsigned int unused0:9;
		unsigned int refdiv:9;
		unsigned int fbdiv:9;
		unsigned int unused1:5;
	} b;
	unsigned int v;
} dsp_audio_pll2_config_reg1;


static union {
	struct {
		unsigned int unused1:20;
		unsigned int postdiv:7;
		unsigned int unused0:5;
	} b;
	unsigned int v;
} dsp_audio_pll1_config_reg2;

static union {
	struct {
		unsigned int unused1:20;
		unsigned int postdiv:7;
		unsigned int unused0:5;
	} b;
	unsigned int v;
} dsp_audio_pll2_config_reg2;
#endif

static union {
	struct {
		unsigned int reserved0:8;
		unsigned int isp_isob:1;
		unsigned int isp_pwrup:2;
		unsigned int reserved1:21;
	} b;
	unsigned int v;
} apmu_isp_pwr_ctrl;

static union {
	struct {
		unsigned int unused0:25;
		unsigned int lpm_status:1;
		unsigned int unused1:6;
	} b;
	unsigned int v;
} apmu_coresp_pwrmode;


static inline int __pll_vco_is_enabled(int id)
{
	union mpmu_cr_pll pllx_cr;
	union mpmu_ctrl1_pll pllx_ctrl1;
	u32 val = __raw_readl(APMU_AP_PLL_CTRL);

	switch (id) {
	case 2:
		pllx_cr.v = mpmu_pll2cr.v;
		pllx_ctrl1.v = mpmu_pll2_ctrl1.v;
		break;
	case 3:
		pllx_cr.v = mpmu_pll3cr.v;
		pllx_ctrl1.v = mpmu_pll3_ctrl1.v;
		break;
	case 4:
		pllx_cr.v = mpmu_pll4cr.v;
		pllx_ctrl1.v = mpmu_pll4_ctrl1.v;
		break;
	case 5:
		pllx_cr.v = mpmu_pll5cr.v;
		pllx_ctrl1.v = mpmu_pll5_ctrl1.v;
		break;
#ifdef CONFIG_ARM64
	case 6:
		pllx_cr.v = mpmu_pll6cr.v;
		pllx_ctrl1.v = mpmu_pll6_ctrl1.v;
		break;
	case 7:
		pllx_cr.v = mpmu_pll7cr.v;
		pllx_ctrl1.v = mpmu_pll7_ctrl1.v;
		break;
#endif
	}
	if (pllx_ctrl1.b.pll_rst && pllx_cr.b.sw_en &&
			((val >> PLL_GATING_CTRL_SHIFT(id)) & 0x3) != 0x3)
		return 1;

	return 0;
}

/* no PLL3, PLL4 status refdiv, fbdiv
 * no clkout status div
 * use R/W instead
 */
static void cal_pll(void)
{
	/* 26M refdiv is fixed to 3 */
	if (mpmu_pll2cr.b.refdiv != 3) {
		pr_err("mpmu_pll2cr.b.refdiv error:0x%x", mpmu_pll2cr.b.refdiv);
		return;
	}

	cal_pll_vco(2);
	cal_pll_vco(3);
	cal_pll_vco(4);
	cal_pll_vco(5);
#ifdef CONFIG_ARM64
	cal_pll_vco(6);
	cal_pll_vco(7);
#endif

	/* PLL2 */
	pll_array[PLL2_CLKOUT] = pll2_vco >>
		mpmu_pll2_ctrl1.b.clkout_se_div_sel;
	if (mpmu_pll2_ctrl1.b.ctrl1_pll_bit_30)
		pll_array[PLL2_CLKOUTP] = mpmu_pll2_ctrl4.b.clkout_diff_en ?
			pll2_vco >> mpmu_pll2_ctrl4.b.clkout_diff_div_sel : 0;
	else
		pll_array[PLL2_CLKOUTP] = mpmu_pll2_ctrl4.b.clkout_diff_en ?
			pll2_vco / 3 : 0;

	/* PLL3 */
	pll_array[PLL3_CLKOUT] = pll3_vco >>
			mpmu_pll3_ctrl1.b.clkout_se_div_sel;
	pll_array[PLL3_CLKOUTP] = mpmu_pll3_ctrl4.b.clkout_diff_en ?
		pll3_vco >> mpmu_pll3_ctrl4.b.clkout_diff_div_sel : 0;

	/* PLL4 */
	if (mpmu_pll4_ctrl1.b.ctrl1_pll_bit_30)
		pll_array[PLL4_CLKOUT] = pll4_vco >>
				apmu_mc_clk_stat.b.pll4_vcodiv_sel_se;
	else
		pll_array[PLL4_CLKOUT] = pll4_vco >>
				mpmu_pll4_ctrl1.b.clkout_se_div_sel;
	pll_array[PLL4_CLKOUTP] = mpmu_pll4_ctrl4.b.clkout_diff_en ?
		pll4_vco >> mpmu_pll4_ctrl4.b.clkout_diff_div_sel : 0;

	/* PLL5 */
	pll_array[PLL5_CLKOUT] = pll5_vco >>
			mpmu_pll5_ctrl1.b.clkout_se_div_sel;
	if (mpmu_pll5_ctrl1.b.ctrl1_pll_bit_30)
		pll_array[PLL5_CLKOUTP] = mpmu_pll5_ctrl4.b.clkout_diff_en ?
			pll5_vco >> mpmu_pll5_ctrl4.b.clkout_diff_div_sel : 0;
	else
		pll_array[PLL5_CLKOUTP] = mpmu_pll5_ctrl4.b.clkout_diff_en ?
			pll5_vco / 3 : 0;

#ifdef CONFIG_ARM64
	/* PLL6 */
	pll_array[PLL6_CLKOUT] = pll6_vco >>
			mpmu_pll6_ctrl1.b.clkout_se_div_sel;
	pll_array[PLL6_CLKOUTP] = mpmu_pll6_ctrl4.b.clkout_diff_en ?
		pll6_vco >> mpmu_pll6_ctrl4.b.clkout_diff_div_sel : 0;

	/* PLL7 */
	pll_array[PLL7_CLKOUT] = pll7_vco >>
			mpmu_pll7_ctrl1.b.clkout_se_div_sel;
	pll_array[PLL7_CLKOUTP] = mpmu_pll7_ctrl4.b.clkout_diff_en ?
		pll7_vco >> mpmu_pll7_ctrl4.b.clkout_diff_div_sel : 0;
#endif
}

static u32 axi_src_mux(u32 src)
{
	switch (src) {
	case 0:
		return pll_array[PLL1_312];
	case 1:
		return pll_array[PLL1_624];
	case 2:
		return pll_array[PLL2_CLKOUT];
	case 3:
		return pll_array[PLL1_416];
	case 4:
		return VCXO;
	default:
		pr_err("apmu_bus_clk_res_ctrl.b.soc_axi_clk_pll_sel error !\n");
		return 0;
	}
}

static u32 gc_1x2x_mux(u32 src)
{
	switch (src) {
	case 0:
		return pll_array[PLL1_624];
	case 1:
		return pll_array[PLL2_CLKOUT];
	case 2:
		return pll_array[PLL5_CLKOUT];
#ifdef CONFIG_ARM64
	case 6:
		return pll_array[PLL5_CLKOUTP];
#else
	case 3:
		return pll_array[PLL2_CLKOUTP];
#endif
	default:
		pr_err("gc 1x2x clk sel error !\n");
		return 0;
	}

}

static u32 gc320_mux(u32 src)
{
	switch (src) {
	case 0:
		return pll_array[PLL1_624];
	case 1:
		return pll_array[PLL2_CLKOUT];
#ifdef CONFIG_ARM64
	case 2:
		return pll_array[PLL1_416];
	case 6:
		return pll_array[PLL5_CLKOUTP];
#else
	case 2:
		return pll_array[PLL5_CLKOUT];
	case 3:
		return pll_array[PLL5_CLKOUTP];
#endif
	default:
		pr_err("gc320 sel error !\n");
		return 0;
	}

}

#ifdef CONFIG_ARM64
static u32 gc2d_axi_mux(u32 src)
{
	switch (src) {
	case 0:
		return pll_array[PLL1_624];
	case 1:
		return pll_array[PLL2_CLKOUT];
	case 2:
		return pll_array[PLL1_416];
	case 6:
		return pll_array[PLL5_CLKOUT];
	default:
		pr_err("gc axi clk sel error !\n");
		return 0;
	}
}
#endif

static u32 gc_axi_mux(u32 src)
{
	switch (src) {
	case 0:
		return pll_array[PLL1_624];
	case 1:
		return pll_array[PLL2_CLKOUT];
#ifdef CONFIG_ARM64
	case 2:
		return pll_array[PLL1_416];
	case 6:
		return pll_array[PLL5_CLKOUTP];
#else
	case 2:
		return pll_array[PLL5_CLKOUTP];
	case 3:
		return pll_array[PLL1_416];
#endif
	default:
		pr_err("gc axi clk sel error !\n");
		return 0;
	}

}

static u32 vpu_src_mux(u32 src)
{
	switch (src) {
	case 0:
		return pll_array[PLL1_624];
	case 1:
		return pll_array[PLL5_CLKOUTP];
	case 2:
#ifdef CONFIG_ARM64
		return pll_array[PLL1_416];
	case 6:
		return pll_array[PLL7_CLKOUT];
#else
		return pll_array[PLL5_CLKOUT];
	case 3:
		return pll_array[PLL1_416];
#endif
	default:
		pr_err("vpu clk sel error !\n");
		return 0;
	}
}

typedef u32 (*src_mux) (u32 src);
static void apmu_clk_handle(u32 en, u32 sel, u32 div,
		char *buf, src_mux src)
{
	u32 tmp;

	if (en) {
		tmp = src(sel) / div;
		sprintf(buf, "%d", tmp);
	} else
		sprintf(buf, "OFF");
}

#ifdef CONFIG_ARM64
static void get_hantro(char *hantro_pwr,
		char *hantro_encoder,
		char *hantro_decoder,
		char *hantro_axi)
{
	int aclk_on, dclk_on, eclk_on;
	if (apmu_isld_vpu_pwrctrl.b.pwr_status) {
		sprintf(hantro_pwr, "ON");
		if (pxa1928_is_a0()) {
			aclk_on = apmu_vpu_clk_ctrl.b.axi_clk_div;
			dclk_on = apmu_vpu_clk_ctrl.b.dec_clk_div;
			eclk_on = apmu_vpu_clk_ctrl.b.enc_clk_div;
		} else {
			aclk_on = apmu_vpu_clk_ctrl.b.axi_clk_en;
			dclk_on = apmu_vpu_clk_ctrl.b.dec_clk_en;
			eclk_on = apmu_vpu_clk_ctrl.b.enc_clk_en;
		}

		apmu_clk_handle(eclk_on, apmu_vpu_clk_ctrl.b.enc_clksrc_sel,
				apmu_vpu_clk_ctrl.b.enc_clk_div,
				hantro_encoder,
				vpu_src_mux);
		apmu_clk_handle(dclk_on, apmu_vpu_clk_ctrl.b.dec_clksrc_sel,
				apmu_vpu_clk_ctrl.b.dec_clk_div,
				hantro_decoder,
				vpu_src_mux);
		apmu_clk_handle(aclk_on, apmu_vpu_clk_ctrl.b.axi_clksrc_sel,
				apmu_vpu_clk_ctrl.b.axi_clk_div,
				hantro_axi,
				vpu_src_mux);
	} else {
		sprintf(hantro_pwr, "OFF");
		sprintf(hantro_encoder, "OFF");
		sprintf(hantro_decoder, "OFF");
		sprintf(hantro_axi, "OFF");
	}

}

static void get_gc(char *gc3d_pwr,
	    char *gc3d_1x,
	    char *gc3d_sh,
	    char *gc3d_axi,
	    char *gc2d_pwr,
	    char *gc2d_1x,
	    char *gc2d_axi)
{
	int gc3d_aclk_en, gc3d_clk1x_en, gc3d_clksh_en, gc2d_clk1x_en;
	if (apmu_isld_gc3d_pwrctrl.b.pwr_status) {
		sprintf(gc3d_pwr, "ON");
		if (pxa1928_is_a0()) {
			gc3d_aclk_en = apmu_gc3d_clk_ctrl.b.axi_clk_div;
			gc3d_clk1x_en = apmu_gc3d_clk_ctrl.b.clk1x_div;
			gc3d_clksh_en = apmu_gc3d_clk_ctrl.b.clksh_div;
		} else {
			gc3d_aclk_en = apmu_gc3d_clk_ctrl.b.axi_clk_en;
			gc3d_clk1x_en = apmu_gc3d_clk_ctrl.b.clk1x_en;
			gc3d_clksh_en = apmu_gc3d_clk_ctrl.b.clksh_en;
		}

		apmu_clk_handle(gc3d_clksh_en, apmu_gc3d_clk_ctrl.b.clksh_clksrc_sel,
				apmu_gc3d_clk_ctrl.b.clksh_div,
				gc3d_sh,
				gc_1x2x_mux);

		apmu_clk_handle(gc3d_clk1x_en, apmu_gc3d_clk_ctrl.b.clk1x_clksrc_sel,
				apmu_gc3d_clk_ctrl.b.clk1x_div,
				gc3d_1x,
				gc_1x2x_mux);

		apmu_clk_handle(gc3d_aclk_en, apmu_gc3d_clk_ctrl.b.axi_clksrc_sel,
				apmu_gc3d_clk_ctrl.b.axi_clk_div,
				gc3d_axi,
				gc_axi_mux);
	} else {
		sprintf(gc3d_pwr, "OFF");
		sprintf(gc3d_axi, "OFF");
		sprintf(gc3d_1x, "OFF");
		sprintf(gc3d_sh, "OFF");
	}

	if (apmu_isld_gc2d_pwrctrl.b.pwr_status) {
		sprintf(gc2d_pwr, "ON");
		if (pxa1928_is_a0())
			gc2d_clk1x_en = apmu_gc2d_clk_ctrl.b.clk1x_div;
		else
			gc2d_clk1x_en = apmu_gc2d_clk_ctrl.b.clk1x_en;

		apmu_clk_handle(apmu_gc2d_clk_ctrl.b.axi_clk_en,
				apmu_aclk11_clk_ctrl.b.axi_clksrc_sel,
				apmu_aclk11_clk_ctrl.b.axi_clk_div,
				gc2d_axi,
				gc2d_axi_mux);

		apmu_clk_handle(gc2d_clk1x_en, apmu_gc2d_clk_ctrl.b.clk1x_clksrc_sel,
				apmu_gc2d_clk_ctrl.b.clk1x_div,
				gc2d_1x,
				gc320_mux);
	} else {
		sprintf(gc2d_pwr, "OFF");
		sprintf(gc2d_axi, "OFF");
		sprintf(gc2d_1x, "OFF");
	}
}

static void get_isp(char **isp, char **isp_clk4x_clk, char **isp_axi_clk,
		char **isp_p_sclk, char **isp_wb_clk, char **isp_csi_clk,
		char **isp_tx_4ln_clk, char **isp_tx_2ln_clk)
{
	u32 tmp;

	on_off_slow_judge(apmu_isp_pwr_ctrl.b.isp_pwrup, isp);
	on_off_judge(apmu_isp_clk_ctrl.b.clk4x_div, isp_clk4x_clk);
	on_off_judge(apmu_isp_clk_ctrl.b.aclk_div, isp_axi_clk);
	on_off_judge(apmu_isp_clk_ctrl.b.p_sclk_div, isp_p_sclk);
	on_off_judge(apmu_isp_clk_ctrl.b.wb_clk_div, isp_wb_clk);
	on_off_judge(apmu_isp_clk2_ctrl.b.csi_clk_div, isp_csi_clk);
	on_off_judge(apmu_isp_clk2_ctrl.b.csi_txclk_esc_4ln_en, isp_tx_4ln_clk);
	on_off_judge(apmu_isp_clk2_ctrl.b.csi_txclk_esc_2ln_en, isp_tx_2ln_clk);
}

static void get_audio(char **audio_pwr)
{
	u32 tmp;

	on_off_slow_judge(apmu_audio_pwr_ctrl.b.audio_pwrup,
			audio_pwr);
}

#else

static void get_hantro(char *hantro_pwr,
		char *hantro_encoder,
		char *hantro_decoder,
		char *hantro_axi)
{
	if (apmu_vpu_clk_res_ctrl.b.vpu_pwrup) {
		sprintf(hantro_pwr, "ON");
		apmu_clk_handle(apmu_vpu_clk_res_ctrl.b.vpu_encoder_clk_en,
				apmu_vpu_clk_res_ctrl.b.vpu_encoder_clk_sel,
				apmu_vpu_clk_res_ctrl.b.vpu_encoder_clk_div,
				hantro_encoder,
				vpu_src_mux);
		apmu_clk_handle(apmu_vpu_clk_res_ctrl.b.vpu_decoder_clk_en,
				apmu_vpu_clk_res_ctrl.b.vpu_decoder_clk_sel,
				apmu_vpu_clk_res_ctrl.b.vpu_decoder_clk_div,
				hantro_decoder,
				vpu_src_mux);
		apmu_clk_handle(apmu_vpu_clk_res_ctrl.b.vpu_axi_clk_en,
				apmu_vpu_clk_res_ctrl.b.vpu_axi_clk_sel,
				apmu_vpu_clk_res_ctrl.b.vpu_axi_clk_div,
				hantro_axi,
				vpu_src_mux);
	} else {
		sprintf(hantro_pwr, "OFF");
		sprintf(hantro_encoder, "OFF");
		sprintf(hantro_decoder, "OFF");
		sprintf(hantro_axi, "OFF");
	}

}

static void get_gc(char *gc3d_pwr,
	    char *gc3d_1x,
	    char *gc3d_2x,
	    char *gc3d_axi,
	    char *gc2d,
	    char *gc2d_axi)
{
	if (apmu_gc_clk_res_ctrl.b.gc_pwrup) {
		sprintf(gc3d_pwr, "ON");
		apmu_clk_handle(apmu_gc_clk_res_ctrl.b.gc2200_clk1x_clk2x_en,
				apmu_gc_clk_res_ctrl.b.gc2200_clk2x_src_sel,
				apmu_gc_clk_res_ctrl.b.gc2200_clk_2x_div,
				gc3d_2x,
				gc_1x2x_mux);
		apmu_clk_handle(apmu_gc_clk_res_ctrl.b.gc2200_clk1x_clk2x_en,
				apmu_gc_clk_res_ctrl.b.gc2200_clk1x_src_sel,
				apmu_gc_clk_res_ctrl.b.gc2200_clk_1x_div,
				gc3d_1x,
				gc_1x2x_mux);
	} else {
		sprintf(gc3d_pwr, "OFF");
		sprintf(gc3d_2x, "OFF");
		sprintf(gc3d_1x, "OFF");
	}

	apmu_clk_handle(apmu_gc_clk_res_ctrl.b.gc2200_axiclk_en,
			apmu_gc_clk_res_ctrl.b.gc2200_aclk_sel,
			apmu_gc_clk_res_ctrl.b.gc2200_axi_clk_div,
			gc3d_axi,
			gc_axi_mux);

	apmu_clk_handle(apmu_gc_clk_res_ctrl2.b.gc320_clk_en,
			apmu_gc_clk_res_ctrl2.b.gc320_clk_src_sel,
			apmu_gc_clk_res_ctrl2.b.gc320_clk_div,
			gc2d,
			gc320_mux);

	apmu_clk_handle(apmu_gc_clk_res_ctrl2.b.gc320_axiclk_en,
			apmu_gc_clk_res_ctrl2.b.gc320_axi_clk_sel,
			apmu_gc_clk_res_ctrl2.b.gc320_axi_clk_div,
			gc2d_axi,
			gc_axi_mux);
}

static void get_isp(char **isp, char **isp_clk)
{
	u32 tmp;

	on_off_slow_judge(apmu_isp_pwr_ctrl.b.isp_pwrup, isp);
	on_off_judge(apmu_isp_clk_res_ctrl.b.isp_clk_enable, isp_clk);
}

static void get_ccic(char **ccic_arb,
	      char **ccic_phy,
	      char **ccic_peripheral,
	      char **ccic_axi,
	      char **ccic2_physlow,
	      char **ccic2_phy,
	      char **ccic2_peripheral,
	      char **ccic2_axi)
{
	u32 tmp;

	on_off_judge(apmu_ccic_clk_res_ctrl.b.ccic_axi_arb_clk_en,
			ccic_arb);
	on_off_judge(apmu_ccic_clk_res_ctrl.b.ccic_phyclk_en,
			ccic_phy);
	on_off_judge(apmu_ccic_clk_res_ctrl.b.ccic_clk_en,
			ccic_peripheral);
	on_off_judge(apmu_ccic_clk_res_ctrl.b.ccic_axiclk_en,
			ccic_axi);
	on_off_judge(apmu_ccic2_clk_res_ctrl.b.ccic2_physlowclk_en,
			ccic2_physlow);
	on_off_judge(apmu_ccic2_clk_res_ctrl.b.ccic2_phyclk_en,
			ccic2_phy);
	on_off_judge(apmu_ccic2_clk_res_ctrl.b.ccic2_clk_en,
			ccic2_peripheral);
	on_off_judge(apmu_ccic2_clk_res_ctrl.b.ccic2_axiclk_en,
			ccic2_axi);
}

static void get_audio(char **audio_pwr,
	       char **audio_peripheral)
{
	u32 tmp;

	on_off_slow_judge(apmu_audio_clk_res_ctrl.b.audio_pwrup,
			audio_pwr);
	on_off_judge(apmu_audio_clk_res_ctrl.b.audio_clk_en,
			audio_peripheral);
}

static u32 cal_apll(int index)
{
	u32 fbdiv, refdiv, postdiv;

	if (index == 1) {
		fbdiv = dsp_audio_pll1_config_reg1.b.fbdiv;
		refdiv = dsp_audio_pll1_config_reg1.b.refdiv;
		postdiv = dsp_audio_pll1_config_reg2.b.postdiv;
	} else {
		fbdiv = dsp_audio_pll2_config_reg1.b.fbdiv;
		refdiv = dsp_audio_pll2_config_reg1.b.refdiv;
		postdiv = dsp_audio_pll2_config_reg2.b.postdiv;
	}

	if (refdiv == 0 || postdiv == 0) {
		pr_err("wrong div: refdiv: %d, postdiv %d\n", refdiv, postdiv);
		return 0;
	}

	return 26000 * fbdiv / refdiv / postdiv;

}
#endif

static void cal_axi(u32 *axi_array)
{
	u32 tmp;

	tmp = axi_src_mux(apmu_pll_sel_status.b.soc_axi_pll_sel);

	axi_array[1] = tmp / (apmu_dm2_cc.b.soc_axi_clk2_div + 1);
#ifndef CONFIG_ARM64
	axi_array[0] = tmp / (apmu_dm_cc_pj.b.soc_axi_clk_div + 1);
	axi_array[2] = tmp / (apmu_dm2_cc.b.soc_axi_clk3_div + 1);
	axi_array[3] = tmp / (apmu_dm2_cc.b.soc_axi_clk4_div + 1);
#endif
}

static u32 ddr_src_mux(u32 src)
{
	switch (src) {
	case 0:
		return pll_array[PLL1_416];
	case 1:
		return pll_array[PLL1_624];
	case 2:
		return pll_array[PLL2_CLKOUT];
	case 3:
		return pll_array[PLL5_CLKOUT];
	default:
		pr_err("hw dfc in progress\n"
			"apmu_mc_clk_stat.b.pmu_ddr_clk_sel: 0x%x !\n",
			src);
		return 0;
	}
}

static void cal_cclk(u32 *core, u32 *axi)
{
	u32 tmp;
	union apmu_coreapss_clkctrl ac =
		apmu_coreapss_clkctrl;

	switch (ac.b.coreap_clksrc_sel) {
	case 0:
		tmp = pll_array[PLL1_624];
		break;
	case 1:
#ifdef CONFIG_ARM64
		if (pxa1928_is_a0())
			tmp = pll_array[PLL4_CLKOUT];
		else
			tmp = pll_array[PLL1_1248];
		break;
	case 2:
		tmp = pll_array[PLL2_CLKOUT];
		break;
	case 6:
#else
		tmp = pll_array[PLL2_CLKOUT];
		break;
	case 3:
#endif
		tmp = pll_array[PLL2_CLKOUTP];
		break;
	default:
		pr_err("apmu_coreapss_clkctrl.b.coreap_clksrc_sel error: 0x%x !\n",
				ac.b.coreap_clksrc_sel);
		*core = 0;
		*axi = 0;
		return;
	}

	if (!ac.b.coreap_clk_div) {
		*core = 0;
		pr_err("apmu_coreapss_clkctrl.b.coreap_clk_div error: 0!\n");
	} else
		*core = tmp / ac.b.coreap_clk_div;

	if (!ac.b.coreap_axim_clk_div) {
		*axi = 0;
		pr_err("apmu_coreapss_clkctrl.b.coreap_axim_clk_div error: 0!\n");
	} else
		*axi = *core / ac.b.coreap_axim_clk_div;
}

static u32 hw_pll4_vco(void)
{
	return 4 * ddrdfc_fp_upr.b.fp_upr2
			* VCXO / ddrdfc_fp_upr.b.fp_upr1;
}

static u32 cal_hw_dclk(void)
{
	if (ddrdfc_fp_lwr.b.sel_ddrpll)
		return pll_array[PLL4_CLKOUT];
	else
		return ddr_src_mux(ddrdfc_fp_upr.b.fp_upr0) /
			(ddrdfc_fp_upr.b.fp_upr3 + 1);
}

static u32 cal_dclk(void)
{
	if (apmu_mc_clk_stat.b.mc_clk_src_sel)
		return pll_array[PLL4_CLKOUT];
	else
		return ddr_src_mux(apmu_mc_clk_stat.b.pmu_ddr_clk_sel) /
			(apmu_mc_clk_stat.b.pmu_ddr_clk_div + 1);
}

static void read_cur_clk(void)
{
	mpmu_pll2cr.v = readl(MPMU_PLL2CR);
	mpmu_pll3cr.v = readl(MPMU_PLL3CR);
	mpmu_pll4cr.v = readl(MPMU_PLL4CR);
	mpmu_pll5cr.v = readl(MPMU_PLL5CR);
	mpmu_pll2_ctrl1.v = readl(MPMU_PLL2_CTRL1);
	mpmu_pll3_ctrl1.v = readl(MPMU_PLL3_CTRL1);
	mpmu_pll4_ctrl1.v = readl(MPMU_PLL4_CTRL1);
	mpmu_pll5_ctrl1.v = readl(MPMU_PLL5_CTRL1);
	mpmu_pll2_ctrl4.v = readl(MPMU_PLL2_DIFF_CTRL);
	mpmu_pll3_ctrl4.v = readl(MPMU_PLL3_DIFF_CTRL);
	mpmu_pll4_ctrl4.v = readl(MPMU_PLL4_DIFF_CTRL);
	mpmu_pll5_ctrl4.v = readl(MPMU_PLL5_DIFF_CTRL);
#ifdef CONFIG_ARM64
	mpmu_pll6cr.v = readl(MPMU_PLL6CR);
	mpmu_pll7cr.v = readl(MPMU_PLL7CR);
	mpmu_pll6_ctrl1.v = readl(MPMU_PLL6_CTRL1);
	mpmu_pll7_ctrl1.v = readl(MPMU_PLL7_CTRL1);
	mpmu_pll6_ctrl4.v = readl(MPMU_PLL6_DIFF_CTRL);
	mpmu_pll7_ctrl4.v = readl(MPMU_PLL7_DIFF_CTRL);
#endif

	apmu_ddrdfc_ctrl.v = readl(DDRDFC_CTRL);
	ddrdfc_fp_upr.v = readl(DDRDFC_FP_UPR);
	ddrdfc_fp_lwr.v = readl(DDRDFC_FP_LWR);
	apmu_mc_clk_stat.v = readl(APMU_MC_CLK_STAT);
#ifndef CONFIG_ARM64
	apmu_dm_cc_pj.v = readl(APMU_DM_CC_PJ);
#endif
	apmu_dm2_cc.v = readl(APMU_DM2_CC);
	apmu_pll_sel_status.v = readl(APMU_PLL_SEL_STATUS);
	apmu_coreapss_clkctrl.v = readl(APMU_COREAPSS_CLKCTRL);
	apmu_core_pwrmode[0].v = readl(APMU_CORE0_PWRMODE);
	apmu_core_pwrmode[1].v = readl(APMU_CORE1_PWRMODE);
	apmu_core_pwrmode[2].v = readl(APMU_CORE2_PWRMODE);
	apmu_core_pwrmode[3].v = readl(APMU_CORE3_PWRMODE);
#ifdef CONFIG_ARM64
	apmu_isld_vpu_pwrctrl.v = readl(APMU_ISLD_VPU_PWRCTRL);
	apmu_isld_gc3d_pwrctrl.v = readl(APMU_ISLD_GC3D_PWRCTRL);
	apmu_isld_gc2d_pwrctrl.v = readl(APMU_ISLD_GC2D_PWRCTRL);
	apmu_vpu_clk_ctrl.v = readl(APMU_VPU_CLK_CTRL);
	apmu_gc3d_clk_ctrl.v = readl(APMU_GC3D_CLK_CTRL);
	apmu_gc2d_clk_ctrl.v = readl(APMU_GC2D_CLK_CTRL);
	apmu_aclk11_clk_ctrl.v = readl(APMU_ACLK11_CLK_CTRL);
	apmu_isp_pwr_ctrl.v = readl(APMU_ISP_PWR);
	apmu_isp_clk_ctrl.v = readl(APMU_ISP_CLK);
	apmu_isp_clk2_ctrl.v = readl(APMU_ISP_CLK2);
	apmu_audio_pwr_ctrl.v = readl(APMU_AUDIO_PWR_CTRL);
	apmu_dvc_stat.v = readl(APMU_DVC_STAT);
#else
	apmu_vpu_clk_res_ctrl.v = readl(APMU_VPU_CLK_RES_CTRL);
	apmu_gc_clk_res_ctrl.v = readl(APMU_GC_CLK_RES_CTRL);
	apmu_gc_clk_res_ctrl2.v = readl(APMU_GC_CLK_RES_CTRL2);
	apmu_isp_clk_res_ctrl.v = readl(APMU_ISPCLK);
	apmu_isp_pwr_ctrl.v = readl(APMU_ISPPWR);
	apmu_ccic_clk_res_ctrl.v = readl(APMU_CCIC_RST);
	apmu_ccic2_clk_res_ctrl.v = readl(APMU_CCIC2_RST);
	apmu_audio_clk_res_ctrl.v = readl(APMU_AUDIO_CLK_RES_CTRL);
	if (apmu_audio_clk_res_ctrl.b.audio_pwrup &&
	    apmu_audio_clk_res_ctrl.b.audio_clk_en) {
		dsp_audio_pll1_config_reg1.v =
			readl(DSP_AUDIO_PLL1_CONFIG_REG1);
		dsp_audio_pll1_config_reg2.v =
			readl(DSP_AUDIO_PLL1_CONFIG_REG2);
	}
#endif
	apmu_coresp_pwrmode.v = readl(APMU_CORESP_PWRMODE);
	apmu_dvc_apcoress.v = readl(APMU_DVC_APCORESS);
	apmu_dvc_apss.v = readl(APMU_DVC_APSS);
}

static char *core_status[] = {
	"C0",
	"C1",
	"C2",
};

static char *core_subsystem_status[] = {
	"MP0",
	"MP1",
	"MP2",
	"OFF",
};

static void get_core_status(char **core)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (apmu_core_pwrmode[i].b.pwr_status) {
			if (apmu_core_pwrmode[i].b.lpm_status)
				core[i] = core_status[1];
			else
				core[i] = core_status[0];
		} else
			core[i] = core_status[2];
	}
}

static void get_core_subsystem_status(char **core_sub)
{
	/* if we can read syset, must be in MP0 */
	*core_sub = core_subsystem_status[0];
}

static void get_sp(char **sp)
{
	u32 tmp;

	on_off_judge(apmu_coresp_pwrmode.b.lpm_status, sp);
}

#ifdef CONFIG_ARM64
static void get_dvc_stat(u32 *dvc_stat_cp, u32 *dvc_stat_cur)
{
	*dvc_stat_cp = apmu_dvc_stat.b.vl_cp;
	*dvc_stat_cur = apmu_dvc_stat.b.vl_cur;
}
#endif

static void get_dvc(u32 *mp2l2off_vl,
	     u32 *mp2l2on_vl,
	     u32 *mp1_vl,
	     u32 *mp0_vl,
	     u32 *d0_vl,
	     u32 *d1_vl,
	     u32 *d0cg_vl,
	     u32 *d2_vl)
{
	*mp2l2off_vl = apmu_dvc_apcoress.b.mp2l2off_vl;
	*mp2l2on_vl = apmu_dvc_apcoress.b.mp2l2on_vl;
	*mp1_vl = apmu_dvc_apcoress.b.mp1_vl;
	*mp0_vl = apmu_dvc_apcoress.b.mp0_vl;
	*d0_vl = apmu_dvc_apss.b.d0_vl;
	*d1_vl = apmu_dvc_apss.b.d1_vl;
	*d0cg_vl = apmu_dvc_apss.b.d0cg_vl;
	*d2_vl = apmu_dvc_apss.b.d2_vl;
}

static ssize_t pxa1928_sysset_read(char *buf, ssize_t size)
{
	int s;
	u32 axi_array[4];
	u32 cclk, dclk, core_axi;
	u32 mp2l2off_vl, mp2l2on_vl, mp1_vl, mp0_vl;
	u32 d0_vl, d1_vl, d0cg_vl, d2_vl;
	char *core[4];
	char *core_subsystem;
	char *sp;
	char hantro[10], hantro_encoder[10];
	char hantro_decoder[10], hantro_axi[10];
#ifdef CONFIG_ARM64
	char gc3d_pwr[10], gc3d_1x[10], gc3d_sh[10], gc3d_axi[10];
	char gc2d_pwr[10], gc2d_1x[10], gc2d_axi[10];
	char *isp, *isp_clk4x_clk, *isp_axi_clk, *isp_p_sclk, *isp_wb_clk;
	char *isp_csi_clk, *isp_tx_4ln_clk, *isp_tx_2ln_clk;
	char *audio_pwr;
	u32 dvc_stat_cp, dvc_stat_cur;
#else
	u32 apll1, apll2;
	char gc3d_pwr[10], gc3d_1x[10], gc3d_2x[10], gc3d_axi[10];
	char gc2d[10], gc2d_axi[10];
	char *isp, *isp_clk;
	char *ccic_arb, *ccic_phy, *ccic_peripheral;
	char *ccic_axi, *ccic2_physlow, *ccic2_phy;
	char *ccic2_peripheral, *ccic2_axi;
	char *audio_peripheral, *audio_pwr;
#endif

	read_cur_clk();

	/* cal_pll must be called first */
	cal_pll();

	cal_axi(axi_array);

	cal_cclk(&cclk, &core_axi);

	if (apmu_ddrdfc_ctrl.b.ddrpll_ctrl &&
		apmu_ddrdfc_ctrl.b.dfc_hw_en) {
		pll4_vco = hw_pll4_vco();
		pll_array[PLL4_CLKOUT] = pll4_vco
				>> ddrdfc_fp_upr.b.fp_upr3;
		dclk = cal_hw_dclk() / 2;
	} else
		dclk = cal_dclk() / 2;

	get_core_status(core);

	get_core_subsystem_status(&core_subsystem);

	get_hantro(hantro, hantro_encoder, hantro_decoder, hantro_axi);

#ifdef CONFIG_ARM64
	get_gc(gc3d_pwr, gc3d_1x, gc3d_sh, gc3d_axi,
					gc2d_pwr, gc2d_1x, gc2d_axi);

	get_isp(&isp, &isp_axi_clk, &isp_clk4x_clk, &isp_p_sclk,
		&isp_wb_clk, &isp_csi_clk, &isp_tx_4ln_clk, &isp_tx_2ln_clk);

	get_audio(&audio_pwr);
	get_dvc_stat(&dvc_stat_cp, &dvc_stat_cur);
#else
	if (apmu_audio_clk_res_ctrl.b.audio_pwrup &&
	    apmu_audio_clk_res_ctrl.b.audio_clk_en) {
		apll1 = cal_apll(1);
		apll2 = cal_apll(2);
	} else {
		apll1 = apll2 = 0;
	}

	get_gc(gc3d_pwr, gc3d_1x, gc3d_2x, gc3d_axi, gc2d, gc2d_axi);

	get_isp(&isp, &isp_clk);

	get_ccic(&ccic_arb, &ccic_phy, &ccic_peripheral,
			&ccic_axi, &ccic2_physlow, &ccic2_phy,
			&ccic2_peripheral, &ccic2_axi);

	get_audio(&audio_pwr, &audio_peripheral);
#endif

	get_sp(&sp);


	get_dvc(&mp2l2off_vl, &mp2l2on_vl, &mp1_vl, &mp0_vl,
			&d0_vl, &d1_vl, &d0cg_vl, &d2_vl);

	s = snprintf(buf, size, "PLL1_312[%d], PLL1_416[%d], PLL1_624[%d],\n",
		pll_array[PLL1_312], pll_array[PLL1_416], pll_array[PLL1_624]);
	s += snprintf(buf + s, size - s, "PLL2_VCO[%d], PLL2[%d], Pll2P[%d],\n",
		pll2_vco, pll_array[PLL2_CLKOUT], pll_array[PLL2_CLKOUTP]);
	s += snprintf(buf + s, size - s, "PLL3_VCO[%d], PLL3[%d], PLL3P[%d],\n",
		pll3_vco, pll_array[PLL3_CLKOUT], pll_array[PLL3_CLKOUTP]);
	s += snprintf(buf + s, size - s, "PLL4_VCO[%d], PLL4[%d], PLL4P[%d],\n",
		pll4_vco, pll_array[PLL4_CLKOUT], pll_array[PLL4_CLKOUTP]);
	s += snprintf(buf + s, size - s, "PLL5_VCO[%d], PLL5[%d], PLL5P[%d]\n",
		pll5_vco, pll_array[PLL5_CLKOUT], pll_array[PLL5_CLKOUTP]);
#ifdef CONFIG_ARM64
	s += snprintf(buf + s, size - s, "PLL6_VCO[%d], PLL6[%d], PLL6P[%d]\n",
		pll6_vco, pll_array[PLL6_CLKOUT], pll_array[PLL6_CLKOUTP]);
	s += snprintf(buf + s, size - s, "PLL7_VCO[%d], PLL7[%d], PLL7P[%d]\n",
		pll7_vco, pll_array[PLL7_CLKOUT], pll_array[PLL7_CLKOUTP]);
#endif

	s += snprintf(buf + s, size - s, "\nCORE[%d], CORE_AXI[%d],",
		cclk, core_axi);
	s += snprintf(buf + s, size - s, " DCLK_1x[%d]\n", dclk);
#ifdef CONFIG_ARM64
	s += snprintf(buf + s, size - s, "AXI[%d]\n", axi_array[1]);
#else
	s += snprintf(buf + s, size - s, "AXI1[%d], AXI2[%d],",
		axi_array[0], axi_array[1]);
	s += snprintf(buf + s, size - s, " AXI3[%d], AXI4[%d],\n",
		axi_array[2], axi_array[3]);
#endif
	s += snprintf(buf + s, size - s, "CORE1[%s], CORE2[%s],",
		core[0], core[1]);
	s += snprintf(buf + s, size - s, " CORE3[%s], CORE4[%s],\n",
		core[2], core[3]);
	s += snprintf(buf + s, size - s, "CORE_SUDSYSTEM[%s]\n",
		core_subsystem);

	s += snprintf(buf + s, size - s, "\nHANTRO_PWR[%s],", hantro);
	s += snprintf(buf + s, size - s, " HANTRO_ENCODER_CLK[%s],\n",
		hantro_encoder);
	s += snprintf(buf + s, size - s, "HANTRO_DECODER_CLK[%s],",
		hantro_decoder);
	s += snprintf(buf + s, size - s, " HANTRO_AXI_CLK[%s],\n", hantro_axi);
	s += snprintf(buf + s, size - s, "GC3D_PWR[%s], GC3D_1X_CLK[%s],\n",
		gc3d_pwr, gc3d_1x);
#ifdef CONFIG_ARM64
	s += snprintf(buf + s, size - s, "GC3D_SH_CLK[%s], GC3D_AXI_CLK[%s],\n",
		gc3d_sh, gc3d_axi);
	s += snprintf(buf + s, size - s, "GC2D_PWR[%s], GC2D_AXI_CLK[%s],",
		gc2d_pwr, gc2d_axi);
	s += snprintf(buf + s, size - s, " GC2D_1X[%s]\n", gc2d_1x);
	s += snprintf(buf + s, size - s, "\nSP[%s], ISP_PWR[%s],",
		sp, isp);
	s += snprintf(buf + s, size - s, " ISP_AXI_CLK[%s]\n", isp_axi_clk);
	s += snprintf(buf + s, size - s, "ISP_CLK4X_CLK[%s], ISP_P_SCLK[%s], ",
		isp_clk4x_clk, isp_p_sclk);
	s += snprintf(buf + s, size - s, "ISP_WB_CLK[%s], ISP_CSI_CLK[%s], ",
		isp_wb_clk, isp_csi_clk);
	s += snprintf(buf + s, size - s, "ISP_TX_4LN_CLK[%s],", isp_tx_4ln_clk);
	s += snprintf(buf + s, size - s, " ISP_TX2LN_CLK[%s]\n",
		isp_tx_2ln_clk);

	s += snprintf(buf + s, size - s, "\nAUDIO_PWR[%s]\n", audio_pwr);
#else
	s += snprintf(buf + s, size - s, "GC3D_2X_CLK[%s], GC3D_AXI_CLK[%s],\n",
		gc3d_2x, gc3d_axi);
	s += snprintf(buf + s, size - s, "GC2D_AXI_CLK[%s], GC2D[%s]\n",
		gc2d_axi, gc2d);
	s += snprintf(buf + s, size - s, "\nSP[%s], ISP_PWR[%s], ISP_CLK[%s]\n",
		sp, isp, isp_clk);

	s += snprintf(buf + s, size - s, "CCIC_ARB[%s], CCIC_PHY[%s],\n",
		ccic_arb, ccic_phy);
	s += snprintf(buf + s, size - s, "CCIC_PERIPHERAL[%s], CCIC_AXI[%s],\n",
		ccic_peripheral, ccic_axi);
	s += snprintf(buf + s, size - s, "CCIC2_PHYSHOW[%s], CCIC2_PHY[%s],\n",
		ccic2_physlow, ccic2_phy);
	s += snprintf(buf + s, size - s, "CCIC2_PERIPHERAL[%s],",
		ccic2_peripheral);
	s += snprintf(buf + s, size - s, " CCIC2_AXI[%s]\n", ccic2_axi);

	s += snprintf(buf + s, size - s, "\nAUDIO_PLL1[%d], AUDIO_PLL2[%d],\n",
		apll1, apll2);
	s += snprintf(buf + s, size - s, "AUDIO_PWR[%s],", audio_pwr);
	s += snprintf(buf + s, size - s, " AUDIO_PERIPHERAL[%s]\n",
		audio_peripheral);
#endif

	s += snprintf(buf + s, size - s, "\nMP2L2OFF_VL[%d], MP2L2ON_VL[%d],\n",
		mp2l2off_vl, mp2l2on_vl);
	s += snprintf(buf + s, size - s, "MP1_VL[%d], MP0_VL[%d],\n",
		mp1_vl, mp0_vl);
	s += snprintf(buf + s, size - s, "D0_VL[%d], D1_VL[%d],",
		d0_vl, d1_vl);
	s += snprintf(buf + s, size - s, " D0CG_VL[%d],	D2_VL[%d]\n",
		d0cg_vl, d2_vl);
#ifdef CONFIG_ARM64
	s += snprintf(buf + s, size - s, " CP_VL[%d],	DVC_CUR_VL[%d]\n",
		dvc_stat_cp, dvc_stat_cur);
#endif

	return s;
}

#define		SYSSET_BUFSIZE		2048

static char buf[SYSSET_BUFSIZE];
int pxa1928_sysset_print(void)
{
	ssize_t i = 0, len;

	len = pxa1928_sysset_read(buf, SYSSET_BUFSIZE);
	if (len < 0)
		return -EINVAL;

	while (i < len)
		printk("%c", buf[i++]);

	return 0;
}
EXPORT_SYMBOL(pxa1928_sysset_print);

static int read_reg_value(char *buf, int type, int i, int s, ssize_t size)
{
	unsigned int reg_value;
	char *id;

	switch (type) {
	case MPMU:
		id = "MPMU";
		reg_value = readl(MPMU_REG(i));
		break;
	case APMU:
		id = "APMU";
		reg_value = readl(APMU_REG(i));
		break;
	case MCU:
		id = "MCU";
		reg_value = readl(MCU_REG(i));
		break;
	case ICU:
		id = "ICU";
		reg_value = readl(ICU_REG(i));
		break;

	default:
		id = NULL;
		reg_value = 0;
	}

	return snprintf(buf + s, size - s, "%s[0x%x]: 0x%x\n",
						id, i, reg_value);
}

static int reg_read_print(int *reg_table, char *buf, ssize_t size, int type)
{
	unsigned int index = 0;
	unsigned int i, start, end;
	int s = 0, s1 = 0;

	while (reg_table[index] != -2) {
		if (reg_table[index] == -1) {
			start = reg_table[index + 1];
			end = reg_table[index + 2];
			for (i = start; i <= end; i += 4) {
				s1 = read_reg_value(buf, type, i, s, size);
				s += s1;
			}
			index += 3;
		} else {
			i = reg_table[index];
			s1 = read_reg_value(buf, type, i, s, size);
			s += s1;
			index += 1;
		}
	}
	return s;
}

#ifdef CONFIG_ARM64
/*
 * Independent  numver stands by independent register.
 * -1 means the following two number constitute
 * a registres range. -2 means the end of the
 */
static int pxa1928_pmum_table[] = {
	-1, 0x0, 0x18,
	-1, 0x20, 0x44,
	-1, 0x50, 0x74,
	0x100, 0x200, 0x204,
	-1, 0x414, 0x41c,
	0x1000,
	-1, 0x1020, 0x1028,
	0x1060, 0x1070, 0x1074, 0x1080,
	-1, 0x1100, 0x1128,
	0x1130, 0x1134,
	-1, 0x1140, 0x1144,
	-1, 0x1200, 0x1208,
	-1, 0x1300, 0x1310,
	-1, 0x1400, 0x1410,
	-2
};
#else
static int pxa1928_pmum_table[] = {
	-1, 0x0, 0x18,
	-1, 0x20, 0x44,
	-1, 0x50, 0x70,
	0x100, 0x200, 0x204,
	-1, 0x414, 0x41c,
	0x1000,
	-1, 0x1020, 0x1028,
	-1, 0x1100, 0x1128,
	0x1130, 0x1134,
	-1, 0x1140, 0x1154,
	-1, 0x1200, 0x1208,
	-2
};
#endif

static ssize_t pxa1928_pmum_read(char *buf, ssize_t size)
{
	return reg_read_print(pxa1928_pmum_table, buf, size, MPMU);
}

#ifdef CONFIG_ARM64
static int pxa1928_pmua_table[] = {
	-1, 0, 0, 0x10,0x14,
	0x20, 0x24,
	-1, 0x100, 0x1fc,
	-1, 200, 0x208,0x214,
	0x21c, 0x228, 0x234,
	-1, 0x254, 0x26c,
	0x27c, 0x288,
	-1, 0x298, 0x2a0,
	0x2b0, 0x2b4, 0x2c0, 0x2c4,
	0x2d4, 0x2dc,
	-1, 0x2e0, 0x2ec,
	0x2f8, 0x30c, 0x318,
	0x320, 0x334,
	0x350, 0x354, 0x35c,
	0x360, 0x364,
	-1, 0x370, 0x38c,
	-1, 0x394, 0x3b8,
	0x3d8,
	-1, 0x3e0, 0x3e8,
	0x3f0, 0x3f4, 0x3fc,
	-1, 0x400, 0x414,
	-1, 0x420, 0x424,
	-1, 0x440, 0x444, 0x458,
	0x464, 0x46c,
	-1, 0x470, 0x478,
	-1, 0x480, 0x48c,
	-1, 0x4a0, 0x4ac,
	-1, 0x4c0, 0x4d0,
	0x4e0, 0x4e4,
	0x500, 0x504, 0x510,
	-1, 0x540, 0x548,
	-1, 0x550, 0x558, 0x55c,
	-1, 0x560, 0x56c,
	-1, 0x580, 0x594,
	0x59c, 0x5a0, 0x5a8, 0x5ac,
	-1, 0x5b0, 0x5b8,
	-1, 0x5c0, 0x5cc,
	-1, 0x5e0, 0x5ec,
	-2
};
#else
static int pxa1928_pmua_table[] = {
	-1, 0, 0x14,
	-1, 0x1c, 0x20,
	0x28, 0x34, 0x40,
	-1, 0x48, 0x6c,
	0x7c,
	-1, 0x98, 0xa4,
	0xb0, 0xb4, 0xc0, 0xc4, 0xcc,
	-1, 0xd0, 0xec,
	-1, 0xf4, 0x104,
	-1, 0x10c, 0x110,
	-1, 0x118, 0x134,
	-1, 0x13c, 0x140,
	-1, 0x14c, 0x154,
	-1, 0x15c, 0x164,
	-1, 0x18c, 0x1b4,
	0x1d8, 0x1fc,
	-1, 0x220, 0x224,
	0x240, 0x244, 0x258, 0x264,
	-1, 0x26c, 0x28c,
	-1, 0x2a0, 0x2ac,
	-1, 0x2c0, 0x2d0,
	-1, 0x2e0, 0x2e4,
	-1, 0x340, 0x354,
	-1, 0x380, 0x394,
	0x39c,
	-1, 0x3b0, 0x3cc,
	-1, 0x3e0, 0x3ec,
	-2
};
#endif

static ssize_t pxa1928_pmua_read(char *buf, ssize_t size)
{
	return reg_read_print(pxa1928_pmua_table, buf, size, APMU);
}

static int pxa1928_mcu_table[] = {
	-1, 0, 0x8,
	-1, 0x40, 0x4c,
	-1, 0x54, 0x60,
	-1, 0x80, 0x8c,
	-1, 0x200, 0x204,
	-1, 0x210, 0x214,
	-1, 0x2c0, 0x2c8,
	-1, 0x300, 0x304,
	-1, 0x310, 0x314,
	-1, 0x340, 0x348,
	-1, 0x380, 0x3b4,
	-1, 0x400, 0x438,
	-1, 0x500, 0x50c,
	-2
};
static ssize_t pxa1928_mcu_read(char *buf, ssize_t size)
{
	return reg_read_print(pxa1928_mcu_table, buf, size, MCU);

}

static int pxa1928_icu_table[] = {
	-1, 0, 0x144,
	-1, 0x150, 0x160,
	-1, 0x168, 0x184,
	-1, 0x18c, 0x248,
	-2
};
static ssize_t pxa1928_icu_read(char *buf, ssize_t size)
{
	return reg_read_print(pxa1928_icu_table, buf, size, ICU);

}

PXA1928_SYSSET_OPS(pxa1928_sysset);
PXA1928_SYSSET_OPS(pxa1928_pmum);
PXA1928_SYSSET_OPS(pxa1928_pmua);
PXA1928_SYSSET_OPS(pxa1928_mcu);
PXA1928_SYSSET_OPS(pxa1928_icu);

int __init init_pxa1928_sysset(void)
{

	struct dentry *sysset;
	struct dentry *pxa1928_sysset, *pxa1928_pmum;
	struct dentry *pxa1928_pmua, *pxa1928_mcu, *pxa1928_icu;

	apmu_virt_base = (unsigned long)ioremap(APMU_BASE, APMU_RANGE);
	mpmu_virt_base = (unsigned long)ioremap(MPMU_BASE, MPMU_RANGE);
	mcu_virt_base = (unsigned long)ioremap(MCU_BASE, MCU_RANGE);
	icu_virt_base = (unsigned long)ioremap(ICU_BASE, ICU_RANGE);
	aud_virt_base = (unsigned long)ioremap(AUD_BASE, AUD_RANGE);
	dfc_virt_base = (unsigned long)ioremap(DFC_BASE, DFC_RANGE);

	sysset = debugfs_create_dir("pxa1928_sysset", NULL);
	if (!sysset)
		return -ENOENT;

	pxa1928_sysset = debugfs_create_file("pxa1928_sysset", 0664, sysset,
					NULL, &pxa1928_sysset_ops);
	if (!pxa1928_sysset) {
		pr_err("debugfs entry created failed in %s\n", __func__);
		goto err_pxa1928_sysset;
	}

	pxa1928_pmum = debugfs_create_file("pxa1928_pmum", 0664, sysset,
					NULL, &pxa1928_pmum_ops);
	if (!pxa1928_pmum) {
		pr_err("debugfs entry created failed in %s\n", __func__);
		goto err_pmum;
	}

	pxa1928_pmua = debugfs_create_file("pxa1928_pmua", 0664, sysset,
					NULL, &pxa1928_pmua_ops);
	if (!pxa1928_pmua) {
		pr_err("debugfs entry created failed in %s\n", __func__);
		goto err_pmua;
	}

	pxa1928_mcu = debugfs_create_file("pxa1928_mcu", 0664, sysset,
					NULL, &pxa1928_mcu_ops);
	if (!pxa1928_mcu) {
		pr_err("debugfs entry created failed in %s\n", __func__);
		goto err_mcu;
	}

	pxa1928_icu = debugfs_create_file("pxa1928_icu", 0664, sysset,
					NULL, &pxa1928_icu_ops);
	if (!pxa1928_icu) {
		pr_err("debugfs entry created failed in %s\n", __func__);
		goto err_icu;
	}
	return 0;

err_icu:
	debugfs_remove(pxa1928_mcu);
err_mcu:
	debugfs_remove(pxa1928_pmua);
err_pmua:
	debugfs_remove(pxa1928_pmum);
err_pmum:
	debugfs_remove(pxa1928_sysset);
err_pxa1928_sysset:
	debugfs_remove(sysset);

	return -ENOENT;
}
