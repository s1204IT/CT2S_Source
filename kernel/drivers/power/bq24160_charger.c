/*
 * bq24160 charger driver
 *
 * Copyright (C) 2014-2015 SANYO Techno Solutions Tottori Co., Ltd.
 *
 * Based on bq2415x charger driver
 * Copyright (C) 2011-2012  Pali Rohár <pali.rohar@gmail.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/param.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/sysfs.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/idr.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/edge_wakeup_mmp.h>

#include <linux/power/bq24160_charger.h>

#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#endif /* CONFIG_OF */

#ifdef CONFIG_USB_MV_OTG
#include <linux/platform_data/mv_usb.h>
#endif /* CONFIG_USB_MV_OTG */

struct bq24160_device {
	struct device *dev;
	struct power_supply ac;
	struct power_supply usb;
	struct power_supply bat;
	struct delayed_work work_bq;
	struct workqueue_struct *bq24160_wq;
	struct work_struct workq;
	int percent;
	int old_percent;
	int charger_online;
	int usb_charger_online;
	int first_delay_count;
	bool usb_in;
	bool ac_in;
	bool chg;
	bool fault;
	int voltage_uV;
	int battery_status;
	unsigned int interval;
#ifdef CONFIG_USB_MV_OTG
	int gpio_vbst;
	int gpio_vbus;
	struct notifier_block chg_notif;
#endif /* CONFIG_USB_MV_OTG */
};

#define BATTERY_UPDATE_INTERVAL		5	/*seconds*/
#define FULL_STATUS_PERCENT		100

#define BQ24160_STATUS_CONTROL		0x00
#define BQ24160_BATTERY_SUPPLY_STATUS	0x01
#define BQ24160_CONTROL			0x02
#define BQ24160_BATTERY_VOLTAGE		0x03
#define BQ24160_V_P_R			0x04

#define BQ24160_STAT_MASK		0x70
#define BQ24160_STAT_CHRG_FROM_IN	0x30
#define BQ24160_STAT_CHRG_FROM_USB	0x40

#define BQ24160_INSTAT_MASK		0xC0
#define BQ24160_INSTAT_NORMAL		0x00
#define BQ24160_USBSTAT_MASK		0x30
#define BQ24160_USBSTAT_NORMAL		0x00

#define BQ24160_OTG_LOCK		0x08

#define BQ24160_RESET			0x80
#define BQ24160_IUSB_LIMIT_MASK		0x70
#define BQ24160_IUSB_LIMIT_USB100	0x00
#define BQ24160_IUSB_LIMIT_USB500	0x20
#define BQ24160_IUSB_LIMIT_USB1500	0x50

#define BQ24160_DP_DN_EN		0x01

static int bq24160_suspend_flag = 0;

#ifdef CONFIG_USB_MV_OTG
static struct bq24160_device *global_bq = NULL;
#endif /* CONFIG_USB_MV_OTG */

#ifdef CONFIG_BATTERY_LC709203F
extern int lc709203f_get_vcell_external(void);
extern int lc709203f_get_soc_external(void);
extern int lc709203f_get_temp_external(void);
#endif

static DEFINE_MUTEX(bq24160_i2c_mutex);
static DEFINE_MUTEX(bq24160_work_mutex);

static int bq24160_i2c_read(struct bq24160_device *bq, u8 reg, u8 *val)
{
	struct i2c_client *client = to_i2c_client(bq->dev);
	struct i2c_msg msg[2];
	int ret;

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = &reg;
	msg[0].len = sizeof(reg);
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = val;
	msg[1].len = sizeof(*val);

	mutex_lock(&bq24160_i2c_mutex);
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	mutex_unlock(&bq24160_i2c_mutex);

	if (ret < 0)
		return ret;

	dev_dbg(bq->dev, "%s, reg %02x data %02x\n", __func__, reg , *val);

	return 0;
}

static int bq24160_i2c_write(struct bq24160_device *bq, u8 reg, u8 val)
{
	struct i2c_client *client = to_i2c_client(bq->dev);
	struct i2c_msg msg[1];
	u8 data[2];
	int ret;

	data[0] = reg;
	data[1] = val;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = data;
	msg[0].len = ARRAY_SIZE(data);

	mutex_lock(&bq24160_i2c_mutex);
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	mutex_unlock(&bq24160_i2c_mutex);

	if (ret < 0)
		return ret;
	else if (ret != 1)
		return -EIO;

	return 0;
}

