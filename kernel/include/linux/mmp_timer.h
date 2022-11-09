/*
 * mmp_timer: Soc timer driver for mmp architecture.
 *
 * Copyright (C) 2008 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MMP_TIMER_H__
#define __LINUX_MMP_TIMER_H__

int __init mmp_timer_init(int id, void __iomem *base,
				unsigned int flag, unsigned int fc_freq,
				unsigned int apb_freq, unsigned int crsr_off);

int __init mmp_counter_clocksource_init(int tid, int cid, int rating,
				unsigned int freq);
int __init mmp_counter_timer_delay_init(int tid, int cid, unsigned int freq);
int __init mmp_counter_clockevent_init(int tid, int cid, int rating, int irq,
				int freq, int dynirq, unsigned int cpu);

#endif /* __LINUX_MMP_TIMER_H__ */
