/*
 * Copyright (C) 2014 Marvell Technology Group Ltd.
 *
 * Authors: Chao Xie <chao.xie@marvell.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */


#ifndef __MMP_POWER_DOMAIN_H
#define __MMP_POWER_DOMAIN_H

int mmp_pd_init(struct generic_pm_domain *genpd,
	struct dev_power_governor *gov, bool is_off);

extern struct generic_pm_domain *clk_to_genpd(const char *name);
#endif
