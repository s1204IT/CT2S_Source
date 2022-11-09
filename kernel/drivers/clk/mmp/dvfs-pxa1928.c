/*
 *  linux/arch/arm/mach-mmp/dvfs-pxa1928.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk/dvfs.h>
#include <linux/clk/dvfs-dvc.h>
#include <linux/clk/mmp.h>
#include <linux/regs-addr.h>
#include <linux/mfd/88pm80x.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/cputype.h>
#include <trace/events/pxa.h>
#include "pxa1928_svc_data.h"

#define APMU_REG(x)			(apmu_base + (x))
#define APMU_DVC_APCORESS		APMU_REG(0x350)
/* bit definition of APMU_DVC_APSS */
#define APMU_DVC_APSS		APMU_REG(0x354)
#define VC_INT			(1 << 31)
#define VC_INT_MSK		(1 << 30)
#define VC_INT_CLR		(1 << 29)
#define VL_CHG_EN		(1 << 24)
#define VL_D2_SHIFT		20
#define VL_D1_SHIFT		16
#define VL_D0CG_SHIFT		8
#define VL_D0_SHIFT		0
#define VL_RW_MSK		0xF
/* end */

#define APMU_DVC_DEBUG1			APMU_REG(0x35C)
#define APMU_DVC_WAIT_CFG0		APMU_REG(0x364)
#define APMU_DVC_WAIT_CFG1		APMU_REG(0x368)
#define MSK_STBL_CNT			0x1F
#define STBL_CNT_SHIFT			0
#define APMU_DVC_CTRL			APMU_REG(0x360)
/* bit definition of APMU_DVC_CTRL */
#define EN_DDRDLL_UPD_REQ	(1 << 29)
#define MSK_VL_GC_AP		(1 << 28)
#define MSK_VL_CORE_AP		(1 << 27)
#define MSK_VL_MAIN_DFC		(1 << 26)
#define MSK_VL_MAIN_CP		(1 << 25)
#define MSK_VL_MAIN_AP		(1 << 24)
#define WAITCNT_SHIFT		16
#define WAITCNT_MSK		0xFF
#define DLL_UPD_EN_MSK		(0x7FFF)
/* end */

#define APMU_DVC_STATUS		APMU_REG(0x36C)
#define DVC_IN_PROGRESS		(1 << 31)

#define PMIC_RAMP_RATE		12500	/* uV/uS */
#define DVC_STB_UNIT		38	/* nS */
#define US_TO_NS		1000

#define PMIC_RAMPUP_TIME	16	/* uS */

static unsigned int mv_profile;
static void __iomem *apmu_base;
static unsigned int pxa1928_dvfs_en = 1;
#ifdef CONFIG_PXA1928_THERMAL
static unsigned int pxa1928_svc_thermal_en;
#endif
static unsigned int pxa1928_dvfs_delay_en = 1;
static int stepping_is_a0;
static unsigned long (*freqs_cmb_table)[][PXA1928_DVC_LEVEL_NUM + 1];
static int (*svc_volt_table)[][PXA1928_DVC_LEVEL_NUM];

/* notifier for hwdvc */
static ATOMIC_NOTIFIER_HEAD(dvc_notifier);
static void hwdvc_notifier_call_chain(enum hwdvc_rails rails,
		struct hwdvc_notifier_data *data);
/* cached current voltage lvl for each hwdvc rails */
int cached_cur_lvl[AP_COMP_MAX];

/*dvc apcore register */
union apmudvc_xp {
	struct {
		unsigned int mp0_vl:4;
		unsigned int rsved:4;
		unsigned int mp1_vl:4;
		unsigned int mp2l2on:4;
		unsigned int mp2l2off:4;
		unsigned int rsv:12;
	} b;
	unsigned int v;
};

/* hwdvc apsub register */
union apmudvc_apsub {
	struct {
		unsigned int d0_vl:4;
		unsigned int rsred0:4;
		unsigned int d0cg_vl:4;
		unsigned int rsred1:4;
		unsigned int d1_vl:4;
		unsigned int d2_en:4;
		unsigned int vl_chg_en:1;
		unsigned int rsred2:3;
		unsigned int vc_p:1;
		unsigned int vc_int_c:1;
		unsigned int vc_int_m:1;
		unsigned int vc_int:1;
	} b;
	unsigned int v;
};

#if 0
struct dvfs_limit {
	int vol;
	unsigned long clk;
};

