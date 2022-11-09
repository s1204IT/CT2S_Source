/*
 * mmp map Audio clock operation source file
 *
 * Copyright (C) 2014 Marvell
 * Zhao Ye <zhaoy@marvell.com>
 * Nenghua Cao <nhcao@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/mmp_dspaux.h>

#include "clk.h"

#define to_clk_audio(clk) (container_of(clk, struct clk_audio, hw))

#define AUD_NO_RESET_CTRL (1)

static DEFINE_SPINLOCK(clk_lock);

struct popular_reference_clock_freq refclock_map[] = {
/* refclk refdiv update fbdiv  out      in   bit_0_14 bit_15 bit_hex *
 * -----  ----  -----   --    ------  --------   ---   ---   ---    */
{ 11289600, 2, 5644800, 24, 135475200, 135475200,    0, 0,    0x0 },
{ 11289600, 2, 5644800, 26, 146764800, 147456000, 2469, 0, 0x09A5 },
{ 12288000, 2, 6144000, 22, 135168000, 135475200, 1192, 0, 0x04A8 },
{ 12288000, 2, 6144000, 24, 147456000, 147456000,    0, 1,    0x0 },
{ 13000000, 3, 4333333, 31, 134333333, 135475200, 4457, 0, 0x1169 },
{ 13000000, 3, 4333333, 34, 147333333, 147456000,  437, 0, 0x01B5 },
{ 16934400, 3, 5644800, 24, 135475200, 135475200,    0, 0,    0x0 },
{ 16934400, 3, 5644800, 26, 146764800, 147456000, 2469, 0, 0x09A5 },
{ 18432000, 3, 6144000, 22, 135168000, 135475200, 1192, 0, 0x04A8 },
{ 18432000, 3, 6144000, 24, 147456000, 147456000,    0, 0,    0x0 },
{ 22579200, 4, 5644800, 24, 135475200, 135475200,    0, 0,    0x0 },
{ 22579200, 4, 5644800, 26, 146764800, 147456000, 2469, 0, 0x09A5 },
{ 24576000, 4, 6144000, 22, 135168000, 135475200, 1192, 0, 0x04A8 },
{ 24576000, 4, 6144000, 24, 147456000, 147456000,    0, 0,    0x0 },
{ 26000000, 6, 4333333, 31, 134333333, 135475200, 4457, 0, 0x1169 },
{ 26000000, 6, 4333333, 34, 147333333, 147456000,  437, 0, 0x01B5 },
{ 38400000, 6, 6400000, 21, 134400000, 135475200, 4194, 0, 0x1062 },
{ 38400000, 6, 6400000, 23, 147200000, 147456000,  912, 0, 0x0390 },
};

struct post_divider_phase_interpolator pdiv_map_8k[] = {
/* audio  over  clkout  prediv  postdiv *
 * -----  ----  -----   -----   ------- */
{ 32000, 128,  4096000,  147456000,  36 },
{ 32000, 192,  6144000,  147456000,  24 },
{ 32000, 256,  8192000,  147456000,  18 },
{ 32000, 384, 12288000,  147456000,  12 },
{ 32000, 512, 16384000,  147456000,   9 },
{ 48000, 128,  6144000,  147456000,  24 },
{ 48000, 192,  9216000,  147456000,  16 },
{ 48000, 256, 12288000,  147456000,  12 },
{ 48000, 384, 18432000,  147456000,   8 },
{ 48000, 512, 24567000,  147456000,   6 },
{ 96000, 128, 12288000,  147456000,  12 },
{ 96000, 192, 18432000,  147456000,   8 },
{ 96000, 256, 24567000,  147456000,   6 },
{ 96000, 384, 36864000,  147456000,   4 },
{ 96000, 512, 49152000,  147456000,   3 },
};

struct post_divider_phase_interpolator pdiv_map_11k[] = {
/* audio  over  clkout  prediv  postdiv *
 * -----  ----  -----   -----   ------- */
{ 11025, 128,  1411200,  135475200,  96 },
{ 11025, 192,  2116800,  135475200,  64 },
{ 11025, 256,  2822400,  135475200,  48 },
{ 11025, 384,  4233600,  135475200,  32 },
{ 11025, 512,  5644800,  135475200,  24 },
{ 22050, 128,  2822400,  135475200,  48 },
{ 22050, 192,  4233600,  135475200,  32 },
{ 22050, 256,  5644800,  135475200,  24 },
{ 22050, 384,  8467200,  135475200,  16 },
{ 22050, 512, 11289600,  135475200,  12 },
{ 44100, 128,  5644800,  135475200,  24 },
{ 44100, 192,  8467200,  135475200,  16 },
{ 44100, 256, 11289600,  135475200,  12 },
{ 44100, 384, 16934400,  135475200,   8 },
{ 44100, 512, 22579200,  135475200,   6 },
{ 88200, 128, 11289600,  135475200,  12 },
{ 88200, 192, 16934400,  135475200,   8 },
{ 88200, 256, 22579200,  135475200,   6 },
{ 88200, 384, 33868800,  135475200,   4 },
{ 88200, 512, 45158400,  135475200,   3 },
};

struct fvco_clk_with_32kref {
	uint32_t fbdiv_int;
	uint32_t fbdiv_dec;
	uint32_t fvco;
};

/* audio pll1 clock table */
struct fvco_clk_with_32kref fvco_clock_with_32kref_map[] = {
	/* fbdiv_int	fbdiv_dec	fvco*
	 * --------	-----		------*/
	{137,	13,		451584000},
	{165,	6,		541900800},
	{180,	0,		589824000},
};

