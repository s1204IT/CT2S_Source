/* linux/drivers/char/watchdog/pxa988_wdt.c
 *
 *	Yi Zhang <yizhang@marvell.com>
 *
 * Pxa988 Watchdog Timer Support
 *
 * Based on, s2c2410_wdt.c by Ben Dooks,
 *	(c) Copyright 2012 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/miscdevice.h> /* for MODULE_ALIAS_MISCDEV */
#include <linux/watchdog.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/cpufreq.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/pxa1928_smc.h>

#include <linux/of.h>

/* Watchdog Timer Registers Offset */
#define TMR_WMER	(0x0064)
#define TMR_WMR		(0x0068)
#define TMR_WVR		(0x006c)
#define TMR_WCR		(0x0098)
#define TMR_WSR		(0x0070)
#define TMR_WFAR	(0x009c)
#define TMR_WSAR	(0x00A0)

#define CONFIG_PXA988_WATCHDOG_ATBOOT		(0)
/* default timeout is 60s */
#define CONFIG_PXA988_WATCHDOG_DEFAULT_TIME	(60)
#define PXA988_WATCHDOG_EXPIRE_TIME		(100)
#define PXA988_WATCHDOG_FEED_TIMEOUT	(90 * HZ)

#define MPMU_APRR		(0x1020)
#define MPMU_WDTPCR		(0x0200)
#define MPMU_APRR_WDTR	(1<<4)
#define DEFAULT_SHIFT (8)

static bool nowayout	= WATCHDOG_NOWAYOUT ? true : false;
static int tmr_atboot	= CONFIG_PXA988_WATCHDOG_ATBOOT;
static int reboot_lock;

module_param(tmr_atboot,  int, 0);
module_param(nowayout,   bool, 0);

MODULE_PARM_DESC(tmr_atboot,
		"Watchdog is started at boot time if set to 1, default="
			__MODULE_STRING(CONFIG_PXA988_WATCHDOG_ATBOOT));
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
			__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");


struct pxa988_wdt_info {
	struct resource	*wdt_mem;
	struct resource	*mpmu_mem;
	void __iomem	*wdt_base;
	void __iomem	*mpmu_base;
	struct device *dev;
	struct workqueue_struct *wdt_wq;
	struct delayed_work wdt_work;
	spinlock_t wdt_lock;
	struct watchdog_device wdt_dev;
	unsigned long wtmatch_save;
	unsigned long wtcount_save;
	unsigned long wtdiff_save;
	int ctrl;

};
struct pxa988_wdt_info *wdt_info;

#ifdef CONFIG_GLB_SECURE
static int pxa988wdt_stop(struct watchdog_device *wdd)
{
	struct pxa988_wdt_info *info =
		container_of(wdd, struct pxa988_wdt_info, wdt_dev);
	spin_lock(&info->wdt_lock);

	/* reset counter */
	smc_timer2_write(TMR_WFAR, 0xbaba);
	smc_timer2_write(TMR_WSAR, 0xeb10);
	smc_timer2_write(TMR_WCR, 0x1);

	/* disable WDT */
	smc_timer2_write(TMR_WFAR, 0xbaba);
	smc_timer2_write(TMR_WSAR, 0xeb10);
	smc_timer2_write(TMR_WMER, 0x0);

	spin_unlock(&info->wdt_lock);

	return 0;
}

static int pxa988wdt_start(struct watchdog_device *wdd)
{
	struct pxa988_wdt_info *info =
		container_of(wdd, struct pxa988_wdt_info, wdt_dev);
	u32 reg;
	void __iomem *mpmu_aprr;

	spin_lock(&info->wdt_lock);

	/*
	 * enable WDT
	 * 1. write 0xbaba to match 1st key
	 * 2. write 0xeb10 to match 2nd key
	 * 3. enable wdt count, reboot when experies
	 */
	smc_timer2_write(TMR_WFAR, 0xbaba);
	smc_timer2_write(TMR_WSAR, 0xeb10);
	smc_timer2_write(TMR_WMER, 0x3);

	/* negate hardware reset to the WDT after system reset */
	mpmu_aprr = info->mpmu_base + MPMU_APRR;
	reg = readl(mpmu_aprr) | MPMU_APRR_WDTR;
	writel(reg, mpmu_aprr);

	/* clear previous WDT status */
	smc_timer2_write(TMR_WFAR, 0xbaba);
	smc_timer2_write(TMR_WSAR, 0xeb10);
	smc_timer2_write(TMR_WSR, 0);

	spin_unlock(&info->wdt_lock);

	return 0;
}

