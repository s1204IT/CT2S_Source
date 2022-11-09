/* Marvell ISP S5K5E3 Driver
 *
 * Copyright (C) 2009-2014 Marvell International Ltd.
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
#include "b52-s5k5e3.h"

static int S5K5E3_get_pixelclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
{
	/*pixel clock is 179.4Mhz*/
	*rate = 0xab16d40;
	return 0;
}
static int S5K5E3_get_dphy_desc(struct v4l2_subdev *sd, struct csi_dphy_desc *dphy_desc, u32 mclk)
{
	return 0;
}

