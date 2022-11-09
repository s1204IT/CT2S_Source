/* drivers/input/touchscreen/gslX68X.h
 *
 * 2010 - 2013 SLIEAD Technology.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be a reference
 * to you, when you are integrating the SLIEAD's CTP IC into your system,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/byteorder/generic.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/firmware.h>
#include <linux/proc_fs.h>
#include <linux/i2c/gslX68X.h>
#include <linux/uaccess.h>
#include <linux/gpio.h>
#include <linux/pm_runtime.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/version.h>
#ifdef GSL_REPORT_POINT_SLOT
#include <linux/input/mt.h>
#endif

#ifdef GSL_DEBUG
#define print_info(fmt, args...)   \
	pr_info("[tp-gsl][%s]"fmt, __func__, ##args);
#else
	#define print_info(fmt, args...)
#endif

#define TP_SENSOR_DEBUG		0
#define tp_sensor_print(fmt, args...) \
	do { \
		if (TP_SENSOR_DEBUG)\
			pr_info("[tp-gslx68x][%s]" fmt, __func__, ##args); \
	} while (0)

#define TP_IDT_DEBUG		1
#define tp_idt_print(fmt, args...) \
	do {  \
		if (TP_IDT_DEBUG) \
			pr_info("[tp-gslx68x][%s]" fmt, __func__, ##args); \
	} while (0)

/* #define TPD_PROC_DEBUG */
#ifdef TPD_PROC_DEBUG
static struct proc_dir_entry *gsl_config_proc;
#define GSL_CONFIG_PROC_FILE "gsl_config"
#define CONFIG_LEN 31
static char gsl_read[CONFIG_LEN];
static u8 gsl_data_proc[8] = { 0 };

static u8 gsl_proc_flag;
#endif

#ifdef TP_PROXIMITY_SENSOR
#define SENSOR_NAME_PROXIMITY	"sitronix_ps"
/* indicate whether TP is in the state turn off screen for incoming call */
static int PROXIMITY_SWITCH;
/* indicate current state as proximity sensor  0: remote 1: near */
static int PROXIMITY_STATE;
/* indicate whether it has incoming call currently.
 * 1: TP need enter turn off screen for incoming call */
static int INCALLING;
/* indicate whether TP has power supply */
static int TP_POWER;
#endif

/*define golbal variable*/
bool need_free_pdata;
static struct gsl_ts_data *ddata;
/* extern int create_marvell_device_link(struct device *dev, char *name); */

static int gsl_read_interface(struct i2c_client *client, u8 reg, u8 *buf,
			      u32 num)
{
	struct i2c_msg xfer_msg[2];

	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = 1;
	/* xfer_msg[0].flags = client->flags & I2C_M_TEN; */
	xfer_msg[0].flags = !I2C_M_RD;
	xfer_msg[0].buf = &reg;
	/* xfer_msg[0].timing = 400; */

	xfer_msg[1].addr = client->addr;
	xfer_msg[1].len = num;
	xfer_msg[1].flags = I2C_M_RD;
	xfer_msg[1].buf = buf;
	/* xfer_msg[1].timing = 400; */

	if (reg < 0x80) {
		i2c_transfer(client->adapter, xfer_msg, ARRAY_SIZE(xfer_msg));
		usleep_range(5000, 10000);
	}

	return i2c_transfer(client->adapter, xfer_msg,
			    ARRAY_SIZE(xfer_msg)) ==
	    ARRAY_SIZE(xfer_msg) ? 0 : -EFAULT;
}

static int gsl_write_interface(struct i2c_client *client, const u8 reg,
			       u8 *buf, u32 num)
{
	struct i2c_msg xfer_msg[1];
	u8 tmp_buf[num + 1];
	tmp_buf[0] = reg;
	memcpy(tmp_buf + 1, buf, num);
	xfer_msg[0].addr = client->addr;
	xfer_msg[0].len = num + 1;
	/* xfer_msg[0].flags = client->flags & I2C_M_TEN; */
	xfer_msg[0].flags = !I2C_M_RD;
	xfer_msg[0].buf = tmp_buf;
	/* xfer_msg[0].timing = 400; */

	return i2c_transfer(client->adapter, xfer_msg, 1) == 1 ? 0 : -EFAULT;
}

static void gsl_load_fw(struct i2c_client *client,
			const struct fw_data *GSL_DOWNLOAD_DATA, int data_len)
{
	u8 buf[4] = { 0 };
	/* u8 send_flag = 1; */
	u8 addr = 0;
	u32 source_line = 0;
	u32 source_len = data_len;	/* ARRAY_SIZE(GSL_DOWNLOAD_DATA); */

	pr_info("=============gsl_load_fw start============== %d\n",
	       gsl_cfg_index);

	for (source_line = 0; source_line < source_len; source_line++) {
		/* init page trans, set the page val */
		addr = (u8) GSL_DOWNLOAD_DATA[source_line].offset;
		memcpy(buf, &GSL_DOWNLOAD_DATA[source_line].val, 4);
		gsl_write_interface(client, addr, buf, 4);
	}
	pr_info("=============gsl_load_fw end==============\n");
}

static void gsl_start_core(struct i2c_client *client)
{
	/* u8 tmp = 0x00; */
	u8 buf[4] = { 0 };
	/* FAE confirmed this is invalid 20121114
	   buf[0]=0;
	   buf[1]=0x10;
	   buf[2]=0xfe;
	   buf[3]=0x1;
	   gsl_write_interface(client,0xf0,buf,4);
	   buf[0]=0xf;
	   buf[1]=0;
	   buf[2]=0;
	   buf[3]=0;
	   gsl_write_interface(client,0x4,buf,4);
	 */
	usleep_range(20000, 30000);
	buf[0] = 0;
	gsl_write_interface(client, 0xe0, buf, 4);
#ifdef GSL_ALG_ID
	gsl_DataInit(gsl_cfg_table[gsl_cfg_index].data_id);
#endif
}

static void gsl_reset_core(struct i2c_client *client)
{
	u8 buf[4] = { 0x00 };

	buf[0] = 0x88;
	gsl_write_interface(client, 0xe0, buf, 4);
	usleep_range(5000, 10000);

	buf[0] = 0x04;
	gsl_write_interface(client, 0xe4, buf, 4);
	usleep_range(5000, 10000);

	buf[0] = 0;
	gsl_write_interface(client, 0xbc, buf, 4);
	usleep_range(5000, 10000);
}

static void gsl_clear_reg(struct i2c_client *client)
{
	u8 buf[4] = { 0 };
	/* clear reg */
	buf[0] = 0x88;
	gsl_write_interface(client, 0xe0, buf, 4);
	usleep_range(20000, 30000);
	buf[0] = 0x3;
	gsl_write_interface(client, 0x80, buf, 4);
	usleep_range(5000, 10000);
	buf[0] = 0x4;
	gsl_write_interface(client, 0xe4, buf, 4);
	usleep_range(5000, 10000);
	buf[0] = 0x0;
	gsl_write_interface(client, 0xe0, buf, 4);
	usleep_range(20000, 30000);

}