static int pxa988wdt_keepalive(struct watchdog_device *wdd)
{
	int count, match, ret = 0;

	struct pxa988_wdt_info *info =
		container_of(wdd, struct pxa988_wdt_info, wdt_dev);
	spin_lock(&info->wdt_lock);
	/* read count */
	smc_timer2_write(TMR_WFAR, 0xbaba);
	smc_timer2_write(TMR_WSAR, 0xeb10);
	count = smc_timer2_read(TMR_WVR);
	/* for debug */
	smc_timer2_write(TMR_WFAR, 0xbaba);
	smc_timer2_write(TMR_WSAR, 0xeb10);
	match = smc_timer2_read(TMR_WMR);

	dev_dbg(info->dev, "before: count = %d, match = %d\n",
		 count, match);

	/* update match */
	if (wdd->timeout > 0 && !reboot_lock) {
		smc_timer2_write(TMR_WFAR, 0xbaba);
		smc_timer2_write(TMR_WSAR, 0xeb10);
		smc_timer2_write(TMR_WMR, count + wdd->timeout);
	} else
		ret = -EINVAL;

	/* read count */
	smc_timer2_write(TMR_WFAR, 0xbaba);
	smc_timer2_write(TMR_WSAR, 0xeb10);
	count = smc_timer2_read(TMR_WVR);
	/* for debug */
	smc_timer2_write(TMR_WFAR, 0xbaba);
	smc_timer2_write(TMR_WSAR, 0xeb10);
	match = smc_timer2_read(TMR_WMR);

	dev_dbg(info->dev, "after: count = %d, match = %d\n",
		 count, match);
	spin_unlock(&info->wdt_lock);

	return ret;
}

static
int pxa988wdt_set_heartbeat(struct watchdog_device *wdd, unsigned timeout)
{
	struct pxa988_wdt_info *info =
		container_of(wdd, struct pxa988_wdt_info, wdt_dev);
	/*
	 * the wdt timer is 16 bit,
	 * frequence is 256HZ
	 */
	if ((timeout <= 0) ||
	    ((long long)timeout) > 0xffff) {
		dev_info(info->dev, "use default value!\n");
		timeout = CONFIG_PXA988_WATCHDOG_DEFAULT_TIME;
	}

	smc_timer2_write(TMR_WFAR, 0xbaba);
	smc_timer2_write(TMR_WSAR, 0xeb10);
	smc_timer2_write(TMR_WMR, timeout);

	wdd->timeout = timeout;

	return 0;
}
#if 0
static irqreturn_t pxa988_wdt_handler(int irq, void *data)
{
	struct pxa988_wdt_info *info =
		(struct pxa988_wdt_info *)data;

	pr_info("\n%s is called: dong...\n", __func__);
	/* clear interrupt */
	smc_timer2_write(TMR_WFAR, 0xbaba);
	smc_timer2_write(TMR_WSAR, 0xeb10);
	smc_timer2_write(TMR_WICR, 0x1);
	/* update match = timeout + count */
	pxa988wdt_keepalive(&info->wdt_dev);
	return IRQ_HANDLED;
}
#endif
#else
static int pxa988wdt_stop(struct watchdog_device *wdd)
{
	struct pxa988_wdt_info *info =
		container_of(wdd, struct pxa988_wdt_info, wdt_dev);
	spin_lock(&info->wdt_lock);

	/* reset counter */
	writel(0xbaba, info->wdt_base + TMR_WFAR);
	writel(0xeb10, info->wdt_base + TMR_WSAR);
	writel(0x1, info->wdt_base + TMR_WCR);

	/* disable WDT */
	writel(0xbaba, info->wdt_base + TMR_WFAR);
	writel(0xeb10, info->wdt_base + TMR_WSAR);
	writel(0x0, info->wdt_base + TMR_WMER);

	spin_unlock(&info->wdt_lock);

	return 0;
}

