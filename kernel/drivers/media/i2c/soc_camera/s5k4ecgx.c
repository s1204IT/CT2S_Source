/*
 * SAMSUNG S5K4ECGX Camera Driver
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

//#define DEBUG_ENABLE_SYSFS

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/platform_data/camera-mmp.h>
#include <linux/v4l2-mediabus.h>
#include <media/v4l2-chip-ident.h>
#include <media/mrvl-camera.h>
#include "s5k4ecgx.h"

static const enum v4l2_mbus_pixelcode s5k4ecgx_fmts[] = {
	V4L2_MBUS_FMT_UYVY8_2X8,
	V4L2_MBUS_FMT_VYUY8_2X8,
	V4L2_MBUS_FMT_YUYV8_2X8,
	V4L2_MBUS_FMT_YVYU8_2X8,
//	V4L2_MBUS_FMT_JPEG_1X8,
};

static const struct s5k4ecgx_resolution_table s5k4ecgx_resolutions[] = {
	{  176,  144, S5K4ECGX_FMT_QCIF },
	{  320,  240, S5K4ECGX_FMT_QVGA },
	{  640,  480, S5K4ECGX_FMT_VGA  },
	{  800,  600, S5K4ECGX_FMT_SVGA },
	{ 1024,  768, S5K4ECGX_FMT_XGA  },
	{ 1280,  960, S5K4ECGX_FMT_QUAD_VGA },
	{ 2560, 1920, S5K4ECGX_FMT_2560x1920 },
	{  720,  480, S5K4ECGX_FMT_480P },
	{ 1280,  720, S5K4ECGX_FMT_720P },
};


static struct s5k4ecgx *to_s5k4ecgx(const struct i2c_client *client)
{
	return container_of(i2c_get_clientdata(client),
				struct s5k4ecgx, subdev);
}

static int s5k4ecgx_i2c_read(struct i2c_client *client, u16 addr, u16 *data)
{
	int ret;
	u8 buf[2];

	buf[0] = (u8)(addr >> 8);
	buf[1] = (u8)addr;

	ret = i2c_master_send(client, buf, sizeof(buf));
	if (ret != sizeof(buf)) {
		pr_err("%s: i2c_master_send() failed. (%d)\n", __FUNCTION__, ret);
		if (ret >= 0)
			ret = -EIO;
		return ret;
	}
	ret = i2c_master_recv(client, buf, sizeof(buf));
	if (ret != sizeof(buf)) {
		pr_err("%s: i2c_master_recv() failed. (%d)\n", __FUNCTION__, ret);
		if (ret >= 0)
			ret = -EIO;
		return ret;
	}

	*data = (buf[0] << 8) | buf[1];

	return 0;
}

static int s5k4ecgx_i2c_write(struct i2c_client *client, u16 addr, u16 data)
{
	int ret;
	u8 buf[4];

	buf[0] = (u8)(addr >> 8);
	buf[1] = (u8)addr;
	buf[2] = (u8)(data >> 8);
	buf[3] = (u8)data;

	ret = i2c_master_send(client, buf, sizeof(buf));
	if (ret != sizeof(buf)) {
		pr_err("%s: i2c_master_send() failed. (%d)\n", __FUNCTION__, ret);
		if (ret > 0)
			ret = -EIO;
		return ret;
	}

	return 0;
}

static int s5k4ecgx_i2c_burst_write(struct i2c_client *client, const reg_value_t *seq, size_t seq_len)
{
	int ret;
	u32 i;

	if (seq == NULL) {
		pr_err("%s: seq == NULL\n", __FUNCTION__);
		return -EINVAL;
	}

	for (i=0; i<seq_len; i++) {
		ret = s5k4ecgx_i2c_write(client, seq[i].addr, seq[i].data);
		if (ret != 0) {
			pr_err( "%s: s5k4ecgx_i2c_write(0x%04x,0x%04x) failed. (%d)\n",
					__FUNCTION__, seq[i].addr, seq[i].data, ret);
			return ret;
		}
		if (seq[i].delay != 0)
			msleep(seq[i].delay);
	}

	return 0;
}

static int s5k4ecgx_read_reg(struct i2c_client *client, s5k4ecgx_register_t reg, u16 *data)
{
	int ret;

	reg_value_t seq[] = {
		{ 0xFCFC, 0xD000 },
		{ 0x002C, 0x7000 },
		{ 0x002E, (u16)reg },
	};

	ret = s5k4ecgx_i2c_burst_write(client, seq, ARRAY_SIZE(seq));
	if (ret != 0) {
		pr_err("s5k4ecgx_i2c_burst_write() failed. (%d)\n", ret);
		return -1;
	}

	ret = s5k4ecgx_i2c_read(client, 0x0F12, data);
	if (ret != 0) {
		pr_err("s5k4ecgx_i2c_read() failed. (%d)\n", ret);
		return -1;
	}

	return 0;
}

static int s5k4ecgx_write_reg(struct i2c_client *client, s5k4ecgx_register_t reg, u16 data)
{
	int ret;

	reg_value_t seq[] = {
		{ 0xFCFC, 0xD000 },
		{ 0x0028, 0x7000 },
		{ 0x002A, (u16)reg },
		{ 0x0F12, data },
	};

	ret = s5k4ecgx_i2c_burst_write(client, seq, ARRAY_SIZE(seq));
	if (ret != 0) {
		pr_err("s5k4ecgx_i2c_burst_write() failed. (%d)\n", ret);
		return -1;
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////
// V4L2 functions

static int s5k4ecgx_g_chip_ident(struct v4l2_subdev *sd,
				   struct v4l2_dbg_chip_ident *id)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k4ecgx *s5k4ecgx = to_s5k4ecgx(client);

	id->ident = s5k4ecgx->model;
	id->revision = 0;

	return 0;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int s5k4ecgx_g_register(struct v4l2_subdev *sd,
				 struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	return s5k4ecgx_read_reg(client,
		(s5k4ecgx_register_t)reg->reg, (u16 *)&(reg->val));
}

static int s5k4ecgx_s_register(struct v4l2_subdev *sd,
				 const struct v4l2_dbg_register *reg)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	return s5k4ecgx_write_reg(client,
		(s5k4ecgx_register_t)reg->reg, (u16)reg->val);
}
#endif

static int s5k4ecgx_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k4ecgx *s5k4ecgx = to_s5k4ecgx(client);
	int ret = 0;

	if (enable) {
		if (s5k4ecgx->ctrls.cap_mode->val == V4L2_S5K4ECGX_CAP_MODE_STILL) {
			ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnablePreview, 0);
			ret += s5k4ecgx_write_reg(client, REG_TC_GP_NewConfigSync, 1);
			ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnablePreviewChanged, 1);
			ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnableCapture, 1);
			ret += s5k4ecgx_write_reg(client, REG_TC_GP_NewConfigSync, 1);
			ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnableCaptureChanged, 1);
		}
		else {
			ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnableCapture, 0);
			ret += s5k4ecgx_write_reg(client, REG_TC_GP_NewConfigSync, 1);
			ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnableCaptureChanged, 1);
			ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnablePreview, 1);
			ret += s5k4ecgx_write_reg(client, REG_TC_GP_NewConfigSync, 1);
			ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnablePreviewChanged, 1);
		}
	}
	else {
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnablePreview, 0);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_NewConfigSync, 1);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnablePreviewChanged, 1);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnableCapture, 0);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_NewConfigSync, 1);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnableCaptureChanged, 1);
	}
	if (ret != 0) {
		dev_err(&client->dev, "could not %s stream.\n",
			enable? "enable":"disable");
		return -EINVAL;
	}

	dev_info(&client->dev, "stream %s\n", enable? "on":"off");

	return 0;
}

static int s5k4ecgx_enum_fmt(struct v4l2_subdev *sd,
		unsigned int index,
		enum v4l2_mbus_pixelcode *code)
{
	if (index >= ARRAY_SIZE(s5k4ecgx_fmts))
		return -EINVAL;

	*code = s5k4ecgx_fmts[index];

	return 0;
}

static enum s5k4ecgx_res_support
s5k4ecgx_get_res_code(struct v4l2_mbus_framefmt *mf)
{
	int i;

	for (i=0; i < ARRAY_SIZE(s5k4ecgx_resolutions); i++) {
		if (s5k4ecgx_resolutions[i].width  == mf->width &&
			s5k4ecgx_resolutions[i].height == mf->height) {
			return s5k4ecgx_resolutions[i].res_code;
		}
	}
	return S5K4ECGX_FMT_INVALID;
}

static int s5k4ecgx_try_fmt(struct v4l2_subdev *sd,
			   struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);

	switch (mf->code)
	{
	case V4L2_MBUS_FMT_UYVY8_2X8:
	case V4L2_MBUS_FMT_VYUY8_2X8:
	case V4L2_MBUS_FMT_YVYU8_2X8:
	case V4L2_MBUS_FMT_YUYV8_2X8:
	case V4L2_MBUS_FMT_JPEG_1X8:
		if (S5K4ECGX_FMT_INVALID == s5k4ecgx_get_res_code(mf)) {
			dev_warn(&client->dev,
				 "%d x %d is not supported. set to default. (%d x %d)\n",
				 mf->width, mf->height,
				 S5K4ECGX_OUT_WIDTH_DEF, S5K4ECGX_OUT_HEIGHT_DEF);
			mf->width  = S5K4ECGX_OUT_WIDTH_DEF;
			mf->height = S5K4ECGX_OUT_HEIGHT_DEF;
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

static int s5k4ecgx_s_fmt(struct v4l2_subdev *sd,
			 struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k4ecgx *s5k4ecgx = to_s5k4ecgx(client);
	enum s5k4ecgx_res_support res_code;
	const reg_value_t *seq = NULL;
	size_t seq_len = 0;
	int ret;
	int cap_mode;

	res_code = s5k4ecgx_get_res_code(mf);
	if (res_code == S5K4ECGX_FMT_INVALID) {
		dev_err(&client->dev, "%d x %d is not supported.\n",
				 mf->width, mf->height);
		return -EINVAL;
	}
	s5k4ecgx->rect.width  = mf->width;
	s5k4ecgx->rect.height = mf->height;

	cap_mode = s5k4ecgx->ctrls.cap_mode->val;
	switch (res_code)
	{
	case S5K4ECGX_FMT_480P:
		switch (cap_mode)
		{
		case V4L2_S5K4ECGX_CAP_MODE_PREVIEW:
			seq = g_s5k4ecgx_i2cseq_preview_480p;
			seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_preview_480p);
			break;
		case V4L2_S5K4ECGX_CAP_MODE_STILL:
			seq = g_s5k4ecgx_i2cseq_capture_480p;
			seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_capture_480p);
			break;
		default:
			return -EINVAL;
		}
		break;

	case S5K4ECGX_FMT_720P:	
		switch (cap_mode)
		{
		case V4L2_S5K4ECGX_CAP_MODE_PREVIEW:
			seq = g_s5k4ecgx_i2cseq_preview_720p;
			seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_preview_720p);
			break;
		case V4L2_S5K4ECGX_CAP_MODE_STILL:
			seq = g_s5k4ecgx_i2cseq_capture_720p;
			seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_capture_720p);
			break;
		default:
			return -EINVAL;
		}
		break;

	case S5K4ECGX_FMT_QUAD_VGA:	
		switch (cap_mode)
		{
		case V4L2_S5K4ECGX_CAP_MODE_PREVIEW:
			seq = g_s5k4ecgx_i2cseq_preview_quad;
			seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_preview_quad);
			break;
		case V4L2_S5K4ECGX_CAP_MODE_STILL:
			seq = g_s5k4ecgx_i2cseq_capture_4to3;
			seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_capture_4to3);
			break;
		default:
			return -EINVAL;
		}
		break;

	default:
		switch (cap_mode)
		{
		case V4L2_S5K4ECGX_CAP_MODE_PREVIEW:
			seq = g_s5k4ecgx_i2cseq_preview_4to3;
			seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_preview_4to3);
			break;
		case V4L2_S5K4ECGX_CAP_MODE_STILL:
			seq = g_s5k4ecgx_i2cseq_capture_4to3;
			seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_capture_4to3);
			break;
		default:
			return -EINVAL;
		}
		break;
	}

	ret = s5k4ecgx_i2c_burst_write(client, seq, seq_len);
	if (cap_mode == V4L2_S5K4ECGX_CAP_MODE_STILL) {
		ret += s5k4ecgx_write_reg(client, REG_0TC_CCFG_usWidth, mf->width);
		ret += s5k4ecgx_write_reg(client, REG_0TC_CCFG_usHeight, mf->height);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_ActiveCapConfig, 0);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_NewConfigSync, 1);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_CapConfigChanged, 1);
		msleep(50);
		if (ret != 0) {
			dev_err(&client->dev, "could not set capture config. (%d)\n", ret);
			return -EINVAL;
		}
	}
	else {
		ret += s5k4ecgx_write_reg(client, REG_0TC_PCFG_usWidth, mf->width);
		ret += s5k4ecgx_write_reg(client, REG_0TC_PCFG_usHeight, mf->height);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_ActivePrevConfig, 0);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_PrevOpenAfterChange, 1);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_PrevConfigChanged, 1);
		msleep(50);
		if (ret != 0) {
			dev_err(&client->dev, "could not set preview config. (%d)\n", ret);
			return -EINVAL;
		}
	}

	dev_info(
		&client->dev, "set to %d x %d (%s)\n",
		mf->width, mf->height,
		s5k4ecgx->ctrls.cap_mode->val? "capture":"preview");

	return 0;
}

static int s5k4ecgx_g_fmt(struct v4l2_subdev *sd, struct v4l2_mbus_framefmt *mf)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k4ecgx *s5k4ecgx = to_s5k4ecgx(client);

	mf->width		= s5k4ecgx->rect.width;
	mf->height		= s5k4ecgx->rect.height;
	/* all belows are fixed */
	mf->code		= V4L2_MBUS_FMT_UYVY8_2X8;
	mf->field		= V4L2_FIELD_NONE;
	mf->colorspace		= V4L2_COLORSPACE_JPEG;

	return 0;
}

