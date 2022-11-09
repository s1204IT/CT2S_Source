/*
 * Copyright (C) 2014 Marvell
 * Zhen Fu <fuzh@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * pxa1986 audio clock source implementation
 */

#include <linux/slab.h>
#include "adir-clk.h"

#define to_clk_audio(clk) (container_of(clk, struct clk_audio, clk))

struct clk_audio {
	struct clk_hw   hw;
	spinlock_t	*lock;
	struct clk_audio_params	*params;
	unsigned long			flags;
	unsigned long			rate;
};

static int clk_audio_enable(struct clk_hw *hw)
{
	u32 reg_val;
	struct clk_audio *audio = to_clk_audio(hw);
	struct clk_audio_params *params = audio->params;

	spin_lock_check(audio->lock);
	reg_val = readl_relaxed(params->power_reg);
	reg_val |= params->axi_on_bit | params->power_on_bit;
	writel_relaxed(reg_val, params->power_reg);

	spin_unlock_check(audio->lock);
	return 0;
}

static void clk_audio_disable(struct clk_hw *hw)
{
	u32 reg_val;
	struct clk_audio *audio = to_clk_audio(hw);
	struct clk_audio_params *params = audio->params;

	if (!params)
		return;

	spin_lock_check(audio->lock);
	reg_val = readl_relaxed(params->power_reg);
	reg_val &= ~(params->axi_on_bit | params->power_on_bit);
	writel_relaxed(reg_val, params->power_reg);
	spin_unlock_check(audio->lock);
}

static unsigned long clk_audio_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	return to_clk_audio(hw)->rate;
}

static void clk_audio_init(struct clk_hw *hw)
{
	u32 reg_val;
	struct clk_audio *audio = to_clk_audio(hw);
	struct clk_audio_params *params = audio->params;

	if (!params)
		return;

	spin_lock_check(audio->lock);
	reg_val = readl_relaxed(params->ctrl_reg);
	reg_val &= ~(params->postdiv_mask |
		params->fb_int_mask | params->fb_dec_mask);
	reg_val |= (params->postdiv | params->fb_int | params->fb_dec);
	writel_relaxed(reg_val, params->ctrl_reg);
	spin_unlock_check(audio->lock);
}

struct clk_ops clk_audio_ops = {
	.init = clk_audio_init,
	.enable = clk_audio_enable,
	.disable = clk_audio_disable,
	.recalc_rate = clk_audio_recalc_rate,
};

struct clk *clk_register_audio_pll(const char *name, const char *parent_name,
		unsigned long flags, unsigned long mmp_flags,
		unsigned long rate, spinlock_t *lock,
		struct clk_audio_params *params)
{
	void __iomem *base_va;
	struct clk *clk;
	struct clk_audio *audio;
	struct clk_init_data init;

	audio = kzalloc(sizeof(*audio), GFP_KERNEL);
	if (!audio)
		return NULL;

	init.name = name;
	init.ops = &clk_audio_ops;
	init.flags = flags;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	if (likely(params)) {
		base_va = get_base_va(mmp_flags &
			MMP_CLK_CTRL_REG_FROM_MPMU);
		params->ctrl_reg = base_va + params->ctrl_offset;
		params->power_reg = base_va + params->power_offset;
	}

	audio->lock = lock;
	audio->params = params;
	audio->flags = mmp_flags;
	audio->rate = rate;
	audio->hw.init = &init;

	clk = clk_register(NULL, &audio->hw);

	if (IS_ERR(clk))
		kfree(audio);

	return clk;
}