static int pxa988wdt_start(struct watchdog_device *wdd)
{
	struct pxa988_wdt_info *info =
		container_of(wdd, struct pxa988_wdt_info, wdt_dev);
	u32 reg;
	void __iomem *mpmu_aprr;

	spin_lock(&info->wdt_lock);

	/*
	 * enable WDT
	 * 1. write 0xbaba to match 1st key
	 * 2. write 0xeb10 to match 2nd key
	 * 3. enable wdt count, reboot when experies
	 */
	writel(0xbaba, info->wdt_base + TMR_WFAR);
	writel(0xeb10, info->wdt_base + TMR_WSAR);
	writel(0x3, info->wdt_base + TMR_WMER);

	/* negate hardware reset to the WDT after system reset */
	mpmu_aprr = info->mpmu_base + MPMU_APRR;
	reg = readl(mpmu_aprr) | MPMU_APRR_WDTR;
	writel(reg, mpmu_aprr);

	/* clear previous WDT status */
	writel(0xbaba, info->wdt_base + TMR_WFAR);
	writel(0xeb10, info->wdt_base + TMR_WSAR);
	writel(0, info->wdt_base + TMR_WSR);

	spin_unlock(&info->wdt_lock);

	return 0;
}

static int pxa988wdt_keepalive(struct watchdog_device *wdd)
{
	int count, match, ret = 0;

	struct pxa988_wdt_info *info =
		container_of(wdd, struct pxa988_wdt_info, wdt_dev);
	spin_lock(&info->wdt_lock);
	/* read count */
	writel(0xbaba, info->wdt_base + TMR_WFAR);
	writel(0xeb10, info->wdt_base + TMR_WSAR);
	count = readl(info->wdt_base + TMR_WVR);
	/* for debug */
	writel(0xbaba, info->wdt_base + TMR_WFAR);
	writel(0xeb10, info->wdt_base + TMR_WSAR);
	match = readl(info->wdt_base + TMR_WMR);

	dev_dbg(info->dev, "before: count = %d, match = %d\n",
		 count, match);

	/* update match */
	if (wdd->timeout > 0 && !reboot_lock) {
		writel(0xbaba, info->wdt_base + TMR_WFAR);
		writel(0xeb10, info->wdt_base + TMR_WSAR);
		writel(count + wdd->timeout, info->wdt_base + TMR_WMR);
	} else
		ret = -EINVAL;

	/* read count */
	writel(0xbaba, info->wdt_base + TMR_WFAR);
	writel(0xeb10, info->wdt_base + TMR_WSAR);
	count = readl(info->wdt_base + TMR_WVR);
	/* for debug */
	writel(0xbaba, info->wdt_base + TMR_WFAR);
	writel(0xeb10, info->wdt_base + TMR_WSAR);
	match = readl(info->wdt_base + TMR_WMR);

	dev_dbg(info->dev, "after: count = %d, match = %d\n",
		 count, match);
	spin_unlock(&info->wdt_lock);

	return ret;
}

static
int pxa988wdt_set_heartbeat(struct watchdog_device *wdd, unsigned timeout)
{
	struct pxa988_wdt_info *info =
		container_of(wdd, struct pxa988_wdt_info, wdt_dev);
	/*
	 * the wdt timer is 16 bit,
	 * frequence is 256HZ
	 */
	if ((timeout <= 0) ||
	    ((long long)timeout) > 0xffff) {
		dev_info(info->dev, "use default value!\n");
		timeout = CONFIG_PXA988_WATCHDOG_DEFAULT_TIME;
	}

	writel(0xbaba, info->wdt_base + TMR_WFAR);
	writel(0xeb10, info->wdt_base + TMR_WSAR);
	writel(timeout, info->wdt_base + TMR_WMR);

	wdd->timeout = timeout;

	return 0;
}
#if 0
static irqreturn_t pxa988_wdt_handler(int irq, void *data)
{
	struct pxa988_wdt_info *info =
		(struct pxa988_wdt_info *)data;

	pr_info("\n%s is called: dong...\n", __func__);
	/* clear interrupt */
	writel(0xbaba, info->wdt_base + TMR_WFAR);
	writel(0xeb10, info->wdt_base + TMR_WSAR);
	writel(0x1, info->wdt_base + TMR_WICR);
	/* update match = timeout + count */
	pxa988wdt_keepalive(&info->wdt_dev);
	return IRQ_HANDLED;
}
#endif
#endif

#define OPTIONS (WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING)

static const struct watchdog_info pxa988_wdt_ident = {
	.options          =     OPTIONS,
	.firmware_version =	0,
	.identity         =	"PXA988 Watchdog",
};

static struct watchdog_ops pxa988wdt_ops = {
	.owner = THIS_MODULE,
	.start = pxa988wdt_start,
	.stop = pxa988wdt_stop,
	.ping = pxa988wdt_keepalive,
	.set_timeout = pxa988wdt_set_heartbeat,
};