static int gsl_compatible_id(struct i2c_client *client)
{
	u8 buf[4];
	int i, err;
	for (i = 0; i < 5; i++) {
		err = gsl_read_interface(client, 0xfc, buf, 4);
		pr_info("[tp-gsl] 0xfc = {0x%02x%02x%02x%02x}\n",
		buf[3], buf[2], buf[1], buf[0]);
		if (err < 0) {
			if (4 == i)
				break;
			else
				continue;
		} else {
			err = 0;
			break;
		}
	}
	return err;
}

static void gslx68x_touch_reset(void)
{
	if (gpio_request(ddata->pdata->reset, "gslx68x_reset")) {
		pr_err("Failed to request GPIO for gslx68x_reset pin!\n");
		return;
	}

	usleep_range(5000, 10000);
	gpio_direction_output(ddata->pdata->reset, 0);
	usleep_range(20000, 30000);
	gpio_direction_output(ddata->pdata->reset, 1);
	usleep_range(20000, 30000);

	gpio_free(ddata->pdata->reset);
	return;
}

static int gslx68x_touch_io_power_onoff(struct device *dev, int on)
{
	/* static struct regulator *v_ldo; */
	int ret;
	if (!ddata->power_ldo) {
		pr_info("%s: power_ldo is null, start to regulator_get.\n",
			__func__);
		ddata->power_ldo = regulator_get(dev, "avdd");
		if (IS_ERR(ddata->power_ldo)) {
			ddata->power_ldo = NULL;
			pr_err("%s: enable v_ramp for touch fail!\n",
				 __func__);
			return -EIO;
		}
	}

	if (on) {
		regulator_set_voltage(ddata->power_ldo, 2800000, 2800000);
		ret = regulator_enable(ddata->power_ldo);
		pr_info("[tp-gslx68x] %s: enable ldo10\n", __func__);
	} else {
		ret = regulator_disable(ddata->power_ldo);
		pr_info("[tp-gslx68x] %s: disable ldo10\n", __func__);
	}

	return ret;
}

static int gslx68x_touch_parse_dt(struct device_node *np,
				  struct device *dev, struct gsl_ts_data *touch)
{
	int ret;
	/* enum of_gpio_flags flags; */

	ret = of_property_read_u32(np, "gslx68x,abs-x-max",
		&touch->pdata->abs_x_max);
	if (ret)
		return ret;
	ret = of_property_read_u32(np, "gslx68x,abs-y-max",
		&touch->pdata->abs_y_max);
	if (ret)
		return ret;
	ret = of_property_read_u32(np, "gslx68x,gsl-type",
		&touch->pdata->gsl_type);
	if (ret)
		return ret;

	touch->power_ldo = regulator_get(dev, "avdd");

	if (IS_ERR(touch->power_ldo)) {
		pr_info("[tp-gslx68x] touch->power_ldo IS_ERR!\n");
		touch->power_ldo = NULL;
	}

	touch->pdata->reset = of_get_named_gpio(np, "reset-gpio", 0);

	if (gpio_is_valid(touch->pdata->reset)) {
		/*comment temporary, will check further */
		/*
		ret = gpio_request(ddata->pdata->reset, "gslx68x_reset");
		if(ret){
		pr_err("Failed to request GPIO for gslx68x_reset pin!\n");
			return ret;
		}
		*/

		/* need check further */
		/*
		if (!(flags & OF_GPIO_ACTIVE_LOW))
			touch->reset_gpio_invert = 1;
		*/
	}
	return 0;
}

static void gsl_hw_init(void)
{
	/* int error; */
	/* add power */
	if (ddata->power_ldo && !(ddata->power_on)) {
		gslx68x_touch_io_power_onoff(&ddata->client->dev, 1);
		ddata->power_on = 1;
	}
/*
	usleep_range(30000, 40000);;
	error = gpio_request(GSL_IRQ_GPIO_NUM, GSL_IRQ_NAME);
	if (error < 0) {
		printk("Failed to request GPIO %d, error %d\n",
			GSL_IRQ_GPIO_NUM, error);
		return error;
	}

	error = gpio_direction_input(GSL_IRQ_GPIO_NUM);
	if (error < 0) {
		printk("Failed to configure direction for GPIO %d, error %d\n",
			GSL_IRQ_GPIO_NUM,  error);
	}
*/
	/* reset */
	if (ddata->pdata->reset)
		gslx68x_touch_reset();
}

static void gsl_sw_init(struct i2c_client *client)
{
	if (1 == ddata->gsl_sw_flag)
		return;
	ddata->gsl_sw_flag = 1;

	if (ddata->pdata->reset)
		gslx68x_touch_reset();

	gsl_clear_reg(client);
	gsl_reset_core(client);

	gsl_load_fw(client, gsl_cfg_table[gsl_cfg_index].fw,
		    gsl_cfg_table[gsl_cfg_index].fw_size);

	gsl_start_core(client);

	ddata->gsl_sw_flag = 0;
}

static void check_mem_data(struct i2c_client *client)
{

	u8 read_buf[4] = { 0 };
	usleep_range(30000, 40000);
	gsl_read_interface(client, 0xb0, read_buf, 4);
	if (read_buf[3] != 0x5a || read_buf[2] != 0x5a
	    || read_buf[1] != 0x5a || read_buf[0] != 0x5a) {
		pr_info("[tp-gslx68x]0xb4 ={0x%02x%02x%02x%02x}\n",
			read_buf[3], read_buf[2], read_buf[1], read_buf[0]);
		gsl_sw_init(client);
	}
}

#ifdef TPD_PROC_DEBUG
static int char_to_int(char ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	else
		return ch - 'a' + 10;
}

static int gsl_config_read_proc(char *page, char **start, off_t off, int count,
				int *eof, void *data)
{
	char *ptr = page;
	char temp_data[5] = { 0 };
	unsigned int tmp = 0;
	if ('v' == gsl_read[0] && 's' == gsl_read[1]) {
#ifdef GSL_ALG_ID
		tmp = gsl_version_id();
#else
		tmp = 0x20121215;
#endif
		ptr += sprintf(ptr, "version:%x\n", tmp);
	} else if ('r' == gsl_read[0] && 'e' == gsl_read[1]) {
		if ('i' == gsl_read[3]) {
#ifdef GSL_ALG_ID
			tmp = (gsl_data_proc[5] << 8) | gsl_data_proc[4];
			ptr += sprintf(ptr, "gsl_config_data_id[%d] = ", tmp);
			if (tmp >= 0
			    && tmp < gsl_cfg_table[gsl_cfg_index].data_size)
				ptr +=
				    sprintf(ptr, "%d\n",
					    gsl_cfg_table[gsl_cfg_index].
					    data_id[tmp]);
#endif
		} else {
			gsl_write_interface(ddata->client, 0xf0,
					    &gsl_data_proc[4], 4);
			gsl_read_interface(ddata->client, gsl_data_proc[0],
					   temp_data, 4);
			ptr +=
			    sprintf(ptr, "offset : {0x%02x,0x",
				    gsl_data_proc[0]);
			ptr += sprintf(ptr, "%02x", temp_data[3]);
			ptr += sprintf(ptr, "%02x", temp_data[2]);
			ptr += sprintf(ptr, "%02x", temp_data[1]);
			ptr += sprintf(ptr, "%02x};\n", temp_data[0]);
		}
	}
	*eof = 1;
	return ptr - page;
}