static struct dvfs_limit pxa1928a0_dvfs_limit_table[PXA1928_DVC_FACTOR_NUM][PXA1928_DVC_LEVEL_NUM] = {
	[SDH1] = { {1200000, 104000000}, {} },
	[SDH2] = { {1200000, 104000000}, {} },
	[SDH3] = { {1100000, 104000000}, {} },
};

static struct dvfs_limit pxa1928b0_dvfs_limit_table[PXA1928_DVC_FACTOR_NUM][PXA1928_DVC_LEVEL_NUM] = {
	/* SDH1(sd)/SDH2(sdio) only use fakeclk-fixed for dvfs constraints */
	[SDH1] = { {1012500, 156000000}, {} },
	[SDH2] = { {1025000, 156000000}, {} },
	/*
	 * SDH3(emmc) only use fakeclk-tuned (dummy clock) for dvfs contraints for
	 * hs200 auto tuning but still need hold position here
	 */
	[SDH3] = { {950000, 156000000}, {} },
};
#endif

/***************
 * PXA1928_DVC_D0
 ***************/
static int pxa1928_dvc_set_d0_level(struct dvfs_rail *rail, int new_level);
static struct dvfs_rail pxa1928_dvfs_rail_d0 = {
	.reg_id = "dvc_d0",
	.max_millivolts = PXA1928_DVC_LEVEL_NUM - 1,
	.min_millivolts = 0,
	.nominal_millivolts = PXA1928_DVC_LEVEL_NUM - 1,
	.step = PXA1928_DVC_LEVEL_NUM,
	.update_inatomic = 1,
	.set_volt = pxa1928_dvc_set_d0_level,
};

static int pxa1928_dvc_set_d0_level(struct dvfs_rail *rail, int new_level)
{
	u32 apss;
	int i;
	struct hwdvc_notifier_data data;

	pr_debug("[DVC-D0] set to %d.\n", new_level);
	if (cached_cur_lvl[AP_ACTIVE] == new_level)
		return 0;

	trace_pxa_dvc_d0_chg(DVC_CHG_ENTRY, cached_cur_lvl[AP_ACTIVE], new_level);
	/* enable and clear irq */
	apss = readl(APMU_DVC_APSS);
	apss &= ~VC_INT_MSK;
	apss |= VC_INT_CLR;
	writel(apss, APMU_DVC_APSS);

	/* enable DVC */
	if (stepping_is_a0) {
		apss |= VL_CHG_EN;
		writel(apss, APMU_DVC_APSS);
	}

	/* set D0 level value */
	new_level = new_level & VL_RW_MSK;
	apss &= ~(VL_RW_MSK << VL_D0_SHIFT);
	apss |= new_level << VL_D0_SHIFT;
	writel(apss, APMU_DVC_APSS);


	/* wait for irq */
	for (i = 0; i < 1000000; i++) {
		if ((readl(APMU_DVC_APSS) & VC_INT) &&
			!(readl(APMU_DVC_STATUS) & DVC_IN_PROGRESS))
			break;
	}
	if (i == 1000000) {
		pr_err("no ack irq from dvc!\n");
		return -EINVAL;
	} else {
		/* clear irq bit */
		apss = readl(APMU_DVC_APSS);
		apss |= VC_INT_CLR;
		writel(apss, APMU_DVC_APSS);
		trace_pxa_dvc_d0_chg(DVC_CHG_EXIT, cached_cur_lvl[AP_ACTIVE], new_level);
		pr_debug("[DVC-D0] changing finished.\n");
		data.oldlv = cached_cur_lvl[AP_ACTIVE];
		data.newlv = new_level;
		hwdvc_notifier_call_chain(AP_ACTIVE, &data);
		cached_cur_lvl[AP_ACTIVE] = new_level;
		return 0;
	}

}


/***************
 * PXA1928_DVC_D0CG
 ***************/
static int pxa1928_dvc_set_d0cg_level(struct dvfs_rail *rail, int new_level);
static struct dvfs_rail pxa1928_dvfs_rail_d0cg = {
	.reg_id = "dvc_d0cg",
	.max_millivolts = PXA1928_DVC_LEVEL_NUM - 1,
	.min_millivolts = 0,
	.nominal_millivolts = PXA1928_DVC_LEVEL_NUM - 1,
	.step = PXA1928_DVC_LEVEL_NUM,
	.update_inatomic = 1,
	.set_volt = pxa1928_dvc_set_d0cg_level,
};

