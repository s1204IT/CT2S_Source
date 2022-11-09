/*
 * drivers/video/mmp/panel/mipi_bp070wx1.c
 * active panel using DSI interface to do init
 *
 * Copyright (C) 2014 Marvell Technology Group Ltd.
 * Authors: Jianzhong Li<jamkong@marvell.com>
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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <video/mmp_disp.h>
#include <video/mipi_display.h>

static int x_mirror;
struct bp070wx1_plat_data {
	struct mmp_panel *panel;
	void (*plat_onoff)(int status);
	void (*plat_set_backlight)(struct mmp_panel *panel, int level);
};

static char exit_sleep[] = {0x11};
static char display_on[] = {0x29};
static u8 lcd_init0[] = {0xF0, 0x5A, 0x5A};
static u8 lcd_init1[] = {0xF1, 0x5A, 0x5A};
static u8 lcd_init2[] = {0xFC, 0xA5, 0xA5};
static u8 lcd_init3[] = {0xD0, 0x00, 0x10};
static u8 lcd_init4[] = {0xB1, 0x10};
static u8 lcd_init5[] = {0xB2, 0x14, 0x22, 0x2F, 0x04};
static u8 lcd_init6[] = {0xF2, 0x02, 0x08, 0x08, 0x40, 0x10};
static u8 lcd_init7[] = {0xB0, 0x04};
static u8 lcd_init8[] = {0xFD, 0x09};
static u8 lcd_init9[] = {0xF3, 0x01, 0xD7, 0xE2, 0x62, 0xF4, 0xF7, 0x77, 0x3C,
						0x26, 0x00};
static u8 lcd_init10[] = {
0xF4, 0x00, 0x02, 0x03, 0x26, 0x03, 0x02,
0x09, 0x00, 0x07, 0x16, 0x16, 0x03, 0x00,
0x08, 0x08, 0x03, 0x0E, 0x0F, 0x12, 0x1C,
0x1D, 0x1E, 0x0C, 0x09, 0x01, 0x04, 0x02,
0x61, 0x74, 0x75, 0x72, 0x83, 0x80, 0x80,
0xB0, 0x00, 0x01, 0x01, 0x28, 0x04, 0x03,
0x28, 0x01, 0xD1, 0x32
};
static u8 lcd_init11[] = {
0xF5, 0x91, 0x28, 0x28, 0x5F, 0xAB, 0x98, 0x52, 0x0F,
0x33, 0x43, 0x04, 0x59, 0x54, 0x52, 0x05, 0x40, 0x60,
0x4E, 0x60, 0x40, 0x27, 0x26, 0x52, 0x25, 0x6D, 0x18
};
static u8 lcd_init12[] = {0xEE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static u8 lcd_init13[] = {0xEF, 0x12, 0x12, 0x43, 0x43, 0x90, 0x84, 0x24, 0x81};
static u8 lcd_init14[] = {
0xF7, 0x0A, 0x0A, 0x08, 0x08, 0x0B, 0x0B, 0x09, 0x09,
0x04, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x0A,
0x0A, 0x08, 0x08, 0x0B, 0x0B, 0x09, 0x09, 0x04, 0x05,
0x01, 0x01, 0x01, 0x01, 0x01, 0x01
};
static u8 lcd_init15[] = {0xBC, 0x01, 0x4E, 0x08};
static u8 lcd_init16[] = {0xE1, 0x03, 0x10, 0x1C, 0xA0, 0x10};
static u8 lcd_init17[] = {0xF6, 0x60, 0x25, 0xA6, 0x00, 0x00, 0x00};
static u8 lcd_init18[] = {
0xFA, 0x00, 0x34, 0x01, 0x05, 0x0E, 0x07, 0x0C, 0x12,
0x14, 0x1C, 0x23, 0x2B, 0x34, 0x35, 0x2E, 0x2D, 0x30
};
static u8 lcd_init19[] = {
0xFB, 0x00, 0x34, 0x01, 0x05, 0x0E, 0x07, 0x0C, 0x12,
0x14, 0x1C, 0x23, 0x2B, 0x34, 0x35, 0x2E, 0x2D, 0x30
};
static u8 lcd_init20[] = {0xFE, 0x00, 0x0D, 0x03, 0x21, 0x00, 0x08};
static u8 lcd_init21[] = {0xC3, 0x40, 0x00, 0x28};
static u8 lcd_rotation[] = {0x36, 0x0c};

static struct mmp_dsi_cmd_desc bp070wx1_display_on_cmds[] = {
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init0), lcd_init0},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init1), lcd_init1},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init2), lcd_init2},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init3), lcd_init3},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init4), lcd_init4},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init5), lcd_init5},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init6), lcd_init6},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init7), lcd_init7},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init8), lcd_init8},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init9), lcd_init9},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init10), lcd_init10},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init11), lcd_init11},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init12), lcd_init12},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init13), lcd_init13},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init14), lcd_init14},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init15), lcd_init15},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init16), lcd_init16},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init17), lcd_init17},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init18), lcd_init18},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init19), lcd_init19},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init20), lcd_init20},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init21), lcd_init21},
	{MIPI_DSI_DCS_SHORT_WRITE, 0, 10, sizeof(exit_sleep), exit_sleep},
	{MIPI_DSI_DCS_SHORT_WRITE, 0, 1, sizeof(display_on), display_on},
};

static struct mmp_dsi_cmd_desc bp070wx1_display_on_cmds_rotate[] = {
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init0), lcd_init0},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init1), lcd_init1},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init2), lcd_init2},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init3), lcd_init3},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init4), lcd_init4},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init5), lcd_init5},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init6), lcd_init6},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init7), lcd_init7},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init8), lcd_init8},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init9), lcd_init9},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init10), lcd_init10},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init11), lcd_init11},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init12), lcd_init12},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init13), lcd_init13},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init14), lcd_init14},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init15), lcd_init15},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init16), lcd_init16},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init17), lcd_init17},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init18), lcd_init18},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init19), lcd_init19},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init20), lcd_init20},
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_init21), lcd_init21},
	/*lcd rotate*/
	{MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, sizeof(lcd_rotation), lcd_rotation},
	{MIPI_DSI_DCS_SHORT_WRITE, 0, 10, sizeof(exit_sleep), exit_sleep},
	{MIPI_DSI_DCS_SHORT_WRITE, 0, 1, sizeof(display_on), display_on},
};