struct post_divider_phase_interpolator pdiv_for_8k_with_32kref[] = {
	/* audio  over  clkout  prediv  postdiv *
	 * -----  ----  -----   -----   ------- */
	{ 8000,  32,  256000,  589824000,  2304},
	{ 8000,  64,  512000,  589824000,  1152},
	{ 8000, 128, 1024000,  589824000,  576},
	{ 32000, 32,  1024000, 589824000,  576},
	{ 32000, 64,  2048000, 589824000,  288},
	{ 32000, 128, 4096000, 589824000,  144},
	{ 48000, 32,  1536000, 589824000,  384},
	{ 48000, 64,  3072000, 589824000,  192},
	{ 48000, 128, 6144000, 589824000,  96},
};

struct map_clk_apll1_table {
	unsigned long input_rate;
	unsigned long output_rate;
	uint32_t fbdiv_int;
	uint32_t fbdiv_dec;
	uint32_t fvco;
	unsigned long vco_div;
};

static int map_32k_apll_enable(void *dspaux_base, u32 srate)
{
	void __iomem *reg_addr;
	u32 refdiv, post_div, vco_div, fbdec_div, fbdec_int, vco_en, div_en;
	u32 ICP, ICP_DLL, CTUNE, TEST_MON, KVCO, FD_SEL, VDDL;
	u32 val, time_out = 2000;
	unsigned long fvco;
	struct clk *clk;

	if (dspaux_base == NULL) {
		pr_err("wrong audio aux base\n");
		return -EINVAL;
	}

	/* below value are fixed */
	KVCO = 1;
	ICP = 2;
	FD_SEL = 1;
	CTUNE = 1;
	ICP_DLL = 1;
	VDDL = 1;
	TEST_MON = 0;
	/* 32K crysto input */
	refdiv = 1;
	vco_en = 1;
	div_en = 1;

	if ((srate % 8000) == 0) {
		/* 8k famliy */
		fbdec_div = 0;
		fbdec_int = 0xb4;
		/* over-sample rate = 192 */
		post_div = 0x18;
		vco_div = 1;
		fvco = 589824000 / vco_div;
	} else if ((srate % 11025) == 0) {
		/* 8k famliy */
		fbdec_div = 6;
		fbdec_int = 0xa5;
		/* over-sample rate = 192 */
		post_div = 0x18;
		vco_div = 1;
		fvco = 541900800 / vco_div;
	} else {
		pr_err("error: no pll setting for such clock!\n");
		return -EINVAL;
	}

	/*
	 * 1: Assert reset for the APLL and PU: apll1_config1
	 */
	reg_addr = dspaux_base + DSP_AUDIO_PLL1_CONF_1;
	val = readl_relaxed(reg_addr);
	/* set power up, and also set reset */
	val |= 3;
	writel_relaxed(val, reg_addr);
	val = readl_relaxed(reg_addr);

	/*
	 * 2: set ICP, REV_DIV, FBDIV_INT, FBDIV_DEC, ICP_PLL, KVCO
	 */
	reg_addr = dspaux_base + DSP_AUDIO_PLL1_CONF_1;
	val = readl_relaxed(reg_addr);
	/* clear bits: [31-2] */
	val &= 3;
	val |=
	    ((KVCO << 31) | (ICP << 27) | (fbdec_div << 23) | (fbdec_int << 15)
	     | (refdiv << 6) | (FD_SEL << 4) | (CTUNE << 2));
	writel_relaxed(val, reg_addr);
	val = readl_relaxed(reg_addr);

	/*
	 * 3: Set the required POSTDIV_AUDIO value
	 * POSTDIV_AUDIO = 0x93(147) for 48KHz, over-sample rate 64
	 */
	/* 3.1: config apll1 fast clock: VCO_DIV = 1 */
	reg_addr = dspaux_base + DSP_AUDIO_PLL1_CONF_4;
	val = readl_relaxed(reg_addr);
	val &= ~(0xfff);
	val |= vco_div;
	writel_relaxed(val, reg_addr);
	val = readl_relaxed(reg_addr);

	/* 3.2: config apll1 slow clock */
	reg_addr = dspaux_base + DSP_AUDIO_PLL1_CONF_2;
	val = readl_relaxed(reg_addr);
	val &= (0xf << 28);
	val |=
	    ((TEST_MON << 24) | (vco_en << 23) | (post_div << 11) |
	     (div_en << 10) | (ICP_DLL << 5) | (0x1 << 4) | VDDL);
	writel_relaxed(val, reg_addr);
	val = readl_relaxed(reg_addr);
	/*
	 * 4: de-assert reset
	 */
	reg_addr = dspaux_base + DSP_AUDIO_PLL1_CONF_1;
	val = readl_relaxed(reg_addr);
	/* release reset */
	val &= ~(0x1 << 1);
	writel_relaxed(val, reg_addr);
	val = readl_relaxed(reg_addr);

	/*
	 * 5: check DLL lock status
	 */
	reg_addr = dspaux_base + DSP_AUDIO_PLL1_CONF_2;
	val = readl_relaxed(reg_addr);
	while ((!(val & (0x1 << 28))) && time_out) {
		udelay(10);
		val = readl_relaxed(reg_addr);
		time_out--;
	}
	if (time_out == 0) {
		pr_err("32K-PLL: DLL lock fail!\n");
		return -EBUSY;
	}

	/*
	 * 6: check PLL lock status
	 */
	time_out = 2000;
	while ((!(val & (0x1 << 29))) && time_out) {
		udelay(10);
		val = readl_relaxed(reg_addr);
		time_out--;
	}

	if (time_out == 0) {
		pr_err("32K-PLL: PLL lock fail!\n");
		return -EBUSY;
	}

	clk = clk_get(NULL, "apll1_fast");
	if (IS_ERR(clk)) {
		pr_err("apll1_fast may not be ready\n");
		return 0;
	} else {
		/*
		 * clock framework may remember the old value and not really
		 * write register. forcely change it to ensure actual write
		 */
		clk_set_rate(clk, fvco);
		clk_set_rate(clk, fvco / 4);
	}

	return 0;
}