static int s5k4ecgx_g_crop(struct v4l2_subdev *sd, struct v4l2_crop *crop)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	dev_info(&client->dev, "@@@ %s\n", __FUNCTION__);
	/* TODO */
	msleep(5000);
	return -EINVAL;
}

static int s5k4ecgx_s_crop(struct v4l2_subdev *sd, const struct v4l2_crop *crop)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	dev_info(&client->dev, "@@@ %s\n", __FUNCTION__);
	/* TODO */
	msleep(5000);
	return -EINVAL;
}

static int s5k4ecgx_enum_fsizes(struct v4l2_subdev *sd,
				struct v4l2_frmsizeenum *fsize)
{
	if (!fsize)
		return -EINVAL;
	if (fsize->index >= ARRAY_SIZE(s5k4ecgx_resolutions))
		return -EINVAL;

	fsize->type            = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete.height = s5k4ecgx_resolutions[fsize->index].height;
	fsize->discrete.width  = s5k4ecgx_resolutions[fsize->index].width;

	return 0;
}

static int s5k4ecgx_init(struct v4l2_subdev *sd, u32 plat)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = s5k4ecgx_i2c_burst_write(client, g_s5k4ecgx_i2cseq_init,
					ARRAY_SIZE(g_s5k4ecgx_i2cseq_init));
	if (ret != 0) {
		dev_err(&client->dev, "init failed. (%d)\n", ret);
		return -EINVAL;
	}

	dev_info(&client->dev, "init completed\n");

	return 0;
}

