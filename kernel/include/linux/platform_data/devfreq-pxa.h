/*
 * devfreq: Generic Dynamic Voltage and Frequency Scaling (DVFS) Framework
 *	    for Non-CPU Devices.
 *
 * Copyright (C) 2012 Marvell
 *	Xiaoguang Chen <chenxg@marvell.com>
 *	Qiming Wu <wuqm@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __PXA_DEVFREQ_H__
#define __PXA_DEVFREQ_H__

struct devfreq_platform_data {
	const char *clk_name;
	unsigned int freq_tbl_len;
	unsigned int *freq_table;
};

/* calculate workload according to busy and total time, unit percent */
static inline unsigned int cal_workload(unsigned long busy_time,
	unsigned long total_time)
{
	u64 tmp0, tmp1;

	if (!total_time || !busy_time)
		return 0;
	tmp0 = busy_time * 100;
	tmp1 = div_u64(tmp0, total_time);
	return (unsigned int)tmp1;
}


#endif
