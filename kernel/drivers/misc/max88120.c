/*
 * This file is part of the MAX88120 sensor driver.
 * Chip is combined ambient light and RGB color sensor.
 *
 * Only Clear Channel has been implemented, other channles
 * need to complete in the futrue.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/i2c/max88120.h>

#define MAX88120_STARTUP_DEALY     25000	/* us */

#define MAX88120_PROX_DEF_THRES	   100
#define	MAX88120_PROX_HYSTERESIS   10
#define MAX88120_PROX_RANGE	   255

struct max88120_chip {
	struct max88120_platform_data *pdata;
	struct i2c_client *client;
	struct input_dev *max_input_dev;
	struct mutex mutex;	/* avoid parallel access */

	atomic_t enabled;
	u8 prox_continuous_mode;
	struct work_struct irq_work;
	/* Chip parameters */
	u8 persist;
	u8 mcfg;		/* Main Configuration */
	u8 adcfg;		/* ADC Configuration */
	u8 ledcfg;		/* LED Driver Configuration */

	/* Gesture and Proximity data */
	u8 prox_data;
	/**
	 * If prox_data > prox_thres <=> Near to Far
	 * If prox_data < prox_thres <=> Far to Near
	 */
	u8 prox_thres;
};

static int max88120_read_byte(struct max88120_chip *chip, u8 reg, u8 *data)
{
	struct i2c_client *client = chip->client;
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		dev_err(&chip->client->dev, "i2c read byte fail\n");
	*data = ret;
	return ret;
}

static int max88120_write_byte(struct max88120_chip *chip, u8 reg, u8 data)
{
	struct i2c_client *client = chip->client;
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, data);
	if (ret < 0)
		dev_err(&chip->client->dev, "i2c write byte fail\n");
	return ret;
}

/* Called always with mutex locked */
static int max88120_refresh_thres(struct max88120_chip *chip, u8 data)
{
	int ret;
	u8 lo, hi;

	/* If the chip is not in use, don't try to access it */
	if (pm_runtime_suspended(&chip->client->dev))
		return 0;

	if (data < chip->prox_thres) {
		lo = 0;
		hi = chip->prox_thres;
	} else {
		lo = chip->prox_thres - MAX88120_PROX_HYSTERESIS;
		if (chip->prox_continuous_mode)
			hi = chip->prox_thres;
		else
			hi = MAX88120_PROX_RANGE;
	}

	ret = max88120_write_byte(chip, MAX88120_UPTHR, hi);
	ret = max88120_write_byte(chip, MAX88120_LOTHR, lo);

	return ret;
}

static void max88120_irq_do_work(struct work_struct *work)
{
	struct max88120_chip *chip =
	    container_of(work, struct max88120_chip, irq_work);
	u8 reg, value;

	/* Set Sample Delay to 6.25ms to Increase Sample Rate */
	reg = ADCFG_SDLY_6P25 | ADCFG_ALWAYS_SET1;
	max88120_write_byte(chip, MAX88120_ADCFG, reg);

	/* Polling the Sensor for Data */
	max88120_read_byte(chip, MAX88120_ADCD, &value);

	mutex_lock(&chip->mutex);
	chip->prox_data = value;

	max88120_refresh_thres(chip, chip->prox_data);
	if (chip->prox_data < chip->prox_thres)
		chip->prox_data = 0;	/* Near to Far */
	else if (!chip->prox_continuous_mode)
		chip->prox_data = MAX88120_PROX_RANGE;	/* Far to Near */

	input_report_abs(chip->max_input_dev, ABS_DISTANCE, chip->prox_data);
	input_sync(chip->max_input_dev);

	/* Set Sample Delay back to 100ms */
	reg = ADCFG_SDLY_100 | ADCFG_ALWAYS_SET1;
	max88120_write_byte(chip, MAX88120_ADCFG, reg);
	mutex_unlock(&chip->mutex);
}

static irqreturn_t max88120_irq(int irq, void *data)
{
	struct max88120_chip *chip = data;
	u8 status;

	/* Read to De-assert Interrupt */
	max88120_read_byte(chip, MAX88120_STATUS, &status);

	if (status & STATUS_GESINTS)
		schedule_work(&chip->irq_work);

	return IRQ_HANDLED;
}

static int max88120_configure(struct max88120_chip *chip)
{
	/* Set Prox Default Threshold to trigger an interrup */
	max88120_write_byte(chip, MAX88120_UPTHR, MAX88120_PROX_DEF_THRES);
	max88120_write_byte(chip, MAX88120_LOTHR, MAX88120_PROX_DEF_THRES - 1);

	/* Set Consecutive Measurements required to trig an interrupt */
	max88120_write_byte(chip, MAX88120_INTPST, chip->persist);

	/* Set ADC Configuraton register */
	max88120_write_byte(chip, MAX88120_ADCFG, chip->adcfg);

	/* Set LED Configuraton register */
	max88120_write_byte(chip, MAX88120_LEDCFG, chip->ledcfg);

	/* Set Main Configuraton register */
	max88120_write_byte(chip, MAX88120_MCFG, chip->mcfg);

	return 0;
}

