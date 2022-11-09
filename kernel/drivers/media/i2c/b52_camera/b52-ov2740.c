/* Marvell ISP OV2740 Driver
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
#include "b52-ov2740.h"

static int OV2740_get_mipiclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
{
	int temp1, temp2;
	int pll1_pre_divp, pll1_pre_div2x, pll1_multiplier, pll1_divm;
	int pll1_pre_div2x_map[] = {2, 3, 4, 5, 6, 8, 12, 16};
	u32 VCO;

	struct b52_sensor *sensor = to_b52_sensor(sd);

	b52_sensor_call(sensor, i2c_read, 0x030a, &temp1, 1);
	if (temp1 & 0x01)
		pll1_pre_divp = 2;
	else
		pll1_pre_divp = 1;

	b52_sensor_call(sensor, i2c_read, 0x0300, &temp1, 1);
	temp2 = temp1 & 0x07;
	pll1_pre_div2x = pll1_pre_div2x_map[temp2];

	b52_sensor_call(sensor, i2c_read, 0x0301, &temp1, 1);
	temp2 = ((temp1 & 0x03) << 8);
	b52_sensor_call(sensor, i2c_read, 0x0302, &temp1, 1);
	pll1_multiplier = temp1 + temp2;

	b52_sensor_call(sensor, i2c_read, 0x0303, &temp1, 1);
	pll1_divm = temp1 & 0x0f;

	VCO = mclk / pll1_pre_divp / pll1_pre_div2x * 2 * pll1_multiplier;

	*rate = VCO / (1 + pll1_divm);

	return 0;
}

static int OV2740_get_pixelclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
{
	int temp1, temp2;
	int pll2_pre_divp, pll2_pre_div2x, pll2_multiplier, pll2_sys_prediv, pll2_sys_div2x;
	int pll2_pre_div2x_map[] = {2, 3, 4, 5, 6, 8, 12, 16};
	int pll2_sys_div2x_map[] = {2, 3, 4, 5, 6, 7, 8, 9, 10};
	u32 VCO;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	b52_sensor_call(sensor, i2c_read, 0x0312, &temp1, 1);
	if (temp1 & 0x10)
		pll2_pre_divp = 2;
	else
		pll2_pre_divp = 1;

	b52_sensor_call(sensor, i2c_read, 0x030b, &temp1, 1);
	temp2 = temp1 & 0x07;
	pll2_pre_div2x = pll2_pre_div2x_map[temp2];

	b52_sensor_call(sensor, i2c_read, 0x030c, &temp1, 1);
	temp2 = ((temp1 & 0x03) << 8);
	b52_sensor_call(sensor, i2c_read, 0x030d, &temp1, 1);
	pll2_multiplier = temp1 + temp2;

	b52_sensor_call(sensor, i2c_read, 0x030f, &temp1, 1);
	pll2_sys_prediv = temp1 & 0x0f;

	b52_sensor_call(sensor, i2c_read, 0x030e, &temp1, 1);
	temp2 = temp1 & 0x07;
	pll2_sys_div2x = pll2_sys_div2x_map[temp2];

	VCO = mclk / pll2_pre_divp / pll2_pre_div2x * 2 * pll2_multiplier;

	*rate = VCO / (1 + pll2_sys_prediv) /  pll2_sys_div2x * 2;

	return 0;
}

static int OV2740_get_dphy_desc(struct v4l2_subdev *sd,
			struct csi_dphy_desc *dphy_desc, u32 mclk)
{
	OV2740_get_mipiclock(sd, &dphy_desc->clk_freq, mclk);
	dphy_desc->hs_prepare = 90;
	dphy_desc->hs_zero  = 130;

	return 0;
}

static void OV2740_write_i2c(struct b52_sensor *sensor, u16 reg, u8 val)
{
	b52_sensor_call(sensor, i2c_write, reg, val, 1);
}

static u8 OV2740_read_i2c(struct b52_sensor *sensor, u16 reg)
{
	int temp;
	b52_sensor_call(sensor, i2c_read, reg, &temp, 1);
	return temp;
}

/********************************************************
 *	return value:										*
 *	bit[7]: 0 no otp info, 1 valid otp info/AWB			*
 *	bit[4]: 0 no otp lenc, 1 valid otp lenc				*
 ********************************************************/
