/*
 * Copyright (C) 2014 Marvell
 * Bill Zhou <zhoub@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * pxa1986 clock implementation for all kinds of hw components in ACCU & MCCU:
 * processors (apps, graphics, video), controllers, fabrics, and other simple
 * peripherals. All of these could be abstracted as 'clk_peri' and managed by
 * the similiar structures & interfaces implemented in clock driver.
 */

#include <linux/slab.h>
#include "adir-clk.h"

#define to_clk_peri(_hw) container_of(_hw, struct clk_peri, hw)

static void __init_pp_table(struct clk_peri *peri)
{
	u32 i;
	struct clk_peri_pp *pp_table = peri->params->pp_table;

	for (i = 0; i < peri->params->nr_pp; i++) {
		pp_table[i].parent = clk_get_sys(NULL, pp_table[i].parent_name);
		list_add_tail(&pp_table[i].node, &peri->pp_list);
	}
}

static struct clk_peri_pp *__rate2pp(struct clk_peri *peri, unsigned long rate)
{
	struct clk_peri_pp *pp;
	u32 idx = 0;

	if (unlikely(!peri->params->pp_table))
		return NULL;

	list_for_each_entry(pp, &peri->pp_list, node) {
		if ((pp->clk_rate >= rate) ||
				list_is_last(&pp->node, &peri->pp_list))
			break;
		idx++;
	}
	return pp;
}

static struct clk *__src2parent(u32 flags, u32 src)
{
	if (flags & MMP_CLK_SRC_FROM_APPS_SS) {
		if (src == SYS_PLL1_416)
			return clk_get_sys(NULL, PLL1_416_APPS);
		else if (src == SYS_PLL1_500)
			return clk_get_sys(NULL, PLL1_500_APPS);
		else if (src == SYS_PLL1_624)
			return clk_get_sys(NULL, PLL1_624_APPS);
		else if (src == SYS_PLL2_800)
			return clk_get_sys(NULL, PLL2_800_APPS);
		else if (src == CORE_PLL)
			return clk_get_sys(NULL, CORE_PLL1);
		else if (src == SYS_PLL3)
			return clk_get_sys(NULL, PLL3);
		else
			return ERR_PTR(-ENOENT);
	} else
		BUG_ON("__src2parent: not support this subsystem");

	return ERR_PTR(-ENOENT);
}

static u32 __get_fixed_div(struct clk_peri *peri)
{
	u32 reg_val;
	u32 div_shift = peri->params->fixed_div_shift;

	reg_val = readl_relaxed(peri->params->ctrl_reg);
	return (reg_val & (0x1 << div_shift)) >> div_shift;
}

static void __set_fixed_div(struct clk_hw *hw, u32 div)
{
	u32 reg_val, reg_val_new;
	struct clk_peri *peri = to_clk_peri(hw);
	u32 div_shift = peri->params->fixed_div_shift;

	if (!__clk_is_enabled(hw->clk) && hw->clk->parent)
		clk_prepare_enable(hw->clk->parent);

	/* 1. disable clock */
	reg_val = readl_relaxed(peri->params->ctrl_reg);
	reg_val &= ~(peri->params->enable_value);

	reg_val_new = reg_val;
	reg_val_new &= ~(0x1 << div_shift);
	reg_val_new |= (div << div_shift);
	if (__clk_is_enabled(hw->clk))
		reg_val_new |= peri->params->enable_value;

	writel_relaxed(reg_val, peri->params->ctrl_reg);

	/* 2. enable clock with new source & ratio */
	writel_relaxed(reg_val_new, peri->params->ctrl_reg);

	if (!__clk_is_enabled(hw->clk))
		clk_disable_unprepare(hw->clk->parent);
}

static void __get_src_div(struct clk_peri *peri, u32 *src, u32 *div)
{
	u32 reg_val;
	u32 src_shift = peri->params->src_shift;
	u32 src_mask = peri->params->src_mask_bits;
	u32 div_shift = peri->params->div_shift;
	u32 div_mask = peri->params->div_mask_bits;

	reg_val = readl_relaxed(peri->params->ctrl_reg);
	if (src)
		*src = (reg_val & src_mask) >> src_shift;
	if (div)
		*div = (reg_val & div_mask) >> div_shift;
}

