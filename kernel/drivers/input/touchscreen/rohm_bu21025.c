/*
 * Driver for Rohm BU21025GUL - A Resistive touch screen controller with
 * i2c interface
 *
 * Based on mcs5000_ts.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/semaphore.h>
#include <linux/gpio.h>
#include <linux/rtc.h>		//異物混入対応

#ifdef CONFIG_PM_RUNTIME
#include <linux/pm_runtime.h>
#endif /* CONFIG_PM_RUNTIME */

#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#endif /* CONFIG_OF */

#include <linux/regulator/consumer.h>

extern char panel_type[20];

//#define DEBUG
//#define DEBUG2

/* OPECODE */
#define POWER_SETTING 0x00
#define AUX_MEASURE   0x02
#define RESET         0x05
#define X_SET         0x08
#define Y_SET         0x09
#define Z_SET         0x0a
#define SETUP         0x0b
#define X_MEASURE     0x0c
#define Y_MEASURE     0x0d
#define Z1_MEASURE    0x0e
#define Z2_MEASURE    0x0f

#define ADC_MAX			0x1000
#define MAX_X			0xfff
#define MAX_Y			0xfff

/* OPERAND*/
/*type 0*/
#define PD_OFF (1 << 2)
#define PD_ON 0

#define X_CALIB_MAX 1280
#define Y_CALIB_MAX 800

//#define READ_TH 100
#define READ_TH 0

#define Z1_DIFF_MAX 250

//#define RX_VALUE 563	// Xパネルの実測抵抗値
//#define RY_VALUE 319	// Yパネルの実測抵抗値
//#define RX_VALUE 500	// XパネルMIN
//#define RY_VALUE 300	// YパネルMIN
//#define RX_VALUE 585	// 平均
//#define RY_VALUE 360	// 平均
//#define RX_VALUE 610	// XパネルMAX
//#define RY_VALUE 400	// YパネルMAX
//#define RX_VALUE 610
//#define RY_VALUE 380
//#define RX_VALUE 700
//#define RY_VALUE 310
#define RX_VALUE 690
#define RY_VALUE 340

#define X_INV 1
#define Y_INV 0
#define XY_SWAP 0

#define CMD_PRM_READ	0x01
#define CMD_PRM_WRITE	0x03
#define TS_NAME			"ts"
static int TS_MAJOR;
//#define SHOW_REG

struct rohm_bu21025gul_data {
	struct i2c_client		*client;
	struct input_dev		*input_dev;
	struct work_struct  work;
	struct hrtimer timer;
	struct mutex lock;
	volatile bool touch_stopped;
	bool int_enable;
	void (*ts_pwr_on)(int);
	int irq_gpio;
	int orient;
	int init_end;
	struct regulator *tp_1v8;
};

static struct workqueue_struct *wq;

static struct rohm_bu21025gul_data *rohm_bu21025gul_data;

//異物混入対応-s
struct touch_info {
	int flag;
	int x_val;
	int y_val;
	struct timespec uptime;
};

static struct touch_info t_info;

static int first_touch_flag = 0;

static void touch_info_set(int x_data, int y_data)
{
	int x,y,swap_data;

	first_touch_flag = 1;
	
	t_info.flag = 1;
	
	x = x_data;
	y = y_data;

	if(X_INV)
	{
		x = MAX_X - x;
	}

	if(Y_INV)
	{
		y = MAX_Y - y;
	}

	if(XY_SWAP)
	{
		swap_data = x;
		x = y;
		y = swap_data;
	}
	
	t_info.x_val = x;
	t_info.y_val = y;

	do_posix_clock_monotonic_gettime(&t_info.uptime);
	monotonic_to_bootbased(&t_info.uptime);
//printk("set:::flag:%d time_sec:%ld uptime_nsec:%09ld x_val:%d y_val:%d  \n", t_info.flag, t_info.uptime.tv_sec, t_info.uptime.tv_nsec, t_info.x_val, t_info.y_val );
}

