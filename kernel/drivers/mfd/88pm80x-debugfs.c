/*
 * Debugfs inteface for Marvell 88PM80X
 *
 * Copyright (C) 2014 Marvell International Ltd.
 * Jett Zhou <jtzhou@marvell.com>
 * Yi Zhang <yizhang@marvell.com>
 * Shay Pathov <shayp@marvell.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mfd/88pm80x.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define PM80X_BASE_REG_NUM		0xf0
#define PM80X_POWER_REG_NUM		0x9b
#define PM80X_GPADC_REG_NUM		0xb6

static int reg_pm800 = 0xffff;
static int pg_index;

static ssize_t pm800_regdump_read(struct file *file, char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	struct pm80x_chip *chip = file->private_data;
	unsigned int i, reg_val = 0, len = 0;
	char *buf;
	ssize_t ret;
	/* all the registers dumped need almost two pages space to store */
	buf = kzalloc(PAGE_SIZE * 2, GFP_KERNEL);
	if (!buf) {
		pr_err("Cannot allocate buffer!\n");
		return -ENOMEM;
	}

	if (reg_pm800 == 0xffff) {

		/* base page */
		len += sprintf(buf + len, "pm800: base page:\n");

		for (i = 0; i < PM80X_BASE_REG_NUM; i++) {
			regmap_read(chip->regmap, i, &reg_val);
			len += sprintf(buf + len, "[0x%02x]=0x%02x\n", i, reg_val);
		}

		/* power page */
		len += sprintf(buf + len, "pm800: power page:\n");

		for (i = 0; i < PM80X_POWER_REG_NUM; i++) {
			regmap_read(chip->subchip->regmap_power, i, &reg_val);
			len += sprintf(buf + len, "[0x%02x]=0x%02x\n", i, reg_val);
		}

		/* gpadc page */
		len += sprintf(buf + len, "pm800: gpadc page:\n");

		for (i = 0; i < PM80X_GPADC_REG_NUM; i++) {
			regmap_read(chip->subchip->regmap_gpadc, i, &reg_val);
			len += sprintf(buf + len, "[0x%02x]=0x%02x\n", i, reg_val);

		}
	} else {
		switch (pg_index) {
		case 0:
			regmap_read(chip->regmap, reg_pm800, &reg_val);
			break;
		case 1:
			regmap_read(chip->subchip->regmap_power, reg_pm800,
					&reg_val);
			break;
		case 2:
			regmap_read(chip->subchip->regmap_gpadc, reg_pm800,
					&reg_val);
			break;
		case 7:
			regmap_read(chip->subchip->regmap_test, reg_pm800,
					&reg_val);
			break;
		default:
			pr_err("pg_index error!\n");
			kfree(buf);
			return 0;
		}

		len += sprintf(buf, "reg_pm800=0x%x, pg_index=0x%x, val=0x%x\n",
			      reg_pm800, pg_index, reg_val);
	}
	ret = simple_read_from_buffer(userbuf, count, ppos, buf, len);
	kfree(buf);
	return ret;
}

