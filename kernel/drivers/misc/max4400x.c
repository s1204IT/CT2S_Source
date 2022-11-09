/*
 * This file is part of the MAX4400x sensor driver.
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
#include <linux/i2c/max4400x.h>

/* Adc Output Scale , Bit Resolution*/
#define MAX4400X_FULL_SCALE_ADC8  0xFF
#define MAX4400X_FULL_SCALE_ADC10 0x3FF
#define MAX4400X_FULL_SCALE_ADC12  0xFFF
#define MAX4400X_FULL_SCALE_ADC14  0x3FFF

#define MAX4400X_CLEAR_DEF_THRESHI 0x6100 /* Register Low Byte 61, High 00 */
#define MAX4400X_CLEAR_DEF_THRESLO 0x6000 /* Reigster Low byte 60, High 00 */

#define MAX4400X_STARTUP_DEALY     25000  /* us */

struct max4400x_chip {
	struct max4400x_platform_data *pdata;
	struct i2c_client *client;
	struct input_dev *max_input_dev;
	struct mutex mutex;	/* avoid parallel access */

	atomic_t enabled;
	struct work_struct irq_work;
	/* Chip parameters */
	u8 persist;
	u8 mcfg;		/* Main Configuration */
	u8 acfg;		/* Ambient Configuration */
	u16 adc_fullscale;

	/* Clear info */
	u8 clear_level;
	u32 *clear_table;	/* Unit: nW/cm2 */
	u32 clear_adc_table[CLEAR_LEVEL_SIZE];/* Unit: ADC Count */
	u16 clear_lpc;
	u16 clear_adc;
	u32 clear;

};

static int max4400x_read_byte(struct max4400x_chip *chip, u8 reg, u8 *data)
{
	struct i2c_client *client = chip->client;
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		dev_err(&chip->client->dev, "i2c read byte fail!\n");
	*data = ret;
	return ret;
}

static int max4400x_read_word(struct max4400x_chip *chip, u8 reg, u16 *data)
{
	struct i2c_client *client = chip->client;
	int ret;

	ret = i2c_smbus_read_word_data(client, reg);

	if (ret < 0)
		dev_err(&chip->client->dev, "i2c read word fail!\n");
	*data = ret;
	return ret;
}

static int max4400x_write_byte(struct max4400x_chip *chip, u8 reg, u8 data)
{
	struct i2c_client *client = chip->client;
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, data);
	if (ret < 0)
		dev_err(&chip->client->dev, "i2c write byte fail!\n");
	return ret;
}

static int max4400x_write_word(struct max4400x_chip *chip, u8 reg, u16 data)
{
	struct i2c_client *client = chip->client;
	int ret;

	ret = i2c_smbus_write_word_data(client, reg, data);
	if (ret < 0)
		dev_err(&chip->client->dev, "i2c write word fail!\n");
	return ret;
}

/* Called always with mutex locked */
static int max4400x_refresh_thres(struct max4400x_chip *chip, u16 lo, u16 hi)
{
	int ret;
	/* If the chip is not in use, don't try to access it */
	if (pm_runtime_suspended(&chip->client->dev))
		return 0;

	ret = max4400x_write_word(chip, MAX4400X_LOTHRH, (lo << 8) | (lo >> 8));
	ret |=
	    max4400x_write_word(chip, MAX4400X_UPTHRH, (hi << 8) | (hi >> 8));

	return ret;
}

static int max4400x_report_clear(struct max4400x_chip *chip)
{
	u16 adc_value = 0;
	int i, level = 0;
	u16 thres_low, thres_high;

	max4400x_read_word(chip, MAX4400X_CLEARH, &adc_value);
	adc_value = (adc_value << 8) | (adc_value >> 8);

	for (i = 0; i < CLEAR_LEVEL_SIZE; i++) {
		if (adc_value <= (*(chip->clear_adc_table + i))) {
			level = i;
			if (*(chip->clear_adc_table + i))
				break;
		}
		if (i == CLEAR_LEVEL_MAX) {/* avoid i = CLEAR_LEVEL_MAX, since
				   'clear_adc_table' size is CLEAR_LEVEL_MAX */
			level = i;
			break;
		}
	}


	thres_low = *(chip->clear_adc_table + i - 1);
	thres_high = *(chip->clear_adc_table + i);
	if (i == 0 || adc_value == 0)
		thres_low = 0;
	if (i == 9)
		thres_high = chip->adc_fullscale;
	max4400x_refresh_thres(chip, thres_low, thres_high);

	chip->clear_adc = adc_value;
	chip->clear_level = level;
	chip->clear = chip->clear_lpc * adc_value;

	input_report_abs(chip->max_input_dev, ABS_MISC, level);
	input_sync(chip->max_input_dev);

	return chip->clear;
}