static void touch_info_clear(void)
{
	struct touch_info clear_data = { 0, 0, 0, { 0, 0 } };
	
	first_touch_flag = 0;
	t_info = clear_data;
//printk("clear:::flag:%d time_sec:%ld uptime_nsec:%09ld x_val:%d y_val:%d  \n", t_info.flag, t_info.uptime.tv_sec, t_info.uptime.tv_nsec, t_info.x_val, t_info.y_val );
}
//異物混入対応-e

void rohm_bu21025gul_lock(struct rohm_bu21025gul_data *data)
{
	mutex_lock(&data->lock);
}

void rohm_bu21025gul_unlock(struct rohm_bu21025gul_data *data)
{
	mutex_unlock(&data->lock);
}

static int rohm_bu21025gul_write_reg(struct i2c_client *client, int data)
{
	struct i2c_msg i2cmsg;
	unsigned char buf = 0;
	int ret = 0;

	buf = data;

	i2cmsg.addr  = client->addr;
	i2cmsg.len   = 1;
	i2cmsg.buf   = &buf;

	i2cmsg.flags = 0;
	ret = i2c_transfer(client->adapter, &i2cmsg, 1);

	if (ret < 0) {
		dev_info(&client->dev,\
		"_%s:master_xfer Failed!!\n", __func__);
		return ret;
	}

	return 0;
}

static int rohm_bu21025gul_read_data(struct i2c_client *client)
{
	unsigned char buf[2] = {0, 0};
	struct i2c_msg i2cmsg;
	int ret = 0;

	i2cmsg.addr  = client->addr ;
	i2cmsg.len   = 2;
	i2cmsg.buf   = &buf[0];

	/*To read the data on I2C set flag to I2C_M_RD */
	i2cmsg.flags = I2C_M_RD;

	/* Start the i2c transfer by calling host i2c driver function */
	ret = i2c_transfer(client->adapter, &i2cmsg, 1);
	if (ret < 0) {
		dev_info(&client->dev,\
		"_%s:master_xfer Failed!!\n", __func__);
		return ret;
	}

	return (buf[0] << 4) | (buf[1] >> 4);
}

static u16 rohm_bu21025gul_aux(void)
{
	struct i2c_client *client = rohm_bu21025gul_data->client;
	u16 sample_data = 0;
	int err;

	rohm_bu21025gul_lock(rohm_bu21025gul_data);

	err = rohm_bu21025gul_write_reg(client, AUX_MEASURE << 4);
	if (err) {
		dev_err(&client->dev, "work error\n");
		goto err_proc;
	}
	udelay(150);
	sample_data = rohm_bu21025gul_read_data(client);
	sample_data = (sample_data*3150)/4096;

err_proc:
	rohm_bu21025gul_unlock(rohm_bu21025gul_data);

	return sample_data;
}

u16 rohm_bu21025gul_read_adc(void)
{
	u16 adc_data;

	adc_data = rohm_bu21025gul_aux();

	return adc_data;
}
EXPORT_SYMBOL_GPL(rohm_bu21025gul_read_adc);

static int calibration[9];
module_param_array(calibration, int, NULL, S_IRUGO | S_IWUSR);