/**** power supply interface code ****/
static enum power_supply_property bq24160_charger_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
};

static enum power_supply_property bq24160_battery_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CAPACITY,
//	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
//	POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
};

static int bq24160_battery_get_property(struct power_supply *bat,
				       enum power_supply_property psp,
				       union power_supply_propval *val)
{
	struct bq24160_device *di = container_of(bat,
			struct bq24160_device, bat);
	mutex_lock(&bq24160_work_mutex);

	switch (psp) {
		case POWER_SUPPLY_PROP_STATUS:
			val->intval = di->battery_status;
			break;
		case POWER_SUPPLY_PROP_VOLTAGE_NOW:
			val->intval = di->voltage_uV;
			break;
		case POWER_SUPPLY_PROP_CHARGE_NOW:
			val->intval = 0;
			break;
//		case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
//			val->intval = HIGH_VOLT_THRESHOLD;
//			break;
//		case POWER_SUPPLY_PROP_VOLTAGE_MIN_DESIGN:
//			val->intval = LOW_VOLT_THRESHOLD;
//			break;
		case POWER_SUPPLY_PROP_PRESENT:
			val->intval = 1;
			break;
		case POWER_SUPPLY_PROP_CAPACITY:
			val->intval = di->percent < 0 ? 0 :
					(di->percent > 100 ? 100 : di->percent);
			break;
		case POWER_SUPPLY_PROP_HEALTH:
			val->intval = POWER_SUPPLY_HEALTH_GOOD;
			if (di->fault)
				val->intval = POWER_SUPPLY_HEALTH_UNSPEC_FAILURE;
			break;
		case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
			if (di->battery_status == POWER_SUPPLY_STATUS_FULL)
				val->intval = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
			else if (di->percent <= 15)
				val->intval = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
			else
				val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
			break;
		case POWER_SUPPLY_PROP_TEMP:
#ifdef CONFIG_BATTERY_LC709203F
			val->intval = lc709203f_get_temp_external();
#else
			val->intval = 20 * 10;
#endif
			break;
		case POWER_SUPPLY_PROP_TECHNOLOGY:
			val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
			break;
		default:
			mutex_unlock(&bq24160_work_mutex);
			return -EINVAL;
	}
	mutex_unlock(&bq24160_work_mutex);
	return 0;
}
static int bq24160_get_property(struct power_supply *ac,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct bq24160_device *bq = container_of(ac,
			struct bq24160_device, ac);
	mutex_lock(&bq24160_work_mutex);
	switch (psp) {
		case POWER_SUPPLY_PROP_ONLINE:
			val->intval = 0;
			if (bq->ac_in)
				val->intval = 1;
			bq->charger_online = val->intval;
			break;
		default:
			mutex_unlock(&bq24160_work_mutex);
			return -EINVAL;
	}
	mutex_unlock(&bq24160_work_mutex);
	return 0;
}

static int bq24160_get_usb_property(struct power_supply *usb,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct bq24160_device *bq = container_of(usb,
			struct bq24160_device, usb);
	mutex_lock(&bq24160_work_mutex);
	switch (psp) {
		case POWER_SUPPLY_PROP_ONLINE:
			val->intval = 0;
			if (bq->usb_in)
				val->intval = 1;
			bq->usb_charger_online = val->intval;
			break;
		default:
			mutex_unlock(&bq24160_work_mutex);
			return -EINVAL;
	}
	mutex_unlock(&bq24160_work_mutex);
	return 0;
}

static int bq24160_power_supply_init(struct bq24160_device *bq)
{
	int ret;

	bq->ac.name = "bq24160-ac";
	bq->ac.type = POWER_SUPPLY_TYPE_MAINS;
	bq->ac.get_property = bq24160_get_property;
	bq->ac.properties = bq24160_charger_props;
	bq->ac.num_properties = ARRAY_SIZE(bq24160_charger_props);
	ret = power_supply_register(bq->dev, &bq->ac);
	if (ret) {
		dev_err(bq->dev, "failed: power supply register.\n");
		return ret;
	}
	bq->usb.name = "bq24160-usb";
	bq->usb.type = POWER_SUPPLY_TYPE_USB;
	bq->usb.get_property = bq24160_get_usb_property;
	bq->usb.properties = bq24160_charger_props;
	bq->usb.num_properties = ARRAY_SIZE(bq24160_charger_props);
	ret = power_supply_register(bq->dev, &bq->usb);
	if (ret) {
		dev_err(bq->dev, "failed: power supply register.\n");
		return ret;
	}
	bq->bat.name = "bq24160-charger";
	bq->bat.type = POWER_SUPPLY_TYPE_BATTERY;
	bq->bat.properties = bq24160_battery_props;
	bq->bat.num_properties = ARRAY_SIZE(bq24160_battery_props);
	bq->bat.get_property = bq24160_battery_get_property;
	bq->bat.use_for_apm = 1;
	ret = power_supply_register(bq->dev, &bq->bat);
	if (ret) {
		dev_err(bq->dev, "failed to register battery\n");
		return ret;
	}

	return 0;
}

