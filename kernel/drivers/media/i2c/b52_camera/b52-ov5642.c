/* Marvell ISP OV5642 Driver
 *
 * Copyright (C) 2009-2010 Marvell International Ltd.
 *
 * Based on mt9v011 -Micron 1/4-Inch VGA Digital Image OV5642
 *
 * Copyright (c) 2009 Mauro Carvalho Chehab (mchehab@redhat.com)
 * This code is placed under the terms of the GNU General Public License v2
 */

#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/fixed.h>

#include <asm/div64.h>
#include <media/v4l2-device.h>
#include <media/v4l2-chip-ident.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-ctrls.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include "b52-ov5642.h"

static int OV5642_get_dphy_desc(struct v4l2_subdev *sd,
			struct csi_dphy_desc *dphy_desc, u32 mclk)
{
	/*For test*/
	dphy_desc->clk_freq = 1000000;
	dphy_desc->hs_prepare = 50;
	dphy_desc->hs_zero  = 150;

	return 0;
}

static int OV5642_get_pixelclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
{
	u32 tmp = 0, prev_div_2, div1to2p5_2, divp, seld5, divs;
	u32 vco;
	struct b52_sensor *sensor = to_b52_sensor(sd);
	b52_sensor_call(sensor, i2c_read, 0x3012, &tmp, 1);
	prev_div_2 = tmp & 0x03;
	switch (prev_div_2) {
	case 0:
	case 1:
	case 2:
	case 3:
	case 4:
		prev_div_2 += 2;
		break;
	case 5:
	case 6:
	case 7:
		prev_div_2 = 4 + (prev_div_2-5)*2;
		break;
	}
	b52_sensor_call(sensor, i2c_read,
			0x3011, &tmp, 1);
	divp = tmp & 0x3f;
	b52_sensor_call(sensor, i2c_read,
			0x300f, &tmp, 1);
	seld5 = tmp & 0x03;
	switch (seld5) {
	case 0:
	case 1:
		seld5 = 1;
		break;
	case 2:
		seld5 = 4;
		break;
	case 3:
		seld5 = 5;
		break;
	}
	vco = mclk/prev_div_2*divp*seld5/2;
	div1to2p5_2 = tmp & 0x03;
	switch (div1to2p5_2) {
	case 0:
	case 1:
		div1to2p5_2 = 2;
		break;
	case 2:
		div1to2p5_2 = 4;
		break;
	case 3:
		div1to2p5_2 = 5;
		break;
	}
	b52_sensor_call(sensor, i2c_read,
			0x3010, &tmp, 1);
	divs = ((tmp>>4) & 0x0f) + 1;
	b52_sensor_call(sensor, i2c_read,
			0x3003, &tmp, 1);
	if (!(tmp&0x01)) {		/* DVP mode */
		*rate = vco / divs / div1to2p5_2;
	}
	return 0;
}
