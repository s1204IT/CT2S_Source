/*
 * mmp_dspaux: dspaux regs for mmp audio.
 *
 * Copyright (C) 2014 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MMP_DSPAUX_H__
#define __LINUX_MMP_DSPAUX_H__

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk-private.h>

/* dspaux base is defined in mach/addr-map.h */
#define DSP_AUDIO_SRAM_CLK		0
#define DSP_AUDIO_ADMA_CLK		4
#define DSP_AUDIO_INT_STATUS	8
#define DSP_AUDIO_SSPA1_CLK		0xc
#define DSP_AUDIO_SSPA2_CLK		0x10
#define DSP_AUDIO_APB_CLK		0x14
#define DSP_AUDIO_I2S_SYSCLK_1	0x18
#define DSP_AUDIO_WAKEUP_MASK	0x1c
#define DSP_AUDIO_MN_DIV_1		0x20
#define DSP_AUDIO_MN_DIV_2		0x24
#define DSP_AUDIO_I2S_SYSCLK_2	0x28
#define DSP_AUDIO_MAP_CONF		0x2c
#define DSP_AUDIO_PLL1_CONF_1	0x68
#define DSP_AUDIO_PLL1_CONF_2	0x6c
#define DSP_AUDIO_PLL1_CONF_3	0x70
#define DSP_AUDIO_PLL1_CONF_4	0x74
#define DSP_AUDIO_PLL2_CONF_1	0x78
#define DSP_AUDIO_PLL2_CONF_2	0x7c
#define DSP_AUDIO_PLL2_CONF_3	0x80

/* DSP_AUDIO_MAP_CONF: 0x2c */
#define MAP_RESET	(1 << 0)
#define AUD_SSPA1_INPUT_SRC	(1 << 1)
#define AUD_SSPA2_INPUT_SRC	(1 << 2)

#define SSPA1_PAD_OUT_SRC_MASK	(3 << 3)
#define SSPA1_DRIVE_SSPA1_PAD	(0 << 3)
#define TDM_DRIVE_SSPA1_PAD	(1 << 3)
#define DEI2S_DRIVE_SSPA1_PAD	(2 << 3)

#define SSPA2_PAD_OUT_SRC_MASK	(3 << 5)
#define SSPA2_DRIVE_SSPA2_PAD	(0 << 5)
#define DEI2S_DRIVE_SSPA2_PAD	(1 << 5)
#define I2S4_DRIVE_SSPA2_PAD	(2 << 5)

#define CP_GSSP_INPUT_FROM_I2S2	(1 << 9)

#define APMU_AUD_CLK		(0x80)
#define PMUA_AUDIO_CLK_RES_CTRL		(0x164)

#define ANAGRP_CTRL1	0x1130
#define CURR_REF_PU_IDX	0

/*
 * define audio pll source.
 * APLL_32K: 32k audio PLL
 * APLL_26M: 26m audio PLL
 * APLL_NONE: not using audio PLL
 */
enum {
	APLL_32K = 0,
	APLL_26M,
	APLL_NONE,
};

typedef void (*poweron_cb)(void __iomem*, struct clk*, int);
void audio_clk_init(void __iomem *apmu_base, void __iomem *audio_base,
					void __iomem *audio_aux_base,
					poweron_cb poweron, u32 apll);

struct map_clk_audio_pll_table {
	unsigned long input_rate;
	unsigned long output_rate;
	unsigned long output_rate_offseted;
	unsigned long freq_offset;
	unsigned long vco_div;
	u16 refdiv;
	u16 fbdiv;
	u16 icp;
	u16 postdiv;
};

struct clk_audio {
	struct clk_hw hw;
	struct clk	*puclk;
	void __iomem *apmu_base;
	void __iomem *aud_base;
	void __iomem *aud_aux_base;
	u32 rst_mask;
	spinlock_t *lock;
	struct map_clk_audio_pll_table *freq_table;
	poweron_cb poweron;
	int (*apll_enable)(void __iomem *, u32);
	int (*apll_disable)(void __iomem *);
};

struct post_divider_phase_interpolator {
	uint32_t audio_sampling_freq;
	uint32_t over_sampleing_rate;
	uint32_t clkout_audio;
	uint32_t pre_divider;
	uint32_t postdiv_audio;
};

struct popular_reference_clock_freq {
	uint32_t refclk;
	uint32_t refdiv;
	uint32_t update_rate;
	uint32_t fbdiv;
	uint32_t freq_intp_out;
	uint32_t freq_intp_in;
	uint32_t freq_offset_0_14;
	uint32_t freq_offset_15;
	uint32_t freq_offset_0_14_hex;
};

#endif /* __LINUX_MMP_DSPAUX_H__ */
