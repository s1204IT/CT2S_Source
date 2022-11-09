/*
 * linux/drivers/video/mmp/hw/dfc.c
 * dfc driver support
 *
 * Copyright (C) 2014 Marvell Technology Group Ltd.
 * Authors:  huangyh <huangyh@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/stat.h>
#include "mmp_dsi.h"
#include "mmp_ctrl.h"
#include <video/mipi_display.h>
#include <linux/clk-private.h>
#include "clk.h"

#define ROUND_RATE_RANGE_MHZ		2  /* unit: MHz */
#define DFC_RETRY_TIMEOUT	60

#define LCD_PN_SCLK	(0x1a8)
#define	SCLK_SOURCE_SELECT(src)		((src)<<30)
#define	SCLK_SOURCE_SELECT_MASK		0xc0000000
#define	SCLK_SOURCE_SELECT_DC4(src)		(((src)<<29) | ((src)<<12))
#define	SCLK_SOURCE_SELECT_MASK_DC4		0xe0003000

#define	DSI1_BITCLK_DIV(div)			(div<<8)
#define	DSI1_BITCLK_DIV_MASK			0x00000F00
#define	CLK_INT_DIV(div)				(div)
#define	CLK_INT_DIV_MASK				0x000000FF

/*
 * MAX_STATE: max numbers of parent clk;
 * MAX_STR_LEN: the max length of string of clk name;
 */
#define MAX_STATE 8
#define MAX_STR_LEN 12
static unsigned int parent_clk_tbl[8];
static int parent_clk_count;
static const char * const parent0_clk_tbl[] = {"pn_sclk", "dsi_sclk"};
static char parent1_clk_tbl[MAX_STATE][MAX_STR_LEN];
static char parent2_clk_tbl[MAX_STATE][MAX_STR_LEN];
static const char disp_aclk_parent_default[] = {"pll1_416"};
unsigned char parent2_count;
static unsigned long pll3_vco_default;
static unsigned long pll3_default;
static unsigned int lcd_clk_en;
static unsigned int plat;
static unsigned long original_rate;
static unsigned int sclk_div;

/* PLL post divider table */
static struct div_map pll_post_div_tbl[] = {
	/* divider, reg vaule */
	{1, 0},
	{2, 2},
	{3, 4},
	{4, 5},
	{6, 7},
	{8, 8},
};

struct parent_index {
	int parent1;
	int parent2;
};

enum {
	PXA1088,
	PXA1L88,
	PXA1928,
	PXA1U88,
};

enum {
	DFC_FREQ_PARENT2_PLL_NONE,
	DFC_FREQ_PARENT2_PLL1_416M,
	DFC_FREQ_PARENT2_PLL1_624M,
	DFC_FREQ_PARENT2_PLL3,
	DFC_FREQ_PARENT2_PLL2,
	DFC_FREQ_PARENT2_PLL1_832M,
};

enum {
	DFC_FREQ_PARENT1_NONE,
	DFC_FREQ_PARENT1_DISPLAY1,
	DFC_FREQ_PARENT1_DSIPLL,
};

enum {
	DFC_CHANGE_LCD,
	DFC_DISABLE_PLL3,
	DFC_ENABLE_PLL3,
	DFC_CHANGE_PLL3,
	DFC_RESTORE_PLL3,
};

static u32 dsi_get_bpp_from_inputfmt(int fmt)
{
	switch (fmt) {
	case PIXFMT_BGR565:
		return 16;
	case PIXFMT_BGR666PACK:
		return 18;
	case PIXFMT_BGR666UNPACK:
		return 24;
	case PIXFMT_BGR888PACK:
		return 24;
	default:
		pr_err("%s, err: fmt not support or mode not set\n", __func__);
		return 0;
	}
}

static unsigned long set_pixel_clk(struct mmphw_ctrl *ctrl, unsigned long dsi_rate)
{
	struct mmp_path *path = ctrl->path_plats[0].path;
	struct mmp_dsi *dsi = mmp_path_to_dsi(path);
	unsigned long pixel_rate;
	int x;

	u32 bpp = dsi_get_bpp_from_inputfmt(
			dsi->mode.pix_fmt_out);

	if (!DISP_GEN4(ctrl->version)) {
		x = (dsi->setting.lanes > 2) ? 2 : 3;
		pixel_rate = dsi_rate >> x;
	} else {
		x = bpp / dsi->setting.lanes;
		pixel_rate = dsi_rate / x;
	}

	/*
	 * FIXME: On ULC:
	 * For WVGA with 2 lanes whose pixel_clk is lower than
	 * esc clk, we need to set pixel >= esc clk, so that LP
	 * cmd and video mode cna work.
	 */
	if (DISP_GEN4_LITE(dsi->version)) {
		if (pixel_rate < ESC_52M)
			pixel_rate = ESC_52M;
	}

	return pixel_rate;
}