static ssize_t pm800_regdump_write(struct file *file,
				const char __user *buff,
				size_t len, loff_t *ppos)
{
	u8 reg_val;
	struct pm80x_chip *chip = file->private_data;

	char messages[20], index[20];
	memset(messages, '\0', 20);
	memset(index, '\0', 20);

	if (copy_from_user(messages, buff, len))
		return -EFAULT;

	if ('-' == messages[0]) {
		if ((strlen(messages) != 10) &&
		    (strlen(messages) != 9)) {
			pr_err("Right format: -0x[page_addr] 0x[reg_addr]\n");
			return len;
		}
		/* set the register index */
		memcpy(index, messages + 1, 3);

		if (kstrtoint(index, 16, &pg_index) < 0)
			return -EINVAL;

		pr_info("pg_index = 0x%x\n", pg_index);

		memcpy(index, messages + 5, 4);
		if (kstrtoint(index, 16, &reg_pm800) < 0)
			return -EINVAL;
		pr_info("reg_pm800 = 0x%x\n", reg_pm800);
	} else if ('+' == messages[0]) {
		/* enable to get all the reg value */
		reg_pm800 = 0xffff;
		pr_info("read all reg enabled!\n");
	} else {
		if ((reg_pm800 == 0xffff) ||
		    ('0' != messages[0])) {
			pr_err("Right format: -0x[page_addr] 0x[reg_addr]\n");
			return len;
		}
		/* set the register value */
		if (kstrtou8(messages, 16, &reg_val) < 0)
			return -EINVAL;

		switch (pg_index) {
		case 0:
			regmap_write(chip->regmap, reg_pm800, reg_val & 0xff);
			break;
		case 1:
			regmap_write(chip->subchip->regmap_power,
				     reg_pm800, reg_val & 0xff);
			break;
		case 2:
			regmap_write(chip->subchip->regmap_gpadc,
				     reg_pm800, reg_val & 0xff);
			break;
		case 7:
			regmap_write(chip->subchip->regmap_test,
				     reg_pm800, reg_val & 0xff);
			break;
		default:
			pr_err("pg_index error!\n");
			break;

		}
	}

	return len;
}

static const struct file_operations pm800_regdump_ops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.read		= pm800_regdump_read,
	.write		= pm800_regdump_write,
};

static ssize_t pm800_powerup_log_read(struct file *file, char __user *user_buf,
			       size_t count, loff_t *ppos)
{
	struct pm80x_chip *chip = file->private_data;
	unsigned int i, size;
	int len = 0;
	char buf[100];
	char *powerup_name[] = {
		"ONKEY_WAKEUP",
		"CHG_WAKEUP",
		"EXTON_WAKEUP",
		"RSVD",
		"RTC_ALARM_WAKEUP",
		"FAULT_WAKEUP",
		"BAT_WAKEUP"
	};

	size = sizeof(powerup_name) / sizeof(char *);
	for (i = 0; i < size; i++) {
		if ((1 << i) & chip->powerup)
			len += sprintf(&buf[len], "0x%x (%s)\n", chip->powerup, powerup_name[i]);
	}

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations pm800_powerup_log_ops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.read		= pm800_powerup_log_read,
	.write		= NULL,
};


static ssize_t pm800_powerdown_log_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct pm80x_chip *chip = file->private_data;
	unsigned int i, size;
	int len = 0;
	char buf[100];
	char *powerdown1_name[] = {
		"OVER_TEMP",
		"UV_VSYS1",
		"SW_PDOWN",
		"FL_ALARM",
		"WD",
		"LONG_ONKEY",
		"OV_VSYS",
		"RTC_RESET"
	};
	char *powerdown2_name[] = {
		"HYB_DONE",
		"UV_VSYS2",
		"HW_RESET",
		"PGOOD_PDOWN",
		"LONKEY_RTC"
	};

	len += sprintf(&buf[len], "0x%x,0x%x ",
			chip->powerdown1, chip->powerdown2);
	if (!chip->powerdown1 && !chip->powerdown2) {
		len += sprintf(&buf[len], "(NO_PMIC_RESET)\n");
		return simple_read_from_buffer(user_buf, count, ppos, buf, len);
	}

	len += sprintf(&buf[len], "(");
	size = sizeof(powerdown1_name) / sizeof(char *);
	for (i = 0; i < size; i++) {
		if ((1 << i) & chip->powerdown1)
			len += sprintf(&buf[len], "%s ", powerdown1_name[i]);
	}

	size = sizeof(powerdown2_name) / sizeof(char *);
	for (i = 0; i < size; i++) {
		if ((1 << i) & chip->powerdown2)
			len += sprintf(&buf[len], "%s ", powerdown2_name[i]);
	}

	len--;
	len += sprintf(&buf[len], ")\n");

	return simple_read_from_buffer(user_buf, count, ppos, buf, len);
}

static const struct file_operations pm800_powerdown_log_ops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.read		= pm800_powerdown_log_read,
	.write		= NULL,
};