static void bp070wx1_panel_on(struct mmp_panel *panel)
{
	usleep_range(80000, 90000);
	usleep_range(20000, 30000);
	if (x_mirror)
		mmp_panel_dsi_tx_cmd_array(panel, bp070wx1_display_on_cmds_rotate,
			ARRAY_SIZE(bp070wx1_display_on_cmds_rotate));
	else
		mmp_panel_dsi_tx_cmd_array(panel, bp070wx1_display_on_cmds,
			ARRAY_SIZE(bp070wx1_display_on_cmds));
}

#ifdef CONFIG_OF
static void bp070wx1_panel_power(struct mmp_panel *panel, int skip_on, int on)
{
	static struct regulator *lcd_iovdd;
	static struct regulator *vcc_io_wb;
	int lcd_rst_n, boost_en_5v, ret = 0;

	lcd_rst_n = of_get_named_gpio(panel->dev->of_node, "rst_gpio", 0);
	boost_en_5v = of_get_named_gpio(panel->dev->of_node, "power_gpio", 0);

	if (lcd_rst_n < 0 || boost_en_5v < 0) {
		pr_err("%s: of_get_named_gpio failed\n", __func__);
		return;
	}

	if (gpio_request(lcd_rst_n, "lcd reset gpio")) {
		pr_err("gpio %d request failed\n", lcd_rst_n);
		return;
	}

	if (gpio_request(boost_en_5v, "5v boost en")) {
		pr_err("gpio %d request failed\n", boost_en_5v);
		gpio_free(lcd_rst_n);
		return;
	}

	/* FIXME: LCD_IOVDD, 1.8V from buck2 */
	if (panel->is_iovdd && (!lcd_iovdd)) {
		lcd_iovdd = regulator_get(panel->dev, "iovdd");
		if (IS_ERR(lcd_iovdd)) {
			pr_err("%s regulator get error!\n", __func__);
			ret = -EIO;
			goto regu_lcd_iovdd;
		}
	}

	if (panel->is_avdd && (!vcc_io_wb)) {
		vcc_io_wb = regulator_get(panel->dev, "avdd");
		if (IS_ERR(vcc_io_wb)) {
			pr_err("%s regulator get error!\n", __func__);
			ret = -EIO;
			goto regu_lcd_iovdd;
		}
	}

	if (on) {
		if (panel->is_avdd && vcc_io_wb) {
			regulator_set_voltage(vcc_io_wb, 2800000, 2800000);
			ret = regulator_enable(vcc_io_wb);
			if (ret < 0)
				goto regu_lcd_iovdd;
		}

		if (panel->is_iovdd && lcd_iovdd) {
			regulator_set_voltage(lcd_iovdd, 1800000, 1800000);
			ret = regulator_enable(lcd_iovdd);
			if (ret < 0)
				goto regu_lcd_iovdd;
		}
		usleep_range(10000, 12000);

		/* LCD_AVDD+ and LCD_AVDD- ON */
		gpio_direction_output(boost_en_5v, 1);
		usleep_range(10000, 12000);
		if (!skip_on) {
			gpio_direction_output(lcd_rst_n, 0);
			usleep_range(10000, 12000);
			gpio_direction_output(lcd_rst_n, 1);
			usleep_range(10000, 12000);
		}
	} else {
		/* set panel reset */
		gpio_direction_output(lcd_rst_n, 0);

		/* disable AVDD+/- */
		gpio_direction_output(boost_en_5v, 0);

		/* disable LCD_IOVDD 1.8v */
		if (panel->is_iovdd && lcd_iovdd)
			regulator_disable(lcd_iovdd);
		if (panel->is_avdd && vcc_io_wb)
			regulator_disable(vcc_io_wb);
	}

regu_lcd_iovdd:
	gpio_free(lcd_rst_n);
	gpio_free(boost_en_5v);
	if (ret < 0) {
		lcd_iovdd = NULL;
		vcc_io_wb = NULL;
	}
}