static int s5k4ecgx_g_mbus_config(struct v4l2_subdev *sd,
				struct v4l2_mbus_config *cfg)
{
	cfg->type  = V4L2_MBUS_CSI2;
	cfg->flags = V4L2_MBUS_CSI2_2_LANE;
	return 0;
}

static int s5k4ecgx_set_scene_mode(struct i2c_client *client)
{
	struct s5k4ecgx *s5k4ecgx = to_s5k4ecgx(client);
	int ret = 0;

	static const reg_value_t seq_maxlei_auto[] = {
		{ 0xFCFC, 0xD000 },
		{ 0x0028, 0x7000 },
		{ 0x002A, 0x06B8 },
		{ 0x0F12, 0x452C },
		{ 0x0F12, 0x0005 },
	};
	static const reg_value_t seq_maxlei_night[] = {
		{ 0xFCFC, 0xD000 },
		{ 0x0028, 0x7000 },
		{ 0x002A, 0x06B8 },
		{ 0x0F12, 0xFFFF },
		{ 0x0F12, 0x00FF },
	};

	static const reg_value_t seq_expgain_auto[] = {
		{ 0xFCFC, 0xD000 },
		{ 0x0028, 0x7000 },
		{ 0x002A, 0x0638 },
		{ 0x0F12, 0x0001 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x0A3C },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x0D05 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x3408 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x3408 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x6810 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x8214 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0xC350 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0xC350 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0xC350 },
		{ 0x0F12, 0x0000 },
	};
	static const reg_value_t seq_expgain_action[] = {
		{ 0xFCFC, 0xD000 },
		{ 0x0028, 0x7000 },
		{ 0x002A, 0x0638 },
		{ 0x0F12, 0x0001 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x0A3C },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x0D05 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x3408 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x3408 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x3408 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x3408 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x3408 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x3408 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x3408 },
		{ 0x0F12, 0x0000 },
	};
	static const reg_value_t seq_expgain_night[] = {
		{ 0xFCFC, 0xD000 },
		{ 0x0028, 0x7000 },
		{ 0x002A, 0x0638 },
		{ 0x0F12, 0x0001 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x1478 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x1A0A },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x6810 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x6810 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0xD020 },
		{ 0x0F12, 0x0000 },
		{ 0x0F12, 0x0428 },
		{ 0x0F12, 0x0001 },
		{ 0x0F12, 0x1A80 },
		{ 0x0F12, 0x0006 },
		{ 0x0F12, 0x1A80 },
		{ 0x0F12, 0x0006 },
		{ 0x0F12, 0x1A80 },
		{ 0x0F12, 0x0006 },
	};

	static const reg_value_t seq_framerate_auto[] = {
		{ 0xFCFC, 0xD000 },
		{ 0x0028, 0x7000 },
		{ 0x002A, 0x02C2 },
		{ 0x0F12, 0x03E8 },	//REG_0TC_PCFG_usMaxFrTimeMsecMult10 //03E8h:10fps //
		{ 0x0F12, 0x014A },	//REG_0TC_PCFG_usMinFrTimeMsecMult10 //014Ah:30fps //
		{ 0x002A, 0x03B4 },
		{ 0x0F12, 0x07D0 },	//REG_0TC_CCFG_usMaxFrTimeMsecMult10 //07D0h:5fps  //
		{ 0x0F12, 0x03E8 },	//REG_0TC_CCFG_usMinFrTimeMsecMult10 //03E8h:10fps //
	};
	static const reg_value_t seq_framerate_night[] = {
		{ 0xFCFC, 0xD000 },
		{ 0x0028, 0x7000 },
		{ 0x002A, 0x02C2 },
		{ 0x0F12, 0x09C4 },	//REG_0TC_PCFG_usMaxFrTimeMsecMult10 //09C4h:4fps //
		{ 0x0F12, 0x014A },	//REG_0TC_PCFG_usMinFrTimeMsecMult10 //014Ah:30fps //
		{ 0x002A, 0x03B4 },
		{ 0x0F12, 0x1388 },	//REG_0TC_CCFG_usMaxFrTimeMsecMult10 //1388h:2fps //
		{ 0x0F12, 0x1388 },	//REG_0TC_CCFG_usMinFrTimeMsecMult10 //1388h:2fps //
	};

	static const reg_value_t seq_sharpness_auto[] = {
		{ 0xFCFC, 0xD000 },
		{ 0x0028, 0x7000 },
		{ 0x002A, 0x0A28 },
		{ 0x0F12, 0x6024 },
		{ 0x002A, 0x0ADE },
		{ 0x0F12, 0x6024 },
		{ 0x002A, 0x0B94 },
		{ 0x0F12, 0x6024 },
		{ 0x002A, 0x0C4A },
		{ 0x0F12, 0x6024 },
		{ 0x002A, 0x0D00 },
		{ 0x0F12, 0x6024 },
	};
	static const reg_value_t seq_sharpness_barcode[] = {
		{ 0xFCFC, 0xD000 },
		{ 0x0028, 0x7000 },
		{ 0x002A, 0x0A28 },
		{ 0x0F12, 0xE082 },
		{ 0x002A, 0x0ADE },
		{ 0x0F12, 0xE082 },
		{ 0x002A, 0x0B94 },
		{ 0x0F12, 0xE082 },
		{ 0x002A, 0x0C4A },
		{ 0x0F12, 0xE082 },
		{ 0x002A, 0x0D00 },
		{ 0x0F12, 0xE082 },
	};

	static const reg_value_t seq_apply_changes[] = {
		{ 0xFCFC, 0xD000 },
		{ 0x0028, 0x7000 },
		{ 0x002A, 0x0266 },
		{ 0x0F12, 0x0000 },	//REG_TC_GP_ActivePrevConfig //
		{ 0x002A, 0x026A },
		{ 0x0F12, 0x0001 },	//REG_TC_GP_PrevOpenAfterChange //
		{ 0x002A, 0x0268 },
		{ 0x0F12, 0x0001 },	//REG_TC_GP_PrevConfigChanged //
		{ 0x002A, 0x026E },
		{ 0x0F12, 0x0000 },	//REG_TC_GP_ActiveCapConfig //
		{ 0x002A, 0x026A },
		{ 0x0F12, 0x0001 },	//REG_TC_GP_PrevOpenAfterChange //
		{ 0x002A, 0x0270 },
		{ 0x0F12, 0x0001 },	//REG_TC_GP_CapConfigChanged //
		{ 0x002A, 0x024E },
		{ 0x0F12, 0x0001 },	//REG_TC_GP_NewConfigSync //
		{ 0x002A, 0x023E },
		{ 0x0F12, 0x0001 },	//REG_TC_GP_EnablePreview //
		{ 0x0F12, 0x0001 },	//REG_TC_GP_EnablePreviewChanged //
	};

	u16 use_afit	= 0x0000;
	u16 iso_type	= 0x0000;
	u16 iso_val		= 0x0000;
	u16 iso_dgain	= 0x0200;
	u16 saturation	= 0x0000;
	u16 oscar_isat	= 0x0350;
	const reg_value_t *seq_maxlei = seq_maxlei_auto;
	size_t seq_maxlei_len = ARRAY_SIZE(seq_maxlei_auto);
	const reg_value_t *seq_expgain = seq_expgain_auto;
	size_t seq_expgain_len = ARRAY_SIZE(seq_expgain_auto);
	const reg_value_t *seq_framerate = seq_framerate_auto;
	size_t seq_framerate_len = ARRAY_SIZE(seq_framerate_auto);
	const reg_value_t *seq_sharpness = seq_sharpness_auto;
	size_t seq_sharpness_len = ARRAY_SIZE(seq_sharpness_auto);

