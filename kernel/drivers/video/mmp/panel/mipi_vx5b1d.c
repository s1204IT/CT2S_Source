/*
 * Copyright (C) 2014-2015 SANYO Techno Solutions Tottori Co., Ltd.
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

/*
 * This driver is based on mipi_common.c and mipi_lg4951.c.
 *   Copyright (C) 2013 Marvell Technology Group Ltd.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/lcd.h>
#include <video/mmp_disp.h>
#include <video/mipi_display.h>

extern char panel_type[20];

#define VX5B1D_BRIGHTNESS_STEPS		40
#define VX5B1D_BRIGHTNESS_DEFAULT	173
#define VX5B1D_BRIGHTNESS_MAX		255

enum vx5b1d_vee_level {
	VX5B1D_VEE_OFF,
	VX5B1D_VEE_ON,
	VX5B1D_VEE_MAX
};

#define VX5B1D_VEE_LEVEL_DEFAULT	VX5B1D_VEE_ON

struct vx5b1d_plat_data {
	struct mmp_panel *panel;
	struct mmp_mode mode;
	u32 reset_gpio;
	u32 lcd_power_on;
	u32 lcd_pwr_on;
	u32 lcd_backlight_on;
	u32 lcd_power_type_pin;
	int lcd_power_type;
	int lcd_orient;
	struct i2c_adapter *i2c_adapter;
	u8 i2c_devaddr;
	struct lcd_device *lcd;
	struct backlight_device *bl;
	enum vx5b1d_vee_level vee_level;
	int lcd_power;
	int lowbattery;
	int step;
	u16 sysfs_reg_addr;
	bool initialized;
	int spi;
	int micom;
};

/*******************************************************************************
 * VX5B1D Initialization Sequence
 ******************************************************************************/
static const u32 vx5b1d_init_seq[] =
{
0x700, 0x34900040,
0x704, 0x102ED,
0x70C, 0x00004604,
0x710, 0x2C0000B,
0x714, 0x00,
0x718, 0x00000102,
0x71C, 0xA8002F,
0x720, 0x0,
0x154, 0x00000000,
0x154, 0x80000000,
0x700, 0x34900040,
//0x70C, 0x00005E6E,
0x70C, 0x00005E42,
0x718, 0x00000202,
0x154, 0x00000000,
0x154, 0x80000000,
0x120, 0x5,
0x124, 0x13990500,
0x128, 0x102803,
0x12C, 0x27,
//0x124, 0x14190500,
//0x128, 0x302803,
//0x12C, 0x28,
0x130, 0x3C18,
0x134, 0x00000005,
//0x138, 0xFF8000,
0x138, 0xDD8000,
//0x13C, 0x100,
0x13C, 0x1000100,
0x140, 0x10000,
//0x20C, 0x2,
0x20C, 0x1A,
//0x20C, 0x3A,
0x21C, 0x514,
//0x224, 0x0,
0x224, 0x1,
0x228, 0x320000,
0x22C, 0xFF08,
0x230, 0x1,
0x234, 0xCA033E10,
0x238, 0x00000060,
0x23C, 0x82E86030,
0x244, 0x001E0285,
0x258, 0xA004E,
0x158, 0x0,
0x158, 0x1,
0x37C, 0x00001063,
0x380, 0x82A86030,
0x384, 0x2861408B,
0x388, 0x00130285,
0x38C, 0x10630009,
0x394, 0x400B82A8,
0x600, 0x16CC78C,
0x604, 0x3FFFFC00,
0x608, 0x9EC,
0x154, 0x00000000,
0x154, 0x80000000,
};

static const u32 vx5b1d_init_seq_p[] =
{
#if 1 //noise
0x700, 0x34900040,
0x704, 0x1039A,
0x70C, 0x00004600,
0x710, 0x02CF000F,
0x714, 0x00,
0x718, 0x00000102,
0x71C, 0xA8002F,
0x720, 0x0,
0x154, 0x00000000,
0x154, 0x80000000,
0x700, 0x34900040,
0x70C, 0x00005E62,
0x718, 0x00000202,
0x154, 0x00000000,
0x154, 0x80000000,
0x120, 0x5,
0x124, 0xA80320,
0x128, 0x104815,
0x12C, 0x4E,
0x130, 0x3C18,
0x134, 0x00000005,
0x138, 0xFF8000,
0x13C, 0x100,
0x140, 0x10000,
0x20C, 0x1A,
0x21C, 0x514,
0x224, 0x1,
0x228, 0x320000,
0x22C, 0xFF09,
0x230, 0x1,
0x234, 0xCA033E10,
0x238, 0x00000060,
0x23C, 0x82E86030,
0x244, 0x001E0285,
0x258, 0x20002,
0x158, 0x0,
0x158, 0x1,
0x37C, 0x00001063,
0x380, 0x82A86030,
0x384, 0x2861408B,
0x388, 0x00130285,
0x38C, 0x10630009,
0x394, 0x400B82A8,
0x600, 0x16CC78C,
0x604, 0x3FFFFC00,
0x608, 0x9EC,
0x154, 0x00000000,
0x154, 0x80000000,
#else
0x700 , 0x34900040 ,
0x704 , 0x102FE ,
0x70C , 0x00004600 ,
0x710 , 0x02CF000F ,
0x714 , 0x0 ,
0x718 , 0x00000102 ,
0x71C , 0xA8002F ,
0x720 , 0x0 ,
0x154 , 0x00000000 ,
0x154 , 0x80000000 ,
0x700 , 0x34900040 ,
0x70C , 0x00004E62 ,
0x718 , 0x00000202 ,
0x154 , 0x00000000 ,
0x154 , 0x80000000 ,
0x120 , 0x5 ,
0x124 , 0x5280320 ,
0x128 , 0x105401 ,
0x12C , 0x3B ,
0x130 , 0x3C18 ,
0x134 , 0x00000005 ,
0x138 , 0xFD8000 ,
0x13C , 0x100 ,
0x140 , 0x10000 ,
0x20C , 0x1A ,
0x21C , 0x514 ,
0x224 , 0x0 ,
0x228 , 0x320000 ,
0x22C , 0xFF09 ,
0x230 , 0x1 ,
0x234 , 0xCA033E10 ,
0x238 , 0x00000060 ,
0x23C , 0x82E86030 ,
0x240 , 0x2861409C ,//7
0x244 , 0x001E0785 ,//7
//0x244 , 0x001E0285 ,
0x258 , 0x10014 ,
0x158 , 0x0 ,
0x158 , 0x1 ,
0x374 , 0xEA033A10 , // Charge Pump 5uA
0x37C , 0x00001063 ,
0x380 , 0x82A86030 ,
0x384 , 0x2861408B ,
0x388 , 0x00130285 ,
0x38C , 0x10630009 ,
0x394 , 0x400B82A8 ,
0x600 , 0x16CC78C ,
0x604 , 0x3FFFFFE0 ,
0x608 , 0x9EC ,
0x154 , 0x00000000 ,
0x154 , 0x80000000 ,
#endif
};

static const u32 vx5b1d_init_seq_p_spi[] =
{
0x700 , 0x34900070 ,
0x704 , 0x10310 ,
0x70C , 0x00004600 ,
0x710 , 0x024F000F ,
0x714 , 0x0 ,
0x718 , 0x00000102 ,
0x71C , 0x98282F ,
0x720 , 0x0 ,
0x154 , 0x00000000 ,
0x154 , 0x80000000 ,
0x700 , 0x34900070 ,
0x70C , 0x00005E62 ,
0x718 , 0x00000202 ,
0x154 , 0x00000000 ,
0x154 , 0x80000000 ,
0x120 , 0x5 ,
0x124 , 0x5280320 ,
0x128 , 0x104015 ,
0x12C , 0x3B ,
0x130 , 0x3C18 ,
0x134 , 0x00000005 ,
0x138 , 0x998000 ,
0x13C , 0x61000000 ,
0x140 , 0x10000 ,
0x20C , 0x1A ,
0x21C , 0x514 ,
0x224 , 0x0 ,
0x228 , 0x320000 ,
0x22C , 0xFF09 ,
0x230 , 0x1 ,
0x234 , 0xCA033E10 ,
0x238 , 0x00000060 ,
0x23C , 0x82E86030 ,
0x240 , 0x2861409C ,//7
0x244 , 0x001E0785 ,//7
//0x244 , 0x001E0285 ,
0x258 , 0x10014 ,
0x158 , 0x0 ,
0x158 , 0x1 ,
0x374 , 0xEA033A10 , // Charge Pump 5uA
0x37C , 0x00001063 ,
0x380 , 0x82A86030 ,
0x384 , 0x2861408B ,
0x388 , 0x00130285 ,
0x38C , 0x10630009 ,
0x394 , 0x400B82A8 ,
0x600 , 0x16CC78C ,
0x604 , 0x3FFFFFE0 ,
0x608 , 0x9EC ,
0x154 , 0x00000000 ,
0x154 , 0x80000000 ,
};