static int pxa1928_dvc_set_d0cg_level(struct dvfs_rail *rail, int new_level)
{
	u32 apss;
	struct hwdvc_notifier_data data;

	if (cached_cur_lvl[APSUB_IDLE] == new_level)
		return 0;

	pr_debug("[DVC-D0CG] set to %d.\n", new_level);
	trace_pxa_dvc_d0cg_chg(DVC_CHG_ENTRY, cached_cur_lvl[APSUB_IDLE], new_level);
	apss = readl(APMU_DVC_APSS);
	apss &= ~(VL_RW_MSK << VL_D0CG_SHIFT);
	apss |= new_level << VL_D0CG_SHIFT;
	writel(apss, APMU_DVC_APSS);
	trace_pxa_dvc_d0cg_chg(DVC_CHG_EXIT, cached_cur_lvl[APSUB_IDLE], new_level);
	pr_debug("[DVC-D0CG] changing finished.\n");

	data.oldlv = cached_cur_lvl[APSUB_IDLE];
	data.newlv = new_level;
	hwdvc_notifier_call_chain(APSUB_IDLE, &data);
	cached_cur_lvl[APSUB_IDLE] = new_level;

	return 0;
}

/***************
 * DVFS
 ***************/
static int pxa1928_dvfs_rail_register(struct dvfs_rail *rail, char *clk_name[],
			unsigned long (*freqs_cmb)[][PXA1928_DVC_LEVEL_NUM + 1],
			int factor_num)
{
	struct dvfs *vd = NULL;
	struct vol_table *vt = NULL;
	struct clk *c;
	unsigned long rate;
	int i, j;
	int ret = 0;

	for (i = 0; i < factor_num; i++) {
		/* factor is not enabled */
		if (!(*freqs_cmb)[i][0])
			continue;

		vd = kzalloc(sizeof(struct dvfs), GFP_KERNEL);
		if (!vd) {
			pr_err("DVFS: Failed to request dvfs struct!\n");
			ret = -ENOMEM;
			continue;
		}
		vd->num_freqs = PXA1928_DVC_LEVEL_NUM;

		vt = kzalloc(sizeof(struct vol_table) * PXA1928_DVC_LEVEL_NUM,
			     GFP_KERNEL);
		if (!vt) {
			pr_err("DVFS: Failed to request vol table\n");
			kzfree(vd);
			ret = -ENOMEM;
			continue;
		}

		for (j = 1; j < PXA1928_DVC_LEVEL_NUM + 1; j++) {
			vt[j - 1].freq = (*freqs_cmb)[i][j];
			vt[j - 1].millivolts = j - 1;
		}
		vd->vol_freq_table = vt;
		vd->clk_name = clk_name[i];
		vd->dvfs_rail = rail;

		c = clk_get_sys(NULL, vd->clk_name);
		if (IS_ERR_OR_NULL(c)) {
			pr_err("DVFS: no clk found:%s\n", vd->clk_name);
			kzfree(vd->vol_freq_table);
			kzfree(vd);
			ret = -EINVAL;
			continue;
		}
		ret = enable_dvfs_on_clk(c, vd);
		if (ret) {
			pr_err("DVFS: fail to enable dvfs on %s\n", c->name);
			kzfree(vd->vol_freq_table);
			kzfree(vd);
		}
		if (c->enable_count) {
			rate = clk_get_rate(c);
			for (j = 0; j < vd->num_freqs
				&& rate > vd->vol_freq_table[j].freq; j++)
				;
			vd->millivolts = vd->vol_freq_table[j].millivolts;
			vd->cur_rate = rate;
		}
	}
	pr_info("DVFS: rail %s has been registered.\n", rail->reg_id);
	return ret;
}

static void pmic_init_setup(int enable)
{
	int (*dvc_map)[][PXA1928_DVC_LEVEL_NUM];
	__maybe_unused int i;

	dvc_map = svc_volt_table;
#ifdef CONFIG_MFD_88PM80x_DVC
	if (enable) {
		for (i = 0; i < PXA1928_DVC_LEVEL_NUM; i++)
			pm8xx_dvc_setvolt(PM800_ID_BUCK1A, i, (*dvc_map)[mv_profile][i]);
	} else {
		for (i = 0; i < PXA1928_DVC_LEVEL_NUM; i++)
			pm8xx_dvc_setvolt(PM800_ID_BUCK1A, i,
				(*dvc_map)[mv_profile][PXA1928_DVC_LEVEL_NUM - 1]);
	}
#endif
}


#ifdef CONFIG_PXA1928_THERMAL
#define SVC_THERMAL_GAP_HIGH		37500
#define SVC_THERMAL_GAP_MID		25000
#define SVC_THERMAL_GAP_LOW		12500

#define VOL_CUTOFF_VAL_LOW		987500
#define VOL_CUTOFF_VAL_MID1		1025000
#define VOL_CUTOFF_VAL_MID2		1037500
#define VOL_CUTOFF_VAL_HIGH		1050000

