/*
 * This file is part of the MAX4400X sensor driver.
 * Chip is combined ambient light and RGB color sensor.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#ifndef __MAX4400X_H__
#define __MAX4400X_H__

/* Register map */
#define MAX4400X_ISTATUS 0x00   /* Interrupt Status */
#define MAX4400X_MCFG    0x01   /* Main Configuration  */
#define MAX4400X_ACFG    0x02   /* Ambient Configuration  */
#define MAX4400X_CLEARH  0x04   /* Ambient Clear High Byte */
#define MAX4400X_CLEARL  0x05   /* Ambient Clear Low Byte */
#define MAX4400X_REDH    0x06   /* Ambient Red High Byte */
#define MAX4400X_REDL    0x07   /* Ambient Red Low Byte */
#define MAX4400X_GERRNH  0x08   /* Ambient Green High Byte */
#define MAX4400X_GERRNL  0x09   /* Ambient Green Low Byte */
#define MAX4400X_BLUEH   0x0a   /* Ambient Blue High Byte */
#define MAX4400X_BLUEL   0x0b   /* Ambient Blue Low Byte */
#define MAX4400X_IRH     0x0c   /* Ambient Infrared High Byte */
#define MAX4400X_IRL     0x0d   /* Ambient Infrared Low Byte */
#define MAX4400X_IRCOMPH 0x0e   /* Ambient Infrared compesation High Byte */
#define MAX4400X_IRCOMPL 0x0f   /* Ambient Infrared Compesation Low Byte */
#define MAX4400X_TEMPH   0x12   /* Temperature High Byte */
#define MAX4400X_TEMPL   0x13   /* Temperature Low Byte */
#define MAX4400X_UPTHRH  0x14   /* Ambient Upper Threshold High Byte */
#define MAX4400X_UPTHRL  0x15   /* Ambient Upper Threshold Low Byte */
#define MAX4400X_LOTHRH  0x16   /* Ambient Lower Threshold High Byte */
#define MAX4400X_LOTHRL  0x17   /* Ambient Lower Threshold Low Byte */
#define MAX4400X_TPST    0x18   /* Ambient Threshold Persist Timer */
#define MAX4400X_TGCLEAR 0x1d   /* Digital Gain Trim of Clear Channle */
#define MAX4400X_TGRED   0x1e   /* Digital Gain Trim of Red Channle */
#define MAX4400X_TGGREEN 0x1f   /* Digital Gain Trim of Green Channle */
#define MAX4400X_TGBLUE  0x20   /* Digital Gain Trim of Blue Channle */
#define MAX4400X_TGIR    0x21   /* Digital Gain Trim of Infrared Channle */

/* Interrupt Status register */
#define ISTATUS_RESET    (0x1 << 4)     /* Reset Control */
#define ISTATUS_SHDN     (0x1 << 3)     /* Shutdown Control */
#define ISTATUS_PWRON    (0x1 << 2)     /* Power-on Interrupt Status Flag */
#define ISTATUS_AMBINTS  (0x1 << 0)     /* Ambient Interrupt Status Flag */

/* Main Configuration register */
#define MCFG_MODE_RGB_IR  (0x2 << 4)    /* Clear + TEMP + RGB + IR */
#define MCFG_MODE_IR      (0x1 << 4)    /* Clear + TEMP + IR */
#define MCFG_MODE_BASE    (0x0 << 4)    /* Clear + TEMP */
#define MCFG_AMBSEL_TEMP  (0x3 << 2)    /* Temp data compare with threshold */
#define MCFG_AMBSEL_GREEN (0x1 << 2)    /* Green data compare with threshold */
#define MCFG_AMBSEL_IR    (0x2 << 2)    /* IR data compare with threshold */
#define MCFG_AMBSEL_CLEAR (0x0 << 2)    /* Clear data compare with threshold */
#define MCFG_AMBINTE      (0x1 << 0)    /* Ambient Interrupt Enable */

#define MCFG_AMBSEL_MASK  (0x3 << 2)    /* AMBSEL_MASK */

/* Ambient Congfiguration Register */
#define ACFG_TRIM         (0x1 << 7)    /* TRIM Adjust Enable */
#define ACFG_COMPEN       (0x1 << 6)    /* IR Compesation Enable */
#define ACFG_TEMPEN       (0x1 << 5)    /* Temperature Sensor Enable */
#define ACFG_AMBTIM_1X  (0x0 << 2)      /* 100ms Integration Time */
#define ACFG_AMBTIM_4X  (0x1 << 2)      /* 25ms Integration Time */
#define ACFG_AMBTIM_16X (0x2 << 2)      /* 6.25ms Integration Time */
#define ACFG_AMBTIM_64X (0x3 << 2)      /* 1.5625ms Integration Time */
#define ACFG_AMBPGA00   (0x0 << 0)      /* Gain of Channel Measurements */
#define ACFG_AMBPGA01     (0x1 << 0)    /* Gain of Channel Measurements */
#define ACFG_AMBPGA02     (0x2 << 0)    /* Gain of Channel Measurements */
#define ACFG_AMBPGA03     (0x3 << 0)    /* Gain of Channel Measurements */

/* Ambient Threshold Persist Timer Register */
#define TPST_1X           (0x0 << 0)    /* 1 Consecutive Measure Required */
#define TPST_4X           (0x1 << 0)    /* 4 Consecutive Measure Required */
#define TPST_8X           (0x2 << 0)    /* 8 Consecutive Measure Required */
#define TPST_16X          (0x3 << 0)    /* 16 Consecutive Measure Required */

#define CLEAR_LEVEL_SIZE	10
#define CLEAR_LEVEL_MAX		(CLEAR_LEVEL_SIZE - 1)
/**
 * struct max4400x_platform_data - platform data for max4400x.c driver
 */
struct max4400x_platform_data {
	u32 clear_table[CLEAR_LEVEL_SIZE]; /* clear level, Unit nW/cm2 */
	int (*power_on) (int); /* power to the chip */

	/* Register Configuration */
	u8 mcfg;
	u8 acfg;
	u8 persist;
};

#endif