static int gsl_config_write_proc(struct file *file, const char *buffer,
				 unsigned long count, void *data)
{
	u8 buf[8] = { 0 };
	int tmp = 0;
	int tmp1 = 0;
	print_info("[tp-gsl][%s]\n", __func__);

	if (count > CONFIG_LEN) {
		print_info("size not match [%d:%ld]\n", CONFIG_LEN, count);
		return -EFAULT;
	}

	if (copy_from_user
	    (gsl_read, buffer, (count < CONFIG_LEN ? count : CONFIG_LEN))) {
		print_info("copy from user fail\n");
		return -EFAULT;
	}
	print_info("[tp-gsl][%s][%s]\n", __func__, gsl_read);

	buf[3] = char_to_int(gsl_read[14]) << 4 | char_to_int(gsl_read[15]);
	buf[2] = char_to_int(gsl_read[16]) << 4 | char_to_int(gsl_read[17]);
	buf[1] = char_to_int(gsl_read[18]) << 4 | char_to_int(gsl_read[19]);
	buf[0] = char_to_int(gsl_read[20]) << 4 | char_to_int(gsl_read[21]);

	buf[7] = char_to_int(gsl_read[5]) << 4 | char_to_int(gsl_read[6]);
	buf[6] = char_to_int(gsl_read[7]) << 4 | char_to_int(gsl_read[8]);
	buf[5] = char_to_int(gsl_read[9]) << 4 | char_to_int(gsl_read[10]);
	buf[4] = char_to_int(gsl_read[11]) << 4 | char_to_int(gsl_read[12]);
	if ('v' == gsl_read[0] && 's' == gsl_read[1]) { /* version vs */
		pr_info("gsl version\n");
	} else if ('s' == gsl_read[0] && 't' == gsl_read[1]) { /* start st */
		gsl_proc_flag = 1;
		gsl_reset_core(ddata->client);
	} else if ('e' == gsl_read[0] && 'n' == gsl_read[1]) { /* end en */
		usleep_range(20000, 30000);
		gsl_reset_core(ddata->client);
		gsl_start_core(ddata->client);
		gsl_proc_flag = 0;
	} else if ('r' == gsl_read[0] && 'e' == gsl_read[1]) { /* read buf */
		memcpy(gsl_data_proc, buf, 8);
	} else if ('w' == gsl_read[0] && 'r' == gsl_read[1]) { /* write buf */
		gsl_write_interface(ddata->client, buf[4], buf, 4);
	}
#ifdef GSL_ALG_ID
	/* write id config */
	else if ('i' == gsl_read[0] && 'd' == gsl_read[1]) {
		tmp1 = (buf[7] << 24) | (buf[6] << 16)
			| (buf[5] << 8) | buf[4];
		tmp = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
		if (tmp1 >= 0 && tmp1 < gsl_cfg_table[gsl_cfg_index].data_size)
			gsl_cfg_table[gsl_cfg_index].data_id[tmp1] = tmp;
	}
#endif
	return count;
}
#endif

#ifdef GSL_TIMER
static void gsl_timer_check_func(struct work_struct *work)
{
	u8 buf[4] = { 0 };
	u32 tmp;
	static int timer_count;
	if (1 == ddata->gsl_halt_flag)
		return;

	gsl_read_interface(ddata->client, 0xb4, buf, 4);
	tmp = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | (buf[0]);
	print_info("[pre] 0xb4 = %x\n", ddata->gsl_timer_data);
	print_info("[cur] 0xb4 = %x\n", tmp);
	print_info("gsl_timer_flag=%d\n", ddata->gsl_timer_flag);
	if (0 == ddata->gsl_timer_flag) {
		if (tmp == ddata->gsl_timer_data) {
			ddata->gsl_timer_flag = 1;
			if (0 == ddata->gsl_halt_flag) {
				queue_delayed_work(ddata->timer_wq,
						   &ddata->timer_work, 25);
			}
		} else {
			ddata->gsl_timer_flag = 0;
			timer_count = 0;
			if (0 == ddata->gsl_halt_flag) {
				queue_delayed_work(ddata->timer_wq,
						   &ddata->timer_work,
						   GSL_TIMER_CHECK_CIRCLE);
			}
		}
	} else if (1 == ddata->gsl_timer_flag) {
		if (tmp == ddata->gsl_timer_data) {
			if (0 == ddata->gsl_halt_flag) {
				timer_count++;
				ddata->gsl_timer_flag = 2;
				gsl_sw_init(ddata->client);
				ddata->gsl_timer_flag = 1;
			}
			if (0 == ddata->gsl_halt_flag && timer_count < 20) {
				queue_delayed_work(ddata->timer_wq,
						   &ddata->timer_work,
						   GSL_TIMER_CHECK_CIRCLE);
			}
		} else {
			timer_count = 0;
			if (0 == ddata->gsl_halt_flag && timer_count < 20) {
				queue_delayed_work(ddata->timer_wq,
						   &ddata->timer_work,
						   GSL_TIMER_CHECK_CIRCLE);
			}
		}
		ddata->gsl_timer_flag = 0;
	}
	ddata->gsl_timer_data = tmp;
}
#endif

#if GSL_HAVE_TOUCH_KEY
static int gsl_report_key(struct input_dev *idev, int x, int y)
{
	int i;
	for (i = 0; i < GSL_KEY_NUM; i++) {
		if (x > gsl_key_data[i].x_min &&
		    x < gsl_key_data[i].x_max &&
		    y > gsl_key_data[i].y_min && y < gsl_key_data[i].y_max) {
			ddata->gsl_key_state = i + 1;
			input_report_key(idev, gsl_key_data[i].key, 1);
			input_sync(idev);
			return 1;
		}
	}
	return 0;
}
#endif

void gslx68x_irq_disable(void)
{
	unsigned long irqflags;

	ddata->irq_num_d++;
	spin_lock_irqsave(&ddata->irq_lock, irqflags);
	if (!ddata->irq_is_disable) {
		disable_irq_nosync(ddata->client->irq);
		ddata->irq_is_disable = 1;
	}
	spin_unlock_irqrestore(&ddata->irq_lock, irqflags);
}

void gslx68x_irq_enable(void)
{
	unsigned long irqflags = 0;

	ddata->irq_num_e++;
	spin_lock_irqsave(&ddata->irq_lock, irqflags);
	if (ddata->irq_is_disable) {
		enable_irq(ddata->client->irq);
		ddata->irq_is_disable = 0;
	}
	spin_unlock_irqrestore(&ddata->irq_lock, irqflags);
}

#ifdef TP_PROXIMITY_SENSOR
static int tp_face_get_mode(void)
{
	tp_sensor_print(">>>> PROXIMITY_SWITCH is %d <<<<\n",
		PROXIMITY_SWITCH);
	return PROXIMITY_SWITCH;
}

static int tp_face_mode_state(void)
{
	tp_sensor_print(">>>> PROXIMITY_STATE  is %d <<<<\n", PROXIMITY_STATE);
	return PROXIMITY_STATE;
}

