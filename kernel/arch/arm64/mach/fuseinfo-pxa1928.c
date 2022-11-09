/*
 *  linux/arch/arm64/mach/fuseinfo-pxa1928.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/debugfs.h>
#include "fuseinfo-pxa1928.h"

struct fuse_info {
	unsigned int fab;
	unsigned int lvtdro;
	unsigned int svtdro;
	unsigned int profile;
	unsigned int iddq_1050;
	unsigned int iddq_1030;
};
static struct fuse_info eden_fuseinfo;

struct fuse_reg_info {
	u32 arg0;	/* reg: 0xD42928a0 */
	u32 arg1;	/* reg: 0xD42929a4 */
	u32 arg2;	/* reg: 0xD42929a8 */
	u32 arg3;	/* reg: 0xD42929ac */
	u32 arg4;	/* reg: 0xD42929b0 */
	u32 arg5;	/* reg: 0xD42929b4 */
};


static int fuse_profile_mapping_table[] = {
	-1,	0x0003, 0x0007, 0x000f,
	0x001f, 0x003f, 0x007f, 0x00ff,
	0x01ff, 0x03ff, 0x07ff, 0x0fff,
	0x1fff, 0x3fff, 0x7fff, 0xffff,
};

int get_profile_from_fusesetting(struct fuse_reg_info *arg)
{
	int i, val;
	int fuse_profile;

	val = arg->arg0 & 0xffff;
	for (i = 0; i < 16; i++)
		if (val == fuse_profile_mapping_table[i]) {
			fuse_profile = i;
			return fuse_profile;
		}
	pr_info("can't map to any valid fuse setting, assume it is profile 0\n");
	fuse_profile = 0;
	return fuse_profile;
}


int get_lot_from_fusesetting(struct fuse_reg_info *arg, char *clot)
{
	unsigned int temp, lot, i = 0;
	u32 fuseblk15_0_31;
	u32 fuseblk15_32_63;
	fuseblk15_0_31 = arg->arg1;
	fuseblk15_32_63 = arg->arg2;

	lot = ((fuseblk15_32_63 & 0xfffff) << 11) |
		((fuseblk15_0_31 >> 21) & 0x7ff);

	temp = (lot >> 26) & 0x1f;
	clot[i++] = (char)(temp+64);

	temp = (lot >> 20) & 0x3f;
	if (temp & 0x20) {
		temp &= ~0x20;
		clot[i++] = (char)(temp+64);
	} else {
		clot[i++] = (char)(temp+(int)('0'));
	}

	temp = (lot >> 14) & 0x3f;
	if (temp & 0x20) {
		temp &= ~0x20;
		clot[i++] = (char)(temp+64);
	} else {
		clot[i++] = (char)(temp+(int)('0'));
	}

	temp = (lot >>  8) & 0x3f;
	if (temp & 0x20) {
		temp &= ~0x20;
		clot[i++] = (char)(temp+64); }
	else {
		clot[i++] = (char)(temp+(int)('0'));
	}
	temp = (lot >>  4) & 0xf;
	clot[i++] = (char)(temp+(int)('0'));
	temp = lot  & 0xf;
	clot[i++] = (char)(temp+(int)('0'));
	return 0;
}


void read_fuseinfo(void)
{
	struct fuse_reg_info arg;
	/* ULT: read, print and store off statistics */
	u32 fuseblk15_0_31	= 0x632c1546;
	u32 fuseblk15_32_63	= 0x560a0d10;
	u32 fuseblk15_64_95	= 0x5bd141e8;
	u32 fuseblk15_127_96	= 0x1e433294;
	u32 ult_rev, x, y, wafer, lot, ww, year, parity;
	u32 flag, dro_avg, svtdro_avg, nlvtdro_avg, lvtdro_avg;
	u32 nsvtdro_avg, sidd1p05, sidd1p30;
	char clot[7] = {0};

	smc_get_fuse_info(0XC2004000, (void *)&arg);

	fuseblk15_0_31	= arg.arg1;
	fuseblk15_32_63	= arg.arg2;
	fuseblk15_64_95	= arg.arg3;
	fuseblk15_127_96	= arg.arg4;

	ult_rev = fuseblk15_0_31 & 0x3;
	x = (fuseblk15_0_31 >>  2) & 0x7f;
	y = (fuseblk15_0_31 >>  9) & 0x7f;
	wafer = (fuseblk15_0_31 >> 16) & 0x1f;
	lot = ((fuseblk15_32_63 & 0xfffff) << 11) |
		((fuseblk15_0_31 >> 21) & 0x7ff);
	ww = (fuseblk15_32_63 >> 20) & 0x3f;
	year = (fuseblk15_32_63 >> 26) & 0x7;
	parity = (fuseblk15_32_63 >> 29) & 0x1;
	flag = (fuseblk15_32_63 >> 30) & 0x1;

	lvtdro_avg = (fuseblk15_64_95) & 0x7ff;
	svtdro_avg = (fuseblk15_127_96 >> 1) & 0x7ff;
	nlvtdro_avg = (fuseblk15_64_95 >> 11) & 0x7ff;
	nsvtdro_avg = ((fuseblk15_127_96 & 0x1) << 10)
		| ((fuseblk15_64_95 >> 22) & 0x3ff);
	dro_avg = svtdro_avg;

	sidd1p05 = (fuseblk15_127_96 >>  12) & 0x3ff;
	sidd1p30 = (fuseblk15_127_96 >>  22) & 0x3ff;

	eden_fuseinfo.fab = 0;
	eden_fuseinfo.lvtdro = lvtdro_avg;
	eden_fuseinfo.svtdro = svtdro_avg;
	eden_fuseinfo.iddq_1050 = sidd1p05;
	eden_fuseinfo.iddq_1030 = sidd1p30;


	get_lot_from_fusesetting(&arg, clot);
	eden_fuseinfo.profile = get_profile_from_fusesetting(&arg);
	pr_info("***************************\n");
	pr_info("*  ULT: %08X%08X  *\n", fuseblk15_32_63,
			fuseblk15_0_31);
	pr_info("***************************\n");
	pr_info("*  Lot   = %s_%d_%d (FabParentLot_Year_WW)\n", clot,
			year+2009, ww);
	pr_info("*  wafer = %d\n",    wafer);
	pr_info("*  x     = %d\n",        x);
	pr_info("*  y     = %d\n",        y);
	pr_info("***************************\n");
	pr_info("*  Iddq @ 1.05V = %dmA\n",   sidd1p05);
	pr_info("*  Iddq @ 1.30V = %dmA\n",   sidd1p30);
	pr_info("***************************\n");
	pr_info("*  DRO_LVT_AVG  [106: 96] = %d\n", lvtdro_avg);
	pr_info("*  DRO_NLVT_AVG [117:107] = %d\n", nlvtdro_avg);
	pr_info("*  DRO_NSVT_AVG [128:118] = %d\n", nsvtdro_avg);
	pr_info("*  DRO_SVT_AVG  [139:129] = %d (used for DRO_AVG)\n",
			svtdro_avg);
	pr_info("*  DRO_AVG                = %d\n", dro_avg);
	pr_info("***************************\n");
	pr_info("***************************\n");
	pr_info("*  profile: %d\n", eden_fuseinfo.profile);
	pr_info("***************************\n");
	return;
}