static void bq24160_power_supply_exit(struct bq24160_device *bq)
{
	power_supply_unregister(&bq->ac);
	power_supply_unregister(&bq->usb);
	power_supply_unregister(&bq->bat);
}

static int bq24160_read_status(struct bq24160_device *bq)
{
	int ret;
	u8 data;

	bq->chg    = false;
	bq->ac_in  = false;
	bq->usb_in = false;
	bq->fault  = false;

	ret = bq24160_i2c_read(bq, BQ24160_STATUS_CONTROL, &data);
	if (ret < 0) {
		dev_err(bq->dev, "could not read status (%d)\n", ret);
		return ret;
	}
	switch (data & BQ24160_STAT_MASK)
	{
	case BQ24160_STAT_CHRG_FROM_IN:
	case BQ24160_STAT_CHRG_FROM_USB:
	    bq->chg = true;
	    break;

	default:
	    break;
	}

	ret = bq24160_i2c_read(bq, BQ24160_BATTERY_SUPPLY_STATUS, &data);
	if (ret < 0) {
		dev_err(bq->dev, "could not read in/usb status (%d)\n", ret);
		return ret;
	}
	if ((data & BQ24160_INSTAT_MASK) == BQ24160_INSTAT_NORMAL) {
	    bq->ac_in = true;
	}
	if ((data & BQ24160_USBSTAT_MASK) == BQ24160_USBSTAT_NORMAL) {
	    bq->usb_in = true;
	}

	return 0;
}

static void bq24160_update_status(struct bq24160_device *bq)
{
	int temp = 0;
	static int temp_last;
	bool changed_flag = false;
	bool old_usb_in = bq->usb_in;
	bool old_ac_in  = bq->ac_in;
	bool old_chg    = bq->chg;
	bool old_fault  = bq->fault;

	mutex_lock(&bq24160_work_mutex);

	if(bq24160_read_status(bq))
	{
		mutex_unlock(&bq24160_work_mutex);
		return;
	}
	if (bq->usb_in || bq->ac_in) {
		if (bq->ac_in)
			bq->charger_online = 1;

		if (bq->usb_in)
			bq->usb_charger_online = 1;
	} else {
		bq->charger_online = 0;
		bq->usb_charger_online = 0;
	}
	if (bq->charger_online == 0 && bq->usb_charger_online == 0) {
			bq->battery_status = POWER_SUPPLY_STATUS_DISCHARGING;
	} else {
#if 1
		if (bq->ac_in || bq->usb_in) {
			if (bq->percent >= FULL_STATUS_PERCENT)
				bq->battery_status = POWER_SUPPLY_STATUS_FULL;
			else
				bq->battery_status = POWER_SUPPLY_STATUS_CHARGING;
		}
		else
			bq->battery_status = POWER_SUPPLY_STATUS_NOT_CHARGING;
#else
		if (bq->chg == true) {
			bq->battery_status = POWER_SUPPLY_STATUS_CHARGING;
		} else if (bq->ac_in && (bq->chg == false)) {
			if (bq->percent >= FULL_STATUS_PERCENT)
				bq->battery_status = POWER_SUPPLY_STATUS_FULL;
			else
				bq->battery_status = POWER_SUPPLY_STATUS_CHARGING;
		} else if (bq->usb_in && (bq->chg == false)) {
			if (bq->percent >= FULL_STATUS_PERCENT)
				bq->battery_status = POWER_SUPPLY_STATUS_FULL;
			else
				bq->battery_status = POWER_SUPPLY_STATUS_CHARGING;
		}
#endif
	}
	if(old_ac_in != bq->ac_in){
		power_supply_changed(&bq->ac);
	}
	if(old_usb_in != bq->usb_in){
		power_supply_changed(&bq->usb);
	}

#ifdef CONFIG_BATTERY_LC709203F
	if(bq24160_suspend_flag != 1)
	{
		temp = lc709203f_get_vcell_external() * 1000;
	}
	else
	{
		temp = bq->voltage_uV;
	}
#else
	temp = 4000000;
#endif

	if (temp_last == 0) {
		bq->voltage_uV = temp;
		temp_last = temp;
	}
	if (bq->charger_online == 0 && temp_last != 0) {
		temp_last = temp;
		bq->voltage_uV = temp;
	}
	if (bq->charger_online == 1 || bq->usb_charger_online == 1) {
		bq->voltage_uV = temp;
		temp_last = temp;
	}
#ifdef CONFIG_BATTERY_LC709203F
	if(bq24160_suspend_flag != 1)
	{
		bq->percent = lc709203f_get_soc_external();
	}
	if(bq->percent > 100)
	{
		bq->percent = 100;
	}
#else
	bq->percent = 100;
#endif

	if (bq->percent != bq->old_percent) {
		bq->old_percent = bq->percent;
		changed_flag = true;
	}

	if (bq->chg != old_chg) {
		changed_flag = true;
	}

	if (changed_flag) {
		changed_flag = false;
		power_supply_changed(&bq->bat);
	}
	 /*
	  *because boot time gap between led framwork and charger
	  *framwork,when system boots with charger attatched,
	  *charger led framwork loses the first charger online event,
	  *add once extra power_supply_changed can fix this issure
	  */
	if (bq->first_delay_count < 200) {
		bq->first_delay_count = bq->first_delay_count + 1 ;
		power_supply_changed(&bq->bat);
	}

	mutex_unlock(&bq24160_work_mutex);
}