static int calibration_pointer(int *x_orig, int *y_orig)
{
	int x,y,swap_data;

	x = *x_orig;
	y = *y_orig;

	if(X_INV)
	{
		x = MAX_X - x;
	}

	if(Y_INV)
	{
		y = MAX_Y - y;
	}

	if(XY_SWAP)
	{
		swap_data = x;
		x = y;
		y = swap_data;
	}

	/*本来は、4096系で座標を上げて、キャリブレーションすべきだが、
	  Androidが画面サイズにイベントを補正してしまうので、
	  画面サイズに変換しておく。
	*/
	x = x * X_CALIB_MAX / ADC_MAX;
	y = y * Y_CALIB_MAX / ADC_MAX;

	/*calibration*/
	*x_orig = x;
	*y_orig = y;

	if (calibration[6] == 0)
	{
		return -1;
	}

#if 0
	printk("data[0] = %d\n",calibration[0]);
	printk("data[1] = %d\n",calibration[1]);
	printk("data[2] = %d\n",calibration[2]);
	printk("data[3] = %d\n",calibration[3]);
	printk("data[4] = %d\n",calibration[4]);
	printk("data[5] = %d\n",calibration[5]);
#endif
	x = calibration[0] * (*x_orig) +
		calibration[1] * (*y_orig) +
		calibration[2];
	x /= calibration[6];
	if (x < 0)
		x = 0;
	y = calibration[3] * (*x_orig) +
		calibration[4] * (*y_orig) +
		calibration[5];
	y /= calibration[6];
	if (y < 0)
		y = 0;

	/**/

	*x_orig = x;
	*y_orig = y;

	return 0;
}

static enum hrtimer_restart rohm_bu21025gul_timer_func(struct hrtimer *timer)
{
	struct rohm_bu21025gul_data *data = container_of(timer, struct rohm_bu21025gul_data, timer);

	if( data->touch_stopped != true )
	{
		if (wq)
			queue_work(wq, &data->work);
	}
	return HRTIMER_NORESTART;
}

static irqreturn_t rohm_bu21025gul_interrupt(int irq, void *dev_id)
{
	struct rohm_bu21025gul_data *data = dev_id;

	if (gpio_get_value(data->irq_gpio) == 0) {
		if( data->touch_stopped != true )
		{
			data->int_enable = false;
			disable_irq_nosync(irq);
			if (wq)
				queue_work(wq,&data->work);
		}
	}

	return IRQ_HANDLED;
}

