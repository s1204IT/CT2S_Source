#ifndef __ADIR_CLK_SRC_H
#define __ADIR_CLK_SRC_H

#include <linux/clk-provider.h>
#include <linux/clk/mmpdcstat.h>
#include <mach/addr-map.h>
#include <mach/regs-mpmu.h>
#include <mach/regs-mccu.h>
#include <mach/regs-apmu.h>
#include <mach/regs-accu.h>

#define MHZ_TO_HZ	(1000000)
#define MHZ_TO_KHZ	(1000)

#define spin_lock_check(lock) {				\
	if (lock)					\
		spin_lock(lock);			\
}

#define spin_unlock_check(lock) {			\
	if (lock)					\
		spin_unlock(lock);			\
}

#define CLK_SET_BITS(set, clear, addr)	{		\
	u32 reg_val;					\
	reg_val = readl_relaxed(addr);			\
	reg_val &= ~(clear);				\
	reg_val |= (set);				\
	writel_relaxed(reg_val, addr);			\
}

enum mpmu_pll {
	MPMU_SYS_PLL1 = 0x0,
	MPMU_SYS_PLL2,
	MPMU_DDR_PLL,
	MPMU_AUD_PLL,
};

enum mccu_clk {
	MCCU_PLL1_624 = 0x1,
	MCCU_PLL1_500,
	MCCU_PLL1_416,
	MCCU_PLL2_800,
};

enum apmu_pll {
	APMU_PLL3 = 0x0,
	APMU_CORE_PLL1,
	APMU_PLL4,
	APMU_CORE_PLL2,
	NR_APMU_PLL,
};

/* PLL encoding table used by 'SOURCE_BITS' of ACCU control register */
enum accu_clk_option {
	SYS_PLL1_416 = 0x0,
	SYS_PLL1_500,
	SYS_PLL1_624,
	SYS_PLL2_800,
	SYS_PLL3,
	HDMI_PLL,
	MC_DCLK,	/* AKA: DDR_PLL */
	CORE_PLL,
};

/* used by mmp_flags */
/* the register field of the control register of a clock or clock source */
#define MMP_CLK_CTRL_REG_FROM_ACCU	BIT(0)
#define MMP_CLK_CTRL_REG_FROM_MCCU	BIT(1)
#define MMP_CLK_CTRL_REG_FROM_APMU	BIT(2)
#define MMP_CLK_CTRL_REG_FROM_MPMU	BIT(3)
#define MMP_CLK_CTRL_REG_FROM_LCD	BIT(4)
#define MMP_CLK_CTRL_REG_FROM_AUDIO	BIT(18)

/* i.e. SYS_PLL1_624 has different branch for different subsystem */
#define MMP_CLK_SRC_FROM_APPS_SS	BIT(5)
#define MMP_CLK_SRC_FROM_SRD_SS		BIT(6)
#define MMP_CLK_SRC_FROM_AUD_SS		BIT(7)

/* clock request by SW or HW */
#define MMP_CLK_PLL_CHECK_REQ_MODE	BIT(8)
/* some PLLs should be always on though it could be turned off by SW */
#define MMP_CLK_PLL_ALWAYS_ON		BIT(9)
/* for some PLLs, we need check PLL lock status after request it */
#define MMP_CLK_PLL_CHECK_LOCKED	BIT(10)

/* whether a specific cloch supports on-the-fly FC */
#define MMP_CLK_SUPPORT_OTF_FC		BIT(11)
/* whether a specific clock has an anchor clock */
#define MMP_CLK_HAS_ANCHOR		BIT(12)
/* some clocks such as UART, I2C, valid dividers are limited and fixed */
#define MMP_CLK_USE_FIXED_DIV		BIT(13)
/* some clocks are always on */
#define MMP_CLK_ALWAYS_ON		BIT(14)
/* some clocks are always on */
#define MMP_CLK_ONLY_ON_OFF_OPS		BIT(15)
/* GC3D_Unit_FN & GC3D_Unit_SH have two control registers */
#define MMP_CLK_HAS_TWO_CTRL_REGS	BIT(16)
/* i.e. VPU encoder is always the same clock as AXI2 */
#define MMP_CLK_HAS_FIXED_PARENT	BIT(17)