static struct watchdog_device pxa988_wdt = {
	.info = &pxa988_wdt_ident,
	.ops = &pxa988wdt_ops,
};


static void wdt_work_func(struct work_struct *work)
{
	struct pxa988_wdt_info *info =
		container_of(work, struct pxa988_wdt_info,
			     wdt_work.work);

	if (info->ctrl)
		/* update match = timeout + count */
		pxa988wdt_keepalive(&info->wdt_dev);
	/*
	 * set workqueue scheduled intval
	 * be sure the intval < timeout to feed wdt timely
	 */
	queue_delayed_work(info->wdt_wq, &info->wdt_work,
			   PXA988_WATCHDOG_FEED_TIMEOUT);
}

static void pxa988_init_wdt(struct pxa988_wdt_info *info)
{
	if (info->ctrl)
		pxa988wdt_start(&info->wdt_dev);
	/* set timeout = 100s */
	pxa988wdt_set_heartbeat(&info->wdt_dev,
		(PXA988_WATCHDOG_EXPIRE_TIME << DEFAULT_SHIFT));
	if (test_bit(WDOG_ACTIVE, &((info->wdt_dev).status)))
		pxa988wdt_keepalive(&info->wdt_dev);
	/* feed watdog to launch the workqueue*/
	queue_delayed_work(info->wdt_wq, &info->wdt_work,
		PXA988_WATCHDOG_FEED_TIMEOUT);
}

#ifdef CONFIG_OF
static const struct of_device_id pxa_wdt_match[] = {
	{ .compatible = "marvell,pxa-wdt", .data = NULL},
	{},
};
MODULE_DEVICE_TABLE(of, pxa_wdt_match);
#endif

static ssize_t wdt_ctrl_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct pxa988_wdt_info *info = dev_get_drvdata(dev);
	int s = 0;

	s += sprintf(buf, "wdt control: %d\n", info->ctrl);
	return s;
}

static ssize_t wdt_ctrl_store(struct device *dev, struct device_attribute *attr,
			      const char *buf, size_t size)
{
	struct pxa988_wdt_info *info = dev_get_drvdata(dev);
	ssize_t ret = 0;

	if (info == NULL) {
		pr_err("device info is empty!\n");
		return 0;
	}
	ret = sscanf(buf, "%d", &info->ctrl);
	if (ret == 0) {
		pr_err("sscanf() error, try again\n");
		ret = -EINVAL;
	}
	dev_info(dev, "%s: wdt control %s\n",
		 __func__, (info->ctrl ? "enabled" : "disabled"));

	if (ret < 0)
		return ret;

	if (info->ctrl)
		pxa988wdt_start(&info->wdt_dev);
	else
		pxa988wdt_stop(&info->wdt_dev);

	return size;
}
static DEVICE_ATTR(wdt_ctrl, S_IRUGO | S_IWUSR, wdt_ctrl_show, wdt_ctrl_store);

#ifdef CONFIG_OF
static int pxa988wdt_dt_init(struct device_node *np, struct device *dev,
			    struct pxa988_wdt_info *info)
{
	if (info == NULL) {
		pr_err("watchdog dt is empty!\n");
		return -EINVAL;
	}
	if (of_get_property(np, "marvell,pxa-wdt-disabled", NULL))
		info->ctrl = 0;
	else
		info->ctrl = 1;
	return 0;
}
#endif

static int pxa988wdt_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;

	int started = 0;
	int ret;
	static int is_wdt_reset;
	struct pxa988_wdt_info *info;
	info = devm_kzalloc(&pdev->dev, sizeof(struct pxa988_wdt_info),
			GFP_KERNEL);
	if (info == NULL) {
		dev_err(&pdev->dev, "Cannot allocate memory.\n");
		return -ENOMEM;
	}

	info->dev = &pdev->dev;

	info->wdt_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (info->wdt_mem == NULL) {
		dev_err(info->dev, "no memory resource specified for WDT\n");
		return -ENOENT;
	}

	info->wdt_base = devm_ioremap_nocache(&pdev->dev, info->wdt_mem->start,
						  resource_size(info->wdt_mem));
	if (IS_ERR(info->wdt_base))
		return PTR_ERR(info->wdt_base);
	/* dev_info(info->dev, "mapped wdt_base=%p\n", info->wdt_base); */

	info->mpmu_mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (info->mpmu_mem == NULL) {
		dev_err(info->dev, "no memory resource specified for MPMU\n");
		return -ENOENT;
	}

	info->mpmu_base = devm_ioremap_nocache(&pdev->dev,
		info->mpmu_mem->start, resource_size(info->mpmu_mem));
	if (IS_ERR(info->mpmu_base))
		return PTR_ERR(info->mpmu_base);

	/* check before the watchdog is initialized */