static void rohm_bu21025gul_work(struct work_struct *work)
{
	struct rohm_bu21025gul_data *data = container_of(work, struct rohm_bu21025gul_data, work);
	struct i2c_client *client = data->client;
	int err;
	int read_x,read_y,read_z1;
	unsigned int touch = 0 ;
	static int back_z1 = -1;
	int touch_value;
	static int keep_x = -1,keep_y = -1,keep_z1 = -1;
	static int send_x = -1,send_y = -1,send_z1 = -1;
	static unsigned int send_cnt = 0;

	rohm_bu21025gul_lock(data);
repeat:
	err = rohm_bu21025gul_write_reg(client, (X_SET << 4) | 0x00);
	if (err) {
		dev_err(&client->dev, "work error\n");
		goto err_proc;
	}

//	msleep(1);
//	udelay(600);
	udelay(1500);

	err = rohm_bu21025gul_write_reg(client, (X_MEASURE << 4) | PD_OFF);
	if (err) {
		dev_err(&client->dev, "work error\n");
		goto err_proc;
	}
//	udelay(150);

	read_x = rohm_bu21025gul_read_data(client);

	err = rohm_bu21025gul_write_reg(client, (Y_SET << 4) | 0x00);
	if (err) {
		dev_err(&client->dev, "work error\n");
		goto err_proc;
	}

//	msleep(1);
//	udelay(600);
	udelay(800);

	err = rohm_bu21025gul_write_reg(client, (Y_MEASURE << 4) | PD_OFF);
	if (err) {
		dev_err(&client->dev, "work error\n");
		goto err_proc;
	}
//	udelay(150);

	read_y = rohm_bu21025gul_read_data(client);

	err = rohm_bu21025gul_write_reg(client, (Z_SET << 4) | 0x00);
	if (err) {
		dev_err(&client->dev, "work error\n");
		goto err_proc;
	}

//	msleep(1);
	udelay(600);

	err = rohm_bu21025gul_write_reg(client, (Z1_MEASURE << 4) | PD_OFF);
	if (err) {
		dev_err(&client->dev, "work error\n");
		goto err_proc;
	}
//	udelay(150);

	read_z1 = rohm_bu21025gul_read_data(client);

#ifdef DEBUG2
printk("before_change: x %d,y %d,z1 %d\n",read_x,read_y,read_z1);
#endif

//異物混入対応-s
	if( first_touch_flag == 0 )
	{
		touch_info_set(read_x, read_y);
	}
//異物混入対応-e

	if( read_z1 == 0 )
	{
		touch_value = 0;
	}
	else
	{
		touch_value = ((RX_VALUE * read_x) / 4096) * (((4096 * 1000) / read_z1) - 1000) - ((1000 - ((read_y * 1000) / 4096)) * RY_VALUE);
	}
#ifdef DEBUG2
printk("touch_value %d,\n",touch_value);
#endif

	if( back_z1 == -1 && read_z1 != 0 )
	{
		touch = 2;
	}
	else if( read_z1 == 0 )
	{
		touch = 0;
	}
	else if( abs( back_z1 - read_z1 ) > Z1_DIFF_MAX )
	{
		touch = 2;
	}
//	else if( touch_value < 130000 || touch_value > 1300000 )
	else if( touch_value > 900000 )
//	else if( touch_value > 1300000 )
	{
#ifdef DEBUG2
printk("touch_value::NG %d\n",touch_value);
#endif
		touch = 2;
	}
	else
	{
		touch = 1;
	}

	if( read_z1 == 0 )
	{
		back_z1 = -1;
	}
	else
	{
		back_z1 = read_z1;
	}

	send_x = keep_x;
	send_y = keep_y;
	send_z1 = keep_z1;
	if( touch == 1 )
	{
		keep_x = read_x;
		keep_y = read_y;
		keep_z1 = read_z1;
	}
	else
	{
		keep_x = -1;
		keep_y = -1;
		keep_z1 = -1;
	}

#ifdef DEBUG2
printk("keep_x %d,keep_y %d,keep_z1 %d,send_x %d,send_y %d,send_z1 %d\n",keep_x,keep_y,keep_z1,send_x,send_y,send_z1);
#endif

	if((touch == 0) || (read_z1 == 0) || (read_z1 == 4095))
	{
//		if( send_cnt == 0 && send_z1 > READ_TH )
		if( send_cnt == 0 )
		{
			read_x = send_x;
			read_y = send_y;
			calibration_pointer(&read_x, &read_y);
#ifdef DEBUG2
printk("release_one-send x %d,y %d,z1 %d\n",read_x,read_y,read_z1);
#endif
			if(data->orient == 0)
			{
				input_report_abs(data->input_dev, ABS_X, read_x);
				input_report_abs(data->input_dev, ABS_Y, read_y);
			}
			else
			{
				input_report_abs(data->input_dev, ABS_X, Y_CALIB_MAX - read_y);
				input_report_abs(data->input_dev, ABS_Y, read_x);
			}
			input_report_abs(data->input_dev, ABS_PRESSURE, 1);
			input_event(data->input_dev, EV_KEY, BTN_TOUCH, 1);
			input_sync(data->input_dev);
		}

#ifdef DEBUG
		printk("release \n");
#endif
#ifdef DEBUG2
printk("ozaki-test:release  x %d,y %d,z1 %d, back_z1 %d\n",read_x,read_y,read_z1,back_z1);
#endif
		send_cnt = 0;
		
		input_report_abs(data->input_dev, ABS_PRESSURE, 0);
		input_event(data->input_dev, EV_KEY, BTN_TOUCH, 0);
		input_sync(data->input_dev);

		err = rohm_bu21025gul_write_reg(client, (POWER_SETTING << 4) | 0x00);
		if (err) {
			dev_err(&client->dev, "work error\n");
			goto err_proc;
		}
		touch_info_clear();		//異物混入対応
		
		msleep(1);/*これがないとペンアップ時に割り込みが入る*/
		data->int_enable = true;
		enable_irq(client->irq);
	}
	else if( touch == 1 )
	{
		read_x = send_x;
		read_y = send_y;
		read_z1 = send_z1;

#ifdef DEBUG
		printk("x %d,y %d,z1 %d\n",read_x,read_y,read_z1);
#endif
#ifdef DEBUG2
		printk("x %d,y %d,z1 %d,   send_cnt %u\n",read_x,read_y,read_z1,send_cnt);
#endif
		/*z1の値の低いものはここではじく*/
		if(read_z1 > READ_TH)
		{
			send_cnt++;
			
			calibration_pointer(&read_x, &read_y);
#ifdef DEBUG2
		printk("x %d,y %d,z1 %d\n",read_x,read_y,read_z1);
#endif
			if(data->orient == 0)
			{
				input_report_abs(data->input_dev, ABS_X, read_x);
				input_report_abs(data->input_dev, ABS_Y, read_y);
			}
			else
			{
				input_report_abs(data->input_dev, ABS_X, Y_CALIB_MAX - read_y);
				input_report_abs(data->input_dev, ABS_Y, read_x);
			}
			input_report_abs(data->input_dev, ABS_PRESSURE, 1);
			input_event(data->input_dev, EV_KEY, BTN_TOUCH, 1);
			input_sync(data->input_dev);
		}
//		hrtimer_start(&data->timer, ktime_set(0, 10000000), HRTIMER_MODE_REL);
		hrtimer_start(&data->timer, ktime_set(0, 5000000), HRTIMER_MODE_REL);
//		goto repeat;
	}
	else
	{
#ifdef DEBUG2
printk("ozaki-test: touch %d,x %d,y %d,z1 %d,back_z1 %d\n",touch,read_x,read_y,read_z1,back_z1);
#endif
		hrtimer_start(&data->timer, ktime_set(0, 10000000), HRTIMER_MODE_REL);
	}

	rohm_bu21025gul_unlock(data);

	return;

err_proc:
	touch_info_clear();		//異物混入対応
	
	rohm_bu21025gul_write_reg(client, (RESET << 4) | 0x00);/*reset*/
	rohm_bu21025gul_unlock(data);
	data->int_enable = true;
	enable_irq(client->irq);

	return;
}

