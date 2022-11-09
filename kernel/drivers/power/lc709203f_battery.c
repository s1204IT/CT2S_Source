/*
 *  lc709203f_battery.c
 *  Battery Monitor IC for 1 cell Li+ battery
 *
 *  Copyright (C) 2014-2015 SANYO Techno Solutions Tottori Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/interrupt.h>

#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#endif /* CONFIG_OF */

//#define DEBUG
//#define DEBUG2

#define LC709203F_TEMP_REG	0x08
#define LC709203F_VCELL_REG	0x09
#define LC709203F_SOC_REG	0x0d
#define LC709203F_INDI_REG	0x0f
#define LC709203F_VERSION_REG	0x11
#define LC709203F_ALARMSOC_REG	0x13
#define ALARMSOC 0x07 /*7%*/

#define RESCALE_SOC
#define INDI_TOP 910
#define INDI_BOTTOM 70

struct lc709203f_chip {
	struct i2c_client		*client;
	int vcell;
	int soc;
	int init;
	int temp;
};

static struct lc709203f_chip chip_st;

static u8 lc709203f_crc8(u8 before,u8 after)
{
	int i;
	u16 TmpValue = 0;

	TmpValue = (u16)(before ^ after);
	TmpValue <<= 8;

	for(i = 0 ; i < 8 ; i++)
	{
		if(TmpValue & 0x8000)
		{
			TmpValue ^= 0x8380;
		}
		TmpValue <<= 1;
	}

	return TmpValue >> 8;
}

static int lc709203f_write_reg(struct i2c_client *client, u8 reg, u16 value)
{
	s32 ret;
	u8 buf[3];
	u8 crc8;

	buf[0] = value & 0x00ff;
	buf[1] = value >> 8;
	crc8 = lc709203f_crc8(0,0x16);
	crc8 = lc709203f_crc8(crc8,reg);
	crc8 = lc709203f_crc8(crc8,buf[0]);
	crc8 = lc709203f_crc8(crc8,buf[1]);
	buf[2] = crc8;

#ifdef DEBUG2
	printk("write reg %02x,data %02x%02x\n",reg,buf[1],buf[0]);
#endif
	ret = i2c_smbus_write_i2c_block_data(client, reg, 3 ,buf);

	if (ret < 0)
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);

	return ret;
}

static int lc709203f_read_reg(struct i2c_client *client, u8 reg , u16 *value)
{
	u8 buf[3];
	u8 crc8;
	int ret;

	ret = i2c_smbus_read_i2c_block_data(client, reg, 3, buf);
	if (ret < 0)
	{
		dev_err(&client->dev, "%s: err %d\n", __func__, ret);
		return ret;
	}

	crc8 = lc709203f_crc8(0,0x16);
	crc8 = lc709203f_crc8(crc8,reg );
	crc8 = lc709203f_crc8(crc8,0x17);
	crc8 = lc709203f_crc8(crc8,buf[0]);
	crc8 = lc709203f_crc8(crc8,buf[1]);

	if(crc8 == buf[2])
	{
		*value = ((u16)buf[1] << 8) | buf[0];
#ifdef DEBUG2
		printk("read reg %02x,data %02x%02x\n",reg,*value);
#endif
	}
	else
	{
		printk("crc error\n");
		ret = -1;
	}

	return ret;
}

int lc709203f_get_vcell_external(void)
{
	u16 value;
	int ret;

	if(chip_st.init == 0)
	{
		return 4000;
	}

	ret = lc709203f_read_reg(chip_st.client, LC709203F_VCELL_REG, &value);
	if(ret < 0)
	{
		chip_st.init = 0;
		return 4000;
	}
	chip_st.vcell = value;

#ifdef DEBUG
	printk("vcell %04x %dmv\n",value,chip_st.vcell);
#endif

	return chip_st.vcell;
}

#ifdef RESCALE_SOC
int lc709203f_get_soc_external(void)
{
	u16 value;
	int ret;

	if(chip_st.init == 0)
	{
		return 100;
	}

	ret = lc709203f_read_reg(chip_st.client, LC709203F_INDI_REG, &value);
	if(ret < 0)
	{
		chip_st.init = 0;
		return 100;
	}

	if(value >= INDI_TOP)
	{
		chip_st.soc = 100;
	}
	else if(value <= INDI_BOTTOM)
	{
		chip_st.soc = 0;
	}
	else
	{
		chip_st.soc = (int)(value - INDI_BOTTOM) * 1000 / (1000 - INDI_BOTTOM - (1000 - INDI_TOP));
		chip_st.soc /= 10;
	}

#ifdef DEBUG
	printk("soc %d%% rescale %d%%\n",value / 10 ,chip_st.soc);
#endif

	return chip_st.soc;
//	return 100;
}
#else
int lc709203f_get_soc_external(void)
{
	u16 value;
	int ret;

	if(chip_st.init == 0)
	{
		return 100;
	}

	ret = lc709203f_read_reg(chip_st.client, LC709203F_SOC_REG, &value);
	if(ret < 0)
	{
		chip_st.init = 0;
		return 100;
	}

	chip_st.soc = value;

#ifdef DEBUG
	printk("soc %04x %d%%\n",value ,chip_st.soc);
#endif

	return chip_st.soc;
}
#endif