static int OV2740_read_OTP(struct b52_sensor *sensor,
			struct b52_sensor_otp *otp, u32 *flag, u8 *lenc)
{
	int otp_flag, otp_base, temp, i, checksumLSC = 0;
	OV2740_write_i2c(sensor, 0x0100, 0x01);
	 pr_info("stream on = %d\n",OV2740_read_i2c(sensor, 0x0100));
	 usleep_range(2000, 2010);
	/* set 0x5000[5] to "0" */
	temp = OV2740_read_i2c(sensor, 0x5000);
	OV2740_write_i2c(sensor, 0x5000, (temp & (~0x20)));

	/* clear the flag*/
	*flag = 0;

	/*read OTP into buffer*/
	OV2740_write_i2c(sensor, 0x3d84, 0xC0);
	OV2740_write_i2c(sensor, 0x3d88, 0x70); /*OTP start address*/
	OV2740_write_i2c(sensor, 0x3d89, 0x10);
	OV2740_write_i2c(sensor, 0x3d8A, 0x71); /*OTP end address*/
	OV2740_write_i2c(sensor, 0x3d8B, 0xFF);
	OV2740_write_i2c(sensor, 0x3d81, 0x01); /*load otp into buffer*/
	usleep_range(10000, 10010);

	/*OTP base information */
	otp_flag = OV2740_read_i2c(sensor, 0x7010);
	otp_base = 0;
	if ((otp_flag & 0xc0) == 0x40)
	{
		otp_base = 0x7011;/*base address of info group 1*/
	}
	else if((otp_flag & 0x30) == 0x10)
	{
		otp_base = 0x7018;/*base address of info group 2*/
	}
	if (otp_base != 0)
	{
		*flag |= 0x80; /*valid info/wb in OTP*/
		/*read info*/
		otp->module_id = OV2740_read_i2c(sensor, otp_base);
		otp->lens_id = OV2740_read_i2c(sensor, otp_base + 1);
		/*read WB OTP*/
		otp->rg_ratio = (OV2740_read_i2c(sensor, otp_base + 4) << 2) + ((OV2740_read_i2c(sensor, otp_base + 6) >> 6) & 0x03);
		otp->bg_ratio = (OV2740_read_i2c(sensor, otp_base + 5) << 2) + ((OV2740_read_i2c(sensor, otp_base + 6) >> 4) & 0x03);
		pr_info("%s: WB OTP  is valid!,module_id = %d, lens_id = %d, rg_ratio = %d, bg_ratio = %d\n",
		__func__, otp->module_id, otp->lens_id, otp->rg_ratio, otp->bg_ratio);
	}
	else{
		*flag &= ~ 0x80; /*not info/wb in OTP*/
		otp->module_id = 0;
		otp->lens_id = 0;
		otp->rg_ratio = 0;
		otp->bg_ratio = 0;
		pr_err("%s: WB OTP is invalid!\n",__func__);
	}
	/*OTP Lenc Calibration*/
	otp_base = 0;
	if ((otp_flag & 0x0c) == 0x04)
		otp_base = 0x701f; /* base address of Lenc Calibration group 1*/

	/* read Lenc OTP */
	if (otp_base != 0)
	{
		for(i = 0; i < 240; i ++)
		{
			lenc[i]=OV2740_read_i2c(sensor, otp_base + i);
			checksumLSC += lenc[i];
		}
		checksumLSC = checksumLSC % 255 + 1;
		temp = OV2740_read_i2c(sensor, otp_base + 240);
		if (temp == checksumLSC)
			*flag |= 0x10;
		pr_info("%s: Lenc OTP is valid!\n",__func__);
	}
	else
	{
		for(i = 0; i < 240; i++)
			lenc[i] = 0;
		*flag &= ~ 0x10;
		pr_err("%s: Lenc OTP is invalid!\n",__func__);
	}