#else
static void bp070wx1_panel_power(struct mmp_panel *panel,
				int skip_on, int on) {}
#endif

static void bp070wx1_set_status(struct mmp_panel *panel, int status)
{
	struct bp070wx1_plat_data *plat = panel->plat_data;
	int skip_on = (status == MMP_ON_REDUCED);

	if (status_is_on(status)) {
#ifdef CONFIG_DDR_DEVFREQ
		if (panel->ddrfreq_qos != PM_QOS_DEFAULT_VALUE)
			pm_qos_update_request(&panel->ddrfreq_qos_req_min,
				panel->ddrfreq_qos);
#endif
		/* power on */
		if (plat->plat_onoff)
			plat->plat_onoff(1);
		else
			bp070wx1_panel_power(panel, skip_on, 1);
		if (!skip_on)
			bp070wx1_panel_on(panel);
	} else if (status_is_off(status)) {
		/* power off */
		if (plat->plat_onoff)
			plat->plat_onoff(0);
		else
			bp070wx1_panel_power(panel, 0, 0);
#ifdef CONFIG_DDR_DEVFREQ
		if (panel->ddrfreq_qos != PM_QOS_DEFAULT_VALUE)
			pm_qos_update_request(&panel->ddrfreq_qos_req_min,
				PM_QOS_DEFAULT_VALUE);
#endif
	} else
		dev_warn(panel->dev, "set status %s not supported\n",
				status_name(status));
}

static struct mmp_mode mmp_modes_bp070wx1[] = {
	[0] = {
		.pixclock_freq = 103965120,
		.refresh = 60,
		.real_xres = 800,
		.real_yres = 1280,
		.hsync_len = 4,
		.left_margin = 48,
		.right_margin = 485,
		.vsync_len = 4,
		.upper_margin = 8,
		.lower_margin = 4,
		.invert_pixclock = 0,
		.pix_fmt_out = PIXFMT_BGR888PACK,
		.hsync_invert = 0,
		.vsync_invert = 0,
		.height = 110,
		.width = 62,
	},
};

static int bp070wx1_get_modelist(struct mmp_panel *panel,
		struct mmp_mode **modelist)
{
	*modelist = mmp_modes_bp070wx1;
	return 1;
}

