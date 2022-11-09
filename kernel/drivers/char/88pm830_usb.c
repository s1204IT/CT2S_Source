/*
 * 88pm830 VBus driver for Marvell USB
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/mfd/88pm830.h>
#include <linux/delay.h>
#include <linux/of_device.h>
#include <linux/platform_data/mv_usb.h>

#define PM830_OTG_CTRL1		(0x59)
#define OTG_VOLT_MASK		(0x7)
#define OTG_VOLT_SET(x)		(x << 0)
#define PM830_OTG_CTRL2		(0x52)
#define OTG_SR_BYP_EN		(1 << 1)
#define PM830_OTG_CTRL3		(0x5B)
#define OTG_HIGH_SIDE_MASK	(0x7 << 5)
#define OTG_HIGH_SIDE_SET(x)	(x << 5)
#define OTG_LOW_SIDE_MASK	(0x7 << 1)
#define OTG_LOW_SIDE_SET(x)	(x << 1)
#define USB_OTG_MIN		4800 /* mV */
#define USB_INSERTION		4400 /* mV */

struct pm830_usb_info {
	struct pm830_chip	*chip;
	struct work_struct	meas_id_work;
	int			vbus_irq;
	int			id_irq;
	int			id_level;
	int			vbus_gpio;
	int			id_gpadc;
	bool			id_ov_sampling;
	int			id_ov_samp_count;
	int			id_ov_samp_sleep;
	int			chg_in;
	unsigned int		gpadc_meas;
	unsigned int		gpadc_upp_th;
	unsigned int		gpadc_low_th;
	spinlock_t		lock;
};

static struct pm830_usb_info *usb_info;
static int rm_flag;

static int pm830_get_vbus(unsigned int *level)
{
	int ret, val, voltage;
	unsigned char buf[2];
	unsigned long flags;

	spin_lock_irqsave(&usb_info->lock, flags);
	if (rm_flag) {
		*level = 0;
		spin_unlock_irqrestore(&usb_info->lock, flags);
		pr_info("%s do nothing..\n", __func__);
		return 0;
	} else {
		spin_unlock_irqrestore(&usb_info->lock, flags);
	}

	ret = regmap_bulk_read(usb_info->chip->regmap,
				PM830_VCHG_MEAS1, buf, 2);
	if (ret)
		return ret;

	val = ((buf[0] & 0xff) << 4) | (buf[1] & 0x0f);
	voltage = pm830_get_adc_volt(PM830_GPADC_VCHG, val);

	/* read pm830 status to decide it's cable in or out */
	regmap_read(usb_info->chip->regmap, PM830_STATUS, &val);

	/* cable in */
	if (val & PM830_CHG_DET) {
		if (voltage >= USB_INSERTION) {
			/* set charging flag, and disable OTG interrupts */
			if (!usb_info->chg_in) {
				dev_dbg(usb_info->chip->dev,
					"set charging flag, and disable OTG interrupts\n");
				usb_info->chg_in = 1;
				disable_irq(usb_info->id_irq);
			}

			*level = VBUS_HIGH;
			dev_dbg(usb_info->chip->dev,
				"%s: USB cable is valid! (%dmV)\n",
				__func__, voltage);
		} else {
			*level = VBUS_LOW;
			dev_err(usb_info->chip->dev,
				"%s: USB cable not valid! (%dmV)\n",
				__func__, voltage);
		}
	/* cable out */
	} else {
		/* OTG mode */
		if (voltage >= USB_OTG_MIN) {
			*level = VBUS_HIGH;
			dev_dbg(usb_info->chip->dev,
				"%s: OTG voltage detected!(%dmV)\n",
				__func__, voltage);
		} else {
			*level = VBUS_LOW;
			dev_dbg(usb_info->chip->dev,
				"%s: Cable out !(%dmV)\n", __func__, voltage);
		}

		/* clear charging flag, and enable OTG interrupts */
		if (usb_info->chg_in) {
			dev_dbg(usb_info->chip->dev,
				"clear charging flag, and enable OTG interrupts\n");
			usb_info->chg_in = 0;
			enable_irq(usb_info->id_irq);
		}
	}

	return 0;
}

