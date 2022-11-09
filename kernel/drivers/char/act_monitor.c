/*
 * driver/char/act_monitor.c
 *
 *  Copyright (C) 2012 Marvell International Ltd.
 *  All rights reserved.
 *
 *  2013-01-30  Yifan Zhang <zhangyf@marvell.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pm_qos.h>
#include <linux/devfreq.h>
#include <linux/act_monitor.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/clk/mmp.h>
#include <linux/platform_data/devfreq-pxa.h>

#define AM_HIGH_MASK 0x10
#define AM_LOW_MASK 0x8
#define THRESH_UP_MASK 0x7f
#define THRESH_LOW_MASK 0x7f
#define ACT_MON_ENALBE (1 << 3)
#define SAMPLE_WINDOWS_LEN 50
#define KHZ_TO_HZ 1000
#define MHZ_TO_HZ 1000000

static struct act_monitor_info *act_info;
static struct pm_qos_request act_qos_req_min;
static struct mutex constraint_lock;
static struct mutex act_reg_lock;

#if defined(CONFIG_CPU_PXA1986)
static int ddr_max = 1066000000;
#else
static int ddr_max = 528000;
#endif

static int __init ddr_max_setup(char *str)
{
	int freq;

	if (!get_option(&str, &freq))
		return 0;
	ddr_max = freq;
	return 1;
}

__setup("ddr_max=", ddr_max_setup);

static int ddr_op_cur_index(void)
{
	int i;
	u32 freq;
	u32 len;

	freq = clk_get_rate(act_info->ddr_clk) / KHZ_TO_HZ;
	len = act_info->ddr_freq_tbl_len;

	for (i = 0; i < len; i++)
		if (act_info->ddr_freq_tbl[i] == freq)
			break;

	return i;
}

static u32 ddrtbl_find_glb(u32 val)
{
	int i;
	u32 len;

	len = act_info->ddr_freq_tbl_len;
	for (i = 0; i < len; i++)
		if (act_info->ddr_freq_tbl[i] > val)
			break;

	if (i == 0)
		i++;

	return act_info->ddr_freq_tbl[i - 1];
}

/*
 * min or max constraint may not be within PP table
 * for max we set GLB: ddrtbl_find_glb
 * for min we set LUB: clk_set_rate
 */
static void update_freq(u32 freq)
{
	if (atomic_read(&act_info->is_disabled))
		return;

	if (act_info->min_freq && freq < act_info->min_freq)
		freq = act_info->min_freq;

	if (act_info->max_freq && freq > act_info->max_freq)
		freq = ddrtbl_find_glb(act_info->max_freq);

	clk_set_rate(act_info->ddr_clk, freq * KHZ_TO_HZ);
}

void ddrfreq1down(void)
{
	int cur;
	u32 len;

	cur = ddr_op_cur_index();
	len = act_info->ddr_freq_tbl_len;

	/* if cur freq not in pp table, fail */
	if (cur >= len) {
		pr_err("ddrfreq down fail!\n");
		return;
	}

	mutex_lock(&constraint_lock);
	/* if already min, do nothing */
	if (cur > 0)
		update_freq(act_info->ddr_freq_tbl[cur - 1]);
	mutex_unlock(&constraint_lock);
}

#if 0
void ddrfreq1up(void)
{
	int cur;
	int len;

	cur = ddr_op_cur_index();
	len = act_info->ddr_freq_tbl_len;

	/* if cur freq not in pp table, fail */
	if (cur < 0) {
		pr_err("ddrfreq up fail!\n");
		return;
	}

	/* if already max, do nothing */
	if (cur < len - 1)
		clk_set_rate(act_info->ddr_clk,
			act_info->ddr_freq_tbl[cur + 1]);
}
#else
void ddrfreqmmax(void)
{
	int cur;
	int len;

	cur = ddr_op_cur_index();
	len = act_info->ddr_freq_tbl_len;

	/* if cur freq not in pp table, fail */
	if (cur >= len) {
		pr_err("ddrfreq up fail!\n");
		return;
	}

	mutex_lock(&constraint_lock);
	/* if already max, do nothing */
	if (cur < len - 1)
		update_freq(act_info->ddr_freq_tbl[len - 1]);
	mutex_unlock(&constraint_lock);
}
#endif

static irqreturn_t act_mon_threaded_handler(int irq, void *data)
{
	u32 isr_val;

	isr_val = readl(act_info->act_mon_base);
	if (isr_val & AM_HIGH_MASK)
		ddrfreqmmax();
	else if (isr_val & AM_LOW_MASK)
		ddrfreq1down();
	else
		pr_err("unhandled mc irq 0x%x", isr_val);

	return IRQ_HANDLED;
}

