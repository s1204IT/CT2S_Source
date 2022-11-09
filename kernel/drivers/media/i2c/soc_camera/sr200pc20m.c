/*
 * Siliconfile SR200PC20M Camera Driver
 *
 * Copyright (C) 2014-2015 SANYO Techno Solutions Tottori Co., Ltd.
 *
 * - Based on s5k8aa Camera Driver.
 *     Copyright (c) 2013 Marvell Ltd.
 *     Libin Yang <lbyang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
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
#include "sr200pc20m.h"

static const struct sr200pc20m_datafmt sr200pc20m_colour_fmts[] = {
	{V4L2_MBUS_FMT_UYVY8_2X8, V4L2_COLORSPACE_JPEG},
	{V4L2_MBUS_FMT_VYUY8_2X8, V4L2_COLORSPACE_JPEG},
	{V4L2_MBUS_FMT_YUYV8_2X8, V4L2_COLORSPACE_JPEG},
	{V4L2_MBUS_FMT_YVYU8_2X8, V4L2_COLORSPACE_JPEG},
};

static const struct sr200pc20m_resolution_table sr200pc20m_resolutions[] = {
	{  176,  144, SR200PC20M_FMT_QCIF },
	{  320,  240, SR200PC20M_FMT_QVGA },
	{  640,  480, SR200PC20M_FMT_VGA },
	{ 1024,  768, SR200PC20M_FMT_XGA },
	{ 1600, 1200, SR200PC20M_FMT_UXGA },
	{  720,  480, SR200PC20M_FMT_480P },
	{ 1280,  720, SR200PC20M_FMT_720P },
};


static struct sr200pc20m *to_sr200pc20m(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client),
				struct sr200pc20m, subdev);
}

static int sr200pc20m_i2c_read(struct i2c_client *client, u8 addr, u8 *data)
{
	int ret;

	ret = i2c_master_send(client, &addr, 1);
	if (ret != 1) {
		dev_err(&client->dev,
			"i2c send error. (addr=%02x)\n", addr);
		if (ret >= 0) {
			ret = -EIO;
		}
		return ret;
	}

	ret = i2c_master_recv(client, data, 1);
	if (ret != 1) {
		dev_err(&client->dev,
			"i2c recv error. (addr=%02x)\n", addr);
		if (ret >= 0) {
			ret = -EIO;
		}
		return ret;
	}

	return 0;
}

static int sr200pc20m_i2c_write(struct i2c_client *client, u8 addr, u8 data)
{
	int ret;
	u8 buf[2] = { addr, data };

	ret = i2c_master_send(client, buf, sizeof(buf));
	if (ret != sizeof(buf)) {
		dev_err(&client->dev,
			"i2c send error. (addr=%02x, data=%02x)\n", addr, data);
		if (ret >= 0) {
			ret = -EIO;
		}
		return ret;
	}

	return 0;
}

static int sr200pc20m_i2c_burst_write(struct i2c_client *client,
				      const struct reg_value *seq, size_t seq_len)
{
	int ret;
	u32 i;

	if (seq == NULL) {
		dev_err(&client->dev, "sequence is not specified.\n");
		return -EINVAL;
	}

	for (i=0; i<seq_len; i++) {
		if (seq[i].mask != 0) {
			/* mask is not supported */
			dev_err(&client->dev, "seq[%u].mask=0x%02x\n", i, seq[i].mask);
			return -EINVAL;
		}
		ret = sr200pc20m_i2c_write(client, seq[i].addr, seq[i].data);
		if (ret != 0) {
			dev_err(&client->dev, "seq[%u] send error.\n", i);
			return ret;
		}
		if (seq[i].delay != 0) {
			msleep(seq[i].delay);
		}
	}

	return 0;
}

static int sr200pc20m_read_reg(struct i2c_client *client,
			       u8 page, u8 addr, u8 *data)
{
	int ret = 0;

	ret += sr200pc20m_i2c_write(client, 0x03, page);
	ret += sr200pc20m_i2c_read(client, addr, data);
	if (ret != 0) {
		dev_err(&client->dev,
			"could not read register. (page=%02x, addr=%02x)\n",
			page, addr);
		return ret;
	}

	return 0;
}