//--- STS
		dev_err(&client->dev, "%s: set Scene:%d\n",
			 __FUNCTION__, s5k4ecgx->ctrls.scn_mode->val);
//--- STS

	switch (s5k4ecgx->ctrls.scn_mode->val)
	{
	case S5K4ECGX_SCENE_MODE_AUTO:
		break;

	case S5K4ECGX_SCENE_MODE_ACTION:
		use_afit = 0x0001;
		iso_type = 0x0002;	iso_val = 0x0100;	iso_dgain = 0x0100;
		seq_expgain = seq_expgain_action;
		seq_expgain_len = ARRAY_SIZE(seq_expgain_action);
		break;

	case S5K4ECGX_SCENE_MODE_NIGHT:
		seq_maxlei = seq_maxlei_night;
		seq_maxlei_len = ARRAY_SIZE(seq_maxlei_night);
		oscar_isat = 0x15C0;
		seq_expgain = seq_expgain_night;
		seq_expgain_len = ARRAY_SIZE(seq_expgain_night);
		seq_framerate = seq_framerate_night;
		seq_framerate_len = ARRAY_SIZE(seq_framerate_night);
		break;

	case S5K4ECGX_SCENE_MODE_SUNSET:
		break;

	case S5K4ECGX_SCENE_MODE_PARTY:
		use_afit = 0x0001;
		iso_type = 0x0001;	iso_val = 0x0340;	iso_dgain = 0x0180;
		saturation = 0x0030;
		break;

	case S5K4ECGX_SCENE_MODE_BARCODE:
		seq_sharpness = seq_sharpness_barcode;
		seq_sharpness_len = ARRAY_SIZE(seq_sharpness_barcode);
		break;

	default:
		pr_err("%s: invalid mode %d\n", __FUNCTION__, s5k4ecgx->ctrls.scn_mode->val);
		return -1;
	}

	ret  = s5k4ecgx_write_reg(client, afit_bUseNB_Afit, use_afit);
	ret += s5k4ecgx_write_reg(client, REG_SF_USER_IsoType, iso_type);
	ret += s5k4ecgx_write_reg(client, REG_SF_USER_IsoVal, iso_val);
	ret += s5k4ecgx_write_reg(client, REG_SF_USER_IsoChanged, 1);
	ret += s5k4ecgx_write_reg(client, lt_bUseSecISODgain, iso_dgain);
	ret += s5k4ecgx_write_reg(client, REG_TC_UserSaturation, saturation);
	ret += s5k4ecgx_write_reg(client, _ccm_oscar_iSaturation, oscar_isat);
	ret += s5k4ecgx_i2c_burst_write(client, seq_maxlei, seq_maxlei_len);
	ret += s5k4ecgx_i2c_burst_write(client, seq_expgain, seq_expgain_len);
	ret += s5k4ecgx_i2c_burst_write(client, seq_framerate, seq_framerate_len);
	ret += s5k4ecgx_i2c_burst_write(client, seq_sharpness, seq_sharpness_len);
	ret += s5k4ecgx_i2c_burst_write(client, seq_apply_changes, ARRAY_SIZE(seq_apply_changes));
	if (ret != 0) {
		pr_err("%s: write reg failed. (%d)", __FUNCTION__, ret);
		return -1;
	}

	return 0;
}