static int map_32k_apll_disable(void *dspaux_base)
{
	void __iomem *reg_addr;
	u32 val;

	reg_addr = dspaux_base + DSP_AUDIO_PLL1_CONF_1;
	val = readl_relaxed(reg_addr);
	/* reset & power off */
	val &= ~0x1;
	val |= (0x1 << 1);
	writel_relaxed(val, reg_addr);

	return 0;
}

static int map_26m_apll_enable(void *dspaux_base, u32 srate)
{
	void __iomem *reg_addr;
	u32 refdiv, post_div, vco_div, fbdiv, freq_off;
	u32 vco_en, vco_div_en, post_div_en, val;
	u32 ICP, CTUNE, TEST_MON, FD_SEL, CLK_DET_EN, INTPI, PI_EN;
	u32 time_out = 2000;
	unsigned long fvco;
	struct clk *clk;

	if (dspaux_base == NULL) {
		pr_err("wrong audio aux base\n");
		return -EINVAL;
	}

	/* below value are fixed */
	ICP = 6;
	FD_SEL = 1;
	CTUNE = 1;
	TEST_MON = 0;
	INTPI = 2;
	CLK_DET_EN = 1;
	PI_EN = 1;
	/* 26M clock input */
	refdiv = 6;
	vco_en = 1;
	vco_div_en = 1;
	post_div_en = 1;

	if ((srate % 8000) == 0) {
		/* 8k famliy */
		fbdiv = 34;
		freq_off = 0x1b5;
		/* over-sample rate = 192 */
		post_div = 0x6;
		vco_div = 1;
		fvco = 589824000 / vco_div;
	} else if ((srate % 11025) == 0) {
		/* 8k famliy */
		fbdiv = 31;
		freq_off = 0x1169;
		/* over-sample rate = 192 */
		post_div = 0x6;
		vco_div = 1;
		fvco = 541900800 / vco_div;
	} else
		return -EINVAL;


	/*
	 * 1: power up and reset pll
	 */
	reg_addr = dspaux_base + DSP_AUDIO_PLL2_CONF_1;
	val = readl_relaxed(reg_addr);
	/* set power up, and also set reset */
	val |= 3;
	writel_relaxed(val, reg_addr);
	val = readl_relaxed(reg_addr);

	/*
	 * 2: set ICP, REV_DIV, FBDIV_IN, FBDIV_DEC, ICP_PLL, KVCO
	 */
	reg_addr = dspaux_base + DSP_AUDIO_PLL2_CONF_1;
	val = readl_relaxed(reg_addr);
	val &= 3;
	val |=
	    ((ICP << 27) | (fbdiv << 18) | (refdiv << 9) | (CLK_DET_EN << 8) |
	     (INTPI << 6) | (FD_SEL << 4) | (CTUNE << 2));
	writel_relaxed(val, reg_addr);
	val = readl_relaxed(reg_addr);

	/*
	 * 3: enable clk_vco
	 */
	reg_addr = dspaux_base + DSP_AUDIO_PLL2_CONF_3;
	val = readl_relaxed(reg_addr);
	val &= ~(0x7ff << 14);
	val |=
	    ((vco_div_en << 24) | (vco_div << 15) | (vco_en << 14) |
	     (TEST_MON << 0));
	writel_relaxed(val, reg_addr);
	val = readl_relaxed(reg_addr);

	/*
	 * 4: enable clk_audio
	 */
	reg_addr = dspaux_base + DSP_AUDIO_PLL2_CONF_2;
	val = readl_relaxed(reg_addr);
	val &= ~((0x7fffff << 4) | 0xf);
	val |=
	    ((post_div << 20) | (freq_off << 4) | (post_div_en << 0x1) |
	     (PI_EN << 0));
	writel_relaxed(val, reg_addr);
	val = readl_relaxed(reg_addr);

	/*
	 * 5: de-assert reset
	 */
	reg_addr = dspaux_base + DSP_AUDIO_PLL2_CONF_1;
	val = readl_relaxed(reg_addr);
	/* release reset */
	val &= ~(0x1 << 1);
	writel_relaxed(val, reg_addr);
	val = readl_relaxed(reg_addr);

	/*
	 * 6: apply freq_offset_valid: wait 50us according to DE
	 */
	udelay(50);
	reg_addr = dspaux_base + DSP_AUDIO_PLL2_CONF_2;
	val = readl_relaxed(reg_addr);
	val |= (1 << 2);
	writel_relaxed(val, reg_addr);
	val = readl_relaxed(reg_addr);

	/*
	 * 7: check PLL lock status
	 */
	reg_addr = dspaux_base + DSP_AUDIO_PLL2_CONF_1;
	val = readl_relaxed(reg_addr);
	while (!(val & (0x1 << 31)) && time_out) {
		udelay(10);
		val = readl_relaxed(reg_addr);
		time_out--;
	}
	if (time_out == 0) {
		pr_err("26M-PLL: PLL lock fail!\n");
		return -EBUSY;
	}

	clk = clk_get(NULL, "apll2_fast");
	if (IS_ERR(clk)) {
		pr_err("apll2_fast may not be ready\n");
		return 0;
	} else {
		/*
		 * clock framework may remember the old value and not really
		 * write register. forcely change it to ensure actual write
		 */
		clk_set_rate(clk, fvco);
		clk_set_rate(clk, fvco / 4);
	}

	return 0;
}

static int map_26m_apll_disable(void *dspaux_base)
{
	void __iomem *reg_addr;
	u32 val;

	reg_addr = dspaux_base + DSP_AUDIO_PLL2_CONF_1;
	val = readl_relaxed(reg_addr);
	/* reset & power off */
	val &= ~0x1;
	val |= (0x1 << 1);
	writel_relaxed(val, reg_addr);

	return 0;
}

