/*
 * Marvell 88PM80x Interface
 *
 * Copyright (C) 2012 Marvell International Ltd.
 * Qiao Zhou <zhouqiao@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MFD_88PM860_H
#define __LINUX_MFD_88PM860_H

#define	PM860_INVALID_PAGE	0
#define	PM860_BASE_PAGE		1
#define	PM860_POWER_PAGE	2
#define	PM860_GPADC_PAGE	3

#define PM860_NUM_BUCK (6)	/*6 Bucks */
#define PM860_NUM_LDO (20)	/*20 ldos */

/* page 0 basic: slave adder 0x60 */

#define PM860_STATUS_1			(0x01)
#define PM860_ONKEY_STS1		(1 << 0)
#define PM860_EXTON_STS1		(1 << 1)
#define PM860_CHG_STS1			(1 << 2)
#define PM860_BAT_STS1			(1 << 3)
#define PM860_VBUS_STS1			(1 << 4)
#define PM860_LDO_PGOOD_STS1	(1 << 5)
#define PM860_BUCK_PGOOD_STS1	(1 << 6)

#define PM860_STATUS_2			(0x02)
#define PM860_RTC_ALARM_STS2	(1 << 0)

/* Wakeup Registers */
#define PM860_WAKEUP1		(0x0D)

#define PM860_WAKEUP2		(0x0E)
#define PM860_WAKEUP2_INV_INT		(1 << 0)
#define PM860_WAKEUP2_INT_CLEAR		(1 << 1)
#define PM860_WAKEUP2_INT_READ_CLEAR		(0 << 1)
#define PM860_WAKEUP2_INT_WRITE_CLEAR		(1 << 1)
#define PM860_WAKEUP2_INT_MASK		(1 << 2)

#define PM860_POWER_UP_LOG	(0x10)

/* Referance and low power registers */
#define PM860_LOW_POWER1		(0x20)
#define PM860_LOW_POWER2		(0x21)
#define PM860_LOW_POWER_CONFIG3	(0x22)
#define PM860_LOW_POWER_CONFIG4	(0x23)

/* GPIO register */
#define PM860_GPIO_0_1_CNTRL		(0x30)
#define PM860_GPIO0_VAL				(1 << 0)
#define PM860_GPIO0_GPIO_MODE(x)	(x << 1)
#define PM860_GPIO1_VAL				(1 << 4)
#define PM860_GPIO1_GPIO_MODE(x)	(x << 5)

#define PM860_GPIO_2_3_CNTRL		(0x31)
#define PM860_GPIO2_VAL				(1 << 0)
#define PM860_GPIO2_GPIO_MODE(x)	(x << 1)
#define PM860_GPIO3_VAL				(1 << 4)
#define PM860_GPIO3_GPIO_MODE(x)	(x << 5)
#define PM860_GPIO3_MODE_MASK		0x1F
#define PM860_GPIO3_HEADSET_MODE	PM860_GPIO3_GPIO_MODE(6)

#define PM860_GPIO_4_CNTRL			(0x32)
#define PM860_GPIO4_VAL				(1 << 0)
#define PM860_GPIO4_GPIO_MODE(x)	(x << 1)

#define PM860_HEADSET_CNTRL		(0x38)
#define PM860_HEADSET_DET_EN		(1 << 7)
#define PM860_HSDET_SLP			(1 << 1)
/* PWM register */
#define PM860_PWM1		(0x40)
#define PM860_PWM2		(0x41)
#define PM860_PWM3		(0x42)
#define PM860_PWM4		(0x43)