static void bq24160_workqueue(struct work_struct *work)
{
	struct bq24160_device *bq;
	bq = container_of(work, struct bq24160_device, workq);

	bq24160_update_status(bq);
}

static irqreturn_t bq24160_irq_thread(int irq, void *data)
{
	struct bq24160_device *bq = data;

	queue_work(bq->bq24160_wq, &bq->workq);

	return IRQ_HANDLED;
}

static void bq24160_battery_work(struct work_struct *work)
{
	struct bq24160_device *bq;
	bq = container_of(work, struct bq24160_device, work_bq.work);
	bq->interval = HZ * BATTERY_UPDATE_INTERVAL;

	bq24160_update_status(bq);
	/* reschedule for the next time */
	schedule_delayed_work(&bq->work_bq, bq->interval);
}

#ifdef CONFIG_USB_MV_OTG
static int vbus_on_before_probe = -1;

int bq24160_set_vbus(int on)
{
	struct bq24160_device *bq = global_bq;
	int ret;
	u8 data;

	if (!bq) {
		pr_warn("%s: device is not probed.\n", __FUNCTION__);
		vbus_on_before_probe = on;
		return 0;
	}

	/* update OTG_LOCK bit */
	ret = bq24160_i2c_read(bq, BQ24160_BATTERY_SUPPLY_STATUS, &data);
	if (ret != 0) {
		dev_err(bq->dev, "could not read otg status. (%d)\n", ret);
		return ret;
	}
	if (on) {
		data |= BQ24160_OTG_LOCK;
	}
	else {
		data &= ~BQ24160_OTG_LOCK;
	}
	ret = bq24160_i2c_write(bq, BQ24160_BATTERY_SUPPLY_STATUS, data);
	if (ret != 0) {
		dev_err(bq->dev, "could not write OTG_LOCK bit. (%d)\n", ret);
		return ret;
	}

	if (gpio_is_valid(bq->gpio_vbst)) {
		gpio_request(bq->gpio_vbst, "vbst_5v0_on");
		gpio_direction_output(bq->gpio_vbst, on? 1:0);
		gpio_free(bq->gpio_vbst);
	}
	if (gpio_is_valid(bq->gpio_vbus)) {
		gpio_request(bq->gpio_vbus, "usb_otg_vbus_on");
		gpio_direction_output(bq->gpio_vbus, on? 1:0);
		gpio_free(bq->gpio_vbus);
	}

	return 0;
}

int bq24160_charger_detect(void)
{
	struct bq24160_device *bq = global_bq;

	if (!bq) {
		pr_warn("%s: device is not probed.\n", __FUNCTION__);
		return NULL_CHARGER;
	}

	if (bq->usb_in || bq->ac_in) {
		return DEFAULT_CHARGER;
	}
	else {
		return NULL_CHARGER;
	}
}