static int rohm_bu21025gul_chip_init(struct i2c_client *client)
{
	int error = 0;

	error = rohm_bu21025gul_write_reg(client, (RESET << 4) | 0x00);
	if (error) {
		dev_err(&client->dev, "Failed to init RESET\n");
		return error;
	}
	error = rohm_bu21025gul_write_reg(client, (SETUP << 4) | 0x00);
	if (error) {
		dev_err(&client->dev, "Failed to init SETUP\n");
	}

	return error;
}

//異物混入対応-s
ssize_t ts_info_show(struct device *dev, struct device_attribute *attr,char *buf)
{
	return sprintf(buf, "%d %ld%09ld %d %d\n", t_info.flag, t_info.uptime.tv_sec, t_info.uptime.tv_nsec, t_info.x_val, t_info.y_val );
}

ssize_t ts_info_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	return count;
}

DEVICE_ATTR(ts_info, 0644, ts_info_show, ts_info_store);

static struct attribute *tsinfo_attributes[] = {
	&dev_attr_ts_info.attr,
	NULL,
};

static struct attribute_group tsinfo_attr_group = {
	.attrs = tsinfo_attributes,
};
//異物混入対応-e

static int rohm_bu21025gul_probe(struct i2c_client *client,
				       const struct i2c_device_id *id)
{
	struct rohm_bu21025gul_data *data;
	struct input_dev *input_dev;
	struct regulator *tp_1v8;
	int error;
	int ret;

	data = kzalloc(sizeof(struct rohm_bu21025gul_data), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!data || !input_dev) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		error = -ENOMEM;
		goto err_free_mem;
	}
	data->client = client;
	data->input_dev = input_dev;
	INIT_WORK(&data->work, rohm_bu21025gul_work);
	data->touch_stopped = false;
	data->init_end = 0;

	memset(calibration, 0, sizeof(calibration));

	touch_info_clear();		//異物混入対応

	mutex_init(&data->lock);

	input_dev->name = "rohm_bu21025";
	input_dev->id.bustype = BUS_I2C;
	input_dev->dev.parent = &client->dev;

	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	input_dev->evbit[0] = (BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS));

	if (!strcmp(panel_type, "WXGA_P")) {
		data->orient = 1;
	}
	else if (!strcmp(panel_type, "WXGA")) {
		data->orient = 0;
	}

	if(data->orient == 0) {
		input_set_abs_params(input_dev, ABS_X, 0, X_CALIB_MAX, 0, 0);
		input_set_abs_params(input_dev, ABS_Y, 0, Y_CALIB_MAX, 0, 0);
	}
	else {
		input_set_abs_params(input_dev, ABS_X, 0, Y_CALIB_MAX, 0, 0);
		input_set_abs_params(input_dev, ABS_Y, 0, X_CALIB_MAX, 0, 0);
	}
	input_set_drvdata(input_dev, data);

	client->irq = 0;
	ret = of_get_named_gpio(client->dev.of_node, "irq-gpio", 0);
	if (ret >= 0) {
		data->irq_gpio = ret;
		gpio_request(data->irq_gpio, NULL);
		gpio_direction_input(data->irq_gpio);
		client->irq = gpio_to_irq(data->irq_gpio);
	}

