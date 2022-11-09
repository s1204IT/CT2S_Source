/*
 * bq24160 charger driver
 *
 * Copyright (C) 2011-2012  Pali Rohár <pali.rohar@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef BQ24160_CHARGER_H
#define BQ24160_CHARGER_H

struct bq24160_platform_data {
	int interval;
	int gpio_int;
#ifdef CONFIG_USB_MV_OTG
	int gpio_vbst;
	int gpio_vbus;
#endif /* CONFIG_USB_MV_OTG */
	u32 edge_wakeup_gpio;
};

#ifdef CONFIG_USB_MV_OTG
extern int bq24160_set_vbus(int on);
extern int bq24160_charger_detect(void);
#endif /* CONFIG_USB_MV_OTG */

#endif
