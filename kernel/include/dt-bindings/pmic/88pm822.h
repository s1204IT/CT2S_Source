/*
 * Marvell 88PM822 Interface
 *
 * Copyright (C) 2013 Marvell International Ltd.
 * Yipeng <ypyao@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __LINUX_MFD_88PM822_H
#define __LINUX_MFD_88PM822_H

#define	PM822_INVALID_PAGE	0
#define	PM822_BASE_PAGE		1
#define	PM822_POWER_PAGE	2
#define	PM822_GPADC_PAGE	3

/*
 * Page 0: basic
 */
#define PM822_CHIP_ID			(0x00)
#define PM822_STATUS1			(0x01)
#define PM822_ONKEY_STS1		(1 << 0)
#define PM822_CHG_STS1			(1 << 2)
#define PM822_BAT_STS1			(1 << 3)
#define PM822_LDO_PGOOD_STS1	(1 << 5)
#define PM822_BUCK_PGOOD_STS1	(1 << 6)

/* Interrupt Registers */
#define PM822_INT_STATUS1		(0x05)
#define PM822_INT_STATUS2		(0x06)
#define PM822_INT_STATUS3		(0x07)
#define PM822_INT_EN1			(0x09)
	/* PM822_INT_EN1 bit definition */
#define PM822_IRQ_ONKEY_EN		(1 << 0)
#define PM822_IRQ_CHG_EN		(1 << 2)
#define PM822_IRQ_BAT_EN		(1 << 3)
#define PM822_IRQ_RTC_EN		(1 << 4)
#define PM822_IRQ_CLASSD_EN		(1 << 5)
	/* end */
#define PM822_INT_EN2			(0x0A)
	/* PM822_INT_EN2 bit definition */
#define PM822_IRQ_VBAT_EN		(1 << 0)
#define PM822_IRQ_VSYS_EN		(1 << 1)
#define PM822_IRQ_TINT_EN		(1 << 3)
#define PM822_IRQ_LDO_PGOOD_EN		(1 << 4)
#define PM822_IRQ_BUCK_PGOOD_EN		(1 << 5)
	/* end */
#define PM822_INT_EN3			(0x0B)
	/* PM822_INT_EN3 bit definition */
#define PM822_IRQ_GPADC0_EN		(1 << 0)
#define PM822_IRQ_GPADC1_EN		(1 << 1)
#define PM822_IRQ_GPADC2_EN		(1 << 2)
#define PM822_IRQ_GPADC3_EN		(1 << 3)
#define PM822_IRQ_MIC_DET_EN		(1 << 4)
#define PM822_IRQ_HS_DET_EN		(1 << 5)
	/* end */

/* Wakeup Registers */
#define PM822_WAKEUP1			(0x0D)
#define PM822_WAKEUP2			(0x0E)
	/* PM822_WAKEUP2 bit definition */
#define PM822_INV_INT			(1 << 0)
#define PM822_INT_CLEAR_MODE		(1 << 1)
#define PM822_INT_MASK_MODE		(1 << 2)
	/* end */
#define PM822_POWER_UP_LOG		(0x10)

/* Referance and low power registers */
#define PM822_LOW_POWER1		(0x20)
#define PM822_LOW_POWER2		(0x21)
#define PM822_LOW_POWER_CONFIG3		(0x22)
#define PM822_LOW_POWER_CONFIG4		(0x23)
#define PM822_LOW_POWER_CONFIG5		(0x24)

/* GPIO register */
#define PM822_GPIO0_CTRL		(0x30)
#define PM822_GPIO0_VAL			(1 << 0)
#define PM822_GPIO0_GPIO_MODE(x)	(x << 1)
#define PM822_GPIO1_CTRL		(0x31)
#define PM822_GPIO1_VAL			(1 << 0)
#define PM822_GPIO1_GPIO_MODE(x)	(x << 1)
#define PM822_GPIO2_CTRL		(0x32)
#define PM822_GPIO2_VAL			(1 << 0)
#define PM822_GPIO2_GPIO_MODE(x)	(x << 1)
#define PM822_GPIO3_CTRL		(0x33)
#define PM822_GPIO3_VAL			(1 << 0)
#define PM822_GPIO3_GPIO_MODE(x)	(x << 1)

#define PM822_HEADSET_CNTRL		(0x38)
#define PM822_HEADSET_DET_EN		(1 << 7)
#define PM822_HSDET_SLP			(1 << 1)
#define PM822_MIC_CNTRL			(0x39)
#define PM822_MIC_DET_EN			(1 << 0)

/* PWM register */
#define PM822_PWM1			(0x40)
#define PM822_PWM2			(0x41)
#define PM822_PWM3			(0x42)
#define PM822_PWM4			(0x43)

