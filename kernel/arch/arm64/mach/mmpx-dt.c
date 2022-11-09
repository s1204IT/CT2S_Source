/*
 *  linux/arch/arm64/mach/mmpx-dt.c
 *
 *  Copyright (C) 2012 Marvell Technology Group Ltd.
 *  Author: Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 *  Copyright (C) 2015 SANYO Techno Solutions Tottori Co., Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/clocksource.h>
#include <linux/gpio.h>
#include <linux/memblock.h>
#include <linux/platform_data/mv_usb.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/pxa1928_smc.h>
#include <linux/clk.h>
#include <asm/device_mapping.h>

#include <asm/mach/mach_desc.h>

#include <media/soc_camera.h>
#include <media/mrvl-camera.h>

#include "mmpx-dt.h"
#include "fuseinfo-pxa1928.h"

#ifdef CONFIG_LEDS_REGULATOR
#include <linux/leds-regulator.h>
#endif

#ifdef CONFIG_CHARGER_BQ24160
#include <linux/power/bq24160_charger.h>
#endif /* CONFIG_CHARGER_BQ24160 */

#ifdef CONFIG_USB_SUPPORT
static struct mv_usb_platform_data pxa1928_usb_pdata = {
	.mode		= MV_USB_MODE_OTG,
#ifdef CONFIG_USB_88PM830
	.extern_attr	= MV_USB_HAS_VBUS_DETECTION |
			MV_USB_HAS_IDPIN_DETECTION,
#endif /* CONFIG_USB_88PM830 */
	.id             = PXA_USB_DEV_OTG,
	.otg_force_a_bus_req = 1,
#ifdef CONFIG_CHARGER_BQ24160
	.set_vbus	= bq24160_set_vbus,
	.charger_detect	= bq24160_charger_detect,
#endif /* CONFIG_CHARGER_BQ24160 */
};

#ifdef CONFIG_USB_EHCI_MV_U2H_HSIC
static struct mv_usb_platform_data pxa1928_hsic_pdata = {
	.mode		= MV_USB_MODE_HOST,
	.id		= PXA_USB_DEV_SPH1,
};
#endif

#endif

#ifdef CONFIG_LEDS_REGULATOR

static struct platform_device keypad_backlight = {
	.name = "button-backlight",
};

#endif

#ifdef CONFIG_SOC_CAMERA_SP0A20_ECS
static int sp0a20_sensor_power(struct device *dev, int on)
{
	static struct regulator *avdd_2v8;
	static struct regulator *dovdd_1v8;
	static struct regulator *af_2v8;
	static struct regulator *dvdd_1v2;

	int cam_enable, cam_reset;
	int ret;

	/* Get the regulators and never put it */
	/*
	 * The regulators is for sensor and should be in sensor driver
	 * As SoC camera does not support device tree,  leave it here
	 */

	if (!avdd_2v8) {
		avdd_2v8 = regulator_get(dev, "avdd_2v8");
		if (IS_ERR(avdd_2v8)) {
			dev_warn(dev, "Failed to get regulator avdd_2v8\n");
			avdd_2v8 = NULL;
		}
	}

		if (!dovdd_1v8) {
			dovdd_1v8 = regulator_get(dev, "dovdd_1v8");
			if (IS_ERR(dovdd_1v8)) {
				dev_warn(dev, "Failed to get regulator dovdd_1v8\n");
				dovdd_1v8 = NULL;
			}
		}

	if (!af_2v8) {
		af_2v8 = regulator_get(dev, "af_2v8");
		if (IS_ERR(af_2v8)) {
			dev_warn(dev, "Failed to get regulator af_2v8\n");
			af_2v8 = NULL;
		}
	}

		if (!dvdd_1v2) {
			dvdd_1v2 = regulator_get(dev, "dvdd_1v2");
			if (IS_ERR(dvdd_1v2)) {
				dev_warn(dev, "Failed to get regulator dvdd_1v2\n");
				dvdd_1v2 = NULL;
			}
		}

	cam_enable = of_get_named_gpio(dev->of_node, "pwdn-gpios", 0);
	if (gpio_is_valid(cam_enable)) {
		if (gpio_request(cam_enable, "CAM2_POWER")) {
			dev_err(dev, "Request GPIO %d failed\n", cam_enable);
			goto cam_enable_failed;
		}
	} else {
		dev_err(dev, "invalid pwdn gpio %d\n", cam_enable);
		goto cam_enable_failed;
	}


	cam_reset = of_get_named_gpio(dev->of_node, "reset-gpios", 0);
	if (gpio_is_valid(cam_reset)) {
		if (gpio_request(cam_reset, "CAM2_RESET")) {
			dev_err(dev, "Request GPIO %d failed\n", cam_reset);
			goto cam_reset_failed;
		}
	} else {
		dev_err(dev, "invalid pwdn gpio %d\n", cam_reset);
		goto cam_reset_failed;
	}

	if (on) {
		if (avdd_2v8) {
			regulator_set_voltage(avdd_2v8, 2800000, 2800000);
			ret = regulator_enable(avdd_2v8);
			if (ret)
				goto cam_regulator_enable_failed;
		}

		if (af_2v8) {
			regulator_set_voltage(af_2v8, 2800000, 2800000);
			ret = regulator_enable(af_2v8);
			if (ret)
				goto cam_regulator_enable_failed;
		}

		if (dovdd_1v8) {
			regulator_set_voltage(dovdd_1v8,
					1800000, 1800000);
			ret = regulator_enable(dovdd_1v8);
			if (ret)
				goto cam_regulator_enable_failed;
		}

		if (dvdd_1v2) {
			regulator_set_voltage(dvdd_1v2,
					1200000, 1200000);
			ret = regulator_enable(dvdd_1v2);
			if (ret)
					goto cam_regulator_enable_failed;
			}

		gpio_direction_output(cam_enable, 0);
		usleep_range(5000, 20000);
		gpio_direction_output(cam_reset, 1);
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
		/*
		 * Keep RESET always on as defined in spec
		 * gpio_direction_output(cam_reset, 0);
		 * usleep_range(5000, 20000);
		 */
		gpio_direction_output(cam_enable, 0);
		usleep_range(5000, 20000);
		gpio_direction_output(cam_enable, 1);
		usleep_range(5000, 20000);

		if (dovdd_1v8)
			regulator_disable(dovdd_1v8);
		usleep_range(5000, 20000);
		if (dvdd_1v2)
			regulator_disable(dvdd_1v2);
		if (avdd_2v8)
			regulator_disable(avdd_2v8);
		usleep_range(5000, 20000);
		if (af_2v8)
			regulator_disable(af_2v8);

	}

	gpio_free(cam_enable);
	gpio_free(cam_reset);

	return 0;

cam_reset_failed:
	gpio_free(cam_enable);
cam_enable_failed:
	ret = -EIO;
cam_regulator_enable_failed:
	if (dvdd_1v2)
		regulator_put(dvdd_1v2);
	dvdd_1v2 = NULL;

	if (af_2v8)
		regulator_put(af_2v8);
	af_2v8 = NULL;

	if (dovdd_1v8)
		regulator_put(dovdd_1v8);
	dovdd_1v8 = NULL;

	if (avdd_2v8)
		regulator_put(avdd_2v8);
	avdd_2v8 = NULL;

	return ret;
}