static const u32 vx5b1d_init_seq_p_micom[] =
{
0x700 , 0x34900040 ,
0x704 , 0x10310 ,
0x70C , 0x00004600 ,
0x710 , 0x024F000F ,
0x714 , 0x0 ,
0x718 , 0x00000102 ,
0x71C , 0xA8002F ,
0x720 , 0x0 ,
0x154 , 0x00000000 ,
0x154 , 0x80000000 ,
0x700 , 0x34900040 ,
0x70C , 0x00004E62 ,
0x718 , 0x00000202 ,
0x154 , 0x00000000 ,
0x154 , 0x80000000 ,
0x120 , 0x5 ,
0x124 , 0x5280320 ,
0x128 , 0x104015 ,
0x12C , 0x3B ,
0x130 , 0x3C18 ,
0x134 , 0x00000005 ,
0x138 , 0xFD8000 ,
0x13C , 0x100 ,
0x140 , 0x10000 ,
0x20C , 0x1A ,
0x21C , 0x514 ,
0x224 , 0x0 ,
0x228 , 0x320000 ,
0x22C , 0xFF09 ,
0x230 , 0x1 ,
0x234 , 0xCA033E10 ,
0x238 , 0x00000060 ,
0x23C , 0x82E86030 ,
0x240 , 0x2861409C ,
0x244 , 0x001E0785 ,
0x258 , 0x10014 ,
0x158 , 0x0 ,
0x158 , 0x1 ,
0x37C , 0x00001063 ,
0x380 , 0x82A86030 ,
0x384 , 0x2861408B ,
0x388 , 0x00130285 ,
0x38C , 0x10630009 ,
0x394 , 0x400B82A8 ,
0x600 , 0x16CC78C ,
0x604 , 0x3FFFFFE0 ,
0x608 , 0x9EC ,
0x154 , 0x00000000 ,
0x154 , 0x80000000 ,
};

static const struct {
	u16 pwm;
	u16 vee;
} vx5b1d_brightness[VX5B1D_VEE_MAX][VX5B1D_BRIGHTNESS_STEPS] = {
	{	/* VX5B1D_VEE_OFF */
		/*  0 */	{ 0x0000, 0x0000 },
		/*  1 */	{ 0x039A, 0x0000 },
		/*  2 */	{ 0x03D8, 0x0000 },
		/*  3 */	{ 0x041B, 0x0000 },
		/*  4 */	{ 0x0462, 0x0000 },
		/*  5 */	{ 0x04AE, 0x0000 },
		/*  6 */	{ 0x04FF, 0x0000 },
		/*  7 */	{ 0x0556, 0x0000 },
		/*  8 */	{ 0x05B3, 0x0000 },
		/*  9 */	{ 0x0616, 0x0000 },
		/* 10 */	{ 0x067F, 0x0000 },
		/* 11 */	{ 0x06F0, 0x0000 },
		/* 12 */	{ 0x0768, 0x0000 },
		/* 13 */	{ 0x07D1, 0x0000 },
		/* 14 */	{ 0x086B, 0x0000 },
		/* 15 */	{ 0x0912, 0x0000 },
		/* 16 */	{ 0x09C6, 0x0000 },
		/* 17 */	{ 0x0A88, 0x0000 },
		/* 18 */	{ 0x0B59, 0x0000 },
		/* 19 */	{ 0x0C3A, 0x0000 },
		/* 20 */	{ 0x0D2D, 0x0000 },
		/* 21 */	{ 0x0E32, 0x0000 },
		/* 22 */	{ 0x0F4C, 0x0000 },
		/* 23 */	{ 0x107B, 0x0000 },
		/* 24 */	{ 0x11C2, 0x0000 },
		/* 25 */	{ 0x1322, 0x0000 },
		/* 26 */	{ 0x149D, 0x0000 },
		/* 27 */	{ 0x1636, 0x0000 },
		/* 28 */	{ 0x17EE, 0x0000 },
		/* 29 */	{ 0x19C9, 0x0000 },
		/* 30 */	{ 0x1BC8, 0x0000 },
		/* 31 */	{ 0x1DEF, 0x0000 },
		/* 32 */	{ 0x2041, 0x0000 },
		/* 33 */	{ 0x22C0, 0x0000 },
		/* 34 */	{ 0x2572, 0x0000 },
		/* 35 */	{ 0x2858, 0x0000 },
		/* 36 */	{ 0x2B78, 0x0000 },
		/* 37 */	{ 0x2ED6, 0x0000 },
		/* 38 */	{ 0x3277, 0x0000 },
		/* 39 */	{ 0x3660, 0x0000 },
	},
	{	/* VX5B1D_VEE_ON */
		/*  0 */	{ 0x0000, 0x0000 },
		/*  1 */	{ 0x039A, 0x1C00 },
		/*  2 */	{ 0x03D8, 0x1C00 },
		/*  3 */	{ 0x041B, 0x1C00 },
		/*  4 */	{ 0x0462, 0x1C00 },
		/*  5 */	{ 0x04AE, 0x1C00 },
		/*  6 */	{ 0x04FF, 0x1C00 },
		/*  7 */	{ 0x0556, 0x1C00 },
		/*  8 */	{ 0x05B3, 0x1C00 },
		/*  9 */	{ 0x0616, 0x1C00 },
		/* 10 */	{ 0x067F, 0x1C00 },
		/* 11 */	{ 0x06F0, 0x1C00 },
		/* 12 */	{ 0x0768, 0x1C00 },
		/* 13 */	{ 0x07D1, 0x1C00 },
		/* 14 */	{ 0x086B, 0x1C00 },
		/* 15 */	{ 0x0912, 0x1C00 },
		/* 16 */	{ 0x09C6, 0x1C00 },
		/* 17 */	{ 0x0A88, 0x1C00 },
		/* 18 */	{ 0x0B59, 0x1C00 },
		/* 19 */	{ 0x0C3A, 0x1D00 },
		/* 20 */	{ 0x0D2D, 0x1E00 },
		/* 21 */	{ 0x0E32, 0x2100 },
		/* 22 */	{ 0x0F4C, 0x2300 },
		/* 23 */	{ 0x107B, 0x2400 },
		/* 24 */	{ 0x11C2, 0x2700 },
		/* 25 */	{ 0x1322, 0x2B00 },
		/* 26 */	{ 0x149D, 0x2E00 },
		/* 27 */	{ 0x1636, 0x3200 },
		/* 28 */	{ 0x17EE, 0x3800 },
		/* 29 */	{ 0x19C9, 0x3A00 },
		/* 30 */	{ 0x1BC8, 0x3D00 },
		/* 31 */	{ 0x1DEF, 0x3D00 },
		/* 32 */	{ 0x2041, 0x3D00 },
		/* 33 */	{ 0x22C0, 0x3C00 },
		/* 34 */	{ 0x2572, 0x3A00 },
		/* 35 */	{ 0x2858, 0x3900 },
		/* 36 */	{ 0x2B78, 0x3800 },
		/* 37 */	{ 0x2ED6, 0x3600 },
		/* 38 */	{ 0x3277, 0x3500 },
		/* 39 */	{ 0x3660, 0x3300 },
	},
};