static void max4400x_irq_do_work(struct work_struct *work)
{
	struct max4400x_chip *chip =
	    container_of(work, struct max4400x_chip, irq_work);

	mutex_lock(&chip->mutex);
	switch (chip->mcfg & MCFG_AMBSEL_MASK) {
	case MCFG_AMBSEL_CLEAR:
		max4400x_report_clear(chip);
		break;
	case MCFG_AMBSEL_GREEN:
		break;
	case MCFG_AMBSEL_IR:
		break;
	case MCFG_AMBSEL_TEMP:
		break;
	default:
		break;
	}
	mutex_unlock(&chip->mutex);
}

static irqreturn_t max4400x_irq(int irq, void *data)
{
	struct max4400x_chip *chip = data;
	u8 status;

	/* Read to De-assert Interrupt */
	max4400x_read_byte(chip, MAX4400X_ISTATUS, &status);

	if (status & ISTATUS_AMBINTS)
		schedule_work(&chip->irq_work);

	return IRQ_HANDLED;
}

static int max4400x_update_adc_table(struct max4400x_chip *chip)
{
	uint32_t tmpdata[CLEAR_LEVEL_SIZE];
	int i;
	u8 ampga, ambtim;

	ampga = chip->acfg & 0x3;
	ambtim = (chip->acfg >> 2) & 0x7;

	switch (ampga) {
	case ACFG_AMBPGA00:
		chip->clear_lpc = 2;
		break;
	case ACFG_AMBPGA01:
		chip->clear_lpc = 8;
		break;
	case ACFG_AMBPGA02:
		chip->clear_lpc = 32;
		break;
	case ACFG_AMBPGA03:
		chip->clear_lpc = 512;
		break;
	default:
		break;
	}

	switch (ambtim) {
	case ACFG_AMBTIM_1X:
		chip->clear_lpc *= 1;
		chip->adc_fullscale = MAX4400X_FULL_SCALE_ADC14;
		break;
	case ACFG_AMBTIM_4X:
		chip->clear_lpc *= 4;
		chip->adc_fullscale = MAX4400X_FULL_SCALE_ADC12;
		break;
	case ACFG_AMBTIM_16X:
		chip->clear_lpc *= 16;
		chip->adc_fullscale = MAX4400X_FULL_SCALE_ADC10;
		break;
	case ACFG_AMBTIM_64X:
		chip->clear_lpc *= 64;
		chip->adc_fullscale = MAX4400X_FULL_SCALE_ADC8;
		break;
	default:
		break;
	}

	for (i = 0; i < CLEAR_LEVEL_SIZE; i++) {
		tmpdata[i] = (*(chip->clear_table + i)) / chip->clear_lpc;
		if (tmpdata[i] <= chip->adc_fullscale)
			chip->clear_adc_table[i] = (uint16_t) tmpdata[i];
		else
			chip->clear_adc_table[i] = chip->adc_fullscale;
	}

	return 0;
}

static int max4400x_configure(struct max4400x_chip *chip)
{
	/* Set Ambient Default Threshold to trigger an interrup */
	max4400x_write_word(chip, MAX4400X_UPTHRH, MAX4400X_CLEAR_DEF_THRESHI);
	max4400x_write_word(chip, MAX4400X_LOTHRH, MAX4400X_CLEAR_DEF_THRESLO);

	/* Set Consecutive Measurements required to trig an interrupt */
	max4400x_write_byte(chip, MAX4400X_TPST, chip->persist);

	/* Set Main and Ambient Configuraton register */
	max4400x_write_byte(chip, MAX4400X_ACFG, chip->acfg);

	max4400x_write_byte(chip, MAX4400X_MCFG, chip->mcfg);
	return 0;
}

static int max4400x_chip_on(struct max4400x_chip *chip)
{
	int err;
	u8 status;

	if (!atomic_cmpxchg(&chip->enabled, 0, 1)) {
		if (chip->pdata->power_on) {
			chip->pdata->power_on(1);
			/* Read to De-assert PWRON Interrupt */
			max4400x_read_byte(chip, MAX4400X_ISTATUS, &status);
		}

		/* Wake up device to normal state */
		err = max4400x_write_byte(chip, MAX4400X_ISTATUS, 0);
		if (err < 0) {
			atomic_set(&chip->enabled, 0);
			return err;
		}

	usleep_range(MAX4400X_STARTUP_DEALY, 2 * MAX4400X_STARTUP_DEALY);

	max4400x_configure(chip);
	}

	return 0;
}