static struct sensor_board_data sp0a20_data = {
	.mount_pos	= SENSOR_USED | SENSOR_POS_FRONT | SENSOR_RES_LOW,
	.bus_type	= V4L2_MBUS_CSI2,
	.bus_flag	= V4L2_MBUS_CSI2_1_LANE,
	.flags = V4L2_MBUS_CSI2_1_LANE,
	.dphy = {0x0D06, 0x0011, 0x0900},
};

static struct i2c_board_info dkb_i2c_sp0a20 = {
		I2C_BOARD_INFO("sp0a20", 0x21),
};

struct soc_camera_desc sp0a20_desc = {
	.subdev_desc = {
		.power          = sp0a20_sensor_power,
		.drv_priv		= &sp0a20_data,
		.flags		= 0,
	},
	.host_desc = {
		.bus_id = 1,	/* Default as ccic2 */
		.i2c_adapter_id = 2,
		.board_info     = &dkb_i2c_sp0a20,
		.module_name    = "sp0a20",
	},
};
#endif

#ifdef CONFIG_SOC_CAMERA_S5K8AA
#define CCIC2_PWDN_GPIO 13
#define CCIC2_RESET_GPIO 111
static int s5k8aa_sensor_power(struct device *dev, int on)
{
	static struct regulator *avdd_2v8;
	static struct regulator *dovdd_1v8;
	static struct regulator *af_2v8;
	static struct regulator *dvdd_1v2;

	int cam_enable, cam_reset;
	int ret;

	/* Get the regulators and never put it */
	/*
	 * The regulators is for sensor and should be in sensor driver
	 * As SoC camera does not support device tree, adding code here
	 */

	if (!avdd_2v8) {
		avdd_2v8 = regulator_get(dev, "avdd_2v8");
		if (IS_ERR(avdd_2v8)) {
			dev_warn(dev, "Failed to get regulator avdd_2v8\n");
			avdd_2v8 = NULL;
		}
	}

	if (!dovdd_1v8) {
		dovdd_1v8 = regulator_get(dev, "dovdd_1v8");
		if (IS_ERR(dovdd_1v8)) {
			dev_warn(dev, "Failed to get regulator dovdd_1v8\n");
			dovdd_1v8 = NULL;
		}
	}

	if (!af_2v8) {
		af_2v8 = regulator_get(dev, "af_2v8");
		if (IS_ERR(af_2v8)) {
			dev_warn(dev, "Failed to get regulator af_2v8\n");
			af_2v8 = NULL;
		}
	}

	if (!dvdd_1v2) {
		dvdd_1v2 = regulator_get(dev, "dvdd_1v2");
		if (IS_ERR(dvdd_1v2)) {
			dev_warn(dev, "Failed to get regulator dvdd_1v2\n");
			dvdd_1v2 = NULL;
		}
	}

	cam_enable = of_get_named_gpio(dev->of_node, "pwdn-gpios", 0);
	if (gpio_is_valid(cam_enable)) {
		if (gpio_request(cam_enable, "CAM2_POWER")) {
			dev_err(dev, "Request GPIO %d failed\n", cam_enable);
			goto cam_enable_failed;
		}
	} else {
		dev_err(dev, "invalid pwdn gpio %d\n", cam_enable);
		goto cam_enable_failed;
	}

	cam_reset = of_get_named_gpio(dev->of_node, "reset-gpios", 0);
	if (gpio_is_valid(cam_reset)) {
		if (gpio_request(cam_reset, "CAM2_RESET")) {
			dev_err(dev, "Request GPIO %d failed\n", cam_reset);
			goto cam_reset_failed;
		}
	} else {
		dev_err(dev, "invalid pwdn gpio %d\n", cam_reset);
		goto cam_reset_failed;
	}

	if (on) {
		if (avdd_2v8) {
			regulator_set_voltage(avdd_2v8, 2800000, 2800000);
			ret = regulator_enable(avdd_2v8);
			if (ret)
				goto cam_regulator_enable_failed;
		}

		if (af_2v8) {
			regulator_set_voltage(af_2v8, 2800000, 2800000);
			ret = regulator_enable(af_2v8);
			if (ret)
				goto cam_regulator_enable_failed;
		}

		if (dvdd_1v2) {
			regulator_set_voltage(dvdd_1v2, 1200000, 1200000);
			ret = regulator_enable(dvdd_1v2);
			if (ret)
				goto cam_regulator_enable_failed;
		}

		if (dovdd_1v8) {
			regulator_set_voltage(dovdd_1v8, 1800000, 1800000);
			ret = regulator_enable(dovdd_1v8);
			if (ret)
				goto cam_regulator_enable_failed;
		}

		gpio_direction_output(cam_reset, 0);
		usleep_range(5000, 20000);
		gpio_direction_output(cam_enable, 1);
		usleep_range(5000, 20000);
		gpio_direction_output(cam_reset, 1);
		usleep_range(5000, 20000);
	} else {
		gpio_direction_output(cam_enable, 0);
		usleep_range(5000, 20000);
		gpio_direction_output(cam_reset, 0);

		if (dovdd_1v8)
			regulator_disable(dovdd_1v8);
		if (dvdd_1v2)
			regulator_disable(dvdd_1v2);
		if (avdd_2v8)
			regulator_disable(avdd_2v8);
		if (af_2v8)
			regulator_disable(af_2v8);
	}

	gpio_free(cam_enable);
	gpio_free(cam_reset);

	return 0;

cam_reset_failed:
	gpio_free(cam_enable);
cam_enable_failed:
	ret = -EIO;
cam_regulator_enable_failed:
	if (dvdd_1v2)
		regulator_put(dvdd_1v2);
	dvdd_1v2 = NULL;
	if (af_2v8)
		regulator_put(af_2v8);
	af_2v8 = NULL;
	if (dovdd_1v8)
		regulator_put(dovdd_1v8);
	dovdd_1v8 = NULL;
	if (avdd_2v8)
		regulator_put(avdd_2v8);
	avdd_2v8 = NULL;

	return ret;
}