static const struct {
	u16 pwm;
	u16 vee;
} vx5b1d_brightness_cpt[VX5B1D_VEE_MAX][VX5B1D_BRIGHTNESS_STEPS] = {
	{	/* VX5B1D_VEE_OFF */
		/*  0 */	{ 0x0000, 0x0000 },
		/*  1 */	{ 0x02B8, 0x0000 },
		/*  2 */	{ 0x02F5, 0x0000 },
		/*  3 */	{ 0x0337, 0x0000 },
		/*  4 */	{ 0x0380, 0x0000 },
		/*  5 */	{ 0x03CF, 0x0000 },
		/*  6 */	{ 0x0424, 0x0000 },
		/*  7 */	{ 0x0482, 0x0000 },
		/*  8 */	{ 0x04E7, 0x0000 },
		/*  9 */	{ 0x0556, 0x0000 },
		/* 10 */	{ 0x05CE, 0x0000 },
		/* 11 */	{ 0x0651, 0x0000 },
		/* 12 */	{ 0x06DF, 0x0000 },
		/* 13 */	{ 0x077A, 0x0000 },
		/* 14 */	{ 0x0833, 0x0000 },
		/* 15 */	{ 0x08FE, 0x0000 },
		/* 16 */	{ 0x09DD, 0x0000 },
		/* 17 */	{ 0x0AD1, 0x0000 },
		/* 18 */	{ 0x0BDD, 0x0000 },
		/* 19 */	{ 0x0D03, 0x0000 },
		/* 20 */	{ 0x0E46, 0x0000 },
		/* 21 */	{ 0x0FA8, 0x0000 },
		/* 22 */	{ 0x112C, 0x0000 },
		/* 23 */	{ 0x12D5, 0x0000 },
		/* 24 */	{ 0x14A8, 0x0000 },
		/* 25 */	{ 0x16A8, 0x0000 },
		/* 26 */	{ 0x18D9, 0x0000 },
		/* 27 */	{ 0x1B41, 0x0000 },
		/* 28 */	{ 0x1D5C, 0x0000 },
		/* 29 */	{ 0x1F9B, 0x0000 },
		/* 30 */	{ 0x21FC, 0x0000 },
		/* 31 */	{ 0x2491, 0x0000 },
		/* 32 */	{ 0x278E, 0x0000 },
		/* 33 */	{ 0x2A31, 0x0000 },
		/* 34 */	{ 0x2D4B, 0x0000 },
		/* 35 */	{ 0x309F, 0x0000 },
		/* 36 */	{ 0x3432, 0x0000 },
		/* 37 */	{ 0x3808, 0x0000 },
		/* 38 */	{ 0x3C26, 0x0000 },
		/* 39 */	{ 0x4092, 0x0000 },
	},
	{	/* VX5B1D_VEE_ON */
		/*  0 */	{ 0x0000, 0x0000 },
		/*  1 */	{ 0x02B8, 0x2800 },
		/*  2 */	{ 0x02F5, 0x2800 },
		/*  3 */	{ 0x0337, 0x2800 },
		/*  4 */	{ 0x0380, 0x2800 },
		/*  5 */	{ 0x03CF, 0x2800 },
		/*  6 */	{ 0x0424, 0x2800 },
		/*  7 */	{ 0x0482, 0x2800 },
		/*  8 */	{ 0x04E7, 0x2800 },
		/*  9 */	{ 0x0556, 0x2800 },
		/* 10 */	{ 0x05CE, 0x2800 },
		/* 11 */	{ 0x0651, 0x2800 },
		/* 12 */	{ 0x06DF, 0x2800 },
		/* 13 */	{ 0x077A, 0x2800 },
		/* 14 */	{ 0x0833, 0x2800 },
		/* 15 */	{ 0x08FE, 0x2800 },
		/* 16 */	{ 0x09DD, 0x2800 },
		/* 17 */	{ 0x0AD1, 0x2800 },
		/* 18 */	{ 0x0BDD, 0x2800 },
		/* 19 */	{ 0x0D03, 0x2A00 },
		/* 20 */	{ 0x0E46, 0x2C00 },
		/* 21 */	{ 0x0FA8, 0x3000 },
		/* 22 */	{ 0x112C, 0x3200 },
		/* 23 */	{ 0x12D5, 0x3400 },
		/* 24 */	{ 0x14A8, 0x3800 },
		/* 25 */	{ 0x16A8, 0x3E00 },
		/* 26 */	{ 0x18D9, 0x4200 },
		/* 27 */	{ 0x1B41, 0x4800 },
		/* 28 */	{ 0x1D5C, 0x5000 },
		/* 29 */	{ 0x1F9B, 0x5400 },
		/* 30 */	{ 0x21FC, 0x5800 },
		/* 31 */	{ 0x2491, 0x5800 },
		/* 32 */	{ 0x278E, 0x5800 },
		/* 33 */	{ 0x2A31, 0x5600 },
		/* 34 */	{ 0x2D4B, 0x5400 },
		/* 35 */	{ 0x309F, 0x5200 },
		/* 36 */	{ 0x3432, 0x5000 },
		/* 37 */	{ 0x3808, 0x4E00 },
		/* 38 */	{ 0x3C26, 0x4C00 },
		/* 39 */	{ 0x4092, 0x4A00 },
	},
};

static const struct {
	u16 pwm;
	u16 vee;
} vx5b1d_brightness_cpt_spi[VX5B1D_VEE_MAX][VX5B1D_BRIGHTNESS_STEPS] = {
	{	/* VX5B1D_VEE_OFF */
		/*  0 */	{ 0x0000, 0x0000 },
		/*  1 */	{ 0x05B7, 0x0000 },
		/*  2 */	{ 0x0615, 0x0000 },
		/*  3 */	{ 0x067A, 0x0000 },
		/*  4 */	{ 0x06E5, 0x0000 },
		/*  5 */	{ 0x0756, 0x0000 },
		/*  6 */	{ 0x07CF, 0x0000 },
		/*  7 */	{ 0x0850, 0x0000 },
		/*  8 */	{ 0x08D9, 0x0000 },
		/*  9 */	{ 0x096B, 0x0000 },
		/* 10 */	{ 0x0A06, 0x0000 },
		/* 11 */	{ 0x0AAC, 0x0000 },
		/* 12 */	{ 0x0B5C, 0x0000 },
		/* 13 */	{ 0x0C18, 0x0000 },
		/* 14 */	{ 0x0CF6, 0x0000 },
		/* 15 */	{ 0x0DE4, 0x0000 },
		/* 16 */	{ 0x0EE3, 0x0000 },
		/* 17 */	{ 0x0FF4, 0x0000 },
		/* 18 */	{ 0x111A, 0x0000 },
		/* 19 */	{ 0x1254, 0x0000 },
		/* 20 */	{ 0x13A4, 0x0000 },
		/* 21 */	{ 0x150D, 0x0000 },
		/* 22 */	{ 0x1690, 0x0000 },
		/* 23 */	{ 0x182F, 0x0000 },
		/* 24 */	{ 0x19EB, 0x0000 },
		/* 25 */	{ 0x1BC7, 0x0000 },
		/* 26 */	{ 0x1DC5, 0x0000 },
		/* 27 */	{ 0x1FE8, 0x0000 },
		/* 28 */	{ 0x2232, 0x0000 },
		/* 29 */	{ 0x24A7, 0x0000 },
		/* 30 */	{ 0x2748, 0x0000 },
		/* 31 */	{ 0x2A1A, 0x0000 },
		/* 32 */	{ 0x2D1F, 0x0000 },
		/* 33 */	{ 0x305C, 0x0000 },
		/* 34 */	{ 0x33D4, 0x0000 },
		/* 35 */	{ 0x378D, 0x0000 },
		/* 36 */	{ 0x3B89, 0x0000 },
		/* 37 */	{ 0x3FCF, 0x0000 },
		/* 38 */	{ 0x4463, 0x0000 },
		/* 39 */	{ 0x494B, 0x0000 },
	},
	{	/* VX5B1D_VEE_ON */
		/*  0 */	{ 0x0000, 0x0000 },
		/*  1 */	{ 0x05B7, 0x2300 },
		/*  2 */	{ 0x0615, 0x2300 },
		/*  3 */	{ 0x067A, 0x2300 },
		/*  4 */	{ 0x06E5, 0x2300 },
		/*  5 */	{ 0x0756, 0x2300 },
		/*  6 */	{ 0x07CF, 0x2300 },
		/*  7 */	{ 0x0850, 0x2300 },
		/*  8 */	{ 0x08D9, 0x2300 },
		/*  9 */	{ 0x096B, 0x2300 },
		/* 10 */	{ 0x0A06, 0x2300 },
		/* 11 */	{ 0x0AAC, 0x2300 },
		/* 12 */	{ 0x0B5C, 0x2300 },
		/* 13 */	{ 0x0C18, 0x2300 },
		/* 14 */	{ 0x0CF6, 0x2300 },
		/* 15 */	{ 0x0DE4, 0x2300 },
		/* 16 */	{ 0x0EE3, 0x2300 },
		/* 17 */	{ 0x0FF4, 0x2300 },
		/* 18 */	{ 0x111A, 0x2300 },
		/* 19 */	{ 0x1254, 0x2500 },
		/* 20 */	{ 0x13A4, 0x2700 },
		/* 21 */	{ 0x150D, 0x2A00 },
		/* 22 */	{ 0x1690, 0x2C00 },
		/* 23 */	{ 0x182F, 0x2E00 },
		/* 24 */	{ 0x19EB, 0x3100 },
		/* 25 */	{ 0x1BC7, 0x3700 },
		/* 26 */	{ 0x1DC5, 0x3A00 },
		/* 27 */	{ 0x1FE8, 0x4000 },
		/* 28 */	{ 0x2232, 0x4700 },
		/* 29 */	{ 0x24A7, 0x4A00 },
		/* 30 */	{ 0x2748, 0x4E00 },
		/* 31 */	{ 0x2A1A, 0x4E00 },
		/* 32 */	{ 0x2D1F, 0x4E00 },
		/* 33 */	{ 0x305C, 0x4C00 },
		/* 34 */	{ 0x33D4, 0x4A00 },
		/* 35 */	{ 0x378D, 0x4800 },
		/* 36 */	{ 0x3B89, 0x4700 },
		/* 37 */	{ 0x3FCF, 0x4500 },
		/* 38 */	{ 0x4463, 0x4300 },
		/* 39 */	{ 0x494B, 0x4100 },
	},
};