static int max4400x_chip_off(struct max4400x_chip *chip)
{
	if (atomic_cmpxchg(&chip->enabled, 1, 0)) {
		max4400x_write_byte(chip, MAX4400X_ISTATUS, ISTATUS_SHDN);

		if (chip->pdata->power_on)
			chip->pdata->power_on(0);
	}
	return 0;
}

static ssize_t attr_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int ret = 0;
	struct max4400x_chip *chip = dev_get_drvdata(dev);

	ret = sprintf(buf, "%d\n", atomic_read(&chip->enabled));

	return ret;
}

static ssize_t attr_enable_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	unsigned long value;
	struct max4400x_chip *chip = dev_get_drvdata(dev);

	if (kstrtoul(buf, 10, &value))
		return -EINVAL;

	mutex_lock(&chip->mutex);
	if (value) {
		pm_runtime_get_sync(&chip->client->dev);
		max4400x_chip_on(chip);
	} else {
		max4400x_chip_off(chip);
	}
	mutex_unlock(&chip->mutex);
	return count;
}

static DEVICE_ATTR(enable, S_IRUGO | S_IWUSR,
		   attr_enable_show, attr_enable_store);

static ssize_t attr_clear_adc_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct max4400x_chip *chip = dev_get_drvdata(dev);
	ssize_t ret;
	u16 adc_value;

	mutex_lock(&chip->mutex);

	max4400x_read_word(chip, MAX4400X_CLEARH, &adc_value);
	adc_value = (adc_value << 8) | (adc_value >> 8);
	chip->clear_adc = adc_value;
	mutex_unlock(&chip->mutex);
	ret = sprintf(buf, "%d\n", chip->clear_adc);
	return ret;
}

static DEVICE_ATTR(clear_adc, S_IRUGO, attr_clear_adc_show, NULL);

static ssize_t attr_clear_lpc_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct max4400x_chip *chip = dev_get_drvdata(dev);
	ssize_t ret;

	ret = sprintf(buf, "%d\n", chip->clear_lpc);
	return ret;
}

static DEVICE_ATTR(clear_lpc, S_IRUGO, attr_clear_lpc_show, NULL);

static ssize_t attr_mcfg_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct max4400x_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%x\n", chip->mcfg);
}

static ssize_t attr_mcfg_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	unsigned long value = 0;
	struct max4400x_chip *chip = dev_get_drvdata(dev);
	if (kstrtoul(buf, 16, &value)) {
		pr_err("Invalid input mcfg value\n");
		return -EINVAL;
	}

	mutex_lock(&chip->mutex);
	chip->mcfg = value;
	max4400x_write_byte(chip, MAX4400X_MCFG, chip->mcfg);
	mutex_unlock(&chip->mutex);
	return count;
}

static DEVICE_ATTR(mcfg, S_IRUGO | S_IWUSR, attr_mcfg_show, attr_mcfg_store);

static ssize_t attr_acfg_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct max4400x_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%x\n", chip->acfg);
}

static ssize_t attr_acfg_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	unsigned long value = 0;
	struct max4400x_chip *chip = dev_get_drvdata(dev);
	if (kstrtoul(buf, 16, &value)) {
		pr_err("Invalid input acfg value\n");
		return -EINVAL;
	}
	mutex_lock(&chip->mutex);
	chip->acfg = value;
	max4400x_write_byte(chip, MAX4400X_ACFG, chip->acfg);
	max4400x_update_adc_table(chip);
	mutex_unlock(&chip->mutex);
	return count;
}

static DEVICE_ATTR(acfg, S_IRUGO | S_IWUSR, attr_acfg_show, attr_acfg_store);

static ssize_t attr_persist_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct max4400x_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%x\n", chip->persist);
}

static ssize_t attr_persist_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	unsigned long value = 0;
	struct max4400x_chip *chip = dev_get_drvdata(dev);
	if (kstrtoul(buf, 16, &value)) {
		pr_err("Invalid input persist value\n");
		return -EINVAL;
	}

	if (value < TPST_1X || value > TPST_16X) {
		pr_err("max4400x value is out of range/n");
		return 0;
	}

	mutex_lock(&chip->mutex);
	chip->persist = value;
	max4400x_write_byte(chip, MAX4400X_TPST, chip->persist);
	mutex_unlock(&chip->mutex);
	return count;
}

