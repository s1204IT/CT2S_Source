/*
 * Copyright (C) 2014 Marvell
 * Bill Zhou <zhoub@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * pxa1986 clock source implementation
 */

#include <linux/slab.h>
#include "adir-clk.h"

#define to_clk_fixed_src(_hw) container_of(_hw, struct clk_fixed_src, hw)

static unsigned long __fixed_src_is_enabled(struct clk_fixed_src *clk_src)
{
	u32 reg_val, on_bit, locked_bit;
	struct clk_fixed_src_params *params = clk_src->params;

	on_bit = params->on_bit;
	locked_bit = params->locked_bit;
	reg_val = readl_relaxed(params->status_reg);

	if (clk_src->flags & MMP_CLK_PLL_CHECK_LOCKED)
		return (reg_val & on_bit) && (reg_val & locked_bit);
	else
		return reg_val & on_bit;
}

static void clk_fixed_src_init(struct clk_hw *hw)
{
	u32 reg_val;
	struct clk_fixed_src *clk_src = to_clk_fixed_src(hw);
	struct clk_fixed_src_params *params = clk_src->params;

	if (!params)
		return;

	if (clk_src->flags & MMP_CLK_PLL_CHECK_REQ_MODE) {
		spin_lock_check(clk_src->lock);
		reg_val = readl_relaxed(params->ctrl_reg);
		reg_val &= params->req_mode_mask_bits;
		reg_val |= params->req_mode_set_bits;
		writel_relaxed(reg_val, params->ctrl_reg);
		spin_unlock_check(clk_src->lock);
	}
}

static int clk_fixed_src_enable(struct clk_hw *hw)
{
	struct clk_fixed_src *clk_src = to_clk_fixed_src(hw);
	struct clk_fixed_src_params *params = clk_src->params;

	if (!params)
		return 0;

	spin_lock_check(clk_src->lock);
	CLK_SET_BITS(params->request_bit, 0, params->ctrl_reg);
	spin_unlock_check(clk_src->lock);

	/* busy wait to make sure clock source is enabled */
	while (!__fixed_src_is_enabled(clk_src))
		;

	return 0;
}

static void clk_fixed_src_disable(struct clk_hw *hw)
{
	struct clk_fixed_src *clk_src = to_clk_fixed_src(hw);
	struct clk_fixed_src_params *params = clk_src->params;

	/* FIXME:
	 * keep fixed clock source always on before control all clocks
	 * under the framework to avoid unexpected shutdown.
	 */
	return;

	if (!params)
		return;

	if (unlikely(!(clk_src->flags & MMP_CLK_PLL_ALWAYS_ON))) {
		spin_lock_check(clk_src->lock);
		CLK_SET_BITS(0, params->request_bit, params->ctrl_reg);
		spin_unlock_check(clk_src->lock);
	}
}

static unsigned long clk_fixed_src_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	return to_clk_fixed_src(hw)->fixed_rate;
}

static int clk_fixed_src_is_enabled(struct clk_hw *hw)
{
	struct clk_fixed_src *clk_src = to_clk_fixed_src(hw);
	struct clk_fixed_src_params *params = clk_src->params;

	if (!params)
		return 1;

	return __fixed_src_is_enabled(to_clk_fixed_src(hw));
}

static struct clk_ops clk_fixed_src_ops = {
	.init = clk_fixed_src_init,
	.enable = clk_fixed_src_enable,
	.disable = clk_fixed_src_disable,
	.recalc_rate = clk_fixed_src_recalc_rate,
	.is_enabled = clk_fixed_src_is_enabled,
};

struct clk *clk_register_fixed_src(const char *name, const char *parent_name,
	unsigned long flags, unsigned long mmp_flags, unsigned long rate,
	spinlock_t *lock, struct clk_fixed_src_params *params)
{
	void __iomem *base_va;
	struct clk *clk;
	struct clk_fixed_src *clk_src;
	struct clk_init_data init;

	clk_src = kzalloc(sizeof(*clk_src), GFP_KERNEL);
	if (!clk_src)
		return NULL;

	init.name = name;
	init.ops = &clk_fixed_src_ops;
	init.flags = flags;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	if (likely(params)) {
		base_va = get_base_va(mmp_flags & (
			MMP_CLK_CTRL_REG_FROM_ACCU |
			MMP_CLK_CTRL_REG_FROM_MCCU |
			MMP_CLK_CTRL_REG_FROM_APMU |
			MMP_CLK_CTRL_REG_FROM_MPMU));
		params->ctrl_reg = base_va + params->ctrl_reg_offset;
		params->status_reg = base_va + params->status_reg_offset;
	}

	clk_src->fixed_rate = rate;
	clk_src->flags = mmp_flags;
	clk_src->lock = lock;
	clk_src->params = params;
	clk_src->hw.init = &init;

	clk = clk_register(NULL, &clk_src->hw);
	if (IS_ERR(clk))
		kfree(clk_src);

	return clk;
}

