/*
 * linux/drivers/video/mvisp_flash
 *
 * simple PWM based flash control, board code has to setup
 * 1) pin configuration so PWM waveforms can output
 * 2) platform_data being correctly configured
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/err.h>
#include <linux/pwm.h>

#include <linux/slab.h>
#include <media/mvisp_flash.h>
#include "isp.h"
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/leds.h>

#define MVISP_FLASH_NAME "mvisp-board-flash"
static inline struct mvisp_board_flash_t *to_flash_core(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mvisp_board_flash_t, subdev);
}

static inline struct v4l2_subdev *to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler,
				struct mvisp_board_flash_t,
				ctrl_handler)->subdev;
}
struct v4l2_subdev *mvisp_board_flash_create(struct device *parent,
					int id, void *pdata)
{
	struct platform_device *mfdev = devm_kzalloc(parent,
						sizeof(struct platform_device),
						GFP_KERNEL);
	int ret = 0;

	if (mfdev == NULL) {
		ret = -ENOMEM;
		goto err;
	}
	mfdev->name = MVISP_FLASH_NAME;
	mfdev->id = id;
	mfdev->dev.platform_data = pdata;
	ret = platform_device_register(mfdev);
	if (ret < 0) {
		pr_err("unable to create host subdev: %d\n", ret);
		goto err;
	}

	return platform_get_drvdata(mfdev);
err:
	return NULL;
}
EXPORT_SYMBOL(mvisp_board_flash_create);

static int control_torch(struct v4l2_subdev *sd,
		int on)
{
	struct mvisp_board_flash_t *core = v4l2_get_subdevdata(sd);
	struct mvisp_flash_data_t *pdata = &core->pdata;
	if (pdata) {
		int torch_on = pdata->torch_enable;
		if (torch_on >= 0) {
			if (gpio_request(torch_on, "TORCH_SET_POWER_ON")) {
				pr_err("Request GPIO failed,gpio:%d\n",
						torch_on);
				return -EIO;
			}

			gpio_direction_output(torch_on, on);

			gpio_free(torch_on);
		} else
			pr_err("GPIO is invalid");
	}
#ifdef CONFIG_VIDEO_MVISP_FLASH_PWM
	if (on) {
		pwm_config(core->pwm,
				core->pdata->pwm_period_ns/2,
				core->pdata->pwm_period_ns);
		pwm_enable(core->pwm);
	} else {
		pwm_config(core->pwm, 0, core->pdata->pwm_period_ns);
		pwm_disable(core->pwm);
	}
#endif
	ledtrig_torch_ctrl(on);
	return 0;
}

static int control_flash(struct v4l2_subdev *sd,
		int on)
{
	struct mvisp_board_flash_t *core = v4l2_get_subdevdata(sd);
	struct mvisp_flash_data_t *pdata = &core->pdata;

	if (pdata) {
		int flash_on = pdata->flash_enable;
		if (flash_on >= 0) {
			if (gpio_request(flash_on, "FLASH_SET_POWER_ON")) {
				pr_err("Request GPIO failed,gpio:%d\n",
						flash_on);
				return -EIO;
			}

			gpio_direction_output(flash_on, on);

			gpio_free(flash_on);
		} else
			pr_err("GPIO is invalid");
	}
#ifdef CONFIG_VIDEO_MVISP_FLASH_PWM
	if (on) {
		pwm_config(core->pwm,
				core->pdata->pwm_period_ns,
				core->pdata->pwm_period_ns);
		pwm_enable(core->pwm);
	} else {
		pwm_config(core->pwm, 0, core->pdata->pwm_period_ns);
		pwm_disable(core->pwm);
	}
#endif
	ledtrig_flash_ctrl(on);
	return 0;
}

static int flash_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = to_sd(ctrl);
	int ret = 0;
	static int cur_mode = V4L2_FLASH_LED_MODE_NONE;
	static int cur_active_mode = V4L2_FLASH_LED_MODE_NONE;

	switch (ctrl->id) {
	case V4L2_CID_FLASH_LED_MODE:
			cur_mode = ctrl->val;
			break;
	case V4L2_CID_FLASH_STROBE_STOP:
		switch (cur_active_mode) {
		case V4L2_FLASH_LED_MODE_FLASH:
			ret = control_flash(sd, 0);
			break;
		case V4L2_FLASH_LED_MODE_TORCH:
			ret = control_torch(sd, 0);
			break;
		default:
			ret = 0;
			pr_err("Warning: You haven't call flash open!\n");
			break;
		}
		cur_active_mode = V4L2_FLASH_LED_MODE_NONE;
		break;
	case V4L2_CID_FLASH_STROBE:
		if (cur_active_mode != V4L2_FLASH_LED_MODE_NONE) {
			ret = -EIO;
			goto error;
		}
		switch (cur_mode) {
		case V4L2_FLASH_LED_MODE_FLASH:
			ret = control_flash(sd, 1);
			break;
		case V4L2_FLASH_LED_MODE_TORCH:
			ret = control_torch(sd, 1);
			break;
		default:
			ret = 0;
			pr_err("Warning: Flash ID mismatch!\n");
			break;
		}
		cur_active_mode = cur_mode;
		break;
	default:
		break;
	}

error:

	return ret;
}
int flash_s_register(struct v4l2_subdev *sd,
			const struct v4l2_dbg_register *reg)
{
	int ret = 0;
	static int cur_mode = V4L2_FLASH_LED_MODE_NONE;
	static int cur_active_mode = V4L2_FLASH_LED_MODE_NONE;
	switch (reg->reg) {
	case V4L2_CID_FLASH_LED_MODE:
			cur_mode = reg->val;
			break;
	case V4L2_CID_FLASH_STROBE_STOP:
		switch (cur_active_mode) {
		case V4L2_FLASH_LED_MODE_FLASH:
			ret = control_flash(sd, 0);
			break;
		case V4L2_FLASH_LED_MODE_TORCH:
			ret = control_torch(sd, 0);
			break;
		default:
			ret = 0;
			break;
		}
		cur_active_mode = V4L2_FLASH_LED_MODE_NONE;
		break;
	case V4L2_CID_FLASH_STROBE:
		if (cur_active_mode != V4L2_FLASH_LED_MODE_NONE) {
			ret = -EIO;
			goto error;
		}
		switch (cur_mode) {
		case V4L2_FLASH_LED_MODE_FLASH:
			ret = control_flash(sd, 1);
			break;
		case V4L2_FLASH_LED_MODE_TORCH:
			ret = control_torch(sd, 1);
			break;
		default:
			ret = 0;
			pr_err("Warning: Flash ID mismatch!\n");
			break;
		}
		cur_active_mode = cur_mode;
		break;
	default:
		break;
	}
error:
	return ret;
}

int flash_g_register(struct v4l2_subdev *sd,
				struct v4l2_dbg_register *reg)
{
	return 0;
}
static const struct v4l2_subdev_core_ops flash_core_ops = {
	.s_ctrl = v4l2_subdev_s_ctrl,
	.querymenu = v4l2_subdev_querymenu,
	.g_ext_ctrls = v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls = v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls = v4l2_subdev_s_ext_ctrls,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = &flash_g_register,
	.s_register = &flash_s_register,
#endif
};
static const struct v4l2_ctrl_ops flash_ctrl_ops = {
	.s_ctrl = flash_s_ctrl,
};
static const struct v4l2_subdev_ops flash_ops = {
	.core  = &flash_core_ops,
};
static int mvisp_board_flash_probe(struct platform_device *pdev)
{
	struct v4l2_subdev *sd;
	struct mvisp_board_flash_t *core;
	struct device_node *host_node;
	struct device_node *flash_node;
	int ret;
	core = devm_kzalloc(&pdev->dev, sizeof(*core), GFP_KERNEL);
	if (!core) {
		dev_err(&pdev->dev, "no memory for state\n");
		ret = -ENOMEM;
		goto no_mem_err;
	}
	host_node = pdev->dev.platform_data;
	flash_node = of_parse_phandle(host_node, "flash_led", 0);
	if (flash_node == NULL)
		return -EINVAL;
	core->pdata.flash_enable =  of_get_named_gpio(flash_node,
						"flash-gpios", 0);
	core->pdata.torch_enable = of_get_named_gpio(flash_node,
						"torcht-gpios", 0);
	sd = &core->subdev;
#ifdef CONFIG_VIDEO_MVISP_FLASH_PWM
	core->pwm = pwm_request(core->pdata->pwm_id, "mvisp flash");
	if (IS_ERR(core->pwm)) {
		dev_err(&pdev->dev, "unable to request PWM for flash\n");
		ret = PTR_ERR(core->pwm);
		goto err;
	} else
		pr_info("enabed pwm %d  for flash\n",
					core->pdata->pwm_id);
	pwm_config(core->pwm, 0, core->pdata->pwm_period_ns);
	pwm_disable(core->pwm);
#endif
	platform_set_drvdata(pdev, core);
	core->dev = &pdev->dev;

	v4l2_ctrl_handler_init(&core->ctrl_handler, 3);
	core->subdev.ctrl_handler = &core->ctrl_handler;
	v4l2_ctrl_new_std_menu(&core->ctrl_handler, &flash_ctrl_ops,
			V4L2_CID_FLASH_LED_MODE, 2, ~7,
			V4L2_FLASH_LED_MODE_NONE);
	v4l2_ctrl_new_std(&core->ctrl_handler, &flash_ctrl_ops,
			V4L2_CID_FLASH_STROBE, 0, 1, 1, 0);
	v4l2_ctrl_new_std(&core->ctrl_handler, &flash_ctrl_ops,
			V4L2_CID_FLASH_STROBE_STOP, 0, 1, 1, 0);
	sd->ctrl_handler = &core->ctrl_handler;

	ret = core->ctrl_handler.error;
	if (ret)
		goto flash_error;
	/* v4l2_subdev_init(sd, &host_subdev_ops); */
	ret = media_entity_init(&sd->entity, 1, &core->pad, 0);
	if (ret < 0)
		goto flash_media_init_error;
	v4l2_subdev_init(sd, &flash_ops);
	v4l2_set_subdevdata(sd, core);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	strcpy(sd->name, pdev->name);
	sd->entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	dev_dbg(core->dev, "host subdev created\n");
	return 0;
