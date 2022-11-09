/* Marvell ISP S5K5E2 Driver
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
#include "b52-s5k5e2.h"
#define SC2_PIN_ST_GPIO	2
#define SC2_MOD_B52ISP	2
extern int sc2_select_pins_state(int id, int pin_state, int sc2_mod);

static int S5K5E2_get_pixelclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
{
	/*pixel clock is 194.13 Mhz*/
	int temp1, temp2;
	int pre_pll_clk_div, pll_multiplier, sys_div;

	struct b52_sensor *sensor = to_b52_sensor(sd);

	b52_sensor_call(sensor, i2c_read, 0x0305, &temp1, 1);
	pre_pll_clk_div = temp1 & 0x3f;

	b52_sensor_call(sensor, i2c_read, 0x0306, &temp1, 1);
	temp2 = ((temp1 & 0x03) << 8);
	b52_sensor_call(sensor, i2c_read, 0x0307, &temp1, 1);
	pll_multiplier = temp1 + temp2;

	b52_sensor_call(sensor, i2c_read, 0x3c1c, &temp1, 1);
	sys_div = ((temp1 & 0xf0) >> 4);

	*rate = mclk / pre_pll_clk_div * pll_multiplier / sys_div;

	pr_debug("%s:MCLK = %ld, pixelclock = %ld\n", __func__, mclk, *rate);

	return 0;

}

static int S5K5E2_get_mipiclock(struct v4l2_subdev *sd, u32 *rate, u32 mclk)
{
    /*pixel clock is 485.33MHZ, 970.6 Mbps*/
	int temp1, temp2;
	int pre_pll_clk_div, pll_multiplier, pll_s;

	struct b52_sensor *sensor = to_b52_sensor(sd);

	b52_sensor_call(sensor, i2c_read, 0x0305, &temp1, 1);
	pre_pll_clk_div = temp1 & 0x3f;

	b52_sensor_call(sensor, i2c_read, 0x0306, &temp1, 1);
	temp2 = ((temp1 & 0x03) << 8);
	b52_sensor_call(sensor, i2c_read, 0x0307, &temp1, 1);
	pll_multiplier = temp1 + temp2;

	b52_sensor_call(sensor, i2c_read, 0x3c1f, &temp1, 1);
	pll_s = temp1 & 0x07;

	*rate = mclk / pre_pll_clk_div * pll_multiplier / (1 << pll_s) / 2;

	pr_debug("%s:MCLK = %ld, mipiclock = %ld\n", __func__, mclk, *rate);

	return 0;
}
static int S5K5E2_get_dphy_desc(struct v4l2_subdev *sd, struct csi_dphy_desc *dphy_desc, u32 mclk)
{
	S5K5E2_get_mipiclock(sd, &dphy_desc->clk_freq, mclk);
	dphy_desc->hs_prepare = 60;
	dphy_desc->hs_zero  = 130;

	return 0;
}
static int b52_sensor_power_on_s5k5e2(struct v4l2_subdev *sd)
{
	int ret	= 0;
	struct sensor_power *power;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	power = (struct sensor_power *) &(sensor->power);

	if (power->pwdn >= 0)
		gpio_request(power->pwdn, "CAM_ENABLE_LOW");

	if (power->pwdn >= 0)
		gpio_direction_output(power->pwdn, power->pwdn_value);

	if (power->avdd_2v8) {
		regulator_set_voltage(power->avdd_2v8,
					2800000, 2800000);
		ret = regulator_enable(power->avdd_2v8);
		if (ret < 0)
			goto avdd_err;
	}
	if (power->dovdd_1v8) {
		regulator_set_voltage(power->dovdd_1v8,
					1800000, 1800000);
		ret = regulator_enable(power->dovdd_1v8);
		if (ret < 0)
			goto dovdd_err;
	}
	if (power->dvdd_1v2) {
		regulator_set_voltage(power->dvdd_1v2,
					1200000, 1200000);
		ret = regulator_enable(power->dvdd_1v2);
		if (ret < 0)
			goto dvdd_err;
	}
	if (power->af_2v8) {
		regulator_set_voltage(power->af_2v8,
					2800000, 2800000);
		ret = regulator_enable(power->af_2v8);
		if (ret < 0)
			goto af_err;
	}
	if (power->pwdn >= 0)
		gpio_direction_output(power->pwdn, !power->pwdn_value);

	return 0;

af_err:
	regulator_disable(power->dvdd_1v2);
dvdd_err:
	regulator_disable(power->dovdd_1v8);
dovdd_err:
	regulator_disable(power->af_2v8);
avdd_err:
	if (sensor->i2c_dyn_ctrl)
		ret = sc2_select_pins_state(sensor->pos - 1,
				SC2_PIN_ST_GPIO, SC2_MOD_B52ISP);
	return ret;
}