static int clk_apll_enable(struct clk_hw *hw)
{
	struct clk_audio *audio = to_clk_audio(hw);

	/* enable audio power domain */
	audio->poweron(audio->apmu_base, audio->puclk, 1);

	/* enable 32K-apll */
	audio->apll_enable(audio->aud_aux_base, 48000);

	return 0;
}

static void clk_apll_disable(struct clk_hw *hw)
{
	struct clk_audio *audio = to_clk_audio(hw);

	/* enable 32K-apll */
	audio->apll_disable(audio->aud_aux_base);

	/* if audio pll is closed, we can close the audio island */
	audio->poweron(audio->apmu_base, audio->puclk, 0);
}

static long __map_apll1_get_rate_table(struct clk_hw *hw,
		struct map_clk_apll1_table *freq_tbl,
		unsigned long drate, unsigned long prate)
{
	int i;
	struct fvco_clk_with_32kref *ref_p;

	for (i = 0; i < ARRAY_SIZE(fvco_clock_with_32kref_map); i++) {

		/* find audio pll setting */
		ref_p = &fvco_clock_with_32kref_map[i];

		if ((ref_p->fvco % drate) == 0)
			goto found;
	}

	return -EINVAL;
found:
	freq_tbl->input_rate = prate;
	freq_tbl->fbdiv_int = ref_p->fbdiv_int;
	freq_tbl->fbdiv_dec = ref_p->fbdiv_dec;
	freq_tbl->output_rate = drate;
	freq_tbl->vco_div = ref_p->fvco / drate;

	return drate;
}

static long clk_apll1_round_rate(struct clk_hw *hw, unsigned long drate,
		unsigned long *prate)
{
	struct map_clk_apll1_table freq_tbl;

	memset(&freq_tbl, 0, sizeof(freq_tbl));
	if (__map_apll1_get_rate_table(hw, &freq_tbl, drate, *prate) < 0)
		return -EINVAL;

	*prate = freq_tbl.input_rate;

	return freq_tbl.output_rate;
}

static unsigned long clk_apll1_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return hw->clk->rate;
}

/* Configures new clock rate */
static int clk_apll1_set_rate(struct clk_hw *hw, unsigned long drate,
				unsigned long prate)
{
	struct clk_audio *audio = to_clk_audio(hw);
	u32 val, vco_div;
	struct map_clk_apll1_table freq_tbl;
	unsigned long flags;

	memset(&freq_tbl, 0, sizeof(freq_tbl));

	spin_lock_irqsave(audio->lock, flags);

	if (__map_apll1_get_rate_table(hw, &freq_tbl, drate, prate) < 0) {
		spin_unlock_irqrestore(audio->lock, flags);
		return -EINVAL;
	}
	vco_div = freq_tbl.vco_div;

	val = readl_relaxed(audio->aud_aux_base + DSP_AUDIO_PLL1_CONF_4);
	if ((val & 0xfff) != vco_div) {
		val &= ~(0xfff);
		val |= vco_div;
		writel_relaxed(val,
			       audio->aud_aux_base + DSP_AUDIO_PLL1_CONF_4);
	}

	hw->clk->rate = drate;

	spin_unlock_irqrestore(audio->lock, flags);

	return 0;
}

struct clk_ops clk_apll1_ops = {
	.prepare = clk_apll_enable,
	.unprepare = clk_apll_disable,
	.recalc_rate = clk_apll1_recalc_rate,
	.round_rate = clk_apll1_round_rate,
	.set_rate = clk_apll1_set_rate,
};

struct clk *mmp_clk_register_apll1(const char *name, const char *parent_name,
				   void __iomem *apmu_base,
				   void __iomem *aud_base,
				   void __iomem *aud_aux_base,
				   struct clk	*puclk,
				   poweron_cb poweron, spinlock_t *lock)
{
	struct clk_audio *audio;
	struct clk *clk;
	struct clk_init_data init;

	audio = kzalloc(sizeof(*audio), GFP_KERNEL);
	if (!audio)
		return NULL;

	init.name = name;
	init.ops = &clk_apll1_ops;
	init.flags = 0;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	audio->apmu_base = apmu_base;
	audio->aud_base = aud_base;
	audio->aud_aux_base = aud_aux_base;
	audio->poweron = poweron;
	audio->apll_enable = map_32k_apll_enable;
	audio->apll_disable = map_32k_apll_disable;
	audio->lock = lock;
	audio->hw.init = &init;
	audio->puclk = puclk;

	clk = clk_register(NULL, &audio->hw);

	if (IS_ERR(clk))
		kfree(audio);

	return clk;
}

static long __map_apll2_get_rate_table(struct clk_hw *hw,
		struct map_clk_audio_pll_table *freq_tbl,
			unsigned long drate, unsigned long prate)
{
	int i;
	struct popular_reference_clock_freq *ref_p;
	unsigned long fvco;

	for (i = 0; i < ARRAY_SIZE(refclock_map); i++) {
		/* find audio pll setting */
		ref_p = &refclock_map[i];
		if (ref_p->refclk != prate)
			continue;

		/* max fvco clock is 4 * freq_intp_in */
		fvco = ref_p->freq_intp_in * 4;
		if ((fvco % drate) == 0)
			goto found;
	}

	return -EINVAL;
found:
	freq_tbl->input_rate = prate;
	freq_tbl->fbdiv = ref_p->fbdiv;
	freq_tbl->refdiv = ref_p->refdiv;
	freq_tbl->freq_offset = ref_p->freq_offset_0_14_hex;
	freq_tbl->output_rate = drate;
	freq_tbl->vco_div = (fvco / drate);

	return drate;
}