int get_svc_thermal_gap(int volt)
{
	int gap;

	if (volt <= VOL_CUTOFF_VAL_LOW)
		gap = (int)SVC_THERMAL_GAP_HIGH;
	else if (volt <= VOL_CUTOFF_VAL_MID1)
		gap = (int)SVC_THERMAL_GAP_MID;
	else if ((volt >= VOL_CUTOFF_VAL_MID2) && (volt <= VOL_CUTOFF_VAL_HIGH))
		gap = (int)SVC_THERMAL_GAP_LOW;
	else
		gap = 0;

	return gap;
}

void pmic_reconfigure(int direction)
{
	int gap;
	int i;
	int voltage;
	int (*dvc_map)[][PXA1928_DVC_LEVEL_NUM];

#ifdef CONFIG_MFD_88PM80x_DVC
	if (pxa1928_svc_thermal_en) {
		dvc_map = svc_volt_table;
		if (direction) {
			for (i = 0; i < PXA1928_DVC_LEVEL_NUM; i++)
				pm8xx_dvc_setvolt(PM800_ID_BUCK1A, i, (*dvc_map)[mv_profile][i]);
		} else {
			for (i = 0; i < PXA1928_DVC_LEVEL_NUM; i++) {
				gap = get_svc_thermal_gap((*dvc_map)[mv_profile][i]);
				voltage = (*dvc_map)[mv_profile][i] - gap;
				pm8xx_dvc_setvolt(PM800_ID_BUCK1A, i, voltage);
			}
		}
	}
#endif
}
EXPORT_SYMBOL(pmic_reconfigure);

#if 0
int dvfs_thermal_update_level(int level)
{
	int gap, i;
	int level_updated = level;
	int (*dvc_map)[][PXA1928_DVC_LEVEL_NUM];

	dvc_map = svc_volt_table;
	gap = get_svc_thermal_gap((*dvc_map)[mv_profile][level]);

	if (gap == 0)
		return level;

	for (i = PXA1928_DVC_LEVEL_NUM - 1; i >= 0; i--) {
		if ((*dvc_map)[mv_profile][i] <= ((*dvc_map)[mv_profile][level] - gap)) {
			level_updated = i;
			break;
		}
	}
	return level_updated;
}
EXPORT_SYMBOL(dvfs_thermal_update_level);
#endif

static ssize_t pxa1928_svc_thermal_en_read(struct file *file, char __user *buffer,
					size_t count, loff_t *ppos)
{
	int len = 0;
	int size = PAGE_SIZE - 1;
	int ret;
	char *buf;

	buf = (char *)__get_free_pages(GFP_NOIO, 0);
	if (!buf)
		return -ENOMEM;

	len += snprintf(buf, size, "%d\n", pxa1928_svc_thermal_en);
	ret = simple_read_from_buffer(buffer, count, ppos, buf, len);
	free_pages((unsigned long)buf, 0);
	return ret;
}

static ssize_t pxa1928_svc_thermal_en_write(struct file *file,
					const char __user *buff,
					size_t len, loff_t *ppos)
{
	char messages[20];
	int val;

	memset(messages, '\0', 20);
	if (copy_from_user(messages, buff, len))
		return -EFAULT;

	if (kstrtoint(messages, 10, &val) < 0)
		return -EINVAL;

	if ((val != 1) && (val != 0))
		return -EINVAL;

	if (val != pxa1928_svc_thermal_en) {
		pxa1928_svc_thermal_en = val;
		if (0 == pxa1928_svc_thermal_en)
			pmic_init_setup(pxa1928_dvfs_en);
		else if (1 == pxa1928_svc_thermal_en)
			svc_thermal_enable();
	}
	return len;
}

static const struct file_operations svc_thermal_en_ops = {
	.read	= pxa1928_svc_thermal_en_read,
	.write	= pxa1928_svc_thermal_en_write,
};

static inline void pxa1928_svc_thermal_debugfs_init(void)
{
	struct dentry *svc_thermal_en;

	svc_thermal_en = debugfs_create_file("svc_thermal_enable", S_IRUGO | S_IFREG,
				NULL, NULL, &svc_thermal_en_ops);
	if (!svc_thermal_en || (svc_thermal_en == ERR_PTR(-ENODEV)))
		pr_err("SVC_THERMAL_EN: create debugfs failed.");
}
#endif