static ssize_t pm800_debug_read(struct file *file, char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	struct pm80x_chip *chip = file->private_data;
	int len;
	ssize_t ret = -EINVAL;
	char *buf;

	if (!chip) {
		pr_err("Cannot find chip!\n");
		return -EINVAL;
	}

	buf = kzalloc(7000, GFP_KERNEL);
	if (!buf) {
		pr_err("Cannot allocate buffer!\n");
		return -ENOMEM;
	}

	ret = pm800_display_regulator(chip, buf);
	if (ret < 0) {
		pr_err("Error in printing the buck & ldo list!\n");
		goto out_print;
	}

	len = ret;

	ret = pm800_display_gpadc(chip, buf + len);
	if (ret < 0) {
		pr_err("Error in printing the GPADC values!\n");
		goto out_print;
	}

	len += ret;

	ret = simple_read_from_buffer(userbuf, count, ppos, buf, len);
out_print:
	kfree(buf);
	return ret;
}

static const struct file_operations pm800_debug_ops = {
	.owner		= THIS_MODULE,
	.open		= simple_open,
	.read		= pm800_debug_read,
	.write		= NULL,
};

static int pm80x_debug_probe(struct platform_device *pdev)
{
	struct pm80x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct dentry *pm800_regdump_reg;
	struct dentry *pm800_powerup_log;
	struct dentry *pm800_powerdown_log;

	pm800_regdump_reg = debugfs_create_file("pm800_reg", S_IRUGO | S_IFREG,
			    NULL, (void *)chip, &pm800_regdump_ops);

	if (pm800_regdump_reg == NULL) {
		dev_err(&pdev->dev, "create pm800 debugfs error!\n");
		return -ENOENT;
	} else if (pm800_regdump_reg == ERR_PTR(-ENODEV)) {
		dev_err(&pdev->dev, "pm800_regdump_reg error!\n");
		return -ENOENT;
	}

	pm800_powerup_log =
		debugfs_create_file("pm800_powerup_log", S_IRUGO | S_IFREG,
				NULL, (void *)chip, &pm800_powerup_log_ops);

	if (pm800_powerup_log == NULL) {
		dev_err(&pdev->dev, "create pm800_powerup_log debugfs error!\n");
		return -ENOENT;
	} else if (pm800_powerup_log == ERR_PTR(-ENODEV)) {
		dev_err(&pdev->dev, "CONFIG_DEBUG_FS is not enabled!\n");
		return -ENOENT;
	}

	pm800_powerdown_log =
		debugfs_create_file("pm800_powerdown_log", S_IRUGO | S_IFREG,
				NULL, (void *)chip, &pm800_powerdown_log_ops);

	if (pm800_powerdown_log == NULL) {
		dev_err(&pdev->dev, "create pm800_powerdown_log debugfs error!\n");
		return -ENOENT;
	} else if (pm800_powerdown_log == ERR_PTR(-ENODEV)) {
		dev_err(&pdev->dev, "CONFIG_DEBUG_FS is not enabled!\n");
		return -ENOENT;
	}

	chip->debugfs = debugfs_create_file("pm800_debug", S_IRUGO | S_IFREG,
			    NULL, (void *)chip, &pm800_debug_ops);

	if (chip->debugfs == NULL) {
		dev_err(&pdev->dev, "create pm800_debug debugfs error!\n");
		return -ENOENT;
	} else if (chip->debugfs == ERR_PTR(-ENODEV)) {
		dev_err(&pdev->dev, "CONFIG_DEBUG_FS is not enabled!\n");
		return -ENOENT;
	}

	return 0;
}

static int pm80x_debug_remove(struct platform_device *pdev)
{
	struct pm80x_chip *chip = dev_get_drvdata(pdev->dev.parent);

	debugfs_remove_recursive(chip->debugfs);
	return 0;
}

static struct platform_driver pm80x_debug_driver = {
	.probe = pm80x_debug_probe,
	.remove = pm80x_debug_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = "88pm80x-debug",
	},
};

module_platform_driver(pm80x_debug_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("88pm80x debug Driver");
MODULE_AUTHOR("Yi Zhang<yizhang@marvell.com>");