static int read_voltage(int flag)
{
	unsigned char buf[2];
	int volt, val;

	if (flag == 1) {
		/* read VBAT average value */
		regmap_bulk_read(usb_info->chip->regmap, PM830_VBAT_AVG,
				buf, 2);
		val = ((buf[0] & 0xff) << 4) | (buf[1] & 0xf);
		volt = pm830_get_adc_volt(PM830_GPADC_VBAT, val);
	} else {
		/* read VCHG average value */
		regmap_bulk_read(usb_info->chip->regmap, PM830_VCHG_AVG,
				buf, 2);
		val = ((buf[0] & 0xff) << 4) | (buf[1] & 0xf);
		volt = pm830_get_adc_volt(PM830_GPADC_VCHG, val);
	}
	return volt;
}

static int pm830_set_vbus(unsigned int vbus)
{
	int ret, i, volt;
	unsigned int data = 0;
	unsigned long flags;
	int booster_volt = 4500;

	spin_lock_irqsave(&usb_info->lock, flags);
	if (rm_flag) {
		spin_unlock_irqrestore(&usb_info->lock, flags);
		pr_info("%s do nothing..\n", __func__);
		return 0;
	} else {
		spin_unlock_irqrestore(&usb_info->lock, flags);
	}

	if (vbus == VBUS_HIGH) {
		/* 1. enable OTG mode */
		ret = regmap_update_bits(usb_info->chip->regmap, PM830_PRE_REGULATOR,
				PM830_USB_OTG_EN, PM830_USB_OTG_EN);

		/*
		 * 2. need to make sure VCHG >= 1500mV three times, this will
		 * prevent powering-off system if a real short is present on
		 * VBUS pin. checked with silicon design team.
		 */
		for (i = 0; i < 3; i++) {
			usleep_range(1000, 2000);
			volt = read_voltage(0);
			if (volt < 1500) {
				/* voltage too low, it should be  shorted */
				pr_info("VBUS pin shorted, stop boost!\n");
				regmap_update_bits(usb_info->chip->regmap, PM830_PRE_REGULATOR,
						PM830_USB_OTG_EN, 0);
				goto out;
			}
		}

		/* 3. set OTG_STR_BYP_EN */
		regmap_update_bits(usb_info->chip->regmap, PM830_OTG_CTRL2,
				   OTG_SR_BYP_EN, OTG_SR_BYP_EN);

		/* 4. wait for 2ms */
		usleep_range(2000, 3000);

		/* 5. make sure VCHG voltage is above 90% of booster voltage */
		for (i = 0; i < 3; i++) {
			usleep_range(1000, 2000);
			volt = read_voltage(0);
			if (volt < booster_volt) {
				/* voltage too low, should stop it */
				pr_info("boost fail, should stop it!\n");
				regmap_update_bits(usb_info->chip->regmap, PM830_PRE_REGULATOR,
						PM830_USB_OTG_EN, 0);
				break;
			}
		}

		/* 6. clear OTG_STR_BYP_EN */
		regmap_update_bits(usb_info->chip->regmap, PM830_OTG_CTRL2,
				   OTG_SR_BYP_EN, 0);
	} else
		ret = regmap_update_bits(usb_info->chip->regmap,
					 PM830_PRE_REGULATOR,
					 PM830_USB_OTG_EN, 0);

	if (ret)
		return ret;

out:
	usleep_range(10000, 20000);

	ret = pm830_get_vbus(&data);
	if (ret)
		return ret;

	if (data != vbus)
		pr_info("vbus set failed %x\n", vbus);
	else
		pr_info("vbus set done %x\n", vbus);

	return 0;
}

static int pm830_ext_read_id_level(unsigned int *level)
{
	unsigned long flags;

	spin_lock_irqsave(&usb_info->lock, flags);
	if (rm_flag) {
		*level = 0;
		spin_unlock_irqrestore(&usb_info->lock, flags);
		pr_info("%s do nothing..\n", __func__);
		return 0;
	} else {
		spin_unlock_irqrestore(&usb_info->lock, flags);
	}

	if (usb_info->chg_in) {
		*level = 1;
		dev_info(usb_info->chip->dev, "idpin is request during charging state\n");
		return 0;
	}

	*level = usb_info->id_level;

	return 0;
};