static ssize_t act_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int s;

	void *base = act_info->act_mon_base;

	s = sprintf(buf, "isr: 0x%x\n", readl(base));
	s += sprintf(buf + s, "ier: 0x%x\n", readl(base + 0x4));
	s += sprintf(buf + s, "period: 0x%x\n", readl(base + 0x20));
	s += sprintf(buf + s, "threshold: 0x%x\n", readl(base + 0x24));

	return s;
}

static ssize_t disable_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	int is_disabled;

	if (0x1 != sscanf(buf, "%d", &is_disabled)) {
		dev_err(dev, "<ERR> wrong parameter\n");
		return -E2BIG;
	}

	/* atomic_t is_disabled is 1 or 0, no mutiple turning on */
	is_disabled = !!is_disabled;
	if (is_disabled == atomic_read(&act_info->is_disabled)) {
		dev_info(dev, "[WARNING] ddr fc is already %s\n",
				atomic_read(&act_info->is_disabled) ?
				"disabled" : "enabled");

		return size;
	}

	if (is_disabled)
		atomic_inc(&act_info->is_disabled);
	else
		atomic_dec(&act_info->is_disabled);

	dev_info(dev, "[WARNING] ddr fc is %s from debug interface!\n",
			atomic_read(&act_info->is_disabled) ?
			"disabled" : "enabled");

	return size;
}

static ssize_t disable_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "ddr fc is_disabled = %d\n",
			atomic_read(&act_info->is_disabled));
}

/*
 * Debug interface used to change ddr rate.
 * It will ignore all devfreq and Qos requests.
 * Use interface disable_ddr_fc prior to it.
 */
static ssize_t store_freq(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	int freq;

	if (!atomic_read(&act_info->is_disabled)) {
		dev_err(dev, "<ERR> It will change ddr rate, disable ddr fc at first\n");
		return -EPERM;
	}

	if (0x1 != sscanf(buf, "%d", &freq)) {
		dev_err(dev, "<ERR> wrong parameter, echo freq > ddr_freq to set ddr rate(unit Khz)\n");
		return -E2BIG;
	}
	clk_set_rate(act_info->ddr_clk, freq * KHZ_TO_HZ);

	dev_dbg(dev, "ddr freq read back: %lu\n",
			clk_get_rate(act_info->ddr_clk));

	return size;
}

static ssize_t show_freq(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%lu\n", clk_get_rate(act_info->ddr_clk)
			/ KHZ_TO_HZ);
}

static ssize_t show_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%u\n", act_info->min_freq);
}

static ssize_t store_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	u32 freq;

	if (0x1 != sscanf(buf, "%u", &freq)) {
		dev_err(dev, "<ERR> wrong parameter, echo freq > min_freq to set min constraint\n");
		return -E2BIG;
	}

	pm_qos_update_request(&act_qos_req_min, freq * KHZ_TO_HZ);

	return size;
}

static ssize_t show_max(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%u\n", act_info->max_freq);
}

static ssize_t store_max(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	u32 max;
	u32 tmp;

	if (0x1 != sscanf(buf, "%u", &max)) {
		dev_err(dev, "<ERR> wrong parameter, echo freq > max_freq to set min constraint\n");
		return -E2BIG;
	}

	mutex_lock(&constraint_lock);
	if (act_info->min_freq && act_info->min_freq > max) {
		mutex_unlock(&constraint_lock);
		dev_err(dev, "invalid max value %u\n", max);
		return -EINVAL;
	}

	act_info->max_freq = max;
	tmp = clk_get_rate(act_info->ddr_clk) / KHZ_TO_HZ;
	if (act_info->max_freq && tmp > max)
		update_freq(max);
	mutex_unlock(&constraint_lock);

	return size;
}

static ssize_t show_avail(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int s = 0;
	u32 len, i;

	len = act_info->ddr_freq_tbl_len;

	for (i = 0; i < len; i++)
		s += sprintf(buf + s, "%d ", act_info->ddr_freq_tbl[i]);

	s += sprintf(buf + s, "\n");

	return s;
}

static ssize_t show_interval(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%u\n", readl(act_info->act_mon_base + 0x20));
}

static ssize_t store_interval(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	u32 tmp;

	if (0x1 != sscanf(buf, "%u", &tmp)) {
		dev_err(dev, "<ERR> wrong parameter, echo val > polling_interval\n");
		return -E2BIG;
	}

	writel(tmp, act_info->act_mon_base + 0x20);

	return size;
}

