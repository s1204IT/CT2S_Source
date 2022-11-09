/*
 * Copyright 2013 Marvell Semiconductor, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/* PXA1928 Thermal Implementation */

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
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/clk/mmp.h>
#include <linux/of.h>
#ifdef CONFIG_CPU_FREQ
#include <linux/cpufreq.h>
#include <linux/cpu_cooling.h>
#endif
#include <linux/devfreq.h>
#include <linux/cooling_dev_mrvl.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/pm_qos.h>
#include <trace/events/pxa.h>
#include <linux/cputype.h>
#include <linux/clk/dvfs-pxa1928.h>
#include <linux/debugfs.h>
#include "thermal_core.h"


#define CONFIG_TSEN_MAX
enum trip_points {
	TRIP_POINT_0 = 0,
	TRIP_POINT_1,
	TRIP_POINT_2,
	TRIP_POINT_3,
	TRIP_POINT_4,
	TRIP_POINT_5,
	TRIP_POINT_6,
	TRIP_POINT_MAX = TRIP_POINT_6,
	TRIP_POINTS_NUM = TRIP_POINT_MAX + 1,
};
enum trip_range {
	TRIP_RANGE_0 = 0,
	TRIP_RANGE_1,
	TRIP_RANGE_2,
	TRIP_RANGE_3,
	TRIP_RANGE_4,
	TRIP_RANGE_5,
	TRIP_RANGE_6,
	TRIP_RANGE_7,
	TRIP_RANGE_MAX = TRIP_RANGE_7,
	TRIP_RANGE_NUM = TRIP_RANGE_MAX + 1,
};


#define TSEN_NUM	4
#define TSEN_VPU	0
#define TSEN_CPU	1
#define TSEN_GPU	2
#define TSEN_MAX	3
#define TSEN_GAIN	3874
#define TSEN_OFFSET	2821
#define INVALID_TEMP	40000
#define PXA1928_THERMAL_POLLING_FREQUENCY_MS 200
#define PXA1928_SVC_OPT_TEMP_THLD 25000
#define PXA1928_WD_TEMP	115000

#define SVC_THERMAL_GAP	37500

#define TSEN_CFG_REG_0			(0x00)
#define TSEN_DEBUG_REG_0		(0x04)
#define TSEN_INT0_WDOG_THLD_REG_0	(0x1c)
#define TSEN_INT1_INT2_THLD_REG_0	(0x20)
#define TSEN_DATA_REG_0			(0x24)
#define TSEN_DATA_RAW_REG_0		(0x28)
#define TSEN_AUTO_READ_VALUE_REG_0	(0x2C)

#define TSEN_CFG_REG_1			(0x30)
#define TSEN_DEBUG_REG_1		(0x34)
#define TSEN_INT0_WDOG_THLD_REG_1	(0x3c)
#define TSEN_INT1_INT2_THLD_REG_1	(0x40)
#define TSEN_DATA_REG_1			(0x44)
#define TSEN_DATA_RAW_REG_1		(0x48)
#define TSEN_AUTO_READ_VALUE_REG_1	(0x4C)

#define TSEN_CFG_REG_2			(0x50)
#define TSEN_DEBUG_REG_2		(0x54)
#define TSEN_INT0_WDOG_THLD_REG_2	(0x5c)
#define TSEN_INT1_INT2_THLD_REG_2	(0x60)
#define TSEN_DATA_REG_2			(0x64)
#define TSEN_DATA_RAW_REG_2		(0x68)
#define TSEN_AUTO_READ_VALUE_REG_2	(0x6c)
/*bit defines of configure reg*/
#define TSEN_ENABLE (1 << 31)
#define TSEN_DIGITAL_RST (1 << 30)
#define TSEN_AUTO_READ (1 << 28)
#define TSEN_DATA_READY (1 << 24)
#define TSEN_AVG_NUM (1 << 25)
#define TSEN_WDOG_DIRECTION (1 << 11)
#define TSEN_WDOG_ENABLE (1 << 10)
#define TSEN_INT2_STATUS (1 << 9)
#define TSEN_INT2_DIRECTION (1 << 8)
#define TSEN_INT2_ENABLE (1 << 7)
#define TSEN_INT1_STATUS (1 << 6)
#define TSEN_INT1_DIRECTION (1 << 5)
#define TSEN_INT1_ENABLE (1 << 4)
#define TSEN_INT0_STATUS (1 << 3)
#define TSEN_INT0_DIRECTION (1 << 2)
#define TSEN_INT0_ENABLE (1 << 1)
#define BALANCE 5000

#define TSEN_THD0_OFF (12)
#define TSEN_THD0_MASK (0xfff000)
#define TSEN_THD1_OFF (0)
#define TSEN_THD1_MASK (0xfff)

#define TSEN_THD2_OFF (12)
#define TSEN_THD2_MASK (0xfff000)
#define TSEN_REG_OFFSET (0x20)
#define TSEN_CFG_REG_OFFSET (0x30)

#define CPU_MAX_NUM	4
#define KHZ_TO_HZ	1000
#define MAX_FREQ_PP	20
#define CMD_BUF_SIZE	120


enum thermal_policy {
	THERMAL_POLICY_POWER_SAVING = 0,
	THERMAL_POLICY_BENCHMARK = 1,
	THERMAL_POLICY_NUM,
};

enum {
	THROTTLE_VLMASTER = 0,
	THROTTLE_VL,
	THROTTLE_CORE,
	THROTTLE_HOTPLUG,
	THROTTLE_DDR,
	THROTTLE_GC3D,
	THROTTLE_GC2D,
	THROTTLE_VPUDEC,
	THROTTLE_VPUENC,
	THROTTLE_NUM,
};

struct freq_cooling_device {
	struct thermal_cooling_device *cool_cpufreq;
	struct thermal_cooling_device *cool_cpuhotplug;
	struct thermal_cooling_device *cool_ddrfreq;
	struct thermal_cooling_device *cool_gc2dfreq;
	struct thermal_cooling_device *cool_gc3dfreq;
	struct thermal_cooling_device *cool_vpuencfreq;
	struct thermal_cooling_device *cool_vpudecfreq;
};

struct tsen_cooling_device {
	int max_state, cur_state;
	struct thermal_cooling_device *cool_tsen;
};

struct state_freq_tbl {
	int freq_num;
	unsigned int freq_tbl[MAX_FREQ_PP];
};

struct pxa1928_thermal_device {
	struct thermal_zone_device *therm_vpu;
	struct thermal_zone_device *therm_cpu;
	struct thermal_zone_device *therm_gc;
	struct thermal_zone_device *therm_max;
	struct tsen_cooling_device vdev;
	struct tsen_cooling_device cdev;
	struct tsen_cooling_device gdev;
	struct tsen_cooling_device maxdev;
	struct freq_cooling_device cool_pxa1928;
	int trip_range[TSEN_NUM];
	struct resource *mem;
	void __iomem *base;
	struct clk *therclk_g, *therclk_vpu;
	struct clk *therclk_cpu, *therclk_gc;
	int hit_trip_cnt[TRIP_POINTS_NUM];
	int irq;
	int pre_temp_cpu, pre_temp_vpu, pre_temp_gc, pre_temp_max;
	int temp_cpu, temp_vpu, temp_gc, temp_max;
	int (*tsen_trips_temp)[TRIP_POINTS_NUM];
	int (*tsen_trips_temp_d)[TRIP_POINTS_NUM];
	unsigned int (*tsen_throttle_tbl)[THROTTLE_NUM][TRIP_RANGE_NUM + 1];
	int profile;
	enum thermal_policy therm_policy;
	struct mutex policy_lock;
	struct mutex data_lock;
	unsigned long (*svc_tbl)[][PXA1928_DVC_LEVEL_NUM + 1];
	struct state_freq_tbl cpufreq_tbl;
	struct state_freq_tbl ddrfreq_tbl;
	struct state_freq_tbl gc3dfreq_tbl;
	struct state_freq_tbl gc2dfreq_tbl;
	struct state_freq_tbl vpudecfreq_tbl;
	struct state_freq_tbl vpuencfreq_tbl;
};

static struct pxa1928_thermal_device thermal_dev;

static int benchmark_tsen_trips_temp[TSEN_NUM][TRIP_POINTS_NUM] = {
	{80000, 81000, 82000, 83000, 84000, 85000, 110000},
	{80000, 81000, 82000, 83000, 84000, 85000, 110000},
	{80000, 81000, 82000, 83000, 84000, 85000, 110000},
	{80000, 81000, 82000, 83000, 84000, 85000, 110000},
};

static int benchmark_tsen_trips_temp_d[TSEN_NUM][TRIP_POINTS_NUM] = {
	{78000, 79000, 80000, 81000, 82000, 83000, 110000},
	{78000, 79000, 80000, 81000, 82000, 83000, 110000},
	{78000, 79000, 80000, 81000, 82000, 83000, 110000},
	{78000, 79000, 80000, 81000, 82000, 83000, 110000},
};

static int powersaving_tsen_trips_temp[TSEN_NUM][TRIP_POINTS_NUM] = {
	{68000, 72000, 76000, 79000, 82000, 85000, 110000},
	{68000, 72000, 76000, 79000, 82000, 85000, 110000},
	{68000, 72000, 76000, 79000, 82000, 85000, 110000},
	{68000, 72000, 76000, 79000, 82000, 85000, 110000},
};

static int powersaving_tsen_trips_temp_d[TSEN_NUM][TRIP_POINTS_NUM] = {
	{65000, 69000, 73000, 76000, 80000, 83000, 110000},
	{65000, 69000, 73000, 76000, 80000, 83000, 110000},
	{65000, 69000, 73000, 76000, 80000, 83000, 110000},
	{65000, 69000, 73000, 76000, 80000, 83000, 110000},
};


static unsigned int pxa1928_tsen_throttle_tbl[][THROTTLE_NUM][TRIP_RANGE_NUM + 1] = {
	[THERMAL_POLICY_POWER_SAVING] = {
		[THROTTLE_VLMASTER]		= { 0b0,
		THROTTLE_CORE, THROTTLE_CORE, THROTTLE_CORE, THROTTLE_CORE,
		THROTTLE_GC3D, THROTTLE_GC3D, THROTTLE_GC3D, THROTTLE_GC3D,
		},
		[THROTTLE_VL]		= { 0b00000000,
		0, 0, 0, 0, 0, 0, 0, 0
		},
		[THROTTLE_CORE]		= { 0b11111111,
		0, 2, 3, 4, 5, 6, 5, 5
		},
		[THROTTLE_HOTPLUG]	= { 0b00000000,
		0, 0, 0, 0, 0, 0, 3, 3
		},
		[THROTTLE_DDR]		= { 0b11111111,
		0, 1, 1, 2, 2, 2, 2, 2
		},
		[THROTTLE_GC3D]		= { 0b11111111,
		0, 1, 1, 3, 3, 3, 3, 3
		},
		[THROTTLE_GC2D]		= { 0b00000001,
		0, 0, 0, 0, 0, 0, 0, 0
		},
		[THROTTLE_VPUDEC]	= { 0b00000001,
		0, 0, 0, 0, 0, 0, 0, 0
		},
		[THROTTLE_VPUENC]	= { 0b00000001,
		0, 0, 0, 0, 0, 0, 0, 0
		},
	},
	[THERMAL_POLICY_BENCHMARK] = {
		[THROTTLE_VLMASTER]		= { 0b0,
		THROTTLE_CORE, THROTTLE_CORE, THROTTLE_CORE, THROTTLE_CORE,
		THROTTLE_GC3D, THROTTLE_GC3D, THROTTLE_GC3D, THROTTLE_GC3D,
		},
		[THROTTLE_VL]		= { 0b00000000,
		0, 0, 0, 0, 0, 0, 0, 0
		},
		[THROTTLE_CORE]		= { 0b11111111,
		0, 2, 3, 4, 5, 6, 5, 5
		},
		[THROTTLE_HOTPLUG]	= { 0b00000000,
		0, 0, 0, 0, 0, 0, 3, 3
		},
		[THROTTLE_DDR]		= { 0b11111111,
		0, 1, 1, 2, 2, 2, 2, 2
		},
		[THROTTLE_GC3D]		= { 0b11111111,
		0, 1, 1, 3, 3, 3, 3, 3
		},
		[THROTTLE_GC2D]		= { 0b00000001,
		0, 0, 0, 0, 0, 0, 0, 0
		},
		[THROTTLE_VPUDEC]	= { 0b00000001,
		0, 0, 0, 0, 0, 0, 0, 0
		},
		[THROTTLE_VPUENC]	= { 0b0000001,
		0, 0, 0, 0, 0, 0, 0, 0
		},
	},
};