static u8 S5K5E2_write_i2c(struct b52_sensor *sensor, u16 reg, u8 val)
{
	b52_sensor_call(sensor, i2c_write, reg, val, 1);
}
static u8 S5K5E2_read_i2c(struct b52_sensor *sensor, u16 reg)
{
	int temp1;
	b52_sensor_call(sensor, i2c_read, reg, &temp1, 1);
	return temp1;
}

static int check_otp(struct b52_sensor *sensor,
			struct b52_sensor_otp *otp)
{
	int temp1 = 0;
	int temp2 = 0;
	int temp3 = 0;
	int disable = 0;
	 /*OTP_access_start*/
	S5K5E2_write_i2c( sensor, 0x0A00, 0x04);
	S5K5E2_write_i2c( sensor, 0x0A02, 0x0c); /*page 12*/
	S5K5E2_write_i2c( sensor, 0x0A00, 0x01);
	usleep_range(1000, 1010);
	temp1 = S5K5E2_read_i2c(sensor, 0x0A04);
	temp2 = S5K5E2_read_i2c(sensor, 0x0A05);
	temp3 = S5K5E2_read_i2c(sensor, 0x0A06);
	 if((temp1==0)&&(temp2==0)&&(temp3==0))
	{
		S5K5E2_write_i2c( sensor, 0x0b00, 0x00);
		pr_info("%s:module has not otp data \n",__func__);
		 return -1;
	}
	 else
	 {
	 	S5K5E2_write_i2c( sensor, 0x0b00, 0x01);
		pr_info("%s:module has valid otp data \n",__func__);
		  return 0;
	}
}
static int update_otp_af(struct b52_sensor *sensor,
			struct b52_sensor_otp *otp)
{
	u32 tmp [3];
	int len;
	int ret = 0;
	char *paddr = NULL;
	u32  infinity = 0;
	u32  hyper = 0;
	u32  macro = 0;
	/*len = otp->user_otp->vcm_otp_len;*/
	len = 12;
	 /*OTP_access_start*/
	S5K5E2_write_i2c( sensor, 0x0A00, 0x04);
	S5K5E2_write_i2c( sensor, 0x0A02, 0x04); /*page4*/
	S5K5E2_write_i2c( sensor, 0x0A00, 0x01);
	infinity = S5K5E2_read_i2c(sensor, 0x0A04);
	infinity = infinity<< 8 | S5K5E2_read_i2c(sensor, 0x0A05);
	hyper = S5K5E2_read_i2c(sensor, 0x0A06);
	hyper = hyper<<8  | S5K5E2_read_i2c(sensor, 0x0A07);
	macro = S5K5E2_read_i2c(sensor, 0x0A08);
	macro = macro<<8  | S5K5E2_read_i2c(sensor, 0x0A09);
	otp->af_inf_cur = infinity;
	otp->af_cal_dir = hyper;
	otp->af_mac_cur = macro;
	tmp[0] = otp->af_inf_cur;
	tmp[1] = otp->af_mac_cur;
	tmp[2] = otp->af_cal_dir;
	pr_info("%s:otp->af_inf_cur = %0x, otp->af_cal_dir = %0x,otp->af_mac_cur = %0x\n",
		__func__,otp->af_inf_cur,otp->af_cal_dir,otp->af_mac_cur);
	if (len > 0) {
		paddr = (char *)otp->user_otp->otp_data;
		if (copy_to_user(paddr,&tmp[0], len))
			ret = -EIO;
	}
	return 0;
}