/* used by all subsystems */
#define MPMU_HW_CLK_REQ		BIT(30)
#define MPMU_SW_CLK_REQ		BIT(31)

/* for MPMU_AP_PLLREQ */
#define MPMU_AP_PLL1_REQ	BIT(16)
#define MPMU_AP_PLL2_REQ	BIT(17)

/* for MPMU_SRD_PLLREQ */
#define MPMU_SRD_PLL1_REQ	BIT(16)
#define MPMU_SRD_PLL2_REQ	BIT(17)
#define MPMU_SRD_PLLD_REQ	BIT(18)

/* for MPMU_AUD_PLLREQ */
#define MPMU_AUD_PLL1_REQ	BIT(16)
#define MPMU_AUD_PLLA_REQ	BIT(17)

/* for MPMU_PLLSTAT */
#define MPMU_PLL_ON(id)		BIT(4 * id)
#define MPMU_PLL_IS_LOCKED(id)	BIT(4 * id + 3)

/* for MCCU clock */
#define MCCU_CLK_REQ(id)	BIT(id)

/* for APMU_AP_CLK_REQ */
#define APMU_PLL_STABLE_LOCK	BIT(8)
#define APMU_SW_CLK_REQ		BIT(5)
#define APMU_HW_CLK_REQ		BIT(4)

/* for configuration of DDR PLL and APMU PLLs */
#define PLL_FBDIV_MASK		(0x1ff)
#define PLL_REFDIV_MASK		(0x1ff)
#define PLL_VCODIV_MASK		(0x7)
#define PLL_KVCO_MASK		(0xf)

#define PLLD_FBDIV_SHIFT	0
#define PLLD_REFDIV_SHIFT	16
#define PLLD_VCODIV_SHIFT	9
#define PLLD_KVCO_SHIFT		12
#define PLLD_CKOUT_GATING_EN	31

#define APMU_PLL_FBDIV_SHIFT	0
#define APMU_PLL_REFDIV_SHIFT	12
#define APMU_PLL_VCODIV_SHIFT	28
#define APMU_PLL_KVCO_SHIFT	24
#define APMU_PLL_FC_GO		31

#define CLK_32K		"clk_32k"
#define CLK_VCXO	"vcxo"
#define USB_PLL		"usb_pll"
#define CLK_32K_RATE	32768UL
#define CLK_VCXO_RATE	26000000UL
#define USB_PLL_RATE	480000000UL

#define MPMU_APPS_PLL1	"mpmu_apps_pll1"
#define MPMU_APPS_PLL2	"mpmu_apps_pll2"
#define MPMU_SRD_PLL1	"mpmu_srd_pll1"
#define MPMU_SRD_PLL2	"mpmu_srd_pll2"
#define MPMU_AUD_PLL1	"mpmu_aud_pll1"
#define PLL1_RATE	2496000000UL
#define PLL2_RATE	800000000UL

#define PLL1_416_APPS	"pll1_416_apps"
#define PLL1_500_APPS	"pll1_500_apps"
#define PLL1_624_APPS	"pll1_624_apps"
#define PLL2_800_APPS	"pll2_800_apps"
#define PLL1_416_SRD	"pll1_416_srd"
#define PLL1_500_SRD	"pll1_500_srd"
#define PLL1_624_SRD	"pll1_624_srd"
#define PLL2_800_SRD	"pll2_800_srd"
#define PLL1_416_AUD	"pll1_416_aud"
#define PLL1_500_AUD	"pll1_500_aud"
#define PLL1_624_AUD	"pll1_624_aud"
#ifdef CONFIG_SOUND
#define PLLA_451_AUD	"plla_451_aud"
#define PLLA_56_AUD	"mmp-audio"
#define SSPA1_MN_DIV	"sspa1_mn_div"
#endif
#define PLL1_416_RATE	416000000UL
#define PLL1_500_RATE	500000000UL
#define PLL1_624_RATE	624000000UL