static void get_parent_index(unsigned long rate, struct parent_index *index)
{
	int i = 0;
	struct clk *clk;

	for (i = 0; i < parent2_count; i++) {
		clk = clk_get(NULL, parent2_clk_tbl[i]);
		if ((!IS_ERR(clk)) && (rate == (clk->rate / MHZ_TO_HZ)))
			index->parent2 = i + DFC_FREQ_PARENT2_PLL1_416M;
	}

	if ((index->parent2 == DFC_FREQ_PARENT2_PLL1_416M) ||
		(index->parent2 == DFC_FREQ_PARENT2_PLL1_624M))
		index->parent1 = DFC_FREQ_PARENT1_DISPLAY1;

	if ((index->parent2 == DFC_FREQ_PARENT2_PLL3) ||
		(index->parent2 == DFC_FREQ_PARENT2_PLL2) ||
		(index->parent2 == DFC_FREQ_PARENT2_PLL1_832M))
		index->parent1 = DFC_FREQ_PARENT1_DSIPLL;
}

static void add_to_parent_tbl(unsigned long rate)
{
	int i;

	rate /= 1000000;
	if (rate > 1000)
		return;
	for (i = 0; i < 8; i++) {
		if (parent_clk_tbl[i] == rate)
			return;
	}
	parent_clk_tbl[parent_clk_count] = rate;
	parent_clk_count++;
}

static void create_parent_tbl(struct clk *clk11)
{
	int i;
	struct clk *clk1;

	for (i = 0; i < clk11->num_parents; i++) {
		clk1 = clk_get(NULL, clk11->parent_names[i]);
		if (clk1) {
			add_to_parent_tbl(clk1->rate);
			create_parent_tbl(clk1);
		}
	}
}

static int find_best_parent(unsigned int rate, unsigned int *count)
{
	int i, j;
	unsigned int tmp = 0xff;
	unsigned int real_rate;
	int ret = 0;

	for (i = 0; i < parent_clk_count; i++) {
		for (j = 1; j < 15; j++) {
			real_rate = parent_clk_tbl[i] / j;
			if (rate > real_rate + ROUND_RATE_RANGE_MHZ)
				break;
			else if ((rate <= (real_rate + ROUND_RATE_RANGE_MHZ)) &&
				 (rate >= (real_rate - ROUND_RATE_RANGE_MHZ))) {
				/* round clock rate in 1MHz range */
				tmp = real_rate;
				*count = i;
				ret = 1;
				break;
			}
		}

		if (tmp != 0xff)
			break;
	}

	return ret;
}

static void get_current_parent_index(struct clk *clk,
	struct parent_index *index)
{
	struct clk *temp = clk;
	int i;

	while (!IS_ERR_OR_NULL(temp)) {
		for (i = 0; i < parent2_count; i++) {
			if (!strcmp(temp->name, parent2_clk_tbl[i])) {
				index->parent2 = i + DFC_FREQ_PARENT2_PLL1_416M;
				break;
			}
		}
		temp = clk_get_parent(temp);
	}
	if ((index->parent2 == DFC_FREQ_PARENT2_PLL1_416M) ||
		(index->parent2 == DFC_FREQ_PARENT2_PLL1_624M))
		index->parent1 = DFC_FREQ_PARENT1_DISPLAY1;

	if ((index->parent2 == DFC_FREQ_PARENT2_PLL3) ||
		(index->parent2 == DFC_FREQ_PARENT2_PLL2) ||
		(index->parent2 == DFC_FREQ_PARENT2_PLL1_832M))
		index->parent1 = DFC_FREQ_PARENT1_DSIPLL;
}

static int is_parent1_changed(struct mmphw_ctrl *ctrl)
{
	return strcmp(ctrl->dfc.current_parent1->name, ctrl->dfc.parent1->name);
}

static int is_parent2_changed(struct mmphw_ctrl *ctrl)
{
	return strcmp(ctrl->dfc.current_parent2->name, ctrl->dfc.parent2->name);
}

static void calculate_reg_value(struct mmphw_ctrl *ctrl)
{
	unsigned int sclk;
	unsigned int apmu;
	struct mmp_dfc *dfc = &ctrl->dfc;
	u32  bclk_div, pclk_div;

	sclk = readl_relaxed(dfc->sclk_reg);
	apmu = readl_relaxed(dfc->apmu_reg);
	if (!DISP_GEN4(ctrl->version))
		sclk &= ~(SCLK_SOURCE_SELECT_MASK |
			 DSI1_BITCLK_DIV_MASK |
			 CLK_INT_DIV_MASK);
	else
		sclk &= ~(SCLK_SOURCE_SELECT_MASK_DC4 |
			 DSI1_BITCLK_DIV_MASK |
			 CLK_INT_DIV_MASK);
	bclk_div = (dfc->parent2->rate + dfc->dsi_rate / 2) / dfc->dsi_rate;
	pclk_div = (dfc->parent2->rate + dfc->path_rate / 2) / dfc->path_rate;
	sclk |= DSI1_BITCLK_DIV(bclk_div) | CLK_INT_DIV(pclk_div);

	if (!strcmp(dfc->parent2->name,
		parent2_clk_tbl[DFC_FREQ_PARENT2_PLL1_624M - 1])) {
		apmu &= ~(1<<lcd_clk_en);
		if (!DISP_GEN4(ctrl->version))
			sclk |= SCLK_SOURCE_SELECT(1);
		else
			sclk |= SCLK_SOURCE_SELECT_DC4(1);
	} else if (!strcmp(dfc->parent2->name,
		parent2_clk_tbl[DFC_FREQ_PARENT2_PLL1_416M - 1])) {
		apmu |= (1<<lcd_clk_en);
		if (!DISP_GEN4(ctrl->version))
			sclk |= SCLK_SOURCE_SELECT(1);
		else
			sclk |= SCLK_SOURCE_SELECT_DC4(1);
	} else {
		if (!DISP_GEN4(ctrl->version))
			sclk |= SCLK_SOURCE_SELECT(3);
		else
			sclk |= SCLK_SOURCE_SELECT_DC4(3);
	}
	dfc->apmu_value = apmu;
	dfc->sclk_value = sclk;
}


