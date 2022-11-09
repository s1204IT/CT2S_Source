/*
 * Regulators driver for Marvell 88PM800
 *
 * Copyright (C) 2012 Marvell International Ltd.
 * Joseph(Yossi) Hanin <yhanin@marvell.com>
 * Yi Zhang <yizhang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/88pm80x.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/regulator/of_regulator.h>

/* LDO1 with DVC[0..3] */
#define PM800_LDO1_VOUT		(0x08) /* VOUT1 */
#define PM800_LDO1_VOUT_2		(0x09)
#define PM800_LDO1_VOUT_3		(0x0A)
#define PM800_LDO2_VOUT		(0x0B)
#define PM800_LDO3_VOUT		(0x0C)
#define PM800_LDO4_VOUT		(0x0D)
#define PM800_LDO5_VOUT		(0x0E)
#define PM800_LDO6_VOUT		(0x0F)
#define PM800_LDO7_VOUT		(0x10)
#define PM800_LDO8_VOUT		(0x11)
#define PM800_LDO9_VOUT		(0x12)
#define PM800_LDO10_VOUT		(0x13)
#define PM800_LDO11_VOUT		(0x14)
#define PM800_LDO12_VOUT		(0x15)
#define PM800_LDO13_VOUT		(0x16)
#define PM800_LDO14_VOUT		(0x17)
#define PM800_LDO15_VOUT		(0x18)
#define PM800_LDO16_VOUT		(0x19)
#define PM800_LDO17_VOUT		(0x1A)
#define PM800_LDO18_VOUT		(0x1B)
#define PM800_LDO19_VOUT		(0x1C)
#define PM800_VOUTSW_VOUT		(0xFF)	/* fake register */

/*88ppm86x register*/
#define PM800_LDO20_VOUT		(0x1D)

/* BUCK1 with DVC[0..3] */
#define PM800_BUCK1_SET_SLP	(0x30)
#define PM800_BUCK2_SET_SLP	(0x31)
#define PM800_BUCK3_SET_SLP	(0x32)
#define PM800_BUCK4_SET_SLP	(0x33)
#define PM800_BUCK5_SET_SLP	(0x34)
#define PM800_BUCK6_SET_SLP	(0x35)
#define PM800_BUCK1B_SET_SLP	(0x36)
#define PM800_BUCK_SLP_MASK	(0x7F)
#define PM800_LDO_SLP_MASK	(0xF0)


#define PM800_BUCK1			(0x3C)
#define PM800_BUCK1_1			(0x3D)
#define PM800_BUCK1_2			(0x3E)
#define PM800_BUCK1_3			(0x3F)
#define PM800_BUCK2			(0x40)
#define PM800_BUCK3			(0x41)
#define PM800_BUCK4			(0x42)
#define PM800_BUCK4_1			(0x43)
#define PM800_BUCK4_2			(0x44)
#define PM800_BUCK4_3			(0x45)
#define PM800_BUCK5			(0x46)

#define PM800_BUCK_ENA			(0x50)
#define PM800_LDO_ENA1_1		(0x51)
#define PM800_LDO_ENA1_2		(0x52)
#define PM800_LDO_ENA1_3		(0x53)

#define PM800_BUCK_ENA2			(0x55)
#define PM800_LDO_ENA2_1		(0x56)
#define PM800_LDO_ENA2_2		(0x57)
#define PM800_LDO_ENA2_3		(0x58)


#define PM800_BUCK1_AUD			(0x38)
#define PM800_BUCK4_AUD			(0x39)
#define PM800_LDO1_AUD			(0x0A) /* correspond to PM800 */
#define PM800_LDO2_AUD			(0x0A) /* correspond to PM822 */
#define PM800_BUCK_AUD_MASK		(0x7F)
#define PM800_LDO_AUD_MASK		(0xF0)

#define PM800_BUCK1_MISC1		(0x78)
#define PM800_BUCK3_MISC1		(0x7E)
#define PM800_BUCK4_MISC1		(0x81)
#define PM800_BUCK5_MISC1		(0x84)

#define PM800_DUMMY			(0xFF)

#define MAX_SLEEP_CURRENT	5000
#define REGULATOR_SLEEP_MODE_DIS	0
#define REGULATOR_SLEEP_MODE_EN	1

struct pm800_regulator_volt_range {
	int	min_uv;
	int	max_uv;
	int	step_uv;
	/* the register value for min_uv */
	int	min_val;
};

struct pm800_regulator_info {
	struct regulator_desc desc;
	int max_ua;
	unsigned int sleep_enable_bit;
	unsigned int sleep_enable_reg;
	unsigned int sleep_vsel_reg;
	unsigned int sleep_vsel_mask;
	struct pm800_regulator_volt_range *volt;
	unsigned int ranges;
	int num_of_sets;
	int audio_mode;
	unsigned int vaudio_reg;
	unsigned int vaudio_mask;
	unsigned int enable2_reg;
};

struct pm800_regulators {
	struct regulator_dev *regulators[PM800_ID_RG_MAX];
	struct pm80x_chip *chip;
	struct regmap *map;
};

struct pm80x_buckldo_print {
	char name[15];
	char enable[15];
	char slp_mode[15];
	char set_slp[10];
	char sets[4][10];
	char audio[10];
};

/*
 * vreg - the buck regs string.
 * ereg - the string for the enable register.
 * e2reg - the string for the enable 2 register.
 * ebit - the bit number in the enable register.
 * amax - the current
 * Buck has 2 kinds of voltage steps. It is easy to find voltage by ranges,
 * not the constant voltage table.
 * sebit - the sleep enable mode bit.
 * sereg - the sleep enable mode register.
 * setnum - the number of sets the regulator have
 * amod - is there audio mode - true/false.
 * areg - the audio set register.
 */