#undef THERMAL_DEBUG
#ifdef THERMAL_DEBUG
enum {
	TSEN_INT0 = 0,
	TSEN_INT1,
	TSEN_INT2,
	TSEN_WDOG,
};
static int ts_get_thld_temp(struct thermal_zone_device *tz, int thld,
		int *temp)
{
	void *reg = NULL;
	int thsens_type = get_tsen_type(tz);

	tz->devdata = thermal_dev.base;
	switch (thsens_type) {
	case TSEN_VPU:
		if (TSEN_INT0 == thld || TSEN_WDOG == thld)
			reg = tz->devdata + TSEN_INT0_WDOG_THLD_REG_0;
		else if (TSEN_INT1 == thld || TSEN_INT2 == thld)
			reg = tz->devdata  + TSEN_INT1_INT2_THLD_REG_0;
		break;

	case TSEN_CPU:
		if (TSEN_INT0 == thld || TSEN_WDOG == thld)
			reg = tz->devdata  + TSEN_INT0_WDOG_THLD_REG_1;
		else if (TSEN_INT1 == thld || TSEN_INT2 == thld)
			reg = tz->devdata  + TSEN_INT1_INT2_THLD_REG_1;
		break;

	case TSEN_GPU:
		if (TSEN_INT0 == thld || TSEN_WDOG == thld)
			reg = tz->devdata  + TSEN_INT0_WDOG_THLD_REG_2;
		else if (TSEN_INT1 == thld || TSEN_INT2 == thld)
			reg = tz->devdata  + TSEN_INT1_INT2_THLD_REG_2;
		break;

	default:
		break;
	}

	switch (thld) {
	case TSEN_INT0:
	case TSEN_INT2:
		*temp = celsius_decode(readl(reg) >> 12)
			* 1000;
		break;

	case TSEN_INT1:
	case TSEN_WDOG:
		*temp = celsius_decode(readl(reg) &
				(0x0000FFF)) * 1000;
		break;

	default:
		*temp = -INVALID_TEMP;
		break;
	}
	return 0;
}

void print_tsen_status(void)
{
	void __iomem *reg_base = thermal_dev.base;
	u32 cfg, int0, int1, data, auto_read;
	int temp, thld0, thld1, thld2, thld3;

	cfg = readl(reg_base + TSEN_CFG_REG_0);
	int0 = readl(reg_base + TSEN_INT0_WDOG_THLD_REG_0);
	int1 = readl(reg_base + TSEN_INT1_INT2_THLD_REG_0);
	data = readl(reg_base + TSEN_DATA_REG_0);
	auto_read = readl(reg_base + TSEN_AUTO_READ_VALUE_REG_0);
	temp = celsius_decode(data) * 1000;

	if (thermal_dev.therm_vpu != NULL) {
		ts_get_thld_temp(thermal_dev.therm_vpu, TSEN_INT0, &thld0);
		ts_get_thld_temp(thermal_dev.therm_vpu, TSEN_INT1, &thld1);
		ts_get_thld_temp(thermal_dev.therm_vpu, TSEN_INT2, &thld2);
		ts_get_thld_temp(thermal_dev.therm_vpu, TSEN_WDOG, &thld3);
	}

	pr_info("thermal_vpu:cfg 0x%x,int0 0x%x,int1 0x%x, data 0x%x, auto_read 0x%x, temp %d, int0_thld %d,int1_thld %d,int2_thld %d, wdog_thld %d\n",
		cfg, int0, int1, data, auto_read, temp, thld0, thld1, thld2, thld3);


	cfg = readl(reg_base + TSEN_CFG_REG_1);
	int0 = readl(reg_base + TSEN_INT0_WDOG_THLD_REG_1);
	int1 = readl(reg_base + TSEN_INT1_INT2_THLD_REG_1);
	data = readl(reg_base + TSEN_DATA_REG_1);
	auto_read = readl(reg_base + TSEN_AUTO_READ_VALUE_REG_1);
	temp = celsius_decode(data) * 1000;

	if (thermal_dev.therm_cpu != NULL) {
		ts_get_thld_temp(thermal_dev.therm_cpu, TSEN_INT0, &thld0);
		ts_get_thld_temp(thermal_dev.therm_cpu, TSEN_INT1, &thld1);
		ts_get_thld_temp(thermal_dev.therm_cpu, TSEN_INT2, &thld2);
		ts_get_thld_temp(thermal_dev.therm_cpu, TSEN_WDOG, &thld3);
	}

	pr_info("thermal_cpu:cfg 0x%x,int0 0x%x,int1 0x%x, data 0x%x, auto_read 0x%x, temp %d, int0_thld %d,int1_thld %d,int2_thld %d, wdog_thld %d\n",
		cfg, int0, int1, data, auto_read, temp, thld0, thld1, thld2, thld3);

	cfg = readl(reg_base + TSEN_CFG_REG_2);
	int0 = readl(reg_base + TSEN_INT0_WDOG_THLD_REG_2);
	int1 = readl(reg_base + TSEN_INT1_INT2_THLD_REG_2);
	data = readl(reg_base + TSEN_DATA_REG_2);
	auto_read = readl(reg_base + TSEN_AUTO_READ_VALUE_REG_2);
	temp = celsius_decode(data) * 1000;

	if (thermal_dev.therm_vpu != NULL) {
		ts_get_thld_temp(thermal_dev.therm_gc, TSEN_INT0, &thld0);
		ts_get_thld_temp(thermal_dev.therm_gc, TSEN_INT1, &thld1);
		ts_get_thld_temp(thermal_dev.therm_gc, TSEN_INT2, &thld2);
		ts_get_thld_temp(thermal_dev.therm_gc, TSEN_WDOG, &thld3);
	}

	pr_info("thermal_gc:cfg 0x%x,int0 0x%x,int1 0x%x, data 0x%x, auto_read 0x%x, temp %d, int0_thld %d,int1_thld %d,int2_thld %d, wdog_thld %d\n",
		cfg, int0, int1, data, auto_read, temp, thld0, thld1, thld2, thld3);
}
#endif

static int vl2therml_state(int vl, unsigned long *vl_freq_tbl,
		struct state_freq_tbl *state_freq_tbl)
{
	int freq;
	int i;
	freq = vl_freq_tbl[vl];
	for (i = 0; i < state_freq_tbl->freq_num; i++) {
		if (freq >= (state_freq_tbl->freq_tbl[i]))
			break;
	}
	/* Note: if no sate meet requirment, should turn off, here just
	 * keep it as the minimum freq */
	if (i == state_freq_tbl->freq_num)
		i--;
	return i;
}

static int freq2vl(int freq, unsigned long *vl_freq_tbl)
{
	int i;
	for (i = 0; i < PXA1928_DVC_LEVEL_NUM; i++) {
		if (freq <= vl_freq_tbl[i])
			break;
	}
		return i;
}

static int therml_state2vl(int state, unsigned long *vl_freq_tbl,
		struct state_freq_tbl *state_freq_tbl)
{
	int freq, vl;
	freq = state_freq_tbl->freq_tbl[state];
	vl = freq2vl(freq, vl_freq_tbl);
	return vl;
}

int  init_policy_state2freq_tbl(void)
{
	int i, max_level = 0, freq, index = 0, descend = -1;
	struct cpufreq_frequency_table *cpufreq_table;
	struct devfreq_frequency_table *devfreq_table;
	unsigned long dev_freqs[THERMAL_MAX_TRIPS] = {0};

	/*  cpu freq table init  */
	freq = CPUFREQ_ENTRY_INVALID;
	max_level = 0;
	descend = -1;
	cpufreq_table = cpufreq_frequency_get_table(0);
	/* get frequency number and order*/
	for (i = 0; cpufreq_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		if (cpufreq_table[i].frequency == CPUFREQ_ENTRY_INVALID)
			continue;

		if (freq != CPUFREQ_ENTRY_INVALID && descend == -1)
			descend = !!(freq > cpufreq_table[i].frequency);

		freq = cpufreq_table[i].frequency;
		max_level++;
	}
	thermal_dev.cpufreq_tbl.freq_num = max_level;
	for (i = 0; cpufreq_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		if (cpufreq_table[i].frequency == CPUFREQ_ENTRY_INVALID)
			continue;

		index = descend ? i : (max_level - i - 1);
		thermal_dev.cpufreq_tbl.freq_tbl[index] =
			(cpufreq_table[i].frequency * KHZ_TO_HZ);
	}

	/*  ddr freq table init  */
	freq = 0;
	max_level = 0;
	descend = -1;
	devfreq_table = devfreq_frequency_get_table(DEVFREQ_DDR);
	/* get frequency number */
	for (i = 0; devfreq_table[i].frequency != DEVFREQ_TABLE_END; i++) {
		if (freq != 0 && descend == -1)
			descend = !!(freq > devfreq_table[i].frequency);
		freq = devfreq_table[i].frequency;
		max_level++;
	}

	thermal_dev.ddrfreq_tbl.freq_num = max_level;
	for (i = 0; devfreq_table[i].frequency != DEVFREQ_TABLE_END; i++) {
		index = descend ? i : (max_level - i - 1);
		thermal_dev.ddrfreq_tbl.freq_tbl[index] =
			(devfreq_table[i].frequency * KHZ_TO_HZ);
	}

	/*  gc3d freq table init  */
	freq = 0;
	max_level = 0;
	descend = -1;
	get_gc3d_freqs_table(dev_freqs, &max_level, THERMAL_MAX_TRIPS);
	for (i = 0; i < max_level; i++) {
		if (freq != 0 && descend == -1)
			descend = !!(freq > dev_freqs[i]);
		freq = dev_freqs[i];
	}
	thermal_dev.gc3dfreq_tbl.freq_num = max_level;
	for (i = 0;  i < max_level; i++) {
		index = descend ? i : (max_level - i - 1);
		thermal_dev.gc3dfreq_tbl.freq_tbl[index] = dev_freqs[i];
	}

	/*  gc2d freq table init  */
	freq = 0;
	max_level = 0;
	descend = -1;
	get_gc2d_freqs_table(dev_freqs, &max_level, THERMAL_MAX_TRIPS);
	for (i = 0; i < max_level; i++) {
		if (freq != 0 && descend == -1)
			descend = !!(freq > dev_freqs[i]);
		freq = dev_freqs[i];
	}
	thermal_dev.gc2dfreq_tbl.freq_num = max_level;
	for (i = 0;  i < max_level; i++) {
		index = descend ? i : (max_level - i - 1);
		thermal_dev.gc2dfreq_tbl.freq_tbl[index] = dev_freqs[i];
	}

	/*  vpudec freq table init  */
	freq = 0;
	max_level = 0;
	descend = -1;
	devfreq_table = devfreq_frequency_get_table(DEVFREQ_VPU_0);
	/* get frequency number */
	for (i = 0; devfreq_table[i].frequency != DEVFREQ_TABLE_END; i++) {
		if (freq != 0 && descend == -1)
			descend = !!(freq > devfreq_table[i].frequency);
		freq = devfreq_table[i].frequency;
		max_level++;
	}

	thermal_dev.vpudecfreq_tbl.freq_num = max_level;
	for (i = 0; devfreq_table[i].frequency != DEVFREQ_TABLE_END; i++) {
		index = descend ? i : (max_level - i - 1);
		thermal_dev.vpudecfreq_tbl.freq_tbl[index] =
			(devfreq_table[i].frequency * KHZ_TO_HZ);
	}

	/*  vpuenc freq table init  */
	freq = 0;
	max_level = 0;
	descend = -1;
	devfreq_table = devfreq_frequency_get_table(DEVFREQ_VPU_1);
	/* get frequency number */
	for (i = 0; devfreq_table[i].frequency != DEVFREQ_TABLE_END; i++) {
		if (freq != 0 && descend == -1)
			descend = !!(freq > devfreq_table[i].frequency);
		freq = devfreq_table[i].frequency;
		max_level++;
	}
	thermal_dev.vpuencfreq_tbl.freq_num = max_level;

	for (i = 0; devfreq_table[i].frequency != DEVFREQ_TABLE_END; i++) {
		index = descend ? i : (max_level - i - 1);
		thermal_dev.vpuencfreq_tbl.freq_tbl[index] =
			(devfreq_table[i].frequency * KHZ_TO_HZ);
	}
	return 0;
}

/* update the master vl table*/
int update_policy_vl_tbl(void)
{
	int vl, i, state_master;
	unsigned long *vl_freq_tbl;
	unsigned int (*throttle_tbl)[THROTTLE_NUM][TRIP_RANGE_NUM + 1];
	throttle_tbl = thermal_dev.tsen_throttle_tbl;
	for (i = TRIP_RANGE_0; i <= TRIP_RANGE_MAX; i++) {
		switch (throttle_tbl[thermal_dev.therm_policy][THROTTLE_VLMASTER][i + 1]) {
		case THROTTLE_CORE:
			state_master = throttle_tbl[thermal_dev.therm_policy][THROTTLE_CORE][i + 1];
			vl_freq_tbl = &((*thermal_dev.svc_tbl)[CORE][1]);
			vl = therml_state2vl(state_master, vl_freq_tbl, &thermal_dev.cpufreq_tbl);
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_VL][i + 1] = vl;
			break;

		case THROTTLE_DDR:
			state_master = throttle_tbl[thermal_dev.therm_policy][THROTTLE_DDR][i + 1];
			vl_freq_tbl = &((*thermal_dev.svc_tbl)[DDR][1]);
			vl = therml_state2vl(state_master, vl_freq_tbl, &thermal_dev.ddrfreq_tbl);
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_VL][i + 1] = vl;
			break;

		case THROTTLE_GC3D:
			state_master = throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC3D][i + 1];
			vl_freq_tbl = &((*thermal_dev.svc_tbl)[GC3D][1]);
			vl = therml_state2vl(state_master, vl_freq_tbl, &thermal_dev.gc3dfreq_tbl);
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_VL][i + 1] = vl;
			break;

		case THROTTLE_GC2D:
			state_master = throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC2D][i + 1];
			vl_freq_tbl = &((*thermal_dev.svc_tbl)[GC2D][1]);
			vl = therml_state2vl(state_master, vl_freq_tbl, &thermal_dev.gc2dfreq_tbl);
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_VL][i + 1] = vl;
			break;

		case THROTTLE_VPUDEC:
			state_master = throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUDEC][i + 1];
			vl_freq_tbl = &((*thermal_dev.svc_tbl)[VPUDE][1]);
			vl = therml_state2vl(state_master, vl_freq_tbl, &thermal_dev.vpudecfreq_tbl);
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_VL][i + 1] = vl;
			break;

		case THROTTLE_VPUENC:
			state_master = throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUENC][i + 1];
			vl_freq_tbl = &((*thermal_dev.svc_tbl)[VPUEN][1]);
			vl = therml_state2vl(state_master, vl_freq_tbl, &thermal_dev.vpuencfreq_tbl);
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_VL][i + 1] = vl;
			break;

		default:
			break;
		}
	}
	return 0;
}