/* rate unit is Mhz */
static void dynamic_change_pll3(unsigned int rate, int type)
{
	struct clk *pll3, *pll3_vco;
	unsigned long i, pll3_rate, pll3vco_rate = 0;

	pll3 = clk_get(NULL, "pll3");
	pll3_vco = clk_get(NULL, "pll3_vco");

	switch (type) {
	case DFC_RESTORE_PLL3:
		if (!(clk_get_rate(pll3) == pll3_default)) {
			pll3_rate = pll3_default;
			if (PXA1928 != plat) {
				pll3vco_rate = pll3_vco_default;
				clk_set_rate(pll3_vco, pll3vco_rate);
			}
			clk_set_rate(pll3, pll3_rate);
		}
		break;
	case DFC_DISABLE_PLL3:
		clk_disable_unprepare(pll3);
		break;
	case DFC_ENABLE_PLL3:
		clk_prepare_enable(pll3);
		break;
	default:
		if (PXA1928 != plat) {
			for (i = 0; i < ARRAY_SIZE(pll_post_div_tbl); i++) {
				pll3vco_rate = rate * pll_post_div_tbl[i].div;
				if ((pll3vco_rate > 1200) &&
						(pll3vco_rate < 2500))
					break;
			}

			if (i == ARRAY_SIZE(pll_post_div_tbl))
				BUG_ON("Multiplier is out of range\n");

			pll3vco_rate *= MHZ_TO_HZ;
			clk_set_rate(pll3_vco, pll3vco_rate);
		}

		pll3_rate = rate * MHZ_TO_HZ;
		clk_set_rate(pll3, pll3_rate);
		break;
	}

	usleep_range(500, 1000);
}

static int dynamic_change_lcd(struct mmphw_ctrl *ctrl, struct mmp_dfc *dfc)
{
	struct mmp_path *path = ctrl->path_plats[0].path;
	struct mmp_dsi *dsi = mmp_path_to_dsi(path);
	struct parent_index source;
	int count = 0;
	struct clk *dsi_clk = dsi->clk;
	unsigned long flags;
	int ret = 0;
	struct clk *clk;

	clk = clk_get(NULL, parent0_clk_tbl[1]);
	memset(&source, 0, sizeof(source));
	get_current_parent_index(dsi_clk, &source);
	dfc->current_parent1 = clk_get(NULL,
		parent1_clk_tbl[source.parent1-1]);
	dfc->current_parent2 = clk_get(NULL,
		parent2_clk_tbl[source.parent2-1]);
	memcpy(&ctrl->dfc, dfc, sizeof(*dfc));

	calculate_reg_value(ctrl);
	if (path->status) {
		atomic_set(&ctrl->dfc.commit, 1);
		do {
			mmp_path_wait_special_vsync(path);
			count++;
		} while (atomic_read(&ctrl->dfc.commit) && (count < DFC_RETRY_TIMEOUT));
	} else {
		writel_relaxed(ctrl->dfc.apmu_value, ctrl->dfc.apmu_reg);
		writel_relaxed(ctrl->dfc.sclk_value, ctrl->dfc.sclk_reg);
		sclk_div = ctrl->dfc.sclk_value;
	}

	spin_lock_irqsave(&ctrl->dfc.lock, flags);
	if (unlikely(count == DFC_RETRY_TIMEOUT)
		&& atomic_read(&ctrl->dfc.commit)) {
		atomic_set(&ctrl->dfc.commit, 0);
		spin_unlock_irqrestore(&ctrl->dfc.lock, flags);
		ret = -EFAULT;
	} else {
		spin_unlock_irqrestore(&ctrl->dfc.lock, flags);
		if (is_parent1_changed(ctrl)) {
			clk_set_parent(ctrl->dfc.parent0, ctrl->dfc.parent1);
			if (DISP_GEN4(ctrl->version))
				clk_set_parent(clk, ctrl->dfc.parent1);
		}
		if (is_parent2_changed(ctrl))
			if (strcmp((ctrl->dfc.parent2->name), "pll3") || (PXA1928 != plat))
				clk_set_parent(ctrl->dfc.parent1, ctrl->dfc.parent2);
	}

	return ret;
}

