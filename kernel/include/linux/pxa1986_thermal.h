/*
 * PXA1986 Thermal Implementation
 *
 * Copyright (C) 2012 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __PXA1986_THERMAL_H_
#define __PXA1986_THERMAL_H_

#include <linux/thermal.h>

#define TRIP_POINTS_NUM		3

struct trip_point_info {
	int temperature;
	char cool_dev_type[THERMAL_NAME_LENGTH];
};

struct pxa1986_thermal_cooling_dev_info {
	char type[THERMAL_NAME_LENGTH];
	void (*set_state)(unsigned int on);
};

struct pxa1986_thermal_platform_data {
	char name[THERMAL_NAME_LENGTH];
	struct trip_point_info trip_points[TRIP_POINTS_NUM];
};

void pxa1986_thermal_cooling_devices_register(
	struct pxa1986_thermal_cooling_dev_info *cool_dev_info,
	unsigned int count);

#endif