static struct mmp_panel panel_bp070wx1 = {
	.name = "bp070wx1",
	.panel_type = PANELTYPE_DSI_VIDEO,
	.is_iovdd = 0,
	.is_avdd = 0,
	.get_modelist = bp070wx1_get_modelist,
	.set_status = bp070wx1_set_status,
};

static int bp070wx1_probe(struct platform_device *pdev)
{
	struct mmp_mach_panel_info *mi;
	struct bp070wx1_plat_data *plat_data;
	struct device_node *np = pdev->dev.of_node;
	const char *path_name;
	int ret;

	plat_data = kzalloc(sizeof(*plat_data), GFP_KERNEL);
	if (!plat_data)
		return -ENOMEM;

	if (IS_ENABLED(CONFIG_OF)) {
		ret = of_property_read_string(np, "marvell,path-name",
				&path_name);
		if (ret < 0)
			return ret;
		panel_bp070wx1.plat_path_name = path_name;

		if (of_find_property(np, "iovdd-supply", NULL))
			panel_bp070wx1.is_iovdd = 1;

		if (of_find_property(np, "avdd-supply", NULL))
			panel_bp070wx1.is_avdd = 1;

#ifdef CONFIG_DDR_DEVFREQ
		ret = of_property_read_u32(np, "marvell,ddrfreq-qos",
				&panel_bp070wx1.ddrfreq_qos);
		if (ret < 0) {
			panel_bp070wx1.ddrfreq_qos = PM_QOS_DEFAULT_VALUE;
			pr_debug("panel %s didn't has ddrfreq min request\n",
				panel_bp070wx1.name);
		} else {
			panel_bp070wx1.ddrfreq_qos_req_min.name = "lcd";
			pm_qos_add_request(&panel_bp070wx1.ddrfreq_qos_req_min,
					PM_QOS_DDR_DEVFREQ_MIN,
					PM_QOS_DEFAULT_VALUE);
			pr_debug("panel %s has ddrfreq min request: %u\n",
				 panel_bp070wx1.name,
				 panel_bp070wx1.ddrfreq_qos);
		}
#endif
		ret = of_property_read_u32(np, "x-mirror", &x_mirror);
		if (ret)
			return ret;
	} else {
		/* get configs from platform data */
		mi = pdev->dev.platform_data;
		if (mi == NULL) {
			dev_err(&pdev->dev, "no platform data defined\n");
			return -EINVAL;
		}
		plat_data->plat_onoff = mi->plat_set_onoff;
		panel_bp070wx1.plat_path_name = mi->plat_path_name;
		plat_data->plat_set_backlight = mi->plat_set_backlight;
	}

	panel_bp070wx1.plat_data = plat_data;
	panel_bp070wx1.dev = &pdev->dev;
	plat_data->panel = &panel_bp070wx1;
	mmp_register_panel(&panel_bp070wx1);
	return 0;
}

static int bp070wx1_remove(struct platform_device *dev)
{
#ifdef CONFIG_DDR_DEVFREQ
	if (panel_bp070wx1.ddrfreq_qos != PM_QOS_DEFAULT_VALUE)
		pm_qos_remove_request(&panel_bp070wx1.ddrfreq_qos_req_min);
#endif
	mmp_unregister_panel(&panel_bp070wx1);
	kfree(panel_bp070wx1.plat_data);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id mmp_bp070wx1_dt_match[] = {
	{ .compatible = "marvell,mmp-bp070wx1" },
	{},
};
#endif

static struct platform_driver bp070wx1_driver = {
	.driver		= {
		.name	= "mmp-bp070wx1",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(mmp_bp070wx1_dt_match),
	},
	.probe		= bp070wx1_probe,
	.remove		= bp070wx1_remove,
};

static int bp070wx1_init(void)
{
	return platform_driver_register(&bp070wx1_driver);
}
static void bp070wx1_exit(void)
{
	platform_driver_unregister(&bp070wx1_driver);
}
module_init(bp070wx1_init);
module_exit(bp070wx1_exit);

MODULE_AUTHOR("Jianzhong Li <jamkong@marvell.com>");
MODULE_DESCRIPTION("Panel driver for MIPI panel bp070wx1");
MODULE_LICENSE("GPL");