static int dfc_prepare(struct mmphw_ctrl *ctrl, unsigned int rate)
{
	struct mmp_path *path = ctrl->path_plats[0].path;
	struct clk *path_clk = path_to_path_plat(path)->clk;
	struct mmp_dsi *dsi = mmp_path_to_dsi(path);
	struct parent_index target, source;
	int best_parent_count = 0;
	struct clk *dsi_clk = dsi->clk;
	struct mmp_dfc *dfc;
	struct mmp_dfc temp;
	int i, ret = 0;

	memset(&target, 0, sizeof(target));
	memset(&temp, 0, sizeof(temp));
	memcpy(&temp, &ctrl->dfc, sizeof(temp));

	temp.dsi_rate = rate * MHZ_TO_HZ;
	temp.path_rate = set_pixel_clk(ctrl, temp.dsi_rate);

	temp.parent0 = clk_get(NULL, parent0_clk_tbl[0]);

	/*
	 * FIXME: For Pxa1928, need to clear the clk table and count.
	 */
	if (PXA1928 == plat) {
		for(i=0;i<8;i++)
			parent_clk_tbl[i] = 0x0;
		parent_clk_count = 0;
	}

	create_parent_tbl(path_clk);
	if (!find_best_parent(rate, &best_parent_count))
		temp.best_parent = DFC_FREQ_PARENT2_PLL_NONE;
	else {
		get_parent_index(parent_clk_tbl[best_parent_count], &target);
		temp.best_parent = target.parent2;
	}

	memset(&source, 0, sizeof(source));
	get_current_parent_index(dsi_clk, &source);
	temp.current_parent1 =
		clk_get(NULL, parent1_clk_tbl[source.parent1-1]);
	temp.current_parent2 =
		clk_get(NULL, parent2_clk_tbl[source.parent2-1]);
	while (!list_empty(&ctrl->dfc_list.queue)) {
		/* free the waitlist elements if any */
		dfc = list_first_entry(&ctrl->dfc_list.queue,
			struct mmp_dfc, queue);
		list_del(&dfc->queue);
		kfree(dfc);
	}

	switch (temp.best_parent) {
	case DFC_FREQ_PARENT2_PLL1_416M:
	case DFC_FREQ_PARENT2_PLL1_624M:
		/* prepare the first change */
		temp.parent2 =
			clk_get(NULL, parent2_clk_tbl[target.parent2-1]);
		temp.parent1 =
			clk_get(NULL, parent1_clk_tbl[target.parent1-1]);

		/* add dfc request to list */
		dfc = kzalloc(sizeof(struct mmp_dfc),	GFP_KERNEL);
		if (dfc == NULL)
			goto prepare_fail;
		memcpy(dfc, &temp, sizeof(*dfc));
		dfc->name = DFC_CHANGE_LCD;
		list_add_tail(&dfc->queue, &ctrl->dfc_list.queue);

		/* prepare the second change */
		dfc = kzalloc(sizeof(struct mmp_dfc), GFP_KERNEL);
		if (dfc == NULL)
			goto prepare_fail;
		dfc->name = DFC_RESTORE_PLL3;
		list_add_tail(&dfc->queue, &ctrl->dfc_list.queue);
		break;
	case DFC_FREQ_PARENT2_PLL3:
		/* prepare enable pll3 change */
		dfc = kzalloc(sizeof(struct mmp_dfc), GFP_KERNEL);
		if (dfc == NULL)
			return -EFAULT;
		dfc->name = DFC_ENABLE_PLL3;
		list_add_tail(&dfc->queue, &ctrl->dfc_list.queue);

		/* prepare the first change */
		temp.parent2 =
			clk_get(NULL, parent2_clk_tbl[target.parent2-1]);
		temp.parent1 =
			clk_get(NULL, parent1_clk_tbl[target.parent1-1]);

		/* add dfc reqyest to list */
		dfc = kzalloc(sizeof(struct mmp_dfc), GFP_KERNEL);
		if (dfc == NULL)
			goto prepare_fail;
		memcpy(dfc, &temp, sizeof(*dfc));
		dfc->name = DFC_CHANGE_LCD;
		list_add_tail(&dfc->queue, &ctrl->dfc_list.queue);

		/* prepare disable pll3 change */
		dfc = kzalloc(sizeof(struct mmp_dfc), GFP_KERNEL);
		if (dfc == NULL)
			goto prepare_fail;
		dfc->name = DFC_DISABLE_PLL3;
		list_add_tail(&dfc->queue, &ctrl->dfc_list.queue);
		break;
	case DFC_FREQ_PARENT2_PLL_NONE:
		if (!strcmp(temp.current_parent2->name,
			parent2_clk_tbl[DFC_FREQ_PARENT2_PLL3 - 1])) {
			/* prepare the first change */
			temp.parent2 =
				clk_get(NULL, parent2_clk_tbl[DFC_FREQ_PARENT2_PLL1_416M - 1]);
			temp.parent1 =
				clk_get(NULL, parent1_clk_tbl[DFC_FREQ_PARENT1_DISPLAY1 - 1]);
			temp.dsi_rate = temp.parent2->rate;
			temp.path_rate = set_pixel_clk(ctrl, temp.dsi_rate);

			/* add dfc reqyest to list */
			dfc = kzalloc(sizeof(struct mmp_dfc),	GFP_KERNEL);
			if (dfc == NULL)
				goto prepare_fail;
			memcpy(dfc, &temp, sizeof(*dfc));
			dfc->name = DFC_CHANGE_LCD;
			list_add_tail(&dfc->queue, &ctrl->dfc_list.queue);
		}

		/* prepare the second change */
		temp.dsi_rate = rate * MHZ_TO_HZ;
		temp.path_rate = set_pixel_clk(ctrl, temp.dsi_rate);

		dfc = kzalloc(sizeof(struct mmp_dfc),	GFP_KERNEL);
		if (dfc == NULL)
			goto prepare_fail;
		memcpy(dfc, &temp, sizeof(*dfc));
		dfc->name = DFC_CHANGE_PLL3;
		list_add_tail(&dfc->queue, &ctrl->dfc_list.queue);

		/* prepare enable pll3 change */
		dfc = kzalloc(sizeof(struct mmp_dfc), GFP_KERNEL);
		if (dfc == NULL)
			return -EFAULT;
		dfc->name = DFC_ENABLE_PLL3;
		list_add_tail(&dfc->queue, &ctrl->dfc_list.queue);

		/* prepare the third change */
		temp.parent2 =
			clk_get(NULL, parent2_clk_tbl[DFC_FREQ_PARENT2_PLL3 - 1]);
		temp.parent1 =
			clk_get(NULL, parent1_clk_tbl[DFC_FREQ_PARENT1_DSIPLL - 1]);
		dfc = kzalloc(sizeof(struct mmp_dfc), GFP_KERNEL);
		if (dfc == NULL)
			goto prepare_fail;
		memcpy(dfc, &temp, sizeof(*dfc));
		dfc->name = DFC_CHANGE_LCD;
		list_add_tail(&dfc->queue, &ctrl->dfc_list.queue);

		/* prepare disable pll3 change */
		dfc = kzalloc(sizeof(struct mmp_dfc), GFP_KERNEL);
		if (dfc == NULL)
			goto prepare_fail;
		dfc->name = DFC_DISABLE_PLL3;
		list_add_tail(&dfc->queue, &ctrl->dfc_list.queue);
		break;
	default:
		break;
	}
	return ret;
prepare_fail:
	while (!list_empty(&ctrl->dfc_list.queue)) {
		/* free the waitlist elements if any */
		dfc = list_first_entry(&ctrl->dfc_list.queue,
			struct mmp_dfc, queue);
		list_del(&dfc->queue);
		kfree(dfc);
	}
	return -EFAULT;
}