#define CORE_PLL1	"core_pll1"
#define CORE_PLL2	"core_pll2"
#define PLL3		"pll3"
#define PLL4		"pll4"
#define DDR_PLL		"ddr_pll"

struct reg_desc {
	u32		offset;
	void __iomem	*addr;
	u32		value;
};

/*
 * helper functions
 */
void __iomem *get_base_va(u32 reg_unit);

/*
 * definition/structure/functions used by fixed rate clock source.
 */
struct clk_fixed_src_params {
	void __iomem	*ctrl_reg;
	u32		ctrl_reg_offset;
	u32		request_bit;
	u32		req_mode_mask_bits;
	u32		req_mode_set_bits;

	void __iomem	*status_reg;
	u32		status_reg_offset;
	u32		on_bit;
	u32		locked_bit;
};

struct clk_fixed_src {
	struct clk_hw			hw;
	unsigned long			fixed_rate;
	unsigned long			flags;
	spinlock_t			*lock;
	struct clk_fixed_src_params	*params;
};

struct clk *clk_register_fixed_src(const char *name, const char *parent_name,
	unsigned long flags, unsigned long mmp_flags, unsigned long rate,
	spinlock_t *lock, struct clk_fixed_src_params *params);

/*
 * definition/structure/functions used by configurable clock source.
 */
struct clk_config_src_params {
	void __iomem	*cfg_reg1;
	u32		cfg_reg1_offset;
	u32		fbdiv_mask_bits;
	u32		fbdiv_shift;
	u32		refdiv_mask_bits;
	u32		refdiv_shift;
	u32		vcodiv_mask_bits;
	u32		vcodiv_shift;
	u32		kvco_mask_bits;
	u32		kvco_shift;
	u32		clear_bits;	/* disable unused feature */

	struct reg_desc	*cfg_regs;	/* additional config regs */
	u32		nr_cfg_regs;

	void __iomem	*ctrl_reg;
	u32		ctrl_reg_offset;
	u32		request_bit;
	u32		req_mode_mask_bits;
	u32		req_mode_set_bits;

	void __iomem	*status_reg;
	u32		status_reg_offset;
	u32		on_bit;
	u32		locked_bit;
};

struct clk_config_src {
	struct clk_hw			hw;
	unsigned long			flags;
	spinlock_t			*lock;
	struct clk_config_src_params	*params;
};

struct clk *clk_register_config_src(const char *name, unsigned long flags,
	unsigned long mmp_flags, spinlock_t *lock,
	struct clk_config_src_params *params);

/*
 * definition/structure/functions used by clock for all kinds of peripherals.
 */
#define ACCU_SRC_SHIFT		16
#define ACCU_RATIO_SHIFT	20

#define MCCU_I2C_FNC_CKEN	BIT(8)
#define MCCU_I2C_BUS_CKEN	BIT(9)

#define LCD_SCLK_DIV		(0x1a8)	/* LCD_SCLK_DIV: 0xD420B1A8 */
#define MMP_DISP_DIV_ONLY	BIT(0)
#define MMP_DISP_MUX_ONLY	BIT(1)
#define MMP_DISP_BUS		BIT(2)

/* ACCU Display Unit Configuration Register */
#define ACCU_HDMI_CEC_CKEN		BIT(8)
#define ACCU_LCD_TX_ESC_CKEN		BIT(12)
#define ACCU_LCD_PHY_CAL_CKEN		BIT(13)
#define ACCU_DISP_BKL_CKEN		BIT(14)

#define ACCU_SDH_CKEN(x)		BIT(7 + x)
#define ACCU_SDH_INT_CKEN		(12)
#define ACCU_SDH_MASTER_BUS_CKEN	BIT(13)
#define ACCU_SDH_SLAVE_BUS_CKEN		BIT(14)
#define ACCU_SDH_CARD_INS_CKEN		(15)