#if 0
	tp_1v8 = regulator_get(&client->dev, "tp_1v8");
	if (IS_ERR_OR_NULL(tp_1v8)) {
		if (PTR_ERR(tp_1v8) < 0) {
			pr_info("%s: the regulator for tp_1v8 not found\n",
				__func__);
		}
	} else {
		data->tp_1v8 = tp_1v8;
		if (regulator_set_voltage(data->tp_1v8, 1800000, 1800000))
			pr_err("fail to set regulator data->tp_1v8 to 1.8v\n");
		if (!regulator_is_enabled(data->tp_1v8) &&
				regulator_enable(data->tp_1v8))
			pr_err("fail to enable regulator tp_1v8\n");
		msleep(5);
	}
#endif

	if(client->irq)
	{
		wq = create_singlethread_workqueue("bu21025gul_wq");
		//timer init
		hrtimer_init(&data->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		data->timer.function = rohm_bu21025gul_timer_func;
	}
	//init
	rohm_bu21025gul_data = data;

	rohm_bu21025gul_data->ts_pwr_on = client->dev.platform_data;

	error = rohm_bu21025gul_chip_init(client);
	if (error) {
		goto err_free_mem;
	}

	error = input_register_device(data->input_dev);
	if (error)
		goto err_free_irq;

	i2c_set_clientdata(client, data);

//異物混入対応-s
	error = sysfs_create_group(&client->dev.kobj, &tsinfo_attr_group);
	if (error)
		goto err_free_irq;
//異物混入対応-e

#ifdef CONFIG_PM_RUNTIME
	pm_runtime_enable(&client->dev);
	pm_runtime_forbid(&client->dev);
#endif /* CONFIG_PM_RUNTIME */

	if(client->irq)
	{
		data->int_enable = true;
		error = request_irq(client->irq, rohm_bu21025gul_interrupt,
			IRQF_TRIGGER_FALLING | IRQF_ONESHOT, "bu21025gul_irq", data);
		if (error) {
			dev_err(&client->dev, "Failed to register interrupt\n");
			goto err_free_mem;
		}
	}

	if (gpio_get_value(data->irq_gpio) == 0)
	{
		if( data->int_enable == true )
		{
			data->int_enable = false;
			disable_irq(client->irq);
			if (wq)
				queue_work(wq,&data->work);
		}
	}

	data->init_end = 1;

	return 0;

err_free_irq:
	free_irq(client->irq, data);
err_free_mem:
	input_free_device(input_dev);
	kfree(data);
	return error;
}

