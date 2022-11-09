/*
 * include/linux/act_monitor.h
 *
 *  Copyright (C) 2012 Marvell International Ltd.
 *  All rights reserved.
 *
 *  2013-01-30  Yifan Zhang <zhangyf@marvell.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 */

#ifndef _ACT_MONITOR_H_
#define _ACT_MONITOR_H_
#include <linux/clk.h>
#include <linux/atomic.h>

#define DDR_FREQ_MAX	10
#define AM_TABLE_END	(~1)

struct am_frequency_table {
	unsigned int index;
	unsigned long frequency;
};

struct act_mon_platform_data {
	const char *clk_name;
	struct am_frequency_table *freq_table;
	struct devfreq_pm_qos_table *qos_list;
	u32 high_thr;
	u32 low_thr;
};

struct act_monitor_info {
	int irq;
	void *act_mon_base;
	struct clk *ddr_clk;
	u32 ddr_freq_tbl[DDR_FREQ_MAX];
	u32 ddr_freq_tbl_len;
	struct devfreq_pm_qos_table *qos_table;

	/* used for debug interface */
	atomic_t is_disabled;
	u32 min_freq;
	u32 max_freq;
};
extern void mck5activity_moniter_window(u32 dclk);
extern void mck5_set_threshold(u32 up_down, u32 val);
#endif