static void tp_enable_ps(int enable)
{
	u8 buf[4] = { 0 };
	gslx68x_irq_disable();
	/* enable == 1 */
	if (enable) {
		/* current is state turn off screen automatically */
		INCALLING = 1;
		/* if it has power supply, need swtich TP state to enable
		function of turn off screen automatically */
		if (TP_POWER == 1) {
			PROXIMITY_SWITCH = 1;
			buf[3] = 0x00;
			buf[2] = 0x00;
			buf[1] = 0x00;
			buf[0] = 0x4;
			gsl_write_interface(ddata->client, 0xf0, buf, 4);
			buf[3] = 0x0;
			buf[2] = 0x0;
			buf[1] = 0x0;
			buf[0] = 0x3;
			gsl_write_interface(ddata->client, 0, buf, 4);
		}
	} else {/* enable == 0 */
		INCALLING = 0;
		if (TP_POWER == 1) {
			PROXIMITY_SWITCH = 0;
			buf[3] = 0x00;
			buf[2] = 0x00;
			buf[1] = 0x00;
			buf[0] = 0x4;
			gsl_write_interface(ddata->client, 0xf0, buf, 4);
			buf[3] = 0x0;
			buf[2] = 0x0;
			buf[1] = 0x0;
			buf[0] = 0x0;
			gsl_write_interface(ddata->client, 0, buf, 4);
		}
	}
	input_report_abs(ddata->idev_ps, ABS_DISTANCE, 1);
	input_sync(ddata->idev_ps);
	gslx68x_irq_enable();
	pr_info("store = %d, SWITCH = %d, INCALLING = %d, TP_POWER = %d\n",
		enable, PROXIMITY_SWITCH, INCALLING, TP_POWER);
}

static ssize_t tp_face_mode_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int proximity_switch, proximity_state;

	proximity_switch = tp_face_get_mode();
	proximity_state = tp_face_mode_state();

	tp_sensor_print("proximity_switch = %d, proximity_state = %d",
			proximity_switch, proximity_state);
	return sprintf(buf, "(%d) : (%d)\n", proximity_switch, proximity_state);
}

static ssize_t tp_face_mode_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	unsigned long val, enable;
	int ret;

	ret = kstrtoul(buf, 10, &val);

	if (!ret)
		enable = val;
	else
		return ret;
	pr_info("store = %lx\n", enable);

	if ((enable == 0) || (enable == 1))
		tp_enable_ps(enable);
	else
		return -EINVAL;

	return count;
}

static struct device_attribute tp_face_mode_attr = {
	.attr = {
		 .name = "active",
		 .mode = S_IRUGO | S_IWUGO,
		 },
	/* For read the proximity state by upper level; */
	.show = &tp_face_mode_show,
	/* For open/close the proximity switch by upper level; */
	.store = &tp_face_mode_store,
};

static struct attribute *gsl_ps_attributes[] = {
	&tp_face_mode_attr.attr,
	NULL
};

static struct attribute_group gsl_ps_attribute_group = {
	.attrs = gsl_ps_attributes
};

#endif

static void gsl_report_point(struct input_dev *idev,
			     struct gsl_touch_info *cinfo)
{
	int i;
	u32 gsl_point_state = 0;
	u32 temp = 0;
	static int pre_touch = 0x0;
	/* int temp_data; */
	for (i = 0; i < cinfo->finger_num; i++) {
		print_info("[gslx68-tp] x = %d y = %d  id = %d\n",
			cinfo->x[i], cinfo->y[i], cinfo->id[i]);
		/* temp_data = cinfo->x[i];
		* cinfo->x[i] = cinfo->y[i];
		* cinfo->y[i] = 600 - temp_data; */
		gsl_point_state |= (0x1 << (cinfo->id[i] - 1));
#ifdef GSL_REPORT_POINT_SLOT
		input_mt_slot(idev, cinfo->id[i] - 1);
		input_report_abs(idev, ABS_MT_TRACKING_ID, cinfo->id[i] - 1);
		input_report_abs(idev, ABS_MT_POSITION_X, cinfo->x[i]);

		if (gsl_cfg_index == GSL_TYPE_REVERSE_Y)
			input_report_abs(idev, ABS_MT_POSITION_Y,
				ddata->pdata->abs_y_max - cinfo->y[i]);
		else
			input_report_abs(idev, ABS_MT_POSITION_Y, cinfo->y[i]);

		input_report_abs(idev, ABS_MT_WIDTH_MAJOR, 1);
#else
		input_report_key(idev, BTN_TOUCH, 1);
		input_report_abs(idev, ABS_MT_TRACKING_ID, cinfo->id[i]);
		input_report_abs(idev, ABS_MT_TOUCH_MAJOR, GSL_PRESSURE);
		input_report_abs(idev, ABS_MT_POSITION_X, cinfo->x[i]);

		if (gsl_cfg_index == GSL_TYPE_REVERSE_Y)
			input_report_abs(idev, ABS_MT_POSITION_Y,
				ddata->pdata->abs_y_max - cinfo->y[i]);
		else
			input_report_abs(idev, ABS_MT_POSITION_Y, cinfo->y[i]);

		input_report_abs(idev, ABS_MT_WIDTH_MAJOR, 1);
		input_mt_sync(idev);
#endif
	}
#ifdef GSL_REPORT_POINT_SLOT
	temp = (pre_touch & gsl_point_state) ^ pre_touch;
	for (i = 0; i < 5; i++) {
		if (cinfo->finger_num == 0 || (temp & (1 << i))) {
			input_mt_slot(idev, i);
			input_report_abs(idev, ABS_MT_TRACKING_ID, -1);
		}
	}
#else
	if (cinfo->finger_num == 0) {
		input_report_abs(idev, ABS_MT_TOUCH_MAJOR, 0);
		input_report_abs(idev, ABS_MT_WIDTH_MAJOR, 0);
		input_mt_sync(idev);
	}
#endif
	pre_touch = gsl_point_state;
/*
	for(i = 0; i < 5; i++){
		if(gsl_point_state & (1<<i)){
#ifdef GSL_REPORT_POINT_SLOT
			input_mt_slot(idev, cinfo->id[i] - 1);
			input_report_abs(idev, ABS_MT_TRACKING_ID,
				cinfo->id[i] - 1);
			input_report_abs(idev, ABS_MT_POSITION_X,
				cinfo->x[i]);
			input_report_abs(idev, ABS_MT_POSITION_Y,
				cinfo->y[i]);
			input_report_abs(idev, ABS_MT_WIDTH_MAJOR, 1);
#else
			input_report_key(idev, BTN_TOUCH, 1);
			input_report_abs(idev, ABS_MT_TRACKING_ID,
				cinfo->id[i]);
			input_report_abs(idev, ABS_MT_TOUCH_MAJOR,
				GSL_PRESSURE);
			input_report_abs(idev, ABS_MT_POSITION_X,
				cinfo->x[i]);
			input_report_abs(idev, ABS_MT_POSITION_Y,
				cinfo->y[i]);
			input_report_abs(idev, ABS_MT_WIDTH_MAJOR, 1);
			input_mt_sync(idev);
#endif
			pre_touch |= (1<<i);
		}else if(pre_touch & (1<<i)){
			pre_touch &= ~(1<<i);
#ifdef	GSL_REPORT_POINT_SLOT
			input_mt_slot(idev, i);
			input_report_abs(idev, ABS_MT_TRACKING_ID, -1);
#else
			input_report_abs(idev, ABS_MT_TOUCH_MAJOR, 0);
			input_report_abs(idev, ABS_MT_WIDTH_MAJOR, 0);
			input_mt_sync(idev);
#endif
		}
	}
*/
	input_sync(idev);
}