#define to_clk_config_src(_hw) container_of(_hw, struct clk_config_src, hw)

/* pll fvco range 1.2G~3.0G */
static u32 __pll_fvco2kvco(unsigned long fvco)
{
	u32 kvco = 8;

	if (fvco >= 2600 && fvco <= 3000)
		kvco = 15;
	else if (fvco >= 2400)
		kvco = 14;
	else if (fvco >= 2200)
		kvco = 13;
	else if (fvco >= 2000)
		kvco = 12;
	else if (fvco >= 1750)
		kvco = 11;
	else if (fvco >= 1500)
		kvco = 10;
	else if (fvco >= 1350)
		kvco = 9;
	else if (fvco >= 1200)
		kvco = 8;
	else
		BUG_ON("pll fvco out of range\n");

	return kvco;
}

static u32 __pll_rate2div(u32 rate, u32 *div_reg)
{
	u32 i = 0, fvco, div = 0;
	u32 div_values[] = { 1, 2, 4, 8, 16, 32, 64, 128 };

	for (i = 0; i < ARRAY_SIZE(div_values); i++) {
		fvco = rate * div_values[i];
		if (fvco >= 1200 && fvco <= 3000) {
			div = div_values[i];
			*div_reg = i;
			break;
		}
	}
	BUG_ON(i == ARRAY_SIZE(div_values));

	return div;
}

static unsigned long __pll_get_rate(struct clk_config_src_params *params)
{
	u32 reg_val;
	u32 fvco, vco_div, ref_div, fb_div;

	reg_val = readl_relaxed(params->cfg_reg1);

	fb_div = (reg_val >> params->fbdiv_shift) & params->fbdiv_mask_bits;
	ref_div = (reg_val >> params->refdiv_shift) & params->refdiv_mask_bits;
	vco_div = 1 << ((reg_val >> params->vcodiv_shift) &
		params->vcodiv_mask_bits);

	if (ref_div == 0)
		ref_div = 1;
	fvco = 4 * 26 * fb_div / ref_div;
	return (fvco / vco_div) * MHZ_TO_HZ;
}

static void __pll_config_rate(struct clk_config_src_params *params,
	unsigned long new_rate)
{
	u32 reg_val;
	unsigned long fvco;
	u32 kvco, vco_div, div_reg, fb_div, ref_div;

	new_rate /= MHZ_TO_HZ;
	ref_div = 3;
	vco_div = __pll_rate2div(new_rate, &div_reg);
	fvco = new_rate * vco_div;
	kvco = __pll_fvco2kvco(fvco);
	fb_div = fvco / (4 * 26 / ref_div);

	reg_val = readl_relaxed(params->cfg_reg1);
	reg_val &= ~(1 << params->clear_bits);
	reg_val &= ~(params->fbdiv_mask_bits << params->fbdiv_shift);
	reg_val |= (fb_div << params->fbdiv_shift);
	reg_val &= ~(params->refdiv_mask_bits << params->refdiv_shift);
	reg_val |= (ref_div << params->refdiv_shift);
	reg_val &= ~(params->kvco_mask_bits << params->kvco_shift);
	reg_val |= (kvco << params->kvco_shift);
	reg_val &= ~(params->vcodiv_mask_bits << params->vcodiv_shift);
	reg_val |= (div_reg << params->vcodiv_shift);
	writel_relaxed(reg_val, params->cfg_reg1);
}

static unsigned long __config_src_is_enabled(struct clk_config_src *clk_src)
{
	u32 reg_val, on_bit, locked_bit;
	struct clk_config_src_params *params = clk_src->params;

	on_bit = params->on_bit;
	locked_bit = params->locked_bit;
	reg_val = readl_relaxed(params->status_reg);

	if (clk_src->flags & MMP_CLK_PLL_CHECK_LOCKED)
		return (reg_val & on_bit) && (reg_val & locked_bit);
	else
		return reg_val & on_bit;
}