static const struct { //inno
	u16 pwm;
	u16 vee;
} vx5b1d_brightness_invert[VX5B1D_VEE_MAX][VX5B1D_BRIGHTNESS_STEPS] = {
	{	/* VX5B1D_VEE_OFF */
		/*  0 */	{ 0x0000, 0x0000 },
		/*  1 */	{ 0x02ED, 0x0000 },
		/*  2 */	{ 0x02EA, 0x0000 },
		/*  3 */	{ 0x02E7, 0x0000 },
		/*  4 */	{ 0x02E4, 0x0000 },
		/*  5 */	{ 0x02E1, 0x0000 },
		/*  6 */	{ 0x02DE, 0x0000 },
		/*  7 */	{ 0x02DB, 0x0000 },
		/*  8 */	{ 0x02D8, 0x0000 },
		/*  9 */	{ 0x02D5, 0x0000 },
		/* 10 */	{ 0x02D2, 0x0000 },
		/* 11 */	{ 0x02CE, 0x0000 },
		/* 12 */	{ 0x02CB, 0x0000 },
		/* 13 */	{ 0x02C9, 0x0000 },
		/* 14 */	{ 0x02BF, 0x0000 },
		/* 15 */	{ 0x02B5, 0x0000 },
		/* 16 */	{ 0x02AA, 0x0000 },
		/* 17 */	{ 0x029E, 0x0000 },
		/* 18 */	{ 0x0292, 0x0000 },
		/* 19 */	{ 0x0284, 0x0000 },
		/* 20 */	{ 0x0276, 0x0000 },
		/* 21 */	{ 0x0267, 0x0000 },
		/* 22 */	{ 0x0257, 0x0000 },
		/* 23 */	{ 0x0247, 0x0000 },
		/* 24 */	{ 0x0235, 0x0000 },
		/* 25 */	{ 0x0222, 0x0000 },
		/* 26 */	{ 0x020D, 0x0000 },
		/* 27 */	{ 0x01F8, 0x0000 },
		/* 28 */	{ 0x01E1, 0x0000 },
		/* 29 */	{ 0x01C9, 0x0000 },
		/* 30 */	{ 0x01AF, 0x0000 },
		/* 31 */	{ 0x0193, 0x0000 },
		/* 32 */	{ 0x0176, 0x0000 },
		/* 33 */	{ 0x0157, 0x0000 },
		/* 34 */	{ 0x0137, 0x0000 },
		/* 35 */	{ 0x0114, 0x0000 },
		/* 36 */	{ 0x00EE, 0x0000 },
		/* 37 */	{ 0x00C7, 0x0000 },
		/* 38 */	{ 0x009D, 0x0000 },
		/* 39 */	{ 0x0071, 0x0000 },
	},
	{	/* VX5B1D_VEE_ON */
		/*  0 */	{ 0x0000, 0x0000 },
		/*  1 */	{ 0x02ED, 0x4000 },
		/*  2 */	{ 0x02EA, 0x4000 },
		/*  3 */	{ 0x02E7, 0x4000 },
		/*  4 */	{ 0x02E4, 0x4000 },
		/*  5 */	{ 0x02E1, 0x4000 },
		/*  6 */	{ 0x02DE, 0x4000 },
		/*  7 */	{ 0x02DB, 0x4000 },
		/*  8 */	{ 0x02D8, 0x4000 },
		/*  9 */	{ 0x02D5, 0x4000 },
		/* 10 */	{ 0x02D2, 0x4000 },
		/* 11 */	{ 0x02CE, 0x4000 },
		/* 12 */	{ 0x02CB, 0x4000 },
		/* 13 */	{ 0x02C9, 0x4000 },
		/* 14 */	{ 0x02BF, 0x4000 },
		/* 15 */	{ 0x02B5, 0x4000 },
		/* 16 */	{ 0x02AA, 0x4000 },
		/* 17 */	{ 0x029E, 0x4100 },
		/* 18 */	{ 0x0292, 0x4100 },
		/* 19 */	{ 0x0284, 0x4300 },
		/* 20 */	{ 0x0276, 0x4300 },
		/* 21 */	{ 0x0267, 0x4400 },
		/* 22 */	{ 0x0257, 0x4500 },
		/* 23 */	{ 0x0247, 0x4700 },
		/* 24 */	{ 0x0235, 0x4800 },
		/* 25 */	{ 0x0222, 0x4B00 },
		/* 26 */	{ 0x020D, 0x4F00 },
		/* 27 */	{ 0x01F8, 0x5100 },
		/* 28 */	{ 0x01E1, 0x5500 },
		/* 29 */	{ 0x01C9, 0x5B00 },
		/* 30 */	{ 0x01AF, 0x5F00 },
		/* 31 */	{ 0x0193, 0x6000 },
		/* 32 */	{ 0x0176, 0x6000 },
		/* 33 */	{ 0x0157, 0x6000 },
		/* 34 */	{ 0x0137, 0x6000 },
		/* 35 */	{ 0x0114, 0x6000 },
		/* 36 */	{ 0x00EE, 0x6000 },
		/* 37 */	{ 0x00C7, 0x6000 },
		/* 38 */	{ 0x009D, 0x6000 },
		/* 39 */	{ 0x0071, 0x6000 },
	},
};

static int vx5b1d_reg_read16(struct vx5b1d_plat_data *plat, u16 addr, u16 *data)
{
	int ret;
	u8 wbuf[5];
	u8 rbuf[2];
	struct i2c_msg msg[] = {
		{
			.addr = plat->i2c_devaddr,
			.flags = 0,
			.len = sizeof(wbuf),
			.buf = wbuf,
		},
		{
			.addr = plat->i2c_devaddr,
			.flags = I2C_M_RD,
			.len = sizeof(rbuf),
			.buf = rbuf,
		},
	};

	wbuf[0] = 0x0E;	/* I2C Direct Access Read */
	wbuf[1] = (u8)(addr >> 8);
	wbuf[2] = (u8)(addr);
	wbuf[3] = 0x00;
	wbuf[4] = 0x04;

	ret = i2c_transfer(plat->i2c_adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0)
		return ret;

	*data = (rbuf[1] << 8) | rbuf[0];
	return 0;
}

static int vx5b1d_reg_read32(struct vx5b1d_plat_data *plat, u16 addr, u32 *data)
{
	int ret;
	u8 wbuf[5];
	u8 rbuf[4];
	struct i2c_msg msg[] = {
		{
			.addr = plat->i2c_devaddr,
			.flags = 0,
			.len = sizeof(wbuf),
			.buf = wbuf,
		},
		{
			.addr = plat->i2c_devaddr,
			.flags = I2C_M_RD,
			.len = sizeof(rbuf),
			.buf = rbuf,
		},
	};

	wbuf[0] = 0x0E;	/* I2C Direct Access Read */
	wbuf[1] = (u8)(addr >> 8);
	wbuf[2] = (u8)(addr);
	wbuf[3] = 0x00;
	wbuf[4] = 0x04;

	ret = i2c_transfer(plat->i2c_adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0)
		return ret;

	*data = (rbuf[3] << 24) | (rbuf[2] << 16) | (rbuf[1] << 8) | rbuf[0];
	return 0;
}