static DEVICE_ATTR(persist, S_IRUGO | S_IWUSR, attr_persist_show,
		   attr_persist_store);

static ssize_t attr_clear_table_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int i;
	unsigned length = 0;
	struct max4400x_chip *chip = dev_get_drvdata(dev);

	for (i = 0; i < 10; i++) {
		length += sprintf(buf + length,
				  "%d\n", *(chip->clear_table + i));
	}
	return length;
}

static ssize_t attr_clear_table_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	int i;
	char *token[CLEAR_LEVEL_SIZE];
	unsigned long int tempdata[CLEAR_LEVEL_SIZE];
	struct max4400x_chip *chip = dev_get_drvdata(dev);

	for (i = 0; i < CLEAR_LEVEL_SIZE; i++) {
		token[i] = strsep((char **)&buf, " ");
		if (kstrtoul(token[i], 10, &tempdata[i]))
			return -EINVAL;
		if (tempdata[i] < 1 || tempdata[i] > 0xffffffff) {
			pr_err("max4400x error clear_table[%d] =  %lu Err\n",
			       i, tempdata[i]);
			return count;
		}
	}
	mutex_lock(&chip->mutex);
	for (i = 0; i < CLEAR_LEVEL_SIZE; i++) {
		chip->clear_table[i] = tempdata[i];
		pr_debug("max4400x set chip->clear_table[%d] =  %d\n",
			i, *(chip->clear_table + i));
	}
	max4400x_update_adc_table(chip);
	mutex_unlock(&chip->mutex);

	return count;
}

static DEVICE_ATTR(clear_table, S_IRUGO | S_IWUSR, attr_clear_table_show,
		   attr_clear_table_store);

static ssize_t attr_clear_adc_table_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	int i;
	unsigned length = 0;
	struct max4400x_chip *chip = dev_get_drvdata(dev);

	for (i = 0; i < CLEAR_LEVEL_SIZE; i++) {
		length += sprintf(buf + length,
				  "%d\n", *(chip->clear_adc_table + i));
	}
	return length;
}

static DEVICE_ATTR(clear_adc_table, S_IRUGO, attr_clear_adc_table_show, NULL);

static ssize_t attr_clear_level_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct max4400x_chip *chip = dev_get_drvdata(dev);
	return sprintf(buf, "%d\n", chip->clear_level);
}

static ssize_t attr_clear_level_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	unsigned long value = 0;
	int level = -1;
	struct max4400x_chip *chip = dev_get_drvdata(dev);
	if (kstrtoul(buf, 10, &value))
		return -EINVAL;
	if (value < 10)
		level = value;
	input_report_abs(chip->max_input_dev, ABS_MISC, level);
	input_sync(chip->max_input_dev);

	level = -1;
	return count;
}

static DEVICE_ATTR(clear_level, S_IRUGO | S_IWUSR,
		   attr_clear_level_show, attr_clear_level_store);

static struct attribute *sysfs_attrs_ctrl[] = {
	&dev_attr_enable.attr,
	&dev_attr_clear_adc.attr,
	&dev_attr_clear_lpc.attr,
	&dev_attr_mcfg.attr,
	&dev_attr_acfg.attr,
	&dev_attr_persist.attr,
	&dev_attr_clear_table.attr,
	&dev_attr_clear_adc_table.attr,
	&dev_attr_clear_level.attr,
	NULL
};

static struct attribute_group max4400x_attribute_group[] = {
	{.attrs = sysfs_attrs_ctrl},
};

