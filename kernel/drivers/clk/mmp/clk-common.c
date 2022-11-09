/*
 * common function for clock framework source file
 *
 * Copyright (C) 2012 Marvell
 * Chao Xie <xiechao.mail@gmail.com>
 * Zhoujie Wu <zjwu@marvell.com>
 * Lu Cao <lucao@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/cpufreq.h>
#include <linux/devfreq.h>
#include "clk.h"
#include <linux/clk/dvfs-pxa1928.h>

/* parameter passed from cmdline to identify DDR mode */
enum ddr_type ddr_mode = DDR_400M;
static int __init __init_ddr_mode(char *arg)
{
	int n;
	if (!get_option(&arg, &n))
		return 0;

	if ((n >= DDR_TYPE_MAX) || (n < DDR_400M))
		pr_info("WARNING: unknown DDR type!");
	else
		ddr_mode = n;

	return 1;
}
__setup("ddr_mode=", __init_ddr_mode);


unsigned long max_freq;
static int __init max_freq_setup(char *str)
{
	int n;
	if (!get_option(&str, &n))
		return 0;
	max_freq = n;
	return 1;
}
__setup("max_freq=", max_freq_setup);

static struct clk *clk_gc;
static struct clk *clk_gc2d;
static struct clk *clk_gcsh;
/* interface use to get peri clock avaliable op num and rate */
unsigned int __clk_periph_get_opnum(struct clk *clk)
{
	struct clk_peri *peri = to_clk_peri(clk->hw);
	struct periph_clk_tbl *cop;
	unsigned int idx = 0;

	BUG_ON(!peri);
	list_for_each_entry(cop, &peri->clktbl_list, node)
		idx++;
	return idx;
}

unsigned long __clk_periph_get_oprate(struct clk *clk,
	unsigned int index)
{
	struct clk_peri *peri = to_clk_peri(clk->hw);
	struct periph_clk_tbl *cop;
	unsigned int idx = 0;

	BUG_ON(!peri || list_empty(&peri->clktbl_list));
	list_for_each_entry(cop, &peri->clktbl_list, node) {
		if ((idx == index) ||
			list_is_last(&cop->node, &peri->clktbl_list))
			break;
		idx++;
	}
	return cop->clk_rate;
}

int __get_gcu_freqs_table(struct clk *clk,
	unsigned long *freqs_table, unsigned int *item_counts,
	unsigned int max_item_counts)
{
	unsigned int index, tbl_size;
	*item_counts = 0;

	if (!freqs_table) {
		pr_err("%s NULL ptr!\n", __func__);
		return -EINVAL;
	}

	tbl_size = __clk_periph_get_opnum(clk);
	if (max_item_counts < tbl_size) {
		pr_err("%s [%s]Too many GC frequencies %u!\n",
			__func__, clk->name,
			max_item_counts);
		return -EINVAL;
	}

	for (index = 0; index < tbl_size; index++)
		freqs_table[index] =
			__clk_periph_get_oprate(clk, index);

	*item_counts = index;
	return 0;
}

/* API used by GC driver to get avaliable GC3d/2d/shader frequencies, unit HZ */
int get_gcu_freqs_table(unsigned long *gcu_freqs_table,
	unsigned int *item_counts, unsigned int max_item_counts)
{
	if (unlikely(!clk_gc)) {
		clk_gc = clk_get_sys(NULL, "GCCLK");
		BUG_ON(!clk_gc);
	}
	return __get_gcu_freqs_table(clk_gc, gcu_freqs_table,
		item_counts, max_item_counts);
}
EXPORT_SYMBOL(get_gcu_freqs_table);

int get_gcu2d_freqs_table(unsigned long *gcu_freqs_table,
	unsigned int *item_counts, unsigned int max_item_counts)
{
	if (unlikely(!clk_gc2d)) {
		clk_gc2d = clk_get_sys(NULL, "GC2DCLK");
		BUG_ON(!clk_gc2d);
	}
	return __get_gcu_freqs_table(clk_gc2d, gcu_freqs_table,
		item_counts, max_item_counts);
}
EXPORT_SYMBOL(get_gcu2d_freqs_table);

int get_gc_shader_freqs_table(unsigned long *gcu_freqs_table,
	unsigned int *item_counts, unsigned int max_item_counts)
{
	if (unlikely(!clk_gcsh)) {
		clk_gcsh = clk_get_sys(NULL, "GC_SHADER_CLK");
		BUG_ON(!clk_gcsh);
	}
	return __get_gcu_freqs_table(clk_gcsh, gcu_freqs_table,
		item_counts, max_item_counts);
}
EXPORT_SYMBOL(get_gc_shader_freqs_table);

enum comp {
	_CORE,
	_DDR,
	_AXI,
	BOOT_DVFS_MAX,
};

static unsigned long minfreq[BOOT_DVFS_MAX] = { 0, 0, 0 };
static unsigned long bootfreq[BOOT_DVFS_MAX];
static struct clk *clk[BOOT_DVFS_MAX];
static char *clk_name[BOOT_DVFS_MAX] = {"cpu", "ddr", "axi"};