static void gsl_report_work(struct work_struct *work)
{
	int rc, tmp;
	u8 buf[44] = { 0 };
	int tmp1 = 0;
	struct gsl_touch_info *cinfo = ddata->cinfo;
	struct i2c_client *client = ddata->client;
	struct input_dev *idev = ddata->idev;

	if (1 == ddata->gsl_sw_flag)
		goto schedule;

#ifdef TP_PROXIMITY_SENSOR
	/* tp_sensor_print("PROXIMITY_SWITCH = %d\n", PROXIMITY_SWITCH); */
	if (!!PROXIMITY_SWITCH == 1) {
		gsl_read_interface(ddata->client, 0xac, buf, 4);
		/* tp_sensor_print("0xac = %02x,%02x,%02x,%02x\n",
		 * buf[3],buf[2],buf[1],buf[0]); */

		if (buf[3] == 0x0 && buf[2] == 0x0 && buf[1] == 0x0
		    && buf[0] == 0x1) {
			input_report_abs(ddata->idev_ps, ABS_DISTANCE, 0);
			PROXIMITY_STATE = 1;
			input_sync(ddata->idev_ps);
			tp_sensor_print("SWITCH = %d, STATE = %d\n",
					PROXIMITY_SWITCH, PROXIMITY_STATE);
			goto schedule;
		} else {
			input_report_abs(ddata->idev_ps, ABS_DISTANCE, 1);
			input_sync(ddata->idev_ps);
			PROXIMITY_STATE = 0;
			tp_sensor_print("SWITCH = %d, STATE = %d\n",
					PROXIMITY_SWITCH, PROXIMITY_STATE);
		}
	}
#endif

#ifdef TPD_PROC_DEBUG
	if (gsl_proc_flag == 1)
		goto schedule;
#endif
#ifdef GSL_TIMER
	if (2 == ddata->gsl_timer_flag)
		goto schedule;
#endif

	/* read data from DATA_REG */
	rc = gsl_read_interface(client, 0x80, buf, 44);
	if (rc < 0) {
		dev_err(&client->dev, "[gsl] I2C read failed\n");
		goto schedule;
	}

	if (buf[0] == 0xff)
		goto schedule;

	cinfo->finger_num = buf[0];
	for (tmp = 0; tmp < (cinfo->finger_num > 10 ? 10 : cinfo->finger_num);
	     tmp++) {
		cinfo->y[tmp] = (buf[tmp * 4 + 4] | ((buf[tmp * 4 + 5]) << 8));
		cinfo->x[tmp] =
		    (buf[tmp * 4 + 6] | ((buf[tmp * 4 + 7] & 0x0f) << 8));
		cinfo->id[tmp] = buf[tmp * 4 + 7] >> 4;
		/* print_info("tp-gsl    x = %d y = %d\n",
		 * cinfo->x[tmp],cinfo->y[tmp]); */
		/* print_info("tp-gsl    id = %d\n",cinfo->id[tmp]); */
	}

	print_info("111 finger_num= %d\n", cinfo->finger_num);
#ifdef GSL_ALG_ID
	cinfo->finger_num =
	    (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | (buf[0]);
	gsl_alg_id_main(cinfo);
	tmp1 = gsl_mask_tiaoping();
	/* print_info("[tp-gsl] tmp1=%x\n",tmp1); */
	if (tmp1 > 0 && tmp1 < 0xffffffff) {
		buf[0] = 0xa;
		buf[1] = 0;
		buf[2] = 0;
		buf[3] = 0;
		gsl_write_interface(client, 0xf0, buf, 4);
		buf[0] = (u8) (tmp1 & 0xff);
		buf[1] = (u8) ((tmp1 >> 8) & 0xff);
		buf[2] = (u8) ((tmp1 >> 16) & 0xff);
		buf[3] = (u8) ((tmp1 >> 24) & 0xff);
		/* print_info("tmp1=%08x,buf[0]=%02x,buf[1]=%02x,buf[2]=%02x,b
		 * uf[3]=%02x\n",tmp1,buf[0],buf[1],buf[2],buf[3]); */
		gsl_write_interface(client, 0x8, buf, 4);
	}
#endif

	print_info("222 finger_num= %d\n", cinfo->finger_num);
	gsl_report_point(idev, cinfo);

schedule:
	gslx68x_irq_enable();
}

static int gsl_request_input_dev(struct gsl_ts_data *ddata)
{
	struct input_dev *input_dev;
	int err;
#if GSL_HAVE_TOUCH_KEY
	int i;
#endif
	/*allocate input device */
	print_info("==input_allocate_device=\n");
	input_dev = input_allocate_device();
	if (!input_dev) {
		err = -ENOMEM;
		goto err_allocate_input_device_fail;
	}
	ddata->idev = input_dev;
	ddata->idev->name = "gslx68x-ts";
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &ddata->client->dev;
	/*set input parameter */
	__set_bit(EV_KEY, input_dev->evbit);
	__set_bit(EV_ABS, input_dev->evbit);
	__set_bit(EV_SYN, input_dev->evbit);
#ifdef GSL_REPORT_POINT_SLOT
	/* __set_bit(EV_REP, input_dev->evbit); */
	__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	input_mt_init_slots(input_dev, 5, 0); /* need check further */
#else
	__set_bit(BTN_TOUCH, input_dev->keybit);
	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, 5, 0, 0);
#endif
	__set_bit(ABS_MT_TOUCH_MAJOR, input_dev->absbit);
	__set_bit(ABS_MT_POSITION_X, input_dev->absbit);
	__set_bit(ABS_MT_POSITION_Y, input_dev->absbit);
	__set_bit(ABS_MT_WIDTH_MAJOR, input_dev->absbit);

#if GSL_HAVE_TOUCH_KEY
	for (i = 0; i < GSL_KEY_NUM; i++)
		__set_bit(gsl_key_data[i].key, input_dev->keybit);
#endif

	input_set_abs_params(input_dev,
			     ABS_MT_POSITION_X, 0, ddata->pdata->abs_x_max, 0,
			     0);
	input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0,
			     ddata->pdata->abs_y_max, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(input_dev, ABS_MT_TRACKING_ID, 0, 255, 0, 0);
	input_dev->name = "gslx68x-ts";	/* dev_name(&client->dev) */

	/*register input device */
	err = input_register_device(input_dev);
	if (err)
		goto err_register_input_device_fail;
#ifdef TP_PROXIMITY_SENSOR
	ddata->idev_ps = input_allocate_device();
	ddata->idev_ps->name = SENSOR_NAME_PROXIMITY;
	ddata->idev_ps->id.bustype = BUS_I2C;
	input_set_capability(ddata->idev_ps, EV_ABS, ABS_MISC);
	__set_bit(EV_ABS, ddata->idev_ps->evbit);
	__set_bit(ABS_DISTANCE, ddata->idev_ps->absbit);
	input_set_abs_params(ddata->idev_ps, ABS_DISTANCE, 0, 1, 0, 0);
	err = input_register_device(ddata->idev_ps);
	if (err)
		goto err_register_input_device_fail;
	err = sysfs_create_group(&ddata->idev_ps->dev.kobj,
		&gsl_ps_attribute_group);
	if (err)
		goto err_register_input_device_fail;
	TP_POWER = 1;
#endif
	return 0;
err_register_input_device_fail:
	input_free_device(input_dev);
err_allocate_input_device_fail:
	return err;
}

