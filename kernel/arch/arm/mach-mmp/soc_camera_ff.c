/*
 *  This file is separated from linux/arch/arm/mach-mmp/soc_camera_dkb.c
 *  to support soc camera sp1628 on HELN-LTE FF(Form Factor).
 *
 *  Copyright (C) 2014 Marvell Technology Group Ltd.
 *  Author: Peng Du <pengdu@marvell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include "soc_camera_ff.h"

#if defined(CONFIG_SOC_CAMERA_SP1628)

static struct regulator *c1_avdd_2v8;
static struct regulator *c1_dovdd_1v8;
static struct regulator *c1_af_2v8;
static struct regulator *c1_dvdd_1v2;

static int sp1628_sensor_power(struct device *dev, int on)
{
	int cam_enable = SP1628_PWDN_PIN;
	int cam_reset = SP1628_RESET_PIN;
	int ret;

	/* Get the regulators and never put it */
	/*
	 * The regulators is for sensor and should be in sensor driver
	 * As SoC camera does not support device tree,  leave it here
	 */

	if (!c1_avdd_2v8) {
		c1_avdd_2v8 = regulator_get(dev, "avdd_2v8");
		if (IS_ERR(c1_avdd_2v8)) {
			dev_warn(dev, "Failed to get regulator avdd_2v8\n");
			c1_avdd_2v8 = NULL;
		}
	}

	if (cpu_is_pxa1L88()) {
		if (!c1_dovdd_1v8) {
			c1_dovdd_1v8 = regulator_get(dev, "dovdd_1v8");
			if (IS_ERR(c1_dovdd_1v8)) {
				dev_warn(dev, "Failed to get regulator dovdd_1v8\n");
				c1_dovdd_1v8 = NULL;
			}
		}
	}

	if (!c1_af_2v8) {
		c1_af_2v8 = regulator_get(dev, "af_2v8");
		if (IS_ERR(c1_af_2v8)) {
			dev_warn(dev, "Failed to get regulator af_2v8\n");
			c1_af_2v8 = NULL;
		}
	}

	if (cpu_is_pxa1L88()) {
		if (!c1_dvdd_1v2) {
			c1_dvdd_1v2 = regulator_get(dev, "dvdd_1v2");
			if (IS_ERR(c1_dvdd_1v2)) {
				dev_warn(dev, "Failed to get regulator dvdd_1v2\n");
				c1_dvdd_1v2 = NULL;
			}
		}
	}

	if (gpio_request(cam_enable, "CAM2_POWER")) {
		dev_err(dev, "Request GPIO failed, gpio: %d\n", cam_enable);
		goto cam_enable_failed;
	}
	if (gpio_request(cam_reset, "CAM2_RESET")) {
		dev_err(dev, "Request GPIO failed, gpio: %d\n", cam_reset);
		goto cam_reset_failed;
	}

	if (on) {
		if (c1_avdd_2v8) {
			regulator_set_voltage(c1_avdd_2v8, 2800000, 2800000);
			ret = regulator_enable(c1_avdd_2v8);
			if (ret)
				goto cam_regulator_enable_failed;
		}

		if (c1_af_2v8) {
			regulator_set_voltage(c1_af_2v8, 2800000, 2800000);
			ret = regulator_enable(c1_af_2v8);
			if (ret)
				goto cam_regulator_enable_failed;
		}

		if (cpu_is_pxa1L88() || cpu_is_pxa1U88()) {
			if (c1_dovdd_1v8) {
				regulator_set_voltage(c1_dovdd_1v8,
						1800000, 1800000);
				ret = regulator_enable(c1_dovdd_1v8);
				if (ret)
					goto cam_regulator_enable_failed;
			}
		}

		if (cpu_is_pxa1L88() || cpu_is_pxa1U88()) {
			if (c1_dvdd_1v2) {
				regulator_set_voltage(c1_dvdd_1v2,
						1200000, 1200000);
				ret = regulator_enable(c1_dvdd_1v2);
				if (ret)
					goto cam_regulator_enable_failed;
			}
		}

		gpio_direction_output(cam_enable, 0);
		usleep_range(5000, 20000);
		gpio_direction_output(cam_reset, 0);
		usleep_range(5000, 20000);
		gpio_direction_output(cam_enable, 1);
		usleep_range(5000, 20000);
		gpio_direction_output(cam_enable, 0);
		usleep_range(5000, 20000);
		gpio_direction_output(cam_reset, 1);
		usleep_range(5000, 20000);
	} else {
		gpio_direction_output(cam_reset, 0);
		usleep_range(5000, 20000);
		gpio_direction_output(cam_enable, 0);

		if (cpu_is_pxa1L88() || cpu_is_pxa1U88())
			if (c1_dovdd_1v8)
				regulator_disable(c1_dovdd_1v8);
		if (cpu_is_pxa1L88() || cpu_is_pxa1U88())
			if (c1_dvdd_1v2)
				regulator_disable(c1_dvdd_1v2);
		if (c1_avdd_2v8)
			regulator_disable(c1_avdd_2v8);
		if (c1_af_2v8)
			regulator_disable(c1_af_2v8);

	}

	gpio_free(cam_enable);
	gpio_free(cam_reset);

	return 0;

cam_reset_failed:
	gpio_free(cam_enable);
cam_enable_failed:
	ret = -EIO;
cam_regulator_enable_failed:
	if (cpu_is_pxa1L88()) {
		if (c1_dvdd_1v2)
			regulator_put(c1_dvdd_1v2);
		c1_dvdd_1v2 = NULL;
	}
	if (c1_af_2v8)
		regulator_put(c1_af_2v8);
	c1_af_2v8 = NULL;
	if (cpu_is_pxa1L88()) {
		if (c1_dovdd_1v8)
			regulator_put(c1_dovdd_1v8);
		c1_dovdd_1v8 = NULL;
	}
	if (c1_avdd_2v8)
		regulator_put(c1_avdd_2v8);
	c1_avdd_2v8 = NULL;
	if (cpu_is_pxa1L88()) {
		if (c1_dovdd_1v8)
			regulator_put(c1_dovdd_1v8);
		c1_dovdd_1v8 = NULL;
	}
	if (c1_avdd_2v8)
		regulator_put(c1_avdd_2v8);
	c1_avdd_2v8 = NULL;

	return ret;
}

static struct sensor_board_data sp1628_data = {
	.mount_pos	= SENSOR_USED | SENSOR_POS_FRONT | SENSOR_RES_LOW,
	.bus_type	= V4L2_MBUS_CSI2,
	.bus_flag	= V4L2_MBUS_CSI2_1_LANE,
	.dphy = {0xFF1D00, 0x00024733, 0x04001001},
};

static struct i2c_board_info dkb_i2c_sp1628 = {
		I2C_BOARD_INFO("sp1628", 0x3c),
};

struct soc_camera_desc soc_camera_desc_3 = {
	.subdev_desc = {
		.power          = sp1628_sensor_power,
		.drv_priv		= &sp1628_data,
		.flags		= 0,
	},
	.host_desc = {
		.bus_id = SP1628_CCIC_PORT,	/* Default as ccic2 */
		.i2c_adapter_id = 1,
		.board_info     = &dkb_i2c_sp1628,
		.module_name    = "sp1628",
	},
};
#endif