static int vx5b1d_reg_write16(struct vx5b1d_plat_data *plat, u16 addr, u16 data)
{
	u8 buf[5];
	struct i2c_msg msg[] = {
		{
			.addr = plat->i2c_devaddr,
			.flags = 0,
			.len = sizeof(buf),
			.buf = buf,
		},
	};

	buf[0] = 0x0A;	/* I2C Direct Access Write */
	buf[1] = (u8)(addr >> 8);
	buf[2] = (u8)(addr);
	buf[3] = (u8)(data);
	buf[4] = (u8)(data >> 8);

	return i2c_transfer(plat->i2c_adapter, msg, ARRAY_SIZE(msg));
}

static int vx5b1d_reg_write32(struct vx5b1d_plat_data *plat, u16 addr, u32 data)
{
	u8 buf[7];
	struct i2c_msg msg[] = {
		{
			.addr = plat->i2c_devaddr,
			.flags = 0,
			.len = sizeof(buf),
			.buf = buf,
		},
	};

	buf[0] = 0x0A;	/* I2C Direct Access Write */
	buf[1] = (u8)(addr >>  8);
	buf[2] = (u8)(addr);
	buf[3] = (u8)(data);
	buf[4] = (u8)(data >>  8);
	buf[5] = (u8)(data >> 16);
	buf[6] = (u8)(data >> 24);

	return i2c_transfer(plat->i2c_adapter, msg, ARRAY_SIZE(msg));
}

static int vx5b1d_spi_write(struct vx5b1d_plat_data *plat, u16 addr ,u8 data)
{
	vx5b1d_reg_write32(plat, 0x44, 0xF); //16bit
	vx5b1d_reg_write32(plat, 0x20, 0x2000 | ((addr >> 8) & 0x00FF));
	vx5b1d_reg_write32(plat, 0x40, 0x1); //tx start
	msleep(1);
	vx5b1d_reg_write32(plat, 0x44, 0xF); //16bit
	vx5b1d_reg_write32(plat, 0x20, 0x0000 | (addr & 0x00FF));
	vx5b1d_reg_write32(plat, 0x40, 0x1); //tx start
	msleep(1);
	vx5b1d_reg_write32(plat, 0x44, 0xF); //16bit
	vx5b1d_reg_write32(plat, 0x20, 0x4000 | data);
	vx5b1d_reg_write32(plat, 0x40, 0x1); //tx start
	msleep(1);
}

static void vx5b1d_set_power(struct vx5b1d_plat_data *plat, int power)
{
	if (power) {
		gpio_request(plat->lcd_power_on, "lcd-power-on");
		gpio_direction_output(plat->lcd_power_on, 1);
		gpio_free(plat->lcd_power_on);
		gpio_request(plat->lcd_pwr_on, "lcd-pwr-on");
		gpio_direction_output(plat->lcd_pwr_on, 1);
		gpio_free(plat->lcd_pwr_on);
//		gpio_request(plat->lcd_backlight_on, "lcd-backlight-on");
//		gpio_direction_output(plat->lcd_backlight_on, 1);
//		gpio_free(plat->lcd_backlight_on);
	}
	else {
		gpio_request(plat->lcd_backlight_on, "lcd-backlight-on");
		gpio_direction_output(plat->lcd_backlight_on, 0);
		gpio_free(plat->lcd_backlight_on);
		gpio_request(plat->lcd_pwr_on, "lcd-pwr-on");
		gpio_direction_output(plat->lcd_pwr_on, 0);
		gpio_free(plat->lcd_pwr_on);
		gpio_request(plat->lcd_power_on, "lcd-power-on");
		gpio_direction_output(plat->lcd_power_on, 0);
		gpio_free(plat->lcd_power_on);
	}
	plat->lcd_power = power;
}

static void vx5b1d_set_vee(struct vx5b1d_plat_data *plat, int level)
{
	u16 vee;
	u32 data[2];

	if (!plat->initialized) {
		pr_warn("vx5b1d: device is not initialized.\n");
		return;
	}

	if(plat->lcd_orient == 1) //cpt
		vee = vx5b1d_brightness_cpt[level][plat->step].vee;
	else if(plat->lcd_power_type == 0) //inno
		vee = vx5b1d_brightness_invert[level][plat->step].vee;
	else //ivo
		vee = vx5b1d_brightness[level][plat->step].vee;

	if (0 != vx5b1d_reg_read32(plat, 0x70C, &data[0])) {
		pr_err("vx5b1d: cannot get register (addr=0x70C)\n");
		return;
	}
	if (0 != vx5b1d_reg_read32(plat, 0x710, &data[1])) {
		pr_err("vx5b1d: cannot get register (addr=0x710)\n");
		return;
	}

	vx5b1d_reg_write32(plat, 0x154, 0);
	vx5b1d_reg_write32(plat, 0x174, 0x7F);
	vx5b1d_reg_write32(plat, 0x70C, data[0] | 0x10);
	vx5b1d_reg_write32(plat, 0x710, data[1] | 0x40);
	vx5b1d_reg_write32(plat, 0x154, 0x80000000);
	vx5b1d_reg_write32(plat, 0x410, 0x05E50300);
	vx5b1d_reg_write16(plat, 0x400, (vee != 0)? 0x1F07:0x1F06);
	vx5b1d_reg_write16(plat, 0x402, vee);
	if((plat->spi == 1) && (plat->lcd_orient == 1)) //cpt spi
		vx5b1d_reg_write32(plat, 0x404, 0x55550B00);
	else if(plat->lcd_power_type == 0)//inno
		vx5b1d_reg_write32(plat, 0x404, 0x55550A00);
	else if(plat->lcd_orient == 1)//cpt noise
		vx5b1d_reg_write32(plat, 0x404, 0x55550500);
	else
		vx5b1d_reg_write32(plat, 0x404, 0x55551800);//ivo

	if(plat->lcd_power_type == 0) //inno
		vx5b1d_reg_write32(plat, 0x434, 0x55550039);
	else if((plat->spi == 1) && (plat->lcd_orient == 1)) //cpt spi
		vx5b1d_reg_write32(plat, 0x43A, 0x00807E80);
	else if(plat->lcd_orient == 0)
		vx5b1d_reg_write32(plat, 0x43A, 0x00888083);//ivo

	vx5b1d_reg_write32(plat, 0x154, 0);
	vx5b1d_reg_write32(plat, 0x154, 0x80000000);

	plat->vee_level = level;
}

