/*
 *
 * Copyright (C) 2012 Marvell Internation Ltd.
 *
 * Chunlin Hu <huchl@marvell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/videodev2.h>
#include <linux/i2c.h>
#include <linux/uaccess.h>
#include <media/dw9714l.h>
#include <media/v4l2-ctrls.h>
#include <linux/module.h>
#include <media/v4l2-device.h>
#include <linux/regulator/machine.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
struct dw9714l {
	struct v4l2_subdev subdev;
	/* Controls */
	struct v4l2_ctrl_handler ctrls;
	struct regulator *af_vcc;
	int pwdn_gpio;
	int pwdn_val;
	char openflag;
};

#define to_dw9714l(sd) container_of(sd, struct dw9714l, subdev)
static int dw9714l_write(struct i2c_client *client, u16 val)
{
	int ret = 0;
	u8 data[2];
	data[0] = val>>8;
	data[1] = val;
	ret = i2c_master_send(client, data, 2);
	if (ret < 0)
		dev_err(&client->dev,
			"dw9714l:failed to write val-0x%02x\n", val);
	return (ret > 0) ? 0 : ret;
}

static int dw9714l_read(struct i2c_client *client, s32 *val)
{
	int ret;
	u8 data[2];
	ret = i2c_master_recv(client, data, 2);
	if (ret < 0) {
		dev_err(&client->dev, "dw9714l: failed to read\n");
		return -1;
	}
	*val = data[0]<<8 | data[1];
	return 0;
}

static int dw9714l_set_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	struct dw9714l *vcm =
		container_of(ctrl->handler, struct dw9714l, ctrls);
	struct i2c_client *client = v4l2_get_subdevdata(&vcm->subdev);
	switch (ctrl->id) {
	case V4L2_CID_FOCUS_ABSOLUTE:
		ret = dw9714l_write(client, ctrl->val);
		return ret;

	default:
		return -EPERM;
	}

}
static int dw9714l_get_ctrl(struct v4l2_ctrl *ctrl)
{
	int ret = 0;
	struct dw9714l *vcm =
		container_of(ctrl->handler, struct dw9714l, ctrls);
	struct i2c_client *client = v4l2_get_subdevdata(&vcm->subdev);
	switch (ctrl->id) {
	case V4L2_CID_FOCUS_ABSOLUTE:
		ret = dw9714l_read(client, &(ctrl->val));
		return ret;

	default:
		return -EPERM;
	}

}
static void vcm_power(struct dw9714l *vcm, int on)
{
	/* Enable voltage for camera sensor vcm */
	if (on) {
		regulator_set_voltage(vcm->af_vcc, 2800000, 2800000);
		if (regulator_enable(vcm->af_vcc) < 0)
			pr_err("enable aff_vcc fail\n");
	} else {
		regulator_disable(vcm->af_vcc);
	}
}
static int dw9714l_subdev_close(struct v4l2_subdev *sd,
		struct v4l2_subdev_fh *fh)
{
	struct dw9714l *core = to_dw9714l(sd);
	struct i2c_client *client = v4l2_get_subdevdata(&core->subdev);
	dw9714l_write(client, 0x8000);
	vcm_power(core, 0);
	core->openflag = 0;
	return 0;
}

static int dw9714l_subdev_open(struct v4l2_subdev *sd,
				struct v4l2_subdev_fh *fh)
{
	struct dw9714l *core = to_dw9714l(sd);
	struct i2c_client *client = v4l2_get_subdevdata(&core->subdev);
	if (core->openflag == 1)
		return -EBUSY;
	core->openflag = 1;
	vcm_power(core, 1);
	dw9714l_write(client, 0x0000);
	usleep_range(12000, 12050);
	v4l2_ctrl_handler_setup(&core->ctrls);
	return 0;
}
static const struct v4l2_ctrl_ops dw9714l_ctrl_ops = {
	.g_volatile_ctrl = dw9714l_get_ctrl,
	.s_ctrl = dw9714l_set_ctrl,
};

static int dw9714l_init_controls(struct dw9714l *vcm)
{
	struct v4l2_ctrl *ctrl;
	v4l2_ctrl_handler_init(&vcm->ctrls, 1);
	/* V4L2_CID_FOCUS_ABSOLUTE */
	ctrl = v4l2_ctrl_new_std(&vcm->ctrls, &dw9714l_ctrl_ops,
			 V4L2_CID_FOCUS_ABSOLUTE, 0, (1<<16)-1, 1, 0x8000);
	if (ctrl != NULL)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
	vcm->subdev.ctrl_handler = &vcm->ctrls;

	return vcm->ctrls.error;
}
/* subdev internal operations */
static const struct v4l2_subdev_internal_ops dw9714l_v4l2_internal_ops = {
	.open = dw9714l_subdev_open,
	.close = dw9714l_subdev_close,
};