int update_policy_throttle_tbl(void)
{
	int i, vl;
	int state_cpu, state_ddr, state_gc3d;
	int state_gc2d, state_vpudec, state_vpuenc;
	unsigned long *vl_freq_tbl;
	unsigned int (*throttle_tbl)[THROTTLE_NUM][TRIP_RANGE_NUM + 1];

	throttle_tbl = thermal_dev.tsen_throttle_tbl;
	for (i = TRIP_RANGE_0; i <= TRIP_RANGE_MAX; i++) {
		vl = throttle_tbl[thermal_dev.therm_policy][THROTTLE_VL][i + 1];

		vl_freq_tbl = &((*thermal_dev.svc_tbl)[CORE][1]);
		state_cpu = vl2therml_state(vl, vl_freq_tbl, &thermal_dev.cpufreq_tbl);
		if (!test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_CORE][0]))
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_CORE][i + 1]
				= state_cpu;
		if (throttle_tbl[thermal_dev.therm_policy][THROTTLE_CORE][i + 1]
				>= thermal_dev.cpufreq_tbl.freq_num)
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_CORE][i + 1]
				= thermal_dev.cpufreq_tbl.freq_num - 1;

		vl_freq_tbl = &((*thermal_dev.svc_tbl)[DDR][1]);
		state_ddr = vl2therml_state(vl, vl_freq_tbl, &thermal_dev.ddrfreq_tbl);
		if (!test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_DDR][0]))
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_DDR][i + 1]
				= state_ddr;
		if (throttle_tbl[thermal_dev.therm_policy][THROTTLE_DDR][i + 1]
				>= thermal_dev.ddrfreq_tbl.freq_num)
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_DDR][i + 1]
				= thermal_dev.ddrfreq_tbl.freq_num - 1;

		vl_freq_tbl = &((*thermal_dev.svc_tbl)[GC3D][1]);
		state_gc3d = vl2therml_state(vl, vl_freq_tbl, &thermal_dev.gc3dfreq_tbl);
		if (!test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC3D][0]))
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC3D][i + 1]
				= state_gc3d;
		if (throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC3D][i + 1]
				>= thermal_dev.gc3dfreq_tbl.freq_num)
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC3D][i + 1]
				= thermal_dev.gc3dfreq_tbl.freq_num - 1;

		vl_freq_tbl = &((*thermal_dev.svc_tbl)[GC2D][1]);
		state_gc2d = vl2therml_state(vl, vl_freq_tbl, &thermal_dev.gc2dfreq_tbl);
		if (!test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC2D][0]))
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC2D][i + 1]
				= state_gc2d;
		if (throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC2D][i + 1]
				>= thermal_dev.gc2dfreq_tbl.freq_num)
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC2D][i + 1]
				= thermal_dev.gc2dfreq_tbl.freq_num - 1;

		vl_freq_tbl = &((*thermal_dev.svc_tbl)[VPUDE][1]);
		state_vpudec = vl2therml_state(vl, vl_freq_tbl, &thermal_dev.vpudecfreq_tbl);
		if (!test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUDEC][0]))
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUDEC][i + 1]
				= state_vpudec;
		if (throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUDEC][i + 1]
				>= thermal_dev.vpudecfreq_tbl.freq_num)
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUDEC][i + 1]
				= thermal_dev.vpudecfreq_tbl.freq_num - 1;

		vl_freq_tbl = &((*thermal_dev.svc_tbl)[VPUEN][1]);
		state_vpuenc = vl2therml_state(vl, vl_freq_tbl, &thermal_dev.vpuencfreq_tbl);
		if (!test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUENC][0]))
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUENC][i + 1]
				= state_vpuenc;
		if (throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUENC][i + 1]
				>= thermal_dev.vpuencfreq_tbl.freq_num)
			throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUENC][i + 1]
				= thermal_dev.vpuencfreq_tbl.freq_num - 1;
	}

	return 0;

}
int tsen_policy_dump(char *buf, int size)
{
	int i, s = 0, vl, vl_master;
	int state_cpu, state_ddr, state_gc3d;
	int state_gc2d, state_vpudec, state_vpuenc;
	int vl_cpu, vl_ddr, vl_gc3d;
	int vl_gc2d, vl_vpudec, vl_vpuenc;
	char buf_name[20];
	unsigned int (*throttle_tbl)[THROTTLE_NUM][TRIP_RANGE_NUM + 1];
	unsigned long *vl_freq_tbl;

	throttle_tbl = thermal_dev.tsen_throttle_tbl;
	for (i = TRIP_RANGE_0; i <= TRIP_RANGE_MAX; i++) {
		vl_master = throttle_tbl[thermal_dev.therm_policy][THROTTLE_VLMASTER][i + 1];
		vl = throttle_tbl[thermal_dev.therm_policy][THROTTLE_VL][i + 1];
		state_cpu = throttle_tbl[thermal_dev.therm_policy][THROTTLE_CORE][i + 1];
		state_ddr = throttle_tbl[thermal_dev.therm_policy][THROTTLE_DDR][i + 1];
		state_gc3d = throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC3D][i + 1];
		state_gc2d = throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC2D][i + 1];
		state_vpudec = throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUDEC][i + 1];
		state_vpuenc = throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUENC][i + 1];

		if (NULL == buf) {
			if (i == TRIP_RANGE_0) {
				pr_info("*************THERMAL RANG & POLICY*********************\n");
				pr_info("* Thermal Stage:%d(up:Temp <%dC; down:Temp<%dC)\n",
					i, thermal_dev.tsen_trips_temp[TSEN_MAX][i],
					thermal_dev.tsen_trips_temp_d[TSEN_MAX][i]);
			} else if (i == TRIP_RANGE_MAX) {
				pr_info("* Thermal Stage:%d(up:Temp>%dC; down:Temp>%dC)\n",
					i, thermal_dev.tsen_trips_temp[TSEN_MAX][i - 1],
					 thermal_dev.tsen_trips_temp_d[TSEN_MAX][i - 1]);
			} else {
				pr_info("* Thermal Stage:%d(up:%d<Temp<%dC; down:%d<Temp<%dC)\n",
					i, thermal_dev.tsen_trips_temp[TSEN_MAX][i - 1],
					thermal_dev.tsen_trips_temp[TSEN_MAX][i],
					thermal_dev.tsen_trips_temp_d[TSEN_MAX][i - 1],
					thermal_dev.tsen_trips_temp_d[TSEN_MAX][i]);
			}

			if (vl_master == THROTTLE_VL)
				strcpy(buf_name, "(vl_master)");
			else
				strcpy(buf_name, "(vl_show)");
			pr_info("        %svl:%d;\n", buf_name, vl);

			/* core state dump */
			if (vl_master == THROTTLE_CORE)
				strcpy(buf_name, "(vl_master)");
			else if (test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_CORE][0])) {
				vl_freq_tbl = &((*thermal_dev.svc_tbl)[CORE][1]);
				vl_cpu = therml_state2vl(state_cpu, vl_freq_tbl, &thermal_dev.cpufreq_tbl);
				if (vl_cpu > vl)
					strcpy(buf_name, "(vl_private*)");
				else
					strcpy(buf_name, "(vl_private)");
			} else
				strcpy(buf_name, "(vl_slave)");
			pr_info("        %scpu:state_%d(freq:%dhz)\n", buf_name,
				state_cpu, thermal_dev.cpufreq_tbl.freq_tbl[state_cpu]);

			/* core number dump */
			pr_info("        cpu_num:%d\n", CPU_MAX_NUM -
				throttle_tbl[thermal_dev.therm_policy][THROTTLE_HOTPLUG][i + 1]);

			/* ddr state dump */
			if (vl_master == THROTTLE_DDR)
				strcpy(buf_name, "(vl_master)");
			else if (test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_DDR][0])) {
				vl_freq_tbl = &((*thermal_dev.svc_tbl)[DDR][1]);
				vl_ddr = therml_state2vl(state_ddr, vl_freq_tbl, &thermal_dev.ddrfreq_tbl);
				if (vl_ddr > vl)
					strcpy(buf_name, "(vl_private*)");
				else
					strcpy(buf_name, "(vl_private)");
			} else
				strcpy(buf_name, "(vl_slave)");
			pr_info("        %sddr:state_%d(freq:%dhz)\n", buf_name,
				state_ddr,  thermal_dev.ddrfreq_tbl.freq_tbl[state_ddr]);

			/* gc3d state dump */
			if (vl_master == THROTTLE_GC3D)
				strcpy(buf_name, "(vl_master)");
			else if (test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC3D][0])) {
				vl_freq_tbl = &((*thermal_dev.svc_tbl)[GC3D][1]);
				vl_gc3d = therml_state2vl(state_gc3d, vl_freq_tbl, &thermal_dev.ddrfreq_tbl);
				if (vl_gc3d > vl)
					strcpy(buf_name, "(vl_private*)");
				else
					strcpy(buf_name, "(vl_private)");
			} else
				strcpy(buf_name, "(vl_slave*)");
			pr_info("        %sgc3d:state_%d(freq:%dhz)\n", buf_name,
				state_gc3d,  thermal_dev.gc3dfreq_tbl.freq_tbl[state_gc3d]);

			/* gc2d state dump */
			if (vl_master == THROTTLE_GC2D)
				strcpy(buf_name, "(vl_master)");
			else if (test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC2D][0])) {
				vl_freq_tbl = &((*thermal_dev.svc_tbl)[GC2D][1]);
				vl_gc2d = therml_state2vl(state_gc2d, vl_freq_tbl, &thermal_dev.ddrfreq_tbl);
				if (vl_gc2d > vl)
					strcpy(buf_name, "(vl_private*)");
				else
					strcpy(buf_name, "(vl_private)");
			} else
					strcpy(buf_name, "(vl_slave)");
			pr_info("        %sgc2d:state_%d(frq:%dhz)\n", buf_name,
				state_gc2d,  thermal_dev.gc2dfreq_tbl.freq_tbl[state_gc2d]);

			/* vpudec state dump */
			if (vl_master == THROTTLE_VPUDEC)
				strcpy(buf_name, "(vl_master)");
			else if (test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUDEC][0])) {
				vl_freq_tbl = &((*thermal_dev.svc_tbl)[VPUDE][1]);
				vl_vpudec = therml_state2vl(state_vpudec, vl_freq_tbl, &thermal_dev.ddrfreq_tbl);
				if (vl_vpudec > vl)
					strcpy(buf_name, "(vl_private*)");
				else
					strcpy(buf_name, "(vl_private)");
			} else
				strcpy(buf_name, "(vl_slave)");
			pr_info("        %svpudec:state_%d(freq:%dhz)\n", buf_name,
				state_vpudec,  thermal_dev.vpudecfreq_tbl.freq_tbl[state_vpudec]);

			/* vpuenc state dump */
			if (vl_master == THROTTLE_VPUENC)
				strcpy(buf_name, "(vl_master)");
			else if (test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUENC][0])) {
				vl_freq_tbl = &((*thermal_dev.svc_tbl)[VPUEN][1]);
				vl_vpuenc = therml_state2vl(state_vpuenc, vl_freq_tbl, &thermal_dev.ddrfreq_tbl);
				if (vl_vpuenc > vl)
					strcpy(buf_name, "(vl_private*)");
				else
					strcpy(buf_name, "(vl_private)");
			} else
				strcpy(buf_name, "(vl_slave)");

			pr_info("        %svpuenc:state_%d(freq:%dhz)\n", buf_name,
				state_vpuenc, thermal_dev.vpuencfreq_tbl.freq_tbl[state_vpuenc]);
		} else {
			if (i == TRIP_RANGE_0) {
				s += snprintf(buf + s, size - s,
					"*************THERMAL RANG & POLICY*********************\n");
				s += snprintf(buf + s, size - s,
					"* Thermal Stage:%d(up:Temp <%dC; down:Temp<%dC)\n",
					i, thermal_dev.tsen_trips_temp[TSEN_MAX][i],
					thermal_dev.tsen_trips_temp_d[TSEN_MAX][i]);
			} else if (i == TRIP_RANGE_MAX) {
				s += snprintf(buf + s, size - s,
					"* Thermal Stage:%d(up:Temp>%dC; down:Temp>%dC)\n",
					i, thermal_dev.tsen_trips_temp[TSEN_MAX][i - 1],
					 thermal_dev.tsen_trips_temp_d[TSEN_MAX][i - 1]);
			} else {
				s += snprintf(buf + s, size - s,
					"* Thermal Stage:%d(up:%d<Temp<%dC; down:%d<Temp<%dC)\n",
					i, thermal_dev.tsen_trips_temp[TSEN_MAX][i - 1],
					thermal_dev.tsen_trips_temp[TSEN_MAX][i],
					thermal_dev.tsen_trips_temp_d[TSEN_MAX][i - 1],
					thermal_dev.tsen_trips_temp_d[TSEN_MAX][i]);
			}

			if (vl_master == THROTTLE_VL)
				strcpy(buf_name, "(vl_master)");
			else
				strcpy(buf_name, "(vl_show)");

			s += snprintf(buf + s, size - s, "        %svl:%d;\n", buf_name, vl);
			if (vl_master == THROTTLE_CORE)
				strcpy(buf_name, "(vl_master)");
			else if (test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_CORE][0])) {
				vl_freq_tbl = &((*thermal_dev.svc_tbl)[CORE][1]);
				vl_cpu = therml_state2vl(state_cpu, vl_freq_tbl, &thermal_dev.cpufreq_tbl);
				if (vl_cpu > vl)
					strcpy(buf_name, "(vl_private*)");
				else
					strcpy(buf_name, "(vl_private)");
			} else
				strcpy(buf_name, "(vl_slave)");

			s += snprintf(buf + s, size - s,
				"        %scpu:state_%d(freq:%dhz)\n",
				buf_name, state_cpu, thermal_dev.cpufreq_tbl.freq_tbl[state_cpu]);

			s += snprintf(buf + s, size - s, "        cpu_num:%d\n",
				CPU_MAX_NUM -  throttle_tbl[thermal_dev.therm_policy][THROTTLE_HOTPLUG][i + 1]);
			if (vl_master == THROTTLE_DDR)
				strcpy(buf_name, "(vl_master)");
			else if (test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_DDR][0])) {
				vl_freq_tbl = &((*thermal_dev.svc_tbl)[DDR][1]);
				vl_ddr = therml_state2vl(state_ddr, vl_freq_tbl, &thermal_dev.ddrfreq_tbl);
				if (vl_ddr > vl)
					strcpy(buf_name, "(vl_private*)");
				else
					strcpy(buf_name, "(vl_private)");

			} else
				strcpy(buf_name, "(vl_slave)");

			s += snprintf(buf + s, size - s, "        %sddr:state_%d(freq:%dhz)\n",
				buf_name, state_ddr,  thermal_dev.ddrfreq_tbl.freq_tbl[state_ddr]);
			if (vl_master == THROTTLE_GC3D)
				strcpy(buf_name, "(vl_master)");
			else if (test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC3D][0])) {
				vl_freq_tbl = &((*thermal_dev.svc_tbl)[GC3D][1]);
				vl_gc3d = therml_state2vl(state_gc3d, vl_freq_tbl, &thermal_dev.ddrfreq_tbl);
				if (vl_gc3d > vl)
					strcpy(buf_name, "(vl_private*)");
				else
					strcpy(buf_name, "(vl_private)");
			} else
				strcpy(buf_name, "(vl_slave)");

			s += snprintf(buf + s, size - s, "        %sgc3d:state_%d(freq:%dhz)\n",
				buf_name, state_gc3d,  thermal_dev.gc3dfreq_tbl.freq_tbl[state_gc3d]);
			if (vl_master == THROTTLE_GC2D)
				strcpy(buf_name, "(vl_master)");
			else if (test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC2D][0])) {
				vl_freq_tbl = &((*thermal_dev.svc_tbl)[GC2D][1]);
				vl_gc2d = therml_state2vl(state_gc2d, vl_freq_tbl, &thermal_dev.ddrfreq_tbl);
				if (vl_gc2d > vl)
					strcpy(buf_name, "(vl_private*)");
				else
					strcpy(buf_name, "(vl_private)");
			} else
				strcpy(buf_name, "(vl_slave)");

			s += snprintf(buf + s, size - s, "        %sgc2d:state_%d(frq:%dhz)\n",
				buf_name, state_gc2d,  thermal_dev.gc2dfreq_tbl.freq_tbl[state_gc2d]);
			if (vl_master == THROTTLE_VPUDEC)
				strcpy(buf_name, "(vl_master)");
			else if (test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUDEC][0])) {
				vl_freq_tbl = &((*thermal_dev.svc_tbl)[VPUDE][1]);
				vl_vpudec = therml_state2vl(state_vpudec, vl_freq_tbl, &thermal_dev.ddrfreq_tbl);
				if (vl_vpudec > vl)
					strcpy(buf_name, "(vl_private*)");
				else
					strcpy(buf_name, "(vl_private)");
			} else
				strcpy(buf_name, "(vl_slave)");

			s += snprintf(buf + s, size - s, "        %svpudec:state_%d(freq:%dhz)\n",
				buf_name, state_vpudec,  thermal_dev.vpudecfreq_tbl.freq_tbl[state_vpudec]);
			if (vl_master == THROTTLE_VPUENC)
				strcpy(buf_name, "(vl_master)");
			else if (test_bit(i, (void *)&throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUENC][0])) {
				vl_freq_tbl = &((*thermal_dev.svc_tbl)[VPUEN][1]);
				vl_vpuenc = therml_state2vl(state_vpuenc, vl_freq_tbl, &thermal_dev.ddrfreq_tbl);
				if (vl_vpuenc > vl)
					strcpy(buf_name, "(vl_private*)");
				else
					strcpy(buf_name, "(vl_private)");
			} else
				strcpy(buf_name, "(vl_slave)");

			s += snprintf(buf + s, size - s, "        %svpuenc:state_%d(freq:%dhz)\n",
				buf_name, state_vpuenc, thermal_dev.vpuencfreq_tbl.freq_tbl[state_vpuenc]);

		}

	}
	return s;

}