static void vx5b1d_set_brightness(struct vx5b1d_plat_data *plat, int level)
{
	u32 data;
	u16 pwm;
	int step;

	if (!plat->initialized) {
		pr_warn("vx5b1d: device is not initialized.\n");
		return;
	}
	step = level * VX5B1D_BRIGHTNESS_STEPS / VX5B1D_BRIGHTNESS_MAX;

	if ((step < 0) || (step >= VX5B1D_BRIGHTNESS_STEPS)) {
		pr_err("vx5b1d: invalid brightness \"%d\"\n", level);
		return;
	}

	if(plat->lowbattery && step > 32)
		step = 32;

	plat->step = step;
	if(plat->lcd_orient == 1){ //cpt
		if(plat->spi == 0)
			pwm = vx5b1d_brightness_cpt[plat->vee_level][step].pwm;
		else
			pwm = vx5b1d_brightness_cpt_spi[plat->vee_level][step].pwm;
	}
	else if(plat->lcd_power_type == 0) { //inno
		pwm = vx5b1d_brightness_invert[plat->vee_level][step].pwm;
	}
	else { //ivo
		pwm = vx5b1d_brightness[plat->vee_level][step].pwm;
	}

	if (0 != vx5b1d_reg_read32(plat, 0x138, &data)) {
		pr_err("vx5b1d: cannot get register (addr=0x138)\n");
		return;
	}

	if (step != 0) {
		if(plat->lcd_power_type) {
			if(plat->spi == 0)
				data &= ~0x020000;	// GPIO[2]: output
			else
				data &= ~0x200000;	// GPIO[6]: output
			vx5b1d_reg_write32(plat, 0x138, data);
			if(plat->spi == 0)
				vx5b1d_reg_write32(plat, 0x160, 0x43F8);
			else
				vx5b1d_reg_write32(plat, 0x160, 0x494B);
			vx5b1d_reg_write32(plat, 0x164, pwm);
			vx5b1d_reg_write32(plat, 0x15C, 0x05);
			vx5b1d_reg_write32(plat, 0x114, 0xC6302);	// Enable PWM
		}
		else { //inno
			vx5b1d_reg_write32(plat, 0x160, 870);
			vx5b1d_reg_write32(plat, 0x164, pwm);
			vx5b1d_reg_write32(plat, 0x15C, 0x05);
			vx5b1d_reg_write32(plat, 0x114, 0xC6302);	// Enable PWM
			vx5b1d_reg_write32(plat, 0x13C, 0x1000100);
		}
	}
	else {
		if(plat->lcd_power_type) {
			if(plat->spi == 0)
				data |= 0x020000;	// GPIO[2]: input
			else
				data |= 0x200000;	// GPIO[6]: input
			vx5b1d_reg_write32(plat, 0x138, data);
			vx5b1d_reg_write32(plat, 0x15C, 0x01);
			vx5b1d_reg_write32(plat, 0x114, 0xC6300);	// Disable PWM
		}
		else { //inno
			if(plat->spi == 0)
			{
				data &= ~0x020000;	// GPIO[2]: output
				data |= 0x00004;
			}
			else
			{
				data &= ~0x200000;	// GPIO[6]: output
				data |= 0x00040;
			}
			vx5b1d_reg_write32(plat, 0x138, data);
			if(plat->spi == 0)
				vx5b1d_reg_write32(plat, 0x13C, 0x2000200);
			else
				vx5b1d_reg_write32(plat, 0x13C, 0x8000800);
			vx5b1d_reg_write32(plat, 0x15C, 0x01);
			vx5b1d_reg_write32(plat, 0x114, 0xC6300);	// Disable PWM
		}
	}
	vx5b1d_set_vee(plat, plat->vee_level);
}

///////////////////////////////////////////////////////////////////////////////

static int vx5b1d_get_modelist(struct mmp_panel *panel,
		struct mmp_mode **modelist)
{
	struct vx5b1d_plat_data *plat = panel->plat_data;
	*modelist = &plat->mode;
	return 1;
}

static void vx5b1d_panel_start(struct mmp_panel *panel, int on)
{
	struct vx5b1d_plat_data *plat = panel->plat_data;

	if (on) {
		/* turn on backlight */
		vx5b1d_set_brightness(plat, plat->bl->props.brightness);
		msleep(150);
		gpio_request(plat->lcd_backlight_on, "lcd-backlight-on");
		gpio_direction_output(plat->lcd_backlight_on, 1);
		gpio_free(plat->lcd_backlight_on);
	}
	else {
		/* turn off backlight */
		vx5b1d_set_brightness(plat, 0);
		gpio_request(plat->lcd_backlight_on, "lcd-backlight-on");
		gpio_direction_output(plat->lcd_backlight_on, 0);
		gpio_free(plat->lcd_backlight_on);
	}
}

static void vx5b1d_set_status(struct mmp_panel *panel, int status)
{
	struct vx5b1d_plat_data *plat = panel->plat_data;

	if (status_is_on(status)) {
		/* turn on backlight */
//		vx5b1d_set_brightness(plat, plat->bl->props.brightness);
		vx5b1d_set_power(plat, 4);
	}
	else {
		/* turn off backlight */
//		vx5b1d_set_brightness(plat, 0);
		vx5b1d_set_power(plat, 0);
	}
}

static void vx5b1d_prepare(struct mmp_panel *panel)
{
	struct vx5b1d_plat_data *plat = panel->plat_data;
	int i;

	/* reset device */
	gpio_request(plat->reset_gpio, "vx5b1d-reset");
	gpio_direction_output(plat->reset_gpio, 0);
	msleep(10);
	gpio_direction_output(plat->reset_gpio, 1);
	gpio_free(plat->reset_gpio);

	/* run initialize sequence */
	if(plat->lcd_orient == 0) {
		for (i=0; i<ARRAY_SIZE(vx5b1d_init_seq); i+=2) {
			u16 addr = (u16)vx5b1d_init_seq[i+0];
			u32 data= vx5b1d_init_seq[i+1];
			int ret = vx5b1d_reg_write32(plat, addr, data);
			if (ret < 0) {
				pr_err("cannot write register (addr=0x%04x, data=0x%08x, err=%d)\n",
					   addr, data, ret);
				/* continue */
			}
		}
	}
	else {
		if(plat->spi == 1) {//cpt spi
			for (i=0; i<ARRAY_SIZE(vx5b1d_init_seq_p_spi); i+=2) {
				u16 addr = (u16)vx5b1d_init_seq_p_spi[i+0];
				u32 data = vx5b1d_init_seq_p_spi[i+1];
				if(plat->micom == 0)//micon spi disable
				{
					if(addr == 0x138)
						data = 0xDF800;
					if(addr == 0x13C)
						data = 0x1000000;
					if(addr == 0x140)
						data = 0x1A000;
				}
				int ret = vx5b1d_reg_write32(plat, addr, data);
				if (ret < 0) {
					pr_err("cannot write register (addr=0x%04x, data=0x%08x, err=%d)\n",
						   addr, data, ret);
					/* continue */
				}
			}
			if(plat->micom == 1)
			{
				/* 0xF602 command send*/
				vx5b1d_spi_write(plat, 0xFF00,0xAA);
				vx5b1d_spi_write(plat, 0xFF01,0x55);
				vx5b1d_spi_write(plat, 0xFF02,0x25);
				vx5b1d_spi_write(plat, 0xFF03,0x01);
				vx5b1d_spi_write(plat, 0xF602,0x03);
			}
		}
		else {
			if(plat->micom == 0)//cpt micon
			{
				for (i=0; i<ARRAY_SIZE(vx5b1d_init_seq_p_micom); i+=2) {
					u16 addr = (u16)vx5b1d_init_seq_p_micom[i+0];
					u32 data = vx5b1d_init_seq_p_micom[i+1];
					int ret = vx5b1d_reg_write32(plat, addr, data);
					if (ret < 0) {
							pr_err("cannot write register (addr=0x%04x, data=0x%08x, err=%d)\n",
						   addr, data, ret);
						/* continue */
					}
				}
			}
			else //cpt noise
			{
				for (i=0; i<ARRAY_SIZE(vx5b1d_init_seq_p); i+=2) {
					u16 addr = (u16)vx5b1d_init_seq_p[i+0];
					u32 data = vx5b1d_init_seq_p[i+1];
					int ret = vx5b1d_reg_write32(plat, addr, data);
					if (ret < 0) {
						pr_err("cannot write register (addr=0x%04x, data=0x%08x, err=%d)\n",
							   addr, data, ret);
						/* continue */
					}
				}
			}
		}
	}

	plat->initialized = true;
}

static void vx5b1d_unprepare(struct mmp_panel *panel)
{
	struct vx5b1d_plat_data *plat = panel->plat_data;

	gpio_request(plat->reset_gpio, "vx5b1d-reset");
	gpio_direction_output(plat->reset_gpio, 0);
	gpio_free(plat->reset_gpio);

	plat->initialized = false;
}

static struct mmp_panel panel_vx5b1d = {
	.name = "vx5b1d",
	.panel_type = PANELTYPE_DSI_VIDEO,
	.is_iovdd = 0,
	.is_avdd = 0,
	.get_modelist = vx5b1d_get_modelist,
	.set_status = vx5b1d_set_status,
	.prepare = vx5b1d_prepare,
	.unprepare = vx5b1d_unprepare,
	.panel_start = vx5b1d_panel_start,
};

///////////////////////////////////////////////////////////////////////////////

static int vx5b1d_lcd_get_contrast(struct lcd_device *lcd)
{
	struct vx5b1d_plat_data *plat = dev_get_drvdata(&lcd->dev);
	return plat->vee_level;
}

static int vx5b1d_lcd_set_contrast(struct lcd_device *lcd, int contrast)
{
	struct vx5b1d_plat_data *plat = dev_get_drvdata(&lcd->dev);
	vx5b1d_set_vee(plat, contrast);
	return 0;
}

static struct lcd_ops vx5b1d_lcd_ops = {
	.get_contrast = vx5b1d_lcd_get_contrast,
	.set_contrast = vx5b1d_lcd_set_contrast,
};

///////////////////////////////////////////////////////////////////////////////