static int s5k4ecgx_set_af_mode(struct i2c_client *client )
{
	struct s5k4ecgx *s5k4ecgx = to_s5k4ecgx(client);
	int ret = 0;
	const reg_value_t *seq;
	size_t seq_len;

printk("%s: mode:%d\n",__FUNCTION__,s5k4ecgx->ctrls.scn_mode->val);

	if (s5k4ecgx->ctrls.scn_mode->val == S5K4ECGX_FOCUS_MACRO) {
		seq = g_s5k4ecgx_i2cseq_af_macro;
		seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_af_macro);
	}
	else {
		seq = g_s5k4ecgx_i2cseq_af_normal;
		seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_af_normal);
	}

	ret = s5k4ecgx_i2c_burst_write(client, seq, seq_len);
	if (ret != 0) {
		pr_err("s5k4ecgx_i2c_burst_write() failed. (%d)\n", ret);
		return -1;
	}

	if((s5k4ecgx->ctrls.scn_mode->val == S5K4ECGX_FOCUS_AUTO_ONESHOT) 
		|| (s5k4ecgx->ctrls.scn_mode->val == S5K4ECGX_FOCUS_INFINITY)) {
//	if(s5k4ecgx->ctrls.scn_mode->val == S5K4ECGX_FOCUS_INFINITY) {
printk("%s: Set Af Fixed\n",__FUNCTION__);
		seq = g_s5k4ecgx_i2cseq_af_fix;
		seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_af_fix);
		ret = s5k4ecgx_i2c_burst_write(client, seq, seq_len);
		if (ret != 0) {
			pr_err("s5k4ecgx_i2c_burst_write() failed. (%d)\n", ret);
			return -1;
		}
	}

	return 0;
}