static ssize_t show_up(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	u32 tmp;

	tmp = readl(act_info->act_mon_base + 0x24) >> 8;
	tmp &= THRESH_UP_MASK;

	return sprintf(buf, "%u\n", tmp);
}

static ssize_t show_down(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	u32 tmp;

	tmp = readl(act_info->act_mon_base + 0x24);
	tmp &= THRESH_LOW_MASK;

	return sprintf(buf, "%u\n", tmp);
}

/*
 * up_down: 1 highthreshold; 0 lowthreshlod
 */
void mck5_set_threshold(u32 up_down, u32 val)
{
	u32 tmp;

	mutex_lock(&act_reg_lock);
	tmp = readl(act_info->act_mon_base + 0x24);
	if (up_down) {
		tmp &= ~(THRESH_UP_MASK << 8);
		tmp |= (val & THRESH_UP_MASK) << 8;
	} else {
		tmp &= ~THRESH_LOW_MASK;
		tmp |= val & THRESH_LOW_MASK;
	}
	writel(tmp, act_info->act_mon_base + 0x24);
	mutex_unlock(&act_reg_lock);
}


static ssize_t store_up(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	u32 val;

	if (0x1 != sscanf(buf, "%u", &val)) {
		dev_err(dev, "<ERR> wrong parameter, echo val > upthreshold\n");
		return -E2BIG;
	}

	mck5_set_threshold(1, val);

	return size;
}

static ssize_t store_down(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t size)
{
	u32 val;

	if (0x1 != sscanf(buf, "%u", &val)) {
		dev_err(dev, "<ERR> wrong parameter, echo val > downthreshold\n");
		return -E2BIG;
	}

	mck5_set_threshold(0, val);

	return size;
}

static DEVICE_ATTR(act_reg_dump, S_IRUGO, act_show, NULL);
static DEVICE_ATTR(disable_ddr_fc, S_IRUGO | S_IWUSR, disable_show,
		disable_store);
static DEVICE_ATTR(ddr_freq, S_IRUGO | S_IWUSR, show_freq, store_freq);
static DEVICE_ATTR(min_freq, S_IRUGO | S_IWUSR, show_min, store_min);
static DEVICE_ATTR(max_freq, S_IRUGO | S_IWUSR, show_max, store_max);
static DEVICE_ATTR(available_freqs, S_IRUGO, show_avail, NULL);
static DEVICE_ATTR(polling_interval, S_IRUGO | S_IWUSR, show_interval,
		store_interval);
static DEVICE_ATTR(upthreshold, S_IRUGO | S_IWUSR, show_up, store_up);
static DEVICE_ATTR(downthreshold, S_IRUGO | S_IWUSR, show_down, store_down);

static struct attribute *act_attrs[] = {
	&dev_attr_act_reg_dump.attr,
	&dev_attr_disable_ddr_fc.attr,
	&dev_attr_ddr_freq.attr,
	&dev_attr_min_freq.attr,
	&dev_attr_max_freq.attr,
	&dev_attr_available_freqs.attr,
	&dev_attr_polling_interval.attr,
	&dev_attr_upthreshold.attr,
	&dev_attr_downthreshold.attr,
	NULL
};

static unsigned long level2val(unsigned long min)
{
	int i;

	if (min == 0)
		return 0;

	for (i = 0; act_info->qos_table[i].qos_value; i++)
		if (min <= act_info->qos_table[i].qos_value)
			return act_info->qos_table[i].freq;

	return act_info->qos_table[i - 1].freq;
}

static struct attribute_group act_attr_group = {
	.attrs = act_attrs,
};

static int act_min_notify(struct notifier_block *b,
			 unsigned long min, void *v)
{
	u32 len, tmp;

	mutex_lock(&constraint_lock);
	act_info->min_freq = level2val(min);

	len = act_info->ddr_freq_tbl_len;

	/* if dfc disabled, do nothing */
	if (atomic_read(&act_info->is_disabled)) {
		mutex_unlock(&constraint_lock);
		return NOTIFY_OK;
	}

	if (act_info->min_freq > act_info->ddr_freq_tbl[len - 1] ||
	    (act_info->max_freq && act_info->min_freq > act_info->max_freq)) {
		mutex_unlock(&constraint_lock);
		pr_err("invalid min value: %lu\n", min);
		return NOTIFY_OK;
	}

	tmp = clk_get_rate(act_info->ddr_clk) / KHZ_TO_HZ;
	if (tmp < act_info->min_freq)
		update_freq(act_info->min_freq);

	mutex_unlock(&constraint_lock);

	return NOTIFY_OK;
}

static struct notifier_block act_min_notifier = {
	.notifier_call = act_min_notify,
};