static int vx5b1d_bl_get_brightness(struct backlight_device *bl)
{
	if (bl->props.fb_blank == FB_BLANK_UNBLANK &&
	    bl->props.power == FB_BLANK_UNBLANK) {
		return bl->props.brightness;
	}
	return 0;
}

static int vx5b1d_bl_update_status(struct backlight_device *bl)
{
	struct vx5b1d_plat_data *plat = dev_get_drvdata(&bl->dev);
	vx5b1d_set_brightness(plat, vx5b1d_bl_get_brightness(bl));
	return 0;
}

static const struct backlight_ops vx5b1d_bl_ops = {
	.get_brightness = vx5b1d_bl_get_brightness,
	.update_status  = vx5b1d_bl_update_status,
};

///////////////////////////////////////////////////////////////////////////////

static int mmp_dsi_panel_parse_video_mode(
	struct device_node *np, struct vx5b1d_plat_data *plat_data)
{
	int ret = 0;

	ret += of_property_read_u32(np, "dsi-panel-xres",
				    &plat_data->mode.xres);
	ret += of_property_read_u32(np, "dsi-panel-yres",
				    &plat_data->mode.yres);
	ret += of_property_read_u32(np, "dsi-panel-xres",
				    &plat_data->mode.real_xres);
	ret += of_property_read_u32(np, "dsi-panel-yres",
				    &plat_data->mode.real_yres);
	ret += of_property_read_u32(np, "dsi-panel-width",
				    &plat_data->mode.width);
	ret += of_property_read_u32(np, "dsi-panel-height",
				    &plat_data->mode.height);
	ret += of_property_read_u32(np, "dsi-h-front-porch",
				    &plat_data->mode.right_margin);
	ret += of_property_read_u32(np, "dsi-h-back-porch",
				    &plat_data->mode.left_margin);
	ret += of_property_read_u32(np, "dsi-h-sync-width",
				    &plat_data->mode.hsync_len);
	ret += of_property_read_u32(np, "dsi-v-front-porch",
				    &plat_data->mode.lower_margin);
	ret += of_property_read_u32(np, "dsi-v-back-porch",
				    &plat_data->mode.upper_margin);
	ret += of_property_read_u32(np, "dsi-v-sync-width",
				    &plat_data->mode.vsync_len);

	if (ret != 0)
		ret = -EINVAL;

	if(plat_data->lcd_orient == 0)
		plat_data->mode.refresh = 60;
	else
		plat_data->mode.refresh = 60;

	plat_data->mode.pix_fmt_out = PIXFMT_BGR888PACK;
	plat_data->mode.hsync_invert = 0;
	plat_data->mode.vsync_invert = 0;

	plat_data->mode.pixclock_freq = plat_data->mode.refresh *
		(plat_data->mode.xres + plat_data->mode.right_margin +
		 plat_data->mode.left_margin + plat_data->mode.hsync_len) *
		(plat_data->mode.yres + plat_data->mode.upper_margin +
		 plat_data->mode.lower_margin + plat_data->mode.vsync_len);

	plat_data->vee_level = VX5B1D_VEE_LEVEL_DEFAULT;

	return ret;
}

/*** SYSFS ***/
static ssize_t vx5b1d_reg_show(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	struct vx5b1d_plat_data *plat = panel_vx5b1d.plat_data;
	u16 addr;

	if (plat == NULL) {
		return sprintf(buf, "ERROR\n");
	}

	addr = plat->sysfs_reg_addr & 0xFFFE;
	if (((0x400 <= addr) && (addr < 0x4FF)) || ((addr & 0x02) != 0))
	{	// 16-bit read
		u16 data = 0;
		if (0 != vx5b1d_reg_read16(plat, addr, &data)) {
			dev_err(
				dev, "cannot read register (addr=0x%04x)\n",
				addr);
			return sprintf(buf, "ERROR\n");
		}
		return sprintf(buf, "0x%04x: 0x%04x\n", addr, data);
	}
	else
	{	// 32-bit read
		u32 data = 0;
		if (0 != vx5b1d_reg_read32(plat, addr, &data)) {
			dev_err(
				dev, "cannot read register (addr=0x%04x)\n",
				addr);
			return sprintf(buf, "ERROR\n");
		}
		return sprintf(buf, "0x%04x: 0x%08x\n", addr, data);
	}
}

static ssize_t vx5b1d_reg_store(
	struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct vx5b1d_plat_data *plat = panel_vx5b1d.plat_data;
	u32 i;
	char tmp[64];
	u16 addr;
	u32 data;
	int write = 0;

	if (plat == NULL) {
		return count;
	}

	if (count >= sizeof(tmp)) {
		dev_err(dev, "too long message !!\n");
		return count;
	}
	strncpy(tmp, buf, count);
	tmp[count] = '\0';

	for (i=0; tmp[i] != '\0'; i++) {
		if (tmp[i] == ' ') {
			tmp[i] = '\0';
			i++;
			break;
		}
	}
	addr = (u16)simple_strtoul(tmp, NULL, 0) & 0xFFFE;
	plat->sysfs_reg_addr = addr;
	for (; tmp[i] != '\0'; i++) {
		if (tmp[i] != ' ') {
			data = simple_strtoul(&tmp[i], NULL, 0);
			write = 1;
			break;
		}
	}
	if (!write) {
		return count;
	}

	if (((0x400 <= addr) && (addr < 0x4FF)) || ((addr & 0x02) != 0))
	{	// 16-bit write
		if (0 != vx5b1d_reg_write16(plat, addr, (u16)data)) {
			dev_err(
				dev,
				"cannot write register" \
				"(addr=0x%04x, data=0x%04x)\n",
				addr, (u16)data);
		}
	}
	else
	{	// 32-bit write
		if (0 != vx5b1d_reg_write32(plat, addr, data)) {
			dev_err(
				dev,
				"cannot write register" \
				"(addr=0x%04x, data=0x%08x)\n",
				addr, data);
		}
	}
	return count;
}

static ssize_t vx5b1d_vee_show(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	struct vx5b1d_plat_data *plat = panel_vx5b1d.plat_data;

	if (plat != NULL) {
		return sprintf(buf, "%d\n", plat->vee_level);
	}
	else {
		return sprintf(buf, "ERROR\n");
	}
}

static ssize_t vx5b1d_vee_store(
	struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct vx5b1d_plat_data *plat = panel_vx5b1d.plat_data;
	char tmp[16];
	enum vx5b1d_vee_level vee_level;

	if (plat == NULL) {
		return count;
	}
	if (count >= sizeof(tmp)) {
		return count;
	}
	strncpy(tmp, buf, count);
	tmp[count] = '\0';
	vee_level = (enum vx5b1d_vee_level)simple_strtol(tmp, NULL, 0);
	if ((vee_level < 0) || (vee_level >= VX5B1D_VEE_MAX)) {
		dev_err(dev, "invalid vee level %d\n", vee_level);
		return count;
	}
	vx5b1d_set_vee(plat, vee_level);

	return count;
}

static ssize_t vx5b1d_lowbattery_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	struct vx5b1d_plat_data *plat = panel_vx5b1d.plat_data;

	return sprintf(buf, "%d\n",plat->lowbattery);
}

static ssize_t vx5b1d_lowbattery_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct vx5b1d_plat_data *plat = panel_vx5b1d.plat_data;
	unsigned long val;
	int error;
	
	error = strict_strtoul(buf, 10, &val);
	if (error){
		printk(KERN_ERR "vx5b1d: lowbattery store failed\n");
		return error;
	}

	if(0!=val && 1!=val){
		printk(KERN_ERR "vx5b1d: lowbattery store failed val=%ld\n",val);
		return count;
	}

	plat->lowbattery = (int)val;

	vx5b1d_set_brightness(plat, plat->bl->props.brightness);

	return count;
}

static DEVICE_ATTR(reg, 0644, vx5b1d_reg_show, vx5b1d_reg_store);
static DEVICE_ATTR(vee, 0644, vx5b1d_vee_show, vx5b1d_vee_store);
static DEVICE_ATTR(lowbattery, 0644, vx5b1d_lowbattery_show, vx5b1d_lowbattery_store);

static struct attribute *vx5b1d_attributes[] = {
	&dev_attr_reg.attr,
	&dev_attr_vee.attr,
	&dev_attr_lowbattery.attr,
	NULL,
};

static struct attribute_group vx5b1d_attr_group = {
	.attrs = vx5b1d_attributes,
};
/*** SYSFS ***/