#define WAITCNT_RESOL_STEP	2000	/* ns */
static void pxa1928_dvfs_setup_par(void)
{
	u32 val, waitcnt_resol, stbl_cnt;
	int i;
	unsigned int vol_gap = 0;
	int (*dvc_map)[][PXA1928_DVC_LEVEL_NUM];
	int waitcnt_gap[PXA1928_DVC_LEVEL_NUM];
	struct hwdvc_notifier_data data;

	dvc_map = svc_volt_table;

	waitcnt_resol =  WAITCNT_RESOL_STEP / DVC_STB_UNIT;
	for (i = 0; i < PXA1928_DVC_LEVEL_NUM - 1; i++) {
		vol_gap = (*dvc_map)[mv_profile][i + 1] - (*dvc_map)[mv_profile][i];
		if (stepping_is_a0 && pxa1928_dvfs_delay_en)
			waitcnt_gap[i] = ((PMIC_RAMPUP_TIME + vol_gap/PMIC_RAMP_RATE) * US_TO_NS) /
				WAITCNT_RESOL_STEP + 1;
		else
			waitcnt_gap[i] = ((vol_gap / PMIC_RAMP_RATE) * US_TO_NS) /
				WAITCNT_RESOL_STEP + 1;
	}

	/*
	 * configure PMUA_DVC_WAIT_CFG1, when dvc change covers multiple intervals,
	 * the wait time is the cumulative of wait times for all intervals between
	 * the old dvc level and new dvc level
	 */
	val = 0;
	for (i = 0; i < PXA1928_DVC_LEVEL_NUM / 2; i++)
		val |= waitcnt_gap[i] << (4 * i);
	writel(val, APMU_DVC_WAIT_CFG0);

	val = 0;
	for (i = 0; i < (PXA1928_DVC_LEVEL_NUM / 2 - 1); i++)
		val |= waitcnt_gap[8+i] << (4 * i);
	writel(val, APMU_DVC_WAIT_CFG1);

	/* cofigure APMU_DVC_DEBUG1, it is for stabilization time for DVC change */
	if (!stepping_is_a0) {
		val = readl(APMU_DVC_DEBUG1);
		val &= ~MSK_STBL_CNT;
		stbl_cnt = (PMIC_RAMPUP_TIME * US_TO_NS) / WAITCNT_RESOL_STEP;
		val |= stbl_cnt << STBL_CNT_SHIFT;
		writel(val, APMU_DVC_DEBUG1);
	}

	/* config APMU_DVC_CTRL */
	val = readl(APMU_DVC_CTRL);
	val &= ~MSK_VL_MAIN_AP;
	val &= ~MSK_VL_MAIN_CP;
	val &= ~MSK_VL_MAIN_DFC;
	val &= ~(WAITCNT_MSK << WAITCNT_SHIFT);
	val |= waitcnt_resol << WAITCNT_SHIFT;
	val &= ~DLL_UPD_EN_MSK;
	writel(val, APMU_DVC_CTRL);

	/* enable and clear irq */
	val = readl(APMU_DVC_APSS);
	val &= ~VC_INT_MSK;
	val |= VC_INT_CLR;
	writel(val, APMU_DVC_APSS);

	/* enable DVC */
	if (stepping_is_a0) {
		val |= VL_CHG_EN;
		writel(val, APMU_DVC_APSS);
	}

	val = readl(APMU_DVC_APSS);
	/* D2 */
	val &= ~(VL_RW_MSK << VL_D2_SHIFT);
	val |= (0 << VL_D2_SHIFT);
	/* D1 */
	val &= ~(VL_RW_MSK << VL_D1_SHIFT);
	val |= (0 << VL_D1_SHIFT);
	/* D0CG */
	val &= ~(VL_RW_MSK << VL_D0CG_SHIFT);
	val |= ((PXA1928_DVC_LEVEL_NUM - 1) << VL_D0CG_SHIFT);
	/* D0 */
	val &= ~(VL_RW_MSK << VL_D0_SHIFT);
	val |= ((PXA1928_DVC_LEVEL_NUM - 1) << VL_D0_SHIFT);
	writel(val, APMU_DVC_APSS);

	writel(0x0, APMU_DVC_APCORESS);

	/* wait for irq */
	for (i = 0; i < 1000000; i++) {
		if ((readl(APMU_DVC_APSS) & VC_INT) &&
			!(readl(APMU_DVC_STATUS) & DVC_IN_PROGRESS))
			break;
	}
	if (i == 1000000)
		pr_err("no ack irq from dvc!\n");
	else {
		/* clear irq bit */
		val = readl(APMU_DVC_APSS);
		val |= VC_INT_CLR;
		writel(val, APMU_DVC_APSS);
	}

	data.oldlv = data.newlv = VL0;
	hwdvc_notifier_call_chain(APSUB_IDLE, &data);
}