	/* clear OTP buffer, recommended use continuous write to accelerate */
	for(i = 0x7010; i <= 0x71FF; i++)
		OV2740_write_i2c(sensor, i, 0);
	/*set 0x5000[5] to "1"*/
	temp = OV2740_read_i2c(sensor, 0x5000);
	OV2740_write_i2c(sensor, 0x5000, temp | 0x20 );
	OV2740_write_i2c(sensor, 0x0100, 0x00);
	return *flag;
}

static int OV2740_update_wb(struct b52_sensor *sensor,
			struct b52_sensor_otp *otp, u32 *flag)
{
	int R_gain, G_gain, B_gain, Base_gain;
	if (*flag & 0x80 && otp->rg_ratio && otp->bg_ratio){
		/*calculate G gain*/
		R_gain = (RG_Ratio_Typical * 1000) / otp->rg_ratio;
		B_gain = (BG_Ratio_Typical * 1000) / otp->bg_ratio;
		G_gain = 1000;

		if (R_gain < 1000 || B_gain < 1000)
			Base_gain = R_gain > B_gain ? R_gain: B_gain;
		else
			Base_gain = G_gain;

		R_gain = 0x400 * R_gain / (Base_gain);
		B_gain = 0x400 * B_gain / (Base_gain);
		G_gain = 0x400 * G_gain / (Base_gain);
		/* update sensor WB gain */
		if (R_gain > 0x400)
		{
			OV2740_write_i2c(sensor, 0x500A, R_gain >> 8);
			OV2740_write_i2c(sensor, 0x500B, R_gain & 0x00ff);
		}
		if (G_gain > 0x400)
		{
			OV2740_write_i2c(sensor, 0x500C, G_gain >> 8);
			OV2740_write_i2c(sensor, 0x500D, G_gain & 0x00ff);
		}
		if (B_gain > 0x400)
		{
			OV2740_write_i2c(sensor, 0x500E, B_gain >> 8);
			OV2740_write_i2c(sensor, 0x500F, B_gain & 0x00ff);
		}
	}
	return *flag;
}

static int OV2740_update_lenc(struct b52_sensor *sensor,
			struct b52_sensor_otp *otp, u32 *flag, u8 *lenc)
{
	int temp, i;
	if (*flag & 0x10){
		/* enable LENC */
		temp = OV2740_read_i2c(sensor, 0x5000);
		OV2740_write_i2c(sensor, 0x5000, 0x80 | temp);
		for(i = 0; i < 240; i ++)
		{
			OV2740_write_i2c(sensor, 0x5800 + i, lenc[i]);
		}
	}
	return *flag;
}

static int OV2740_update_otp(struct v4l2_subdev *sd,
				struct b52_sensor_otp *otp)
{
	int ret = 0;
	int flag = 0;/* bit[7]: info/wb, bit[4]:lenc */
	u8 lenc[240];

	struct b52_sensor *sensor = to_b52_sensor(sd);
	if (otp->user_otp->otp_type ==  SENSOR_TO_SENSOR){
		/*read otp data firstly*/
		ret = OV2740_read_OTP(sensor, otp, &flag, lenc);
		if (ret < 0)
			goto fail;

		/*apply wb otp*/
		if ((otp->otp_ctrl & V4L2_CID_SENSOR_OTP_CONTROL_WB) && (flag & 0x80))
		{
			ret = OV2740_update_wb(sensor, otp, &flag);
			if (ret < 0)
				goto fail;
		}
		/*apply lenc otp*/
	    if ((otp->otp_ctrl & V4L2_CID_SENSOR_OTP_CONTROL_LENC) && (flag & 0x80))
		{
			ret	= OV2740_update_lenc(sensor, otp, &flag, lenc);
			if	(ret < 0)
				goto fail;
		}
	}
fail:
	if (ret < 0)
		return ret;
	return 0;
}