#define PM800_BUCK(chip, vreg, ereg, e2reg, ebit, amax, sebit, sereg, v_range, set, amod, areg) \
{									\
	.desc	= {							\
		.name	= #vreg,					\
		.ops	= &pm800_volt_range_ops,			\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= PM800_ID_##vreg,				\
		.owner	= THIS_MODULE,					\
		.vsel_reg	= chip##_##vreg,			\
		.vsel_mask	= 0x7f,					\
		.enable_reg	= PM800_##ereg,				\
		.enable_mask	= 1 << (ebit),				\
	},								\
	.max_ua		= (amax),					\
	.volt		= (buck_volt_range##v_range),			\
	.ranges		= ARRAY_SIZE(buck_volt_range##v_range),		\
	.sleep_enable_bit	= sebit,				\
	.sleep_enable_reg	= PM800_##sereg,			\
	.num_of_sets		= set,					\
	.audio_mode = amod,						\
	.vaudio_mask = PM800_BUCK_AUD_MASK,				\
	.vaudio_reg	= PM800_##areg,					\
	.sleep_vsel_reg = PM800_##vreg##_SET_SLP,			\
	.sleep_vsel_mask = PM800_BUCK_SLP_MASK,				\
	.enable2_reg = PM800_##e2reg					\
}

/*
 * vreg - the LDO regs string
 * ereg -  the string for the enable register.
 * e2reg - the string for the enable 2 register.
 * ebit - the bit number in the enable register.
 * amax - the current
 * volt_table - the LDO voltage table
 * For all the LDOes, there are too many ranges. Using volt_table will be
 * simpler and faster.
 * sebit - the sleep enable mode bit.
 * sereg - the sleep enable mode register.
 * amod - is there audio mode - true/false.
 * areg - the audio set register.
 */
#define PM800_LDO(vreg, ereg, e2reg, ebit, amax, sebit, sereg, ldo_table, amod, areg)	\
{									\
	.desc	= {							\
		.name	= #vreg,					\
		.ops	= &pm800_volt_table_ops,			\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= PM800_ID_##vreg,				\
		.owner	= THIS_MODULE,					\
		.n_voltages = ARRAY_SIZE(ldo_volt_table##ldo_table),		\
		.vsel_reg	= PM800_##vreg##_VOUT,			\
		.vsel_mask	= 0xf,					\
		.enable_reg	= PM800_##ereg,				\
		.enable_mask	= 1 << (ebit),				\
		.volt_table	= ldo_volt_table##ldo_table,			\
	},								\
	.max_ua		= (amax),					\
	.sleep_enable_bit	= sebit,				\
	.sleep_enable_reg	= PM800_##sereg,			\
	.audio_mode = amod,						\
	.num_of_sets = 1,						\
	.vaudio_mask = PM800_LDO_AUD_MASK,				\
	.vaudio_reg	= PM800_##areg,					\
	.sleep_vsel_reg = PM800_##vreg##_VOUT,				\
	.sleep_vsel_mask = PM800_LDO_SLP_MASK,				\
	.enable2_reg = PM800_##e2reg					\
}

/* 88pm800 buck1 and 88pm822 buck1*/
static struct pm800_regulator_volt_range buck_volt_range1[] = {
	{600000,	1587500,	12500,	0x0},
	{1600000,	1800000,	50000,	0x50},
};

/* 88pm800 buck 2 ~ 5 and 88pm822 buck 2 ~ 4 */
static struct pm800_regulator_volt_range buck_volt_range2[] = {
	{600000,	1587500,	12500,	0x0},
	{1600000,	3300000,	50000,	0x50},
};

/* 88pm822 buck5 */
static struct pm800_regulator_volt_range buck_volt_range3[] = {
	{600000,	1587500,	12500,	0x0},
	{1600000,	3950000,	50000,	0x50},
};

/* 88pm800 ldo1; 88pm86x ldo19 */
static const unsigned int ldo_volt_table1[] = {
	600000,  650000,  700000,  750000,  800000,  850000,  900000,  950000,
	1000000, 1050000, 1100000, 1150000, 1200000, 1300000, 1400000, 1500000,
};

/* 88pm800 ldo2; 88pm86x ldo20 */
static const unsigned int ldo_volt_table2[] = {
	1700000, 1800000, 1900000, 2000000, 2100000, 2500000, 2700000, 2800000,
};

/* 88pm800 ldo 3 ~ 17*/
static const unsigned int ldo_volt_table3[] = {
	1200000, 1250000, 1700000, 1800000, 1850000, 1900000, 2500000, 2600000,
	2700000, 2750000, 2800000, 2850000, 2900000, 3000000, 3100000, 3300000,
};

/* 88pm800 18 ~ 19 */
static const unsigned int ldo_volt_table4[] = {
	1700000, 1800000, 1900000, 2500000, 2800000, 2900000, 3100000, 3300000,
};

/* 88pm822 ldo1 and ldo2 ;88pm86x ldo1 ~ 3*/
static const unsigned int ldo_volt_table5[] = {
	1700000, 1800000, 1900000, 2500000, 2800000, 2900000, 3100000, 3300000,
};

/* 88pm822 ldo 3~11; 88pm86x ldo4 ~ 18*/
static const unsigned int ldo_volt_table6[] = {
	1200000, 1250000, 1700000, 1800000, 1850000, 1900000, 2500000, 2600000,
	2700000, 2750000, 2800000, 2850000, 2900000, 3000000, 3100000, 3300000,
};

/* 88pm822 ldo12*/
static const unsigned int ldo_volt_table7[] = {
	600000,  650000,  700000,  750000,  800000,  850000,  900000,  950000,
	1000000, 1050000, 1100000, 1150000, 1200000, 1300000, 1400000, 1500000,
};

/* 88pm822 ldo13 */
static const unsigned int ldo_volt_table8[] = {
	1700000, 1800000, 1900000, 2500000, 2800000, 2900000, 3100000, 3300000,
};

/* 88pm822 ldo14 */
static const unsigned int ldo_volt_table9[] = {
	1700000, 1800000, 1900000, 2000000, 2100000, 2500000, 2700000, 2800000,
};

/* dummy table */
static const unsigned int ldo_volt_table_voutsw[] = {
};

int pm800_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct pm800_regulator_info *info = rdev_get_drvdata(rdev);
	unsigned int val, sleep_bit, sleep_enable_mask, reg;

	sleep_bit = info->sleep_enable_bit;
	sleep_enable_mask = (0x3 << sleep_bit);

	/*
	 * if mode is REGULATOR_SLEEP_MODE_EN, enable related LDO sleep mode
	 * else disable related LDO sleep mode.
	 */
	switch (mode) {
	case REGULATOR_MODE_IDLE:
		/*
		 * WE CAN'T USE SLEEP MODE !!!
		 *
		 * info->sleep_vsel_reg and info->sleep_vsel_mask
		 * is not manipulated.
		 */
//		val = (0x2 << sleep_bit);
		val = (0x3 << sleep_bit);
		break;
	case REGULATOR_MODE_NORMAL:
		val = (0x3 << sleep_bit);
		break;
	default:
		dev_err(rdev_get_dev(rdev), "No right mode to set!\n");
		return -EINVAL;
	}

	reg = info->sleep_enable_reg;

	return regmap_update_bits(rdev->regmap, reg, sleep_enable_mask, val);
}

static unsigned int pm800_get_optimum_mode(struct regulator_dev *rdev,
						 int input_uV, int output_uV,
						 int output_uA)
{
	struct pm800_regulator_info *info = rdev_get_drvdata(rdev);

	if (info == NULL) {
		dev_err(rdev_get_dev(rdev), "regulator info null pointer\n");
		return REGULATOR_MODE_IDLE;
	}
	if (output_uA < 0) {
		dev_err(rdev_get_dev(rdev), "wrong current input, current < 0!!!\n");
		return REGULATOR_MODE_IDLE;
	}
	/*
	 * get_optimum_mode be called at enbale/disable_regulator function.
	 * If output_uA is not set it will be 0,
	 * set its defult value to be REGULATOR_MODE_IDLE.
	 */
	return (MAX_SLEEP_CURRENT > output_uA) ? REGULATOR_MODE_IDLE : REGULATOR_MODE_NORMAL;
}

static int pm800_get_current_limit(struct regulator_dev *rdev)
{
	struct pm800_regulator_info *info = rdev_get_drvdata(rdev);

	return info->max_ua;
}

static int pm800_set_voltage(struct regulator_dev *rdev,
			     int min_uv, int max_uv, unsigned *selector)
{
	struct pm800_regulator_info *info = rdev_get_drvdata(rdev);
	struct pm800_regulator_volt_range *range;
	int i, best_index = -1;

	if (info->volt == NULL)
		return -EINVAL;

	if (info->desc.id == PM800_ID_VOUTSW)
		return 0;

	/*
	 * Ranges are sorted in ascending order. So if we found a best_uv
	 * in this range, we can break out.
	 */
	for (i = 0; i < info->ranges; i++) {
		range = &info->volt[i];

		if (min_uv <= range->max_uv && max_uv >= range->min_uv) {
			if (min_uv <= range->min_uv)
				best_index = 0;
			else
				best_index = (min_uv - range->min_uv +
					range->step_uv - 1) / range->step_uv;
			break;
		}
	}

	if (best_index == -1)
		return -EINVAL;

	*selector = best_index + range->min_val;

	return regulator_set_voltage_sel_regmap(rdev, *selector);
}

/*
 * The function convert the buck voltage register value
 * to a real voltage value (in uV) according to the voltage table.
 */
static int pm800_get_vbuck_vol(unsigned int val, struct pm800_regulator_info *info)
{
	struct pm800_regulator_volt_range *range;
	int i, max_val, volt = -EINVAL;

	/* get the voltage via the register value */
	for (i = 0; i < info->ranges; i++) {
		range = &info->volt[i];
		if (!range)
			return -EINVAL;
		max_val = (range->max_uv - range->min_uv) / range->step_uv
				+ range->min_val;

		if (val >= range->min_val && val <= max_val) {
			volt = (val - range->min_val) * range->step_uv
				+ range->min_uv;
			break;
		}
	}
	return volt;
}

static int pm800_get_voltage(struct regulator_dev *rdev)
{
	struct pm800_regulator_info *info = rdev_get_drvdata(rdev);
	int  val;

	if (info->volt == NULL)
		return -EINVAL;

	val = regulator_get_voltage_sel_regmap(rdev);
	if (val < 0)
		return val;

	if (info->desc.id == PM800_ID_VOUTSW)
		return 0;
	return pm800_get_vbuck_vol(val, info);
}

static struct regulator_ops pm800_volt_range_ops = {
	.set_voltage = pm800_set_voltage,
	.get_voltage = pm800_get_voltage,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_current_limit = pm800_get_current_limit,
	.get_optimum_mode = pm800_get_optimum_mode,
	.set_mode = pm800_set_mode,
};

static struct regulator_ops pm800_volt_table_ops = {
	.list_voltage = regulator_list_voltage_table,
	.map_voltage = regulator_map_voltage_iterate,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.get_current_limit = pm800_get_current_limit,
	.get_optimum_mode = pm800_get_optimum_mode,
	.set_mode = pm800_set_mode,
};

/* The array is indexed by id(PM800_ID_XXX) */
static struct pm800_regulator_info pm800_regulator_info[] = {
	/*PM800_BUCK(chip, vreg, ereg, e2reg, ebit, amax, sebit, sereg, v_range, set, amod, areg)*/
	PM800_BUCK(PM800, BUCK1, BUCK_ENA, BUCK_ENA2, 0, 3000000, 0, BUCK_SLP1, 1, 4, 1, BUCK1_AUD),
	PM800_BUCK(PM800, BUCK2, BUCK_ENA, BUCK_ENA2, 1, 1200000, 2, BUCK_SLP1, 2, 1, 0, DUMMY),
	PM800_BUCK(PM800, BUCK3, BUCK_ENA, BUCK_ENA2, 2, 1200000, 4, BUCK_SLP1, 2, 1, 0, DUMMY),
	PM800_BUCK(PM800, BUCK4, BUCK_ENA, BUCK_ENA2, 3, 1200000, 6, BUCK_SLP1, 2, 4, 1, BUCK4_AUD),
	PM800_BUCK(PM800, BUCK5, BUCK_ENA, BUCK_ENA2, 4, 1200000, 0, BUCK_SLP2, 2, 1, 0, DUMMY),
	PM800_BUCK(PM800, BUCK6, BUCK_ENA, BUCK_ENA2, 5, 1500000, 2, BUCK_SLP2, 2, 1, 0, DUMMY),
	PM800_BUCK(PM800, BUCK1B, BUCK_ENA, BUCK_ENA2, 6, 3500000, 4, BUCK_SLP2, 2, 1, 0, DUMMY),

	/* PM800_LDO(vreg, ereg, e2reg, ebit, amax, sebit, sereg, ldo_table, amod, areg) */
	PM800_LDO(LDO1, LDO_ENA1_1, LDO_ENA2_1, 0, 200000, 0, LDO_SLP1, 1, 1, LDO1_AUD),
	PM800_LDO(LDO2, LDO_ENA1_1, LDO_ENA2_1, 1, 10000, 2, LDO_SLP1, 2, 0, DUMMY),
	PM800_LDO(LDO3, LDO_ENA1_1, LDO_ENA2_1, 2, 300000, 4, LDO_SLP1, 3, 0, DUMMY),
	PM800_LDO(LDO4, LDO_ENA1_1, LDO_ENA2_1, 3, 300000, 6, LDO_SLP1, 3, 0, DUMMY),
	PM800_LDO(LDO5, LDO_ENA1_1, LDO_ENA2_1, 4, 300000, 0, LDO_SLP2, 3, 0, DUMMY),
	PM800_LDO(LDO6, LDO_ENA1_1, LDO_ENA2_1, 5, 300000, 2, LDO_SLP2, 3, 0, DUMMY),
	PM800_LDO(LDO7, LDO_ENA1_1, LDO_ENA2_1, 6, 300000, 4, LDO_SLP2, 3, 0, DUMMY),
	PM800_LDO(LDO8, LDO_ENA1_1, LDO_ENA2_1, 7, 300000, 6, LDO_SLP2, 3, 0, DUMMY),
	PM800_LDO(LDO9, LDO_ENA1_2, LDO_ENA2_2, 0, 300000, 0, LDO_SLP3, 3, 0, DUMMY),
	PM800_LDO(LDO10, LDO_ENA1_2, LDO_ENA2_2, 1, 300000, 2, LDO_SLP3, 3, 0, DUMMY),
	PM800_LDO(LDO11, LDO_ENA1_2, LDO_ENA2_2, 2, 300000, 4, LDO_SLP3, 3, 0, DUMMY),
	PM800_LDO(LDO12, LDO_ENA1_2, LDO_ENA2_2, 3, 300000, 6, LDO_SLP3, 3, 0, DUMMY),
	PM800_LDO(LDO13, LDO_ENA1_2, LDO_ENA2_2, 4, 300000, 0, LDO_SLP4, 3, 0, DUMMY),
	PM800_LDO(LDO14, LDO_ENA1_2, LDO_ENA2_2, 5, 300000, 2, LDO_SLP4, 3, 0, DUMMY),
	PM800_LDO(LDO15, LDO_ENA1_2, LDO_ENA2_2, 6, 300000, 4, LDO_SLP4, 3, 0, DUMMY),
	PM800_LDO(LDO16, LDO_ENA1_2, LDO_ENA2_2, 7, 300000, 6, LDO_SLP4, 3, 0, DUMMY),
	PM800_LDO(LDO17, LDO_ENA1_3, LDO_ENA2_3, 0, 300000, 0, LDO_SLP5, 3, 0, DUMMY),
	PM800_LDO(LDO18, LDO_ENA1_3, LDO_ENA2_3, 1, 200000, 2, LDO_SLP5, 4, 0, DUMMY),
	PM800_LDO(LDO19, LDO_ENA1_3, LDO_ENA2_3, 2, 200000, 4, LDO_SLP5, 4, 0, DUMMY),
};

static struct pm800_regulator_info pm822_regulator_info[] = {
	/*PM800_BUCK(chip, vreg, ereg, e2reg, ebit, amax, sebit, sereg, v_range, set, amod, areg)*/
	PM800_BUCK(PM800, BUCK1, BUCK_ENA, BUCK_ENA2, 0, 3500000, 0, BUCK_SLP1, 1, 4, 1, BUCK1_AUD),
	PM800_BUCK(PM800, BUCK2, BUCK_ENA, BUCK_ENA2, 1, 750000, 2, BUCK_SLP1, 2, 1, 0, DUMMY),
	PM800_BUCK(PM800, BUCK3, BUCK_ENA, BUCK_ENA2, 2, 1500000, 4, BUCK_SLP1, 2, 4, 0, DUMMY),
	PM800_BUCK(PM800, BUCK4, BUCK_ENA, BUCK_ENA2, 3, 750000, 6, BUCK_SLP1, 2, 1, 0, DUMMY),
	PM800_BUCK(PM800, BUCK5, BUCK_ENA, BUCK_ENA2, 4, 1500000, 0, BUCK_SLP2, 3, 4, 0, DUMMY),
	PM800_BUCK(PM800, BUCK6, BUCK_ENA, BUCK_ENA2, 5, 1500000, 2, BUCK_SLP2, 2, 1, 0, DUMMY),
	PM800_BUCK(PM800, BUCK1B, BUCK_ENA, BUCK_ENA2, 6, 3500000, 4, BUCK_SLP2, 2, 1, 0, DUMMY),

	/* PM800_LDO(vreg, ereg, e2reg, ebit, amax, sebit, sereg, ldo_table, amod, areg) */
	PM800_LDO(LDO1, LDO_ENA1_1, LDO_ENA2_1, 0, 100000, 0, LDO_SLP1, 5, 0, DUMMY),
	PM800_LDO(LDO2, LDO_ENA1_1, LDO_ENA2_1, 1, 100000, 2, LDO_SLP1, 5, 1, LDO2_AUD),
	PM800_LDO(LDO3, LDO_ENA1_1, LDO_ENA2_1, 2, 400000, 4, LDO_SLP1, 6, 0, DUMMY),
	PM800_LDO(LDO4, LDO_ENA1_1, LDO_ENA2_1, 3, 400000, 6, LDO_SLP1, 6, 0, DUMMY),
	PM800_LDO(LDO5, LDO_ENA1_1, LDO_ENA2_1, 4, 200000, 0, LDO_SLP2, 6, 0, DUMMY),
	PM800_LDO(LDO6, LDO_ENA1_1, LDO_ENA2_1, 5, 200000, 2, LDO_SLP2, 6, 0, DUMMY),
	PM800_LDO(LDO7, LDO_ENA1_1, LDO_ENA2_1, 6, 100000, 4, LDO_SLP2, 6, 0, DUMMY),
	PM800_LDO(LDO8, LDO_ENA1_1, LDO_ENA2_1, 7, 100000, 6, LDO_SLP2, 6, 0, DUMMY),
	PM800_LDO(LDO9, LDO_ENA1_2, LDO_ENA2_2, 0, 200000, 0, LDO_SLP3, 6, 0, DUMMY),
	PM800_LDO(LDO10, LDO_ENA1_2, LDO_ENA2_2, 1, 400000, 2, LDO_SLP3, 6, 0, DUMMY),
	PM800_LDO(LDO11, LDO_ENA1_2, LDO_ENA2_2, 2, 200000, 4, LDO_SLP3, 6, 0, DUMMY),
	PM800_LDO(LDO12, LDO_ENA1_2, LDO_ENA2_2, 3, 400000, 6, LDO_SLP3, 7, 0, DUMMY),
	PM800_LDO(LDO13, LDO_ENA1_2, LDO_ENA2_2, 4, 180000, 0, LDO_SLP4, 8, 0, DUMMY),
	PM800_LDO(LDO14, LDO_ENA1_2, LDO_ENA2_2, 5, 8000, 2, LDO_SLP4, 9, 0, DUMMY),
	PM800_LDO(VOUTSW, MISC_EN1, MISC_EN2, 4, 0, 0, LDO_SLP4, _voutsw, 0, DUMMY),
};

static struct pm800_regulator_info pm86x_regulator_info[] = {
	/*PM800_BUCK(chip, vreg, ereg, e2reg, ebit, amax, sebit, sereg, v_range, set, amod, areg)*/
	PM800_BUCK(PM800, BUCK1, BUCK_ENA, BUCK_ENA2, 0, 3000000, 0, BUCK_SLP1, 1, 4, 1, BUCK1_AUD),
	PM800_BUCK(PM800, BUCK2, BUCK_ENA, BUCK_ENA2, 1, 750000, 2, BUCK_SLP1, 2, 1, 0, DUMMY),
	PM800_BUCK(PM800, BUCK3, BUCK_ENA, BUCK_ENA2, 2, 1500000, 4, BUCK_SLP1, 2, 4, 0, DUMMY),
	PM800_BUCK(PM860, BUCK4, BUCK_ENA, BUCK_ENA2, 3, 750000, 6, BUCK_SLP1, 2, 1, 0, DUMMY),
	PM800_BUCK(PM800, BUCK5, BUCK_ENA, BUCK_ENA2, 4, 1500000, 0, BUCK_SLP2, 3, 4, 0, DUMMY),
	PM800_BUCK(PM800, BUCK6, BUCK_ENA, BUCK_ENA2, 5, 800000, 2, BUCK_SLP2, 2, 1, 0, DUMMY),
	PM800_BUCK(PM800, BUCK1B, BUCK_ENA, BUCK_ENA2, 6, 3000000, 4, BUCK_SLP2, 2, 4, 0, DUMMY),

	/* PM800_LDO(vreg, ereg, e2reg, ebit, amax, sebit, sereg, ldo_table, amod, areg) */
	PM800_LDO(LDO1, LDO_ENA1_1, LDO_ENA2_1, 0, 100000, 0, LDO_SLP1, 5, 0, DUMMY),
	PM800_LDO(LDO2, LDO_ENA1_1, LDO_ENA2_1, 1, 100000, 2, LDO_SLP1, 5, 1, LDO2_AUD),
	PM800_LDO(LDO3, LDO_ENA1_1, LDO_ENA2_1, 2, 100000, 4, LDO_SLP1, 5, 0, DUMMY),
	PM800_LDO(LDO4, LDO_ENA1_1, LDO_ENA2_1, 3, 400000, 6, LDO_SLP1, 6, 0, DUMMY),
	PM800_LDO(LDO5, LDO_ENA1_1, LDO_ENA2_1, 4, 400000, 0, LDO_SLP2, 6, 0, DUMMY),
	PM800_LDO(LDO6, LDO_ENA1_1, LDO_ENA2_1, 5, 400000, 2, LDO_SLP2, 6, 0, DUMMY),
	PM800_LDO(LDO7, LDO_ENA1_1, LDO_ENA2_1, 6, 400000, 4, LDO_SLP2, 6, 0, DUMMY),
	PM800_LDO(LDO8, LDO_ENA1_1, LDO_ENA2_1, 7, 400000, 6, LDO_SLP2, 6, 0, DUMMY),
	PM800_LDO(LDO9, LDO_ENA1_2, LDO_ENA2_2, 0, 400000, 0, LDO_SLP3, 6, 0, DUMMY),
	PM800_LDO(LDO10, LDO_ENA1_2, LDO_ENA2_2, 1, 200000, 2, LDO_SLP3, 6, 0, DUMMY),
	PM800_LDO(LDO11, LDO_ENA1_2, LDO_ENA2_2, 2, 200000, 4, LDO_SLP3, 6, 0, DUMMY),
	PM800_LDO(LDO12, LDO_ENA1_2, LDO_ENA2_2, 3, 200000, 6, LDO_SLP3, 6, 0, DUMMY),
	PM800_LDO(LDO13, LDO_ENA1_2, LDO_ENA2_2, 4, 200000, 0, LDO_SLP4, 6, 0, DUMMY),
	PM800_LDO(LDO14, LDO_ENA1_2, LDO_ENA2_2, 5, 200000, 2, LDO_SLP4, 6, 0, DUMMY),
	PM800_LDO(LDO15, LDO_ENA1_2, LDO_ENA2_2, 6, 200000, 4, LDO_SLP4, 6, 0, DUMMY),
	PM800_LDO(LDO16, LDO_ENA1_2, LDO_ENA2_2, 7, 200000, 6, LDO_SLP4, 6, 0, DUMMY),
	PM800_LDO(LDO17, LDO_ENA1_3, LDO_ENA2_3, 0, 200000, 0, LDO_SLP5, 6, 0, DUMMY),
	PM800_LDO(LDO18, LDO_ENA1_3, LDO_ENA2_3, 1, 200000, 2, LDO_SLP5, 6, 0, DUMMY),
	PM800_LDO(LDO19, LDO_ENA1_3, LDO_ENA2_3, 2, 400000, 4, LDO_SLP5, 1, 0, DUMMY),
	PM800_LDO(LDO20, LDO_ENA1_3, LDO_ENA2_3, 3, 10000, 6, LDO_SLP5, 2, 0, DUMMY),
};
#define PM800_REGULATOR_OF_MATCH(id)					\
	{								\
		.name = "88PM800-" #id,					\
		.driver_data = &pm800_regulator_info[PM800_ID_##id],	\
	}

#define PM822_REGULATOR_OF_MATCH(id)					\
	{								\
		.name = "88PM800-" #id,					\
		.driver_data = &pm822_regulator_info[PM800_ID_##id],	\
	}

#define PM86X_REGULATOR_OF_MATCH(id)					\
	{								\
		.name = "88PM800-" #id,					\
		.driver_data = &pm86x_regulator_info[PM800_ID_##id],	\
	}

static struct of_regulator_match pm800_regulator_matches[] = {
	PM800_REGULATOR_OF_MATCH(BUCK1),
	PM800_REGULATOR_OF_MATCH(BUCK2),
	PM800_REGULATOR_OF_MATCH(BUCK3),
	PM800_REGULATOR_OF_MATCH(BUCK4),
	PM800_REGULATOR_OF_MATCH(BUCK5),
	PM800_REGULATOR_OF_MATCH(LDO1),
	PM800_REGULATOR_OF_MATCH(LDO2),
	PM800_REGULATOR_OF_MATCH(LDO3),
	PM800_REGULATOR_OF_MATCH(LDO4),
	PM800_REGULATOR_OF_MATCH(LDO5),
	PM800_REGULATOR_OF_MATCH(LDO6),
	PM800_REGULATOR_OF_MATCH(LDO7),
	PM800_REGULATOR_OF_MATCH(LDO8),
	PM800_REGULATOR_OF_MATCH(LDO9),
	PM800_REGULATOR_OF_MATCH(LDO10),
	PM800_REGULATOR_OF_MATCH(LDO11),
	PM800_REGULATOR_OF_MATCH(LDO12),
	PM800_REGULATOR_OF_MATCH(LDO13),
	PM800_REGULATOR_OF_MATCH(LDO14),
	PM800_REGULATOR_OF_MATCH(LDO15),
	PM800_REGULATOR_OF_MATCH(LDO16),
	PM800_REGULATOR_OF_MATCH(LDO17),
	PM800_REGULATOR_OF_MATCH(LDO18),
	PM800_REGULATOR_OF_MATCH(LDO19),
};

static struct of_regulator_match pm822_regulator_matches[] = {
	PM822_REGULATOR_OF_MATCH(BUCK1),
	PM822_REGULATOR_OF_MATCH(BUCK2),
	PM822_REGULATOR_OF_MATCH(BUCK3),
	PM822_REGULATOR_OF_MATCH(BUCK4),
	PM822_REGULATOR_OF_MATCH(BUCK5),
	PM822_REGULATOR_OF_MATCH(LDO1),
	PM822_REGULATOR_OF_MATCH(LDO2),
	PM822_REGULATOR_OF_MATCH(LDO3),
	PM822_REGULATOR_OF_MATCH(LDO4),
	PM822_REGULATOR_OF_MATCH(LDO5),
	PM822_REGULATOR_OF_MATCH(LDO6),
	PM822_REGULATOR_OF_MATCH(LDO7),
	PM822_REGULATOR_OF_MATCH(LDO8),
	PM822_REGULATOR_OF_MATCH(LDO9),
	PM822_REGULATOR_OF_MATCH(LDO10),
	PM822_REGULATOR_OF_MATCH(LDO11),
	PM822_REGULATOR_OF_MATCH(LDO12),
	PM822_REGULATOR_OF_MATCH(LDO13),
	PM822_REGULATOR_OF_MATCH(LDO14),
	PM822_REGULATOR_OF_MATCH(VOUTSW),
};

static struct of_regulator_match pm86x_regulator_matches[] = {
	PM86X_REGULATOR_OF_MATCH(BUCK1A),
	PM86X_REGULATOR_OF_MATCH(BUCK2),
	PM86X_REGULATOR_OF_MATCH(BUCK3),
	PM86X_REGULATOR_OF_MATCH(BUCK4),
	PM86X_REGULATOR_OF_MATCH(BUCK5),
	PM86X_REGULATOR_OF_MATCH(BUCK6),
	PM86X_REGULATOR_OF_MATCH(BUCK1B),
	PM86X_REGULATOR_OF_MATCH(LDO1),
	PM86X_REGULATOR_OF_MATCH(LDO2),
	PM86X_REGULATOR_OF_MATCH(LDO3),
	PM86X_REGULATOR_OF_MATCH(LDO4),
	PM86X_REGULATOR_OF_MATCH(LDO5),
	PM86X_REGULATOR_OF_MATCH(LDO6),
	PM86X_REGULATOR_OF_MATCH(LDO7),
	PM86X_REGULATOR_OF_MATCH(LDO8),
	PM86X_REGULATOR_OF_MATCH(LDO9),
	PM86X_REGULATOR_OF_MATCH(LDO10),
	PM86X_REGULATOR_OF_MATCH(LDO11),
	PM86X_REGULATOR_OF_MATCH(LDO12),
	PM86X_REGULATOR_OF_MATCH(LDO13),
	PM86X_REGULATOR_OF_MATCH(LDO14),
	PM86X_REGULATOR_OF_MATCH(LDO15),
	PM86X_REGULATOR_OF_MATCH(LDO16),
	PM86X_REGULATOR_OF_MATCH(LDO17),
	PM86X_REGULATOR_OF_MATCH(LDO18),
	PM86X_REGULATOR_OF_MATCH(LDO19),
	PM86X_REGULATOR_OF_MATCH(LDO20),
};

/*
 * The function convert the ldo voltage register value
 * to a real voltage value (in uV) according to the voltage table.
 */
static int pm800_get_ldo_vol(unsigned int val, struct pm800_regulator_info *info)
{
	/* In the case of voutsw */
	if (info->desc.n_voltages == 0)
		return 0;
	else if (!info->desc.volt_table[val])
		return -EINVAL;
	else
		return info->desc.volt_table[val];
}

/* The function return the value in the regulator voltage register */
static unsigned int pm800_check_vol(struct regmap *map, unsigned int reg, unsigned int mask)
{
	int ret;
	unsigned int vol_val;

	ret = regmap_bulk_read(map, reg, &vol_val, 1);
	if (ret < 0)
		return ret;

	/* mask and shift the relevant value from the register */
	vol_val = (vol_val & mask) >> (ffs(mask) - 1);

	return vol_val;
}

static int pm800_check_hsdet_en(struct pm80x_chip *chip)
{
	struct regmap *map = chip->regmap;
	int ret, value;
	unsigned int en1, en2;

	switch (chip->type) {
	case CHIP_PM800:
		ret = regmap_read(map, PM800_HEADSET_CNTRL, &en1);
		if (ret < 0)
			return ret;
		ret = regmap_read(map, PM800_GPIO_2_3_CNTRL, &en2);
		if (ret < 0)
			return ret;

		value  = (en1 & PM800_HSDET_AUTO) && (en2 & PM800_GPIO3_VAL);
		break;
	case CHIP_PM822:
	case CHIP_PM86X:
		ret = regmap_read(map, PM800_HEADSET_CNTRL, &en1);
		if (ret < 0)
			return ret;
		ret = regmap_read(map, PM800_HEADSET_CNTRL2, &en2);
		if (ret < 0)
			return ret;
		/*
		 * check that auto mode is on (PM800_HSDET_AUTO bit is 0)
		 * and the status bit is the same as polarity bit
		 *
		 */
		value  = !(en1 & PM800_HSDET_AUTO) &&
				(!(en2 & PM800_HEADSET_STATUS) == !(en2 & PM800_HEADSET_POL));
		break;
	default:
		pr_err("Cannot find chip type\n");
		return -ENODEV;
	}

	return value;
}

/* The function check if the regulator register is configured to enable/disable */
static int pm800_check_en(struct pm80x_chip *chip, unsigned int reg, unsigned int mask,
			unsigned int reg2, int hs_check)
{
	struct regmap *map = chip->subchip->regmap_power;
	int ret, value;
	unsigned int enable1, enable2;

	ret = regmap_read(map, reg, &enable1);
	if (ret < 0)
		return ret;

	ret = regmap_read(map, reg2, &enable2);
	if (ret < 0)
		return ret;

	value = (enable1 | enable2) & mask;

	if ((!value) && (hs_check))
		value = pm800_check_hsdet_en(chip);

	return value;
}

/* The function check the regulator sleep mode as configured in his register */
static int pm800_check_slp_mode(struct regmap *map, unsigned int reg, int off)
{
	int ret;
	unsigned int slp_mode;

	ret = regmap_read(map, reg, &slp_mode);
	if (ret < 0)
		return ret;

	slp_mode = (slp_mode >> off) & 0x3;

	return slp_mode;
}

/* The function update the values in the print template struct */
static int pm800_update_print(struct pm80x_chip *chip, struct pm800_regulator_info *info,
		struct pm80x_buckldo_print *print_temp, int index, int buck_num, int hs_index)
{
	int ret, i, set_num, volt;
	struct regmap *map = chip->subchip->regmap_power;
	int max_set_num = sizeof(print_temp->sets)/sizeof(print_temp->sets[0]);
	int (*get_volt)(unsigned int, struct pm800_regulator_info*);
	char *slp_mode_str[] = {"off", "active_slp", "sleep", "active"};
	int slp_mode_num = sizeof(slp_mode_str)/sizeof(slp_mode_str[0]);

	set_num = info[index].num_of_sets;
	if (index < buck_num)
		get_volt = &pm800_get_vbuck_vol;
	else
		get_volt = &pm800_get_ldo_vol;

	if (info[index].desc.id == hs_index)
		sprintf(print_temp->name, "%s", "MIC_BIAS");
	else
		sprintf(print_temp->name, "%s", info[index].desc.name);

	/* check enable/disable */
	ret = pm800_check_en(chip, info[index].desc.enable_reg, info[index].desc.enable_mask,
		info[index].enable2_reg, info[index].desc.id == hs_index);
	if (ret < 0)
		return ret;
	else if (ret)
		strcpy(print_temp->enable, "enable");
	else
		strcpy(print_temp->enable, "disable");

	/* check sleep mode */
	ret = pm800_check_slp_mode(map, info[index].sleep_enable_reg, info[index].sleep_enable_bit);
	if (ret < 0)
		return ret;
	if (ret < slp_mode_num)
		strcpy(print_temp->slp_mode, slp_mode_str[ret]);
	else
		strcpy(print_temp->slp_mode, "unknown");

	/* print active voltage(s) */
	for (i = 0; i < max_set_num; i++) {
		if (i < set_num) {
			ret = pm800_check_vol(map, info[index].desc.vsel_reg+i,
					info[index].desc.vsel_mask);
			if (ret < 0)
				return ret;

			volt = (*get_volt)(ret, &info[index]);
			if (volt < 0)
				return volt;
			else
				sprintf(print_temp->sets[i], "%4d", volt/1000);
		} else
			sprintf(print_temp->sets[i], "  -");
	}

	/* print sleep voltage */
	ret = pm800_check_vol(map, info[index].sleep_vsel_reg, info[index].sleep_vsel_mask);
	if (ret < 0)
		return ret;

	volt = (*get_volt)(ret, &info[index]);
	if (volt < 0)
		return volt;
	else
		sprintf(print_temp->set_slp, "%4d", volt/1000);

	/* print audio voltage */
	if (info[index].audio_mode) {
		ret = pm800_check_vol(map, info[index].vaudio_reg, info[index].vaudio_mask);
		if (ret < 0)
			return ret;

		volt = (*get_volt)(ret, &info[index]);
		if (volt < 0)
			return volt;
		else
			sprintf(print_temp->audio, "%4d", volt/1000);
	} else
		sprintf(print_temp->audio, "  -");

	return 0;
}

int pm800_display_regulator(struct pm80x_chip *chip, char *buf)
{
	struct pm80x_buckldo_print *print_temp;
	struct pm800_regulator_info *info;
	int hs_index, buck_num, ldo_num, i, len = 0;
	ssize_t ret;

	switch (chip->type) {
	case CHIP_PM800:
		info = pm800_regulator_info;
		buck_num = PM800_NUM_BUCK;
		ldo_num = PM800_NUM_LDO;
		hs_index = PM800_ID_LDO2;
		break;
	case CHIP_PM822:
		info = pm822_regulator_info;
		buck_num = PM822_NUM_BUCK;
		ldo_num = PM860_NUM_LDO;
		hs_index = PM800_ID_LDO14;
		break;
	case CHIP_PM86X:
		info = pm86x_regulator_info;
		buck_num = PM860_NUM_BUCK;
		ldo_num = PM860_NUM_LDO;
		hs_index = PM800_ID_LDO20;
		break;
	default:
		pr_err("Cannot find chip type\n");
		return -ENODEV;
	}

	print_temp = kmalloc(sizeof(struct pm80x_buckldo_print), GFP_KERNEL);

	if (!print_temp) {
		pr_err("Cannot allocate print template!\n");
		return -ENOMEM;
	}
	len += sprintf(buf + len, "\nBUCK & LDO");

	len += sprintf(buf + len, "\n------------------------------------------");
	len += sprintf(buf + len, "-----------------------------------------------\n");
	len += sprintf(buf + len, "|   name   | status  |  slp_mode  |set_slp ");
	len += sprintf(buf + len, "|  set0  |  set1  |  set2  |  set3  | audio  |\n");
	len += sprintf(buf + len, "------------------------------------------");
	len += sprintf(buf + len, "-----------------------------------------------\n");

	for (i = 0; i < (buck_num + ldo_num); i++) {
		if (i == buck_num) {
			len += sprintf(buf+len, "--------------------------------------------");
			len += sprintf(buf+len, "---------------------------------------------\n");
		}
		ret = pm800_update_print(chip, info, print_temp, i, buck_num, hs_index);
		if (ret < 0) {
			pr_err("Print of regulator %s failed\n", print_temp->name);
			goto out_print;
		}
		len += sprintf(buf + len, "| %-8s |", print_temp->name);
		len += sprintf(buf + len, " %-7s |", print_temp->enable);
		len += sprintf(buf + len, "  %-10s|", print_temp->slp_mode);
		len += sprintf(buf + len, "  %-5s |", print_temp->set_slp);
		len += sprintf(buf + len, "  %-5s |", print_temp->sets[0]);
		len += sprintf(buf + len, "  %-5s |", print_temp->sets[1]);
		len += sprintf(buf + len, "  %-5s |", print_temp->sets[2]);
		len += sprintf(buf + len, "  %-5s |", print_temp->sets[3]);
		len += sprintf(buf + len, "  %-5s |\n", print_temp->audio);
	}

	len += sprintf(buf + len, "------------------------------------------");
	len += sprintf(buf + len, "-----------------------------------------------\n");

	ret = len;
out_print:
	kfree(print_temp);
	return ret;
}

static int pm800_regulator_dt_init(struct platform_device *pdev,
	struct of_regulator_match **regulator_matches, int *range)
{
	struct pm80x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct device_node *np = pdev->dev.of_node;

	switch (chip->type) {
		case CHIP_PM800:
			*regulator_matches = pm800_regulator_matches;
			*range = ARRAY_SIZE(pm800_regulator_matches);
			break;
		case CHIP_PM822:
			*regulator_matches = pm822_regulator_matches;
			*range = ARRAY_SIZE(pm822_regulator_matches);
			break;
		case CHIP_PM86X:
			*regulator_matches = pm86x_regulator_matches;
			*range = ARRAY_SIZE(pm86x_regulator_matches);
			break;
		default:
			return -ENODEV;
	}

	return of_regulator_match(&pdev->dev, np, *regulator_matches, *range);
}

static int pm800_regulator_probe(struct platform_device *pdev)
{
	struct pm80x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm80x_platform_data *pdata = pdev->dev.parent->platform_data;
	struct pm800_regulators *pm800_data;
	struct pm800_regulator_info *info;
	struct regulator_config config = { };
	struct regulator_init_data *init_data;
	int i, ret, range = 0;
	struct of_regulator_match *regulator_matches;

	if (!pdata || pdata->num_regulators == 0) {
		if (IS_ENABLED(CONFIG_OF)) {
			ret = pm800_regulator_dt_init(pdev, &regulator_matches,
					&range);
			if (ret < 0)
				return ret;
		} else {
			return -ENODEV;
		}
	} else if (pdata->num_regulators) {
		/* Check whether num_regulator is valid. */
		unsigned int count = 0;
		for (i = 0; pdata->regulators[i]; i++)
			count++;
		if (count != pdata->num_regulators)
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	pm800_data = devm_kzalloc(&pdev->dev, sizeof(*pm800_data),
					GFP_KERNEL);
	if (!pm800_data) {
		dev_err(&pdev->dev, "Failed to allocate pm800_regualtors");
		return -ENOMEM;
	}

	pm800_data->map = chip->subchip->regmap_power;
	pm800_data->chip = chip;

	platform_set_drvdata(pdev, pm800_data);

	for (i = 0; i < range; i++) {
		if (!pdata || pdata->num_regulators == 0)
			init_data = regulator_matches->init_data;
		else
			init_data = pdata->regulators[i];
		if (!init_data) {
			dev_err(&pdev->dev, "%s not matched!\n",
					regulator_matches->name);
			regulator_matches++;
			continue;
		}
		info = regulator_matches->driver_data;
		config.dev = &pdev->dev;
		config.init_data = init_data;
		config.driver_data = info;
		config.regmap = pm800_data->map;
		config.of_node = regulator_matches->of_node;

		pm800_data->regulators[i] =
				regulator_register(&info->desc, &config);
		if (IS_ERR(pm800_data->regulators[i])) {
			ret = PTR_ERR(pm800_data->regulators[i]);
			dev_err(&pdev->dev, "Failed to register %s\n",
				info->desc.name);

			while (--i >= 0 && pm800_data->regulators[i])
				regulator_unregister(pm800_data->regulators[i]);

			return ret;
		}
		pm800_data->regulators[i]->constraints->valid_ops_mask |=
				(REGULATOR_CHANGE_DRMS | REGULATOR_CHANGE_MODE);
		pm800_data->regulators[i]->constraints->valid_modes_mask |=
				(REGULATOR_MODE_NORMAL | REGULATOR_MODE_IDLE);
		pm800_data->regulators[i]->constraints->input_uV = 1000;

		regulator_matches++;
	}

	return 0;
}

static int pm800_regulator_remove(struct platform_device *pdev)
{
	struct pm800_regulators *pm800_data = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < PM800_ID_RG_MAX && pm800_data->regulators[i]; i++)
		regulator_unregister(pm800_data->regulators[i]);

	return 0;
}

static struct platform_driver pm800_regulator_driver = {
	.driver		= {
		.name	= "88pm80x-regulator",
		.owner	= THIS_MODULE,
	},
	.probe		= pm800_regulator_probe,
	.remove		= pm800_regulator_remove,
};

static int __init pm800_regulator_init(void)
{
	return platform_driver_register(&pm800_regulator_driver);
}
subsys_initcall(pm800_regulator_init);

static void __exit pm800_regulator_exit(void)
{
	platform_driver_unregister(&pm800_regulator_driver);
}
module_exit(pm800_regulator_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joseph(Yossi) Hanin <yhanin@marvell.com>");
MODULE_DESCRIPTION("Regulator Driver for Marvell 88PM800 PMIC");
MODULE_ALIAS("platform:88pm800-regulator");