/* Oscillator control */
#define PM822_OSC_CTRL1			(0x50)
#define PM822_OSC_CTRL2			(0x51)
#define PM822_OSC_CTRL3			(0x52)
#define PM822_OSC_CTRL4			(0x53)
#define PM822_OSC_CTRL5			(0x54)
#define PM822_OSC_CTRL6			(0x55)
#define PM822_OSC_CTRL7			(0x56)
#define PM822_OSC_CTRL8			(0x57)
#define PM822_OSC_CTRL9			(0x58)
#define PM822_OSC_CTRL11		(0x5A)
#define PM822_OSC_CTRL12		(0x5B)
#define PM822_OSC_CTRL13		(0x5C)

/* RTC Registers */
#define PM822_RTC_CTRL			(0xD0)
/* bit definitions of RTC Register 1 (0xD0) */
#define PM822_ALARM1_EN			(1 << 0)
#define PM822_ALARM_WAKEUP		(1 << 4)
#define PM822_ALARM			(1 << 5)
#define PM822_RTC1_USE_XO		(1 << 7)

	/* end */
#define PM822_RTC_COUNTER1		(0xD1)
#define PM822_RTC_COUNTER2		(0xD2)
#define PM822_RTC_COUNTER3		(0xD3)
#define PM822_RTC_COUNTER4		(0xD4)
#define PM822_RTC_EXPIRE1_1		(0xD5)
#define PM822_RTC_EXPIRE1_2		(0xD6)
#define PM822_RTC_EXPIRE1_3		(0xD7)
#define PM822_RTC_EXPIRE1_4		(0xD8)
#define PM822_RTC_TRIM1			(0xD9)
#define PM822_RTC_TRIM2			(0xDA)
#define PM822_RTC_TRIM3			(0xDB)
#define PM822_RTC_TRIM4			(0xDC)
#define PM822_RTC_EXPIRE2_1		(0xDD)
#define PM822_RTC_EXPIRE2_2		(0xDE)
#define PM822_RTC_EXPIRE2_3		(0xDF)
#define PM822_RTC_EXPIRE2_4		(0xE0)
#define PM822_RTC_MISC1			(0xE1)
#define PM822_RTC_MISC2			(0xE2)
#define PM822_RTC_MISC3			(0xE3)
#define PM822_RTC_MISC4			(0xE4)
#define PM822_RTC_MISC5			(0xE7)
#define PM822_RTC_MISC6			(0xE8)
#define PM822_RTC_MISC7			(0xE9)
#define PM822_USER_DATA1		(0xEA)
#define PM822_USER_DATA2		(0xEB)
#define PM822_USER_DATA3		(0xEC)
#define PM822_USER_DATA4		(0xED)
#define PM822_USER_DATA5		(0xEE)
#define PM822_USER_DATA6		(0xEF)
#define PM822_POWER_DOWN_LOG1		(0xE5)
#define PM822_POWER_DOWN_LOG2		(0xE6)

/*
 * Page 1: power
 */
#define PM822_NOUSE			(0xFF)
#define PM822_LDO1			(0x08)
#define PM822_LDO2_AUDIO		(0x0A)
#define PM822_LDO2			(0x0B)
#define PM822_LDO3			(0x0C)
#define PM822_LDO4			(0x0D)
#define PM822_LDO5			(0x0E)
#define PM822_LDO6			(0x0F)
#define PM822_LDO7			(0x10)
#define PM822_LDO8			(0x11)
#define PM822_LDO9			(0x12)
#define PM822_LDO10			(0x13)
#define PM822_LDO11			(0x14)
#define PM822_LDO12			(0x15)
#define PM822_LDO13			(0x16)
#define PM822_LDO14			(0x17)
#define PM822_VOUTSW			(0xFF)	/* fake register */

#define PM822_BUCK1_SLP			(0x30)
#define PM822_BUCK2_SLP			(0x31)
#define PM822_BUCK3_SLP			(0x32)
#define PM822_BUCK4_SLP			(0x33)
#define PM822_BUCK5_SLP			(0x34)
#define PM822_AUDIO_MODE		(0x38)

#define PM822_BUCK1			(0x3C)
#define PM822_BUCK2			(0x40)
#define PM822_BUCK3			(0x41)
#define PM822_BUCK3_1			(0x42)
#define PM822_BUCK3_2			(0x43)
#define PM822_BUCK3_3			(0x44)
#define PM822_BUCK4			(0x45)
#define PM822_BUCK5			(0x46)
#define PM822_BUCK5_1			(0x47)
#define PM822_BUCK5_2			(0x48)
#define PM822_BUCK5_3			(0x49)

#define PM822_BUCK_EN1			(0x50)
#define PM822_LDO1_8_EN1		(0x51)
#define PM822_LDO9_14_EN1		(0x52)
#define PM822_MISC_EN1			(0x54)
#define PM822_BUCK_EN2			(0X55)
#define PM822_LDO1_8_EN2		(0x56)
#define PM822_LDO9_14_EN2		(0x57)
#define PM822_MISC_EN3			(0x59)