static int s5k4ecgx_s_ctrl(struct v4l2_ctrl *ctrl)
{	
	struct s5k4ecgx_ctrls *ctrls = container_of(
		ctrl->handler, struct s5k4ecgx_ctrls, ctrl_handler);
	struct s5k4ecgx *s5k4ecgx = container_of(
		ctrls, struct s5k4ecgx, ctrls);
	struct i2c_client *client = s5k4ecgx->client;
	int ret = 0;

	switch (ctrl->id)
	{
	case V4L2_CID_S5K4ECGX_CAP_MODE:
		/* nop */
		break;

	case V4L2_CID_S5K4ECGX_SCENE_MODE:
		ret = s5k4ecgx_set_scene_mode(client);
		break;

	case V4L2_CID_S5K4ECGX_FOCUS_MODE:
		ret = s5k4ecgx_set_af_mode(client);
		break;

	default:
		dev_warn(&client->dev, "%s: unsupported id 0x%08x\n",
			 __FUNCTION__, ctrl->id);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct v4l2_subdev_core_ops s5k4ecgx_subdev_core_ops = {
	.g_chip_ident = s5k4ecgx_g_chip_ident,
	.init		  = s5k4ecgx_init,
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register	  = s5k4ecgx_g_register,
	.s_register	  = s5k4ecgx_s_register,
#endif
};

static const struct v4l2_subdev_video_ops s5k4ecgx_subdev_video_ops = {
	.s_stream        = s5k4ecgx_s_stream,
	.g_mbus_fmt      = s5k4ecgx_g_fmt,
	.s_mbus_fmt      = s5k4ecgx_s_fmt,
	.try_mbus_fmt    = s5k4ecgx_try_fmt,
	.enum_framesizes = s5k4ecgx_enum_fsizes,
	.enum_mbus_fmt   = s5k4ecgx_enum_fmt,
	.g_crop          = s5k4ecgx_g_crop,
	.s_crop          = s5k4ecgx_s_crop,
	.g_mbus_config   = s5k4ecgx_g_mbus_config,
};

static const struct v4l2_subdev_ops s5k4ecgx_subdev_ops = {
	.core  = &s5k4ecgx_subdev_core_ops,
	.video = &s5k4ecgx_subdev_video_ops,
};

static const struct v4l2_ctrl_ops s5k4ecgx_ctrl_ops = {
	.s_ctrl = s5k4ecgx_s_ctrl,
};

static const struct v4l2_ctrl_config s5k4ecgx_ctrl_cap_mode_cfg = {
	.ops  = &s5k4ecgx_ctrl_ops,
	.id   = V4L2_CID_S5K4ECGX_CAP_MODE,
	.name = "Capture mode",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min  = V4L2_S5K4ECGX_CAP_MODE_PREVIEW,
	.max  = V4L2_S5K4ECGX_CAP_MODE_STILL,
	.step = 1,
	.def  = V4L2_S5K4ECGX_CAP_MODE_PREVIEW,
};

static const struct v4l2_ctrl_config s5k4ecgx_ctrl_scene_mode_cfg = {
	.ops  = &s5k4ecgx_ctrl_ops,
	.id   = V4L2_CID_S5K4ECGX_SCENE_MODE,
	.name = "Scene mode",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min  = S5K4ECGX_SCENE_MODE_AUTO,
	.max  = S5K4ECGX_SCENE_MODE_BARCODE,
	.step = 1,
	.def  = S5K4ECGX_SCENE_MODE_AUTO,
};

static const struct v4l2_ctrl_config s5k4ecgx_ctrl_focus_mode_cfg = {
	.ops  = &s5k4ecgx_ctrl_ops,
	.id   = V4L2_CID_S5K4ECGX_FOCUS_MODE,
	.name = "Focus mode",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min  = S5K4ECGX_FOCUS_AUTO_ONESHOT,
	.max  = S5K4ECGX_FOCUS_MANUAL,
	.step = 1,
	.def  = S5K4ECGX_FOCUS_AUTO_ONESHOT,
};

////////////////////////////////////////////////////////////////////////////////
// sysfs functions (for debug)

#ifdef DEBUG_ENABLE_SYSFS

static ssize_t
s5k4ecgx_sysfs_init(
	struct device *dev, struct device_attribute *attr,
	char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct s5k4ecgx *s5k4ecgx = to_s5k4ecgx(client);

	int ret;

	struct v4l2_mbus_framefmt mf;
	enum s5k4ecgx_res_support res_code;
	const reg_value_t *seq = NULL;
	size_t seq_len = 0;
	int cap_mode;

	/*
	ret = s5k4ecgx_i2c_burst_write(client, g_s5k4ecgx_i2cseq_init,
					ARRAY_SIZE(g_s5k4ecgx_i2cseq_init));
	if (ret != 0) {
		return sprintf(buf, "ERROR\n");
	}
	*/

	mf.width  = s5k4ecgx->rect.width;
	mf.height = s5k4ecgx->rect.height;
	res_code = s5k4ecgx_get_res_code(&mf);
	if (res_code == S5K4ECGX_FMT_INVALID) {
		return sprintf(buf, "ERROR\n");
	}
	cap_mode = s5k4ecgx->ctrls.cap_mode->val;
	switch (res_code)
	{
	case S5K4ECGX_FMT_480P:
		switch (cap_mode)
		{
		case V4L2_S5K4ECGX_CAP_MODE_PREVIEW:
			seq = g_s5k4ecgx_i2cseq_preview_480p;
			seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_preview_480p);
			break;
		case V4L2_S5K4ECGX_CAP_MODE_STILL:
			seq = g_s5k4ecgx_i2cseq_capture_480p;
			seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_capture_480p);
			break;
		default:
			return -EINVAL;
		}
		break;

	case S5K4ECGX_FMT_720P:	
		switch (cap_mode)
		{
		case V4L2_S5K4ECGX_CAP_MODE_PREVIEW:
			seq = g_s5k4ecgx_i2cseq_preview_720p;
			seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_preview_720p);
			break;
		case V4L2_S5K4ECGX_CAP_MODE_STILL:
			seq = g_s5k4ecgx_i2cseq_capture_720p;
			seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_capture_720p);
			break;
		default:
			return -EINVAL;
		}
		break;

	default:
		switch (cap_mode)
		{
		case V4L2_S5K4ECGX_CAP_MODE_PREVIEW:
			seq = g_s5k4ecgx_i2cseq_preview_4to3;
			seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_preview_4to3);
			break;
		case V4L2_S5K4ECGX_CAP_MODE_STILL:
			seq = g_s5k4ecgx_i2cseq_capture_4to3;
			seq_len = ARRAY_SIZE(g_s5k4ecgx_i2cseq_capture_4to3);
			break;
		default:
			return -EINVAL;
		}
		break;
	}

	ret = s5k4ecgx_i2c_burst_write(client, seq, seq_len);
	if (cap_mode == V4L2_S5K4ECGX_CAP_MODE_STILL) {
		ret += s5k4ecgx_write_reg(client, REG_0TC_CCFG_usWidth, mf.width);
		ret += s5k4ecgx_write_reg(client, REG_0TC_CCFG_usHeight, mf.height);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_ActiveCapConfig, 0);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_NewConfigSync, 1);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_CapConfigChanged, 1);
		msleep(50);
		if (ret != 0) {
			dev_err(&client->dev, "could not set capture config. (%d)\n", ret);
			return -EINVAL;
		}
	}
	else {
		ret += s5k4ecgx_write_reg(client, REG_0TC_PCFG_usWidth, mf.width);
		ret += s5k4ecgx_write_reg(client, REG_0TC_PCFG_usHeight, mf.height);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_ActivePrevConfig, 0);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_PrevOpenAfterChange, 1);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_PrevConfigChanged, 1);
		msleep(50);
		if (ret != 0) {
			dev_err(&client->dev, "could not set preview config. (%d)\n", ret);
			return -EINVAL;
		}
	}

	return sprintf(buf, "OK\n");
}

