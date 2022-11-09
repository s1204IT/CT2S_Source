/*
 * sp1628 Camera Driver
 *
 * Copyright (c) 2013 Marvell Ltd.
 * yangxing <yangxing@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/platform_data/camera-mmp.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-chip-ident.h>
#include "sp1628.h"

MODULE_DESCRIPTION("Superpixel SP1628 Camera Driver");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0644);

static const struct sp1628_datafmt sp1628_colour_fmts[] = {
	{V4L2_MBUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG},
	{V4L2_MBUS_FMT_VYUY8_2X8, V4L2_COLORSPACE_JPEG},
	{V4L2_MBUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_JPEG},
	{V4L2_MBUS_FMT_YVYU8_2X8, V4L2_COLORSPACE_JPEG},
};

static const struct v4l2_queryctrl sp1628_controls[] = {
	{
	 .id = V4L2_CID_BRIGHTNESS,
	 .type = V4L2_CTRL_TYPE_INTEGER,
	 .name = "brightness",
	 }
};

static struct sp1628_regval *regs_resolution;
static struct sp1628_regval *regs_display_setting;
static struct sp1628_regval *regs_res_all[] = {
	[SP1628_FMT_QCIF] = regs_res_qcif,
	[SP1628_FMT_240X160] = regs_res_240x160,
	[SP1628_FMT_QVGA] = regs_res_qvga,
	[SP1628_FMT_CIF] = regs_res_cif,
	[SP1628_FMT_VGA] = regs_res_vga,
	[SP1628_FMT_720X480] = regs_res_720x480,
	[SP1628_FMT_WVGA] = regs_res_wvga,
	[SP1628_FMT_720P] = regs_res_720P,
	[SP1628_FMT_1280X960] = regs_res_1280x960,
};

static struct sp1628 *to_sp1628(const struct i2c_client
				*client)
{
	return container_of(i2c_get_clientdata(client), struct sp1628, subdev);
}

static int sp1628_i2c_read(struct i2c_client *client, u16 addr, u16 *val)
{
	u8 wbuf = { addr & 0xFF };
	struct i2c_msg msg[2];
	u8 rbuf;
	int ret;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = &wbuf;

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 1;
	msg[1].buf = &rbuf;

	ret = i2c_transfer(client->adapter, msg, 2);
	*val = (u16) rbuf;
	return ret == 2 ? 0 : ret;
}

static int sp1628_i2c_write(struct i2c_client *client, u16 addr, u16 val)
{
	u8 buf[2] = { addr & 0xFF, val & 0xFF };
	int ret;
	ret = i2c_master_send(client, buf, 2);
	if (ret < 0)
		dev_err(&client->dev, "ret = %d\n", ret);
	return (ret < 0) ? ret : 0;
}

/* The general register write */
static int sp1628_write(struct i2c_client *client, u16 addr, u16 val)
{
	return sp1628_i2c_write(client, addr, val);
}

/* The general register read */
static int sp1628_read(struct i2c_client *client, u16 addr, u16 *val)
{
	return sp1628_i2c_read(client, addr, val);
}

static int sp1628_write_raw_array(struct v4l2_subdev *sd,
				  const struct sp1628_regval *msg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	while (msg->addr != SP1628_TERM) {
		ret = sp1628_write(client, msg->addr, msg->val);
		if (ret)
			break;
		/* Assume that msg->addr is always less than 0xfc */
		msg++;
	}

	return ret;
}

static int sp1628_g_chip_ident(struct v4l2_subdev *sd,
			       struct v4l2_dbg_chip_ident *id)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sp1628 *sp1628 = to_sp1628(client);

	id->ident = sp1628->model;
	id->revision = 0;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int sp1628_g_register(struct v4l2_subdev *sd,
			     struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	return sp1628_read(client, (u16) reg->reg, (u16 *) &(reg->val));
}

static int sp1628_s_register(struct v4l2_subdev *sd,
				 const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	return sp1628_write(client, (u16) reg->reg, (u16) reg->val);
}
#endif

static int sp1628_s_stream(struct v4l2_subdev *sd, int enable)
{
	int ret = 0;
	return ret;
}