static struct pm_qos_request sdh_core_qos_max, sdh_ddr_qos_max;
static struct pm_qos_request sdh_core_qos_min;

static int __init sdh_tuning_init(void)
{
#if defined(CONFIG_CPU_FREQ) && defined(CONFIG_PM_DEVFREQ)
	struct cpufreq_frequency_table *cpufreq_table =
		cpufreq_frequency_get_table(0);
	struct devfreq_frequency_table *ddrfreq_table =
		devfreq_frequency_get_table(DEVFREQ_DDR);

	if (cpufreq_table)
		minfreq[_CORE] = cpufreq_table[0].frequency;
	else
		pr_err("%s cpufreq_table get failed, use 0 to set!\n", __func__);

	if (ddrfreq_table)
		minfreq[_DDR] = ddrfreq_table[0].frequency;
	else
		pr_err("%s ddrfreq_table get failed, use 0 to set!\n", __func__);
#else
	pr_info("%s CONFIG_CPU_FREQ & CONFIG_PM_DEVFREQ not defined!\n", __func__);
	minfreq[_CORE] = 0;
	minfreq[_DDR] = 0;
#endif

	sdh_core_qos_max.name = "sdh_tuning_core_max";
	pm_qos_add_request(&sdh_core_qos_max,
			PM_QOS_CPUFREQ_MAX, PM_QOS_DEFAULT_VALUE);

	sdh_core_qos_min.name = "sdh_tuning_core_min";
	pm_qos_add_request(&sdh_core_qos_min,
			PM_QOS_CPUFREQ_MIN, PM_QOS_DEFAULT_VALUE);

	sdh_ddr_qos_max.name = "sdh_tuning_ddr_max";
	pm_qos_add_request(&sdh_ddr_qos_max,
			PM_QOS_DDR_DEVFREQ_MAX, PM_QOS_DEFAULT_VALUE);

	return 0;
}
arch_initcall(sdh_tuning_init);

int sdh_tunning_savefreq(void)
{
	int i;

	for (i = 0; i < BOOT_DVFS_MAX; i++) {
		if (unlikely(!clk[i])) {
			clk[i] = clk_get(NULL, clk_name[i]);
			if (!clk[i]) {
				pr_err("failed to get clk %s\n", clk_name[i]);
				return -1;
			}
		}

		if (!bootfreq[i])
			bootfreq[i] = clk_get_rate(clk[i]);

	}
#if defined(CONFIG_ARM64) && defined(CONFIG_PXA_DVFS)
	return 0;
#else
	clk_set_rate(clk[_AXI], minfreq[_AXI]*1000);
	pm_qos_update_request(&sdh_core_qos_max, minfreq[_CORE]);
	pm_qos_update_request(&sdh_ddr_qos_max, minfreq[_DDR]);
#endif
	return 0;
}

#if defined(CONFIG_ARM64) && defined(CONFIG_PXA_DVFS)
int sdh_tunning_updatefreq(unsigned int dvfs_level)
{
	long freq;

	/* request PM_QOS_CPUFREQ_MAX */
	freq = get_dvc_freq(0, dvfs_level);
	if (freq >= 0) {
		pm_qos_update_request(&sdh_core_qos_max, (s32)(freq/1000));
		pm_qos_update_request(&sdh_core_qos_min, (s32)(freq/1000));
	} else {
		pm_qos_update_request(&sdh_core_qos_max, 0);
		pr_err("fail to get CORE freq for dvfs level %d\n", dvfs_level);
	}
	/* request PM_QOS_DDR_DEVFREQ_MAX */
	freq = get_dvc_freq(2, dvfs_level);
	if (freq >= 0) {
		pm_qos_update_request(&sdh_ddr_qos_max, (s32)(freq/1000));
	} else {
		pm_qos_update_request(&sdh_ddr_qos_max, 0);
		pr_err("fail to get DDR freq for dvfs level %d\n", dvfs_level);
	}

	return 0;
}
#else
int sdh_tunning_updatefreq(unsigned int dvfs_level)
{
	return 0;
}
#endif

int sdh_tunning_restorefreq(void)
{
	int i;

	pm_qos_update_request(&sdh_core_qos_max, PM_QOS_DEFAULT_VALUE);
	pm_qos_update_request(&sdh_ddr_qos_max, PM_QOS_DEFAULT_VALUE);
#if defined(CONFIG_ARM64) && defined(CONFIG_PXA_DVFS)
	pm_qos_update_request(&sdh_core_qos_min, PM_QOS_DEFAULT_VALUE);
#endif
	for (i = 0; i < BOOT_DVFS_MAX; i++) {
		if (!clk[i])
			continue;
		if (i != _CORE)
			clk_set_rate(clk[i], bootfreq[i]);
	}
	return 0;
}

static unsigned int platvl_min, platvl_max;

void plat_set_vl_min(u32 vl_num)
{
	platvl_min = vl_num;
}

unsigned int plat_get_vl_min(void)
{
	return platvl_min;
}
void plat_set_vl_max(u32 vl_num)
{
	platvl_max = vl_num;
}

unsigned int plat_get_vl_max(void)
{
	return platvl_max;
}