flash_media_init_error:
	media_entity_cleanup(&sd->entity);
flash_error:
	v4l2_ctrl_handler_free(&core->ctrl_handler);
#ifdef CONFIG_VIDEO_MVISP_FLASH_PWM
	pwm_config(core->pwm, 0, core->pdata->pwm_period_ns);
	pwm_disable(core->pwm);
	pwm_free(core->pwm);
err:
#endif
	devm_kfree(&pdev->dev, core);
no_mem_err:
	return ret;
}

static int mvisp_board_flash_remove(struct platform_device *pdev)
{

	struct mvisp_board_flash_t *core = platform_get_drvdata(pdev);
	struct v4l2_subdev *sd;
	sd = &core->subdev;
#ifdef CONFIG_VIDEO_MVISP_FLASH_PWM
	pwm_config(core->pwm, 0, core->pdata->pwm_period_ns);
	pwm_disable(core->pwm);
	pwm_free(core->pwm);
#endif
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&core->ctrl_handler);
	v4l2_device_unregister_subdev(sd);
	devm_kfree(&pdev->dev, core);
	return 0;
}


static struct platform_driver mvisp_board_flash_driver = {
	.driver		= {
		.name	= MVISP_FLASH_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= mvisp_board_flash_probe,
	.remove		= mvisp_board_flash_remove,
};

module_platform_driver(mvisp_board_flash_driver);

MODULE_DESCRIPTION("Board flash led use gpio or pwm Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pwm-flash");