static int __perform_fc(struct clk_hw *hw, struct clk *new_parent,
	u32 src, u32 div)
{
	u32 reg_val, reg_val_new;
	struct clk_peri *peri = to_clk_peri(hw);
	u32 src_shift = peri->params->src_shift;
	u32 src_mask = peri->params->src_mask_bits;
	u32 div_shift = peri->params->div_shift;
	u32 div_mask = peri->params->div_mask_bits;

	/* 1. enable both old and new parents */
	clk_prepare_enable(new_parent);
	if (!__clk_is_enabled(hw->clk) && hw->clk->parent)
		clk_prepare_enable(hw->clk->parent);

	if (peri->flags & MMP_CLK_SUPPORT_OTF_FC) {
		/* 2. set new source & ratio and clear done bit */
		reg_val = readl_relaxed(peri->params->ctrl_reg);
		reg_val &= ~src_mask;
		reg_val |= (src << src_shift);
		reg_val &= ~div_mask;
		reg_val |= (div << div_shift);
		reg_val |= peri->params->fc_done_bit;
		writel_relaxed(reg_val, peri->params->ctrl_reg);

		while (readl_relaxed(peri->params->ctrl_reg) &
			peri->params->fc_done_bit)
			/* wait for done bit was cleared */;

		/* 3. set go bit and wait for fc done */
		reg_val = readl_relaxed(peri->params->ctrl_reg);
		reg_val |= peri->params->fc_go_bit;
		writel_relaxed(reg_val, peri->params->ctrl_reg);

		while (!(readl_relaxed(peri->params->ctrl_reg) &
			peri->params->fc_done_bit))
			/* wait for fc was done */;
	} else {
		/* below two steps should be done as quickly as possiable */
		/* 2. disable clock */
		reg_val = readl_relaxed(peri->params->ctrl_reg);
		reg_val &= ~(peri->params->enable_value);

		reg_val_new = reg_val;
		reg_val_new &= ~src_mask;
		reg_val_new |= (src << src_shift);
		reg_val_new &= ~div_mask;
		reg_val_new |= (div << div_shift);
		if (__clk_is_enabled(hw->clk))
			reg_val_new |= peri->params->enable_value;

		writel_relaxed(reg_val, peri->params->ctrl_reg);

		/* 3. enable clock with new source & ratio */
		writel_relaxed(reg_val_new, peri->params->ctrl_reg);
	}

	/* 4. disable old parent after fc was done and new parent if possible */
	if (hw->clk->parent)
		clk_disable_unprepare(hw->clk->parent);
	if (!__clk_is_enabled(hw->clk))
		clk_disable_unprepare(new_parent);

	__clk_reparent(hw->clk, new_parent);
	return 0;
}

static void clk_peri_init(struct clk_hw *hw)
{
	struct clk_peri *peri = to_clk_peri(hw);

	INIT_LIST_HEAD(&peri->pp_list);

	if (peri->params->pp_table)
		__init_pp_table(peri);
}

static int clk_peri_prepare(struct clk_hw *hw)
{
	int i;
	struct clk *clk_depend;
	struct clk_peri *peri = to_clk_peri(hw);

	for (i = 0; i < peri->params->nr_depend_clks; i++) {
		BUG_ON(!peri->params->depend_clks_name[i]);
		clk_depend = clk_get_sys(NULL,
			peri->params->depend_clks_name[i]);
		BUG_ON(!clk_depend);
		clk_prepare_enable(clk_depend);
	}
	return 0;
}

static void clk_peri_unprepare(struct clk_hw *hw)
{
	int i;
	struct clk *clk_depend = NULL;
	struct clk_peri *peri = to_clk_peri(hw);

	for (i = peri->params->nr_depend_clks - 1; i >= 0; i--) {
		if (peri->params->depend_clks_name[i])
			clk_depend = clk_get_sys(NULL,
				peri->params->depend_clks_name[i]);
		if (clk_depend)
			clk_disable_unprepare(clk_depend);
	}
}

static int clk_peri_enable(struct clk_hw *hw)
{
	struct clk_peri *peri = to_clk_peri(hw);

	spin_lock_check(peri->lock);
	CLK_SET_BITS(peri->params->enable_value, 0, peri->params->ctrl_reg);
	if (unlikely(peri->flags & MMP_CLK_HAS_TWO_CTRL_REGS))
		CLK_SET_BITS(peri->params->enable_value, 0,
			peri->params->ctrl_reg2);
	spin_unlock_check(peri->lock);
	return 0;
}

static void clk_peri_disable(struct clk_hw *hw)
{
	struct clk_peri *peri = to_clk_peri(hw);

	spin_lock_check(peri->lock);
	CLK_SET_BITS(0, peri->params->enable_value, peri->params->ctrl_reg);
	if (unlikely(peri->flags & MMP_CLK_HAS_TWO_CTRL_REGS))
		CLK_SET_BITS(0, peri->params->enable_value,
			peri->params->ctrl_reg2);
	spin_unlock_check(peri->lock);
}

static unsigned long clk_peri_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	u32 src, div;
	struct clk *clk_parent;
	struct clk_peri *peri = to_clk_peri(hw);

	__get_src_div(peri, &src, &div);
	BUG_ON(div == 0);
	clk_parent = __src2parent(peri->flags, src);
	return clk_get_rate(clk_parent) / div;
}

static long clk_peri_round_rate(struct clk_hw *hw, unsigned long rate,
	unsigned long *parent_rate)
{
	struct clk_peri_pp *pp;
	struct clk_peri *peri = to_clk_peri(hw);

	pp = __rate2pp(peri, rate);
	return !pp ? rate : pp->clk_rate;
}