#define ACCU_TEMP_ANALOG_CKEN		BIT(10)
#define ACCU_TEMP_CTL32K_CKEN		BIT(11)

struct mmp_clk_disp {
	struct clk_hw		hw;
	struct clk_mux		mux;
	struct clk_divider	divider;

	spinlock_t	*lock;
	void __iomem	*apmu_base;
	u32		reg_mux;
	u32		reg_div;
	u32		reg_div_shadow;
	u32		reg_mux_shadow;

	u32		reg_rst;
	u32		reg_rst_shadow;
	u32		reg_rst_mask;

	const char	**dependence;
	int		num_dependence;
	const struct clk_ops	*mux_ops;
	const struct clk_ops	*div_ops;
};

struct clk_parent {
	struct clk	*parent;
	char		*parent_name;
	u32		src;	/* defined by accu_clk_option */
};

struct clk_peri_pp {
	unsigned long	clk_rate;	/* clk rate */
	char		*parent_name;	/* clk parent name*/
	struct clk	*parent;	/* clk parent */
	u32		src;		/* SOURCE_BITS in ctrl register */
	u32		div;		/* RATIO_BITS in ctrl register */

	struct		list_head node;
};

struct clk_peri_params {
	struct clk_peri_pp	*pp_table;
	u32			nr_pp;
	struct clk_parent	*parents;
	u32			nr_parents;

	struct clk		**depend_clks;
	char			**depend_clks_name;
	u32			nr_depend_clks;

	void __iomem		*ctrl_reg;
	u32			ctrl_reg_offset;
	u32			enable_value;
	void __iomem		*ctrl_reg2;
	u32			ctrl_reg2_offset;
	u32			src_mask_bits;
	u32			src_shift;
	u32			div_mask_bits;
	u32			div_shift;
	u32			fixed_div_shift;
	u32			fc_go_bit;
	u32			fc_done_bit;
};

struct clk_peri {
	struct clk_hw		hw;
	u32			flags;
	spinlock_t		*lock;
	struct clk_peri_params	*params;
	/* list used represent all supported pp */
	struct list_head	pp_list;
};

#ifdef CONFIG_SOUND
#define PLLA_POSTDIV_AUDIO_MASK	(0xfff << 20)
#define PLLA_POSTDIV_AUDIO(x)	((x) << 20)
#define AUD_AXI_EN	(0x1 << 4)
#define AUD_PWR_ON	(0x1 << 3)

struct clk_audio_params {
	void __iomem	*ctrl_reg;
	u32	ctrl_offset;
	u32	postdiv_mask;
	u32	postdiv;
	u32	fb_int_mask;
	u32	fb_int;
	u32	fb_dec_mask;
	u32	fb_dec;

	void __iomem	*power_reg;
	u32	power_offset;
	u32	axi_on_bit;
	u32	power_on_bit;
};

struct clk_factor_masks {
	unsigned int	factor;
	unsigned int	num_mask;
	unsigned int	den_mask;
	unsigned int	num_shift;
	unsigned int	den_shift;
};

struct clk_factor_tbl {
	unsigned int num;
	unsigned int den;
};

extern struct clk *clk_register_audio_pll(const char *name,
		const char *parent_name, unsigned long flags,
		unsigned long mmp_flags, unsigned long rate,
		spinlock_t *lock, struct clk_audio_params *params);

extern struct clk *mmp_clk_register_factor(const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *base, struct clk_factor_masks *masks,
		struct clk_factor_tbl *ftbl, unsigned int ftbl_cnt);
#endif

struct clk *clk_register_peri(const char *name, const char **parent_name,
	u8 num_parents, unsigned long flags, unsigned long mmp_flags,
	spinlock_t *lock, struct clk_peri_params *params);
extern struct clk *mmp_clk_register_disp(const char *name,
		const char **parent_name, int num_parents,
		u32 disp_flags, unsigned long flags,
		void __iomem *accu_base, struct mmp_clk_disp *disp);
void __init pxa1986_disp_clk_init(void);

#endif