static irqreturn_t gsl_ts_interrupt(int irq, void *dev_id)
{
	print_info("gslX68X_ts_interrupt");

	gslx68x_irq_disable();
	if (!work_pending(&ddata->work))
		queue_work(ddata->wq, &ddata->work);

	return IRQ_HANDLED;
}

#ifdef GSL_IDT_TP

#define GSL_C		100
#define GSL_TP_1	0xff800000	/* no pin NC */
#define GSL_TP_2	0xff800001	/* 17pin NC */
#define GSL_TP_3	0xff800002	/* 18pin NC */
#define GSL_TP_4	0xff800004	/* 19pin NC */

static unsigned int gsl_count_one(unsigned int flag) /* check count of bit */
{
	unsigned int tmp = 0;
	int i = 0;
	for (i = 0; i < 32; i++) {
		if (flag & (0x1 << i))
			tmp++;
	}
	return tmp;
}

static int gsl_identify_tp(struct i2c_client *client)
{
	u8 buf[4];
	int i, err = 1;
	int flag = 0;
	unsigned int tmp, tmp0;
	unsigned int tmp1, tmp2, tmp3, tmp4;
	u32 num;
identify_tp_repeat:
	/* clr_reg(client); */
	gsl_clear_reg(client);
	gsl_reset_core(client);

	/* can read parameter configration correctly */
	num = ARRAY_SIZE(GSLX68X_FW_TEST);
	gsl_load_fw(client, GSLX68X_FW_TEST, num);

	gsl_start_core(client);
	usleep_range(90000, 100000);
	gsl_read_interface(client, 0xb4, buf, 4);
	tp_idt_print("the test 0xb4 = {0x%02x%02x%02x%02x}\n", buf[3], buf[2],
		     buf[1], buf[0]);
	if (((buf[3] << 8) | buf[2]) > 1) {
		tp_idt_print("TP-GSL is start ok\n");
		gsl_read_interface(client, 0xb8, buf, 4);
		tmp = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
		tp_idt_print("the test 0xb8 = {0x%02x%02x%02x%02x}\n", buf[3],
			     buf[2], buf[1], buf[0]);

		/* handle error and compatibility */
		tmp1 = gsl_count_one(GSL_TP_1 ^ tmp);
		tmp0 = gsl_count_one((tmp & GSL_TP_1) ^ GSL_TP_1);
		tmp1 += tmp0 * GSL_C;

		tmp2 = gsl_count_one(GSL_TP_2 ^ tmp);
		tmp0 = gsl_count_one((tmp & GSL_TP_2) ^ GSL_TP_2);
		tmp2 += tmp0 * GSL_C;

		tmp3 = gsl_count_one(GSL_TP_3 ^ tmp);
		tmp0 = gsl_count_one((tmp & GSL_TP_3) ^ GSL_TP_3);
		tmp3 += tmp0 * GSL_C;

		tmp4 = gsl_count_one(GSL_TP_4 ^ tmp);
		tmp0 = gsl_count_one((tmp & GSL_TP_4) ^ GSL_TP_4);
		tmp4 += tmp0 * GSL_C;
		tp_idt_print("tmp1 = %d, tmp2 = %d, tmp3 = %d, tmp4 = %d\n",
			     tmp1, tmp2, tmp3, tmp4);

		tmp = tmp1;	/* get mininum */
		if (tmp1 > tmp2)
			tmp = tmp2;
		if (tmp > tmp3)
			tmp = tmp3;
		if (tmp > tmp4)
			tmp = tmp4;

		tp_idt_print("tmp = %x\n", tmp);

		if (tmp == tmp1) {/* NO NC */
			if (((ddata->pdata->gsl_type) & 0xF) == 1)
				gsl_cfg_index = GSL_TYPE_REVERSE_Y;
			else
				gsl_cfg_index = GSL_TYPE_DEFAULT;
		} else if (tmp == tmp2) {/* 17pin NC */
			gsl_cfg_index = GSL_TYPE_DEFAULT;
		} else if (tmp == tmp3) {/* 18pin NC */
			gsl_cfg_index = GSL_TYPE_DEFAULT;
		} else if (tmp == tmp4) {/* 19pin NC */
			if ((((ddata->pdata->gsl_type) >> 4) & 0xF) == 1)
				gsl_cfg_index = GSL_TYPE_BXC;
			else
				gsl_cfg_index = GSL_TYPE_DZH;
		} else {	/* impossible */
			pr_info("tp-gslx68x][%s], error, use default config\n",
				__func__);
			gsl_cfg_index = GSL_TYPE_DEFAULT;
			return 0;
		}
		pr_info("[tp-gslx68x][%s], use %d config\n", __func__,
		       gsl_cfg_index);
		err = 1;
	} else {
		flag++;
		if (flag < 3)
			goto identify_tp_repeat;
		pr_info("tp-gslx68x][%s], error, use default config\n",
		       __func__);
		gsl_cfg_index = GSL_TYPE_DEFAULT;
		err = 0;
	}
	return err;
}

static const struct file_operations gsl_ts_proc_fops = {
	.read = gsl_config_read_proc,
	.write = gsl_config_write_proc,
};

#endif
/*
struct gslx68x_virtual_key {
	u32 name_val;
	char *x;
	char *y;
	char *width;
	char *height;
};

#define GSLX68X_VKEY_INIT(_name_val, _x, _y, _width, _height)	\
{						\
	.name_val		= _name_val,	\
	.x			= _x,		\
	.y			= _y,		\
	.width			= _width,	\
	.height			= _height,	\
}

static struct gslx68x_virtual_key gslx68x_Vkey[] = {
	[0] = GSLX68X_VKEY_INIT(KEY_MENU, "67", "1010", "135", "100"),
	[1] = GSLX68X_VKEY_INIT(KEY_HOMEPAGE, "202", "1010", "135", "100"),
	[2] = GSLX68X_VKEY_INIT(KEY_BACK, "338", "1010", "135", "100"),
	[3] = GSLX68X_VKEY_INIT(KEY_SEARCH, "472", "1010", "135", "100"),
};

#define GSLX68X_VKEY_SET(a) (gslx68x_Vkey[a].name_val, gslx68x_Vkey[a].x, \
	gslx68x_Vkey[a].y, gslx68x_Vkey[a].width, gslx68x_Vkey[a].height)

#define VIRT_KEYS(x...)  __stringify(x)

static ssize_t gslx68x_virtual_keys_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return sprintf(buf,
		VIRT_KEYS(EV_KEY) ":%d:%s:%s:%s:%s" ":"
		VIRT_KEYS(EV_KEY) ":%d:%s:%s:%s:%s" ":"
		VIRT_KEYS(EV_KEY) ":%d:%s:%s:%s:%s" ":"
		VIRT_KEYS(EV_KEY) ":%d:%s:%s:%s:%s\n",
		GSLX68X_VKEY_SET(0),
		GSLX68X_VKEY_SET(1),
		GSLX68X_VKEY_SET(2),
		GSLX68X_VKEY_SET(3));
}

static struct kobj_attribute gslx68x_virtual_keys_attr = {
	.attr = {
		 .name = "virtualkeys.gslx68x-ts",
		 .mode = S_IRUGO,
		 },
	.show = &gslx68x_virtual_keys_show,
};

static struct attribute *gslx68x_props_attrs[] = {
	&gslx68x_virtual_keys_attr.attr,
	NULL
};

static struct attribute_group gslx68x_props_attr_group = {
	.attrs = gslx68x_props_attrs,
};

static int gslx68x_set_virtual_key(struct input_dev *input_dev)
{
	struct kobject *props_kobj;
	int ret = 0;

	props_kobj = kobject_create_and_add("board_properties", NULL);
	if (props_kobj)
		ret = sysfs_create_group(props_kobj, &gslx68x_props_attr_group);
	if (!props_kobj || ret)
		pr_err("failed to create board_properties!\n");

	return 0;
}
*/