static int sp1628_enum_fmt(struct v4l2_subdev *sd,
			   unsigned int index, enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(sp1628_colour_fmts))
		return -EINVAL;

	*code = sp1628_colour_fmts[index].code;
	return 0;
}

static inline enum sp1628_res_support find_res_code(int width, int height)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(sp1628_resolutions); i++) {
		if (sp1628_resolutions[i].width == width &&
		    sp1628_resolutions[i].height == height)
			return sp1628_resolutions[i].res_code;
	}
	return -EINVAL;
}

static struct sp1628_regval *sp1628_get_res_regs(int width, int height)
{
	enum sp1628_res_support res_code;

	res_code = find_res_code(width, height);
	if (res_code == -EINVAL)
		return NULL;	/* default setting */
	return regs_res_all[res_code];
}

static int sp1628_try_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sp1628 *sp1628 = to_sp1628(client);

	switch (mf->code) {
	case V4L2_MBUS_FMT_UYVY8_2X8:
	case V4L2_MBUS_FMT_VYUY8_2X8:
	case V4L2_MBUS_FMT_YVYU8_2X8:
	case V4L2_MBUS_FMT_YUYV8_2X8:
		regs_resolution = sp1628_get_res_regs(mf->width, mf->height);
		if (!regs_resolution) {
			/* set default resolution */
			dev_warn(&client->dev,
				 "sp1628: res not support, set to VGA\n");
			mf->width = SP1628_OUT_WIDTH_DEF;
			mf->height = SP1628_OUT_HEIGHT_DEF;
			regs_resolution =
			    sp1628_get_res_regs(mf->width, mf->height);
		}
		sp1628->rect.width = mf->width;
		sp1628->rect.height = mf->height;
		/* colorspace should be set in host driver */
		mf->colorspace = V4L2_COLORSPACE_JPEG;
		if (mf->code == V4L2_MBUS_FMT_UYVY8_2X8)
			regs_display_setting = regs_display_uyvy_setting;
		else if (mf->code == V4L2_MBUS_FMT_VYUY8_2X8)
			regs_display_setting = regs_display_vyuy_setting;
		else if (mf->code == V4L2_MBUS_FMT_YUYV8_2X8)
			regs_display_setting = regs_display_yuyv_setting;
		else
			regs_display_setting = regs_display_yvyu_setting;
		break;
	default:
		/* This should never happen */
		mf->code = V4L2_MBUS_FMT_UYVY8_2X8;
		return -EINVAL;
	}
	return 0;
}

/*
 * each time before start streaming,
 * this function must be called
 */
static int sp1628_s_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	int ret = 0;
	ret = sp1628_write_raw_array(sd, regs_stop_stream);
	msleep(99);
	msleep(50);
	sp1628_write_raw_array(sd, regs_init);
	msleep(20);
	ret |= sp1628_write_raw_array(sd, regs_display_setting);
	ret |= sp1628_write_raw_array(sd, regs_resolution);
	msleep(20);
	return ret;
}

static int sp1628_g_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sp1628 *sp1628 = to_sp1628(client);

	mf->width = sp1628->rect.width;
	mf->height = sp1628->rect.height;
	/* all belows are fixed */
	mf->code = V4L2_MBUS_FMT_UYVY8_2X8;
	mf->field = V4L2_FIELD_NONE;
	mf->colorspace = V4L2_COLORSPACE_JPEG;
	return 0;
}

static int sp1628_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *crop)
{
	return 0;
}

static int sp1628_s_crop(struct v4l2_subdev *sd, const struct v4l2_crop *crop)
{
	return 0;
}

static int sp1628_enum_fsizes(struct v4l2_subdev *sd,
			      struct v4l2_frmsizeenum *fsize)
{
	if (!fsize)
		return -EINVAL;
	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	if (fsize->index >= ARRAY_SIZE(sp1628_resolutions))
		return -EINVAL;
	fsize->discrete.height = sp1628_resolutions[fsize->index].height;
	fsize->discrete.width = sp1628_resolutions[fsize->index].width;
	return 0;
}

static int sp1628_set_brightness(struct i2c_client *client, int value)
{
	return 0;
}

