/*
 * PXA1986 Thermal Implementation
 *
 * This software program is licensed subject to the GNU General Public License
 * (GPL).Version 2,June 1991, available at http://www.fsf.org/copyleft/gpl.html
 * (C) Copyright 2013 Marvell International Ltd.
 * All Rights Reserved
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/io.h>
#include <linux/syscalls.h>
#include <linux/cpufreq.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
#include <linux/pxa1986_thermal.h>
#include <linux/of.h>

#define TRIP_POINTS_ACTIVE_NUM	(TRIP_POINTS_NUM - 1)
#define POLLING_FREQUENCY_MS	2000
#define TSU_READ_RETRIES		20000
/* TSU Registers */
#define TSU_CTRL		0x00
#define TSU_INTR		0x08
#define TSU_THL			0x0C
#define TSU_THH			0x10
#define TSU_WDT			0x14
/* TSU_CTRL 0x00 */
#define TSU_CTRL_TSR		(0xFFF << 8)
#define TSU_CTRL_VALID		(1 << 7)
#define TSU_CTRL_ISO_EN		(1 << 6)
#define TSU_CTRL_START		(1 << 2)
#define TSU_CTRL_TSU_EN		(1 << 1)
/* TSU_INTR 0x08 */
#define TSU_INTR_L1_EN		(1 << 0)
#define TSU_INTR_L2_EN		(1 << 1)
#define TSU_INTR_H1_EN		(1 << 2)
#define TSU_INTR_H2_EN		(1 << 3)
#define TSU_INTR_OWM_EN		(1 << 8)

#define TSU_CTRL_EN_MASK	0x1a
#define TSU_INTR_EN_MASK	0x000F
#define TSU_INTR_ST_MASK	0x00F0
#define TSU_TH_MASK			0xFFF
#define TSU_INVALID_TEMP	0xFFFFFFFF
/* TSU thresholds */
#define TSU_OWM_TH_TEMP			100400
#define TSU_TH_OFFSET_TEMP		3000
#define TSU_TH_LOCK_TEMP		(TSU_TH_OFFSET_TEMP / 2)

#define TSU_TH_LOW1		0
#define TSU_TH_LOW2		1
#define TSU_TH_HIGH1	2
#define TSU_TH_HIGH2	3

#define COOLDEV_STATE_OFF		0
#define COOLDEV_STATE_ON		1
#define COOLDEV_MAX_STATE		1
#define COOLDEV_MAX_COUNT		8

#define TSEN_GAIN			3874
#define TSEN_OFFSET			-282100

static int celsius_decode(int tcode)
{
	return (tcode * TSEN_GAIN) / 10 + TSEN_OFFSET;
}

static int celsius_encode(int cels)
{
	return ((cels - TSEN_OFFSET) * 10) / TSEN_GAIN;
}

struct pxa1986_thermal_zone {
	struct pxa1986_thermal_platform_data *pdata;
	struct thermal_zone_device *tzdev;
	void __iomem *base;
	struct clk *tsu_clk;
	int irq;
};

static int thermal_get_temp(struct pxa1986_thermal_zone *tz);
static int thermal_get_threshold(struct pxa1986_thermal_zone *tz,
				 unsigned th_id);
static void thermal_set_trip_point(struct pxa1986_thermal_zone *tz,
				   unsigned point, int temp);
static void thermal_set_interrupts(struct pxa1986_thermal_zone *tz,
				   int current_temp);

struct pxa1986_cool_dev_data {
	char type[THERMAL_NAME_LENGTH];
	int current_state;
	int max_state;
	void (*set_cur_state)(unsigned int on);
};

static unsigned int cdev_count;
static struct pxa1986_cool_dev_data cdev_data[COOLDEV_MAX_COUNT];

static int cool_dev_get_max_state(struct thermal_cooling_device *cdev,
				  unsigned long *state)
{
	struct pxa1986_cool_dev_data *cdata = cdev->devdata;
	*state = cdata->max_state;
	return 0;
}