static void clk_config_src_init(struct clk_hw *hw)
{
	u32 reg_val, i;
	struct clk_config_src *clk_src = to_clk_config_src(hw);
	struct clk_config_src_params *params = clk_src->params;

	/* need lock if access common control register */
	if (clk_src->flags & MMP_CLK_PLL_CHECK_REQ_MODE) {
		spin_lock_check(clk_src->lock);
		reg_val = readl_relaxed(params->ctrl_reg);
		reg_val &= params->req_mode_mask_bits;
		reg_val |= params->req_mode_set_bits;
		writel_relaxed(reg_val, params->ctrl_reg);
		spin_unlock_check(clk_src->lock);
	}

	/* not need lock if access individual configuration register */
	for (i = 0; i < params->nr_cfg_regs; i++)
		writel_relaxed(params->cfg_regs[i].value,
			params->cfg_regs[i].addr);

	hw->clk->rate = 0;
	if (__config_src_is_enabled(clk_src))
		hw->clk->rate = __pll_get_rate(params);
	pr_info("%s boot up @%luHz\n", hw->clk->name, hw->clk->rate);
}

static int clk_config_src_enable(struct clk_hw *hw)
{
	struct clk_config_src *clk_src = to_clk_config_src(hw);
	struct clk_config_src_params *params = clk_src->params;

	spin_lock_check(clk_src->lock);
	CLK_SET_BITS(params->request_bit, 0, params->ctrl_reg);
	spin_unlock_check(clk_src->lock);

	/* busy wait to make sure clock source is enabled */
	while (!__config_src_is_enabled(clk_src))
		;

	return 0;
}

static void clk_config_src_disable(struct clk_hw *hw)
{
	struct clk_config_src *clk_src = to_clk_config_src(hw);
	struct clk_config_src_params *params = clk_src->params;

	/* FIXME:
	 * keep configurable clock source always on before control all clocks
	 * under the framework to avoid unexpected shutdown.
	 */
	return;

	spin_lock_check(clk_src->lock);
	CLK_SET_BITS(0, params->request_bit, params->ctrl_reg);
	spin_unlock_check(clk_src->lock);
	/* for configurable clock source, ensure it's off after disabled */
	while (__config_src_is_enabled(clk_src))
		;
}

static long clk_config_src_round_rate(struct clk_hw *hw, unsigned long rate,
	unsigned long *parent_rate)
{
	return rate;
}

static int clk_config_src_setrate(struct clk_hw *hw, unsigned long new_rate,
	unsigned long best_parent_rate)
{
	struct clk_config_src *clk_src = to_clk_config_src(hw);
	struct clk_config_src_params *params = clk_src->params;

	/* we don't support on-the-fly freq change. */
	if (__config_src_is_enabled(clk_src)) {
		pr_warn("cannot setrate when configurable PLL is running\n");
		return -1;
	}

	if (new_rate == hw->clk->rate)
		return 0;

	__pll_config_rate(params, new_rate);
	hw->clk->rate = new_rate;
	return 0;
}

static unsigned long clk_config_src_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	return hw->clk->rate;
}

static int clk_config_src_is_enabled(struct clk_hw *hw)
{
	return __config_src_is_enabled(to_clk_config_src(hw));
}

static struct clk_ops clk_config_src_ops = {
	.init = clk_config_src_init,
	.enable = clk_config_src_enable,
	.disable = clk_config_src_disable,
	.round_rate = clk_config_src_round_rate,
	.set_rate = clk_config_src_setrate,
	.recalc_rate = clk_config_src_recalc_rate,
	.is_enabled = clk_config_src_is_enabled,
};

struct clk *clk_register_config_src(const char *name, unsigned long flags,
	unsigned long mmp_flags, spinlock_t *lock,
	struct clk_config_src_params *params)
{
	void __iomem *base_va;
	struct clk *clk;
	struct clk_config_src *clk_src;
	struct clk_init_data init;

	clk_src = kzalloc(sizeof(*clk_src), GFP_KERNEL);
	if (!clk_src)
		return NULL;

	init.name = name;
	init.ops = &clk_config_src_ops;
	init.flags = flags;
	init.parent_names = NULL;
	init.num_parents = 0;

	if (likely(params)) {
		base_va = get_base_va(mmp_flags & (
			MMP_CLK_CTRL_REG_FROM_ACCU |
			MMP_CLK_CTRL_REG_FROM_MCCU |
			MMP_CLK_CTRL_REG_FROM_APMU |
			MMP_CLK_CTRL_REG_FROM_MPMU));
		params->cfg_reg1 = base_va + params->cfg_reg1_offset;
		params->ctrl_reg = base_va + params->ctrl_reg_offset;
		params->status_reg = base_va + params->status_reg_offset;
	}

	clk_src->flags = mmp_flags;
	clk_src->lock = lock;
	clk_src->params = params;
	clk_src->hw.init = &init;

	clk = clk_register(NULL, &clk_src->hw);
	if (IS_ERR(clk))
		kfree(clk_src);

	return clk;
}