static int pm830_get_id_level(unsigned int *level)
{
	int ret, data;
	unsigned char buf[2];

	ret = regmap_bulk_read(usb_info->chip->regmap, usb_info->gpadc_meas, buf, 2);
	if (ret)
		return ret;

	data = ((buf[0] & 0xff) << 4) | (buf[1] & 0x0f);
	if (data > 0x100)
		*level = 1;
	else
		*level = 0;

	dev_dbg(usb_info->chip->dev,
		"usb id voltage = %d mV, level is %s\n",
		(((data) & 0xfff) * 175) >> 9, (*level == 1 ? "HIGH" : "LOW"));

	return 0;
}

static int pm830_update_id_level(void)
{
	int ret;

	ret = pm830_get_id_level(&usb_info->id_level);
	if (ret)
		return ret;

	if (usb_info->id_level) {
		regmap_write(usb_info->chip->regmap, usb_info->gpadc_low_th, 0x10);
		regmap_write(usb_info->chip->regmap, usb_info->gpadc_upp_th, 0xff);
	} else {
		regmap_write(usb_info->chip->regmap, usb_info->gpadc_low_th, 0);
		regmap_write(usb_info->chip->regmap, usb_info->gpadc_upp_th, 0x10);
	}

	dev_info(usb_info->chip->dev, "idpin is %s\n", usb_info->id_level ? "HIGH" : "LOW");
	return 0;
}

static void pm830_meas_id_work(struct work_struct *work)
{
	int i = 0;
	unsigned int level, last_level = 1;

	/*
	 * 1.loop until the line is stable
	 * 2.in every iteration do the follwing:
	 *	- measure the line voltage
	 *	- check if the voltage is the same as the previous value
	 *	- if not, start the loop again (set loop index to 0)
	 *	- if yes, continue the loop to next iteration
	 * 3.if we get x (id_meas_count) identical results, loop end
	 */
	while (i < usb_info->id_ov_samp_count) {
		pm830_get_id_level(&level);

		if (i == 0) {
			last_level = level;
			i++;
		} else if (level != last_level) {
			i = 0;
		} else {
			i++;
		}

		msleep(usb_info->id_ov_samp_sleep);
	}

	/* set the GPADC thrsholds for next insertion/removal */
	if (last_level) {
		regmap_write(usb_info->chip->regmap, usb_info->gpadc_low_th, 0x10);
		regmap_write(usb_info->chip->regmap, usb_info->gpadc_upp_th, 0xff);
	} else {
		regmap_write(usb_info->chip->regmap, usb_info->gpadc_low_th, 0);
		regmap_write(usb_info->chip->regmap, usb_info->gpadc_upp_th, 0x10);
	}

	/* after the line is stable, we can enable the id interrupt */
	enable_irq(usb_info->id_irq);

	/* in case we missed interrupt till we enable it, we take one more measurment */
	pm830_get_id_level(&level);

	/*
	 * if the last measurment is different from the stable value,
	 *  need to start the process again
	*/
	if (level != last_level) {
		disable_irq(usb_info->id_irq);
		schedule_work(&usb_info->meas_id_work);
		return;
	}

	/* notify to wake up the usb subsystem if ID pin value changed */
	if (last_level != usb_info->id_level) {
		usb_info->id_level = last_level;
		pxa_usb_notify(PXA_USB_DEV_OTG, EVENT_ID, 0);

		dev_info(usb_info->chip->dev, "idpin is %s\n", usb_info->id_level ? "HIGH" : "LOW");
	}
}

int pm830_init_id(void)
{
	return 0;
}

static irqreturn_t pm830_vbus_handler(int irq, void *data)
{

	struct pm830_usb_info *info = data;
	/*
	 * 88pm830 has no ability to distinguish
	 * AC/USB charger, so notify usb framework to do it
	 */
	pxa_usb_notify(PXA_USB_DEV_OTG, EVENT_VBUS, 0);
	dev_info(info->chip->dev, "88pm830 vbus interrupt is served..\n");
	return IRQ_HANDLED;
}

static irqreturn_t pm830_id_handler(int irq, void *data)
{
	struct pm830_usb_info *info = data;

	if (info->id_ov_sampling) {
		/* disable id interrupt, and start measurment process */
		disable_irq_nosync(info->id_irq);
		schedule_work(&info->meas_id_work);
	} else {
		/* update id value */
		pm830_update_id_level();

		/* notify to wake up the usb subsystem if ID pin is pulled down */
		pxa_usb_notify(PXA_USB_DEV_OTG, EVENT_ID, 0);
	}

	dev_info(info->chip->dev, "88pm830 id interrupt is served..\n");
	return IRQ_HANDLED;
}