static struct sensor_board_data s5k8aa_data = {
	.mount_pos	= SENSOR_USED | SENSOR_POS_FRONT | SENSOR_RES_LOW,
	.bus_type	= V4L2_MBUS_CSI2,
	.bus_flag	= V4L2_MBUS_CSI2_1_LANE,
	.dphy = {0xFF1D00, 0x00024733, 0x04001001}, /* obsoleted, using dt */
	.flags = V4L2_MBUS_CSI2_1_LANE,
};

static struct i2c_board_info concord_i2c_camera = {
	I2C_BOARD_INFO("s5k8aay", 0x3c),
};

static struct soc_camera_desc s5k8aa_desc = {
	.subdev_desc = {
		.power			= s5k8aa_sensor_power,
		.drv_priv		= &s5k8aa_data,
		.flags		= 0,
	},
	.host_desc = {
		.bus_id = 1,	/* Must match with the camera ID */
		.i2c_adapter_id = 2,	/* twsi 3 */
		.board_info		= &concord_i2c_camera,
		.module_name	= "s5k8aay",
	},
};
#endif

#ifdef CONFIG_SOC_CAMERA_S5K4ECGX
static struct sensor_board_data s5k4ecgx_data = {
	.mount_pos	= SENSOR_USED | SENSOR_POS_BACK | SENSOR_RES_HIGH,
	.bus_type	= V4L2_MBUS_CSI2,
	.bus_flag	= V4L2_MBUS_CSI2_2_LANE,
	.dphy = {0xff1c09, 0x0033, 0x0c00}, /* obsoleted, using dt */
	.flags = V4L2_MBUS_CSI2_2_LANE,
	.manual_mclk_ctrl = 1,
};