static int gsl_ts_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int err = 0;
	pr_info("[tp-gslx68x] enter %s\n", __func__);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		err = -ENODEV;
		pr_info("[tp-gslx68x] i2c_check_func has err: %d\n", err);
		goto exit_check_functionality_failed;
	}

	ddata = kzalloc(sizeof(struct gsl_ts_data), GFP_KERNEL);
	if (ddata == NULL) {
		err = -ENOMEM;
		goto exit_alloc_data_failed;
	}

	ddata->power_on = 0;
	ddata->gsl_halt_flag = 0;
	ddata->gsl_sw_flag = 0;
	ddata->gsl_up_flag = 0;
	ddata->gsl_point_state = 0;
#if GSL_HAVE_TOUCH_KEY
	ddata->gsl_key_state = 0;
#endif

	ddata->cinfo = kzalloc(sizeof(struct gsl_touch_info), GFP_KERNEL);
	if (ddata->cinfo == NULL) {
		err = -ENOMEM;
		goto exit_alloc_cinfo_failed;
	}

	if (client->dev.platform_data) {
		ddata->pdata = client->dev.platform_data;
		need_free_pdata = false;
	} else {
		ddata->pdata =
		    kzalloc(sizeof(struct gsl_ts_platform_data), GFP_KERNEL);
		if (ddata->pdata == NULL) {
			err = -ENOMEM;
			goto exit_alloc_cinfo_failed;
		} else
			need_free_pdata = true;
	}

	ddata->client = client;

	err = gslx68x_touch_parse_dt(client->dev.of_node, &client->dev, ddata);
	if (err) {
		dev_err(&client->dev, "failed to parse dt, error %d\n", err);
		return err;
	}

	i2c_set_clientdata(client, ddata);
	print_info("I2C addr=%x", client->addr);

#ifdef GSL_IDT_TP
	gsl_identify_tp(client);
#endif

	gsl_hw_init();
	err = gsl_compatible_id(client);
	if (err < 0) {
		pr_info("[tp-gslx68x] gsl_compatible_id has err: %d\n", err);
		goto exit_i2c_transfer_fail;
	}
	/*request input system */
	err = gsl_request_input_dev(ddata);
	if (err < 0)
		goto exit_i2c_transfer_fail;

	/*init work queue */
	INIT_WORK(&ddata->work, gsl_report_work);
	ddata->wq = create_singlethread_workqueue(dev_name(&client->dev));
	if (!ddata->wq) {
		err = -ESRCH;
		goto exit_create_singlethread;
	}
	if (ddata->pdata->set_virtual_key)
		err = ddata->pdata->set_virtual_key(ddata->idev);
	BUG_ON(err != 0);
	/*request irq */
	/* client->irq = GSL_IRQ_NUM; */
	spin_lock_init(&ddata->irq_lock);
	ddata->irq_is_disable = 0;
	ddata->irq_num_e = 0;
	ddata->irq_num_d = 0;
	print_info("%s: ==request_irq=\n", __func__);
	print_info("%s IRQ number is %d", client->name, client->irq);
	err =
	    request_irq(client->irq, gsl_ts_interrupt, IRQF_TRIGGER_RISING,
			client->name, ddata);
	if (err < 0) {
		dev_err(&client->dev, "gslX68X_probe: request irq failed\n");
		goto exit_irq_request_failed;
	}

	/*gsl of software init */
	gsl_sw_init(client);
	usleep_range(20000, 30000);
	check_mem_data(client);
#ifdef TPD_PROC_DEBUG
	gsl_config_proc = proc_create_data(GSL_CONFIG_PROC_FILE, 0666, NULL,
					   &gsl_ts_proc_fops, NULL);
	if (gsl_config_proc == NULL)
		print_info("create_proc_entry %s failed\n",
			   GSL_CONFIG_PROC_FILE);
	gsl_proc_flag = 0;
#endif

#ifdef GSL_TIMER
	INIT_DELAYED_WORK(&ddata->timer_work, gsl_timer_check_func);
	ddata->timer_wq = create_workqueue("gsl_timer_wq");
	queue_delayed_work(ddata->timer_wq, &ddata->timer_work,
			   GSL_TIMER_CHECK_CIRCLE);
#endif

	pm_runtime_enable(&client->dev);
	pm_runtime_forbid(&client->dev);

	print_info("%s: ==probe over =\n", __func__);
	return 0;

	free_irq(client->irq, ddata);
#ifdef TPD_PROC_DEBUG
	if (gsl_config_proc != NULL)
		remove_proc_entry(GSL_CONFIG_PROC_FILE, NULL);
#endif
	pm_runtime_disable(&client->dev);
exit_irq_request_failed:
	cancel_work_sync(&ddata->work);
	destroy_workqueue(ddata->wq);
exit_create_singlethread:

	input_unregister_device(ddata->idev);
	input_free_device(ddata->idev);
exit_i2c_transfer_fail:
	i2c_set_clientdata(client, NULL);
	kfree(ddata->cinfo);
	if (need_free_pdata)
		kfree(ddata->pdata);
exit_alloc_cinfo_failed:
	kfree(ddata);
exit_alloc_data_failed:
exit_check_functionality_failed:
	return err;
}