static ssize_t pxa1928_dvfs_en_read(struct file *file, char __user *buffer,
					size_t count, loff_t *ppos)
{
	int len = 0;
	int size = PAGE_SIZE - 1;
	int ret;
	char *buf;

	buf = (char *)__get_free_pages(GFP_NOIO, 0);
	if (!buf)
		return -ENOMEM;

	len += snprintf(buf, size, "%d\n", pxa1928_dvfs_en);
	ret = simple_read_from_buffer(buffer, count, ppos, buf, len);
	free_pages((unsigned long)buf, 0);
	return ret;
}

static ssize_t pxa1928_dvfs_en_write(struct file *file,
					const char __user *buff,
					size_t len, loff_t *ppos)
{
	char messages[20];
	int val;

	memset(messages, '\0', 20);
	if (copy_from_user(messages, buff, len))
		return -EFAULT;

	if (kstrtoint(messages, 10, &val) < 0)
		return -EINVAL;

	if ((val != 1) && (val != 0))
		return -EINVAL;

	if (val != pxa1928_dvfs_en) {
		pxa1928_dvfs_en = val;
		pmic_init_setup(pxa1928_dvfs_en);
	}
	return len;
}

static const struct file_operations dvfs_en_ops = {
	.read		= pxa1928_dvfs_en_read,
	.write		= pxa1928_dvfs_en_write,
};

static inline void pxa1928_dvfs_debugfs_init(void)
{
	struct dentry *dvfs_en;

	dvfs_en = debugfs_create_file("dvfs_enable", S_IRUGO | S_IFREG,
				NULL, NULL, &dvfs_en_ops);
	if (!dvfs_en || (dvfs_en == ERR_PTR(-ENODEV)))
		pr_err("DVFS: create debugfs failed.");
}

static struct dvfs_rail *pxa1928_dvfs_rails[] = {
	&pxa1928_dvfs_rail_d0,
	&pxa1928_dvfs_rail_d0cg,
};

static void read_mv_profile(void)
{
	struct device_node *node;
	int pro;
	static int first_call;

	node = of_find_compatible_node(NULL, NULL, "marvell,profile");
	if (node)
		of_property_read_u32(node, "marvell,profile-number", &pro);
	if (pro > 15 || pro < 0)
		pr_err("Wrong profile setting! Use 0 as default.\n");
	else
		mv_profile = pro;

	if (!(first_call++))
		pr_info("PXA1928 SoC profile number: %d\n", mv_profile);
}

/* notifier for hwdvc */
int hwdvc_notify_register(struct notifier_block *n)
{
	return atomic_notifier_chain_register(&dvc_notifier, n);
}
EXPORT_SYMBOL(hwdvc_notify_register);

int hwdvc_notify_unregister(struct notifier_block *n)
{
	return atomic_notifier_chain_unregister(&dvc_notifier, n);
}
EXPORT_SYMBOL(hwdvc_notify_unregister);

static void hwdvc_notifier_call_chain(enum hwdvc_rails rails,
	struct hwdvc_notifier_data *data)
{
	atomic_notifier_call_chain(&dvc_notifier, rails, data);
}