int  tsen_update_policy(void)
{
	init_policy_state2freq_tbl();
	update_policy_vl_tbl();
	update_policy_throttle_tbl();
	return 0;
}



#define reg_read(off) readl(thermal_dev.base + (off))
#define reg_write(val, off) writel((val), thermal_dev.base + (off))
#define reg_clr_set(off, clr, set) \
	reg_write(((reg_read(off) & ~(clr)) | (set)), off)
static int pxa1928_set_threshold(int id, int range);

/*
 * 1: don't have read data feature.
 * 2: have the read data feature, but don't have the valid data check
 * 3: have the read data feature and valid data check
*/
static int celsius_decode(u32 tcode)
{
	int cels;
	cels = (int)(tcode * TSEN_GAIN - TSEN_OFFSET * 1000) / 10000;
	if (cels < 0)
		cels--;
	else
		cels++;

	return cels;
}

static int celsius_encode(int cels)
{
	u32 tcode;
	cels /= 1000;
	tcode = (cels * 10 + TSEN_OFFSET) * 1000 / (TSEN_GAIN);
	return tcode;
}

static void disable_tsen(void *reg)
{
	 writel(readl(reg) & ~TSEN_ENABLE, reg);
}

static void enable_tsen(void *reg)
{
	writel(readl(reg) | TSEN_ENABLE, reg);
	writel(readl(reg) & ~TSEN_DIGITAL_RST, reg);
}

static void init_wd_int0_threshold(int id, void *reg)
{
	writel((readl(reg) & 0x000FFF) |
			(celsius_encode(thermal_dev.tsen_trips_temp[id][TRIP_POINT_0]) << 12), reg);
	writel((readl(reg) & 0xFFF000) |
			celsius_encode(PXA1928_WD_TEMP), reg);
}

static void init_int1_int2_threshold(int id, void *reg)
{
	writel((readl(reg) & 0xFFF000) |
			celsius_encode(thermal_dev.tsen_trips_temp[id][TRIP_POINT_1]), reg);
	writel((readl(reg) & 0x000FFF) |
			(celsius_encode(thermal_dev.tsen_trips_temp[id][TRIP_POINT_2]) << 12), reg);
}

static void init_tsen_irq_dir(void *reg)
{
	writel(readl(reg) | TSEN_WDOG_DIRECTION |
		TSEN_WDOG_ENABLE | TSEN_INT2_DIRECTION | TSEN_INT2_ENABLE |
		TSEN_INT0_DIRECTION | TSEN_INT0_ENABLE, reg);
}

static void enable_auto_read(void *reg)
{
	writel(readl(reg) | TSEN_AUTO_READ, reg);
}

static void set_auto_read_interval(void *reg)
{
	writel(readl(reg) | PXA1928_THERMAL_POLLING_FREQUENCY_MS , reg);
}

u32 data_reg_offset(int id)
{
	u32 offset = id * TSEN_REG_OFFSET;
	return offset;
}

u32 cfg_reg_offset(int id)
{
	u32 offset = 0;
	if (id == 1)
		offset = TSEN_CFG_REG_OFFSET;
	else if (id == 2)
		offset = TSEN_CFG_REG_OFFSET + TSEN_REG_OFFSET;
	return offset;
}

static int tsen_temp_read(int *pre_temp, void *config_reg, void *data_reg)
{
	int temp = 0;
	temp = celsius_decode(readl(data_reg)) * 1000;
	if (temp >= 125000 || temp < -40000) {
		pr_info("Temperature is %d beyond the scope of -40C to 125C.\n", temp);
		temp = *pre_temp;
	} else
		*pre_temp = temp;

	return temp;
}

static int get_tsen_type(struct thermal_zone_device *tz)
{
	int tsen_type = -1;

	if (!strnicmp(tz->type, "tsen_vpu", strlen(tz->type)))
		tsen_type = TSEN_VPU;
	else if (!strnicmp(tz->type, "tsen_cpu", strlen(tz->type)))
		tsen_type = TSEN_CPU;
	else if (!strnicmp(tz->type, "tsen_gc", strlen(tz->type)))
		tsen_type = TSEN_GPU;
	else if (!strnicmp(tz->type, "tsen_max", strlen(tz->type)))
		tsen_type = TSEN_MAX;
	else
		pr_err("Invalid thermal zone type: %s\n", tz->type);

	return tsen_type;
}


static int
tsen_sys_get_temp(struct thermal_zone_device *tz, int *temp)
{
	int tsen_type = get_tsen_type(tz);
	int temp_gc = -INVALID_TEMP;
	int temp_vpu = -INVALID_TEMP;
	int temp_cpu = -INVALID_TEMP;

	*temp = 0;
	tz->devdata = thermal_dev.base;
	switch (tsen_type) {
	case TSEN_VPU:
		if (tz->devdata)
			*temp = tsen_temp_read(&thermal_dev.pre_temp_vpu,
					tz->devdata + TSEN_CFG_REG_0,
					tz->devdata + TSEN_DATA_REG_0);
		thermal_dev.temp_vpu = *temp;
		break;

	case TSEN_CPU:
		if (tz->devdata)
			*temp = tsen_temp_read(&thermal_dev.pre_temp_cpu,
				tz->devdata + TSEN_CFG_REG_1,
				tz->devdata + TSEN_DATA_REG_1);
		thermal_dev.temp_cpu = *temp;
		break;

	case TSEN_GPU:
		if (tz->devdata)
			*temp = tsen_temp_read(&thermal_dev.pre_temp_gc,
				tz->devdata + TSEN_CFG_REG_2,
				tz->devdata + TSEN_DATA_REG_2);
		thermal_dev.temp_gc = *temp;
		break;

	case TSEN_MAX:
		if (tz->devdata) {
			temp_vpu = tsen_temp_read(&thermal_dev.pre_temp_vpu,
					tz->devdata + TSEN_CFG_REG_0,
					tz->devdata + TSEN_DATA_REG_0);
			temp_cpu = tsen_temp_read(&thermal_dev.pre_temp_cpu,
				tz->devdata + TSEN_CFG_REG_1,
				tz->devdata + TSEN_DATA_REG_1);
			temp_gc = tsen_temp_read(&thermal_dev.pre_temp_gc,
				tz->devdata + TSEN_CFG_REG_2,
				tz->devdata + TSEN_DATA_REG_2);
		}
		*temp = (temp_vpu > temp_cpu) ? temp_vpu : temp_cpu;
		if (*temp < temp_gc)
			*temp = temp_gc;
		thermal_dev.temp_max = *temp;
		break;

	default:
		*temp = -INVALID_TEMP;
		return -EINVAL;
	}

	return 0;
}

static int tsen_sys_get_mode(struct thermal_zone_device *thermal,
		enum thermal_device_mode *mode)
{
	*mode = THERMAL_DEVICE_ENABLED;
	return 0;

}

static int tsen_sys_get_trip_type(struct thermal_zone_device *thermal, int trip,
		enum thermal_trip_type *type)
{
	if ((trip >= TRIP_POINT_0) && (trip < TRIP_POINT_MAX))
		*type = THERMAL_TRIP_ACTIVE;
	else if (TRIP_POINT_MAX == trip)
		*type = THERMAL_TRIP_CRITICAL;
	else
		*type = (enum thermal_trip_type)(-1);
	return 0;
}

static int tsen_sys_get_trip_temp(struct thermal_zone_device *thermal, int trip,
		int *temp)
{
	int tsen_type = get_tsen_type(thermal);

	*temp = thermal_dev.tsen_trips_temp[tsen_type][trip];
	return 0;
}

static int tsen_sys_get_trip_temp_d(struct thermal_zone_device *thermal,
		int trip, int *temp)
{
	int tsen_type = get_tsen_type(thermal);

	if ((trip >= 0) && (trip < TRIP_POINTS_NUM))
		*temp = thermal_dev.tsen_trips_temp_d[tsen_type][trip];
	else
		*temp = -1;
	return 0;
}

static int tsen_set_trip_temp_d(struct thermal_zone_device *tz,
		int trip, unsigned long temp)
{
	int tsen_type = get_tsen_type(tz);

	if ((trip >= 0) && (trip < TRIP_POINT_MAX))
		thermal_dev.tsen_trips_temp_d[tsen_type][trip] = temp;
	if ((TSEN_VPU == tsen_type) || (TSEN_CPU == tsen_type) || (TSEN_GPU == tsen_type))
		pxa1928_set_threshold(tsen_type, thermal_dev.trip_range[tsen_type]);

	return 0;
}