static  int rohm_bu21025gul_remove(struct i2c_client *client)
{
	struct rohm_bu21025gul_data *data = i2c_get_clientdata(client);

	if(client->irq)
	{
		free_irq(client->irq, data);

		if (wq)
			destroy_workqueue(wq);

		hrtimer_cancel(&data->timer);
	}
	mutex_destroy(&data->lock);

	input_unregister_device(data->input_dev);
	kfree(data);

	return 0;
}

static long rohm_bu21025gul_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	int 		ret = 0;
	int		*ibuf = (int *)arg;
	int i;
	
#ifdef SHOW_REG
	printk("rohm_ts_command() Call\n");
#endif
	switch( cmd ) {

		case CMD_PRM_READ:
#ifdef SHOW_REG
				printk("Commnd:Parm Read\n");
#endif
			for( i=0;  i<7; i++) {
				ibuf[i] = calibration[i];
			}
			break;

		case CMD_PRM_WRITE:
#ifdef SHOW_REG
				printk("Commnd:Parm Write\n");
#endif
				for( i=0;  i<7; i++) {
					calibration[i] = ibuf[i];
				}
#ifdef SHOW_REG
				for( i=0;  i<7; i++) {
					printk("Commnd:Parm Write i:%d param src: %d dis:%d\n",i,ibuf[i],calibration[i] );
				}
#endif

			break;
		default:
			ret = -EIO;				//
			break;
	}

#ifdef SHOW_REG
	printk("rohm_ts_command() Call End Return Status:%d\n",ret);
#endif
	return ret;
}
static const struct file_operations ts_fops = {
	.owner			= THIS_MODULE,
	.unlocked_ioctl	= rohm_bu21025gul_ioctl,
};
static struct class *ts_dev_class;

#ifdef CONFIG_PM
/**
 * bu21025_suspend() - suspend the touch screen controller
 * @dev: pointer to device structure
 *
 * This function is used to suspend the
 * touch panel controller and returns integer
 */
static int bu21025_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client( dev );
	struct rohm_bu21025gul_data *data = i2c_get_clientdata(client);

	touch_info_clear();		//異物混入対応

#if 0
	if (data->tp_1v8) {
		if (regulator_is_enabled(data->tp_1v8) &&
				regulator_disable(data->tp_1v8))
			pr_err("fail to disable regulator tp_1v8\n");
	}
#endif

	return 0;
}

/**
 * bu21025_resume() - resume the touch screen controller
 * @dev: pointer to device structure
 *
 * This function is used to resume the touch panel
 * controller and returns integer.
 */
static int bu21025_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client( dev );
	struct rohm_bu21025gul_data *data = i2c_get_clientdata(client);

#if 0
	if (data->tp_1v8) {
		if (regulator_set_voltage(data->tp_1v8, 1800000, 1800000))
			pr_err("fail to set regulator data->tp_1v8 to 1.8v\n");
		if (!regulator_is_enabled(data->tp_1v8) &&
				regulator_enable(data->tp_1v8))
			pr_err("fail to enable regulator tp_1v8\n");

		mdelay(5);
	}
#endif

	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int bu21025_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client( dev );
	struct rohm_bu21025gul_data *data = i2c_get_clientdata(client);

	data->touch_stopped = true;
	destroy_workqueue(wq);
	wq = 0;
	rohm_bu21025gul_lock(data);
	hrtimer_cancel(&data->timer);
	if(data->int_enable == true)
	{
		disable_irq_nosync(client->irq);
		data->int_enable = false;
	}
	rohm_bu21025gul_unlock(data);

	return 0;
}