static long clk_apll2_round_rate(struct clk_hw *hw, unsigned long drate,
		unsigned long *prate)
{
	struct map_clk_audio_pll_table freq_tbl;

	memset(&freq_tbl, 0, sizeof(freq_tbl));
	if (__map_apll2_get_rate_table(hw, &freq_tbl, drate, *prate) < 0)
		return -EINVAL;

	*prate = freq_tbl.input_rate;

	return freq_tbl.output_rate;
}

static unsigned long clk_apll2_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return hw->clk->rate;
}

/* Configures new clock rate */
static int clk_apll2_set_rate(struct clk_hw *hw, unsigned long drate,
				unsigned long prate)
{
	struct clk_audio *audio = to_clk_audio(hw);
	unsigned int val;
	struct map_clk_audio_pll_table freq_tbl;
	unsigned long vco_div, flags;

	memset(&freq_tbl, 0, sizeof(freq_tbl));
	spin_lock_irqsave(audio->lock, flags);

	if (__map_apll2_get_rate_table(hw, &freq_tbl, drate, prate) < 0) {
		spin_unlock_irqrestore(audio->lock, flags);
		return -EINVAL;
	}

	vco_div = freq_tbl.vco_div;

	val = readl_relaxed(audio->aud_aux_base + DSP_AUDIO_PLL2_CONF_3);
	if (((val >> 15) & 0x1FF) != vco_div) {
		val &= ~(0x1FF << 15);
		val |= (vco_div << 15);
		writel_relaxed(val,
			       audio->aud_aux_base + DSP_AUDIO_PLL2_CONF_3);
	}

	hw->clk->rate = drate;
	spin_unlock_irqrestore(audio->lock, flags);

	return 0;
}

struct clk_ops clk_apll2_ops = {
	.enable = clk_apll_enable,
	.disable = clk_apll_disable,
	.recalc_rate = clk_apll2_recalc_rate,
	.round_rate = clk_apll2_round_rate,
	.set_rate = clk_apll2_set_rate,
};

struct clk *mmp_clk_register_apll2(const char *name, const char *parent_name,
				   void __iomem *apmu_base,
				   void __iomem *aud_base,
				   void __iomem *aud_aux_base,
				   struct clk	*puclk,
				   poweron_cb poweron, spinlock_t *lock)
{
	struct clk_audio *audio;
	struct clk *clk;
	struct clk_init_data init;

	audio = kzalloc(sizeof(*audio), GFP_KERNEL);
	if (!audio)
		return NULL;

	init.name = name;
	init.ops = &clk_apll2_ops;
	init.flags = 0;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	audio->apmu_base = apmu_base;
	audio->aud_base = aud_base;
	audio->aud_aux_base = aud_aux_base;
	audio->poweron = poweron;
	audio->apll_enable = map_26m_apll_enable;
	audio->apll_disable = map_26m_apll_disable;
	audio->lock = lock;
	audio->hw.init = &init;
	audio->puclk = puclk;

	clk = clk_register(NULL, &audio->hw);

	if (IS_ERR(clk))
		kfree(audio);

	return clk;
}

/* Common audio component reset/enable bit */

#define to_clk_audio_res(hw) container_of(hw, struct clk_audio_res, hw)
struct clk_audio_res {
	struct clk_hw		hw;
	void __iomem		*base;
	unsigned int		en_bit_offset;
	unsigned int		res_bit_offset;
	unsigned int		delay;
	unsigned int		flags;
	spinlock_t		*lock;
};

static int clk_audio_prepare(struct clk_hw *hw)
{
	struct clk_audio_res *audio = to_clk_audio_res(hw);
	unsigned int data;
	unsigned long flags = 0;

	if (audio->lock)
		spin_lock_irqsave(audio->lock, flags);

	data = readl_relaxed(audio->base);
	data |= (1 << audio->en_bit_offset);
	writel_relaxed(data, audio->base);

	if (audio->lock)
		spin_unlock_irqrestore(audio->lock, flags);

	udelay(audio->delay);

	if (!(audio->flags & AUD_NO_RESET_CTRL)) {
		if (audio->lock)
			spin_lock_irqsave(audio->lock, flags);

		data = readl_relaxed(audio->base);
		data |= (1 << audio->res_bit_offset);
		writel_relaxed(data, audio->base);

		if (audio->lock)
			spin_unlock_irqrestore(audio->lock, flags);
	}

	return 0;
}

static void clk_audio_unprepare(struct clk_hw *hw)
{
	struct clk_audio_res *audio = to_clk_audio_res(hw);
	unsigned long data;
	unsigned long flags = 0;

	if (audio->lock)
		spin_lock_irqsave(audio->lock, flags);

	data = readl_relaxed(audio->base);
	data &= ~(1 << audio->en_bit_offset);
	writel_relaxed(data, audio->base);

	if (audio->lock)
		spin_unlock_irqrestore(audio->lock, flags);
}

struct clk_ops clk_audio_res_ops = {
	.prepare = clk_audio_prepare,
	.unprepare = clk_audio_unprepare,
};

struct clk *mmp_clk_register_aud_res(const char *name, const char *parent_name,
		void __iomem *base, unsigned int en_bit_offset,
		unsigned int res_bit_offset, unsigned int delay,
		unsigned int audio_res_flags, spinlock_t *lock)
{
	struct clk_audio_res *audio;
	struct clk *clk;
	struct clk_init_data init;

	audio = kzalloc(sizeof(*audio), GFP_KERNEL);
	if (!audio)
		return NULL;

	init.name = name;
	init.ops = &clk_audio_res_ops;
	init.flags = CLK_SET_RATE_PARENT;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	audio->base = base;
	audio->en_bit_offset = en_bit_offset;
	audio->res_bit_offset = res_bit_offset;
	audio->delay = delay;
	audio->flags = audio_res_flags;
	audio->lock = lock;
	audio->hw.init = &init;

	clk = clk_register(NULL, &audio->hw);
	if (IS_ERR(clk))
		kfree(audio);

	return clk;
}