static int tsen_sys_set_trip_temp_d(struct thermal_zone_device *tz,
		int trip, unsigned long temp)
{
	int tsen_type = get_tsen_type(tz);

	if (THERMAL_POLICY_POWER_SAVING == thermal_dev.therm_policy ||
			THERMAL_POLICY_BENCHMARK == thermal_dev.therm_policy) {
		if (TSEN_MAX == tsen_type) {
			if (NULL != thermal_dev.therm_max)
				tsen_set_trip_temp_d(thermal_dev.therm_max, trip, temp);
			if (NULL != thermal_dev.therm_vpu)
				tsen_set_trip_temp_d(thermal_dev.therm_vpu, trip, temp);
			if (NULL != thermal_dev.therm_cpu)
				tsen_set_trip_temp_d(thermal_dev.therm_cpu, trip, temp);
			if (NULL != thermal_dev.therm_gc)
				tsen_set_trip_temp_d(thermal_dev.therm_gc, trip, temp);
		} else {
			pr_info("use max temp solution, couldn't change this one\n");
		}
	} else {
		tsen_set_trip_temp_d(tz, trip, temp);
	}
	return 0;
}

static int pxa1928_set_threshold(int id, int range)
{
	u32 tmp;
	u32 off = data_reg_offset(id);
	u32 cfg_off = cfg_reg_offset(id);
	if (range < 0 || range > TRIP_RANGE_MAX) {
		pr_err("soc thermal: invalid threshold %d\n", range);
		return -1;
	}

	if (0 == range) {
		tmp = (celsius_encode(thermal_dev.tsen_trips_temp[id][0])
				<< TSEN_THD0_OFF) & TSEN_THD0_MASK;
		reg_clr_set(off + TSEN_INT0_WDOG_THLD_REG_0,
				TSEN_THD0_MASK, tmp);

		tmp = (celsius_encode(thermal_dev.tsen_trips_temp_d[id][0])
				<< TSEN_THD1_OFF) & TSEN_THD1_MASK;
		reg_clr_set(off + TSEN_INT1_INT2_THLD_REG_0,
				TSEN_THD1_MASK, tmp);

		reg_clr_set(cfg_off + TSEN_CFG_REG_0, 0, TSEN_INT0_ENABLE);
		reg_clr_set(cfg_off + TSEN_CFG_REG_0, TSEN_INT1_ENABLE, 0);

	} else if (TRIP_RANGE_MAX == range) {
		tmp = (celsius_encode(thermal_dev.tsen_trips_temp[id][range - 1]) <<
						TSEN_THD0_OFF) & TSEN_THD0_MASK;
		reg_clr_set(off + TSEN_INT0_WDOG_THLD_REG_0,
				TSEN_THD0_MASK, tmp);
		tmp = (celsius_encode(thermal_dev.tsen_trips_temp_d[id][range - 1]) <<
						TSEN_THD1_OFF) & TSEN_THD1_MASK;
		reg_clr_set(off + TSEN_INT1_INT2_THLD_REG_0,
				TSEN_THD1_MASK, tmp);

		reg_clr_set(cfg_off + TSEN_CFG_REG_0, TSEN_INT0_ENABLE, 0);
		reg_clr_set(cfg_off + TSEN_CFG_REG_0, 0, TSEN_INT1_ENABLE);

	} else {
		tmp = (celsius_encode(thermal_dev.tsen_trips_temp[id][range]) <<
						TSEN_THD0_OFF) & TSEN_THD0_MASK;
		reg_clr_set(off + TSEN_INT0_WDOG_THLD_REG_0,
				TSEN_THD0_MASK, tmp);
		tmp = (celsius_encode(thermal_dev.tsen_trips_temp_d[id][range - 1]) <<
						TSEN_THD1_OFF) & TSEN_THD1_MASK;
		reg_clr_set(off + TSEN_INT1_INT2_THLD_REG_0,
				TSEN_THD1_MASK, tmp);
		reg_clr_set(cfg_off + TSEN_CFG_REG_0, 0, TSEN_INT0_ENABLE);
		reg_clr_set(cfg_off + TSEN_CFG_REG_0, 0, TSEN_INT1_ENABLE);
	}
	return 0;
}
static int pxa1928_resync_threshold(int id, int range)
{
	u32 tmp;
	int temp;
	u32 off = data_reg_offset(id);
	u32 cfg_off = cfg_reg_offset(id);
	if (range < 0 || range > TRIP_RANGE_MAX) {
		pr_err("soc thermal: invalid threshold %d\n", range);
		return -1;
	}
	switch (id) {
	case TSEN_VPU:
		tsen_sys_get_temp(thermal_dev.therm_vpu, &temp);
		break;

	case TSEN_CPU:
		tsen_sys_get_temp(thermal_dev.therm_cpu, &temp);

	case TSEN_GPU:
		tsen_sys_get_temp(thermal_dev.therm_gc, &temp);
		break;

	default:
		break;
	}

	if ((0 == range) && (temp >= thermal_dev.tsen_trips_temp_d[id][0])) {
		tmp = (celsius_encode(thermal_dev.tsen_trips_temp[id][0])
				<< TSEN_THD0_OFF) & TSEN_THD0_MASK;
		reg_clr_set(off + TSEN_INT0_WDOG_THLD_REG_0,
				TSEN_THD0_MASK, tmp);

		tmp = (celsius_encode(thermal_dev.tsen_trips_temp_d[id][0])
				<< TSEN_THD1_OFF) & TSEN_THD1_MASK;
		reg_clr_set(off + TSEN_INT1_INT2_THLD_REG_0,
				TSEN_THD1_MASK, tmp);
		reg_clr_set(cfg_off + TSEN_CFG_REG_0, 0, TSEN_INT0_ENABLE);
		reg_clr_set(cfg_off + TSEN_CFG_REG_0, 0, TSEN_INT1_ENABLE);

	}
	return 0;
}


static int tsen_set_trip_temp(struct thermal_zone_device *tz, int trip,
		unsigned long temp)
{
	int tsen_type = get_tsen_type(tz);

	if ((trip >= 0) && (trip < TRIP_POINTS_NUM))
		thermal_dev.tsen_trips_temp[tsen_type][trip] = temp;
	if ((TSEN_VPU == tsen_type) || (TSEN_CPU == tsen_type) || (TSEN_GPU == tsen_type)) {
		if ((TSEN_CPU != tsen_type) && ((TRIP_POINTS_NUM - 1) == trip)) {
			u32 tmp = (celsius_encode
			(thermal_dev.tsen_trips_temp_d[tsen_type][TRIP_POINTS_NUM - 1]) <<
					TSEN_THD2_OFF) & TSEN_THD2_MASK;
			u32 off = data_reg_offset(tsen_type);
			reg_clr_set(off + TSEN_INT1_INT2_THLD_REG_0,
					TSEN_THD2_MASK, tmp);
		}
		pxa1928_set_threshold(tsen_type, thermal_dev.trip_range[tsen_type]);
	}
	return 0;
}

static int tsen_sys_set_trip_temp(struct thermal_zone_device *tz, int trip,
		unsigned long temp)
{
	int tsen_type = get_tsen_type(tz);

	if (THERMAL_POLICY_POWER_SAVING == thermal_dev.therm_policy ||
			THERMAL_POLICY_BENCHMARK == thermal_dev.therm_policy) {
		if (TSEN_MAX == tsen_type) {
			if (NULL != thermal_dev.therm_max)
				tsen_set_trip_temp(thermal_dev.therm_max, trip, temp);
			if (NULL != thermal_dev.therm_vpu)
				tsen_set_trip_temp(thermal_dev.therm_vpu, trip, temp);
			if (NULL != thermal_dev.therm_cpu)
				tsen_set_trip_temp(thermal_dev.therm_cpu, trip, temp);
			if (NULL != thermal_dev.therm_gc)
				tsen_set_trip_temp(thermal_dev.therm_gc, trip, temp);
		} else {
			pr_info("use max temp solution, couldn't change this one\n");
		}
	} else {
		tsen_set_trip_temp(tz, trip, temp);
	}
	return 0;
}

static void auto_read_temp(struct thermal_zone_device *tz, const u32 reg,
		const u32 datareg)
{
	int int0, int1, int2;
	int temp = 0;
	int tsen_type = get_tsen_type(tz);
	tz->devdata = thermal_dev.base;

	mutex_lock(&thermal_dev.data_lock);

	int0 = (readl(tz->devdata + reg) & TSEN_INT0_STATUS) >> 3;
	int1 = (readl(tz->devdata + reg) & TSEN_INT1_STATUS) >> 6;
	int2 = (readl(tz->devdata + reg) & TSEN_INT2_STATUS) >> 9;

	if (int0 | int1 | int2) {
		if (int0) {
			writel(readl(tz->devdata + reg),
					tz->devdata + reg);
			thermal_dev.trip_range[tsen_type]++;
			if (thermal_dev.trip_range[tsen_type] > TRIP_RANGE_MAX)
				thermal_dev.trip_range[tsen_type] =
					TRIP_RANGE_MAX;
			pxa1928_set_threshold(tsen_type, thermal_dev.trip_range[tsen_type]);

		} else if (int1) {
			writel(readl(tz->devdata + reg),
					tz->devdata + reg);
			thermal_dev.trip_range[tsen_type]--;
			if (thermal_dev.trip_range[tsen_type] < 0)
				thermal_dev.trip_range[tsen_type] = 0;
			pxa1928_set_threshold(tsen_type, thermal_dev.trip_range[tsen_type]);
		} else {
			writel(readl(tz->devdata + reg),
				tz->devdata + reg);
			if (TSEN_CPU == tsen_type) {
				tsen_sys_get_temp(thermal_dev.therm_cpu, &temp);
				if (temp > PXA1928_SVC_OPT_TEMP_THLD) {
					pmic_reconfigure(0);
					reg_clr_set(TSEN_CFG_REG_1, TSEN_INT2_DIRECTION, 0);
				} else {
					pmic_reconfigure(1);
					reg_clr_set(TSEN_CFG_REG_1, 0, TSEN_INT2_DIRECTION);
				}
			} else {
				/* wait framework shutdown */
				tsen_sys_get_temp(thermal_dev.therm_cpu, &temp);
				pr_info("critical temp = %d, shutdown\n", temp);
			}
		}

		thermal_zone_device_update(tz);
	}

	mutex_unlock(&thermal_dev.data_lock);
}

static irqreturn_t thermal_threaded_handle_irq(int irq, void *dev_id)
{
	if (NULL != thermal_dev.therm_vpu)
		auto_read_temp(thermal_dev.therm_vpu, TSEN_CFG_REG_0, TSEN_DATA_REG_0);
	if (NULL != thermal_dev.therm_cpu)
		auto_read_temp(thermal_dev.therm_cpu, TSEN_CFG_REG_1, TSEN_DATA_REG_1);
	if (NULL != thermal_dev.therm_gc)
		auto_read_temp(thermal_dev.therm_gc, TSEN_CFG_REG_2, TSEN_DATA_REG_2);
	if (NULL != thermal_dev.therm_max) {
		pxa1928_resync_threshold(TSEN_VPU, thermal_dev.trip_range[TSEN_VPU]);
		pxa1928_resync_threshold(TSEN_CPU, thermal_dev.trip_range[TSEN_CPU]);
		pxa1928_resync_threshold(TSEN_GPU, thermal_dev.trip_range[TSEN_GPU]);
		thermal_zone_device_update(thermal_dev.therm_max);
	}
	return IRQ_HANDLED;
}

static int tsen_sys_get_crit_temp(struct thermal_zone_device *thermal,
		int *temp)
{
	int tsen_type = get_tsen_type(thermal);

	return thermal_dev.tsen_trips_temp[tsen_type][TRIP_POINT_MAX];
}

static int tsen_sys_notify(struct thermal_zone_device *thermal, int count,
		enum thermal_trip_type trip_type)
{
	if (THERMAL_TRIP_CRITICAL == trip_type)
		pr_info("notify critical temp hit\n");
	else
		pr_err("unexpected temp notify\n");
	return 0;
}

static struct thermal_zone_device_ops ts_ops = {
	.get_mode = tsen_sys_get_mode,
	.get_temp = tsen_sys_get_temp,
	.get_trip_type = tsen_sys_get_trip_type,
	.get_trip_temp = tsen_sys_get_trip_temp,
	.get_trip_temp_d = tsen_sys_get_trip_temp_d,
	.set_trip_temp = tsen_sys_set_trip_temp,
	.set_trip_temp_d = tsen_sys_set_trip_temp_d,
	.get_crit_temp = tsen_sys_get_crit_temp,
	.notify = tsen_sys_notify,
};

#ifdef CONFIG_PM
static void pxa1928_vpu_thermal_disable(void)
{
	void __iomem *reg_base = thermal_dev.base;
	disable_tsen(reg_base + TSEN_CFG_REG_0);
}

static void pxa1928_vpu_thermal_enable(void)
{
	void __iomem *reg_base = thermal_dev.base;
	enable_tsen(reg_base + TSEN_CFG_REG_0);
}

static void pxa1928_cpu_thermal_disable(void)
{
	void __iomem *reg_base = thermal_dev.base;
	disable_tsen(reg_base + TSEN_CFG_REG_1);
}

static void pxa1928_cpu_thermal_enable(void)
{
	void __iomem *reg_base = thermal_dev.base;
	enable_tsen(reg_base + TSEN_CFG_REG_1);
}