unsigned int get_chipprofile(void)
{
	return eden_fuseinfo.profile;
}
EXPORT_SYMBOL(get_chipprofile);

unsigned int get_iddq_105(void)
{
	return eden_fuseinfo.iddq_1050;
}
EXPORT_SYMBOL(get_iddq_105);

unsigned int get_iddq_130(void)
{
	return eden_fuseinfo.iddq_1030;
}
EXPORT_SYMBOL(get_iddq_130);
static ssize_t fab_fuse_read(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	char val[20];
	sprintf(val, "%d\n", eden_fuseinfo.fab);
	return simple_read_from_buffer(user_buf, count, ppos, val,
			strlen(val));
}
static const struct file_operations fab_fuse_ops = {
	.read		= fab_fuse_read,
};

static ssize_t svtdro_fuse_read(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	char val[20];
	sprintf(val, "%d\n", eden_fuseinfo.svtdro);
	return simple_read_from_buffer(user_buf, count, ppos, val,
			strlen(val));
}
static const struct file_operations svtdro_fuse_ops = {
	.read		= svtdro_fuse_read,
};

static ssize_t lvtdro_fuse_read(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	char val[20];
	sprintf(val, "%d\n", eden_fuseinfo.lvtdro);
	return simple_read_from_buffer(user_buf, count, ppos,
			val, strlen(val));
}
static const struct file_operations lvtdro_fuse_ops = {
	.read		= lvtdro_fuse_read,
};

static ssize_t profile_fuse_read(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	char val[20];
	sprintf(val, "%d\n", eden_fuseinfo.profile);
	return simple_read_from_buffer(user_buf, count, ppos, val,
			strlen(val));
}
static const struct file_operations profile_fuse_ops = {
	.read		= profile_fuse_read,
};

static ssize_t iddq_1050_fuse_read(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	char val[20];
	sprintf(val, "%d\n", eden_fuseinfo.iddq_1050);
	return simple_read_from_buffer(user_buf, count, ppos, val,
			strlen(val));
}
static const struct file_operations iddq_1050_fuse_ops = {
	.read		= iddq_1050_fuse_read,
};


static struct dentry *fuse;
static int __init __init_fuse_debugfs_node(void)
{
	struct dentry *lvtdro_fuse = NULL, *svtdro_fuse = NULL;
	struct dentry *fab_fuse = NULL;
	struct dentry *profile_fuse = NULL;
	struct dentry *iddq_1050_fuse = NULL;

	fuse = debugfs_create_dir("fuse", NULL);
	if (!fuse)
		return -ENOENT;

	fab_fuse = debugfs_create_file("fab", S_IRUGO, fuse, NULL,
			&fab_fuse_ops);
	if (!fab_fuse)
		goto err_fab_fuse;

	svtdro_fuse = debugfs_create_file("svtdro", S_IRUGO, fuse, NULL,
			&svtdro_fuse_ops);
	if (!svtdro_fuse)
		goto err_svtdro_fuse;

	lvtdro_fuse = debugfs_create_file("lvtdro", S_IRUGO, fuse, NULL,
			&lvtdro_fuse_ops);
	if (!lvtdro_fuse)
		goto err_lvtdro_fuse;

	profile_fuse = debugfs_create_file("profile", S_IRUGO, fuse, NULL,
			&profile_fuse_ops);
	if (!profile_fuse)
		goto err_profile_fuse;

	iddq_1050_fuse = debugfs_create_file("iddq1050", S_IRUGO, fuse,
			NULL, &iddq_1050_fuse_ops);
	if (!iddq_1050_fuse)
		goto err_iddq_1050_fuse;

	return 0;

err_iddq_1050_fuse:
	debugfs_remove(profile_fuse);
err_profile_fuse:
	debugfs_remove(lvtdro_fuse);
err_lvtdro_fuse:
	debugfs_remove(svtdro_fuse);
err_svtdro_fuse:
	debugfs_remove(fab_fuse);
err_fab_fuse:
	debugfs_remove(fuse);
	pr_err("debugfs entry created failed in %s\n", __func__);
	return -ENOENT;

}

arch_initcall(__init_fuse_debugfs_node);