static int bq24160_chg_notifier_callback(
	struct notifier_block *nb, unsigned long type, void *chg_event)
{
	struct bq24160_device *bq =
		container_of(nb, struct bq24160_device, chg_notif);

	int ret;
	u8 data, tmp;
	static int type_bak = NULL_CHARGER;

	if (type == type_bak) {
		return 0;
	}

	dev_info(bq->dev, "enable D+/D- detection\n");
	ret = bq24160_i2c_read(bq, BQ24160_BATTERY_VOLTAGE, &data);
	if (ret < 0) {
		dev_err(bq->dev, "could not read D+/D-_EN\n");
		return ret;
	}
	data |= BQ24160_DP_DN_EN;
	ret = bq24160_i2c_write(bq, BQ24160_BATTERY_VOLTAGE, data);
	if (ret < 0) {
		dev_err(bq->dev, "could not write D+/D-_EN\n");
		return ret;
	}
	type_bak = type;

	ret = bq24160_i2c_read(bq, BQ24160_CONTROL, &data);
	if (ret < 0) {
		dev_err(bq->dev, "could not read control register\n");
		return ret;
	}
	data &= ~BQ24160_RESET;
	tmp = data & BQ24160_IUSB_LIMIT_MASK;
	switch (type)
	{
	case NULL_CHARGER:
	case DEFAULT_CHARGER:
		if (tmp != BQ24160_IUSB_LIMIT_USB100) {
			dev_info(bq->dev, "set current limit to 100mA\n");
			data &= ~BQ24160_IUSB_LIMIT_MASK;
			data |=  BQ24160_IUSB_LIMIT_USB100;
		}
		break;

	case SDP_CHARGER:
		if (tmp < BQ24160_IUSB_LIMIT_USB500) {
			dev_info(bq->dev, "set current limit to 500mA\n");
			data &= ~BQ24160_IUSB_LIMIT_MASK;
			data |=  BQ24160_IUSB_LIMIT_USB500;
		}
		break;

	case CDP_CHARGER:
	case DCP_CHARGER:
		if (tmp < BQ24160_IUSB_LIMIT_USB1500) {
			dev_info(bq->dev, "set current limit to 1500mA\n");
			data &= ~BQ24160_IUSB_LIMIT_MASK;
			data |=  BQ24160_IUSB_LIMIT_USB1500;
		}
		break;

	default:
		dev_warn(bq->dev, "non-standard charger was detected.\n");
		if (tmp != BQ24160_IUSB_LIMIT_USB100) {
			dev_info(bq->dev, "set current limit to 100mA\n");
			data &= ~BQ24160_IUSB_LIMIT_MASK;
			data |=  BQ24160_IUSB_LIMIT_USB100;
		}
		break;
	}
	ret = bq24160_i2c_write(bq, BQ24160_CONTROL, data);
	if (ret < 0) {
		dev_err(bq->dev, "could not write control register\n");
		return ret;
	}

	return 0;
}
#endif /* CONFIG_USB_MV_OTG */

#ifdef CONFIG_OF
static struct bq24160_platform_data * bq24160_probe_dt(
	struct i2c_client *client)
{
	struct bq24160_platform_data *pdata = NULL;

	pdata = kzalloc(sizeof(struct bq24160_platform_data), GFP_KERNEL);
	if (pdata == NULL)
		return NULL;
	client->dev.platform_data = pdata;

	pdata->interval = HZ * BATTERY_UPDATE_INTERVAL;

#ifdef CONFIG_USB_MV_OTG
	pdata->gpio_vbst = of_get_named_gpio(client->dev.of_node, "otg_vbst_gpio", 0);
	pdata->gpio_vbus = of_get_named_gpio(client->dev.of_node, "otg_vbus_gpio", 0);
#endif /* CONFIG_USB_MV_OTG */

	if (of_property_read_u32(client->dev.of_node, "edge-wakeup-gpio", &pdata->edge_wakeup_gpio))
		dev_err(&client->dev, "no edge-wakeup-gpio defined\n");

	return pdata;
}
#endif /* CONFIG_OF */

static int bq24160_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	enable_irq_wake(client->irq);

	return 0;
}

static int bq24160_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	disable_irq_wake(client->irq);

	return 0;
}

/* main bq24160 probe function */
static int bq24160_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int ret;
	struct bq24160_device *bq;
	struct bq24160_platform_data *pdata = client->dev.platform_data;

#ifdef CONFIG_OF
	pdata = bq24160_probe_dt(client);