#define PM822_BUCK_SLP1			(0x5A)

/* BUCK Sleep Mode Register 1: BUCK[1..4] */
#define PM822_BUCK_SLP_PWR_OFF	0x0	/* power off */
#define PM822_BUCK_SLP_PWR_ACT1	0x1	/* auto & VBUCK_SET_SLP */
#define PM822_BUCK_SLP_PWR_LOW	0x2	/* VBUCK_SET_SLP */
#define PM822_BUCK_SLP_PWR_ACT2	0x3	/* auto & VBUCK_SET */

#define PM822_BUCK1_SLP1_SHIFT		0
#define PM822_BUCK1_SLP1_MASK		(0x3 << PM822_BUCK1_SLP1_SHIFT)
#define PM822_BUCK2_SLP1_SHIFT		2
#define PM822_BUCK2_SLP1_MASK		(0x3 << PM822_BUCK2_SLP1_SHIFT)

#define PM822_BUCK_SLP2			(0x5B)
#define PM822_LDO_SLP1			(0x5C)
#define PM822_LDO_SLP2			(0x5D)
#define PM822_LDO_SLP3			(0x5E)
#define PM822_LDO_SLP4			(0x5F)

/*
 * Page 2: GPADC
 */
#define PM822_GPADC_MEAS_EN1		(0x01)
#define PM822_GPADC_MEAS_EN2		(0x02)
#define PM822_GPADC_MISC_CONFIG1	(0x05)
#define PM822_GPADC4_DIR		(1 << 6)
#define PM822_GPADC_MISC_CONFIG2	(0x06)
	/* PM822_GPADC_MISC_CONFIG2 bit definition */
#define PM822_GPADC_EN			(1 << 0)
	/* end */

#define PM822_GPADC_MEAS_OFF1		(0x07)
#define PM822_GPADC_MEAS_OFF2		(0x08)
#define PM822_GPADC_MISC_CONFIG3	(0x0A)
#define PM822_GPADC_BIAS1		(0x0B)
#define PM822_GPADC_BIAS2		(0x0C)
#define PM822_GPADC_BIAS3		(0x0D)
#define PM822_GPADC_BIAS4		(0x0E)
#define PM822_GPADC_BIAS_EN1		(0x14)
#define PM822_VBAT_LOW_TH		(0x18)
#define PM822_VSYS_LOW_TH		(0x19)
#define PM822_GPADC0_LOW_TH		(0x20)
#define PM822_GPADC1_LOW_TH		(0x21)
#define PM822_GPADC2_LOW_TH		(0x22)
#define PM822_GPADC3_LOW_TH		(0x23)
#define PM822_MIC_DET_LOW_TH		(0x24)

#define PM822_VBAT_UPP_TH		(0x28)
#define PM822_VSYS_UPP_TH		(0x29)
#define PM822_VCHG_UPP_TH		(0x2A)
#define PM822_GPADC0_UPP_TH		(0x30)
#define PM822_GPADC1_UPP_TH		(0x31)
#define PM822_GPADC2_UPP_TH		(0x32)
#define PM822_GPADC3_UPP_TH		(0x33)
#define PM822_MIC_DET_UPP_TH		(0x34)

#define PM822_VBBAT_MEAS1		(0x40)
#define PM822_VBBAT_MEAS2		(0x41)
#define PM822_VBAT_MEAS1		(0x42)
#define PM822_VBAT_MEAS2		(0x43)
#define PM822_VSYS_MEAS1		(0x44)
#define PM822_VSYS_MEAS2		(0x45)
#define PM822_TINT_MEAS1		(0x50)
#define PM822_TINT_MEAS2		(0x51)

#define PM822_GPADC0_MEAS1		(0x54)
#define PM822_GPADC0_MEAS2		(0x55)
#define PM822_GPADC1_MEAS1		(0x56)
#define PM822_GPADC1_MEAS2		(0x57)
#define PM822_GPADC2_MEAS1		(0x58)
#define PM822_GPADC2_MEAS2		(0x59)
#define PM822_GPADC3_MEAS1		(0x5A)
#define PM822_GPADC3_MEAS2		(0x5B)
#define PM822_MIC_DET_MEAS1		(0x5C)
#define PM822_MIC_DET_MEAS2		(0x5D)

#define PM822_VBAT_AVG                  (0xA0)
#define PM822_VBAT_AVG2                 (0xA1)
#define PM822_VSYS_AVG                  (0xA2)
#define PM822_VSYS_AVG2                 (0xA3)
#define PM822_GPADC3_AVG1		(0xA6)
#define PM822_GPADC3_AVG2		(0xA7)
#define PM822_GPADC4_AVG2		(0xA9)
#define PM822_VBAT_SLP			(0xB0)

#endif /* __LINUX_MFD_88PM822_H */