static int sr200pc20m_write_reg(struct i2c_client *client,
				u8 page, u8 addr, u8 data)
{
	int ret = 0;

	ret += sr200pc20m_i2c_write(client, 0x03, page);
	ret += sr200pc20m_i2c_write(client, addr, data);
	if (ret != 0) {
		dev_err(&client->dev,
			"could not write register. (page=%02x, addr=%02x, data=%02x)\n",
			page, addr, data);
		return ret;
	}

	return 0;
}

static int sr200pc20m_g_chip_ident(struct v4l2_subdev *sd,
				   struct v4l2_dbg_chip_ident *id)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sr200pc20m *sr200pc20m = to_sr200pc20m(client);

	id->ident = sr200pc20m->model;
	id->revision = 0;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int sr200pc20m_g_register(struct v4l2_subdev *sd,
				 struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	u8 val = 0;
	int ret = 0;

	#if 0
		dev_err(&client->dev, "DBG_G_REGISTER is not supported.\n");
		return -ENOTSUPP;
	#else
//		ret = sr200pc20m_i2c_read(client, (u8)reg->reg,&val);
//		if(ret==0) reg->val = val;
//		dev_err(&client->dev,
//			"%s:( addr=%02x, data=%02x)\n",__FUNCTION__, (u8)reg->reg, (u8*)reg->val);
		return ret;				 
	#endif
}

static int sr200pc20m_s_register(struct v4l2_subdev *sd,
				 const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	# if 0
		dev_err(&client->dev, "DBG_S_REGISTER is not supported.\n");
		return -ENOTSUPP;
	#else
		return sr200pc20m_i2c_write(client, (u8)reg->reg, (u8)reg->val);		
	#endif
}
#endif

static int sr200pc20m_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	dev_info(&client->dev, "stream %s\n", enable? "on":"off");
	/* TODO */
	return 0;
}

static int sr200pc20m_enum_fmt(struct v4l2_subdev *sd,
		unsigned int index,
		enum v4l2_mbus_pixelcode *code)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	if (index >= ARRAY_SIZE(sr200pc20m_colour_fmts))
		return -EINVAL;

	*code = sr200pc20m_colour_fmts[index].code;

	return 0;
}

static enum sr200pc20m_res_support
sr200pc20m_get_res_code(struct v4l2_mbus_framefmt *mf)
{
	int i;

	for (i=0; i < ARRAY_SIZE(sr200pc20m_resolutions); i++) {
		if (sr200pc20m_resolutions[i].width  == mf->width &&
			sr200pc20m_resolutions[i].height == mf->height) {
			return sr200pc20m_resolutions[i].res_code;
		}
	}
	return 0;
}

static int sr200pc20m_try_fmt(struct v4l2_subdev *sd,
			   struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	switch (mf->code)
	{
	case V4L2_MBUS_FMT_UYVY8_2X8:
	case V4L2_MBUS_FMT_VYUY8_2X8:
	case V4L2_MBUS_FMT_YVYU8_2X8:
	case V4L2_MBUS_FMT_YUYV8_2X8:
		if (SR200PC20M_FMT_INVALID == sr200pc20m_get_res_code(mf)) {
			dev_warn(&client->dev,
				 "%d x %d is not supported. set to default. (%d x %d)\n",
				 mf->width, mf->height,
				 SR200PC20M_OUT_WIDTH_DEF, SR200PC20M_OUT_HEIGHT_DEF);
			mf->width  = SR200PC20M_OUT_WIDTH_DEF;
			mf->height = SR200PC20M_OUT_HEIGHT_DEF;
		}
		/* colorspace should be set in host driver */
		mf->colorspace = V4L2_COLORSPACE_JPEG;
		break;

	default:
		/* This should never happen */
		mf->code = V4L2_MBUS_FMT_UYVY8_2X8;
		return -EINVAL;
	}

	return 0;
}