static const struct v4l2_subdev_core_ops dw9714l_core_ops = {
	.g_ext_ctrls = v4l2_subdev_g_ext_ctrls,
	.try_ext_ctrls = v4l2_subdev_try_ext_ctrls,
	.s_ext_ctrls = v4l2_subdev_s_ext_ctrls,
	.g_ctrl = v4l2_subdev_g_ctrl,
	.s_ctrl = v4l2_subdev_s_ctrl,
	.queryctrl = v4l2_subdev_queryctrl,
	.querymenu = v4l2_subdev_querymenu,
};

static const struct v4l2_subdev_ops dw9714l_ops = {
	.core = &dw9714l_core_ops,
};

static int  dw9714l_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	int ret = -1;
	s32 vcmid;
	int pwdn = 0;
	struct dw9714l *vcm;
	struct device_node *power_np;
	struct device_node *sensor_np = client->dev.platform_data;

	vcm = kzalloc(sizeof(struct dw9714l), GFP_KERNEL);
	if (!vcm) {
		dev_err(&client->dev, "dw9714l: failed to allocate dw9714l struct\n");
		return -ENOMEM;
	}
	if (!sensor_np)
		return -EIO;
	power_np = of_get_child_by_name(sensor_np, "sensor_power");
	vcm->pwdn_gpio = of_get_named_gpio(power_np, "pwdn-gpios", 0);
	if (vcm->pwdn_gpio < 0) {
		pr_err("%x: of_get_named_gpio failed\n", __LINE__);
		goto out_gpio;
	}
	ret = of_property_read_u32(power_np, "pwdn-validvalue", &vcm->pwdn_val);
	if (ret < 0) {
		pr_err("%x: of_get_gpio_value failed\n", __LINE__);
		goto out_gpio;
	}
	if (gpio_request(vcm->pwdn_gpio, "vcm power gpio")) {
		pr_err("gpio %d request failed\n", vcm->pwdn_gpio);
		goto out_gpio;
	}
	vcm->af_vcc = devm_regulator_get(&client->dev, "af_2v8");
	if (IS_ERR(vcm->af_vcc)) {
		dev_warn(&client->dev, "Failed to get regulator af_2v8\n");
		vcm->af_vcc = NULL;
		goto out_af_vcc;
	}
	vcm_power(vcm, 1);

	pwdn = gpio_get_value(vcm->pwdn_gpio);
	gpio_direction_output(vcm->pwdn_gpio, vcm->pwdn_val);
	usleep_range(12000, 12050);
	if (dw9714l_read(client, &vcmid)) {
		pr_err("VCM: DW9714L vcm detect failure!\n");
		goto out_pwdn;
	}
	if (vcm->pwdn_gpio) {
		gpio_direction_output(vcm->pwdn_gpio, pwdn);
		gpio_free(vcm->pwdn_gpio);
	}
	if (vcm->af_vcc)
		vcm_power(vcm, 0);
	v4l2_i2c_subdev_init(&vcm->subdev, client, &dw9714l_ops);
	vcm->subdev.internal_ops = &dw9714l_v4l2_internal_ops;
	vcm->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ret = dw9714l_init_controls(vcm);
	if (ret < 0)
		goto out;
	ret = media_entity_init(&vcm->subdev.entity, 0, NULL, 0);
	if (ret < 0)
		goto out;
	dw9714l_write(client, 0x8000);
	pr_info("dw9714l detected!\n");
	return 0;

out:
	v4l2_ctrl_handler_free(&vcm->ctrls);
out_pwdn:
	if (vcm->af_vcc) {
		vcm_power(vcm, 0);
		devm_regulator_put(vcm->af_vcc);
		vcm->af_vcc = NULL;
	}
out_af_vcc:
	if (vcm->pwdn_gpio) {
		gpio_direction_output(vcm->pwdn_gpio, pwdn);
		gpio_free(vcm->pwdn_gpio);
	}
out_gpio:
	kfree(vcm);
	vcm = NULL;
	return ret;
}
static int dw9714l_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct dw9714l *vcm = to_dw9714l(subdev);
	if (vcm) {
		v4l2_device_unregister_subdev(subdev);
		v4l2_ctrl_handler_free(&vcm->ctrls);
		media_entity_cleanup(&vcm->subdev.entity);
		kfree(vcm);
	}
	return 0;
}

static const struct i2c_device_id dw9714l_id[] = {
	{"dw9714l", 0},
};

MODULE_DEVICE_TABLE(i2c, dw9714l_id);

static struct i2c_driver dw9714l_driver = {
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "vcm<dw9714l>",
		   },
	.probe = dw9714l_probe,
	.remove = dw9714l_remove,
	.id_table = dw9714l_id,
};

static int __init dw9714l_mod_init(void)
{
	return i2c_add_driver(&dw9714l_driver);
}

module_init(dw9714l_mod_init);

static void __exit dw9714l_mode_exit(void)
{
	i2c_del_driver(&dw9714l_driver);
}

module_exit(dw9714l_mode_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Chunlin Hu <huchl@marvell.com>");
MODULE_DESCRIPTION("dw9714l vcm driver");