/* Oscillator control */
#define PM860_OSC_CNTRL1		(0x50)
#define PM860_OSC_CNTRL2		(0x51)
#define PM860_OSC_CNTRL3		(0x52)
#define PM860_OSC_CNTRL4		(0x53)
#define PM860_OSC_CNTRL5		(0x54)
#define PM860_OSC_CNTRL6		(0x55)
#define PM860_OSC_CNTRL7		(0x56)
#define PM860_OSC_CNTRL8		(0x57)
#define PM860_OSC_CNTRL9		(0x58)
#define PM860_OSC_CNTRL11		(0x5a)
#define PM860_OSC_CNTRL12		(0x5b)
#define PM860_OSC_CNTRL13		(0x5c)

/* RTC Registers */
#define PM860_RTC_CONTROL		(0xD0)
#define PM860_RTC_EXPIRE2_1		(0xDD)
#define PM860_RTC_EXPIRE2_2		(0xDE)
#define PM860_RTC_EXPIRE2_3		(0xDF)
#define PM860_RTC_EXPIRE2_4		(0xE0)

/* bit definitions of RTC Register 1 (0xD0) */
#define PM860_ALARM1_EN			(1 << 0)
#define PM860_ALARM_WAKEUP		(1 << 4)
#define PM860_ALARM			(1 << 5)
#define PM860_RTC1_USE_XO		(1 << 7)

#define PM860_RTC_MISC6			(0xE8)
#define PM860_RTC_MISC7			(0xE9)

/* bit definitions of PM860_RTC_MISC6 (0xE8) */
#define PM860_RTC_MISC6_XO_CAP_SEL_MASK	(0x70)
#define PM860_RTC_MISC6_XO_CAP_12PF	(0x40)
#define PM860_RTC_MISC6_XO_CAP_18PF	(0x60)

/*for save RTC offset*/
#define PM860_USER_DATA5		(0xEE)
#define PM860_USER_DATA6		(0xEF)

#define PM860_POWER_DOWN_LOG1	(0xE5)
#define PM860_POWER_DOWN_LOG2	(0xE6)

/* page 1 POWER: slave adder 0x01 */

/* buck registers */
#define PM860_BUCK1_SLEEP	(0x30)
#define PM860_BUCK1_SLEEP_MASK		0x7f
/*sllep voltage for eden A0  0.7V*/
#define PM860_BUCK1_SLEEP_VOL_EDEN		0x08
#define PM860_AUDIO_MODE_CONFIG1	(0x38)
#define PM860_BUCK3		(0x41)
#define PM860_BUCK3_1		(0x42)
#define PM860_BUCK3_2		(0x43)
#define PM860_BUCK3_3		(0x44)
#define PM860_BUCK4		(0x45)
#define PM860_BUCK5		(0x46)
#define PM860_BUCK5_1		(0x47)
#define PM860_BUCK5_2		(0x48)
#define PM860_BUCK5_3		(0x49)
#define PM860_BUCK6		(0x4A)
#define PM860_BUCK6_VOL_MASK	0x7f
#define PM860_BUCK6_VOL_1p5	0x48
#define PM860_BUCK6_MISC_3	(0x89)
#define PM860_BUCK6_MISC_3_SHORT_PFM_OFFTIME	(1 << 1)
#define PM860_BUCK6_MISC_3_DVC_PWM		(1 << 3)
#define PM860_BUCK_EN		(0x50)
#define PM860_BUCK6_EN		(1 << 5)
#define PM860_LDO1_8_EN1		(0x51)
#define PM860_LDO9_16_EN1		(0x52)
#define PM860_LDO17_20_EN1		(0x53)


/* BUCK Sleep Mode Register 1: BUCK[1..4] */
#define PM860_BUCK_SLP1		(0x5A)
#define PM860_BUCK1_SLP1_SHIFT	0
#define PM860_BUCK1_SLP1_MASK	(0x3 << PM860_BUCK1_SLP1_SHIFT)
#define PM860_BUCK2_SLP1_SHIFT	2
#define PM860_BUCK2_SLP1_MASK	(0x3 << PM860_BUCK2_SLP1_SHIFT)