static int cool_dev_get_cur_state(struct thermal_cooling_device *cdev,
				  unsigned long *state)
{
	struct pxa1986_cool_dev_data *cdata = cdev->devdata;
	*state = cdata->current_state;
	return 0;
}

static int cool_dev_set_cur_state(struct thermal_cooling_device *cdev,
				  unsigned long state)
{
	struct pxa1986_cool_dev_data *cdata = cdev->devdata;
	if (state > COOLDEV_MAX_STATE)
		state = COOLDEV_MAX_STATE;
	if (state == cdata->current_state)
		return 0;
	cdata->current_state = state;
	pr_info("%s is %s\n", cdev->type,
		state == COOLDEV_STATE_ON ? "on" : "off");
	if (cdata->set_cur_state)
		cdata->set_cur_state(state == COOLDEV_STATE_ON ? 1 : 0);

	return 0;
}

static struct thermal_cooling_device_ops cool_dev_ops = {
	.get_max_state = cool_dev_get_max_state,
	.get_cur_state = cool_dev_get_cur_state,
	.set_cur_state = cool_dev_set_cur_state,
};

void pxa1986_thermal_cooling_devices_register(
	struct pxa1986_thermal_cooling_dev_info *cdev_info,
	unsigned int count)
{
	int i;

	if (count > COOLDEV_MAX_COUNT) {
		pr_err("%s: Cooling device count > max limit", __func__);
		return;
	}
	cdev_count = count;
	for (i = 0; i < cdev_count; i++) {
		cdev_data[i].current_state = 0;
		cdev_data[i].max_state = COOLDEV_MAX_STATE;
		cdev_data[i].set_cur_state = cdev_info[i].set_state;
		strncpy(cdev_data[i].type, cdev_info[i].type,
			THERMAL_NAME_LENGTH - 1);
	}
}
EXPORT_SYMBOL(pxa1986_thermal_cooling_devices_register);

static int cooldev_init;
void thermal_cooling_devices_register(void)
{
	int i;
	if (cooldev_init)
		return;
	for (i = 0; i < cdev_count; i++) {
		thermal_cooling_device_register(cdev_data[i].type,
					&cdev_data[i],
					&cool_dev_ops);
	}
	cooldev_init = 1;
}

static int thermal_sys_bind(struct thermal_zone_device *tzdev,
			    struct thermal_cooling_device *cdev)
{
	int i;
	struct pxa1986_thermal_zone *tz = tzdev->devdata;
	for (i = 0; i < TRIP_POINTS_ACTIVE_NUM; i++) {
		if (!strcmp(tz->pdata->trip_points[i].cool_dev_type,
			    cdev->type))
			return thermal_zone_bind_cooling_device(tzdev, i, cdev,
					THERMAL_NO_LIMIT, THERMAL_NO_LIMIT);
	}
	return 0;
}

static int thermal_sys_get_temp(struct thermal_zone_device *tzdev,
				unsigned long *temp)
{
	int temperature;
	struct pxa1986_thermal_zone *tz = tzdev->devdata;
	if (!tz)
		return -EINVAL;

	temperature = thermal_get_temp(tz);
	if (temperature < 0) {
		*temp = 0;
		return -EAGAIN;
	} else {
		*temp = temperature;
		return 0;
	}
}

static int thermal_sys_get_mode(struct thermal_zone_device *tzdev,
				enum thermal_device_mode *mode)
{
	*mode = THERMAL_DEVICE_ENABLED;
	return 0;
}

static int thermal_sys_get_trip_type(struct thermal_zone_device *tzdev,
				     int trip, enum thermal_trip_type *type)
{
	if ((trip >= 0) && (trip < TRIP_POINTS_ACTIVE_NUM))
		*type = THERMAL_TRIP_ACTIVE;
	else if (TRIP_POINTS_ACTIVE_NUM == trip)
		*type = THERMAL_TRIP_CRITICAL;
	else
		*type = (enum thermal_trip_type)(-1);
	return 0;
}