const char *adma_parent[] = {
	"apll1_slow", "apll2_slow",
	"vctcxo", "pll1_312_gate",
};

const char *sspa1_parent[] = {
	"apll1_slow", "apll2_slow",
	"vctcxo", "pll1_312_gate",
};

const char *sspa1_bypass[] = {
	"sspa1_mn_div", "sspa1_mux",
};

static struct clk_factor_masks sspa_factor_masks = {
	.factor = 1,
	.den_mask = 0xfff,
	.num_mask = 0x7fff,
	.den_shift = 19,
	.num_shift = 4,
};
static struct clk_factor_tbl sspa1_factor_tbl[] = {
	{.num = 1625, .den = 256},	/* 8kHz */
	{.num = 3042, .den = 1321},	/* 44.1kHZ */
};

const char *sspa2_parent[] = {
	"apll1_slow", "apll2_slow",
	"vctcxo", "pll1_312_gate",
};

const char *sspa2_bypass[] = {
	"sspa2_mn_div", "sspa2_mux",
};

static struct clk_factor_tbl sspa2_factor_tbl[] = {
	{.num = 1625, .den = 256},	/* 8kHz */
	{.num = 3042, .den = 1321},	/* 44.1kHZ */
};

static struct clk_factor_masks i2s_bclk_masks = {
	.factor = 1,
	.den_mask = 0xfff,
	.num_mask = 0xffff,
	.den_shift = 16,
	.num_shift = 0,
};

/* this table is not finalized yet */
static struct clk_factor_tbl i2s_bclk_tbl[] = {
	{.num = 288, .den = 1},	/* 8kHz for 147.456M */
	{.num = 144, .den = 1},	/* 16kHz for 147.456M */
	{.num = 23040, .den = 441},	/* 48kHz for 147.456M*/
	{.num = 12, .den = 1},	/* DEI2S: tdm frame 48K for 147.456M */
	{.num = 8125, .den = 1372},	/* 44.1kHZ for 26M */
};

const char *sram_parent[] = {
	"apll1_slow", "apll1_fast",
};

const char *apll_parent[] = {
	"apll2_fast", "apll1_fast",
};

const char *mclk_high_parent[] = {
	"map_apll_fast", "pll1_312_gate",
};

const char *bclk_parent[] = {
	"vctcxo", "map_mclk_high",
};

const char *tdm_parent[] = {
	"vctcxo", "map_apll_fast",
};

const char *digit_parent[] = {
	"vctcxo", "map_digit_div",
};

const char *dsp_parent[] = {
	"map_apb", "map_digit_clk",
};

const char *i2s1_bclk_parent[] = {
	"vctcxo", "i2s1_mn_div",
};
const char *i2s2_bclk_parent[] = {
	"vctcxo", "i2s2_mn_div",
};
const char *i2s3_bclk_parent[] = {
	"vctcxo", "i2s3_mn_div",
};
const char *i2s4_bclk_parent[] = {
	"vctcxo", "i2s4_mn_div",
};
const char *tdm_fclk_parent[] = {
	"vctcxo", "tdm_mn_div",
};