#ifdef CONFIG_GLB_SECURE
	is_wdt_reset = smc_timer2_read(TMR_WSR);
#else
	is_wdt_reset = readl(info->wdt_base + TMR_WSR);
#endif
	if (is_wdt_reset)
		pr_info("System boots up because of SoC watchdog reset.\n");
	else
		pr_info("System boots up not because of SoC watchdog reset.\n");
	/* watchdog status is cleared when it's enabled, no need to clear here */

	spin_lock_init(&info->wdt_lock);

	info->wdt_dev = pxa988_wdt;
	ret = pxa988wdt_set_heartbeat(&info->wdt_dev, 0);
	if (ret) {
		dev_err(info->dev, "set timeout error(%d)\n", ret);
		return ret;
	}

	watchdog_set_nowayout(&pxa988_wdt, nowayout);

	ret = watchdog_register_device(&info->wdt_dev);
	if (ret) {
		dev_err(info->dev, "cannot register watchdog (%d)\n", ret);
		return ret;
	}

	if (tmr_atboot && started == 0) {
		dev_info(info->dev, "starting watchdog timer\n");
		pxa988wdt_start(&info->wdt_dev);
	} else if (!tmr_atboot) {
		/* if we're not enabling the watchdog, then ensure it is
		 * disabled if it has been left running from the bootloader
		 * or other source */
		pxa988wdt_stop(&info->wdt_dev);
	}
	info->wdt_wq = alloc_workqueue("wdt_workqueue", WQ_HIGHPRI |
				WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (info->wdt_wq == NULL) {
		dev_err(info->dev, "alloc_workqueue failed\n");
		ret = -ENOMEM;
		goto err_alloc;
	}
	INIT_DELAYED_WORK(&info->wdt_work, wdt_work_func);
	wdt_info = info;
	platform_set_drvdata(pdev, info);

#ifdef CONFIG_OF
	pxa988wdt_dt_init(np, info->dev, info);
#endif
	pxa988_init_wdt(info);

#if 0
	/* this interrupt is handled by CP only */
	ret = request_threaded_irq(67, NULL, pxa988_wdt_handler,
			     IRQF_ONESHOT, "watchdog", info);
	if (ret < 0) {
		dev_err(info->dev, "Failed to request IRQ: #%d: %d\n",
			67, ret);
		return ret;
	}
#endif

	ret = device_create_file(info->dev, &dev_attr_wdt_ctrl);
	if (ret < 0) {
		dev_err(&pdev->dev, "device attr create fail: %d\n", ret);
		return ret;
	}

	return 0;

err_alloc:
	watchdog_unregister_device(&info->wdt_dev);
	return ret;
}

static int pxa988wdt_remove(struct platform_device *pdev)
{
	struct pxa988_wdt_info *info = platform_get_drvdata(pdev);
	watchdog_unregister_device(&info->wdt_dev);
	return 0;
}

static void pxa988wdt_shutdown(struct platform_device *pdev)
{
	struct pxa988_wdt_info *info = platform_get_drvdata(pdev);
	pxa988wdt_stop(&info->wdt_dev);
}

#ifdef CONFIG_PM
#ifdef CONFIG_GLB_SECURE
static int pxa988wdt_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct pxa988_wdt_info *info = platform_get_drvdata(pdev);

	if (info->ctrl) {
		/* Save watchdog state, and turn it off. */
		smc_timer2_write(TMR_WFAR, 0xbaba);
		smc_timer2_write(TMR_WSAR, 0xeb10);
		info->wtmatch_save = smc_timer2_read(TMR_WMR);

		smc_timer2_write(TMR_WFAR, 0xbaba);
		smc_timer2_write(TMR_WSAR, 0xeb10);
		info->wtcount_save = smc_timer2_read(TMR_WVR);
		info->wtdiff_save = info->wtmatch_save - info->wtcount_save;

		/* for debug */
		/* dev_info(info->dev, "%s, wtcount = %lu, wtmatch = %lu\n",
			 __func__, info->wtcount_save, info->wtmatch_save); */

		cancel_delayed_work_sync(&info->wdt_work);
		pxa988wdt_stop(&info->wdt_dev);
	};

	return 0;
}