static int bu21025_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client( dev );
	struct rohm_bu21025gul_data *data = i2c_get_clientdata(client);

	if(data->init_end == 0)
		return 0;

	rohm_bu21025gul_lock(data);
	rohm_bu21025gul_chip_init(client);
	if(wq == 0)
	{
		wq = create_singlethread_workqueue("rohm_bu21025gul_wq");
	}
	data->touch_stopped = false;
	if(data->int_enable == false)
	{
		data->int_enable = true;
		enable_irq(client->irq);
	}
	rohm_bu21025gul_unlock(data);

	return 0;
}
#endif

static const struct dev_pm_ops bu21025_dev_pm_ops = {
	.suspend = bu21025_suspend,
	.resume  = bu21025_resume,
#ifdef CONFIG_PM_RUNTIME
	SET_RUNTIME_PM_OPS(
		bu21025_runtime_suspend,
		bu21025_runtime_resume,
		NULL)
#endif /* CONFIG_PM_RUNTIME */
};
#endif

static const struct i2c_device_id rohm_bu21025gul_id[] = {
	{"bu21025", 0},
	{ }
};
MODULE_DEVICE_TABLE(i2c, rohm_bu21025gul_id);

#ifdef CONFIG_OF
static struct of_device_id rohm_bu21025gul_of_idtable[] = {
	{ .compatible = "rohm,bu21025gul", },
	{}
};
#endif /* CONFIG_OF */

static struct i2c_driver rohm_bu21025gul_driver = {
	.driver = {
		.name	= "bu21025",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	=	&bu21025_dev_pm_ops,
#endif
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(rohm_bu21025gul_of_idtable),
#endif /* CONFIG_OF */
	},
	.id_table	= rohm_bu21025gul_id,
	.probe		= rohm_bu21025gul_probe,
	.remove		= rohm_bu21025gul_remove,
};

static int __init rohm_bu21025gul_init(void)
{
#if 0
	return i2c_add_driver(&rohm_bu21025gul_driver);
#else
#ifdef SHOW_DEBUG
	printk("ROHM BU21025BU rohm_bu21025gul_init \n");
#endif
        int res;

	TS_MAJOR = register_chrdev(0, TS_NAME, &ts_fops);
	if (TS_MAJOR<0) {
		printk(KERN_ERR "%s: Driver Initialisation failed\n", __FILE__);
		res = TS_MAJOR;
		goto out;
	}

	printk("/dev/ts  MAJOR:%d\n",TS_MAJOR);

	ts_dev_class = class_create(THIS_MODULE, "ts-dev");
	if (IS_ERR(ts_dev_class)) {
		res = PTR_ERR(ts_dev_class);
		goto out_unreg_chrdev;
	}

	/* register this i2c device with the driver core */
	if( device_create(ts_dev_class, NULL,
					 MKDEV(TS_MAJOR, 0), NULL, "ts") == NULL ) {
		res = -1;
		goto out_unreg_class; ;
	}

	if( (res = i2c_add_driver(&rohm_bu21025gul_driver)) < 0 ) goto out_destroy; 

	printk("ROHM BU21025BU rohm_bu21025gul_init Success \n");
	return res;

out_destroy:
	device_destroy(ts_dev_class, MKDEV(TS_MAJOR, 0));

out_unreg_class:
	class_destroy(ts_dev_class);
out_unreg_chrdev:
	unregister_chrdev(TS_MAJOR, TS_NAME);
out:
	printk("ROHM BU21025BU rohm_bu21025gul_init Fail \n");
	return res;
#endif
}

static void __exit rohm_bu21025gul_exit(void)
{
	i2c_del_driver(&rohm_bu21025gul_driver);
	class_destroy(ts_dev_class);
	unregister_chrdev(TS_MAJOR, TS_NAME);
}

module_init(rohm_bu21025gul_init);
module_exit(rohm_bu21025gul_exit);

MODULE_AUTHOR("STST");
MODULE_DESCRIPTION("Touchscreen driver for ROHM BU21025GUL controller");
MODULE_LICENSE("GPL");