static void pxa1928_gc_thermal_disable(void)
{
	void __iomem *reg_base = thermal_dev.base;
	disable_tsen(reg_base + TSEN_CFG_REG_2);
}

static void pxa1928_gc_thermal_enable(void)
{
	void __iomem *reg_base = thermal_dev.base;
	enable_tsen(reg_base + TSEN_CFG_REG_2);
}

static int thermal_suspend(struct device *dev)
{
	pxa1928_vpu_thermal_disable();
	pxa1928_cpu_thermal_disable();
	pxa1928_gc_thermal_disable();
	clk_disable_unprepare(thermal_dev.therclk_vpu);
	clk_disable_unprepare(thermal_dev.therclk_cpu);
	clk_disable_unprepare(thermal_dev.therclk_gc);
	clk_disable_unprepare(thermal_dev.therclk_g);
	return 0;
}

static int thermal_resume(struct device *dev)
{
	clk_prepare_enable(thermal_dev.therclk_g);
	clk_prepare_enable(thermal_dev.therclk_vpu);
	clk_prepare_enable(thermal_dev.therclk_cpu);
	clk_prepare_enable(thermal_dev.therclk_gc);
	pxa1928_vpu_thermal_enable();
	pxa1928_cpu_thermal_enable();
	pxa1928_gc_thermal_enable();
	return 0;
}

static const struct dev_pm_ops thermal_pm_ops = {
	.suspend = thermal_suspend,
	.resume = thermal_resume,
};
#endif

#ifdef CONFIG_OF
static const struct of_device_id pxa1928_thermal_match[] = {
	{ .compatible = "mrvl,thermal", },
	{},
};
MODULE_DEVICE_TABLE(of, pxa1928_thermal_match);
#endif

static int pxa1928_register_cooling(void)
{

	thermal_dev.cool_pxa1928.cool_cpufreq = cpufreq_cool_register();
	thermal_dev.cool_pxa1928.cool_cpuhotplug = cpuhotplug_cool_register();
	thermal_dev.cool_pxa1928.cool_ddrfreq = ddrfreq_cool_register();
	thermal_dev.cool_pxa1928.cool_gc2dfreq = gpufreq_cool_register("gc2d");
	thermal_dev.cool_pxa1928.cool_gc3dfreq = gpufreq_cool_register("gc3d");
	thermal_dev.cool_pxa1928.cool_vpudecfreq = vpufreq_cool_register(DEVFREQ_VPU_0);
	thermal_dev.cool_pxa1928.cool_vpuencfreq = vpufreq_cool_register(DEVFREQ_VPU_1);
	return 0;
}

static int max_get_max_state(struct thermal_cooling_device *cdev,
		unsigned long *state)
{
	*state = thermal_dev.maxdev.max_state;
	return 0;
}

static int max_get_cur_state(struct thermal_cooling_device *cdev,
		unsigned long *state)
{
	*state = thermal_dev.maxdev.cur_state;
	return 0;
}

static int max_set_cur_state(struct thermal_cooling_device *cdev,
		unsigned long state)
{
	struct thermal_cooling_device *c_freq = thermal_dev.cool_pxa1928.cool_cpufreq;
	struct thermal_cooling_device *c_plug =
		thermal_dev.cool_pxa1928.cool_cpuhotplug;
	struct thermal_cooling_device *ddr_freq = thermal_dev.cool_pxa1928.cool_ddrfreq;
	struct thermal_cooling_device *gc2d_freq = thermal_dev.cool_pxa1928.cool_gc2dfreq;
	struct thermal_cooling_device *gc3d_freq = thermal_dev.cool_pxa1928.cool_gc3dfreq;
	struct thermal_cooling_device *vpudec_freq = thermal_dev.cool_pxa1928.cool_vpudecfreq;
	struct thermal_cooling_device *vpuenc_freq = thermal_dev.cool_pxa1928.cool_vpuencfreq;
	unsigned long freq_state = 0, plug_state = 0;
	unsigned long ddr_freq_state;
	unsigned long gc2d_freq_state = 0, gc3d_freq_state = 0;
	unsigned long vpudec_freq_state = 0, vpuenc_freq_state = 0;
	static u32 prev_state;
	if (state > thermal_dev.maxdev.max_state)
		return -EINVAL;
	thermal_dev.maxdev.cur_state = state;

	/* core related throttle */
	freq_state = thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][THROTTLE_CORE][state + 1];
	plug_state = thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][THROTTLE_HOTPLUG][state + 1];
	if (c_freq)
		c_freq->ops->set_cur_state(c_freq, freq_state);
	if (c_plug)
		c_plug->ops->set_cur_state(c_plug, plug_state);
	trace_pxa_thermal_trip("max", thermal_dev.temp_max,
			state, freq_state);
	pr_info("Thermal max temp: %d; CPU Throttle state %lu: cpufreq qos %lu, core_num qos %lu\n",
	thermal_dev.temp_max, state, freq_state, plug_state);

	/* ddr related throttle */
	ddr_freq_state = thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][THROTTLE_DDR][state + 1];

	if (ddr_freq)
		ddr_freq->ops->set_cur_state(ddr_freq, ddr_freq_state);

	trace_pxa_thermal_trip("max", thermal_dev.temp_max,
			state, ddr_freq_state);
	pr_info("Thermal max temp: %d; DDR Throttle state %lu: ddrfreq qos %lu\n",
	thermal_dev.temp_max, state, ddr_freq_state);

	/* gc related throttle */
	gc3d_freq_state = thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC3D][state + 1];
	gc2d_freq_state = thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC2D][state + 1];
	if (gc2d_freq)
		gc2d_freq->ops->set_cur_state(gc2d_freq, gc2d_freq_state);
	if (gc3d_freq)
		gc3d_freq->ops->set_cur_state(gc3d_freq, gc3d_freq_state);
	trace_pxa_thermal_trip("max", thermal_dev.temp_max,
			state, gc3d_freq_state);
	pr_info("Thermal max temp: %d: GC Throttle state %lu: gc2d qos %lu, gc3d qos %lu\n",
			thermal_dev.temp_max, state, gc2d_freq_state,
			gc3d_freq_state);

	/* vpu related throttle */
	vpudec_freq_state = thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUDEC][state + 1];
	vpuenc_freq_state = thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUENC][state + 1];
	if (vpudec_freq)
		vpudec_freq->ops->set_cur_state(vpudec_freq, vpudec_freq_state);
	if (vpuenc_freq)
		vpuenc_freq->ops->set_cur_state(vpuenc_freq, vpuenc_freq_state);
	trace_pxa_thermal_trip("max", thermal_dev.temp_max,
			state, vpuenc_freq_state);
	pr_info("Thermal max temp: %d; VPU Throttle state %lu: vpudec qos %lu, vpuenc qos %lu\n",
			thermal_dev.temp_max, state, vpudec_freq_state,
			vpuenc_freq_state);

	if (prev_state < state)
		pr_info("Thermal frequency limitation, performance impact expected!");
	prev_state = state;
	return 0;
}

static struct thermal_cooling_device_ops const max_cooling_ops = {
	.get_max_state = max_get_max_state,
	.get_cur_state = max_get_cur_state,
	.set_cur_state = max_set_cur_state,
};

/* Register with the in-kernel thermal management */
static int pxa1928_register_max_thermal(void)
{
	int i, trip_w_mask = 0;

	thermal_dev.maxdev.cool_tsen = thermal_cooling_device_register(
		"max-combine-cool", &thermal_dev, &max_cooling_ops);

	thermal_dev.maxdev.max_state = TRIP_RANGE_MAX;
	thermal_dev.maxdev.cur_state = 0;

	for (i = 0; i < TRIP_POINTS_NUM; i++)
		trip_w_mask |= (1 << i);

	thermal_dev.therm_max = thermal_zone_device_register(
			"tsen_max", TRIP_POINTS_NUM, trip_w_mask,
			&thermal_dev, &ts_ops, NULL, 0, 0);
	/*
	 * enable bi_direction state machine, then it didn't care
	 * whether up/down trip points are crossed or not
	 */
	thermal_dev.therm_max->tzdctrl.state_ctrl = true;
	/* bind combine cooling */
	if (THERMAL_POLICY_POWER_SAVING == thermal_dev.therm_policy ||
			THERMAL_POLICY_BENCHMARK == thermal_dev.therm_policy)
		thermal_zone_bind_cooling_device(thermal_dev.therm_max,
			0, thermal_dev.maxdev.cool_tsen,
			THERMAL_NO_LIMIT, THERMAL_NO_LIMIT);

	return 0;
}

static int cpu_get_max_state(struct thermal_cooling_device *cdev,
		unsigned long *state)
{
	*state = thermal_dev.cdev.max_state;
	return 0;
}

static int cpu_get_cur_state(struct thermal_cooling_device *cdev,
		unsigned long *state)
{
	*state = thermal_dev.cdev.cur_state;
	return 0;
}

static int cpu_set_cur_state(struct thermal_cooling_device *cdev,
		unsigned long state)
{
	struct thermal_cooling_device *c_freq = thermal_dev.cool_pxa1928.cool_cpufreq;
	struct thermal_cooling_device *c_plug =
		thermal_dev.cool_pxa1928.cool_cpuhotplug;

	unsigned long freq_state = 0, plug_state = 0;
	static u32 prev_state;

	if (state > thermal_dev.cdev.max_state)
		return -EINVAL;
	thermal_dev.cdev.cur_state = state;

	freq_state = thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][THROTTLE_CORE][state + 1];
	plug_state = thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][THROTTLE_HOTPLUG][state + 1];

	if (c_freq)
		c_freq->ops->set_cur_state(c_freq, freq_state);
	if (c_plug)
		c_plug->ops->set_cur_state(c_plug, plug_state);

	trace_pxa_thermal_trip("cpu", thermal_dev.temp_cpu,
			state, freq_state);
	pr_info("Thermal cpu temp %d, state %lu, cpufreq qos %lu, core_num qos %lu\n",
	thermal_dev.temp_cpu, state, freq_state, plug_state);
	if (prev_state < state)
		pr_info("Thermal cpu frequency limitation, performance impact expected!");
	prev_state = state;
	return 0;

}

static struct thermal_cooling_device_ops const cpu_cooling_ops = {
	.get_max_state = cpu_get_max_state,
	.get_cur_state = cpu_get_cur_state,
	.set_cur_state = cpu_set_cur_state,
};

/* Register with the in-kernel thermal management */
static int pxa1928_register_cpu_thermal(void)
{
	int i, trip_w_mask = 0;

	thermal_dev.cdev.cool_tsen = thermal_cooling_device_register(
		"cpu-combine-cool", &thermal_dev, &cpu_cooling_ops);
	thermal_dev.cdev.max_state = TRIP_RANGE_MAX;
	thermal_dev.cdev.cur_state = 0;

	for (i = 0; i < TRIP_POINTS_NUM; i++)
		trip_w_mask |= (1 << i);

	thermal_dev.therm_cpu = thermal_zone_device_register(
			"tsen_cpu", TRIP_POINTS_NUM, trip_w_mask,
			&thermal_dev, &ts_ops, NULL, 0, 0);
	/*
	 * enable bi_direction state machine, then it didn't care
	 * whether up/down trip points are crossed or not
	 */
	thermal_dev.therm_cpu->tzdctrl.state_ctrl = true;
	/* bind combine cooling */
#ifndef CONFIG_TSEN_MAX
	if (!(THERMAL_POLICY_POWER_SAVING == thermal_dev.therm_policy ||
			THERMAL_POLICY_POWER_SAVING == thermal_dev.therm_policy))
		thermal_zone_bind_cooling_device(thermal_dev.therm_cpu,
				0, thermal_dev.cdev.cool_tsen,
				THERMAL_NO_LIMIT, THERMAL_NO_LIMIT);
#endif
	return 0;
}

void pxa1928_cpu_thermal_initialize(void)
{
	u32 tmp;
	void __iomem *reg_base = thermal_dev.base;
	enable_tsen(reg_base + TSEN_CFG_REG_1);

	init_wd_int0_threshold(TSEN_CPU, reg_base +
			TSEN_INT0_WDOG_THLD_REG_1);
	init_int1_int2_threshold(TSEN_CPU, reg_base +
			TSEN_INT1_INT2_THLD_REG_1);

	tmp = (celsius_encode(PXA1928_SVC_OPT_TEMP_THLD) <<
			TSEN_THD2_OFF) & TSEN_THD2_MASK;
	reg_clr_set(TSEN_INT1_INT2_THLD_REG_1, TSEN_THD2_MASK, tmp);

	init_tsen_irq_dir(reg_base + TSEN_CFG_REG_1);
	enable_auto_read(reg_base + TSEN_CFG_REG_1);
	reg_clr_set(TSEN_CFG_REG_1, TSEN_INT1_DIRECTION, 0);

	thermal_dev.trip_range[TSEN_CPU] = 0;
	pxa1928_set_threshold(TSEN_CPU, thermal_dev.trip_range[TSEN_CPU]);

	set_auto_read_interval(reg_base + TSEN_AUTO_READ_VALUE_REG_1);
}


static int vpu_get_max_state(struct thermal_cooling_device *vdev,
		unsigned long *state)
{
	*state = thermal_dev.vdev.max_state;
	return 0;
}

static int vpu_get_cur_state(struct thermal_cooling_device *vdev,
		unsigned long *state)
{
	*state = thermal_dev.vdev.cur_state;
	return 0;
}