static int s5k4ecgx_sensor_power(struct device *dev, int on)
{
	static struct regulator *avdd_2v8;
	static struct regulator *dovdd_1v8;
	static struct regulator *af_2v8;
	static struct regulator *dvdd_1v2;

	struct clk *mclk = s5k4ecgx_data.mclk;

	int cam_enable, cam_reset;
	int ret = -EIO;

	dev_info(dev, "s5k4ecgx: power %s\n", on? "on":"off");

	if (!mclk) {
		dev_err(dev, "s5k4ecgx: mclk is not set.\n");
		return -EINVAL;
	}

	/* Get the regulators and never put it */
	/*
	 * The regulators is for sensor and should be in sensor driver
	 * As SoC camera does not support device tree, adding code here
	 */

	if (!avdd_2v8) {
		avdd_2v8 = regulator_get(dev, "avdd_2v8");
		if (IS_ERR(avdd_2v8)) {
			dev_warn(dev, "Failed to get regulator avdd_2v8\n");
			avdd_2v8 = NULL;
		}
	}

	if (!dovdd_1v8) {
		dovdd_1v8 = regulator_get(dev, "dovdd_1v8");
		if (IS_ERR(dovdd_1v8)) {
			dev_warn(dev, "Failed to get regulator dovdd_1v8\n");
			dovdd_1v8 = NULL;
		}
	}

	if (!af_2v8) {
		af_2v8 = regulator_get(dev, "af_2v8");
		if (IS_ERR(af_2v8)) {
			dev_warn(dev, "Failed to get regulator af_2v8\n");
			af_2v8 = NULL;
		}
	}

	if (!dvdd_1v2) {
		dvdd_1v2 = regulator_get(dev, "dvdd_1v2");
		if (IS_ERR(dvdd_1v2)) {
			dev_warn(dev, "Failed to get regulator dvdd_1v2\n");
			dvdd_1v2 = NULL;
		}
	}

	cam_enable = of_get_named_gpio(dev->of_node, "pwdn-gpios", 0);
	if (gpio_is_valid(cam_enable)) {
		if (gpio_request(cam_enable, "CAM2_POWER")) {
			dev_err(dev, "Request GPIO %d failed\n", cam_enable);
			goto cam_enable_failed;
		}
	} else {
		dev_err(dev, "invalid pwdn gpio %d\n", cam_enable);
		goto cam_enable_failed;
	}

	cam_reset = of_get_named_gpio(dev->of_node, "reset-gpios", 0);
	if (gpio_is_valid(cam_reset)) {
		if (gpio_request(cam_reset, "CAM2_RESET")) {
			dev_err(dev, "Request GPIO %d failed\n", cam_reset);
			goto cam_reset_failed;
		}
	} else {
		dev_err(dev, "invalid reset gpio %d\n", cam_reset);
		goto cam_reset_failed;
	}

	if (on) {
		if (avdd_2v8) {
			regulator_set_voltage(avdd_2v8, 2800000, 2800000);
			ret = regulator_enable(avdd_2v8);
			if (ret)
				goto cam_regulator_enable_failed;
		}

		if (af_2v8) {
			regulator_set_voltage(af_2v8, 2800000, 2800000);
			ret = regulator_enable(af_2v8);
			if (ret)
				goto cam_regulator_enable_failed;
		}

		if (dvdd_1v2) {
			regulator_set_voltage(dvdd_1v2, 1200000, 1200000);
			ret = regulator_enable(dvdd_1v2);
			if (ret)
				goto cam_regulator_enable_failed;
		}

		if (dovdd_1v8) {
			regulator_set_voltage(dovdd_1v8, 1800000, 1800000);
			ret = regulator_enable(dovdd_1v8);
			if (ret)
				goto cam_regulator_enable_failed;
		}

		clk_set_rate(mclk, 26000000);
		clk_prepare_enable(mclk);
		usleep_range(50000, 100000);
		gpio_direction_output(cam_enable, 1);
		usleep_range(50000, 100000);
		gpio_direction_output(cam_reset, 1);
		usleep_range(50000, 100000);
	}
	else {
		gpio_direction_output(cam_reset, 0);
		usleep_range(50000, 100000);
		gpio_direction_output(cam_enable, 0);
		clk_disable_unprepare(mclk);

		if (dovdd_1v8)
			regulator_disable(dovdd_1v8);
		if (dvdd_1v2)
			regulator_disable(dvdd_1v2);
		if (avdd_2v8)
			regulator_disable(avdd_2v8);
		if (af_2v8)
			regulator_disable(af_2v8);
	}

	gpio_free(cam_reset);
	gpio_free(cam_enable);

	return 0;

cam_regulator_enable_failed:
	if (dvdd_1v2)
		regulator_put(dvdd_1v2);
	dvdd_1v2 = NULL;
	if (af_2v8)
		regulator_put(af_2v8);
	af_2v8 = NULL;
	if (dovdd_1v8)
		regulator_put(dovdd_1v8);
	dovdd_1v8 = NULL;
	if (avdd_2v8)
		regulator_put(avdd_2v8);
	avdd_2v8 = NULL;

	gpio_free(cam_reset);

cam_reset_failed:
	gpio_free(cam_enable);

cam_enable_failed:

	return ret;
}

static struct i2c_board_info s5k4ecgx_i2c = {
	I2C_BOARD_INFO("s5k4ecgx", 0x56),
};

static struct soc_camera_desc s5k4ecgx_desc = {
	.subdev_desc = {
		.power		= s5k4ecgx_sensor_power,
		.drv_priv	= &s5k4ecgx_data,
		.flags		= 0,
	},
	.host_desc = {
		.bus_id = 0,	/* Must match with the camera ID */
		.i2c_adapter_id = 2,	/* twsi 3 */
		.board_info		= &s5k4ecgx_i2c,
		.module_name	= "s5k4ecgx",
	},
};
#endif /* CONFIG_SOC_CAMERA_S5K4ECGX */