static int dfc_commit(struct mmphw_ctrl *ctrl)
{
	struct mmp_dfc *dfc;
	int ret = 0;

	while (!list_empty(&ctrl->dfc_list.queue)) {
		/* free the waitlist elements if any */
		dfc = list_first_entry(&ctrl->dfc_list.queue,
			struct mmp_dfc, queue);
		list_del(&dfc->queue);
		switch (dfc->name) {
		case DFC_CHANGE_LCD:
			ret = dynamic_change_lcd(ctrl, dfc);
			break;
		case DFC_DISABLE_PLL3:
		case DFC_ENABLE_PLL3:
		case DFC_CHANGE_PLL3:
		case DFC_RESTORE_PLL3:
			dynamic_change_pll3(dfc->dsi_rate / MHZ_TO_HZ, dfc->name);
			break;
		default:
			break;
		}
		kfree(dfc);
	}

	return ret;
}

static void dfc_handle(void *data)
{
	u32 temp;

	struct device *dev = (struct device *)data;
	struct mmphw_ctrl *ctrl = dev_get_drvdata(dev);

	if (atomic_read(&ctrl->dfc.commit)) {
		/* disable sclk firstly */
		temp = readl_relaxed(ctrl->dfc.sclk_reg);
		temp |= 0x1<<28;
		writel_relaxed(temp, ctrl->dfc.sclk_reg);
		/* change the source */
		writel_relaxed(ctrl->dfc.apmu_value, ctrl->dfc.apmu_reg);
		temp = (ctrl->dfc.sclk_value | (0x1<<28));
		writel_relaxed(ctrl->dfc.sclk_value, ctrl->dfc.sclk_reg);

		sclk_div = ctrl->dfc.sclk_value;
		/* enable sclk end */
		temp = readl_relaxed(ctrl->dfc.sclk_reg);
		temp &= ~(0x1<<28);
		writel_relaxed(temp, ctrl->dfc.sclk_reg);
		atomic_set(&ctrl->dfc.commit, 0);

	}
}