static int clk_peri_set_rate(struct clk_hw *hw, unsigned long rate,
	unsigned long parent_rate)
{
	struct clk *new_parent;
	struct clk_peri_pp *new_pp;
	struct clk_peri *peri = to_clk_peri(hw);
	unsigned long old_rate = hw->clk->rate;

	if (rate == old_rate)
		return 0;

	new_pp = __rate2pp(peri, rate);
	if (likely(new_pp)) {
		new_parent = new_pp->parent;
		spin_lock_check(peri->lock)
		__perform_fc(hw, new_parent, new_pp->src, new_pp->div);
		spin_unlock_check(peri->lock);
	} else {
		pr_err("%s: %s no pp for new rate\n", __func__, hw->clk->name);
		return -EINVAL;
	}

	return 0;
}

/* return the index in 'struct clk_parent xxx_parents[]' */
static u8 clk_peri_get_parent(struct clk_hw *hw)
{
	u32 i, src;
	struct clk_peri *peri = to_clk_peri(hw);

	__get_src_div(peri, &src, NULL);
	for (i = 0; i < peri->params->nr_parents; i++) {
		if (peri->params->parents[i].src == src)
			break;
	}
	return i;
}

static unsigned long clk_peri_fixed_div_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	u32 div;
	struct clk_peri *peri = to_clk_peri(hw);

	div = __get_fixed_div(peri);
	return peri->params->pp_table[div].clk_rate;
}

static int clk_peri_fixed_div_set_rate(struct clk_hw *hw, unsigned long rate,
	unsigned long parent_rate)
{
	struct clk_peri_pp *new_pp;
	struct clk_peri *peri = to_clk_peri(hw);
	unsigned long old_rate = hw->clk->rate;

	if (rate == old_rate)
		return 0;

	new_pp = __rate2pp(peri, rate);
	if (likely(new_pp)) {
		spin_lock_check(peri->lock)
		__set_fixed_div(hw, new_pp->div);
		spin_unlock_check(peri->lock);
	} else {
		pr_err("%s: %s no pp for new rate\n", __func__, hw->clk->name);
		return -EINVAL;
	}

	return 0;
}

static u8 clk_peri_fixed_div_get_parent(struct clk_hw *hw)
{
	return 0;	/* only SYS_PLL1_624 for clock with fixed div */
}

struct clk_ops clk_peri_on_off_ops = {
	.enable = clk_peri_enable,
	.disable = clk_peri_disable,
};

struct clk_ops clk_peri_fixed_div_ops = {
	.init = clk_peri_init,
	.prepare = clk_peri_prepare,
	.unprepare = clk_peri_unprepare,
	.enable = clk_peri_enable,
	.disable = clk_peri_disable,
	.recalc_rate = clk_peri_fixed_div_recalc_rate,
	.round_rate = clk_peri_round_rate,
	.set_rate = clk_peri_fixed_div_set_rate,
	.get_parent = clk_peri_fixed_div_get_parent,
};

struct clk_ops clk_peri_ops = {
	.init = clk_peri_init,
	.prepare = clk_peri_prepare,
	.unprepare = clk_peri_unprepare,
	.enable = clk_peri_enable,
	.disable = clk_peri_disable,
	.recalc_rate = clk_peri_recalc_rate,
	.round_rate = clk_peri_round_rate,
	.set_rate = clk_peri_set_rate,
	.get_parent = clk_peri_get_parent,
};

struct clk *clk_register_peri(const char *name, const char **parent_name,
	u8 num_parents, unsigned long flags, unsigned long mmp_flags,
	spinlock_t *lock, struct clk_peri_params *params)
{
	void __iomem *base_va;
	struct clk *clk;
	struct clk_peri *peri;
	struct clk_init_data init;

	peri = kzalloc(sizeof(*peri), GFP_KERNEL);
	if (!peri)
		return NULL;

	init.name = name;
	init.flags = flags;
	init.parent_names = parent_name;
	init.num_parents = num_parents;
	if (unlikely(mmp_flags & MMP_CLK_ONLY_ON_OFF_OPS))
		init.ops = &clk_peri_on_off_ops;
	else if (unlikely(mmp_flags & MMP_CLK_USE_FIXED_DIV))
		init.ops = &clk_peri_fixed_div_ops;
	else
		init.ops = &clk_peri_ops;

	base_va = get_base_va(mmp_flags & (
		MMP_CLK_CTRL_REG_FROM_ACCU | MMP_CLK_CTRL_REG_FROM_MCCU |
		MMP_CLK_CTRL_REG_FROM_APMU | MMP_CLK_CTRL_REG_FROM_MPMU));
	params->ctrl_reg = base_va + params->ctrl_reg_offset;
	if (unlikely(mmp_flags & MMP_CLK_HAS_TWO_CTRL_REGS))
		params->ctrl_reg2 = base_va + params->ctrl_reg2_offset;
	peri->flags = mmp_flags;
	peri->lock = lock;
	peri->params = params;
	peri->hw.init = &init;

	clk = clk_register(NULL, &peri->hw);
	if (IS_ERR(clk))
		kfree(peri);

	return clk;
}