static int gsl_ts_remove(struct i2c_client *client)	/* need check further */
{

	pr_info("==gslX68X_ts_remove=\n");
	pm_runtime_disable(&client->dev);
#ifdef TPD_PROC_DEBUG
	if (gsl_config_proc != NULL)
		remove_proc_entry(GSL_CONFIG_PROC_FILE, NULL);
#endif

#ifdef GSL_TIMER
	cancel_delayed_work_sync(&ddata->timer_work);
	destroy_workqueue(ddata->timer_wq);
#endif

	free_irq(client->irq, ddata);
	input_unregister_device(ddata->idev);
	input_free_device(ddata->idev);

	cancel_work_sync(&ddata->work);
	destroy_workqueue(ddata->wq);
	i2c_set_clientdata(client, NULL);
	kfree(ddata->cinfo);
	if (need_free_pdata)
		kfree(ddata->pdata);
	kfree(ddata);

	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int gslx68x_ts_runtime_suspend(struct device *dev)
{
	u32 tmp;
	pr_info("[tp-gslx68x] %s: enter\n", __func__);

#ifdef TP_PROXIMITY_SENSOR
	pr_info("SWITCH=%d,INCALL=%d,TP_POWER=%d,irq_num_e=%d,irq_num_d=%d\n",
		PROXIMITY_SWITCH, INCALLING,
		TP_POWER, ddata->irq_num_e, ddata->irq_num_d);
	if ((TP_POWER == 1) && (PROXIMITY_SWITCH == 1)) {
		/* if it has power and TP need function of
		* turn off screen, just return. */
		pr_info("[tp-gslx68x] %s: leave.\n", __func__);
		return 0;
	}
#endif

	if (1 == ddata->gsl_sw_flag) {
		pr_info("%s: leave, 1==ddata->gsl_sw_flag\n", __func__);
		return 0;
	}
#ifdef TPD_PROC_DEBUG
	if (gsl_proc_flag == 1) {
		pr_info("%s: leave, gsl_proc_flag == 1\n", __func__);
		return 0;
	}
#endif
#ifdef GSL_ALG_ID
	tmp = gsl_version_id();
	pr_info("[tp-gsl]the version of alg_id:%x\n", tmp);
#endif
	/* version info */
	ddata->gsl_halt_flag = 1;
#ifdef GSL_TIMER
	cancel_delayed_work_sync(&ddata->timer_work);
	if (2 == ddata->gsl_timer_flag) {
		pr_info("%s: leave, 2==ddata->gsl_timer_flag\n", __func__);
		return 0;
	}
#endif
	gslx68x_irq_disable();
	cancel_work_sync(&ddata->work);
	if (gpio_request(ddata->pdata->reset, "gslx68x_reset"))
		pr_err("Failed to request GPIO for gslx68x_reset pin!\n");

	gpio_direction_output(ddata->pdata->reset, 0);
	usleep_range(5000, 10000);
	gpio_free(ddata->pdata->reset);
	if (ddata->power_ldo && (ddata->power_on)) {
		gslx68x_touch_io_power_onoff(&ddata->client->dev, 0);
		ddata->power_on = 0;
	}
#ifdef TP_PROXIMITY_SENSOR
	TP_POWER = 0;
#endif
	pr_info("%s end ok\n", __func__);
	return 0;
}

static int gslx68x_ts_runtime_resume(struct device *dev)
{
	int i;
	struct i2c_client *client = to_i2c_client(dev);
	pr_info("[tp-gslx68x] %s: enter\n", __func__);

#ifdef TP_PROXIMITY_SENSOR
	pr_info("SWITCH=%d,INCALL=%d,TP_POWER=%d,irq_num_e=%d,irq_num_d=%d\n",
		PROXIMITY_SWITCH, INCALLING,
		TP_POWER, ddata->irq_num_e, ddata->irq_num_d);
	if (TP_POWER == 1) {
		/* if it has power, just return */
		if (PROXIMITY_SWITCH == 1) {
			/* avoid the situation that screen can't be lighted
			 * when ear leave from screen. */
			input_report_abs(ddata->idev_ps, ABS_DISTANCE, 1);
			input_sync(ddata->idev_ps);
		}
		pr_info("[tp-gslx68x] %s: leave, TP_POWER==1\n", __func__);
		return 0;
	}
#endif
	if (ddata->power_ldo && !(ddata->power_on)) {
		gslx68x_touch_io_power_onoff(&ddata->client->dev, 1);
		ddata->power_on = 1;
	}

	if (1 == ddata->gsl_sw_flag) {
		ddata->gsl_halt_flag = 0;
		pr_info("%s: leave,1==ddata->gsl_sw_flag\n", __func__);
		return 0;
	}
#ifdef TPD_PROC_DEBUG
	if (gsl_proc_flag == 1) {
		ddata->gsl_halt_flag = 0;
		pr_info("%s: leave, gsl_proc_flag == 1\n", __func__);
		return 0;
	}
#endif

#ifdef GSL_TIMER
	if (2 == ddata->gsl_timer_flag) {
		ddata->gsl_halt_flag = 0;
		gslx68x_irq_enable();
		("%s: leave, 2==ddata->gsl_timer_flag\n", __func__);
		return 0;
	}
#endif
	if (ddata->pdata->reset)
		gslx68x_touch_reset();

	gsl_reset_core(client);
	gsl_start_core(client);
	usleep_range(20000, 30000);
	check_mem_data(client);

#ifdef GSL_TIMER
	queue_delayed_work(ddata->timer_wq, &ddata->timer_work,
			   GSL_TIMER_CHECK_CIRCLE);
	ddata->gsl_timer_flag = 0;
#endif
	ddata->gsl_halt_flag = 0;

	for (i = 0; i < 5; i++) {
		input_mt_slot(ddata->idev, i);
		input_report_abs(ddata->idev, ABS_MT_TRACKING_ID, -1);
	}
	input_sync(ddata->idev);
	gslx68x_irq_enable();
	usleep_range(10000, 20000);
#ifdef TP_PROXIMITY_SENSOR
	if (INCALLING != PROXIMITY_SWITCH) {
		/* if it has no power and TP state is not same as
		current TP suate, need set TP state again. */
		TP_POWER = 1;
		usleep_range(80000, 90000);
		tp_enable_ps(INCALLING);
	}

	TP_POWER = 1;
#endif
	pr_info("%s end ok\n", __func__);
	return 0;
}

#endif

static const struct dev_pm_ops gslx68x_ts_pmops = {
	SET_RUNTIME_PM_OPS(gslx68x_ts_runtime_suspend,
			   gslx68x_ts_runtime_resume, NULL)
};

static const struct of_device_id gslx68x_dt_ids[] = {
	{.compatible = "sliead,gslx68x-touch",},
	{},
};

MODULE_DEVICE_TABLE(of, gslx68x_dt_ids);

static const struct i2c_device_id gsl_ts_id[] = {
	{GSL_TS_NAME, 0}, {}
};

MODULE_DEVICE_TABLE(i2c, gsl_ts_id);

static struct i2c_driver gsl_ts_driver = {
	.probe = gsl_ts_probe,
	.remove = gsl_ts_remove,
	.id_table = gsl_ts_id,
	.driver = {
		   .name = GSL_TS_NAME,
		   .owner = THIS_MODULE,
		   .pm = &gslx68x_ts_pmops,
		   .of_match_table = of_match_ptr(gslx68x_dt_ids),
		   },
};

static int __init gslx68x_init(void)
{
	/* register I2C driver */
	int res = i2c_add_driver(&gsl_ts_driver);
	if (res) {
		print_info("add gslx68x i2c driver failed.\n");
		return -ENODEV;
	}
	return res;
}

static void __exit gslx68x_exit(void)
{
	print_info("removing gslx68x i2c driver.\n");
	i2c_del_driver(&gsl_ts_driver);
}

late_initcall(gslx68x_init);
module_exit(gslx68x_exit);
/* module_i2c_driver(gsl_ts_driver); */
MODULE_DESCRIPTION("gslx68x touch Driver");
MODULE_LICENSE("GPL");