static int dfc_set_rate(struct mmp_path *path, unsigned long rate)
{
	struct mmphw_ctrl *ctrl = path_to_ctrl(path);
	int ret;

	mutex_lock(&ctrl->access_ok);

	if (!path->status) {
		/*
		 * FIXME: enable hclk to do register operation.
		 * TODO: Better use power domain on off
		 */
		clk_prepare_enable(clk_get(NULL, "LCDCIHCLK"));
		writel_relaxed(ctrl->regs_store[LCD_SCLK_DIV/4],
				ctrl->reg_base + LCD_SCLK_DIV);
	}

	ret = dfc_prepare(ctrl, rate);
	if (ret < 0) {
		pr_err("dfc prepare failure\n");
		mutex_unlock(&ctrl->access_ok);
		return -1;
	}
	ret = dfc_commit(ctrl);
	if (ret < 0) {
		pr_err("dfc commit failure\n");
		mutex_unlock(&ctrl->access_ok);
		return -1;
	}

	if (!path->status) {
		ctrl->regs_store[LCD_SCLK_DIV/4] = readl_relaxed(ctrl->reg_base + LCD_SCLK_DIV);
		clk_disable_unprepare(clk_get(NULL, "LCDCIHCLK"));
	}

	mutex_unlock(&ctrl->access_ok);
	return 0;
}

static unsigned long dfc_get_rate(struct mmp_path *path)
{
	struct mmphw_ctrl *ctrl = path_to_ctrl(path);
	unsigned long rate;
	int val;

	mutex_lock(&ctrl->access_ok);
	if (!path->status) {
		clk_prepare_enable(clk_get(NULL, "LCDCIHCLK"));
		writel_relaxed(ctrl->regs_store[LCD_SCLK_DIV/4],
				ctrl->reg_base + LCD_SCLK_DIV);
	}

	/*
	 * Show mipi dsi rate in sprintf buffer so that
	 * upper side can get this rate directly by read
	 * sysfs node.
	 */
	rate = clk_get_rate(clk_get(NULL, "dsi_sclk"));
	val = sclk_div;
	val &= DSI1_BITCLK_DIV_MASK;
	val = val >> 8;
	rate = rate/val/1000000;

	if (!path->status) {
		ctrl->regs_store[LCD_SCLK_DIV/4] = readl_relaxed(ctrl->reg_base + LCD_SCLK_DIV);
		clk_disable_unprepare(clk_get(NULL, "LCDCIHCLK"));
	}
	mutex_unlock(&ctrl->access_ok);
	return rate;
}

#define    DIP_START   1
#define    DIP_END     0

int dfc_request(struct notifier_block *b, unsigned long val, void *v)
{
	struct mmp_path *path = mmp_get_path("mmp_pnpath");
	u32 rate;
	int ret;

	if (val == DIP_START) {
		rate = *((u32 *)v);
		if (rate == 0) {
			pr_err("LCD DFC Failed: The rate is 0\n");
			return NOTIFY_DONE;
		}
	} else
		rate = original_rate / MHZ_TO_HZ;

	ret = dfc_set_rate(path, (unsigned long)rate);
	if (ret < 0)
		return NOTIFY_BAD;

	return NOTIFY_OK;
}

static struct notifier_block dip_disp_notifier = {
	.notifier_call = dfc_request,
};

ssize_t freq_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	struct mmphw_ctrl *ctrl = dev_get_drvdata(dev);
	struct mmp_path *path = ctrl->path_plats[0].path;
	struct clk *path_clk = path_to_path_plat(path)->clk;
	struct clk *clk;
	unsigned long rate;
	int s = 0;

	clk = path_clk;
	while (!IS_ERR_OR_NULL(clk)) {
		rate = clk_get_rate(clk);
		pr_info("current clk parent : %s ...num:%d...rate: %lu..count : %d.\n",
			clk->name, clk->num_parents, rate, clk->enable_count);
		clk = clk_get_parent(clk);
	}

	rate = dfc_get_rate(path);
	s += sprintf(buf + s, "%ld\n", rate);

	return s;
}

static ssize_t freq_store(
		struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	struct mmphw_ctrl *ctrl = dev_get_drvdata(dev);
	struct mmp_path *path = ctrl->path_plats[0].path;
	unsigned long rate;
	int ret;

	if (size > 30) {
		pr_err("%s size = %zd > max 30 chars\n", __func__, size);
		return size;
	}

	ret = (int)kstrtoul(buf, 0, &rate);
	if (ret < 0) {
		dev_err(dev, "strtoul err.\n");
		return ret;
	}

	if (rate == 0) {
		pr_err("LCD DFC Failed: The rate is 0\n");
		return -1;
	}
	ret = dfc_set_rate(path, rate);
	if (ret < 0)
		return ret;

	return size;
}
DEVICE_ATTR(freq, S_IRUGO | S_IWUSR, freq_show, freq_store);

static int find_best_aclk_index(struct mmphw_ctrl *ctrl, unsigned int rate)
{
	int i, disp_aclk_options;
	disp_aclk_options = ctrl->disp_aclk_options;

	for (i = 0; i < disp_aclk_options; i++)
		if (ctrl->disp_aclk_tbl[i] > rate)
			break;

	/* doesn't find best aclk */
	if (i == disp_aclk_options-1)
		pr_err("%s: fail to find best aclk\n", __func__);
	return i;
}
/*
 * Change ACLK frequency according to active layers and source size
 */