void __init audio_clk_init(void __iomem *apmu_base, void __iomem *audio_base,
		void __iomem *audio_aux_base, poweron_cb poweron, u32 apll)
{
	struct clk *clk;
	struct clk *puclk;
	u32 fvco = 589824000;

	if (apll == APLL_32K)
		/* 32k pu */
		puclk = clk_get(NULL, "32kpu");
	else
		puclk = NULL;

	/* apll1 */
	clk = mmp_clk_register_apll1("map_apll1", "clk32",
			apmu_base, audio_base, audio_aux_base,
			puclk, poweron, &clk_lock);

	clk_register_clkdev(clk, "map_apll1", NULL);
	/* enable power for audio island */
	if (apll != APLL_32K && apll != APLL_26M) {
		pr_err("wrong apll source settings 0x%x\n", apll);
		return;
	}

	if (apll == APLL_32K) {
		clk_prepare_enable(clk);
		clk_set_rate(clk, fvco);
	}

	/* apll2 */
	clk = mmp_clk_register_apll2("map_apll2", "vctcxo",
			apmu_base, audio_base, audio_aux_base,
			puclk, poweron, &clk_lock);
	clk_register_clkdev(clk, "map_apll2", NULL);
	/* enable power for audio island */
	if (apll == APLL_26M) {
		clk_prepare_enable(clk);
		clk_set_rate(clk, fvco);
	}

	clk = clk_register_divider(NULL, "apll1_vco_div", "map_apll1", 0,
			audio_aux_base + 0x74, 0, 12,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			&clk_lock);
	clk_register_clkdev(clk, "apll1_vco_div", NULL);

	clk = clk_register_gate(NULL, "apll1_fast", "apll1_vco_div",
				CLK_SET_RATE_PARENT,
				audio_aux_base + 0x6c, 23, 0, &clk_lock);
	clk_register_clkdev(clk, "apll1_fast", NULL);
	clk_set_rate(clk, fvco / 4);

	clk = clk_register_divider(NULL, "apll1_postdiv", "map_apll1", 0,
			audio_aux_base + 0x6c, 11, 12,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			&clk_lock);
	clk_register_clkdev(clk, "apll1_postdiv", NULL);

	clk = clk_register_gate(NULL, "apll1_slow", "apll1_postdiv", 0,
				audio_aux_base + 0x6c, 10, 0, &clk_lock);
	clk_register_clkdev(clk, "apll1_slow", NULL);

	/* apll1_mux: choose apll1_fast or apll1_slow */
	clk = clk_register_mux(NULL, "apll1_mux", sram_parent,
			ARRAY_SIZE(sram_parent), 0,
			audio_aux_base + 0x0, 0, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "apll1_mux", NULL);

	clk = clk_register_divider(NULL, "sram_div", "sram_mux", 0,
			audio_aux_base + 0x0, 3, 4,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			&clk_lock);
	clk_register_clkdev(clk, "sram_div", NULL);

	clk = mmp_clk_register_aud_res("mmp-sram", "sram_div",
				audio_aux_base + 0x0, 1, 2, 10,
				0, &clk_lock);
	clk_register_clkdev(clk, "mmp-sram", NULL);

	clk = clk_register_divider(NULL, "apb_div", "map_apb_mux", 0,
			audio_aux_base + 0x14, 2, 4,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			&clk_lock);
	clk_register_clkdev(clk, "apb_div", NULL);

	clk = mmp_clk_register_aud_res("map_apb", "apb_div",
				audio_aux_base + 0x14, 0, 1, 10,
				0, &clk_lock);
	clk_register_clkdev(clk, "map_apb", NULL);

	/* adma */
	clk = clk_register_mux(NULL, "adma_mux", adma_parent,
			ARRAY_SIZE(adma_parent), 0,
			audio_aux_base + 0x4, 2, 2, 0, &clk_lock);
	clk_register_clkdev(clk, "adma_mux", NULL);

	clk = clk_register_divider(NULL, "adma_div", "adma_mux", 0,
			audio_aux_base + 0x4, 4, 4,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			&clk_lock);
	clk_register_clkdev(clk, "adma_div", NULL);

	clk = mmp_clk_register_aud_res("mmp-adma", "adma_div",
				audio_aux_base + 0x4, 0, 1, 10,
				0, &clk_lock);
	clk_register_clkdev(clk, "mmp-adma", NULL);

	/* apll2 slow clock has the internal 4 divider */
	clk = clk_register_fixed_factor(NULL, "map_apll2_slow", "map_apll2",
				0, 1, 4);
	clk_register_clkdev(clk, "map_apll2_slow", NULL);

	clk = clk_register_gate(NULL, "apll2_vco_en", "map_apll2",
				CLK_SET_RATE_PARENT,
				audio_aux_base + 0x80, 14, 0, &clk_lock);
	clk_register_clkdev(clk, "apll2_vco_en", NULL);

	clk = clk_register_gate(NULL, "apll2_vco_div_en", "apll2_vco_en",
				CLK_SET_RATE_PARENT,
				audio_aux_base + 0x80, 24, 0, &clk_lock);
	clk_register_clkdev(clk, "apll2_vco_en", NULL);

	clk = clk_register_divider(NULL, "apll2_fast", "apll2_vco_div_en", 0,
			audio_aux_base + 0x80, 15, 9,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			&clk_lock);
	clk_register_clkdev(clk, "apll2_fast", NULL);
	clk_set_rate(clk, fvco / 4);

	clk = clk_register_divider(NULL, "apll2_postdiv", "map_apll2_slow", 0,
			audio_aux_base + 0x7c, 20, 7,
			CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO,
			&clk_lock);
	clk_register_clkdev(clk, "apll2_postdiv", NULL);

	clk = clk_register_gate(NULL, "apll2_slow", "apll2_postdiv", 0,
				audio_aux_base + 0x7c, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "apll2_slow", NULL);

	/* sspa1 */
	clk = clk_register_mux(NULL, "sspa1_mux", sspa1_parent,
			ARRAY_SIZE(sspa1_parent), 0,
			audio_aux_base + 0xc, 0, 2, 0, &clk_lock);
	clk_register_clkdev(clk, "sspa1_mux", NULL);

	clk =  mmp_clk_register_factor("sspa1_mn_div", "sspa1_mux",
				0,
				audio_aux_base + 0xc,
				&sspa_factor_masks, sspa1_factor_tbl,
				ARRAY_SIZE(sspa1_factor_tbl));
	clk_register_clkdev(clk, "sspa1_mn_div", NULL);

	clk = clk_register_mux(NULL, "sspa1_bypass", sspa1_bypass,
			ARRAY_SIZE(sspa1_bypass), CLK_SET_RATE_PARENT,
			audio_aux_base + 0xc, 31, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "sspa1_bypass", NULL);

	clk = mmp_clk_register_aud_res("mmp-sspa-dai.0", "sspa1_bypass",
				audio_aux_base + 0xc, 3, 2, 10,
				0, &clk_lock);
	clk_register_clkdev(clk, "mmp-sspa-dai.0", NULL);

	/* sspa2 */
	clk = clk_register_mux(NULL, "sspa2_mux", sspa2_parent,
			ARRAY_SIZE(sspa2_parent), 0,
			audio_aux_base + 0x10, 0, 2, 0, &clk_lock);
	clk_register_clkdev(clk, "sspa2_mux", NULL);

	clk =  mmp_clk_register_factor("sspa2_mn_div", "sspa2_mux",
				0,
				audio_aux_base + 0x10,
				&sspa_factor_masks, sspa2_factor_tbl,
				ARRAY_SIZE(sspa2_factor_tbl));
	clk_register_clkdev(clk, "sspa2_mn_div", NULL);

	clk = clk_register_mux(NULL, "sspa2_bypass", sspa2_bypass,
			ARRAY_SIZE(sspa2_bypass), CLK_SET_RATE_PARENT,
			audio_aux_base + 0x10, 31, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "sspa2_bypass", NULL);

	clk = mmp_clk_register_aud_res("mmp-sspa-dai.1", "sspa2_bypass",
				audio_aux_base + 0x10, 3, 2, 10,
				0, &clk_lock);
	clk_register_clkdev(clk, "mmp-sspa-dai.1", NULL);

	/*
	 * map_apll_fast: choose apll1_fast or apll2 fast, providing
	 * to MAP mclk_high, TDM clock, TDM clock
	 */
	clk = clk_register_mux(NULL, "map_apll_fast", apll_parent,
			ARRAY_SIZE(apll_parent), 0,
			audio_aux_base + 0x2c, 8, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "map_apll_fast", NULL);

	/*
	 * mclk_high_parent: choose 312M pll or one of map_apll_fast
	 */
	clk = clk_register_mux(NULL, "map_mclk_high", mclk_high_parent,
			ARRAY_SIZE(mclk_high_parent), 0,
			audio_base + 0x28, 1, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "map_mclk_high", NULL);

	/*
	 * bclk_parent: choose mclk_high_parent or vctcxo
	 */
	clk = clk_register_mux(NULL, "map_bclk", bclk_parent,
			ARRAY_SIZE(bclk_parent), 0,
			audio_base + 0x28, 2, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "map_bclk", NULL);

	/*
	 * tdm_parent: choose map_apll_fast or vctcxo
	 */
	clk = clk_register_mux(NULL, "map-tdm", tdm_parent,
			ARRAY_SIZE(tdm_parent), 0,
			audio_base + 0x28, 3, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "map-tdm", NULL);

	/*
	 * map_digit_div: divider for digital pll
	 */
	clk = clk_register_divider(NULL, "map_digit_div", "map_mclk_high",
		0, audio_aux_base + 0x28, 4, 2, 0, &clk_lock);
	clk_register_clkdev(clk, "map_digit_div", NULL);

	/*
	 * map_digit_clk: choose map_digit_div or vctcxo
	 */
	clk = clk_register_mux(NULL, "map_digit_clk", digit_parent,
			ARRAY_SIZE(digit_parent), 0,
			audio_base + 0x28, 0, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "map_digit_clk", NULL);

	/*
	 * map_dsp_clk: choose map_apb or map_digit_clk
	 */
	clk = clk_register_mux(NULL, "map_dsp_clk", dsp_parent,
			ARRAY_SIZE(dsp_parent), 0,
			audio_base + 0x28, 6, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "map_dsp_clk", NULL);

	/*
	 * bit clock for i2s 1/2/3/4: currently it's not used
	 * since we need to configure bit 28 together with mn_div
	 */
	/* i2s1 bit clock */
	clk =  mmp_clk_register_factor("i2s1_mn_div", "map_bclk",
				0, audio_base + 0x74,
				&i2s_bclk_masks, i2s_bclk_tbl,
				ARRAY_SIZE(i2s_bclk_tbl));
	clk_register_clkdev(clk, "i2s1_mn_div", NULL);

	clk = clk_register_mux(NULL, "map-i2s1-dai", i2s1_bclk_parent,
			ARRAY_SIZE(i2s1_bclk_parent), CLK_SET_RATE_PARENT,
			audio_base + 0x74, 28, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "map-i2s1-dai", NULL);

	/* i2s2 bit clock */
	clk =  mmp_clk_register_factor("i2s2_mn_div", "map_bclk",
				0, audio_base + 0x80,
				&i2s_bclk_masks, i2s_bclk_tbl,
				ARRAY_SIZE(i2s_bclk_tbl));
	clk_register_clkdev(clk, "i2s2_mn_div", NULL);

	clk = clk_register_mux(NULL, "map-i2s2-dai", i2s2_bclk_parent,
			ARRAY_SIZE(i2s2_bclk_parent), CLK_SET_RATE_PARENT,
			audio_base + 0x80, 28, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "map-i2s2-dai", NULL);

	/* i2s3 bit clock */
	clk =  mmp_clk_register_factor("i2s3_mn_div", "map_bclk",
				0, audio_base + 0x78,
				&i2s_bclk_masks, i2s_bclk_tbl,
				ARRAY_SIZE(i2s_bclk_tbl));
	clk_register_clkdev(clk, "i2s3_mn_div", NULL);

	clk = clk_register_mux(NULL, "map-i2s3-dai", i2s3_bclk_parent,
			ARRAY_SIZE(i2s3_bclk_parent), CLK_SET_RATE_PARENT,
			audio_base + 0x78, 28, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "map-i2s3-dai", NULL);

	/* i2s4 bit clock */
	clk =  mmp_clk_register_factor("i2s4_mn_div", "map_bclk",
				0, audio_base + 0x7c,
				&i2s_bclk_masks, i2s_bclk_tbl,
				ARRAY_SIZE(i2s_bclk_tbl));
	clk_register_clkdev(clk, "i2s4_mn_div", NULL);

	clk = clk_register_mux(NULL, "map-i2s4-dai", i2s4_bclk_parent,
			ARRAY_SIZE(i2s4_bclk_parent), CLK_SET_RATE_PARENT,
			audio_base + 0x7c, 28, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "map-i2s4-dai", NULL);

	/* tdm frame clock */
	clk =  mmp_clk_register_factor("tdm_mn_div", "map_bclk",
				0, audio_base + 0x8c,
				&i2s_bclk_masks, i2s_bclk_tbl,
				ARRAY_SIZE(i2s_bclk_tbl));
	clk_register_clkdev(clk, "tdm_mn_div", NULL);

	clk = clk_register_mux(NULL, "tdm_fclk", tdm_fclk_parent,
			ARRAY_SIZE(tdm_fclk_parent), CLK_SET_RATE_PARENT,
			audio_base + 0x8c, 28, 1, 0, &clk_lock);
	clk_register_clkdev(clk, "tdm_fclk", NULL);
}