/* dclk = 400M
 * window in sec = 50ms
 * window in tick = 400M / (1000 / 50)
 *		= 20000000 cycles
 *
 * ACT_MON_PERIOD = window in tick
 */
void mck5activity_moniter_window(u32 dclk)
{
#if defined(CONFIG_CPU_PXA1986)
	/* FIXME: transfer mtps to dclk to calculate sample window
	 * currently use 1:2 mode with ddr fc by default.
	 */
	dclk = dclk / 2;
	writel(dclk * MHZ_TO_HZ / (1000 / SAMPLE_WINDOWS_LEN),
			act_info->act_mon_base + 0x20);
#else
	writel(dclk * KHZ_TO_HZ / (1000 / SAMPLE_WINDOWS_LEN),
			act_info->act_mon_base + 0x20);
#endif
}

static struct am_frequency_table *get_ddrfreq_tbl(void)
{
	u32 i = 0;
	u32 ddr_freq_num = pxa1928_get_ddr_op_num();
	struct am_frequency_table *am_table;

	am_table = kmalloc(sizeof(struct am_frequency_table) *
					(ddr_freq_num + 1), GFP_KERNEL);
	if (!am_table)
		return NULL;

	for (i = 0; i < ddr_freq_num; i++) {
		am_table[i].index = i;
		am_table[i].frequency = pxa1928_get_ddr_op_rate(i);
	}
	am_table[i].index = i;
	am_table[i].frequency = AM_TABLE_END;

	return am_table;
}

static int act_monitor_probe(struct platform_device *pdev)
{
	struct act_mon_platform_data *pdata = NULL;
	struct device_node *np = pdev->dev.of_node;
	struct devfreq_pm_qos_table *qos_list;
	struct am_frequency_table *tbl;
	struct resource *res;
	int freq_qos;
	int i = 0;
	int ret;
	int high_thr, low_thr;
	u32 tmp;

	act_info = kzalloc(sizeof(struct act_monitor_info), GFP_KERNEL);
	if (!act_info) {
		dev_err(&pdev->dev, "Cannot allocate memory.\n");
		return -ENOMEM;
	}

	if (IS_ENABLED(CONFIG_OF)) {
		if (of_property_read_u32(np, "marvell,high-thr", &high_thr)) {
			dev_err(&pdev->dev, "%s: get high-thr fail\n",
					__func__);
			ret = -EINVAL;
			goto failed_free_act_info;
		}

		if (of_property_read_u32(np, "marvell,low-thr", &low_thr)) {
			dev_err(&pdev->dev, "%s: get low-thr fail\n",
					__func__);
			ret = -EINVAL;
			goto failed_free_act_info;
		}

		/* save ddr frequency tbl */
		i = 0;
		tbl = get_ddrfreq_tbl();
		if (tbl) {
			while (tbl->frequency != DEVFREQ_TABLE_END) {
				act_info->ddr_freq_tbl[i] = tbl->frequency;
				tbl++;
				i++;
			}
			act_info->ddr_freq_tbl_len = i;
		}

		if (of_property_read_bool(np, "marvell,qos"))
			freq_qos = 1;

		if (freq_qos) {
			qos_list = devm_kzalloc(&pdev->dev,
					sizeof(struct devfreq_pm_qos_table)
					* act_info->ddr_freq_tbl_len,
					GFP_KERNEL);
			if (qos_list == NULL) {
				dev_err(&pdev->dev, "Cannot allocate memory for qos_list.\n");
				ret = -ENOMEM;
				goto failed_free_act_info;
			}

			for (i = 0; i < act_info->ddr_freq_tbl_len; i++) {
				qos_list[i].freq = act_info->ddr_freq_tbl[i];
				qos_list[i].qos_value = DDR_CONSTRAINT_LVL0 + i;
				dev_dbg(&pdev->dev,
						"ddr_devfreq: qos: %ld, %d\n",
						qos_list[i].freq,
						qos_list[i].qos_value);
			}
			/* add the tail of qos_list */
			qos_list[i].freq = 0;
			qos_list[i].qos_value = 0;

			act_info->qos_table = qos_list;
		}
	} else {
		pdata = (struct act_mon_platform_data *)pdev->dev.platform_data;
		if (!pdata) {
			dev_err(&pdev->dev, "No Platform data!\n");
			ret = -EINVAL;
			goto failed_free_act_info;
		}

		act_info->qos_table = pdata->qos_list;
		if (pdata->qos_list == NULL) {
			dev_err(&pdev->dev, "no qos_list defined!\n");
			ret = -ENOENT;
			goto failed_free_act_info;
		}

		/* save ddr frequency tbl */
		i = 0;
		if (pdata->freq_table != NULL) {
			while (pdata->freq_table[i].frequency != AM_TABLE_END) {
				act_info->ddr_freq_tbl[i] =
					pdata->freq_table[i].frequency;
				i++;
			}
			act_info->ddr_freq_tbl_len = i;
		}

		high_thr = pdata->high_thr;
		low_thr = pdata->low_thr;
	}

	if (high_thr > 100 || low_thr > 100) {
		dev_err(&pdev->dev, "wrong threshold value\n");
		ret = -EINVAL;
		goto failed_free_act_info;
	}

	act_info->max_freq = ddr_max;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "no IO memory defined!\n");
		ret = -ENOENT;
		goto failed_free_act_info;
	}

	act_info->irq = platform_get_irq(pdev, 0);
	if (act_info->irq < 0) {
		dev_err(&pdev->dev, "no IRQ defined\n");
		ret = -ENOENT;
		goto failed_free_act_info;
	}

	ret = request_threaded_irq(act_info->irq, NULL,
			act_mon_threaded_handler, IRQF_ONESHOT,
			pdev->name, NULL);
	if (ret < 0) {
		dev_err(&pdev->dev, "unabled to request irq\n");
		ret = -ENXIO;
		goto failed_free_act_info;
	}

	act_info->act_mon_base = devm_ioremap_nocache(&pdev->dev, res->start,
			resource_size(res));
	if (act_info->act_mon_base == NULL) {
		dev_err(&pdev->dev, "%s: res %lx - %lx map failed\n", __func__,
				(unsigned long)res->start,
				(unsigned long)res->end);
		ret = -ENOMEM;
		goto failed_free_irq;
	}

	act_info->ddr_clk = clk_get_sys("devfreq-ddr", NULL);
	if (IS_ERR(act_info->ddr_clk)) {
		dev_err(&pdev->dev, "Cannot get clk ptr.\n");
		ret = PTR_ERR(act_info->ddr_clk);
		goto failed_ioremap;
	}

	pm_qos_add_notifier(PM_QOS_DDR_ACT_MIN, &act_min_notifier);

	pm_qos_add_request(&act_qos_req_min, PM_QOS_DDR_ACT_MIN,
			PM_QOS_DEFAULT_VALUE);
	mutex_init(&constraint_lock);
	mutex_init(&act_reg_lock);

	atomic_set(&act_info->is_disabled, 0);