#endif /* CONFIG_OF */

	if (!pdata) {
		dev_err(&client->dev, "platform data not set\n");
		return -ENODEV;
	}

	bq = devm_kzalloc(&client->dev, sizeof(*bq), GFP_KERNEL);
	if (!bq) {
		dev_err(&client->dev, "failed to allocate device data\n");
		return -ENOMEM;
	}

	i2c_set_clientdata(client, bq);

	bq->dev = &client->dev;

	bq->interval= pdata->interval;

	ret = bq24160_read_status(bq);
	if (ret) {
		dev_err(bq->dev, "failed to read status: %d\n", ret);
		goto error;
	}

	ret = bq24160_power_supply_init(bq);
	if (ret) {
		dev_err(bq->dev, "failed to register power supply: %d\n", ret);
		goto error;
	}

	bq->bq24160_wq = create_singlethread_workqueue("bq24160_wq");
	if (!bq->bq24160_wq) {
		printk(KERN_ERR " %s: create workqueue failed\n", __func__);
		ret = -ENOMEM;
		goto error;
	}
	INIT_WORK(&bq->workq, bq24160_workqueue);

	INIT_DELAYED_WORK(&bq->work_bq, bq24160_battery_work);
	schedule_delayed_work(&bq->work_bq, bq->interval);

	ret = request_threaded_irq(client->irq,
			NULL, bq24160_irq_thread,
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT,
			"BQ24160 INT", bq);
	if (ret) {
		dev_err(bq->dev, "Cannot request irq %d (%d)\n",
				client->irq, ret);
		goto error;
	}

	if (pdata->edge_wakeup_gpio >= 0) {
		ret = request_mfp_edge_wakeup(pdata->edge_wakeup_gpio, NULL,
						(void *)pdata, &client->dev);
		if (ret)
			dev_err(&client->dev, "failed to request edge wakeup.\n");
	}

	device_init_wakeup(&client->dev, 1);

#ifdef CONFIG_USB_MV_OTG
	bq->gpio_vbst = pdata->gpio_vbst;
	bq->gpio_vbus = pdata->gpio_vbus;
	global_bq = bq;
	if(vbus_on_before_probe != -1)
	{
		bq24160_set_vbus(vbus_on_before_probe);
		vbus_on_before_probe = -1;
	}

	bq->chg_notif.notifier_call = bq24160_chg_notifier_callback;
	ret = mv_udc_register_client(&bq->chg_notif);
	if (ret < 0) {
		dev_err(&client->dev, "failed to register client!\n");
		goto error;
	}
#endif /* CONFIG_USB_MV_OTG */

	dev_info(bq->dev, "driver registered\n");
	return 0;

error:
	bq24160_power_supply_exit(bq);

	return ret;
}

/* main bq24160 remove function */

static int bq24160_remove(struct i2c_client *client)
{
	struct bq24160_device *bq = i2c_get_clientdata(client);

	bq24160_power_supply_exit(bq);

#ifdef CONFIG_OF
	if (client->dev.platform_data != NULL) {
		kfree(client->dev.platform_data);
		client->dev.platform_data = NULL;
	}
#endif /* CONFIG_OF */

	dev_info(bq->dev, "driver unregistered\n");

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static struct dev_pm_ops bq24160_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(
		bq24160_suspend,
		bq24160_resume)
};
#endif /* CONFIG_PM_SLEEP */

static const struct i2c_device_id bq24160_i2c_id_table[] = {
	{ "bq24160", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, bq24160_i2c_id_table);

#ifdef CONFIG_OF
static struct of_device_id bq24160_of_id_table[] = {
	{ .compatible = "ti,bq24160", },
	{}
};
MODULE_DEVICE_TABLE(of, bq24160_of_id_table);
#endif /* CONFIG_OF */

static struct i2c_driver bq24160_driver = {
	.driver = {
		.name = "bq24160-charger",
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(bq24160_of_id_table),
#endif /* CONFIG_OF */
#ifdef CONFIG_PM_SLEEP
		.pm	= &bq24160_pm_ops,
#endif /* CONFIG_PM_SLEEP */
	},
	.probe = bq24160_probe,
	.remove = bq24160_remove,
	.id_table = bq24160_i2c_id_table,
};
module_i2c_driver(bq24160_driver);

MODULE_AUTHOR("STST");
MODULE_DESCRIPTION("bq24160 charger driver");
MODULE_LICENSE("GPL");