static int max4400x_probe(struct i2c_client *client,
				    const struct i2c_device_id *id)
{
	struct max4400x_chip *chip;
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

	/* Set confiure register value for max4400x */
	chip->clear_table = chip->pdata->clear_table;
	if (chip->pdata->mcfg)
		chip->mcfg = chip->pdata->mcfg;
	else
		chip->mcfg = MCFG_AMBINTE | MCFG_MODE_BASE | MCFG_AMBSEL_CLEAR;
	if (chip->pdata->acfg)
		chip->acfg = chip->pdata->acfg;
	else
		chip->acfg = ACFG_AMBPGA00 | ACFG_AMBTIM_1X;
	if (chip->pdata->persist)
		chip->persist = chip->pdata->persist;
	else
		chip->persist = TPST_1X;

	mutex_init(&chip->mutex);

	/* Init Input Device */
	chip->max_input_dev = input_allocate_device();
	if (!chip->max_input_dev) {
		pr_err("max4400x allocate input dev fail\n");
		goto fail2;
	}

	chip->max_input_dev->name = "max4400x";
	input_set_drvdata(chip->max_input_dev, chip);

	set_bit(EV_ABS, chip->max_input_dev->evbit);
	input_set_abs_params(chip->max_input_dev, ABS_MISC, 0, 9, 0, 0);

	err = input_register_device(chip->max_input_dev);
	if (err < 0) {
		pr_err("max4400x register input dev fail\n");
		goto fail3;
	}

	if (chip->pdata->power_on) {
		err = chip->pdata->power_on(1);
		if (err < 0) {
			dev_err(&client->dev, "power on failed\n");
			goto fail4;
		}
		/* Read to De-assert PWRON Interrupt */
		max4400x_read_byte(chip, MAX4400X_ISTATUS, &status);
	}

	usleep_range(MAX4400X_STARTUP_DEALY, 2 * MAX4400X_STARTUP_DEALY);

	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);

	max4400x_configure(chip);
	max4400x_update_adc_table(chip);

	atomic_set(&chip->enabled, 1);
	INIT_WORK(&chip->irq_work, max4400x_irq_do_work);

	err = sysfs_create_group(&chip->max_input_dev->dev.kobj,
				 max4400x_attribute_group);
	if (err < 0) {
		dev_err(&chip->max_input_dev->dev, "Sysfs registration failed\n");
		goto fail5;
	}

	err = request_threaded_irq(client->irq, NULL,
				   max4400x_irq,
				   IRQF_TRIGGER_FALLING | IRQF_TRIGGER_LOW |
				   IRQF_ONESHOT, "max4400x", chip);
	if (err) {
		dev_err(&client->dev, "could not get IRQ %d\n", client->irq);
		goto fail6;
	}

	max4400x_chip_off(chip);

	return err;
fail6:
	sysfs_remove_group(&chip->max_input_dev->dev.kobj,
			   &max4400x_attribute_group[0]);
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

static int max4400x_remove(struct i2c_client *client)
{
	struct max4400x_chip *chip = i2c_get_clientdata(client);

	free_irq(client->irq, chip);
	sysfs_remove_group(&chip->max_input_dev->dev.kobj,
					max4400x_attribute_group);

	if (!pm_runtime_suspended(&client->dev))
		max4400x_chip_off(chip);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	input_unregister_device(chip->max_input_dev);
	input_free_device(chip->max_input_dev);
	kfree(chip);
	return 0;
}

#ifdef CONFIG_PM
static int max4400x_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct max4400x_chip *chip = i2c_get_clientdata(client);

	max4400x_chip_off(chip);
	return 0;
}

static int max4400x_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct max4400x_chip *chip = i2c_get_clientdata(client);

	/*
	 * If we were enabled at suspend time, it is expected
	 * everything works nice and smoothly. Chip_on is enough
	 */
	max4400x_chip_on(chip);

	return 0;
}
#endif

#ifdef CONFIG_PM_RUNTIME
static int max4400x_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct max4400x_chip *chip = i2c_get_clientdata(client);
	max4400x_chip_off(chip);
	return 0;
}

static int max4400x_runtime_resume(struct device *dev)
{
	struct i2c_client *client = container_of(dev, struct i2c_client, dev);
	struct max4400x_chip *chip = i2c_get_clientdata(client);

	max4400x_chip_on(chip);
	return 0;
}
#endif

static const struct i2c_device_id max4400x_id[] = {
	{"max4400x", 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, max4400x_id);

static const struct dev_pm_ops max4400x_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(max4400x_suspend, max4400x_resume)
	SET_RUNTIME_PM_OPS(max4400x_runtime_suspend,
				max4400x_runtime_resume,
				NULL)
};

static struct i2c_driver max4400x_driver = {
	.driver = {
		   .name = "max4400x",
		   .owner = THIS_MODULE,
		   .pm = &max4400x_pm_ops,
		   },
	.probe = max4400x_probe,
	.remove = max4400x_remove,
	.id_table = max4400x_id,
};

module_i2c_driver(max4400x_driver);

MODULE_DESCRIPTION("MAX4400X combined ALS and RGB color sensor");
MODULE_AUTHOR("Shouming Wang <wangshm@marvell.com>");
MODULE_LICENSE("GPL v2");