#if defined(CONFIG_CPU_PXA1986)
	tmp = clk_get_rate(act_info->ddr_clk) / MHZ_TO_HZ;
#else
	tmp = clk_get_rate(act_info->ddr_clk) / KHZ_TO_HZ;
#endif
	mck5activity_moniter_window(tmp);

	writel(high_thr << 8 | low_thr,
		act_info->act_mon_base + 0x24);

	/* enable activity monitor interrupt */
	tmp = readl(act_info->act_mon_base + 4);
	tmp |= ACT_MON_ENALBE;
	writel(tmp, act_info->act_mon_base + 4);

	ret = sysfs_create_group(&pdev->dev.kobj, &act_attr_group);
	if (ret) {
		dev_err(&pdev->dev, "device attr create fail: %d\n", ret);
		goto failed_free_irq;
	}

	platform_set_drvdata(pdev, act_info);

	return 0;
failed_ioremap:
	devm_iounmap(&pdev->dev, act_info->act_mon_base);
failed_free_irq:
	free_irq(act_info->irq, NULL);
failed_free_act_info:
	kfree(act_info);

	return ret;
}

static int act_monitor_remove(struct platform_device *pdev)
{
	struct act_monitor_info *info =
			platform_get_drvdata(pdev);

	sysfs_remove_group(&pdev->dev.kobj, &act_attr_group);
	devm_iounmap(&pdev->dev, act_info->act_mon_base);
	free_irq(act_info->irq, NULL);
	kfree(info->ddr_freq_tbl);
	kfree(info);

	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id mmp_act_monitor_dt_match[] = {
	{.compatible = "marvell,act_monitor" },
	{},
};
#endif

static struct platform_driver act_monitor_driver = {
	.probe = act_monitor_probe,
	.remove = act_monitor_remove,
	.driver = {
		.name = "act_monitor",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(mmp_act_monitor_dt_match),
	},
};

static void __init act_monitor_exit(void)
{
	platform_driver_unregister(&act_monitor_driver);
}


static int __init act_monitor_init(void)
{
	return platform_driver_register(&act_monitor_driver);
}

fs_initcall(act_monitor_init);
module_exit(act_monitor_exit);

MODULE_DESCRIPTION("activity monitor driver");
MODULE_LICENSE("GPL");