int lc709203f_get_temp_external(void)
{
	u16 value;
	int ret;

	if(chip_st.init == 0)
	{
		return 200;
	}

	ret = lc709203f_read_reg(chip_st.client, LC709203F_TEMP_REG, &value);
	if(ret < 0)
	{
		chip_st.init = 0;
		return 200;
	}

	chip_st.temp = value - 2732;

#ifdef DEBUG
	printk("temp %04x %d.%d\n",value ,chip_st.temp/10,chip_st.temp%10);
#endif

	return chip_st.temp;
}

static irqreturn_t lc709203f_irq_thread(int irq, void *data)
{
	struct lc709203f_chip *chip = data;

#ifdef DEBUG
	printk("lc709203f_irq_thread\n");
#endif

	disable_irq_nosync(irq);

	return IRQ_RETVAL(0);
}

static int lc709203f_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct lc709203f_chip *chip;
	int ret;
	u16 reg;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_I2C_BLOCK))
		return -EIO;

	chip = &chip_st;
	chip->client = client;
	chip->init = 0;

	i2c_set_clientdata(client, chip);

	ret = lc709203f_read_reg(client, LC709203F_VERSION_REG ,&reg);
	if(ret < 0)
	{
		return ret;
	}
	printk("lc709203f version %04x\n",reg);

	chip->init = 1;

	/*battery alart*/
	ret = lc709203f_write_reg(client, LC709203F_ALARMSOC_REG, ALARMSOC);
	if(ret < 0)
	{
		return ret;
	}

	if(client->irq)
	{
		printk("lc709203f_probe irq %d\n",client->irq);
		ret = request_threaded_irq(client->irq, NULL, lc709203f_irq_thread,
				     IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				     "lc709203f", chip);
		if (ret) {
			dev_err(&client->dev, "Failed to register interrupt\n");
		}
	}

	return 0;
}

static int lc709203f_remove(struct i2c_client *client)
{
	struct lc709203f_chip *chip = i2c_get_clientdata(client);

	if(client->irq)
	{
		free_irq(client->irq, chip);
	}
	return 0;
}

#ifdef CONFIG_PM

static int lc709203f_suspend(struct i2c_client *client,
		pm_message_t state)
{
	struct lc709203f_chip *chip = i2c_get_clientdata(client);

	if(client->irq)
	{
		enable_irq_wake(client->irq);
	}

	return 0;
}

static int lc709203f_resume(struct i2c_client *client)
{
	struct lc709203f_chip *chip = i2c_get_clientdata(client);

	if(client->irq)
	{
		disable_irq_wake(client->irq);
	}

	return 0;
}

#else

#define lc709203f_suspend NULL
#define lc709203f_resume NULL

#endif /* CONFIG_PM */

static const struct i2c_device_id lc709203f_id[] = {
	{ "lc709203f", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lc709203f_id);

#ifdef CONFIG_OF
static struct of_device_id lc709203f_of_id_table[] = {
	{ .compatible = "onsemi,lc709203f", },
	{}
};
MODULE_DEVICE_TABLE(of, lc709203f_of_id_table);
#endif /* CONFIG_OF */

static struct i2c_driver lc709203f_i2c_driver = {
	.driver	= {
		.name	= "lc709203f",
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(lc709203f_of_id_table),
#endif /* CONFIG_OF */
	},
	.probe		= lc709203f_probe,
	.remove		= lc709203f_remove,
	.suspend	= lc709203f_suspend,
	.resume		= lc709203f_resume,
	.id_table	= lc709203f_id,
};

static int __init lc709203f_init(void)
{
	return i2c_add_driver(&lc709203f_i2c_driver);
}
module_init(lc709203f_init);

static void __exit lc709203f_exit(void)
{
	i2c_del_driver(&lc709203f_i2c_driver);
}
module_exit(lc709203f_exit);

MODULE_AUTHOR("Sanyo Ltd..");
MODULE_DESCRIPTION("LC709203F");
MODULE_LICENSE("GPL");