static void pm830_usb_config(struct pm830_usb_info *info)
{
	unsigned int gpadc_en;

	/* 1. set booster voltage to 5.0V */
	regmap_update_bits(info->chip->regmap, PM830_OTG_CTRL1,
			OTG_VOLT_MASK, OTG_VOLT_SET(5));
	/*
	 * 2. booster high-side/low-side over current protection must be
	 * increased
	 */
	regmap_update_bits(info->chip->regmap, PM830_OTG_CTRL3,
			(OTG_HIGH_SIDE_MASK | OTG_LOW_SIDE_MASK),
			(OTG_HIGH_SIDE_SET(6) | OTG_LOW_SIDE_SET(4)));

	/* 3. set gpadc parameters */
	switch (info->id_gpadc) {
	case PM830_GPADC0:
		info->gpadc_meas = PM830_GPADC0_MEAS1;
		info->gpadc_low_th = PM830_GPADC0_LOW_TH;
		info->gpadc_upp_th = PM830_GPADC0_UPP_TH;
		gpadc_en = PM830_GPADC0_MEAS_EN;

		/* Set bias to 36uA*/
		regmap_update_bits(info->chip->regmap, PM830_GPADC0_BIAS,
				BIAS_GP0_MASK, BIAS_GP0_SET(36));
		break;
	case PM830_GPADC1:
		info->gpadc_meas = PM830_GPADC1_MEAS1;
		info->gpadc_low_th = PM830_GPADC1_LOW_TH;
		info->gpadc_upp_th = PM830_GPADC1_UPP_TH;
		gpadc_en = PM830_GPADC1_MEAS_EN;

		/* Set bias to 36uA*/
		regmap_update_bits(info->chip->regmap, PM830_GPADC1_BIAS,
				BIAS_GP1_MASK, BIAS_GP1_SET(36));
		break;
	default:
		dev_err(info->chip->dev, "no configuration for gpadc number %d\n", info->id_gpadc);
		return;
	}

	/* 4. read ID level, and set the thresholds for GPADC to prepare for interrupt */
	pm830_update_id_level();

	regmap_update_bits(info->chip->regmap, PM830_GPADC_MEAS_EN, gpadc_en, gpadc_en);
}

static int pm830_usb_dt_init(struct device_node *np,
			     struct device *dev,
			     struct pm830_usb_pdata *pdata)
{
	int ret;

	ret = of_property_read_u32(np, "gpadc-number", &pdata->id_gpadc);
	if (ret) {
		pr_err("cannot get gpadc number.\n");
		return -EINVAL;
	}

	pdata->id_ov_sampling = of_property_read_bool(np, "id-ov-sampling");

	if (pdata->id_ov_sampling) {
		ret = of_property_read_u32(np, "id-ov-samp-count", &pdata->id_ov_samp_count);
		if (ret) {
			pr_err("cannot get id measurments count.\n");
			return -EINVAL;
		}

		ret = of_property_read_u32(np, "id-ov-samp-sleep", &pdata->id_ov_samp_sleep);
		if (ret) {
			pr_err("cannot get id sleep time.\n");
			return -EINVAL;
		}
	}

	return 0;
}