static int thermal_sys_get_trip_temp(struct thermal_zone_device *tzdev,
				     int trip, unsigned long *temp)
{
	struct pxa1986_thermal_zone *tz = tzdev->devdata;
	if (NULL == tz)
		return -EINVAL;
	if ((trip >= 0) && (trip < TRIP_POINTS_NUM))
		*temp = tz->pdata->trip_points[trip].temperature;
	else
		*temp = -1;
	return 0;
}

static int thermal_sys_set_trip_temp(struct thermal_zone_device *tzdev,
			int trip, unsigned long temp)
{
	struct pxa1986_thermal_zone *tz = tzdev->devdata;
	if (NULL == tz)
		return -EINVAL;

	tz->pdata->trip_points[trip].temperature = temp;
	thermal_set_trip_point(tz, trip, temp);
	thermal_set_interrupts(tz, thermal_get_temp(tz));
	return 0;
}

static int thermal_sys_get_crit_temp(struct thermal_zone_device *tzdev,
				     unsigned long *temp)
{
	struct pxa1986_thermal_zone *tz = tzdev->devdata;
	if (NULL == tz)
		return -EINVAL;
	*temp = tz->pdata->trip_points[TRIP_POINTS_NUM - 1].temperature;
	return 0;
}

static int thermal_sys_notify(struct thermal_zone_device *tzdev, int count,
			      enum thermal_trip_type trip_type)
{
	if (THERMAL_TRIP_CRITICAL == trip_type)
		return 0;
	else
		return -1;
}

static struct thermal_zone_device_ops thermal_dev_ops = {
	.bind = thermal_sys_bind,
	.get_temp = thermal_sys_get_temp,
	.get_mode = thermal_sys_get_mode,
	.get_trip_type = thermal_sys_get_trip_type,
	.get_trip_temp = thermal_sys_get_trip_temp,
	.set_trip_temp = thermal_sys_set_trip_temp,
	.get_crit_temp = thermal_sys_get_crit_temp,
	.notify = thermal_sys_notify,
};

static int thermal_get_temp(struct pxa1986_thermal_zone *tz)
{
	unsigned tsu_ctrl, retry = TSU_READ_RETRIES;

	tsu_ctrl = __raw_readl(tz->base + TSU_CTRL);
	/* Verify valid bit */
	while (!(tsu_ctrl & TSU_CTRL_VALID) && --retry)
		tsu_ctrl = __raw_readl(tz->base + TSU_CTRL);
	if (retry > 0)
		return celsius_decode((tsu_ctrl & TSU_CTRL_TSR) >> 8);
	else
		return TSU_INVALID_TEMP;
}

static int thermal_get_threshold(struct pxa1986_thermal_zone *tz,
				 unsigned th_id)
{
	unsigned reg, offset, th;
	if (th_id > TSU_TH_HIGH2)
		return 0;
	reg = (th_id / 2) ? TSU_THH : TSU_THL;
	offset = 12 * (th_id % 2);
	th = __raw_readl(tz->base + reg) & (TSU_TH_MASK << offset);
	return celsius_decode(th >> offset);
}

static void thermal_set_threshold(struct pxa1986_thermal_zone *tz,
				  unsigned th_id, int temp)
{
	unsigned reg, offset, th;
	if (th_id > TSU_TH_HIGH2)
		return;
	reg = (th_id / 2) ? TSU_THH : TSU_THL;
	offset = 12 * (th_id % 2);
	th = __raw_readl(tz->base + reg) & ~(TSU_TH_MASK << offset);
	th |= (celsius_encode(temp) << offset);
	__raw_writel(th, tz->base + reg);
}

static void thermal_set_owm_threshold(struct pxa1986_thermal_zone *tz, int temp)
{
	__raw_writel(celsius_encode(temp), tz->base + TSU_WDT);
}

static void thermal_enable(struct pxa1986_thermal_zone *tz)
{
	__raw_writel(TSU_CTRL_EN_MASK, tz->base + TSU_CTRL);
}