static int update_awb_gain(struct b52_sensor *sensor, int r_gain,
				int g_gain, int b_gain)
{
	/*wrint R_gain*/
	S5K5E2_write_i2c(sensor, 0x0210, r_gain >> 8);
	S5K5E2_write_i2c(sensor, 0x0211, r_gain & 0x00ff);
	/*wrint Gr_gain*/
	S5K5E2_write_i2c(sensor, 0x020E, g_gain >> 8);
	S5K5E2_write_i2c(sensor, 0x020F, g_gain & 0x00ff);
	/*wrint Gb_gain*/
	S5K5E2_write_i2c(sensor, 0x0214, g_gain >> 8);
	S5K5E2_write_i2c(sensor, 0x0215, g_gain & 0x00ff);
	/*wrint B_gain*/
	S5K5E2_write_i2c(sensor, 0x0212, b_gain >> 8);
	S5K5E2_write_i2c(sensor, 0x0213, b_gain & 0x00ff);
	return 0;
}
static int update_otp_wb(struct b52_sensor *sensor, struct b52_sensor_otp *otp)
{
	int temp1 = 0;
	int temp2 = 0;
	int temp3 = 0;
	int R_gain = 0;
         int G_gain = 0;
	int B_gain = 0;
          /*OTP_access_start*/
	S5K5E2_write_i2c( sensor, 0x0A00, 0x04);
	S5K5E2_write_i2c( sensor, 0x0A02, 0x05); /*page 5*/
	S5K5E2_write_i2c( sensor, 0x0A00, 0x01);
	usleep_range(1000, 1010);
	temp1 = S5K5E2_read_i2c(sensor, 0x0A14);
	temp2 = S5K5E2_read_i2c(sensor, 0x0A24);
	temp3 = S5K5E2_read_i2c(sensor, 0x0A34);
	R_gain = temp1|temp2|temp3;
	temp1 = S5K5E2_read_i2c(sensor, 0x0A15);
	temp2 = S5K5E2_read_i2c(sensor, 0x0A25);
	temp3 = S5K5E2_read_i2c(sensor, 0x0A35);
	R_gain= R_gain<<8 |(temp1|temp2|temp3);
	temp1 = S5K5E2_read_i2c(sensor, 0x0A16);
	temp2 = S5K5E2_read_i2c(sensor, 0x0A26);
	temp3 = S5K5E2_read_i2c(sensor, 0x0A36);
	G_gain = temp1|temp2|temp3;
	temp1 = S5K5E2_read_i2c(sensor, 0x0A17);
	temp2 = S5K5E2_read_i2c(sensor, 0x0A27);
	temp3 = S5K5E2_read_i2c(sensor, 0x0A37);
	G_gain = G_gain<<8|(temp1|temp2|temp3);
	temp1 = S5K5E2_read_i2c(sensor, 0x0A18);
	temp2 = S5K5E2_read_i2c(sensor, 0x0A28);
	temp3 = S5K5E2_read_i2c(sensor, 0x0A38);
	B_gain= temp1|temp2|temp3;
	temp1 = S5K5E2_read_i2c(sensor, 0x0A19);
	temp2 = S5K5E2_read_i2c(sensor, 0x0A29);
	temp3 = S5K5E2_read_i2c(sensor, 0x0A39);
	B_gain= B_gain<<8|(temp1|temp2|temp3);
	if (R_gain != 0)
	{
		 update_awb_gain(sensor,R_gain, G_gain, B_gain);
	}
	else
	{
		pr_info("s5k5e2 has not otp data\n");
	}
	return 0;
}

static int update_otp_lenc(struct b52_sensor *sensor, struct b52_sensor_otp *otp)
{
	int temp1 = 0;
	int temp2 = 0;
	int temp3 = 0;
	int disable = 0;
	          /*OTP_access_start*/
	S5K5E2_write_i2c( sensor, 0x0A00, 0x04);
	S5K5E2_write_i2c( sensor, 0x0A02, 0x05); /*page 5*/
	S5K5E2_write_i2c( sensor, 0x0A00, 0x01);
	usleep_range(1000, 1010);
	temp1 = S5K5E2_read_i2c(sensor, 0x0A04);
	temp2 = S5K5E2_read_i2c(sensor, 0x0A05);
	temp3 = S5K5E2_read_i2c(sensor, 0x0A06);
	disable = temp1|temp2|temp3;
	if(disable != 0)
	{
		pr_info("%s:lens otp is invalid \n",__func__);
		 return -1;
	}
	return 0;
}
static int S5K5E2_update_otp(struct v4l2_subdev *sd,
				struct b52_sensor_otp *otp)
{
         int ret = 0;
	struct b52_sensor *sensor = to_b52_sensor(sd);

	if (otp->user_otp->otp_type ==  SENSOR_TO_SENSOR){
		ret = check_otp(sensor, otp);
		if (ret < 0)
			return ret;
		if (otp->otp_ctrl & V4L2_CID_SENSOR_OTP_CONTROL_LENC) {
			ret = update_otp_lenc(sensor,otp);
			if (ret < 0)
			return ret;
		}

		if (otp->otp_ctrl & V4L2_CID_SENSOR_OTP_CONTROL_WB)
		{
			ret = update_otp_wb(sensor, otp);
			if (ret < 0)
			return ret;
		}
	}
	if (otp->user_otp->otp_type ==  SENSOR_TO_ISP)
	{
		ret = update_otp_af(sensor, otp);
		if (ret < 0)
			return ret;
	}
	return 0;
}