static int pm830_usb_probe(struct platform_device *pdev)
{
	struct pm830_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm830_usb_pdata *pdata;
	struct pm830_usb_info *usb;
	struct device_node *node = pdev->dev.of_node;
	int ret;

	usb = devm_kzalloc(&pdev->dev,
			   sizeof(struct pm830_usb_info), GFP_KERNEL);
	if (!usb)
		return -ENOMEM;

	pdata = pdev->dev.platform_data;
	if (IS_ENABLED(CONFIG_OF)) {
		if (!pdata) {
			pdata = devm_kzalloc(&pdev->dev,
					     sizeof(*pdata), GFP_KERNEL);
			if (!pdata)
				return -ENOMEM;
		}
		ret = pm830_usb_dt_init(node, &pdev->dev, pdata);
		if (ret)
			goto out;
	} else if (!pdata) {
		return -EINVAL;
	}

	usb->chip = chip;
	usb->vbus_gpio = pdata->vbus_gpio;
	usb->id_gpadc = pdata->id_gpadc;
	usb->id_ov_sampling = pdata->id_ov_sampling;
	usb->id_ov_samp_count = pdata->id_ov_samp_count;
	usb->id_ov_samp_sleep = pdata->id_ov_samp_sleep;
	usb->id_level = 1;
	usb->chg_in = 0;

	/* global variable used by get/set_vbus */
	usb_info = usb;

	pm830_usb_config(usb);

	INIT_WORK(&usb->meas_id_work, pm830_meas_id_work);

	usb->vbus_irq = platform_get_irq(pdev, 0);
	if (usb->vbus_irq < 0) {
		dev_err(&pdev->dev, "failed to get vbus irq\n");
		ret = -ENXIO;
		goto out;
	}

	usb->id_irq = platform_get_irq(pdev, usb->id_gpadc + 1);
	if (usb->id_irq < 0) {
		dev_err(&pdev->dev, "failed to get idpin irq\n");
		ret = -ENXIO;
		goto out;
	}

	ret = devm_request_threaded_irq(&pdev->dev, usb->vbus_irq, NULL,
					pm830_vbus_handler,
					IRQF_ONESHOT | IRQF_NO_SUSPEND,
					"usb detect", usb);
	if (ret) {
		dev_info(&pdev->dev,
			"cannot request irq for VBUS, return\n");
		goto out;
	}

	spin_lock_init(&usb->lock);
	ret = devm_request_threaded_irq(&pdev->dev, usb->id_irq, NULL,
					pm830_id_handler,
					IRQF_ONESHOT | IRQF_NO_SUSPEND,
					"id detect", usb);
	if (ret) {
		dev_info(&pdev->dev,
			"cannot request irq for idpin, return\n");
		goto out;
	}

	platform_set_drvdata(pdev, usb);
	device_init_wakeup(&pdev->dev, 1);

	pxa_usb_set_extern_call(PXA_USB_DEV_OTG, vbus, set_vbus,
				pm830_set_vbus);
	pxa_usb_set_extern_call(PXA_USB_DEV_OTG, vbus, get_vbus,
				pm830_get_vbus);
	pxa_usb_set_extern_call(PXA_USB_DEV_OTG, idpin, get_idpin,
				pm830_ext_read_id_level);
	pxa_usb_set_extern_call(PXA_USB_DEV_OTG, idpin, init,
				pm830_init_id);

	/*
	 * GPADC is enabled in otg driver via calling pm830_init_id()
	 * without pm830_init_id(), the GPADC is disabled
	 * pm830_init_id();
	 */

	return 0;

out:
	return ret;
}

static int pm830_usb_remove(struct platform_device *pdev)
{
	struct pm830_usb_info *usb = platform_get_drvdata(pdev);
	unsigned long flags;
	if (!usb)
		return 0;

	devm_free_irq(&pdev->dev, usb->vbus_irq, usb);
	devm_free_irq(&pdev->dev, usb->id_irq, usb);

	spin_lock_irqsave(&usb->lock, flags);
	rm_flag = 1;
	spin_unlock_irqrestore(&usb->lock, flags);

	cancel_work_sync(&usb->meas_id_work);

	devm_kfree(&pdev->dev, usb);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static void pm830_usb_shutdown(struct platform_device *pdev)
{
	pm830_usb_remove(pdev);
	return;
}

#ifdef CONFIG_PM
static SIMPLE_DEV_PM_OPS(pm830_usb_pm_ops, pm830_dev_suspend,
			 pm830_dev_resume);
#endif

static const struct of_device_id pm830_vbus_dt_match[] = {
	{ .compatible = "marvell,88pm830-vbus", },
	{ },
};
MODULE_DEVICE_TABLE(of, pm830_vbus_dt_match);

static struct platform_driver pm830_usb_driver = {
	.driver		= {
		.name	= "88pm830-vbus",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(pm830_vbus_dt_match),
	},
	.probe		= pm830_usb_probe,
	.remove		= pm830_usb_remove,
	.shutdown	= pm830_usb_shutdown,
};

static int pm830_usb_init(void)
{
	return platform_driver_register(&pm830_usb_driver);
}
module_init(pm830_usb_init);

static void pm830_usb_exit(void)
{
	platform_driver_unregister(&pm830_usb_driver);
}
module_exit(pm830_usb_exit);

MODULE_DESCRIPTION("VBUS driver for Marvell Semiconductor 88PM830");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:88pm830-usb");
