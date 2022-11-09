/*
 * Copyright (C) 2014 Marvell Technology Group Ltd.
 *
 * Authors: Jane Li <jiele@marvell.com>
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

#ifndef __POWER_DOMAIN_MMP_H
#define __POWER_DOMAIN_MMP_H

extern bool gc3d_pwr_is_on(void);
extern bool gc2d_pwr_is_on(void);
extern bool gcsh_pwr_is_on(void);
extern bool vpu_pwr_is_on(void);

#endif