static int max88120_chip_on(struct max88120_chip *chip)
{
	int err;
	u8 status, reg;

	if (!atomic_cmpxchg(&chip->enabled, 0, 1)) {
		if (chip->pdata->power_on) {
			chip->pdata->power_on(1);
			/* Read to De-assert PWRON Interrupt */
			max88120_read_byte(chip, MAX88120_STATUS, &status);
		}

		/* Wake up device to normal state */
		reg = chip->mcfg & ~MCFG_SHDN;
		err = max88120_write_byte(chip, MAX88120_MCFG, reg);
		if (err < 0) {
			atomic_set(&chip->enabled, 0);
			return err;
		}

		usleep_range(MAX88120_STARTUP_DEALY,
			     2 * MAX88120_STARTUP_DEALY);

		max88120_configure(chip);
	}

	return 0;
}

static int max88120_chip_off(struct max88120_chip *chip)
{
	if (atomic_cmpxchg(&chip->enabled, 1, 0)) {
		max88120_write_byte(chip, MAX88120_MCFG, MCFG_SHDN);

		if (chip->pdata->power_on)
			chip->pdata->power_on(0);
	}
	return 0;
}

static ssize_t attr_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct max88120_chip *chip = dev_get_drvdata(dev);

	ret = sprintf(buf, "%d\n", atomic_read(&chip->enabled));

	return ret;
}

static ssize_t attr_enable_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	unsigned long value;
	struct max88120_chip *chip = dev_get_drvdata(dev);

	if (kstrtoul(buf, 10, &value)) {
		pr_err("Invalid input enable value\n");
		return -EINVAL;
	}

	mutex_lock(&chip->mutex);
	if (value) {
		pm_runtime_get_sync(&chip->client->dev);
		max88120_chip_on(chip);
	} else {
		max88120_chip_off(chip);
	}
	mutex_unlock(&chip->mutex);
	return count;
}

static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR,
		   attr_enable_show, attr_enable_store);

static ssize_t attr_prox_data_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct max88120_chip *chip = dev_get_drvdata(dev);
	ssize_t ret;

	ret = sprintf(buf, "%d\n", chip->prox_data);
	return ret;
}

static DEVICE_ATTR(prox_data, S_IRUGO, attr_prox_data_show, NULL);

static const char reporting_modes[][9] = { "trigger", "periodic" };

static ssize_t attr_prox_reporting_mode_show(struct device *dev,
					     struct device_attribute *attr,
					     char *buf)
{
	struct max88120_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%s\n",
		       reporting_modes[!!chip->prox_continuous_mode]);
}

static ssize_t attr_prox_reporting_mode_store(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	struct max88120_chip *chip = dev_get_drvdata(dev);

	mutex_lock(&chip->mutex);
	if (sysfs_streq(buf, reporting_modes[0]))
		chip->prox_continuous_mode = 0;
	else if (sysfs_streq(buf, reporting_modes[1]))
		chip->prox_continuous_mode = 1;
	else
		return -EINVAL;
	mutex_unlock(&chip->mutex);
	return count;
}

static DEVICE_ATTR(prox_reporting_mode, S_IRUGO | S_IWUSR,
		   attr_prox_reporting_mode_show,
		   attr_prox_reporting_mode_store);

static ssize_t max88120_prox_reporting_avail_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s %s\n", reporting_modes[0], reporting_modes[1]);
}

static DEVICE_ATTR(prox_reporting_mode_avail, S_IRUGO | S_IWUSR,
		max88120_prox_reporting_avail_show, NULL);

static ssize_t attr_prox_threshold_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct max88120_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->prox_thres);
}

static ssize_t attr_prox_threshold_store(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct max88120_chip *chip = dev_get_drvdata(dev);
	unsigned long value;

	if (kstrtoul(buf, 0, &value)) {
		pr_err("Invalid input threshold value\n");
		return -EINVAL;
	}

	if ((value > MAX88120_PROX_RANGE) || (value == 0) ||
	    (value < MAX88120_PROX_HYSTERESIS)) {
		pr_err("Over limit threshold range\n");
		return -EINVAL;
	}

	mutex_lock(&chip->mutex);
	chip->prox_thres = value;

	max88120_write_byte(chip, MAX88120_UPTHR, MAX88120_PROX_DEF_THRES);
	max88120_write_byte(chip, MAX88120_LOTHR, MAX88120_PROX_DEF_THRES - 1);

	mutex_unlock(&chip->mutex);
	return count;
}