#ifdef CONFIG_SOC_CAMERA_SR200PC20M
static struct sensor_board_data sr200pc20m_data = {
	.mount_pos	= SENSOR_USED | SENSOR_POS_FRONT | SENSOR_RES_LOW,
	.bus_type	= V4L2_MBUS_CSI2,
	.bus_flag	= V4L2_MBUS_CSI2_1_LANE,
	.dphy = {0x0d04, 0x0011, 0x0c00}, /* obsoleted, using dt */
	.flags = V4L2_MBUS_CSI2_1_LANE,
	.manual_mclk_ctrl = 1,
};

static int sr200pc20m_sensor_power(struct device *dev, int on)
{
	static struct regulator *avdd_2v8;
	static struct regulator *dovdd_1v8;
	static struct regulator *af_2v8;
//	static struct regulator *dvdd_1v2;

	struct clk *mclk = sr200pc20m_data.mclk;

	int cam_enable, cam_reset;
	int ret = -EIO;

	dev_info(dev, "sr200pc20m: power %s\n", on? "on":"off");

	if (!mclk) {
		dev_err(dev, "sr200pc20m: mclk is not set.\n");
		return -EINVAL;
	}

	/* Get the regulators and never put it */
	/*
	 * The regulators is for sensor and should be in sensor driver
	 * As SoC camera does not support device tree, adding code here
	 */

	if (!avdd_2v8) {
		avdd_2v8 = regulator_get(dev, "avdd_2v8");
		if (IS_ERR(avdd_2v8)) {
			dev_warn(dev, "Failed to get regulator avdd_2v8\n");
			avdd_2v8 = NULL;
		}
	}

	if (!dovdd_1v8) {
		dovdd_1v8 = regulator_get(dev, "dovdd_1v8");
		if (IS_ERR(dovdd_1v8)) {
			dev_warn(dev, "Failed to get regulator dovdd_1v8\n");
			dovdd_1v8 = NULL;
		}
	}

	if (!af_2v8) {
		af_2v8 = regulator_get(dev, "af_2v8");
		if (IS_ERR(af_2v8)) {
			dev_warn(dev, "Failed to get regulator af_2v8\n");
			af_2v8 = NULL;
		}
	}

	/*
	if (!dvdd_1v2) {
		dvdd_1v2 = regulator_get(dev, "dvdd_1v2");
		if (IS_ERR(dvdd_1v2)) {
			dev_warn(dev, "Failed to get regulator dvdd_1v2\n");
			dvdd_1v2 = NULL;
		}
	}
	*/

	cam_enable = of_get_named_gpio(dev->of_node, "pwdn-gpios", 0);
	if (gpio_is_valid(cam_enable)) {
		if (gpio_request(cam_enable, "CAM2_POWER")) {
			dev_err(dev, "Request GPIO %d failed\n", cam_enable);
			goto cam_enable_failed;
		}
	} else {
		dev_err(dev, "invalid pwdn gpio %d\n", cam_enable);
		goto cam_enable_failed;
	}

	cam_reset = of_get_named_gpio(dev->of_node, "reset-gpios", 0);
	if (gpio_is_valid(cam_reset)) {
		if (gpio_request(cam_reset, "CAM2_RESET")) {
			dev_err(dev, "Request GPIO %d failed\n", cam_reset);
			goto cam_reset_failed;
		}
	} else {
		dev_err(dev, "invalid reset gpio %d\n", cam_reset);
		goto cam_reset_failed;
	}

	if (on) {
		if (avdd_2v8) {
			regulator_set_voltage(avdd_2v8, 2800000, 2800000);
			ret = regulator_enable(avdd_2v8);
			if (ret)
				goto cam_regulator_enable_failed;
		}

		if (af_2v8) {
			regulator_set_voltage(af_2v8, 2800000, 2800000);
			ret = regulator_enable(af_2v8);
			if (ret)
				goto cam_regulator_enable_failed;
		}

		/*
		if (dvdd_1v2) {
			regulator_set_voltage(dvdd_1v2, 1200000, 1200000);
			ret = regulator_enable(dvdd_1v2);
			if (ret)
				goto cam_regulator_enable_failed;
		}
		*/

		if (dovdd_1v8) {
			regulator_set_voltage(dovdd_1v8, 1800000, 1800000);
			ret = regulator_enable(dovdd_1v8);
			if (ret)
				goto cam_regulator_enable_failed;
		}

		msleep(2);
		gpio_direction_output(cam_enable, 1);
		msleep(1);
		clk_set_rate(mclk, 26000000);
		clk_prepare_enable(mclk);
		msleep(30);
		gpio_direction_output(cam_reset, 1);
		usleep_range(50000, 100000);
	}
	else {
		gpio_direction_output(cam_reset, 0);
		usleep_range(50000, 100000);
		gpio_direction_output(cam_enable, 0);
		clk_disable_unprepare(mclk);

		if (dovdd_1v8)
			regulator_disable(dovdd_1v8);
//		if (dvdd_1v2)
//			regulator_disable(dvdd_1v2);
		if (avdd_2v8)
			regulator_disable(avdd_2v8);
		if (af_2v8)
			regulator_disable(af_2v8);
	}

	gpio_free(cam_reset);
	gpio_free(cam_enable);

	return 0;

cam_regulator_enable_failed:
	/*
	if (dvdd_1v2)
		regulator_put(dvdd_1v2);
	dvdd_1v2 = NULL;
	*/
	if (af_2v8)
		regulator_put(af_2v8);
	af_2v8 = NULL;
	if (dovdd_1v8)
		regulator_put(dovdd_1v8);
	dovdd_1v8 = NULL;
	if (avdd_2v8)
		regulator_put(avdd_2v8);
	avdd_2v8 = NULL;

	gpio_free(cam_reset);

cam_reset_failed:
	gpio_free(cam_enable);

cam_enable_failed:

	return ret;
}