int __init pxa1928_setup_dvfs(void)
{
	struct device_node *node;
	union apmudvc_xp pmudvc_ap;
	union apmudvc_apsub pmudvc_apsub;
	u32 vl_min = 0;
	int factor = 0, level = 0;
#if 0
	int limit = 0;
	struct dvfs_limit (*pxa1928_dvfs_limit_table)[][PXA1928_DVC_LEVEL_NUM];
#endif
	if (!cpu_is_pxa1928())
		return 0;

	stepping_is_a0 = pxa1928_is_a0();
#ifdef CONFIG_PXA_DEBUG_MAX_VOLTAGE
	pxa1928_dvfs_en = 0;
#endif

	node = of_find_compatible_node(NULL, NULL, "mrvl,mmp-pmu-apmu");
	if (node)
		apmu_base = of_iomap(node, 0);
	if (unlikely(!apmu_base)) {
		pr_err("DVFS: Invalid APMU base\n");
		return -EINVAL;
	}

	read_mv_profile();
	freqs_cmb_table = stepping_is_a0 ? &freqs_cmb_1928a0[mv_profile] :
			(cpu_is_pxa1928_pop() ? &freqs_cmb_1928b0_pop[mv_profile] :
			 (cpu_is_pxa1926_2l_discrete() ? &freqs_cmb_1926b0_2l_discrete[mv_profile] :
					&freqs_cmb_1928b0_4l_discrete[mv_profile]));

	svc_volt_table = stepping_is_a0 ? &svc_volt_1928a0 :
			(cpu_is_pxa1928_pop() ? &svc_volt_1928b0_pop :
			 (cpu_is_pxa1926_2l_discrete() ? &svc_volt_1926b0_2l_discrete :
					&svc_volt_1928b0_4l_discrete));

	pxa1928_dvfs_setup_par();
	pmic_init_setup(pxa1928_dvfs_en);

	dvfs_init_rails(pxa1928_dvfs_rails, ARRAY_SIZE(pxa1928_dvfs_rails));

	/* Fill in SDH dummy dvfs constraints */
	for (factor = 0; factor < PXA1928_DVC_FACTOR_NUM; factor++) {
		if ((factor != SDH3) && (factor != SDH2) && (factor != SDH1))
			continue;
		(*freqs_cmb_table)[factor][0] = 1;
		for (level = 0; level < PXA1928_DVC_LEVEL_NUM; level++) {
			/* SDH use dvfs dummy clock for auto tuning */
			(*freqs_cmb_table)[factor][level + 1] = DUMMY_VL_TO_HZ(level);
		}
	}
#if 0
	pxa1928_dvfs_limit_table = stepping_is_a0 ? &pxa1928a0_dvfs_limit_table :
			&pxa1928b0_dvfs_limit_table;

	/* Fill in dvfs constraints according to limit table if any */
	for (factor = 0; factor < PXA1928_DVC_FACTOR_NUM; factor++) {
		for (limit = 0; limit < PXA1928_DVC_LEVEL_NUM; limit++) {
			if (!(*pxa1928_dvfs_limit_table)[factor][limit].clk || !(*pxa1928_dvfs_limit_table)[factor][limit].vol)
				break;
			(*freqs_cmb_table)[factor][0] = 1;
			for (level = 0; level < PXA1928_DVC_LEVEL_NUM; level++) {
				if ((*svc_volt_table)[mv_profile][level] >= (*pxa1928_dvfs_limit_table)[factor][limit].vol)
					(*freqs_cmb_table)[factor][level + 1]  = (*pxa1928_dvfs_limit_table)[factor][limit].clk;
			}
		}

	}
#endif
	for (factor = 0; factor < PXA1928_DVC_FACTOR_NUM; factor++) {
		for (level = 0; level < PXA1928_DVC_LEVEL_NUM + 1; level++)
			pr_debug("%s: freqs_cmb_1928b0[%d][%d][%d] = %lu \n",
					__func__, mv_profile, factor, level, (*freqs_cmb_table)[factor][level]);
	}

	for (factor = 0; factor < PXA1928_DVC_FACTOR_NUM; factor++) {
		if (!(*freqs_cmb_table)[factor][0])
			continue;
		if ((factor != CORE) && (factor != AXI) && (factor != DDR))
			continue;
		for (level = 0; level < PXA1928_DVC_LEVEL_NUM; level++) {
			if ((*freqs_cmb_table)[factor][level+1]) {
				if (level > vl_min)
					vl_min = level;
				break;
			}
		}
	}
	plat_set_vl_min(vl_min);
	plat_set_vl_max(PXA1928_DVC_LEVEL_NUM);
	pr_debug("%s: vl_min = %u, vl_max = %u\n", __func__, vl_min, PXA1928_DVC_LEVEL_NUM - 1);

	pxa1928_dvfs_rail_register(&pxa1928_dvfs_rail_d0,
			pxa1928_dvfs_clk_name,
			freqs_cmb_table, PXA1928_DVC_FACTOR_NUM);

	/* for D0CG, just disable CORE&AXI factor in pxa1928_dvfs_freqs_cmb_table table */
	(*freqs_cmb_table)[CORE][0] = 0;
	(*freqs_cmb_table)[AXI][0] = 0;
	/* GC2D cannot allow system entering D0CG on pxa1928 A stepping */
	if (stepping_is_a0)
		(*freqs_cmb_table)[GC2D][0] = 0;
	/* SDH1/2/3 won't let system enter D0CG during working */
	(*freqs_cmb_table)[SDH1][0] = 0;
	(*freqs_cmb_table)[SDH2][0] = 0;
	(*freqs_cmb_table)[SDH3][0] = 0;

	pxa1928_dvfs_rail_register(&pxa1928_dvfs_rail_d0cg,
			pxa1928_dvfs_clk_name,
			freqs_cmb_table, PXA1928_DVC_FACTOR_NUM);
	/* Get current level information */
	pmudvc_ap.v = __raw_readl(APMU_DVC_APCORESS);
	pmudvc_apsub.v = __raw_readl(APMU_DVC_APSS);
	cached_cur_lvl[AP_ACTIVE] = pmudvc_apsub.b.d0_vl;
	cached_cur_lvl[APSUB_IDLE] = pmudvc_apsub.b.d0cg_vl;


	pxa1928_dvfs_debugfs_init();
#ifdef CONFIG_PXA1928_THERMAL
	pxa1928_svc_thermal_debugfs_init();
#endif

	return 0;
}
subsys_initcall_sync(pxa1928_setup_dvfs);