static int vpu_set_cur_state(struct thermal_cooling_device *gdev,
		unsigned long state)
{
	struct thermal_cooling_device *vpudec_freq = thermal_dev.cool_pxa1928.cool_vpudecfreq;
	struct thermal_cooling_device *vpuenc_freq = thermal_dev.cool_pxa1928.cool_vpuencfreq;

	unsigned long vpudec_freq_state = 0, vpuenc_freq_state = 0;

	if (state > thermal_dev.vdev.max_state)
		return -EINVAL;
	thermal_dev.vdev.cur_state = state;

	vpudec_freq_state = thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUDEC][state + 1];
	vpuenc_freq_state = thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][THROTTLE_VPUENC][state + 1];

	if (vpudec_freq)
		vpudec_freq->ops->set_cur_state(vpudec_freq, vpudec_freq_state);

	if (vpuenc_freq)
		vpuenc_freq->ops->set_cur_state(vpuenc_freq, vpuenc_freq_state);

	trace_pxa_thermal_trip("gc", thermal_dev.temp_gc,
			state, vpuenc_freq_state);
	pr_info("Thermal gc temp %d, state %lu, vpudec qos %lu, vpuenc qos %lu\n",
			thermal_dev.temp_gc, state, vpudec_freq_state,
			vpuenc_freq_state);
	return 0;
}

static struct thermal_cooling_device_ops const vpu_cooling_ops = {
	.get_max_state = vpu_get_max_state,
	.get_cur_state = vpu_get_cur_state,
	.set_cur_state = vpu_set_cur_state,
};

/* Register with the in-kernel thermal management */
static int pxa1928_register_vpu_thermal(void)
{
	int i, trip_w_mask = 0;

	thermal_dev.vdev.cool_tsen = thermal_cooling_device_register(
			"vpu-combine-cool", &thermal_dev, &vpu_cooling_ops);
	thermal_dev.vdev.max_state = TRIP_RANGE_MAX;
	thermal_dev.vdev.cur_state = 0;

	for (i = 0; i < TRIP_POINTS_NUM; i++)
		trip_w_mask |= (1 << i);

	thermal_dev.therm_vpu = thermal_zone_device_register(
			"tsen_vpu", TRIP_POINTS_NUM, trip_w_mask,
			&thermal_dev, &ts_ops, NULL, 0, 0);

	/*
	 * enable bi_direction state machine, then it didn't care
	 * whether up/down trip points are crossed or not
	 */
	thermal_dev.therm_vpu->tzdctrl.state_ctrl = true;
	/* bind combine cooling */
#ifndef CONFIG_TSEN_MAX
	if (!(THERMAL_POLICY_POWER_SAVING == thermal_dev.therm_policy ||
			THERMAL_POLICY_POWER_SAVING == thermal_dev.therm_policy))
		thermal_zone_bind_cooling_device(thermal_dev.therm_vpu,
			0, thermal_dev.vdev.cool_tsen,
			THERMAL_NO_LIMIT, THERMAL_NO_LIMIT);
#endif

	return 0;
}

void pxa1928_vpu_thermal_initialize(void)
{
	u32 tmp;
	void __iomem *reg_base = thermal_dev.base;
	enable_tsen(reg_base + TSEN_CFG_REG_0);

	init_wd_int0_threshold(TSEN_VPU, reg_base +
			TSEN_INT0_WDOG_THLD_REG_0);
	init_int1_int2_threshold(TSEN_VPU, reg_base +
			TSEN_INT1_INT2_THLD_REG_0);

	tmp = (celsius_encode(thermal_dev.tsen_trips_temp_d[TSEN_VPU][TRIP_POINTS_NUM - 1]) <<
			TSEN_THD2_OFF) & TSEN_THD2_MASK;
	reg_clr_set(TSEN_INT1_INT2_THLD_REG_0, TSEN_THD2_MASK, tmp);

	thermal_dev.trip_range[TSEN_VPU] = 0;
	pxa1928_set_threshold(0, thermal_dev.trip_range[TSEN_VPU]);

	init_tsen_irq_dir(reg_base + TSEN_CFG_REG_0);
	enable_auto_read(reg_base + TSEN_CFG_REG_0);
	reg_clr_set(TSEN_CFG_REG_0, TSEN_INT1_DIRECTION, 0);

	set_auto_read_interval(reg_base + TSEN_AUTO_READ_VALUE_REG_0);
}

static int gpu_get_max_state(struct thermal_cooling_device *gdev,
		unsigned long *state)
{
	*state = thermal_dev.gdev.max_state;
	return 0;
}

static int gpu_get_cur_state(struct thermal_cooling_device *gdev,
		unsigned long *state)
{
	*state = thermal_dev.gdev.cur_state;
	return 0;
}

static int gpu_set_cur_state(struct thermal_cooling_device *gdev,
		unsigned long state)
{
	struct thermal_cooling_device *gc2d_freq = thermal_dev.cool_pxa1928.cool_gc2dfreq;
	struct thermal_cooling_device *gc3d_freq = thermal_dev.cool_pxa1928.cool_gc3dfreq;

	unsigned long gc2d_freq_state = 0, gc3d_freq_state = 0;

	if (state > thermal_dev.gdev.max_state)
		return -EINVAL;
	thermal_dev.gdev.cur_state = state;

	gc3d_freq_state = thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC3D][state + 1];
	gc2d_freq_state = thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][THROTTLE_GC2D][state + 1];

	if (gc2d_freq)
		gc2d_freq->ops->set_cur_state(gc2d_freq, gc2d_freq_state);

	if (gc3d_freq)
		gc3d_freq->ops->set_cur_state(gc3d_freq, gc3d_freq_state);


	trace_pxa_thermal_trip("gc", thermal_dev.temp_gc,
			state, gc3d_freq_state);
	pr_info("Thermal gc temp %d, state %lu, gc2d qos %lu, gc3d qos %lu\n",
			thermal_dev.temp_gc, state, gc2d_freq_state,
			gc3d_freq_state);
	return 0;
}

static struct thermal_cooling_device_ops const gpu_cooling_ops = {
	.get_max_state = gpu_get_max_state,
	.get_cur_state = gpu_get_cur_state,
	.set_cur_state = gpu_set_cur_state,
};

/* Register with the in-kernel thermal management */
static int pxa1928_register_gc_thermal(void)
{
	int i, trip_w_mask = 0;
	int ret = 0;

	for (i = 0; i < TRIP_POINTS_NUM; i++)
		trip_w_mask |= (1 << i);

	thermal_dev.therm_gc = thermal_zone_device_register(
			"tsen_gc", TRIP_POINTS_NUM, trip_w_mask,
			&thermal_dev, &ts_ops, NULL, 0, 0);

	/*
	 * enable bi_direction state machine, then it didn't care
	 * whether up/down trip points are crossed or not
	 */
	thermal_dev.therm_gc->tzdctrl.state_ctrl = true;

	thermal_dev.gdev.cool_tsen = thermal_cooling_device_register(
			"gc-combine-cool", &thermal_dev, &gpu_cooling_ops);
	thermal_dev.gdev.max_state = TRIP_RANGE_MAX;
	thermal_dev.gdev.cur_state = 0;

	/* bind combine cooling */
#ifndef CONFIG_TSEN_MAX
	if (!(THERMAL_POLICY_POWER_SAVING == thermal_dev.therm_policy ||
			THERMAL_POLICY_POWER_SAVING == thermal_dev.therm_policy))
		thermal_zone_bind_cooling_device(thermal_dev.therm_gc,
			0, thermal_dev.gdev.cool_tsen,
			THERMAL_NO_LIMIT, THERMAL_NO_LIMIT);
#endif
	return ret;
}

void pxa1928_gc_thermal_initialize(void)
{
	u32 tmp;
	void __iomem *reg_base = thermal_dev.base;
	enable_tsen(reg_base + TSEN_CFG_REG_2);

	init_wd_int0_threshold(TSEN_GPU, reg_base +
			TSEN_INT0_WDOG_THLD_REG_2);
	init_int1_int2_threshold(TSEN_GPU, reg_base +
			TSEN_INT1_INT2_THLD_REG_2);

	tmp = (celsius_encode(thermal_dev.tsen_trips_temp[TSEN_GPU][TRIP_POINTS_NUM - 1]) <<
			TSEN_THD2_OFF) & TSEN_THD2_MASK;
	reg_clr_set(TSEN_INT1_INT2_THLD_REG_2, TSEN_THD2_MASK, tmp);

	thermal_dev.trip_range[TSEN_GPU] = 0;
	pxa1928_set_threshold(TSEN_GPU, thermal_dev.trip_range[TSEN_GPU]);

	init_tsen_irq_dir(reg_base + TSEN_CFG_REG_2);
	enable_auto_read(reg_base + TSEN_CFG_REG_2);
	reg_clr_set(TSEN_CFG_REG_2, TSEN_INT1_DIRECTION, 0);

	set_auto_read_interval(reg_base + TSEN_AUTO_READ_VALUE_REG_2);

}

static int read_profile_from_dt(void)
{
	struct device_node *node;
	int pro;

	node = of_find_compatible_node(NULL, NULL, "marvell,profile");
	if (node)
		of_property_read_u32(node, "marvell,profile-number", &pro);
	if (pro > 15 || pro < 0)
		pr_err("Wrong profile setting! Use 0 as default.\n");
	return  pro;

}

static int tsen_cdev_update(struct thermal_zone_device *tz)
{
	struct thermal_instance *instance;
	list_for_each_entry(instance, &tz->thermal_instances, tz_node) {
		instance->cdev->updated = false;
		thermal_cdev_update(instance->cdev);
	}
	return 0;
}

static ssize_t policy_read(struct file *filp,
		char __user *buffer, size_t count, loff_t *ppos)
{
	char *buf;
	size_t size = PAGE_SIZE - 1;
	int s;

	buf = (char *)__get_free_pages(GFP_KERNEL, 0);
	if (!buf) {
		pr_err("memory alloc for therm policy dump is failed!!\n");
		return -ENOMEM;
	}
	s = tsen_policy_dump(buf, size);
	return simple_read_from_buffer(buffer, count, ppos, buf, strlen(buf));
}

static ssize_t policy_write(struct file *filp,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	char buf[CMD_BUF_SIZE] = { 0 };
	char buf_name[20];
	unsigned int max_state;
	int i;
	unsigned int vl_master_en, private_en, state0, state1, state2, state3, state4, state5, state6, state7;
	int throttle_type;

	if (copy_from_user(buf, buffer, count))
		return -EFAULT;

	if (sscanf(buf, "%s %u %u %u %u %u %u %u %u %u %u", buf_name, &vl_master_en, &private_en,
		&state0, &state1, &state2, &state3, &state4, &state5, &state6, &state7) != 11) {
		pr_info("format error: component vl_master private_en state0 state1 state2 state3 state4 state5 state6 state7\n");
		return -EINVAL;
	}


	if (!strcmp(buf_name, "VL")) {
		max_state = PXA1928_DVC_LEVEL_NUM - 1;
		throttle_type = THROTTLE_VL;
	} else if (!strcmp(buf_name, "CORE")) {
		max_state = thermal_dev.cpufreq_tbl.freq_num - 1;
		throttle_type = THROTTLE_CORE;
	} else if (!strcmp(buf_name, "HOTPLUG")) {
		max_state = CPU_MAX_NUM - 1;
		throttle_type = THROTTLE_HOTPLUG;
	} else if (!strcmp(buf_name, "DDR")) {
		max_state = thermal_dev.ddrfreq_tbl.freq_num - 1;
		throttle_type = THROTTLE_DDR;
	} else if (!strcmp(buf_name, "GC3D")) {
		max_state = thermal_dev.gc3dfreq_tbl.freq_num - 1;
		throttle_type = THROTTLE_GC3D;
	} else if (!strcmp(buf_name, "GC2D")) {
		max_state = thermal_dev.gc2dfreq_tbl.freq_num - 1;
		throttle_type = THROTTLE_GC2D;
	} else if (!strcmp(buf_name, "VPUDEC")) {
		max_state = thermal_dev.vpudecfreq_tbl.freq_num - 1;
		throttle_type = THROTTLE_VPUDEC;
	} else if (!strcmp(buf_name, "VPUENC")) {
		max_state = thermal_dev.vpuencfreq_tbl.freq_num - 1;
		throttle_type = THROTTLE_VPUENC;
	} else {
		pr_info("format error: component should be <VL, CORE, HOTPLUG, DDR, GC3D, GC2D, VPUDEC, VPUENC>\n");
		return -EINVAL;
	}

	if (vl_master_en < 0 || vl_master_en > ((0x1 << TRIP_RANGE_NUM) - 1)) {
		pr_info("format error: vl_master should be 0 to %d\n", (0x1 << TRIP_RANGE_NUM) - 1);
		return -EINVAL;
	} else if (private_en < 0 || private_en > ((0x1 << TRIP_RANGE_NUM) -1)) {
		pr_info("format error: private_en should be 0 to %d\n", (0x1 << TRIP_RANGE_NUM ) - 1);
		return -EINVAL;
	} else if (state0 > max_state) {
		pr_info("format error: state should be 0 to %d\n", max_state);
		return -EINVAL;
	} else if (state1 > max_state) {
		pr_info("format error: state should be 1 to %d\n", max_state);
		return -EINVAL;
	} else if (state2 > max_state) {
		pr_info("format error: state should be 2 to %d\n", max_state);
		return -EINVAL;
	} else if (state3 > max_state) {
		pr_info("format error: state should be 3 to %d\n", max_state);
		return -EINVAL;
	} else if (state4 > max_state) {
		pr_info("format error: state should be 4 to %d\n", max_state);
		return -EINVAL;
	} else if (state5 > max_state) {
		pr_info("format error: state should be 5 to %d\n", max_state);
		return -EINVAL;
	} else if (state6 > max_state) {
		pr_info("format error: state should be 6 to %d\n", max_state);
		return -EINVAL;
	} else if (state7 > max_state) {
		pr_info("format error: state should be 7 to %d\n", max_state);
		return -EINVAL;
	}

	mutex_lock(&thermal_dev.policy_lock);
	for (i = TRIP_RANGE_0; i <= TRIP_RANGE_MAX; i++) {
		if(test_bit(i, (void *)&vl_master_en)) {
			thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][THROTTLE_VLMASTER][i + 1]
				= throttle_type;
		}
	}
	thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][throttle_type][0] = private_en;
	thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][throttle_type][TRIP_RANGE_0 + 1] = state0;
	thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][throttle_type][TRIP_RANGE_1 + 1] = state1;
	thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][throttle_type][TRIP_RANGE_2 + 1] = state2;
	thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][throttle_type][TRIP_RANGE_3 + 1] = state3;
	thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][throttle_type][TRIP_RANGE_4 + 1] = state4;
	thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][throttle_type][TRIP_RANGE_5 + 1] = state5;
	thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][throttle_type][TRIP_RANGE_6 + 1] = state6;
	thermal_dev.tsen_throttle_tbl[thermal_dev.therm_policy][throttle_type][TRIP_RANGE_7 + 1] = state7;

	tsen_update_policy();
	tsen_cdev_update(thermal_dev.therm_vpu);
	tsen_cdev_update(thermal_dev.therm_cpu);
	tsen_cdev_update(thermal_dev.therm_gc);
	tsen_cdev_update(thermal_dev.therm_max);
	mutex_unlock(&thermal_dev.policy_lock);

	return count;
}