static int sp1628_s_ctrl(struct v4l2_subdev *sd, struct v4l2_control *ctrl)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ret = sp1628_set_brightness(client, ctrl->value);
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static int sp1628_init(struct v4l2_subdev *sd, u32 plat)
{
	return 0;
}

static int sp1628_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	cfg->type = V4L2_MBUS_CSI2;
	return 0;
}

static int sp1628_g_frame_interval(struct v4l2_subdev *sd,
				struct v4l2_subdev_frame_interval *interval)
{
	return -1;
}

static struct v4l2_subdev_core_ops sp1628_subdev_core_ops = {
	.g_chip_ident = sp1628_g_chip_ident,
	.init = sp1628_init,
	.s_ctrl = sp1628_s_ctrl,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = sp1628_g_register,
	.s_register = sp1628_s_register,
#endif
};

static struct v4l2_subdev_video_ops sp1628_subdev_video_ops = {
	.s_stream = sp1628_s_stream,
	.g_mbus_fmt = sp1628_g_fmt,
	.s_mbus_fmt = sp1628_s_fmt,
	.try_mbus_fmt = sp1628_try_fmt,
	.enum_framesizes = sp1628_enum_fsizes,
	.enum_mbus_fmt = sp1628_enum_fmt,
	.g_crop = sp1628_g_crop,
	.s_crop = sp1628_s_crop,
	.g_mbus_config = sp1628_g_mbus_config,
	.g_frame_interval = sp1628_g_frame_interval,
};

static struct v4l2_subdev_ops sp1628_subdev_ops = {
	.core = &sp1628_subdev_core_ops,
	.video = &sp1628_subdev_video_ops,
};

static int sp1628_detect(struct i2c_client *client)
{
	int ret;
	u16 val;

	/* add delay in order to read i2c correctly */
	usleep_range(5000, 10000);
	ret = sp1628_read(client, 0x02, &val);
	if (val != 0x16) {
		dev_err(&client->dev,
	"cannot get right value for 0x02! val = 0x%x\n", val);
		return -ENODEV;
	}
	ret = sp1628_read(client, 0xa0, &val);
	if (val != 0x28) {
		dev_err(&client->dev,
		       "cannot get right value for 0xa0! val = 0x%x\n", val);
		return -ENODEV;
	}
	dev_info(&client->dev, "sp1628 detected!\n");

	return 0;
}

static int sp1628_probe(struct i2c_client *client,
			const struct i2c_device_id *did)
{
	struct sp1628 *sp1628;
	struct soc_camera_link *icl = client->dev.platform_data;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	int ret = 0;

	if (!icl) {
		dev_err(&client->dev, "sp1628: missing soc-camera link!\n");
		return -EINVAL;
	}

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_warn(&adapter->dev,
			 "I2C-Adapter doesn't support I2C_FUNC_SMBUS_WORD\n");
		return -EIO;
	}

	ret = sp1628_detect(client);
	if (ret)
		return ret;

	sp1628 = kzalloc(sizeof(struct sp1628), GFP_KERNEL);
	if (!sp1628)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&sp1628->subdev, client, &sp1628_subdev_ops);
	sp1628->rect.left = 0;
	sp1628->rect.top = 0;
	sp1628->rect.width = SP1628_OUT_WIDTH_DEF;
	sp1628->rect.height = SP1628_OUT_HEIGHT_DEF;
	sp1628->pixfmt = V4L2_PIX_FMT_UYVY;
	return ret;
}

static int sp1628_remove(struct i2c_client *client)
{
	struct sp1628 *sp1628 = to_sp1628(client);

	kfree(sp1628);
	return 0;
}

static const struct i2c_device_id sp1628_idtable[] = {
	{"sp1628", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, sp1628_idtable);

static struct i2c_driver sp1628_driver = {
	.driver = {
		   .name = "sp1628",
		   },
	.probe = sp1628_probe,
	.remove = sp1628_remove,
	.id_table = sp1628_idtable,
};

static int __init sp1628_mod_init(void)
{
	int ret = 0;
	ret = i2c_add_driver(&sp1628_driver);
	return ret;
}

static void __exit sp1628_mod_exit(void)
{
	i2c_del_driver(&sp1628_driver);
}

module_init(sp1628_mod_init);
module_exit(sp1628_mod_exit);