int ctrl_aclk_dfc(struct mmp_path *path)
{
	struct mmphw_path_plat *path_plat = path->plat_data;
	struct mmphw_ctrl *ctrl = path_plat->ctrl;
	struct mmp_overlay overlay;
	struct mmp_mode *mode = &path->mode;
	struct clk *disp_axi, *disp_axi_sclk, *disp_axi_sclk_parent;
	int i, lay = 0, disp_aclk_index;
	int disp_aclk_xres = mode->xres, disp_aclk_yres = mode->yres;
	int disp_aclk_rate = 0;

	/*
	 * Bandwidth calculation as below:
	 * ACLK_theroy = xres*yres*bits_per_pixel*fps/AXI_BUS_WIDTH
	 * Take consideration of efficiency: DDR Efficiency 0.7 and AXI Efficiecny 0.7
	 * ACLK = ACLK_theroy/(0.7*0.7) + delta
	 * bits_per_pixel 32, fps 60, AXI_BUS_WIDTH=128, delta=10MHz=10000000
	 * ACLK = 100* xres *yres*32*60/(128*49) + 10000000
	 *	= (1500/49)*pitch_align(xres)*yres + 10000000 = 31*xres*yres+10000000
	 * 720p panel: aclk = 31*720*1280 + 10000000 = 38.57MHz
	 * 1080p panel: aclk = 31*1080*1920 + 10000000 = 74.28MHz
	 */
	mutex_lock(&ctrl->disp_aclk_lock);
	/* Get ative layers count */
	for (i = 0; i < path->overlay_num; i++) {
		overlay = path->overlays[i];
		if (overlay.status == MMP_ON || overlay.status == MMP_ON_REDUCED
							|| overlay.status == MMP_ON_DMA) {
			lay++;
			/*
			 * Consider transient of burst size data: the min xres(yres) should
			 * be panel x(y) resolution, and if data size in ddr is bigger than
			 * panel size, then use data size resolution.
			 */
			if (overlay.win.xsrc  > disp_aclk_xres)
				disp_aclk_xres = overlay.win.xsrc;
			if (overlay.win.ysrc  > disp_aclk_yres)
				disp_aclk_yres = overlay.win.ysrc;

			disp_aclk_rate += 31 * disp_aclk_xres * disp_aclk_yres + 10000000;
		}
	}

	disp_aclk_index = find_best_aclk_index(ctrl, disp_aclk_rate);

	if (lay > 0) {
		disp_axi_sclk_parent = clk_get(NULL, ctrl->disp_aclk_parent_tbl[disp_aclk_index]);
		if (IS_ERR(disp_axi_sclk_parent)) {
			pr_err("unable to get disp axi sclk parent clk\n");
			mutex_unlock(&ctrl->disp_aclk_lock);
			return -ENOENT;
		}

		disp_axi_sclk = clk_get(NULL, "disp_axi_sclk");
		if (IS_ERR(disp_axi_sclk)) {
			pr_err("unable to get disp axi sclk\n");
			disp_axi_sclk_parent = NULL;
			mutex_unlock(&ctrl->disp_aclk_lock);
			return -ENOENT;
		}
		clk_set_parent(disp_axi_sclk, disp_axi_sclk_parent);

		disp_axi = clk_get(NULL, "disp_axi");
		if (IS_ERR(disp_axi)) {
			pr_err("unable to disp axi clk\n");
			disp_axi_sclk_parent = NULL;
			disp_axi_sclk = NULL;
			mutex_unlock(&ctrl->disp_aclk_lock);
			return -ENOENT;
		} else
			clk_set_rate(disp_axi, ctrl->disp_aclk_tbl[disp_aclk_index]);
	}
	mutex_unlock(&ctrl->disp_aclk_lock);

	return 0;
}