static int vx5b1d_probe(struct platform_device *pdev)
{
	struct mmp_mach_panel_info *mi;
	struct vx5b1d_plat_data *plat_data;
	struct device_node *np = pdev->dev.of_node;
	const char *path_name;
	struct lcd_device *lcd;
	struct backlight_properties bl_props;
	struct backlight_device *bl;
	int ret;
	u32 tmp;
	struct device_node *videomode_node;
	int micom_pin = -1;

	plat_data = kzalloc(sizeof(*plat_data), GFP_KERNEL);
	if (!plat_data)
		return -ENOMEM;

	if (IS_ENABLED(CONFIG_OF)) {
		if (!strcmp(panel_type, "WXGA_P")) {
			plat_data->lcd_orient = 1;
		}
		else if (!strcmp(panel_type, "WXGA")) {
			plat_data->lcd_orient = 0;
		}

		if (0 == of_property_read_u32(np, "vx5b1d,spi", &tmp)) {
			plat_data->spi = (int)tmp;
		}
		else {
			plat_data->spi = 0;
		}

		ret = of_get_named_gpio(np, "lcd,micom", 0);
		if (ret < 0) {
			dev_err(&pdev->dev, "cannot read property \"lcd,micom\"\n");
			kfree(plat_data);
			return -EINVAL;
		}
		micom_pin = ret;
		gpio_request(micom_pin, "micon");
		gpio_direction_input(micom_pin);
		plat_data->micom = gpio_get_value(micom_pin);
		gpio_free(micom_pin);

		if(plat_data->lcd_orient == 0)/*landscape*/
			videomode_node = of_find_node_by_name(np, "video_mode");
		else {
			if((plat_data->spi == 1) || (plat_data->micom == 0))
				videomode_node = of_find_node_by_name(np, "video_mode_spi");
			else
				videomode_node = of_find_node_by_name(np, "video_mode_p");
		}

		if (unlikely(!videomode_node)) {
			dev_err(&pdev->dev, "not found videomode_node!\n");
			return -EINVAL;
		}
		ret = mmp_dsi_panel_parse_video_mode(videomode_node, plat_data);
		if (ret < 0) {
			dev_err(&pdev->dev, "failed to parse video mode\n");
			return ret;
		}

		ret = of_property_read_string(np, "marvell,path-name",
				&path_name);
		if (ret < 0) {
			kfree(plat_data);
			return ret;
		}
		panel_vx5b1d.plat_path_name = path_name;

		if (of_find_property(np, "iovdd-supply", NULL))
			panel_vx5b1d.is_iovdd = 1;

		if (of_find_property(np, "avdd-supply", NULL))
			panel_vx5b1d.is_avdd = 1;

		ret = of_get_named_gpio(np, "vx5b1d,reset_gpio", 0);
		if (ret < 0) {
			dev_err(&pdev->dev, "cannot read property \"vx5b1d,reset_gpio\"\n");
			kfree(plat_data);
			return -EINVAL;
		}
		plat_data->reset_gpio = ret;

		if (0 == of_property_read_u32(np, "vx5b1d,i2c_adapter", &tmp)) {
			plat_data->i2c_adapter = i2c_get_adapter(tmp);
			if (plat_data->i2c_adapter == NULL) {
				dev_err(&pdev->dev, "cannot get i2c adapter\n");
				kfree(plat_data);
				return -EINVAL;
			}
		}
		else {
			dev_err(&pdev->dev, "cannot read property \"vx5b1d,i2c_adapter\"\n");
			kfree(plat_data);
			return -EINVAL;
		}

		if (0 == of_property_read_u32(np, "vx5b1d,i2c_devaddr", &tmp)) {
			plat_data->i2c_devaddr = (u8)tmp;
		}
		else {
			dev_err(&pdev->dev, "cannot read property \"vx5b1d,i2c_devaddr\"\n");
			kfree(plat_data);
			return -EINVAL;
		}

		ret = of_get_named_gpio(np, "lcd,power_on", 0);
		if (ret < 0) {
			dev_err(&pdev->dev, "cannot read property \"lcd,power_on\"\n");
			kfree(plat_data);
			return -EINVAL;
		}
		plat_data->lcd_power_on = ret;

		ret = of_get_named_gpio(np, "lcd,pwr_on", 0);
		if (ret < 0) {
			dev_err(&pdev->dev, "cannot read property \"lcd,pwr_on\"\n");
			kfree(plat_data);
			return -EINVAL;
		}
		plat_data->lcd_pwr_on = ret;

		ret = of_get_named_gpio(np, "lcd,backlight_on", 0);
		if (ret < 0) {
			dev_err(&pdev->dev, "cannot read property \"lcd,backlight_on\"\n");
			kfree(plat_data);
			return -EINVAL;
		}
		plat_data->lcd_backlight_on = ret;

		ret = of_get_named_gpio(np, "lcd,power_type", 0);
		if (ret < 0) {
			dev_err(&pdev->dev, "cannot read property \"lcd,power_type\"\n");
			kfree(plat_data);
			return -EINVAL;
		}
		plat_data->lcd_power_type_pin = ret;
		gpio_request(plat_data->lcd_power_type_pin, "lcd-power");
		gpio_direction_input(plat_data->lcd_power_type_pin);
		plat_data->lcd_power_type = gpio_get_value(plat_data->lcd_power_type_pin);
		gpio_free(plat_data->lcd_power_type_pin);
	}
	else {
		/* get configs from platform data */
		mi = pdev->dev.platform_data;
		if (mi == NULL) {
			dev_err(&pdev->dev, "no platform data defined\n");
			kfree(plat_data);
			return -EINVAL;
		}
		panel_vx5b1d.plat_path_name = mi->plat_path_name;
	}

	plat_data->panel = &panel_vx5b1d;
	panel_vx5b1d.plat_data = plat_data;
	panel_vx5b1d.dev = &pdev->dev;
	mmp_register_panel(&panel_vx5b1d);

	lcd = lcd_device_register(
		"vx5b1d", &pdev->dev, plat_data, &vx5b1d_lcd_ops);
	if (IS_ERR(lcd)) {
		ret = PTR_ERR(lcd);
		dev_err(&pdev->dev, "failed to register lcd device\n");
		return ret;
	}
	lcd->props.max_contrast = VX5B1D_VEE_MAX - 1;
	plat_data->lcd = lcd;

	memset(&bl_props, 0, sizeof(struct backlight_properties));
	bl_props.max_brightness = VX5B1D_BRIGHTNESS_MAX;
	bl_props.type = BACKLIGHT_RAW;

	bl = backlight_device_register("lcd-bl", &pdev->dev, plat_data,
			&vx5b1d_bl_ops, &bl_props);
	if (IS_ERR(bl)) {
		ret = PTR_ERR(bl);
		dev_err(&pdev->dev, "failed to register lcd-backlight\n");
		return ret;
	}
	plat_data->bl = bl;

	bl->props.fb_blank = FB_BLANK_UNBLANK;
	bl->props.power = FB_BLANK_UNBLANK;
	bl->props.brightness = VX5B1D_BRIGHTNESS_DEFAULT;

	plat_data->lowbattery = 0;

	ret = sysfs_create_group(&pdev->dev.kobj, &vx5b1d_attr_group);
	if (ret != 0) {
		dev_err(&pdev->dev, "failed to create sysfs\n");
		return ret;
	}

	return 0;
}

static int vx5b1d_remove(struct platform_device *dev)
{
	mmp_unregister_panel(&panel_vx5b1d);
	kfree(panel_vx5b1d.plat_data);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id mmp_vx5b1d_dt_match[] = {
	{ .compatible = "marvell,mmp-vx5b1d" },
	{},
};
#endif /* CONFIG_OF */

static struct platform_driver vx5b1d_driver = {
	.driver		= {
		.name	= "mmp-vx5b1d",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(mmp_vx5b1d_dt_match),
	},
	.probe		= vx5b1d_probe,
	.remove		= vx5b1d_remove,
};

static int vx5b1d_init(void)
{
	return platform_driver_register(&vx5b1d_driver);
}
static void vx5b1d_exit(void)
{
	platform_driver_unregister(&vx5b1d_driver);
}
module_init(vx5b1d_init);
module_exit(vx5b1d_exit);

MODULE_DESCRIPTION("MMP panel driver for VX5B1D");
MODULE_LICENSE("GPL");