static ssize_t
s5k4ecgx_sysfs_stream_on(
	struct device *dev, struct device_attribute *attr,
	char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct s5k4ecgx *s5k4ecgx = to_s5k4ecgx(client);

	int ret = 0;

	if (s5k4ecgx->ctrls.cap_mode->val == V4L2_S5K4ECGX_CAP_MODE_STILL) {
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnablePreview, 0);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_NewConfigSync, 1);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnablePreviewChanged, 1);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnableCapture, 1);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_NewConfigSync, 1);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnableCaptureChanged, 1);
	}
	else {
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnableCapture, 0);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_NewConfigSync, 1);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnableCaptureChanged, 1);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnablePreview, 1);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_NewConfigSync, 1);
		ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnablePreviewChanged, 1);
	}

	if (ret != 0) {
		return sprintf(buf, "ERROR\n");
	}
	return sprintf(buf, "OK\n");
}

static ssize_t
s5k4ecgx_sysfs_stream_off(
	struct device *dev, struct device_attribute *attr,
	char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct s5k4ecgx *s5k4ecgx = to_s5k4ecgx(client);

	int ret = 0;

	ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnablePreview, 0);
	ret += s5k4ecgx_write_reg(client, REG_TC_GP_NewConfigSync, 1);
	ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnablePreviewChanged, 1);
	ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnableCapture, 0);
	ret += s5k4ecgx_write_reg(client, REG_TC_GP_NewConfigSync, 1);
	ret += s5k4ecgx_write_reg(client, REG_TC_GP_EnableCaptureChanged, 1);

	if (ret != 0) {
		return sprintf(buf, "ERROR\n");
	}
	return sprintf(buf, "OK\n");
}

static ssize_t
s5k4ecgx_sysfs_reg_read(
	struct device *dev, struct device_attribute *attr,
	char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct s5k4ecgx *s5k4ecgx = to_s5k4ecgx(client);

	int ret;
	u16 val;

	ret = s5k4ecgx_read_reg(client, s5k4ecgx->sysfs_reg, &val);
	if (ret != 0) {
		return sprintf(buf, "ERROR\n");
	}

	return sprintf(buf, "0x%04x\n", val);
}

static ssize_t
s5k4ecgx_sysfs_reg_write(
	struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct s5k4ecgx *s5k4ecgx = to_s5k4ecgx(client);

	int ret;
	char *tmp, *p;
	s5k4ecgx_register_t reg;
	u16 val;

	tmp = (char *)kzalloc(count + 1, GFP_KERNEL);
	strcpy(tmp, buf);
	p = strchr(tmp, ' ');
	if (p != NULL) {
		*p = '\0';
		reg = (s5k4ecgx_register_t)simple_strtoul(tmp, NULL, 0);
		val = (u16)simple_strtoul(p+1, NULL, 0);
		ret = s5k4ecgx_write_reg(client, reg, val);
	}
	else {
		reg = (s5k4ecgx_register_t)simple_strtoul(tmp, NULL, 0);
		ret = s5k4ecgx_read_reg(client, reg, &val);
		dev_info(dev, "read value: 0x%04x\n", val);
	}

	if (ret != 0) {
		dev_err(dev, "failed to access register 0x%04x\n", reg);
		return count;
	}
	s5k4ecgx->sysfs_reg = reg;

	return count;
}

static DEVICE_ATTR(
	init, 0444, s5k4ecgx_sysfs_init, NULL);
static DEVICE_ATTR(
	stream_on, 0444, s5k4ecgx_sysfs_stream_on, NULL);
static DEVICE_ATTR(
	stream_off, 0444, s5k4ecgx_sysfs_stream_off, NULL);
static DEVICE_ATTR(
	reg, 0644, s5k4ecgx_sysfs_reg_read, s5k4ecgx_sysfs_reg_write);