static struct i2c_board_info sr200pc20m_i2c = {
	I2C_BOARD_INFO("sr200pc20m", 0x20),
};

static struct soc_camera_desc sr200pc20m_desc = {
	.subdev_desc = {
		.power		= sr200pc20m_sensor_power,
		.drv_priv	= &sr200pc20m_data,
		.flags		= 0,
	},
	.host_desc = {
		.bus_id = 1,	/* Must match with the camera ID */
		.i2c_adapter_id = 2,	/* twsi 3 */
		.board_info		= &sr200pc20m_i2c,
		.module_name	= "sr200pc20m",
	},
};
#endif /* CONFIG_SOC_CAMERA_SR200PC20M */

static const struct of_dev_auxdata mmpx_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("mrvl,mmp-uart", 0xd4030000, "pxa2xx-uart.0", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-uart", 0xd4017000, "pxa2xx-uart.1", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-uart", 0xd4018000, "pxa2xx-uart.2", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-uart", 0xd4016000, "pxa2xx-uart.3", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4011000, "pxa2xx-i2c.0", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4031000, "pxa2xx-i2c.1", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4032000, "pxa2xx-i2c.2", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4033000, "pxa2xx-i2c.3", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4033800, "pxa2xx-i2c.4", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4034000, "pxa2xx-i2c.5", NULL),
	OF_DEV_AUXDATA("marvell,pxa25x-pwm", 0xd401a000, "mmp-pwm.0", NULL),
	OF_DEV_AUXDATA("marvell,pxa25x-pwm", 0xd401a400, "mmp-pwm.1", NULL),
	OF_DEV_AUXDATA("marvell,pxa25x-pwm", 0xd401a800, "mmp-pwm.2", NULL),
	OF_DEV_AUXDATA("marvell,pxa25x-pwm", 0xd401ac00, "mmp-pwm.3", NULL),
	OF_DEV_AUXDATA("marvell,pxa1928-gpio", 0xd4019000, "pxa1928-gpio",
			NULL),
	OF_DEV_AUXDATA("marvell,pxa27x-keypad", 0xd4012000, "pxa27x-keypad",
		       NULL),
	OF_DEV_AUXDATA("marvell,pxa910-spi", 0xd4035000, "pxa910-ssp.1",
			NULL),
	OF_DEV_AUXDATA("mrvl,mmp-sspa-dai", 0xc0ffdc00, "mmp-sspa-dai.0", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-sspa-dai", 0xc0ffdd00, "mmp-sspa-dai.1", NULL),
	OF_DEV_AUXDATA("marvell,map-card", 0, "sound", NULL),
	OF_DEV_AUXDATA("marvell,adma-1.0", 0xC0FFD800, "mmp-adma.0", NULL),
	OF_DEV_AUXDATA("marvell,adma-1.0", 0xC0FFD900, "mmp-adma.1", NULL),
#ifdef CONFIG_MMP_GPS
	OF_DEV_AUXDATA("marvell,mmp-gps", 0, "mmp-gps", NULL),
#endif
#ifdef CONFIG_RTC_DRV_SA1100
	OF_DEV_AUXDATA("mrvl,mmp-rtc", 0xd4010000, "sa1100-rtc", NULL),
#endif
#ifdef CONFIG_MMC_SDHCI_PXAV3
	OF_DEV_AUXDATA("mrvl,pxav3-mmc-v3", 0xd4280000, "sdhci-pxav3.0", NULL),
	OF_DEV_AUXDATA("mrvl,pxav3-mmc-v3", 0xd4280800, "sdhci-pxav3.1", NULL),
	OF_DEV_AUXDATA("mrvl,pxav3-mmc-v3", 0xd4217000, "sdhci-pxav3.2", NULL),
#endif
#ifdef CONFIG_SMC91X
	OF_DEV_AUXDATA("smsc,lan91c111", 0x80000300, "smc91x", NULL),
#endif

#ifdef CONFIG_MV_USB2_PHY
	OF_DEV_AUXDATA("marvell,usb2-phy-28lp", 0xd4207000,
			"pxa1928-usb-phy", NULL),
#endif
#ifdef CONFIG_USB_MV_UDC
	OF_DEV_AUXDATA("marvell,mv-udc", 0xd4208100, "mv-udc",
			&pxa1928_usb_pdata),
#endif
#ifdef CONFIG_USB_EHCI_MV_U2O
	OF_DEV_AUXDATA("marvell,pxa-u2oehci", 0xd4208100, "pxa-u2oehci",
			&pxa1928_usb_pdata),
#endif
#ifdef CONFIG_USB_MV_OTG
	OF_DEV_AUXDATA("marvell,mv-otg", 0xd4208100, "mv-otg",
			&pxa1928_usb_pdata),
#endif
#ifdef CONFIG_USB_EHCI_MV_U2H_HSIC
	OF_DEV_AUXDATA("marvell,hsic-phy-28lp", 0xf0a01800, "pxa1928-hsic-phy", NULL),
	OF_DEV_AUXDATA("marvell,pxa-u2hhsic", 0xf0a01100, "pxa-u2hhsic", &pxa1928_hsic_pdata),
#endif
#ifdef CONFIG_VPU_DEVFREQ
	OF_DEV_AUXDATA("marvell,devfreq-vpu", 0xf0400000,
			"devfreq-vpu.0", NULL),
	OF_DEV_AUXDATA("marvell,devfreq-vpu", 0xf0400800,
			"devfreq-vpu.1", NULL),
#endif
#ifdef CONFIG_SD8XXX_RFKILL
	OF_DEV_AUXDATA("mrvl,sd8x-rfkill", 0, "sd8x-rfkill", NULL),
#endif
#ifdef CONFIG_VIDEO_MV_SC2_MMU
	OF_DEV_AUXDATA("marvell,mmp-sc2mmu", 0xd4209000, "mv_sc2_mmu.0", NULL),
#endif
#ifdef CONFIG_SOC_CAMERA_S5K8AA
	OF_DEV_AUXDATA("soc-camera-pdrv", 0, "soc-camera-pdrv.0", &s5k8aa_desc),
#endif
#ifdef CONFIG_SOC_CAMERA_S5K4ECGX
	OF_DEV_AUXDATA("soc-camera-pdrv", 0, "soc-camera-pdrv.0", &s5k4ecgx_desc),
#endif
#ifdef CONFIG_SOC_CAMERA_SP0A20_ECS
	OF_DEV_AUXDATA("soc-camera-pdrv", 1, "soc-camera-pdrv.1", &sp0a20_desc),
#endif
#ifdef CONFIG_SOC_CAMERA_SR200PC20M
	OF_DEV_AUXDATA("soc-camera-pdrv", 1, "soc-camera-pdrv.1", &sr200pc20m_desc),
#endif
#ifdef CONFIG_VIDEO_MV_SC2_CCIC
	OF_DEV_AUXDATA("marvell,mmp-sc2ccic", 0xd420a000,
				"mv_sc2_ccic.0", NULL),
	OF_DEV_AUXDATA("marvell,mmp-sc2ccic", 0xd420a800,
				"mv_sc2_ccic.1", NULL),
#endif
#ifdef CONFIG_VIDEO_MV_SC2_CAMERA
	OF_DEV_AUXDATA("marvell,mv_sc2_camera", 1, "mv_sc2_camera.1", NULL),
#endif
#ifdef CONFIG_DDR_DEVFREQ
	OF_DEV_AUXDATA("marvell,devfreq-ddr", 0xd0000000,
			"devfreq-ddr", NULL),
#endif
	OF_DEV_AUXDATA("pwm-backlight", 0, "lcd-bl", NULL),
	OF_DEV_AUXDATA("pwm-vibrator", 0, "pwm-vibrator", NULL),
	OF_DEV_AUXDATA("marvell,gpu", 0xd420d000, "mmp-gpu", NULL),
	OF_DEV_AUXDATA("marvell,gpu2d", 0, "mmp-gpu2d", NULL),

#ifdef CONFIG_LEDS_REGULATOR
	OF_DEV_AUXDATA("regulator-leds", 0, "leds-regulator",
						&keypad_backlight),
#endif
	{}
};

#define APB_PHYS_BASE		0xd4000000
#define AXI_PHYS_BASE		0xd4200000
#define AUD_PHYS_BASE           0xc3000000
#define AUD_PHYS_BASE2          0xc0140000
#ifdef CONFIG_ARM_ARCH_TIMER
#define MPMU_CPRR              0x0020
#define MPMU_WDTPCR1           0x0204
#define MPMU_APRR              0x1020

#define MPMU_APRR_WDTR         (1 << 4)
#define MPMU_APRR_CPR          (1 << 0)
#define MPMU_CPRR_DSPR         (1 << 2)
#define MPMU_CPRR_BBR          (1 << 3)

#define TMR_WFAR               (0x009c)
#define TMR_WSAR               (0x00A0)

#define GEN_TMR_CFG            (0x00B0)
#define GEN_TMR_LD1            (0x00B8)

/* Get SoC Access to Generic Timer */
#ifdef CONFIG_GLB_SECURE
inline void arch_timer_soc_access_enable(void __iomem *gen_counter_base)
{
	smc_timer2_write(TMR_WFAR, 0xbaba);
	smc_timer2_write(TMR_WSAR, 0xeb10);
}

void arch_timer_soc_config(void)
{
	void __iomem *gen_counter_base = apb_virt_base + 0x80000;
	void __iomem *mpmu_base = apb_virt_base + 0x50000;

	u32 tmp;

	/* Enable WDTR2*/
	tmp  = __raw_readl(mpmu_base + MPMU_CPRR);
	tmp = tmp | MPMU_APRR_WDTR;
	__raw_writel(tmp, mpmu_base + MPMU_CPRR);

	/* Initialize Counter to zero */
	arch_timer_soc_access_enable(gen_counter_base);
	smc_timer2_write(GEN_TMR_LD1, 0x0);

	/* Program Generic Timer Clk Frequency */
	arch_timer_soc_access_enable(gen_counter_base);
	tmp = smc_timer2_read(GEN_TMR_CFG);
	tmp |= (3 << 4); /* 3.25MHz/32KHz Counter auto switch enabled */
	arch_timer_soc_access_enable(gen_counter_base);
	smc_timer2_write(GEN_TMR_CFG, tmp);

	/* Start the Generic Timer Counter */
	arch_timer_soc_access_enable(gen_counter_base);
	tmp = smc_timer2_read(GEN_TMR_CFG);
	tmp |= 0x3;
	arch_timer_soc_access_enable(gen_counter_base);
	smc_timer2_write(GEN_TMR_CFG, tmp);
}
#else
inline void arch_timer_soc_access_enable(void __iomem *gen_counter_base)
{
	__raw_writel(0xbaba, gen_counter_base + TMR_WFAR);
	__raw_writel(0xeb10, gen_counter_base + TMR_WSAR);
}

void arch_timer_soc_config(void)
{
	void __iomem *gen_counter_base = apb_virt_base + 0x80000;
	void __iomem *mpmu_base = apb_virt_base + 0x50000;

	u32 tmp;
	/* Enable WDTR2*/
	tmp  = __raw_readl(mpmu_base + MPMU_CPRR);
	tmp = tmp | MPMU_APRR_WDTR;
	__raw_writel(tmp, mpmu_base + MPMU_CPRR);

	/* Initialize Counter to zero */
	arch_timer_soc_access_enable(gen_counter_base);
	__raw_writel(0x0, gen_counter_base + GEN_TMR_LD1);

	/* Program Generic Timer Clk Frequency */
	arch_timer_soc_access_enable(gen_counter_base);
	tmp = __raw_readl(gen_counter_base + GEN_TMR_CFG);
	tmp |= (3 << 4); /* 3.25MHz/32KHz Counter auto switch enabled */
	arch_timer_soc_access_enable(gen_counter_base);
	__raw_writel(tmp, gen_counter_base + GEN_TMR_CFG);

	/* Start the Generic Timer Counter */
	arch_timer_soc_access_enable(gen_counter_base);
	tmp = __raw_readl(gen_counter_base + GEN_TMR_CFG);
	tmp |= 0x3;
	arch_timer_soc_access_enable(gen_counter_base);
	__raw_writel(tmp, gen_counter_base + GEN_TMR_CFG);
}
#endif
#endif /* #ifdef CONFIG_ARM_ARCH_TIMER */

#define MPMU_WDTPCR	0x0200

static __init void pxa1928_timer_init(void)
{
	void __iomem *mpmu_base = apb_virt_base + 0x50000;

#ifdef CONFIG_ARM_ARCH_TIMER
	arch_timer_soc_config();
#endif
	/*bit 7.enable wdt reset from thermal
	 *bit 6.enable wdt reset from timer2
	 * */
	__raw_writel(0xD3, mpmu_base + MPMU_WDTPCR);

	clocksource_of_init();
}

/* FW log mem reservation, 64KB by default */
static u32 fw_area_size = 0x10000;
static phys_addr_t fw_area_addr = 0x8141000;

static void __init pxa1928_reserve_fwmem(void)
{
	BUG_ON(memblock_reserve(fw_area_addr, fw_area_size) != 0);
	pr_info("Reserved FW Log memory: 0x%x@0x%llx\n", fw_area_size, fw_area_addr);
}

/* CP memeory reservation, 32MB by default */
static u32 cp_area_size = 0x02000000;
static phys_addr_t cp_area_addr = 0x06000000;

static int __init early_cpmem(char *p)
{
	char *endp;

	cp_area_size = memparse(p, &endp);
	if (*endp == '@')
		cp_area_addr = memparse(endp + 1, NULL);

	return 0;
}
early_param("cpmem", early_cpmem);

static void __init pxa1928_reserve_cpmem(void)
{
	BUG_ON(memblock_reserve(cp_area_addr, cp_area_size) != 0);
	pr_info("Reserved CP memory: 0x%x@0x%llx\n", cp_area_size, cp_area_addr);
}

static void __init pxa1928_reserve(void)
{
	pxa1928_reserve_cpmem();
	pxa1928_reserve_fwmem();
#ifdef CONFIG_MRVL_LOG
	pxa_reserve_logmem();
#endif
}

static void __init pxa1928_init_machine(void)
{
	read_fuseinfo();
	/* init clk */
	pxa1928_clk_init(APB_PHYS_BASE, AXI_PHYS_BASE,
		AUD_PHYS_BASE, AUD_PHYS_BASE2);

	of_platform_populate(NULL, of_default_bus_match_table,
			     mmpx_auxdata_lookup, &platform_bus);

	init_pxa1928_sysset();
}

static const char *pxa1928_dt_board_compat[] __initdata = {
	"mrvl,pxa1928",
	NULL,
};

static const char *pxa1908_dt_board_compat[] __initdata = {
	"mrvl,pxa1908",
	NULL,
};

struct machine_desc mmp_machine_descs[] __initdata = {
	{
		.name		= "MARVELL PXA1928",
		.reserve	= pxa1928_reserve,
		.init_time	= pxa1928_timer_init,
		.init_machine	= pxa1928_init_machine,
		.dt_compat	= pxa1928_dt_board_compat,
	},
	{
		.name           = "MARVELL PXA1908",
		.reserve        = pxa1908_reserve,
		.init_time      = pxa1908_arch_timer_init,
		.init_machine   = pxa1908_dt_init_machine,
		.dt_compat      = pxa1908_dt_board_compat,
	},
	{
		/* use dt_compat to check end */
		.dt_compat	= NULL,
	},
};