long get_dvc_freq(unsigned int factor, unsigned int level)
{
	unsigned long (*freqs_cmb_tbl)[][PXA1928_DVC_LEVEL_NUM + 1];

	read_mv_profile();
	freqs_cmb_tbl = stepping_is_a0 ? &freqs_cmb_1928a0[mv_profile] :
			(cpu_is_pxa1928_pop() ? &freqs_cmb_1928b0_pop[mv_profile] :
			 (cpu_is_pxa1926_2l_discrete() ? &freqs_cmb_1926b0_2l_discrete[mv_profile] :
					&freqs_cmb_1928b0_4l_discrete[mv_profile]));
	if ((factor < PXA1928_DVC_FACTOR_NUM) && (level <= PXA1928_DVC_LEVEL_NUM))
		return (long)(*freqs_cmb_tbl)[factor][level + 1];
	else
		return -EINVAL;
}
EXPORT_SYMBOL(get_dvc_freq);

int ddr_get_dvc_level(int rate)
{
	unsigned long (*freqs_cmb_tbl)[][PXA1928_DVC_LEVEL_NUM + 1];
	int idx;

	read_mv_profile();
	freqs_cmb_tbl = stepping_is_a0 ? &freqs_cmb_1928a0[mv_profile] :
			(cpu_is_pxa1928_pop() ? &freqs_cmb_1928b0_pop[mv_profile] :
			 (cpu_is_pxa1926_2l_discrete() ? &freqs_cmb_1926b0_2l_discrete[mv_profile] :
					&freqs_cmb_1928b0_4l_discrete[mv_profile]));

	for (idx = 1; idx < PXA1928_DVC_LEVEL_NUM + 1; idx++) {
		if ((*freqs_cmb_tbl)[DDR][idx] >= rate)
			return idx - 1;
	}
	return PXA1928_DVC_LEVEL_NUM - 1;
}
EXPORT_SYMBOL(ddr_get_dvc_level);

int get_voltage_table_for_cp(unsigned int *dvc_voltage_tbl, unsigned int *level_num)
{
	int i = 0;
	int (*dvc_map)[][PXA1928_DVC_LEVEL_NUM];

	read_mv_profile();

	*level_num = PXA1928_DVC_LEVEL_NUM;
	svc_volt_table = stepping_is_a0 ? &svc_volt_1928a0 :
			(cpu_is_pxa1928_pop() ? &svc_volt_1928b0_pop :
			 (cpu_is_pxa1926_2l_discrete() ? &svc_volt_1926b0_2l_discrete :
					&svc_volt_1928b0_4l_discrete));


	dvc_map = svc_volt_table;
	for (i = 0; i < *level_num; i++)
		dvc_voltage_tbl[i] = (*dvc_map)[mv_profile][i];

	return 0;
}
EXPORT_SYMBOL(get_voltage_table_for_cp);

int get_svc_freqs_cmb_tbl(unsigned long (**svc_tbl)[][PXA1928_DVC_LEVEL_NUM + 1])
{
	read_mv_profile();
	*svc_tbl = stepping_is_a0 ? &freqs_cmb_1928a0[mv_profile] :
			(cpu_is_pxa1928_pop() ? &freqs_cmb_1928b0_pop[mv_profile] :
			 (cpu_is_pxa1926_2l_discrete() ? &freqs_cmb_1926b0_2l_discrete[mv_profile] :
					&freqs_cmb_1928b0_4l_discrete[mv_profile]));

	return 0;
}
EXPORT_SYMBOL(get_svc_freqs_cmb_tbl);

static int __init nodvfs_set(char *str)
{
	pxa1928_dvfs_en = 0;
	return 1;
}
__setup("nodvfs", nodvfs_set);

static int __init nodvfs_delay_set(char *str)
{
	pxa1928_dvfs_delay_en = 0;
	return 1;
}
__setup("nodvfs_delay", nodvfs_delay_set);