int ctrl_aclk_dfc_init(struct device *dev)
{
#ifdef CONFIG_OF
	int i, j, count, rate, ret;
	int disp_aclk_div_max;
	int disp_aclk_options, disp_aclk_min;
	const char *disp_aclk_parent, *disp_aclk_min_parent;
	struct clk *clk1;
	struct device_node *np = dev->of_node;
	struct mmphw_ctrl *ctrl = dev_get_drvdata(dev);

	mutex_init(&ctrl->disp_aclk_lock);
	/* Can add it to dts for different platforms in the future */
	ctrl->disp_aclk_div_width = 3;
	disp_aclk_div_max = (1 << ctrl->disp_aclk_div_width) - 1;

	count = of_property_count_strings(np, "marvell,disp-aclk-parent");
	if (count < 0) {
		pr_err("%s: marvell,disp-axi-parent property missing or empty\n", __func__);
		return -ENOENT;
	}
	for (i = 0; i < count; i++) {
		ret = of_property_read_string_index(np, "marvell,disp-aclk-parent",
							i, &disp_aclk_parent);
		if (ret < 0)
			disp_aclk_parent = disp_aclk_parent_default;

		clk1 = clk_get(NULL, disp_aclk_parent);
		rate = clk1->rate;
		for (j = 1; j <= disp_aclk_div_max; j++) {
			ctrl->disp_aclk_tbl[i*disp_aclk_div_max + j - 1] = (rate - 1)/j + 1;
			ctrl->disp_aclk_parent_tbl[i*disp_aclk_div_max + j - 1] = disp_aclk_parent;
		}
	}

	/* Sort disp_aclk by asending */
	disp_aclk_options = count * disp_aclk_div_max;
	ctrl->disp_aclk_options = disp_aclk_options;
	for (i = 0; i < disp_aclk_options; i++)
		/* Find the min clk of ctrl->disp_aclk_tbl every cycle */
		for (j = i+1; j < disp_aclk_options; j++)
			if (ctrl->disp_aclk_tbl[j] < ctrl->disp_aclk_tbl[i]) {
				/* switch disp_aclk rate and parent */
				disp_aclk_min = ctrl->disp_aclk_tbl[j];
				ctrl->disp_aclk_tbl[j] = ctrl->disp_aclk_tbl[i];
				ctrl->disp_aclk_tbl[i] = disp_aclk_min;
				/* switch disp_aclk parent */
				disp_aclk_min_parent = ctrl->disp_aclk_parent_tbl[j];
				ctrl->disp_aclk_parent_tbl[j] = ctrl->disp_aclk_parent_tbl[i];
				ctrl->disp_aclk_parent_tbl[i] = disp_aclk_min_parent;
			}
#endif
	return 0;
}

void ctrl_dfc_init(struct device *dev)
{
	struct mmphw_ctrl *ctrl = dev_get_drvdata(dev);
	struct mmp_path *path = ctrl->path_plats[0].path;
	struct clk *path_clk = path_to_path_plat(path)->clk;
	struct mmp_vsync_notifier_node *dfc_notifier_node = NULL;
	unsigned int apmu_lcd;
	int count, i, val;
	struct device_node *np = of_find_node_by_name(NULL, "disp_apmu");
	const char *clk_name = NULL;

	if (np && IS_ENABLED(CONFIG_OF)) {
		if (of_property_read_u32(np, "apmu-reg", &apmu_lcd)) {
			pr_err("%s: No apmu_reg in dts\n", __func__);
			return;
		}
		if (of_property_read_u32(np, "clksrc-bit", &lcd_clk_en)) {
			pr_err("%s: No clksrc choose bit in dts\n", __func__);
			return;
		}

		if (of_property_read_u32(np, "plat", &plat)) {
			pr_err("%s: No plat id in dts\n", __func__);
			return;
		}
		count = of_property_count_strings(np, "parent1-clk-tbl");
		if (count < 0) {
			pr_err("%s: parent1_names property missing or empty\n", __func__);
			return;
		}
		for (i = 0; i < count; i++) {
			if (of_property_read_string_index(np, "parent1-clk-tbl", i, &clk_name))
				continue;
			strcpy(parent1_clk_tbl[i], clk_name);
		}

		parent2_count = of_property_count_strings(np, "parent2-clk-tbl");
		if (count < 0) {
			pr_err("%s: parent2_names property missing or empty\n", __func__);
			return;
		}
		for (i = 0; i < parent2_count; i++) {
			if (of_property_read_string_index(np, "parent2-clk-tbl", i, &clk_name))
				continue;
			strcpy(parent2_clk_tbl[i], clk_name);
		}
	} else {
		pr_err("%s: dts is not suport, or disp_apmu_ver missing\n", __func__);
		return;
	}

	dfc_notifier_node = devm_kzalloc(dev, sizeof(*dfc_notifier_node), GFP_KERNEL);
	if (dfc_notifier_node == NULL) {
		pr_err("dfc alloc failure\n");
		return;
	}

	dfc_notifier_node->cb_notify = dfc_handle;
	dfc_notifier_node->cb_data = dev;
	mmp_path_register_special_vsync_cb(path, dfc_notifier_node);

	create_parent_tbl(path_clk);
	atomic_set(&ctrl->dfc.commit, 0);
	spin_lock_init(&ctrl->dfc.lock);
	INIT_LIST_HEAD(&ctrl->dfc_list.queue);

	ctrl->dfc.apmu_reg = ioremap(apmu_lcd , 4);
	ctrl->dfc.sclk_reg = ctrl->reg_base + LCD_PN_SCLK;

	sclk_div = readl_relaxed(ctrl->dfc.sclk_reg);
	pll3_vco_default = clk_get_rate(clk_get(NULL, "pll3_vco"));
	pll3_default = clk_get_rate(clk_get(NULL, "pll3"));

	device_create_file(dev, &dev_attr_freq);

	path->ops.set_dfc_rate = dfc_set_rate;
	path->ops.get_dfc_rate = dfc_get_rate;

	/*
	 * FIXME: get original rate before dfc, so we can
	 * set the original rate after dfc.
	 */
	original_rate = clk_get_rate(clk_get(NULL, "dsi_sclk"));
	val = sclk_div;
	val &= DSI1_BITCLK_DIV_MASK;
	val = val >> 8;
	original_rate = original_rate/val;


	dip_register_notifier(&dip_disp_notifier, 0);
}