static const struct file_operations policy_ops = {
	.owner = THIS_MODULE,
	.read = policy_read,
	.write = policy_write,
};

static ssize_t policy_switch_write(struct file *filp,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	char buf[CMD_BUF_SIZE] = { 0 };
	char buf_name[20];

	if (copy_from_user(buf, buffer, count))
		return -EFAULT;

	if (sscanf(buf, "%s", buf_name) != 1) {
		pr_info("format error: input benchmark or powersave mode\n");
		return -EINVAL;
	}
	if (!strcmp(buf_name, "benchmark"))
		thermal_dev.therm_policy = THERMAL_POLICY_BENCHMARK;
	else if (!strcmp(buf_name, "powersave"))
		thermal_dev.therm_policy = THERMAL_POLICY_POWER_SAVING;

	if (thermal_dev.therm_policy == THERMAL_POLICY_POWER_SAVING) {
		thermal_dev.tsen_trips_temp = powersaving_tsen_trips_temp;
		thermal_dev.tsen_trips_temp_d = powersaving_tsen_trips_temp_d;
	} else {
		thermal_dev.tsen_trips_temp = benchmark_tsen_trips_temp;
		thermal_dev.tsen_trips_temp_d = benchmark_tsen_trips_temp_d;
	}

	tsen_update_policy();

	pxa1928_set_threshold(TSEN_VPU, thermal_dev.trip_range[TSEN_VPU]);
	pxa1928_set_threshold(TSEN_CPU, thermal_dev.trip_range[TSEN_CPU]);
	pxa1928_set_threshold(TSEN_GPU, thermal_dev.trip_range[TSEN_GPU]);

	if (thermal_dev.therm_vpu != NULL)
		thermal_zone_device_update(thermal_dev.therm_vpu);
	if (NULL != thermal_dev.therm_cpu)
		thermal_zone_device_update(thermal_dev.therm_cpu);
	if (NULL != thermal_dev.therm_gc)
		thermal_zone_device_update(thermal_dev.therm_gc);
	if (NULL != thermal_dev.therm_max)
		thermal_zone_device_update(thermal_dev.therm_max);

	return count;
}


static ssize_t policy_switch_read(struct file *filp,
		char __user *buffer, size_t count, loff_t *ppos)
{
	char val[20];
	switch (thermal_dev.therm_policy) {
	case THERMAL_POLICY_BENCHMARK:
		sprintf(val, "benchmark\n");
		break;
	case THERMAL_POLICY_POWER_SAVING:
		sprintf(val, "powersave\n");
		break;
	default:
		break;
	}

	return simple_read_from_buffer(buffer, count, ppos, val,
			strlen(val));
}

static const struct file_operations policy_switch_ops = {
	.owner = THIS_MODULE,
	.read = policy_switch_read,
	.write = policy_switch_write,
};


int register_debug_interface(void)
{
	struct dentry *sysset;
	struct dentry *sys_policy_reg;
	struct dentry *sys_policy_switch_reg;
	sysset = debugfs_create_dir("pxa1928_thermal", NULL);
	if (!sysset)
		return -ENOENT;
	sys_policy_reg = debugfs_create_file("policy", 0664, sysset,
					NULL, &policy_ops);
	if (!sys_policy_reg) {
		pr_err("debugfs entry created failed in %s\n", __func__);
		return -ENOENT;
	}
	sys_policy_switch_reg = debugfs_create_file("policy_switch", 0664, sysset,
					NULL, &policy_switch_ops);
	if (!sys_policy_switch_reg) {
		pr_err("policy_switch debugfs entry created failed in %s\n", __func__);
		return -ENOENT;
	}
	return 0;
}

void svc_thermal_enable(void)
{
	int temp;

	tsen_sys_get_temp(thermal_dev.therm_cpu, &temp);
	pr_info("the temp of therm_cpu = %d\n", temp);
	if (temp > 25000)
		pmic_reconfigure(0);
	else
		pmic_reconfigure(1);
}
EXPORT_SYMBOL(svc_thermal_enable);

static int pxa1928_thermal_probe(struct platform_device *pdev)
{
	int ret = 0, irq;
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	void *reg_base;
	const char *str = NULL;

	thermal_dev.pre_temp_vpu = -INVALID_TEMP;
	thermal_dev.pre_temp_cpu = -INVALID_TEMP;
	thermal_dev.pre_temp_gc = -INVALID_TEMP;
	/* get resources from platform data */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		pr_err("%s: no IO memory defined\n", __func__);
		ret = -ENOENT;
		goto res_fail;
	}

	/* map registers.*/
	if (!devm_request_mem_region(&pdev->dev, res->start,
			resource_size(res), "thermal")) {
		pr_err("can't request region for resource %p\n", res);
		ret = -EINVAL;
		goto res_fail;
	}

	reg_base = devm_ioremap_nocache(&pdev->dev,
			res->start, resource_size(res));

	if (reg_base == NULL) {
		pr_err("%s: res %lx - %lx map failed\n", __func__,
			(unsigned long)res->start, (unsigned long)res->end);
		ret = -ENOMEM;
		goto remap_fail;
	}
	thermal_dev.base = reg_base;

	thermal_dev.therclk_g = clk_get(NULL, "THERMALCLK_G");
	if (IS_ERR(thermal_dev.therclk_g)) {
		pr_err("Could not get thermal clock global\n");
		ret = PTR_ERR(thermal_dev.therclk_g);
		goto clk_fail;
	}
	clk_prepare_enable(thermal_dev.therclk_g);

	thermal_dev.therclk_vpu = clk_get(NULL, "THERMALCLK_VPU");
	if (IS_ERR(thermal_dev.therclk_vpu)) {
		pr_err("Could not get thermal vpu clock\n");
		ret = PTR_ERR(thermal_dev.therclk_vpu);
		goto clk_disable_g;
	}
	clk_prepare_enable(thermal_dev.therclk_vpu);

	thermal_dev.therclk_cpu = clk_get(NULL, "THERMALCLK_CPU");
	if (IS_ERR(thermal_dev.therclk_cpu)) {
		pr_err("Could not get thermal cpu clock\n");
		ret = PTR_ERR(thermal_dev.therclk_cpu);
		goto clk_disable_0;
	}
	clk_prepare_enable(thermal_dev.therclk_cpu);

	thermal_dev.therclk_gc = clk_get(NULL, "THERMALCLK_GC");
	if (IS_ERR(thermal_dev.therclk_gc)) {
		pr_err("Could not get thermal gc clock\n");
		ret = PTR_ERR(thermal_dev.therclk_gc);
		goto clk_disable_1;
	}
	clk_prepare_enable(thermal_dev.therclk_gc);

	irq = platform_get_irq(pdev, 0);
	thermal_dev.irq = irq;
	if (irq < 0) {
		dev_err(&pdev->dev, "no IRQ resource defined\n");
		ret = -ENXIO;
		goto clk_disable_2;
	}

	ret = request_threaded_irq(irq, NULL, thermal_threaded_handle_irq,
		IRQF_ONESHOT, pdev->name, NULL);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		ret = -ENXIO;
		goto clk_disable_2;
	}

	mutex_init(&thermal_dev.data_lock);
	mutex_init(&thermal_dev.policy_lock);


	thermal_dev.profile = read_profile_from_dt();
	if (IS_ENABLED(CONFIG_OF)) {
		if (np) {
			if (np && !of_property_read_string(np, "thermal_policy", &str)
				&& !strcmp(str, "FF"))
				thermal_dev.therm_policy = THERMAL_POLICY_POWER_SAVING;
			else
				thermal_dev.therm_policy = THERMAL_POLICY_BENCHMARK;
		}
	}

	get_svc_freqs_cmb_tbl(&(thermal_dev.svc_tbl));

	thermal_dev.tsen_throttle_tbl = pxa1928_tsen_throttle_tbl;
	if (thermal_dev.therm_policy == THERMAL_POLICY_POWER_SAVING) {
		thermal_dev.tsen_trips_temp = powersaving_tsen_trips_temp;
		thermal_dev.tsen_trips_temp_d = powersaving_tsen_trips_temp_d;
	} else {
		thermal_dev.tsen_trips_temp = benchmark_tsen_trips_temp;
		thermal_dev.tsen_trips_temp_d = benchmark_tsen_trips_temp_d;
	}

	tsen_update_policy();
	pxa1928_register_cooling();

	pxa1928_register_vpu_thermal();
	pxa1928_vpu_thermal_initialize();

	pxa1928_cpu_thermal_initialize();
	pxa1928_register_cpu_thermal();

	pxa1928_register_gc_thermal();
	pxa1928_gc_thermal_initialize();

	pxa1928_register_max_thermal();

	register_debug_interface();
	return ret;

clk_disable_2:
	clk_disable_unprepare(thermal_dev.therclk_gc);
clk_disable_1:
	clk_disable_unprepare(thermal_dev.therclk_cpu);
clk_disable_0:
	clk_disable_unprepare(thermal_dev.therclk_vpu);
clk_disable_g:
	clk_disable_unprepare(thermal_dev.therclk_g);
clk_fail:
	devm_iounmap(&pdev->dev, thermal_dev.base);
remap_fail:
	devm_release_mem_region(&pdev->dev, res->start, resource_size(res));
res_fail:
	pr_err("device init failed\n");

	return ret;
}

static int pxa1928_thermal_remove(struct platform_device *pdev)
{
	clk_disable_unprepare(thermal_dev.therclk_vpu);
	clk_disable_unprepare(thermal_dev.therclk_cpu);
	clk_disable_unprepare(thermal_dev.therclk_gc);
	clk_disable_unprepare(thermal_dev.therclk_g);

	cpufreq_cool_unregister(thermal_dev.cool_pxa1928.cool_cpufreq);
	cpuhotplug_cool_unregister(thermal_dev.cool_pxa1928.cool_cpuhotplug);
	vpufreq_cool_unregister(thermal_dev.cool_pxa1928.cool_vpudecfreq);
	vpufreq_cool_unregister(thermal_dev.cool_pxa1928.cool_vpuencfreq);
	gpufreq_cool_unregister(thermal_dev.cool_pxa1928.cool_gc3dfreq);
	gpufreq_cool_unregister(thermal_dev.cool_pxa1928.cool_gc2dfreq);

	thermal_cooling_device_unregister(thermal_dev.cdev.cool_tsen);
	thermal_cooling_device_unregister(thermal_dev.vdev.cool_tsen);
	thermal_cooling_device_unregister(thermal_dev.gdev.cool_tsen);
	thermal_cooling_device_unregister(thermal_dev.maxdev.cool_tsen);
	thermal_zone_device_unregister(thermal_dev.therm_cpu);
	thermal_zone_device_unregister(thermal_dev.therm_vpu);
	thermal_zone_device_unregister(thermal_dev.therm_gc);

	free_irq(thermal_dev.irq, NULL);
	pr_info("PXA1928: Kernel Thermal management unregistered\n");
	return 0;
}

static struct platform_driver pxa1928_thermal_driver = {
	.driver = {
		.name	= "thermal",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm	= &thermal_pm_ops,
#endif
		.of_match_table = of_match_ptr(pxa1928_thermal_match),
	},
	.probe		= pxa1928_thermal_probe,
	.remove		= pxa1928_thermal_remove,
};

static int __init pxa1928_thermal_init(void)
{
	return platform_driver_register(&pxa1928_thermal_driver);
}

static void __exit pxa1928_thermal_exit(void)
{
	platform_driver_unregister(&pxa1928_thermal_driver);
}

late_initcall(pxa1928_thermal_init);
//module_init(pxa1928_thermal_init);
//module_exit(pxa1928_thermal_exit);

MODULE_AUTHOR("Marvell Semiconductor");
MODULE_DESCRIPTION("PXA1928 SoC thermal driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa1928-thermal");