static struct attribute *s5k4ecgx_sysfs_attributes[] = {
	&dev_attr_init.attr,
	&dev_attr_stream_on.attr,
	&dev_attr_stream_off.attr,
	&dev_attr_reg.attr,
	NULL,
};

static struct attribute_group s5k4ecgx_sysfs_attr_group = {
	.attrs = s5k4ecgx_sysfs_attributes,
};

static int s5k4ecgx_register_sysfs(struct device *dev)
{
	int ret;

	ret = sysfs_create_group(&dev->kobj, &s5k4ecgx_sysfs_attr_group);
	if (ret != 0) {
		dev_err(dev, "failed to create sysfs group (%d)\n", ret);
		return ret;
	}

	return 0;
}

static void s5k4ecgx_remove_sysfs(struct device *dev)
{
	sysfs_remove_group(&dev->kobj, &s5k4ecgx_sysfs_attr_group);
}

#endif /* DEBUG_ENABLE_SYSFS */

////////////////////////////////////////////////////////////////////////////////
// Init/Remove/Probe functions

static int s5k4ecgx_detect(struct i2c_client *client)
{
	int ret;
	u16 data = 0;

	ret = s5k4ecgx_read_reg(client, REG_FWverControlStr_usFWsenID, &data);
	if (ret != 0) {
		dev_err(&client->dev, "could not read sensor ID.\n");
		return ret;
	}
	if (data != S5K4ECGX_SENSOR_ID) {
		dev_err(&client->dev, "invalid sensor id 0x%04x\n", data);
		return -EINVAL;
	}
	dev_info(&client->dev, "sensor id: 0x%04x\n", data);

	return 0;
}

static void s5k4ecgx_init_ctrls(struct s5k4ecgx_ctrls *ctrls)
{
	struct v4l2_ctrl_handler *handler = &ctrls->ctrl_handler;
	size_t nr_of_controls_hint =
		(sizeof(struct s5k4ecgx_ctrls) - sizeof(struct v4l2_ctrl_handler))
		/ sizeof(struct v4l2_ctrl *);

	v4l2_ctrl_handler_init(handler, nr_of_controls_hint);

	/* capture mode */
	ctrls->cap_mode = v4l2_ctrl_new_custom(
		handler, &s5k4ecgx_ctrl_cap_mode_cfg, NULL
		);

	/* scene mode */
	ctrls->scn_mode = v4l2_ctrl_new_custom(
		handler, &s5k4ecgx_ctrl_scene_mode_cfg, NULL
		);

	/* focus mode */
	ctrls->scn_mode = v4l2_ctrl_new_custom(
		handler, &s5k4ecgx_ctrl_focus_mode_cfg, NULL
		);

}

static int s5k4ecgx_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct s5k4ecgx *s5k4ecgx = NULL;
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	int ret = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(&adapter->dev,
			"I2C-Adapter doesn't support I2C_FUNC_SMBUS_WORD\n");
		return -EIO;
	}

	ret = s5k4ecgx_detect(client);
	if (ret != 0)
		return ret;

#ifdef DEBUG_ENABLE_SYSFS
	ret = s5k4ecgx_register_sysfs(&client->dev);
	if (ret != 0)
		return ret;
#endif /* DEBUG_ENABLE_SYSFS */

	s5k4ecgx = kzalloc(sizeof(struct s5k4ecgx), GFP_KERNEL);
	if (s5k4ecgx == NULL)
		return -ENOMEM;
	s5k4ecgx->client = client;

	v4l2_i2c_subdev_init(
		&s5k4ecgx->subdev, client, &s5k4ecgx_subdev_ops);
	s5k4ecgx_init_ctrls(&s5k4ecgx->ctrls);
	s5k4ecgx->subdev.ctrl_handler = &s5k4ecgx->ctrls.ctrl_handler;

	s5k4ecgx->rect.left   = 0;
	s5k4ecgx->rect.top    = 0;
	s5k4ecgx->rect.width  = S5K4ECGX_OUT_WIDTH_DEF;
	s5k4ecgx->rect.height = S5K4ECGX_OUT_HEIGHT_DEF;
	s5k4ecgx->pixfmt      = V4L2_PIX_FMT_UYVY;

	return ret;
}

static int s5k4ecgx_remove(struct i2c_client *client)
{
	struct s5k4ecgx *s5k4ecgx = to_s5k4ecgx(client);
	kfree(s5k4ecgx);
#ifdef DEBUG_ENABLE_SYSFS
	s5k4ecgx_remove_sysfs(&client->dev);
#endif /* DEBUG_ENABLE_SYSFS */
	return 0;
}

static const struct i2c_device_id s5k4ecgx_id_table[] = {
	{ S5K4ECGX_I2C_DEV_NAME, 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, s5k4ecgx_id_table);

static struct i2c_driver s5k4ecgx_i2c_driver = {
	.driver = {
		.owner 	= THIS_MODULE,
		.name 	= S5K4ECGX_DRIVER_NAME,
	},
	.probe 		= s5k4ecgx_probe,
	.remove 	= s5k4ecgx_remove,
	.id_table 	= s5k4ecgx_id_table,
};

static int __init s5k4ecgx_mod_init(void)
{
	int ret;

	ret = i2c_add_driver(&s5k4ecgx_i2c_driver);
	if (ret != 0)
		pr_err("%s:driver registration failed, error=%d\n",
			__func__, ret);

	return ret;
}

static void __exit s5k4ecgx_mod_exit(void)
{
	i2c_del_driver(&s5k4ecgx_i2c_driver);
}

module_init(s5k4ecgx_mod_init);
module_exit(s5k4ecgx_mod_exit);

MODULE_AUTHOR("SANYO Techno Solutions Tottori Co., Ltd.");
MODULE_DESCRIPTION("SAMSUNG S5K4ECGX Camera Driver");
MODULE_LICENSE("GPL");