static void thermal_set_interrupts(struct pxa1986_thermal_zone *tz,
				   int current_temp)
{
	unsigned mask = TSU_INTR_OWM_EN;
	__raw_writel(TSU_INTR_ST_MASK, tz->base + TSU_INTR);
	if (current_temp == TSU_INVALID_TEMP) {
		pr_info("TSU[%d] interrupts are disabled\n", tz->tzdev->id);
		return;
	}
	if (thermal_get_threshold(tz, TSU_TH_LOW1) <
	    current_temp - TSU_TH_LOCK_TEMP)
		mask |= TSU_INTR_L1_EN;
	if (thermal_get_threshold(tz, TSU_TH_LOW2) <
	    current_temp - TSU_TH_LOCK_TEMP)
		mask |= TSU_INTR_L2_EN;
	if (thermal_get_threshold(tz, TSU_TH_HIGH1) >
	    current_temp + TSU_TH_LOCK_TEMP)
		mask |= TSU_INTR_H1_EN;
	if (thermal_get_threshold(tz, TSU_TH_HIGH2) >
	    current_temp + TSU_TH_LOCK_TEMP)
		mask |= TSU_INTR_H2_EN;
	__raw_writel(mask, tz->base + TSU_INTR);
}

static irqreturn_t thermal_irq_handler(int irq, void *devid)
{
	struct pxa1986_thermal_zone *tz = devid;
	if (!tz)
		return IRQ_NONE;
	if (__raw_readl(tz->base + TSU_INTR) & TSU_INTR_ST_MASK) {
		/* disable TSU interrups, but do not clear the status */
		__raw_writel(0x0, tz->base + TSU_INTR);
		return IRQ_WAKE_THREAD;
	} else
		return IRQ_NONE;
}

static irqreturn_t thermal_threaded_irq_handler(int irq, void *devid)
{
	struct pxa1986_thermal_zone *tz = devid;
	char *temp_info[3] = { "TYPE=unknown_thermal_device",
			"TEMP=100000", NULL };
	int current_temp;
	if (!tz)
		return IRQ_NONE;

	if (__raw_readl(tz->base + TSU_INTR) & TSU_INTR_ST_MASK) {
		current_temp = thermal_get_temp(tz);
		sprintf(temp_info[0], "TYPE=%s", tz->pdata->name);
		sprintf(temp_info[1], "TEMP=%d", current_temp);
		kobject_uevent_env(&(tz->tzdev->device.kobj),
			KOBJ_CHANGE, temp_info);
		thermal_set_interrupts(tz, current_temp);
		thermal_zone_device_update(tz->tzdev);
		return IRQ_HANDLED;
	} else
		return IRQ_NONE;
}

#define TSU_TH_LOW(point)	(point)
#define TSU_TH_HIGH(point)	(point + 2)

static void thermal_set_trip_point(struct pxa1986_thermal_zone *tz,
				   unsigned point, int temp)
{
	if (point >= TRIP_POINTS_ACTIVE_NUM)
		return;

	thermal_set_threshold(tz, TSU_TH_LOW(point), temp - TSU_TH_OFFSET_TEMP);
	thermal_set_threshold(tz, TSU_TH_HIGH(point),
			      temp + TSU_TH_OFFSET_TEMP);
}