static DEVICE_ATTR(prox_thres_above_value, S_IRUGO | S_IWUSR,
		   attr_prox_threshold_show, attr_prox_threshold_store);

static ssize_t attr_mcfg_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct max88120_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%x\n", chip->mcfg);
}

static ssize_t attr_mcfg_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	unsigned long value = 0;
	struct max88120_chip *chip = dev_get_drvdata(dev);
	if (kstrtoul(buf, 16, &value)) {
		return -EINVAL;
		pr_err("Invalid input mcfg value\n");
	}

	mutex_lock(&chip->mutex);
	chip->mcfg = value;
	max88120_write_byte(chip, MAX88120_MCFG, chip->mcfg);
	mutex_unlock(&chip->mutex);
	return count;
}

static DEVICE_ATTR(mcfg, S_IRUGO | S_IWUSR, attr_mcfg_show, attr_mcfg_store);

static ssize_t attr_adcfg_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct max88120_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%x\n", chip->adcfg);
}

static ssize_t attr_adcfg_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	unsigned long value = 0;
	struct max88120_chip *chip = dev_get_drvdata(dev);
	if (kstrtoul(buf, 16, &value)) {
		pr_err("Invalid input adcfg value\n");
		return -EINVAL;
	}

	mutex_lock(&chip->mutex);
	chip->adcfg = value;
	max88120_write_byte(chip, MAX88120_ADCFG, chip->adcfg);
	mutex_unlock(&chip->mutex);
	return count;
}

static DEVICE_ATTR(adcfg, S_IRUGO | S_IRUSR, attr_adcfg_show, attr_adcfg_store);

static ssize_t attr_ledcfg_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct max88120_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%x\n", chip->ledcfg);
}

static ssize_t attr_ledcfg_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	unsigned long value = 0;
	struct max88120_chip *chip = dev_get_drvdata(dev);
	if (kstrtoul(buf, 16, &value)) {
		pr_err("Invalid input ledcfg value\n");
		return -EINVAL;
	}

	mutex_lock(&chip->mutex);
	chip->ledcfg = value;
	max88120_write_byte(chip, MAX88120_LEDCFG, chip->adcfg);
	mutex_unlock(&chip->mutex);
	return count;
}

static DEVICE_ATTR(ledcfg, S_IRUGO | S_IRUSR, attr_ledcfg_show,
		   attr_ledcfg_store);

static ssize_t attr_persist_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct max88120_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%x\n", chip->persist);
}

static ssize_t attr_persist_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	unsigned long value = 0;
	struct max88120_chip *chip = dev_get_drvdata(dev);

	if (kstrtoul(buf, 16, &value)) {
		pr_err("Invalid input mcfg value\n");
		return -EINVAL;
	}

	if (value < INTPST_NUM1 || value > INTPST_NUM8) {
		pr_err("max88120 value is out of range/n");
		return 0;
	}

	mutex_lock(&chip->mutex);
	chip->persist = value;

	max88120_write_byte(chip, MAX88120_INTPST, chip->persist);
	mutex_unlock(&chip->mutex);
	return count;
}

static DEVICE_ATTR(persist, S_IRUGO | S_IWUSR, attr_persist_show,
		   attr_persist_store);

static struct attribute *sysfs_attrs_ctrl[] = {
	&dev_attr_enable.attr,
	&dev_attr_prox_data.attr,
	&dev_attr_prox_reporting_mode.attr,
	&dev_attr_prox_reporting_mode_avail.attr,
	&dev_attr_prox_thres_above_value.attr,
	&dev_attr_mcfg.attr,
	&dev_attr_adcfg.attr,
	&dev_attr_ledcfg.attr,
	&dev_attr_persist.attr,
	NULL
};

static struct attribute_group max88120_attribute_group[] = {
	{.attrs = sysfs_attrs_ctrl},
};