static int sr200pc20m_s_fmt(struct v4l2_subdev *sd,
			 struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sr200pc20m *sr200pc20m = to_sr200pc20m(client);
	enum sr200pc20m_res_support res_code;
	const struct reg_value *seq = NULL;
	size_t seq_len = 0;
	int ret;

	res_code = sr200pc20m_get_res_code(mf);
	if (res_code == SR200PC20M_FMT_INVALID) {
		dev_err(&client->dev, "%d x %d is not supported.\n",
				 mf->width, mf->height);
		return -EINVAL;
	}
	sr200pc20m->rect.width  = mf->width;
	sr200pc20m->rect.height = mf->height;

//--- @@@ プレビュー設定時に毎回の初期化するのを止める
#if 0
	ret = sr200pc20m_i2c_burst_write(client, sr200pc20m_seq_init,
					 ARRAY_SIZE(sr200pc20m_seq_init));
	if (ret != 0) {
		dev_err(&client->dev, "could not send init sequence. (%d)\n", ret);
		return -EINVAL;
	}
#endif
//--- @@@ プレビュー設定時に毎回の初期化するのを止める

	if(sr200pc20m->ctrls.cap_mode->val == V4L2_SR200PC20M_CAP_MODE_STILL) {
		switch (res_code)
		{
		case SR200PC20M_FMT_QCIF:
			seq = sr200pc20m_seq_capture_qcif;
			seq_len = ARRAY_SIZE(sr200pc20m_seq_capture_qcif);
			break;

		case SR200PC20M_FMT_QVGA:
			seq = sr200pc20m_seq_capture_qvga;
			seq_len = ARRAY_SIZE(sr200pc20m_seq_capture_qvga);
			break;

		case SR200PC20M_FMT_VGA:
			seq = sr200pc20m_seq_capture_vga;
			seq_len = ARRAY_SIZE(sr200pc20m_seq_capture_vga);
			break;

		case SR200PC20M_FMT_XGA:
			seq = sr200pc20m_seq_capture_xga;
			seq_len = ARRAY_SIZE(sr200pc20m_seq_capture_xga);
			break;

		case SR200PC20M_FMT_UXGA:
			seq = sr200pc20m_seq_capture_uxga;
			seq_len = ARRAY_SIZE(sr200pc20m_seq_capture_uxga);
			break;

		case SR200PC20M_FMT_480P:
			seq = sr200pc20m_seq_capture_480p;
			seq_len = ARRAY_SIZE(sr200pc20m_seq_capture_480p);
			break;

		case SR200PC20M_FMT_720P:
			seq = sr200pc20m_seq_capture_720p;
			seq_len = ARRAY_SIZE(sr200pc20m_seq_capture_720p);
			break;

		default:
			dev_err(&client->dev, "invalid resolution code %d\n", res_code);
			return -EINVAL;
		}
	} else {
		switch (res_code)
		{
		case SR200PC20M_FMT_QCIF:
			seq = sr200pc20m_seq_preview_qcif;
			seq_len = ARRAY_SIZE(sr200pc20m_seq_preview_qcif);
			break;

		case SR200PC20M_FMT_QVGA:
			seq = sr200pc20m_seq_preview_qvga;
			seq_len = ARRAY_SIZE(sr200pc20m_seq_preview_qvga);
			break;

		case SR200PC20M_FMT_VGA:
			seq = sr200pc20m_seq_preview_vga;
			seq_len = ARRAY_SIZE(sr200pc20m_seq_preview_vga);
			break;

		case SR200PC20M_FMT_XGA:
			seq = sr200pc20m_seq_preview_xga;
			seq_len = ARRAY_SIZE(sr200pc20m_seq_preview_xga);
			break;

		case SR200PC20M_FMT_UXGA:
			seq = sr200pc20m_seq_preview_uxga;
			seq_len = ARRAY_SIZE(sr200pc20m_seq_preview_uxga);
			break;

		case SR200PC20M_FMT_480P:
			seq = sr200pc20m_seq_preview_480p;
			seq_len = ARRAY_SIZE(sr200pc20m_seq_preview_480p);
			break;

		case SR200PC20M_FMT_720P:
			seq = sr200pc20m_seq_preview_720p;
			seq_len = ARRAY_SIZE(sr200pc20m_seq_preview_720p);
			break;

		default:
			dev_err(&client->dev, "invalid resolution code %d\n", res_code);
			return -EINVAL;
		}
	}

	ret = sr200pc20m_i2c_burst_write(client, seq, seq_len);
	if (ret != 0) {
		dev_err(&client->dev, "could not set preview config. (%d)\n", ret);
		return -EINVAL;
	}

	dev_info(
		&client->dev, "set to %d x %d (%s)\n",
		mf->width, mf->height,
		sr200pc20m->ctrls.cap_mode->val? "capture":"preview");


	return 0;
}

