/*
 * MFP Init configuration driver for MMP related platform.
 *
 * This driver is aims to do the initialization on the MFP configuration
 * which is not handled by the component driver.
 * The driver user is required to add your "mfp init deivce" in your used
 * DTS file, whose complatiable is "mrvl,mmp-mfp", in which you can implement
 * pinctrl setting.
 *
 * Copyright:   (C) 2013 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <linux/io.h>
#include <linux/regs-addr.h>
#include <linux/slab.h>

#define MAX_STATE 8
#define MAX_STR_LEN 25

struct pinfunc_data {
	char pin_func_table[MAX_STATE][MAX_STR_LEN];
	char spec_name[MAX_STR_LEN];
	u32 pinfunc_mask;
} *mfp_dump_data;

static int pins_functbl_init(u32 tbl_length, struct device_node *node)
{
	int off = 0;
	struct pinfunc_data *data;
	struct device_node *pin_node;
	char str[MAX_STR_LEN];

	mfp_dump_data = kzalloc(sizeof(struct pinfunc_data) * (tbl_length / 4),
				GFP_ATOMIC);

	if (!mfp_dump_data)
		return -EINVAL;

	data = mfp_dump_data;

	for (off = 0; off < tbl_length; off += 0x4) {
		int tbl_idx = 0, list_idx = 0, bit = -EINVAL;

		snprintf(str, ARRAY_SIZE(str), "0x%x", off);
		pin_node = of_find_compatible_node(node, NULL, str);

		if (pin_node) {
			unsigned long mask;
			const char *spec_name = NULL;

			BUG_ON(of_property_read_u32(pin_node,
				"pin-mask", &(data->pinfunc_mask)));
			mask = data->pinfunc_mask;
			if (mask != 0)
				bit = find_first_bit(&(mask), 32);

			if (of_property_read_string(pin_node,
				"spec-name", &(spec_name))) {
				snprintf(str, ARRAY_SIZE(str),
					"MFP_%03d", (off / 4));
				strcpy(data->spec_name, str);
			} else
				strcpy(data->spec_name, (char *)spec_name);
		} else {
			snprintf(str, ARRAY_SIZE(str), "MFP_%03d", (off / 4));
			strcpy(data->spec_name, str);
		}

		while (1) {
			const char *func_name = NULL;

			BUG_ON(bit >= MAX_STATE);
			if (tbl_idx >= MAX_STATE)
				break;
			if (bit == tbl_idx) {
				unsigned long mask = data->pinfunc_mask;
				of_property_read_string_index(pin_node,
						"pin-func-list",
						list_idx, &func_name);
				if (func_name) {
					strcpy(data->pin_func_table[tbl_idx],
							(char *)func_name);
					bit = find_next_bit(&(mask),
							32, bit + 1);
					if (bit >= 32)
						break;
					list_idx++;
					tbl_idx++;
					continue;
				}
			}
			snprintf(str, ARRAY_SIZE(str),
					"func_%d", tbl_idx);
			strcpy(data->pin_func_table[tbl_idx], str);
			tbl_idx++;
		}
		data++;
	}
	return 0;
}
static int mfp_setting_dump(struct seq_file *m, void *unused)
{
	u32 pa_base = ((u32 *)(m->private))[0];
	u32 length = ((u32 *)(m->private))[1];
	int off = 0;
	void __iomem *va_base = ioremap(pa_base, length);
	struct pinfunc_data *data;

	if (!mfp_dump_data) {
		seq_puts(m, "MFP func table is not inited!!\n");
		iounmap(va_base);
		return 0;
	}

	data = mfp_dump_data;

	seq_printf(m, "%20s%8s %8s %20s %12s %12s %16s %19s\n",
		"Pin Name", "(Offset)", "MFPR", "AltFunc",
		"PullConf", "DRV.STR", "LPM Mode", "EDGE DET");

	for (off = 0; off < length; off += 0x4) {
		u32 val = readl_relaxed(va_base + off);

		seq_printf(m, "%20s(0x%04x) : 0x%04x%22s%12s%11s%22s%18s\n",
			data->spec_name, off, val,
			data->pin_func_table[val & 0x0007],
			((val & 0x7000) == 0x6000) ?
			"Wrong PULL" : (val & 0x8000) ?
			((val & 0x6000) == 0) ? "Pull Float" :
			((val & 0x4000) ? ((val & 0x2000) ?
			"Pull Both" : "Pull High") : "Pull Low") : "No Pull",
			((val & 0x1800) == 0x1800) ?
			"FAST" : ((val & 0x1800) == 0x1000) ?
			"MEDIUM" : ((val & 0x1800) == 0x0800) ?
			"SLOW" : "SLOW0",
			((val & 0x0208) == 0) ?
			"No Sleep control" :
			((val & 0x0208) == 0x0208) ? ((val & 0x0080) ?
			"Sleep Input" : (val & 0x0100) ?
			"Sleep Out High" : "Sleep Out Low") :
			"UNEX Sleep Control",
			((val & 0x0040) == 0x0040) ?
			"No EDGE Detect" : ((val & 0x0020) == 0) ?
			(((val & 0x0010) == 0) ?
			"EDGE Not Set" : "EDGE Fall EN") :
			(val & 0x0010) ? "EDGE Both EN" : "EDGE Rising EN");

		data++;
	}

	iounmap(va_base);

	return 0;
}

static int mfp_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, mfp_setting_dump, inode->i_private);
}

const struct file_operations mfp_dump_fops = {
	.owner	= THIS_MODULE,
	.open = mfp_dump_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int mfp_probe(struct platform_device *dev)
{
	static u32 mfpreg_data[2] = {0};
	struct device_node *node;
	node = of_find_compatible_node(NULL, NULL, "marvell,mmp-mfp-leftover");

	if (!node) {
		pr_err("MFP DTS is not found and no init on MFP\n");
		return -EINVAL;
	}

	if (of_property_read_u32_array(node, "reg", mfpreg_data,
		ARRAY_SIZE(mfpreg_data))) {
		pr_err("Cannot get MFP Reg Base and Length\n");
		return -EINVAL;
	}

	pr_debug("MFPReg Base : 0x%x\n", mfpreg_data[0]);
	pr_debug("MFPReg Length: 0x%x\n", mfpreg_data[1]);

	debugfs_create_file("mfp_setting_dump",
			S_IRUGO, NULL, mfpreg_data, &mfp_dump_fops);

	if (!mfp_dump_data)
		if (pins_functbl_init(mfpreg_data[1], node))
			pr_info("HW Pin func table Initialization fails!\n");

	pr_info("MFP Configuration is inited in MFP init driver\n");
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id mfp_dt_ids[] = {
	{ .compatible = "marvell,mmp-mfp-leftover", },
	{}
};
MODULE_DEVICE_TABLE(of, mfp_dt_ids);
#endif

static struct platform_driver mfp_driver = {
	.probe		= mfp_probe,
	.driver		= {
		.name	= "mmp-mfp-leftover",
		.owner	= THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = of_match_ptr(mfp_dt_ids),
#endif
	},
};

int __init mfp_init(void)
{
	return platform_driver_register(&mfp_driver);
}

subsys_initcall(mfp_init);

MODULE_AUTHOR("Fan Wu<fwu@marvell.com>");
MODULE_DESCRIPTION("MFP Initialization Driver for MMP");
MODULE_LICENSE("GPL v2");
