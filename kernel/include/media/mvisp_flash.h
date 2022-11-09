/*
 * Marvell ISP sensor driver
 *
 * Copyright (C) 2009-2010 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Created:  2010 Jiaquan Su <jqsu@marvell.com>
 * Modified: 2010 Jiaquan Su <jqsu@marvell.com>
 * Modified: 2011 Henry Zhao <xzhao10@marvell.com>
 */


#ifndef _MVISP_FLASH_H_
#define _MVISP_FLASH_H_
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
struct mvisp_flash_data_t {
	unsigned int		pwm_period_ns;
	unsigned int		lth_brightness;
	unsigned char		pwm_id;
	int flash_enable;	/* Flash enable data; -1 means invalid */
	int torch_enable;	/* Torch enable data; -1 means invalid */
};
struct mvisp_board_flash_t {
	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct device		*dev;
	struct pwm_device	*pwm;
	struct mvisp_flash_data_t pdata;

};

struct v4l2_subdev *mvisp_board_flash_create(struct device *parent,
					int id, void *pdata);
#endif