static int sr200pc20m_g_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct sr200pc20m *sr200pc20m = to_sr200pc20m(client);

	mf->width		= sr200pc20m->rect.width;
	mf->height		= sr200pc20m->rect.height;
	/* all belows are fixed */
	mf->code		= V4L2_MBUS_FMT_UYVY8_2X8;
	mf->field		= V4L2_FIELD_NONE;
	mf->colorspace		= V4L2_COLORSPACE_JPEG;

	return 0;
}

static int sr200pc20m_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *crop)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	dev_err(&client->dev, "G_CROP is not supported.\n");
	return -ENOTSUPP;
}

static int sr200pc20m_s_crop(struct v4l2_subdev *sd, const struct v4l2_crop *crop)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	dev_err(&client->dev, "S_CROP is not supported.\n");
	return -ENOTSUPP;
}

static int sr200pc20m_enum_fsizes(struct v4l2_subdev *sd,
				struct v4l2_frmsizeenum *fsize)
{
	if (!fsize)
		return -EINVAL;
	if (fsize->index >= ARRAY_SIZE(sr200pc20m_resolutions))
		return -EINVAL;

	fsize->type            = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.height = sr200pc20m_resolutions[fsize->index].height;
	fsize->discrete.width  = sr200pc20m_resolutions[fsize->index].width;

	return 0;
}

static int sr200pc20m_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct sr200pc20m_ctrls *ctrls = container_of(
		ctrl->handler, struct sr200pc20m_ctrls, ctrl_handler);
	struct sr200pc20m *sr200pc20m = container_of(
		ctrls, struct sr200pc20m, ctrls);
	struct i2c_client *client = sr200pc20m->client;
	int ret = 0;


	switch (ctrl->id)
	{
	case V4L2_CID_SR200PC20M_CAP_MODE:
		/* nop */
		break;

	default:
		dev_warn(&client->dev, "%s: unsupported id 0x%08x\n",
			 __FUNCTION__, ctrl->id);
		ret = -ENOTSUPP;
		break;
	}
	return ret;
}

static int sr200pc20m_init(struct v4l2_subdev *sd, u32 plat)
{
//--- @@@
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = sr200pc20m_i2c_burst_write(client, sr200pc20m_seq_init,
					 ARRAY_SIZE(sr200pc20m_seq_init));
	if (ret != 0) {
		dev_err(&client->dev, "could not send init sequence. (%d)\n", ret);
		return -EINVAL;
	}

	dev_info(&client->dev, "init completed\n");

//--- @@@
	return 0;
}

static int sr200pc20m_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	cfg->type  = V4L2_MBUS_CSI2;
	cfg->flags = V4L2_MBUS_CSI2_1_LANE;
	return 0;
}

static struct v4l2_subdev_core_ops sr200pc20m_subdev_core_ops = {
	.g_chip_ident = sr200pc20m_g_chip_ident,
	.init		  = sr200pc20m_init,
	.s_ctrl		  = sr200pc20m_s_ctrl,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	  = sr200pc20m_g_register,
	.s_register	  = sr200pc20m_s_register,
#endif
};

static struct v4l2_subdev_video_ops sr200pc20m_subdev_video_ops = {
	.s_stream        = sr200pc20m_s_stream,
	.g_mbus_fmt      = sr200pc20m_g_fmt,
	.s_mbus_fmt      = sr200pc20m_s_fmt,
	.try_mbus_fmt    = sr200pc20m_try_fmt,
	.enum_framesizes = sr200pc20m_enum_fsizes,
	.enum_mbus_fmt   = sr200pc20m_enum_fmt,
	.g_crop          = sr200pc20m_g_crop,
	.s_crop          = sr200pc20m_s_crop,
	.g_mbus_config   = sr200pc20m_g_mbus_config,
};

static struct v4l2_subdev_ops sr200pc20m_subdev_ops = {
	.core  = &sr200pc20m_subdev_core_ops,
	.video = &sr200pc20m_subdev_video_ops,
};

static const struct v4l2_ctrl_ops sr200pc20m_ctrl_ops = {
	.s_ctrl = sr200pc20m_s_ctrl,
};