static int pxa1986_thermal_probe(struct platform_device *pdev)
{
	int i, ret = 0;
	struct resource *res;
	struct pxa1986_thermal_zone *tz;

	if (!pdev->dev.platform_data) {
		pr_err("%s: No platform data\n", __func__);
		return -EINVAL;
	}

	/* register cooling devices */
	thermal_cooling_devices_register();
	tz = kzalloc(sizeof(struct pxa1986_thermal_zone), GFP_KERNEL);
	if (!tz) {
		pr_err("%s: kzalloc error\n", __func__);
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	tz->base = ioremap(res->start, resource_size(res));
	if (!tz->base) {
		pr_err("%s: ioremap error\n", __func__);
		ret = -EINVAL;
		goto err_ioremap;
	}

	tz->pdata = pdev->dev.platform_data;
	dev_set_drvdata(&pdev->dev, tz);

	tz->tsu_clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(tz->tsu_clk)) {
		pr_err("%s: could not get clock\n", __func__);
		ret = -EINVAL;
		goto err_clk;
	}
	clk_prepare_enable(tz->tsu_clk);

	tz->irq = platform_get_irq(pdev, 0);
	if (tz->irq < 0) {
		pr_err("%s: No IRQ resource defined\n", __func__);
		ret = -EINVAL;
		goto err_irq;
	}
	ret = request_threaded_irq(tz->irq, thermal_irq_handler,
				   thermal_threaded_irq_handler,
				   IRQF_SHARED, tz->pdata->name, tz);
	if (ret < 0) {
		pr_err("%s: failed to request IRQ\n", __func__);
		goto err_irq;
	}

	thermal_enable(tz);

	for (i = 0; i < TRIP_POINTS_NUM; i++) {
		tz->pdata->trip_points[i].temperature *= 1000;
		thermal_set_trip_point(tz, i,
					tz->pdata->trip_points[i].temperature);
	}
	thermal_set_owm_threshold(tz, TSU_OWM_TH_TEMP);

	tz->tzdev = thermal_zone_device_register(tz->pdata->name,
						TRIP_POINTS_NUM,
						0,
						tz,
						&thermal_dev_ops,
						NULL,
						POLLING_FREQUENCY_MS,
						0);
	if (IS_ERR(tz->tzdev)) {
		pr_err("%s: thermal zone registartion failed\n", __func__);
		ret = -EINVAL;
		goto err_tz_reg;
	}
	thermal_set_interrupts(tz, thermal_get_temp(tz));
	return 0;

err_tz_reg:
	free_irq(tz->irq, tz);
err_irq:
	clk_disable(tz->tsu_clk);
err_clk:
	iounmap(tz->base);
err_ioremap:
	kfree(tz);
	return ret;
}

static int pxa1986_thermal_remove(struct platform_device *pdev)
{
	struct pxa1986_thermal_zone *tz = dev_get_drvdata(&pdev->dev);
	clk_disable(tz->tsu_clk);
	thermal_zone_device_unregister(tz->tzdev);
	free_irq(tz->irq, tz);
	iounmap((void *)tz->base);
	kfree(tz);
	return 0;
}

#ifdef CONFIG_PM
static int pxa1986_thermal_suspend(struct device *dev)
{
	struct pxa1986_thermal_zone *tz = dev_get_drvdata(dev);
	clk_disable(tz->tsu_clk);
	return 0;
}

static int pxa1986_thermal_resume(struct device *dev)
{
	struct pxa1986_thermal_zone *tz = dev_get_drvdata(dev);
	clk_disable(tz->tsu_clk);
	return 0;
}

static const struct dev_pm_ops pxa1986_thermal_pm_ops = {
	.suspend = pxa1986_thermal_suspend,
	.resume = pxa1986_thermal_resume,
};
#endif

#ifdef CONFIG_OF
static const struct of_device_id pxa1986_thermal_match[] = {
	{ .compatible = "mrvl,pxa1986-thermal", },
	{},
};
MODULE_DEVICE_TABLE(of, pxa1986_thermal_match);
#endif

static struct platform_driver pxa1986_thermal_driver = {
	.driver = {
		   .name = "pxa1986-thermal",
		   .owner = THIS_MODULE,
#ifdef CONFIG_PM
		   .pm = &pxa1986_thermal_pm_ops,
#endif
#if CONFIG_OF
	.of_match_table = of_match_ptr(pxa1986_thermal_match),
#endif
	},
	.probe = pxa1986_thermal_probe,
	.remove = pxa1986_thermal_remove,
};

static int __init pxa1986_thermal_init(void)
{
	return platform_driver_register(&pxa1986_thermal_driver);
}

static void __exit pxa1986_thermal_exit(void)
{
	platform_driver_unregister(&pxa1986_thermal_driver);
}

module_init(pxa1986_thermal_init);
module_exit(pxa1986_thermal_exit);

MODULE_AUTHOR("Marvell Semiconductor");
MODULE_DESCRIPTION("PXA1986 SoC thermal driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa1986-thermal");
