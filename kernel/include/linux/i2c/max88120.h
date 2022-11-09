/*
 * This file is part of the MAX88120 sensor driver.
 * Chip is combined gesture and proximity sensor.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#ifndef __MAX88120_H__
#define __MAX88120_H__

/* Register map */
#define MAX88120_STATUS  0x00   /* Main Status */
#define MAX88120_ADCA    0x01   /* Channel A data */
#define MAX88120_ADCB    0x02   /* Channel B data */
#define MAX88120_ADCC	 0x03   /* Channel C data */
#define MAX88120_ADCD    0x04   /* Channel D data */
#define MAX88120_MCFG    0x05   /* Main Configuration */
#define MAX88120_ADCFG   0x06   /* ADC Configuration */
#define MAX88120_LEDCFG  0x07   /* LED Driver Configuration */
#define MAX88120_UPTHR   0x08   /* Upper Threshold Byte */
#define MAX88120_LOTHR   0x09   /* Lower Threshold Byte */
#define MAX88120_INTPST  0x0a   /* Threshold Persist Timer */
#define MAX88120_GAINA   0x0b   /* Gain Setting of Channel A */
#define MAX88120_GAINB   0x0c   /* Gain Setting of Channel B */
#define MAX88120_GAINC   0x0d   /* Gain Setting of Channel C */
#define MAX88120_GAIND   0x0e   /* Gain Setting of Channel D */

/* Main Status register, Read Only */
#define STATUS_PWRON     (0x1 << 2) /* Power-on Interrupt Status */
#define STATUS_EOCINTS   (0x1 << 1) /* End of Conversion Interrupt Status */
#define STATUS_GESINTS   (0x1 << 0) /* Gesture Interrupt Status */

/* Main Configuration register */
#define MCFG_SHDN	  (0x1 << 7)  /* Shutdown Control */
#define MCFG_RESET        (0x1 << 6)  /* Reset Control */
#define MCFG_MODE_PROX    (0x1 << 3)  /* Super Proximity, Single Channle D */
#define MCFG_MODE_GESTURE (0x0 << 3)  /* Gesture, Four Channles */
#define MCFG_EOCINTE      (0x1 << 1)  /* End of Conversion Interrupt Enable */
#define MCFG_GESINTE      (0x1 << 0)  /* Gesture Interrupt Enable */

/* ADC Configuration Register */
#define ADCFG_SDLY_100    (0x7 << 4) /* 100ms Delay Between Samples */
#define ADCFG_SDLY_6P25   (0x3 << 4) /* 6.25ms Delay Between Samples */
#define ADCFG_TIM_DEFAULT (0x0 << 2) /* Integration Time for ADC Conversion */
#define ADCFG_ALWAYS_SET1 (0x3 << 0) /* The LST 2 Bit should always set to 1 */

/* LED Configuration Register */
#define LEDCFG_DRV_100  (0x4 << 4)    /* 100mA LED current */
#define LEDCFG_WID_192  (0x3 << 0)    /* LED Pluse Width 192 us */
#define LEDCFG_WID_144  (0x2 << 0)    /* LED Pluse Width 144 us */
#define LEDCFG_WID_96   (0x1 << 0)    /* LED Pluse Width 96 us */

/* Threshold Persist Timer */
#define INTPST_NUM8	(0x3 << 0) /* 8 Consecutive Measurements required */
#define INTPST_NUM4	(0x2 << 0) /* 4 Consecutive Measurements required */
#define INTPST_NUM2	(0x1 << 0) /* 2 Consecutive Measurements required */
#define INTPST_NUM1	(0x0 << 0) /* 1 Consecutive Measurements required */

/**
 * struct max81200_platform_data - platform data for max81200.c driver
 */
struct max88120_platform_data {
	int (*power_on) (int); /* power to the chip */
};

#endif