#define PM860_BUCK_SLP_PWR_OFF	0x0	/* power off */
#define PM860_BUCK_SLP_PWR_ACT1	0x1	/* auto & VBUCK_SET_SLP */
#define PM860_BUCK_SLP_PWR_LOW	0x2	/* VBUCK_SET_SLP */
#define PM860_BUCK_SLP_PWR_ACT2	0x3	/* auto & VBUCK_SET */

/* BUCK Sleep Mode Register 2: BUCK5 */
#define PM860_BUCK_SLP2		(0x5B)
#define PM860_BUCK5_SLP2_SHIFT	0
#define PM860_BUCK5_SLP2_MASK	(0x3 << PM860_BUCK5_SLP2_SHIFT)

#define PM860_LDO1_1		(0x08)
#define PM860_LDO2_SET_AUDIO	(0x0a)
#define PM860_LDO2		(0x0b)
#define PM860_LDO3		(0x0c)
#define PM860_LDO4		(0x0d)
#define PM860_LDO5		(0x0e)
#define PM860_LDO6		(0x0f)
#define PM860_LDO7		(0x10)
#define PM860_LDO8		(0x11)
#define PM860_LDO9		(0x12)
#define PM860_LDO10		(0x13)
#define PM860_LDO11		(0x14)
#define PM860_LDO12		(0x15)
#define PM860_LDO13		(0x16)
#define PM860_LDO14		(0x17)
#define PM860_LDO15		(0x18)
#define PM860_LDO16		(0x19)
#define PM860_LDO17		(0x1a)
#define PM860_LDO18		(0x1b)
#define PM860_LDO19		(0x1c)
#define PM860_LDO20		(0x1D)

#define PM860_AUDIO_MODE	(0x38)

/* BUCK Sleep Mode Register 1: BUCK[1..4] */
#define PM860_BUCK_SLP1		(0x5A)
#define PM860_BUCK1_SLP1_SHIFT	0
#define PM860_BUCK1_SLP1_MASK	(0x3 << PM860_BUCK1_SLP1_SHIFT)
#define PM860_BUCK_SLP2		(0x5B)

/* page 2 GPADC: slave adder 0x02 */
#define PM860_GPADC_MEAS_EN1		(0x01)
#define PM860_MEAS_EN1_VBAT         (1 << 2)
#define PM860_GPADC_MEAS_EN2		(0x02)

#define PM860_GPADC_MISC_CONFIG1	(0x05)
#define PM860_GPADC_MISC_CONFIG2	(0x06)
#define PM860_GPADC_MISC_GPFSM_EN	(1 << 0)
#define PM860_GPADC_SLOW_MODE(x)	(x << 3)

#define PM860_GPADC_MEASURE_OFF1	(0x07)
#define PM860_GPADC_MEASURE_OFF2	(0x08)

#define PM860_GPADC_MISC_CONFIG3		(0x09)
#define PM860_GPADC_MISC_CONFIG4		(0x0A)
#define PM860_GPADC_BIAS1			(0x0B)
#define PM860_GPADC_BIAS2			(0x0C)
#define PM860_GPADC_BIAS3			(0x0D)
#define PM860_GPADC_BIAS4			(0x0E)

#define GP_BIAS_SET(x)				(x << 0)
#define GP_PREBIAS(x)				(x << 4)

#define PM860_GPADC_PREBIAS1			(0x0F)
#define PM860_GPADC0_GP_PREBIAS_TIME(x)	(x << 0)
#define PM860_GPADC_PREBIAS2			(0x10)

#define PM860_GP_BIAS_ENA1				(0x14)

#define PM860_GP_BIAS_ENA1				(0x14)
#define PM860_GPADC_GP_BIAS_EN0			(1 << 0)
#define PM860_GPADC_GP_BIAS_EN1			(1 << 1)
#define PM860_GPADC_GP_BIAS_EN2			(1 << 2)
#define PM860_GPADC_GP_BIAS_EN3			(1 << 3)



#endif /* __LINUX_MFD_88PM860_H */