static int pxa988wdt_resume(struct platform_device *pdev)
{
	unsigned long count, match;
	struct pxa988_wdt_info *info = platform_get_drvdata(pdev);

	if (info->ctrl) {
		pxa988wdt_start(&info->wdt_dev);

		smc_timer2_write(TMR_WFAR, 0xbaba);
		smc_timer2_write(TMR_WSAR, 0xeb10);
		count = smc_timer2_read(TMR_WVR);

		/* Restore watchdog state. */
		smc_timer2_write(TMR_WFAR, 0xbaba);
		smc_timer2_write(TMR_WSAR, 0xeb10);
		smc_timer2_write(TMR_WMR, count + info->wtdiff_save);

		queue_delayed_work(info->wdt_wq, &info->wdt_work, 0);

		/* for debug */
		smc_timer2_write(TMR_WFAR, 0xbaba);
		smc_timer2_write(TMR_WSAR, 0xeb10);
		match = smc_timer2_read(TMR_WMR);
		/* dev_info(info->dev, "%s, count = %lu, match = %lu\n",
			 __func__, count, match); */
	};

	return 0;
}
#else
static int pxa988wdt_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct pxa988_wdt_info *info = platform_get_drvdata(pdev);

	if (info->ctrl) {
		/* Save watchdog state, and turn it off. */
		writel(0xbaba, info->wdt_base + TMR_WFAR);
		writel(0xeb10, info->wdt_base + TMR_WSAR);
		info->wtmatch_save = readl(info->wdt_base + TMR_WMR);

		writel(0xbaba, info->wdt_base + TMR_WFAR);
		writel(0xeb10, info->wdt_base + TMR_WSAR);
		info->wtcount_save = readl(info->wdt_base + TMR_WVR);
		info->wtdiff_save = info->wtmatch_save - info->wtcount_save;

		/* for debug */
		/* dev_info(info->dev, "%s, wtcount = %lu, wtmatch = %lu\n",
			 __func__, info->wtcount_save, info->wtmatch_save); */

		cancel_delayed_work_sync(&info->wdt_work);
		pxa988wdt_stop(&info->wdt_dev);
	};

	return 0;
}

static int pxa988wdt_resume(struct platform_device *pdev)
{
	unsigned long count, match;
	struct pxa988_wdt_info *info = platform_get_drvdata(pdev);

	if (info->ctrl) {
		pxa988wdt_start(&info->wdt_dev);

		writel(0xbaba, info->wdt_base + TMR_WFAR);
		writel(0xeb10, info->wdt_base + TMR_WSAR);
		count = readl(info->wdt_base + TMR_WVR);

		/* Restore watchdog state. */
		writel(0xbaba, info->wdt_base + TMR_WFAR);
		writel(0xeb10, info->wdt_base + TMR_WSAR);
		writel(count + info->wtdiff_save, info->wdt_base + TMR_WMR);

		queue_delayed_work(info->wdt_wq, &info->wdt_work, 0);

		/* for debug */
		writel(0xbaba, info->wdt_base + TMR_WFAR);
		writel(0xeb10, info->wdt_base + TMR_WSAR);
		match = readl(info->wdt_base + TMR_WMR);
		/* dev_info(info->dev, "%s, count = %lu, match = %lu\n",
			 __func__, count, match); */
	};

	return 0;
}
#endif
#else
#define pxa988wdt_suspend NULL
#define pxa988wdt_resume  NULL
#endif /* CONFIG_PM */

static struct platform_driver pxa988wdt_driver = {
	.probe		= pxa988wdt_probe,
	.remove		= pxa988wdt_remove,
	.shutdown	= pxa988wdt_shutdown,
	.suspend	= pxa988wdt_suspend,
	.resume		= pxa988wdt_resume,
	.driver		= {
		.name	= "pxa-wdt",
		.of_match_table	= of_match_ptr(pxa_wdt_match),
	},
};


module_platform_driver(pxa988wdt_driver);

MODULE_AUTHOR("Yi Zhang<yizhang@marvell.com>, ");
MODULE_DESCRIPTION("Pxa988 Watchdog Device Driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);
MODULE_ALIAS("platform:pxa988-wdt");