static const struct v4l2_ctrl_config sr200pc20m_ctrl_cap_mode_cfg = {
	.ops  = &sr200pc20m_ctrl_ops,
	.id   = V4L2_CID_SR200PC20M_CAP_MODE,
	.name = "Capture mode",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min  = V4L2_SR200PC20M_CAP_MODE_PREVIEW,
	.max  = V4L2_SR200PC20M_CAP_MODE_STILL,
	.step = 1,
	.def  = V4L2_SR200PC20M_CAP_MODE_PREVIEW,
};

static int sr200pc20m_detect(struct i2c_client *client)
{
	int ret;
	u8 data;

	ret = sr200pc20m_read_reg(client, 0x00, 0x04, &data);
	if (ret != 0) {
		dev_err(&client->dev, "could not read device ID.\n");
		return ret;
	}
	if (data != 0xB4) {
		dev_err(&client->dev, "invalid device ID 0x%02x\n", data);
		return -EINVAL;
	}
	dev_info(&client->dev, "Device ID: 0x%02x\n", data);

	return 0;
}

static void sr200pc20m_init_ctrls(struct sr200pc20m_ctrls *ctrls)
{
	struct v4l2_ctrl_handler *handler = &ctrls->ctrl_handler;
	size_t nr_of_controls_hint =
		(sizeof(struct sr200pc20m_ctrls) - sizeof(struct v4l2_ctrl_handler))
		/ sizeof(struct v4l2_ctrl *);

	v4l2_ctrl_handler_init(handler, nr_of_controls_hint);

	/* capture mode */
	ctrls->cap_mode = v4l2_ctrl_new_custom(
		handler, &sr200pc20m_ctrl_cap_mode_cfg, NULL
		);
}


static int sr200pc20m_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct sr200pc20m *sr200pc20m = NULL;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	int ret = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(&adapter->dev,
			"I2C-Adapter doesn't support I2C_FUNC_SMBUS_WORD\n");
		return -EIO;
	}

	ret = sr200pc20m_detect(client);
	if (ret != 0)
		return ret;

	sr200pc20m = kzalloc(sizeof(struct sr200pc20m), GFP_KERNEL);
	if (sr200pc20m == NULL)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&sr200pc20m->subdev, client, &sr200pc20m_subdev_ops);
	sr200pc20m_init_ctrls(&sr200pc20m->ctrls);
	sr200pc20m->subdev.ctrl_handler = &sr200pc20m->ctrls.ctrl_handler;

	sr200pc20m->rect.left   = 0;
	sr200pc20m->rect.top    = 0;
	sr200pc20m->rect.width  = SR200PC20M_OUT_WIDTH_DEF;
	sr200pc20m->rect.height = SR200PC20M_OUT_HEIGHT_DEF;
	sr200pc20m->pixfmt      = V4L2_PIX_FMT_UYVY;

	return ret;
}

static int sr200pc20m_remove(struct i2c_client *client)
{
	struct sr200pc20m *sr200pc20m = to_sr200pc20m(client);
	kfree(sr200pc20m);
	return 0;
}

static const struct i2c_device_id sr200pc20m_id_table[] = {
	{ SR200PC20M_I2C_DEV_NAME, 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, sr200pc20m_id_table);

static struct i2c_driver sr200pc20m_i2c_driver = {
	.driver = {
		.owner 	= THIS_MODULE,
		.name 	= SR200PC20M_DRIVER_NAME,
	},
	.probe 		= sr200pc20m_probe,
	.remove 	= sr200pc20m_remove,
	.id_table 	= sr200pc20m_id_table,
};

static int __init sr200pc20m_mod_init(void)
{
	int ret;

	ret = i2c_add_driver(&sr200pc20m_i2c_driver);
	if (ret != 0)
		pr_err("%s:driver registration failed, error=%d\n",
			__func__, ret);

	return ret;
}

static void __exit sr200pc20m_mod_exit(void)
{
	i2c_del_driver(&sr200pc20m_i2c_driver);
}

module_init(sr200pc20m_mod_init);
module_exit(sr200pc20m_mod_exit);

MODULE_AUTHOR("SANYO Techno Solutions Tottori Co., Ltd.");
MODULE_DESCRIPTION("CAMMSYS SR200PC20M Camera Driver");
MODULE_LICENSE("GPL");