static int max88120_probe(struct i2c_client *client,
				    const struct i2c_device_id *id)
{
	struct max88120_chip *chip;
	int err = -1;
	u8 status;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	i2c_set_clientdata(client, chip);
	chip->client = client;
	chip->pdata = client->dev.platform_data;
	if (chip->pdata == NULL) {
		dev_err(&client->dev, "platform data is mandatory\n");
		err = -EINVAL;
		goto fail1;
	}

	/* Set confiure register value for max88120 */
	chip->mcfg = MCFG_GESINTE | MCFG_MODE_PROX;
	chip->adcfg = ADCFG_SDLY_100 | ADCFG_ALWAYS_SET1;
	chip->ledcfg = LEDCFG_DRV_100 | LEDCFG_WID_96;
	chip->persist = INTPST_NUM1;
	chip->prox_thres = MAX88120_PROX_DEF_THRES;
	mutex_init(&chip->mutex);

	/* Init Input Device */
	chip->max_input_dev = input_allocate_device();
	if (!chip->max_input_dev) {
		pr_err("max88120 allocate input dev fail\n");
		goto fail2;
	}

	chip->max_input_dev->name = "max88120";
	input_set_drvdata(chip->max_input_dev, chip);

	set_bit(EV_ABS, chip->max_input_dev->evbit);
	input_set_abs_params(chip->max_input_dev, ABS_DISTANCE, 0, 255, 0, 0);

	err = input_register_device(chip->max_input_dev);
	if (err < 0) {
		pr_err("max88120 register input dev fail\n");
		goto fail3;
	}

	if (chip->pdata->power_on) {
		err = chip->pdata->power_on(1);
		if (err < 0) {
			dev_err(&client->dev, "power on failed\n");
			goto fail4;
		}
		/* Read to De-assert PWRON Interrupt */
		max88120_read_byte(chip, MAX88120_STATUS, &status);
	}

	usleep_range(MAX88120_STARTUP_DEALY, 2 * MAX88120_STARTUP_DEALY);

	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);

	max88120_configure(chip);

	atomic_set(&chip->enabled, 1);
	INIT_WORK(&chip->irq_work, max88120_irq_do_work);

	err = sysfs_create_group(&chip->max_input_dev->dev.kobj,
				 max88120_attribute_group);
	if (err < 0) {
		dev_err(&chip->client->dev, "Sysfs registration failed\n");
		goto fail5;
	}

	err = request_threaded_irq(client->irq, NULL,
				   max88120_irq,
				   IRQF_TRIGGER_FALLING | IRQF_TRIGGER_LOW |
				   IRQF_ONESHOT, "max88120", chip);
	if (err) {
		dev_err(&client->dev, "could not get IRQ %d\n", client->irq);
		goto fail6;
	}

	max88120_chip_off(chip);

	return err;
fail6:
	sysfs_remove_group(&chip->max_input_dev->dev.kobj,
			   &max88120_attribute_group[0]);
fail5:
	if (chip->pdata->power_on)
		chip->pdata->power_on(0);
fail4:
	input_unregister_device(chip->max_input_dev);
fail3:
	input_free_device(chip->max_input_dev);
fail2:
	mutex_destroy(&chip->mutex);
fail1:
	kfree(chip);
	return err;
}

static int max88120_remove(struct i2c_client *client)
{
	struct max88120_chip *chip = i2c_get_clientdata(client);

	free_irq(client->irq, chip);
	sysfs_remove_group(&chip->max_input_dev->dev.kobj,
					max88120_attribute_group);

	if (!pm_runtime_suspended(&client->dev))
		max88120_chip_off(chip);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	input_unregister_device(chip->max_input_dev);
	input_free_device(chip->max_input_dev);
	kfree(chip);
	return 0;
}

static int max88120_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct max88120_chip *chip = i2c_get_clientdata(client);

	max88120_chip_off(chip);
	return 0;
}

static int max88120_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct max88120_chip *chip = i2c_get_clientdata(client);

	/*
	 * If we were enabled at suspend time, it is expected
	 * everything works nice and smoothly. Chip_on is enough
	 */
	max88120_chip_on(chip);

	return 0;
}

#ifdef CONFIG_PM_RUNTIME
static int max88120_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct max88120_chip *chip = i2c_get_clientdata(client);

	max88120_chip_off(chip);
	return 0;
}

static int max88120_runtime_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct max88120_chip *chip = i2c_get_clientdata(client);

	max88120_chip_on(chip);
	return 0;
}
#endif

static const struct i2c_device_id max88120_id[] = {
	{"max88120", 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, max88120_id);

static const struct dev_pm_ops max88120_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(max88120_suspend, max88120_resume)
	    SET_RUNTIME_PM_OPS(max88120_runtime_suspend,
			       max88120_runtime_resume, NULL)
};

static struct i2c_driver max88120_driver = {
	.driver = {
		   .name = "max88120",
		   .owner = THIS_MODULE,
		   .pm = &max88120_pm_ops,
		   },
	.probe = max88120_probe,
	.remove = max88120_remove,
	.id_table = max88120_id,
};

module_i2c_driver(max88120_driver);

MODULE_DESCRIPTION("MAX88120 combined Gesture and Proximity color sensor");
MODULE_AUTHOR("Shouming Wang <wangshm@marvell.com>");
MODULE_LICENSE("GPL v2");
