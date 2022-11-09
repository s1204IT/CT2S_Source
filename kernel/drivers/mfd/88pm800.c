/*
 * Base driver for Marvell 88PM800
 *
 * Copyright (C) 2013 Marvell International Ltd.
 * Haojian Zhuang <haojian.zhuang@marvell.com>
 * Joseph(Yossi) Hanin <yhanin@marvell.com>
 * Qiao Zhou <zhouqiao@marvell.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/switch.h>
#include <linux/mfd/core.h>
#include <linux/mfd/88pm80x.h>
#include <linux/of_device.h>
#include <linux/reboot.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

static DEFINE_MUTEX(audio_mode_mutex);

/* Interrupt Registers */
#define PM800_INT_STATUS1		(0x05)
#define PM800_ONKEY_INT_STS1		(1 << 0)
#define PM800_EXTON_INT_STS1		(1 << 1)
#define PM800_CHG_INT_STS1			(1 << 2)
#define PM800_BAT_INT_STS1			(1 << 3)
#define PM800_RTC_INT_STS1			(1 << 4)
#define PM800_CLASSD_OC_INT_STS1	(1 << 5)

#define PM800_INT_STATUS2		(0x06)
#define PM800_VBAT_INT_STS2		(1 << 0)
#define PM800_VSYS_INT_STS2		(1 << 1)
#define PM800_VCHG_INT_STS2		(1 << 2)
#define PM800_TINT_INT_STS2		(1 << 3)
#define PM800_GPADC0_INT_STS2	(1 << 4)
#define PM800_TBAT_INT_STS2		(1 << 5)
#define PM800_GPADC2_INT_STS2	(1 << 6)
#define PM800_GPADC3_INT_STS2	(1 << 7)

#define PM800_INT_STATUS3		(0x07)

#define PM800_INT_STATUS4		(0x08)
#define PM800_GPIO0_INT_STS4		(1 << 0)
#define PM800_GPIO1_INT_STS4		(1 << 1)
#define PM800_GPIO2_INT_STS4		(1 << 2)
#define PM800_GPIO3_INT_STS4		(1 << 3)
#define PM800_GPIO4_INT_STS4		(1 << 4)

#define PM800_INT_ENA_1		(0x09)
#define PM800_ONKEY_INT_ENA1		(1 << 0)
#define PM800_EXTON_INT_ENA1		(1 << 1)
#define PM800_CHG_INT_ENA1			(1 << 2)
#define PM800_BAT_INT_ENA1			(1 << 3)
#define PM800_RTC_INT_ENA1			(1 << 4)
#define PM800_CLASSD_OC_INT_ENA1	(1 << 5)

#define PM800_INT_ENA_2		(0x0A)
#define PM800_VBAT_INT_ENA2		(1 << 0)
#define PM800_VSYS_INT_ENA2		(1 << 1)
#define PM800_VCHG_INT_ENA2		(1 << 2)
#define PM800_TINT_INT_ENA2		(1 << 3)
#define PM822_IRQ_LDO_PGOOD_EN		(1 << 4)
#define PM822_IRQ_BUCK_PGOOD_EN		(1 << 5)

#define PM800_INT_ENA_3		(0x0B)
#define PM800_GPADC0_INT_ENA3		(1 << 0)
#define PM800_GPADC1_INT_ENA3		(1 << 1)
#define PM800_GPADC2_INT_ENA3		(1 << 2)
#define PM800_GPADC3_INT_ENA3		(1 << 3)
#define PM800_GPADC4_INT_ENA3		(1 << 4)
#define PM822_IRQ_HS_DET_EN		(1 << 5)

#define PM800_INT_ENA_4		(0x0C)
#define PM800_GPIO0_INT_ENA4		(1 << 0)
#define PM800_GPIO1_INT_ENA4		(1 << 1)
#define PM800_GPIO2_INT_ENA4		(1 << 2)
#define PM800_GPIO3_INT_ENA4		(1 << 3)
#define PM800_GPIO4_INT_ENA4		(1 << 4)

#define PM800_POWER_REG_NUM		(0x9a)
#define PM800_GPADC_REG_NUM		(0xb5)
#define PM822_POWER_REG_NUM		(0x98)
#define PM822_GPADC_REG_NUM		(0xc7)

#define PM80X_BASE_REG_NUM		0xf0
#define PM80X_POWER_REG_NUM		0x9b
#define PM80X_GPADC_REG_NUM		0xb6

/* number of INT_ENA & INT_STATUS regs */
#define PM800_INT_REG_NUM			(4)

const struct regmap_config pm800_power_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = PM800_POWER_REG_NUM,
};

const struct regmap_config pm800_gpadc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = PM800_GPADC_REG_NUM,
};

const struct regmap_config pm822_power_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = PM822_POWER_REG_NUM,
};

const struct regmap_config pm822_gpadc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = PM822_GPADC_REG_NUM,
};

const struct regmap_config pm80x_test_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

enum {
	VBBAT,
	VBAT,
	VSYS,
	VCHG,
	GPADC0,
	GPADC1,
	GPADC2,
	GPADC3,
	GPADC4,
	MIC_DET = GPADC4,
	TINT,
	PMOD
};

struct pm800_gpadc_convert {
	int multi;
	int add;
	int div;
};

struct pm800_gpadc_info {
	int id;
	const char *name;
	const char *units;
	unsigned int meas_reg;
	unsigned int avg_reg;
	unsigned int min_reg;
	unsigned int max_reg;
	unsigned int sleep_reg;
	unsigned int sleep_enable_bit;
	unsigned int enable_reg;
	unsigned int enable_bit;
	unsigned int bias_reg;
	unsigned int bias_mask;
	unsigned int bias_en_reg;
	unsigned int bias_en_bit;
	unsigned int bias_out_bit;
	struct pm800_gpadc_convert conv;
};

/*
 * gname - name of the GPADC
 * uni - measurement units
 * ereg - enable register
 * ebit - enable bit
 * mul - multiplication number to convert measurement
 * adds - add number to convert measurement
 * dive - divide number to convert measurement
 */
#define PM800_GPADC(gname, uni, ereg, ebit, mul, adds, dive)	\
	.id = gname,							\
	.name	= #gname,						\
	.units	= (#uni),						\
	.meas_reg	= PM800_##gname##_MEAS1,			\
	.enable_reg	= PM800_GPADC_MEAS_##ereg,			\
	.enable_bit	= PM800_MEAS_##ebit,				\
	.conv	= {							\
		.multi	= mul,						\
		.add	= adds,						\
		.div	= dive,						\
	},

/*
 * fld - the field to assign value to him
 * cnum - chip number 00/22/60
 * name - name of the GPADC
 * type - the value type (min/max/avg...)
 */
#define PM800_GPADC_EXTRA(fld, cnum, name, type) .fld = PM8##cnum##_##name##_##type,

/*
 * breg - bias register
 * bereg - bias enable register
 * bebit - bias enable bit
 * bmask - bias mask register
 */
#define PM800_GPADC_BIAS(breg, bereg, bebit, bmask)			\
	.bias_reg	= PM800_GPADC_BIAS##breg,			\
	.bias_mask	= PM800_GPADC_BIAS_MASK##bmask,			\
	.bias_en_reg	= PM800_GP_BIAS_##bereg,			\
	.bias_en_bit	= PM800_GPADC_GP_BIAS_EN##bebit,		\
	.bias_out_bit	= PM800_GPADC_GP_BIAS_OUT##bebit,

#define PM800_GPADC_CHECK_EN(val) (val ? "enable" : "disable")

#define IS_DUMMY(val) (val & 0xF00)

#define PM800_GPADC_CONV(value, conv, mul)				\
{									\
	value = ((value * conv->multi + conv->add) / conv->div) * mul;	\
}

static struct pm800_gpadc_info pm800_gpadc_info[] = {
		/*
		 * PM800_GPADC(gname, uni, ereg, ebit, mul, adds, dive)
		 * PM800_GPADC_EXTRA(fld, cnum, name, type)
		 * PM800_GPADC_BIAS(breg, bereg, bebit, bmask)
		 */
		{
		PM800_GPADC(VBBAT, mV, EN1, EN1_VBBAT, 1367, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}, {
		PM800_GPADC(VBAT, mV, EN1, EN1_VBAT, 1367, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, VBAT, AVG1)
		PM800_GPADC_EXTRA(min_reg, 00, VBAT, MIN1)
		PM800_GPADC_EXTRA(max_reg, 00, VBAT, MAX1)
		PM800_GPADC_EXTRA(sleep_reg, 00, VBAT, SLP)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}, {
		PM800_GPADC(VSYS, mV, EN1, EN1_VSYS, 1367, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, VSYS, AVG1)
		PM800_GPADC_EXTRA(min_reg, 00, VSYS, MIN1)
		PM800_GPADC_EXTRA(max_reg, 00, VSYS, MAX1)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}, {
		PM800_GPADC(VCHG, mV, EN2, EN1_VCHG, 1709, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, VCHG, AVG1)
		PM800_GPADC_EXTRA(min_reg, 00, VCHG, MIN1)
		PM800_GPADC_EXTRA(max_reg, 00, VCHG, MAX1)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}, {
		PM800_GPADC(GPADC0, mV, EN2, GP0_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, ENA1, 0, 0)
		}, {
		PM800_GPADC(GPADC1, mV, EN2, GP1_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, ENA1, 1, 1)
		}, {
		PM800_GPADC(GPADC2, mV, EN2, GP2_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(2, ENA1, 2, 0)
		}, {
		PM800_GPADC(GPADC3, mV, EN2, GP3_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, GPADC3, AVG1)
		PM800_GPADC_EXTRA(min_reg, 00, GPADC3, MIN1)
		PM800_GPADC_EXTRA(max_reg, 00, GPADC3, MAX1)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(2, ENA1, 3, 1)
		}, {
		PM800_GPADC(GPADC4, mV, EN2, GP4_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, GPADC4, AVG1)
		PM800_GPADC_EXTRA(min_reg, 00, GPADC4, MIN1)
		PM800_GPADC_EXTRA(max_reg, 00, GPADC4, MAX1)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}, {
		PM800_GPADC(TINT, C, EN2, TINT_EN, 342, -884000, 3611)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}, {
		PM800_GPADC(PMOD, mV, EN2, PMOD_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}
};
static struct pm800_gpadc_info pm822_gpadc_info[] = {
		/*
		 * PM800_GPADC(gname, uni, ereg, ebit, mul, adds, dive)
		 * PM800_GPADC_EXTRA(fld, cnum, name, type)
		 * PM800_GPADC_BIAS(breg, bereg, bebit, bmask)
		 */
		{PM800_GPADC(VBBAT, mV, EN1, EN1_VBBAT, 1367, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}, {
		PM800_GPADC(VBAT, mV, EN1, EN1_VBAT, 1367, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, VBAT, AVG1)
		PM800_GPADC_EXTRA(min_reg, 00, VBAT, MIN1)
		PM800_GPADC_EXTRA(max_reg, 00, VBAT, MAX1)
		PM800_GPADC_EXTRA(sleep_reg, 00, VBAT, SLP)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}, {
		PM800_GPADC(VSYS, mV, EN1, EN1_VSYS, 1367, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, VSYS, AVG1)
		PM800_GPADC_EXTRA(min_reg, 00, VSYS, MIN1)
		PM800_GPADC_EXTRA(max_reg, 00, VSYS, MAX1)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}, {
		PM800_GPADC(GPADC0, mV, EN2, GP0_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, ENA1, 0, 0)
		}, {
		PM800_GPADC(GPADC1, mV, EN2, GP1_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(2, ENA1, 1, 0)
		}, {
		PM800_GPADC(GPADC2, mV, EN2, GP2_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(3, ENA1, 2, 0)
		}, {
		PM800_GPADC(GPADC3, mV, EN2, GP3_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, GPADC3, AVG1)
		PM800_GPADC_EXTRA(min_reg, 00, GPADC3, MIN1)
		PM800_GPADC_EXTRA(max_reg, 00, GPADC3, MAX1)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(4, ENA1, 3, 0)
		}, {
		PM800_GPADC(MIC_DET, mV, EN2, MIC_DET_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 22, MIC_DET, AVG1)
		PM800_GPADC_EXTRA(min_reg, 00, MIC_DET, MIN1)
		PM800_GPADC_EXTRA(max_reg, 00, MIC_DET, MAX1)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}, {
		PM800_GPADC(TINT, C, EN2, TINT_EN, 342, -884000, 3611)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}, {
		PM800_GPADC(PMOD, mV, EN2, PMOD_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}
};

static struct pm800_gpadc_info pm860_gpadc_info[] = {
		/*
		 * PM800_GPADC(gname, uni, ereg, ebit, mul, adds, dive)
		 * PM800_GPADC_EXTRA(fld, cnum, name, type)
		 * PM800_GPADC_BIAS(breg, bereg, bebit, bmask)
		 */
		{PM800_GPADC(VBBAT, mV, EN1, EN1_VBBAT, 1367, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}, {
		PM800_GPADC(VBAT, mV, EN1, EN1_VBAT, 1367, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, VBAT, AVG1)
		PM800_GPADC_EXTRA(min_reg, 00, VBAT, MIN1)
		PM800_GPADC_EXTRA(max_reg, 00, VBAT, MAX1)
		PM800_GPADC_EXTRA(sleep_reg, 00, VBAT, SLP)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}, {
		PM800_GPADC(VSYS, mV, EN1, EN1_VSYS, 1367, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, VSYS, AVG1)
		PM800_GPADC_EXTRA(min_reg, 00, VSYS, MIN1)
		PM800_GPADC_EXTRA(max_reg, 00, VSYS, MAX1)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}, {
		PM800_GPADC(GPADC0, mV, EN2, GP0_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, ENA1, 0, 0)
		}, {
		PM800_GPADC(GPADC1, mV, EN2, GP1_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(2, ENA1, 1, 0)
		}, {
		PM800_GPADC(GPADC2, mV, EN2, GP2_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(3, ENA1, 2, 0)
		}, {
		PM800_GPADC(GPADC3, mV, EN2, GP3_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 60, GPADC3, AVG1)
		PM800_GPADC_EXTRA(min_reg, 00, GPADC3, MIN1)
		PM800_GPADC_EXTRA(max_reg, 00, GPADC3, MAX1)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(4, ENA1, 3, 0)
		}, {
		PM800_GPADC(MIC_DET, mV, EN2, MIC_DET_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, MIC_DET, AVG1)
		PM800_GPADC_EXTRA(min_reg, 00, MIC_DET, MIN1)
		PM800_GPADC_EXTRA(max_reg, 00, MIC_DET, MAX1)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}, {
		PM800_GPADC(TINT, C, EN2, TINT_EN, 104, -273000, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}, {
		PM800_GPADC(PMOD, mV, EN2, PMOD_EN, 342, 0, 1000)
		PM800_GPADC_EXTRA(avg_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(min_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(max_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_EXTRA(sleep_reg, 00, DUMMY, DUMMY)
		PM800_GPADC_BIAS(1, DUMMY, 0, 0)
		}
};

/* Interrupt Number in 88PM800 */
enum {
	PM800_IRQ_ONKEY = 0,	/*EN1b0 *//*0 */
	PM800_IRQ_EXTON,	/*EN1b1 */
	PM800_IRQ_CHG,		/*EN1b2 */
	PM800_IRQ_BAT,		/*EN1b3 */
	PM800_IRQ_RTC,		/*EN1b4 */
	PM800_IRQ_CLASSD,	/*EN1b5 *//*5 */
	PM800_IRQ_VBAT,		/*EN2b0 */
	PM800_IRQ_VSYS,		/*EN2b1 */
	PM800_IRQ_VCHG,		/*EN2b2 */
	PM800_IRQ_TINT,		/*EN2b3 */
	PM822_IRQ_LDO_PGOOD,	/*EN2b4 *//*10 */
	PM822_IRQ_BUCK_PGOOD,	/*EN2b5 */
	PM800_IRQ_GPADC0,	/*EN3b0 */
	PM800_IRQ_GPADC1,	/*EN3b1 */
	PM800_IRQ_GPADC2,	/*EN3b2 */
	PM800_IRQ_GPADC3,	/*EN3b3 *//*15 */
	PM800_IRQ_GPADC4 = 16,	/*EN3b4 */
	PM822_IRQ_MIC_DET = 16,	/*EN3b4 */
	PM822_IRQ_HS_DET = 17,	/*EN3b4 */
	PM800_IRQ_GPIO0,	/*EN4b0 */
	PM800_IRQ_GPIO1,	/*EN4b1 */
	PM800_IRQ_GPIO2,	/*EN4b2 *//*20 */
	PM800_IRQ_GPIO3,	/*EN4b3 */
	PM800_IRQ_GPIO4,	/*EN4b4 */
	PM800_MAX_IRQ,
};

/* PM800: generation identification number */
#define PM800_CHIP_GEN_ID_NUM	0x3

enum pm8xx_parent {
	PM822 = 0x822,
	PM800 = 0x800,
};

static const struct i2c_device_id pm80x_id_table[] = {
	{"88pm822", PM822},
	{"88pm800", PM800},
	{} /* NULL terminated */
};
MODULE_DEVICE_TABLE(i2c, pm80x_id_table);

static const struct of_device_id pm80x_dt_ids[] = {
	{ .compatible = "marvell,88pm800", },
	{ .compatible = "marvell,88pm822", },
	{},
};
MODULE_DEVICE_TABLE(of, pm80x_dt_ids);

static struct resource rtc_resources[] = {
	{
	 .name = "88pm80x-rtc",
	 .start = PM800_IRQ_RTC,
	 .end = PM800_IRQ_RTC,
	 .flags = IORESOURCE_IRQ,
	 },
};

static struct mfd_cell rtc_devs[] = {
	{
	 .name = "88pm80x-rtc",
	 .of_compatible = "marvell,88pm80x-rtc",
	 .num_resources = ARRAY_SIZE(rtc_resources),
	 .resources = &rtc_resources[0],
	 .id = -1,
	 },
};

static struct resource onkey_resources[] = {
	{
	 .name = "88pm80x-onkey",
	 .start = PM800_IRQ_ONKEY,
	 .end = PM800_IRQ_ONKEY,
	 .flags = IORESOURCE_IRQ,
	 },
};

static struct mfd_cell onkey_devs[] = {
	{
	 .name = "88pm80x-onkey",
	 .of_compatible = "marvell,88pm80x-onkey",
	 .num_resources = 1,
	 .resources = &onkey_resources[0],
	 .id = -1,
	 },
};

static struct resource bat_resources[] = {
	{
	.name = "88pm80x-bat",
	.start = PM800_IRQ_VBAT,
	.end = PM800_IRQ_VBAT,
	.flags = IORESOURCE_IRQ,
	},
};

static struct mfd_cell bat_devs[] = {
	{
	.name = "88pm80x-bat",
	.of_compatible = "marvell,88pm80x-battery",
	.num_resources = 1,
	.resources = &bat_resources[0],
	.id = -1,
	},
};

static struct resource usb_resources[] = {
	{
	.name = "88pm80x-usb",
	.start = PM800_IRQ_CHG,
	.end = PM800_IRQ_CHG,
	.flags = IORESOURCE_IRQ,
	},
};

static struct mfd_cell usb_devs[] = {
	{
	.name = "88pm80x-usb",
	 .of_compatible = "marvell,88pm80x-usb",
	.num_resources = 1,
	.resources = &usb_resources[0],
	.id = -1,
	},
};

static struct mfd_cell regulator_devs[] = {
	{
	 .name = "88pm80x-regulator",
	 .of_compatible = "marvell,88pm80x-regulator",
	 .id = -1,
	},
};

static struct mfd_cell dvc_devs[] = {
	{
	 .name = "88pm8xx-dvc",
	 .of_compatible = "marvell,88pm8xx-dvc",
	 .id = -1,
	},
};

static struct mfd_cell vibrator_devs[] = {
	{
	 .name = "88pm80x-vibrator",
	 .of_compatible = "marvell,88pm80x-vibrator",
	 .id = -1,
	},
};

static struct mfd_cell debug_devs[] = {
	{
		.name = "88pm80x-debug",
		.of_compatible = "marvell,88pm80x-debug",
		.id = -1,
	},
};

static const struct regmap_irq pm800_irqs[] = {
	/* INT0 */
	[PM800_IRQ_ONKEY] = {
		.mask = PM800_ONKEY_INT_ENA1,
	},
	[PM800_IRQ_EXTON] = {
		.mask = PM800_EXTON_INT_ENA1,
	},
	[PM800_IRQ_CHG] = {
		.mask = PM800_CHG_INT_ENA1,
	},
	[PM800_IRQ_BAT] = {
		.mask = PM800_BAT_INT_ENA1,
	},
	[PM800_IRQ_RTC] = {
		.mask = PM800_RTC_INT_ENA1,
	},
	[PM800_IRQ_CLASSD] = {
		.mask = PM800_CLASSD_OC_INT_ENA1,
	},
	/* INT1 */
	[PM800_IRQ_VBAT] = {
		.reg_offset = 1,
		.mask = PM800_VBAT_INT_ENA2,
	},
	[PM800_IRQ_VSYS] = {
		.reg_offset = 1,
		.mask = PM800_VSYS_INT_ENA2,
	},
	[PM800_IRQ_VCHG] = {
		.reg_offset = 1,
		.mask = PM800_VCHG_INT_ENA2,
	},
	[PM800_IRQ_TINT] = {
		.reg_offset = 1,
		.mask = PM800_TINT_INT_ENA2,
	},
	[PM822_IRQ_LDO_PGOOD] = {
		.reg_offset = 1,
		.mask = PM822_IRQ_LDO_PGOOD_EN,
	},
	[PM822_IRQ_BUCK_PGOOD] = {
		.reg_offset = 1,
		.mask = PM822_IRQ_BUCK_PGOOD_EN,
	},
	/* INT2 */
	[PM800_IRQ_GPADC0] = {
		.reg_offset = 2,
		.mask = PM800_GPADC0_INT_ENA3,
	},
	[PM800_IRQ_GPADC1] = {
		.reg_offset = 2,
		.mask = PM800_GPADC1_INT_ENA3,
	},
	[PM800_IRQ_GPADC2] = {
		.reg_offset = 2,
		.mask = PM800_GPADC2_INT_ENA3,
	},
	[PM800_IRQ_GPADC3] = {
		.reg_offset = 2,
		.mask = PM800_GPADC3_INT_ENA3,
	},
	[PM800_IRQ_GPADC4] = {
		.reg_offset = 2,
		.mask = PM800_GPADC4_INT_ENA3,
	},
	[PM822_IRQ_HS_DET] = {
		.reg_offset = 2,
		.mask = PM822_IRQ_HS_DET_EN,
	},
	/* INT3 */
	[PM800_IRQ_GPIO0] = {
		.reg_offset = 3,
		.mask = PM800_GPIO0_INT_ENA4,
	},
	[PM800_IRQ_GPIO1] = {
		.reg_offset = 3,
		.mask = PM800_GPIO1_INT_ENA4,
	},
	[PM800_IRQ_GPIO2] = {
		.reg_offset = 3,
		.mask = PM800_GPIO2_INT_ENA4,
	},
	[PM800_IRQ_GPIO3] = {
		.reg_offset = 3,
		.mask = PM800_GPIO3_INT_ENA4,
	},
	[PM800_IRQ_GPIO4] = {
		.reg_offset = 3,
		.mask = PM800_GPIO4_INT_ENA4,
	},
};

static struct resource headset_resources_800[] = {
	{
		.name = "gpio-03",
		.start = PM800_IRQ_GPIO3,
		.end = PM800_IRQ_GPIO3,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "gpadc4",
		.start = PM800_IRQ_GPADC4,
		.end = PM800_IRQ_GPADC4,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource headset_resources_822[] = {
	{
		.name = "88pm822-headset",
		.start = PM822_IRQ_HS_DET,
		.end = PM822_IRQ_HS_DET,
		.flags = IORESOURCE_IRQ,
	},
	{
		.name = "gpadc4",
		.start = PM800_IRQ_GPADC4,
		.end = PM800_IRQ_GPADC4,
		.flags = IORESOURCE_IRQ,
	},
};

static struct mfd_cell headset_devs_800[] = {
	{
	 .name = "88pm800-headset",
	 .of_compatible = "marvell,88pm80x-headset",
	 .num_resources = ARRAY_SIZE(headset_resources_800),
	 .resources = &headset_resources_800[0],
	 .id = -1,
	 },
};

static int pm800_gpadc_check_en(struct regmap *map, unsigned int reg, unsigned int mask)
{
	int ret;
	unsigned int enable;

	ret = regmap_bulk_read(map, reg, &enable, 1);
	if (ret < 0)
		return ret;

	enable = (enable & mask);

	return enable;
}

static int pm800_gpadc_get_meas(struct regmap *map, unsigned int reg, unsigned int *value)
{
	int ret;
	unsigned char val[2];

	ret = regmap_bulk_read(map, reg, val, 2);
	if (ret < 0) {
		*value = reg;
		return ret;
	}

	*value = (val[0] << 4) | (val[1] & 0x0f);
	return 0;
}

static int pm800_gpadc_print_bias(struct regmap *map, struct pm800_gpadc_info *info,
		char *buf, int *len)
{
	unsigned int en_mask, out_mask, mask, val = 0;
	int ret;

	mask = info->bias_mask;
	en_mask = info->bias_en_bit;
	out_mask = info->bias_out_bit;

	if (!IS_DUMMY(info->bias_en_reg)) {
		ret = regmap_bulk_read(map, info->bias_en_reg, &val, 1);
		if (ret < 0)
			return ret;
		else {
			/*
			 * the bias is enabled if:
			 * GP_BIAS_EN is 0
			 * GP_BIAS_EN is 1 and GP_BIAS_OUT is 1
			 * thus, !GP_BIAS_EN || GP_BIAS_OUT
			 */
			val = (!(val & en_mask) || (val & out_mask));
		}
		if (val) {
			ret = regmap_bulk_read(map, info->bias_reg, &val, 1);
			if (ret < 0)
				return ret;
			else {
				val = ((val & mask) >> (ffs(mask) - 1)) * 5 + 1;
				*len += sprintf(buf + *len, "   %-4d |", val);
			}
		} else
			*len += sprintf(buf + *len, "%s |", "Disable");
	} else
		*len += sprintf(buf + *len, "   %-4s |", "-");

	return val;
}

static int pm800_gpadc_print(struct regmap *map, struct pm800_gpadc_info *info,
		int id, char *buf, int *len)
{
	int ret[5], i;
	unsigned int val[5] = {0};
	int enlarge = 1;
	struct pm800_gpadc_convert *conv;


	conv = &info->conv;

	if (id == GPADC4) {
		ret[0] = regmap_bulk_read(map, PM800_GPADC_MISC_CONFIG1, val, 1);
		if (ret[0] < 0)
			return ret[0];
		if (!(val[0] & PM800_GPADC_MISC_DIRECT))
			enlarge = 4;
	}

	/*
	 * ret[i] > 0 when the register is dummy
	 * ret[i] = 0 when a value was read successfully
	 * ret[i] < 0 when the read attempt was failed
	 */
	if (!IS_DUMMY(info->meas_reg))
		ret[0]  = pm800_gpadc_get_meas(map, info->meas_reg, &val[0]);
	else
		ret[0] = 1;

	if (!IS_DUMMY(info->avg_reg))
		ret[1]  = pm800_gpadc_get_meas(map, info->avg_reg, &val[1]);
	else
		ret[1] = 1;

	if (!IS_DUMMY(info->min_reg))
		ret[2]  = pm800_gpadc_get_meas(map, info->min_reg, &val[2]);
	else
		ret[2] = 1;

	if (!IS_DUMMY(info->max_reg))
		ret[3]  = pm800_gpadc_get_meas(map, info->max_reg, &val[3]);
	else
		ret[3] = 1;

	if (!IS_DUMMY(info->sleep_reg))
		ret[4]  = pm800_gpadc_get_meas(map, info->sleep_reg, &val[4]);
	else
		ret[4] = 1;

	for (i = 0; i < 5; i++) {
		if (ret[i] < 0)
			*len += sprintf(buf + *len, "ERR 0x%-2x|", val[i]);
		else if (ret[i] > 0)
			*len += sprintf(buf + *len, "   %-4s |", "-");
		else {
			PM800_GPADC_CONV(val[i], conv, enlarge)
			*len += sprintf(buf + *len, "  %-4d  |", val[i]);
		}
	}

	pm800_gpadc_print_bias(map, info, buf, len);
	*len += sprintf(buf + *len, "\n");

	return *len;
}

int pm800_display_gpadc(struct pm80x_chip *chip, char *buf)
{
	int gpadc_num, i, id, len = 0;
	struct pm800_gpadc_info *info;
	struct regmap *map = chip->subchip->regmap_gpadc;
	struct pm800_gpadc_convert *conv;
	ssize_t ret;

	switch (chip->type) {
	case CHIP_PM800:
		gpadc_num = PM800_NUM_GPADC;
		info = pm800_gpadc_info;
		break;
	case CHIP_PM822:
		gpadc_num = PM822_NUM_GPADC;
		info = pm822_gpadc_info;
		break;
	case CHIP_PM86X:
		gpadc_num = PM860_NUM_GPADC;
		info = pm860_gpadc_info;
		break;
	default:
		pr_err("Cannot find chip type\n");
		return -ENODEV;
	}

	len += sprintf(buf + len, "\nGPADC");

	len += sprintf(buf + len, "\n--------------------------------------");
	len += sprintf(buf + len, "----------------------------------------\n");
	len += sprintf(buf + len, "|    name    | status  | value  |  avg   ");
	len += sprintf(buf + len, "|  min   |  max   |  slp   |bias(uA)|");
	len += sprintf(buf + len, "\n--------------------------------------");
	len += sprintf(buf + len, "----------------------------------------\n");

	for (i = 0; i < gpadc_num; i++) {
		conv = &info[i].conv;
		id = info[i].id;
		len += sprintf(buf + len, "|%-7s %-4s|", info[i].name, info[i].units);
		ret = pm800_gpadc_check_en(map, info[i].enable_reg, info[i].enable_bit);
		if (ret < 0)
			len += sprintf(buf + len, "ERR 0x%-2x |", info[i].enable_reg);
		else
			len += sprintf(buf + len, " %-7s |", PM800_GPADC_CHECK_EN(ret));

		if (!ret) {
			len += sprintf(buf + len, "   %-4s |", "-");
			len += sprintf(buf + len, "   %-4s |", "-");
			len += sprintf(buf + len, "   %-4s |", "-");
			len += sprintf(buf + len, "   %-4s |", "-");
			len += sprintf(buf + len, "   %-4s |", "-");
			len += sprintf(buf + len, "   %-4s |\n", "-");
		} else
			pm800_gpadc_print(map, &info[i], id, buf, &len);
	}

	len += sprintf(buf + len, "--------------------------------------");
	len += sprintf(buf + len, "----------------------------------------\n");

	return len;
}

static int device_gpadc_init(struct pm80x_chip *chip,
				       struct pm80x_platform_data *pdata)
{
	struct pm80x_subchip *subchip = chip->subchip;
	struct regmap *map = subchip->regmap_gpadc;
	int ret = 0;

	if (!map) {
		dev_warn(chip->dev,
			 "Warning: gpadc regmap is not available!\n");
		return -EINVAL;
	}
	/*
	 * initialize GPADC without activating it turn on GPADC
	 * measurments
	 */
	ret = regmap_update_bits(map,
				 PM800_GPADC_MISC_CONFIG2,
				 PM800_GPADC_MISC_GPFSM_EN,
				 PM800_GPADC_MISC_GPFSM_EN);
	if (ret < 0)
		goto out;
	/*
	 * This function configures the ADC as requires for
	 * CP implementation.CP does not "own" the ADC configuration
	 * registers and relies on AP.
	 * Reason: enable automatic ADC measurements needed
	 * for CP to get VBAT and RF temperature readings.
	 */
	ret = regmap_update_bits(map, PM800_GPADC_MEAS_EN1,
				 PM800_MEAS_EN1_VBAT, PM800_MEAS_EN1_VBAT);
	if (ret < 0)
		goto out;

	/* enable all of the GPADC */
	ret = regmap_update_bits(map, PM800_GPADC_MEAS_EN2, 0xff, 0x6c);
	if (ret < 0)
		goto out;

	/*
	 * the defult of PM800 is GPADC operates at 100Ks/s rate
	 * and Number of GPADC slots with active current bias prior
	 * to GPADC sampling = 1 slot for all GPADCs set for
	 * Temprature mesurmants
	 *
	 * enable all of the bias_en/bias_out_en
	 */
	ret = regmap_update_bits(map, PM800_GP_BIAS_ENA1, 0xff, 0xff);
	if (ret < 0)
		goto out;

	dev_info(chip->dev, "pm800 device_gpadc_init: Done\n");
	return 0;

out:
	dev_info(chip->dev, "pm800 device_gpadc_init: Failed!\n");
	return ret;
}

static int device_onkey_init(struct pm80x_chip *chip,
				struct pm80x_platform_data *pdata)
{
	int ret;

	ret = mfd_add_devices(chip->dev, 0, &onkey_devs[0],
			      ARRAY_SIZE(onkey_devs), &onkey_resources[0],
			      regmap_irq_chip_get_base(chip->irq_data), NULL);

	if (ret) {
		dev_err(chip->dev, "Failed to add onkey subdev\n");
		return ret;
	}

	return 0;
}


static int device_usb_init(struct pm80x_chip *chip,
				struct pm80x_platform_data *pdata)
{
	int ret;

	usb_devs[0].platform_data = pdata->usb;
	usb_devs[0].pdata_size = sizeof(struct pm80x_usb_pdata);
	ret = mfd_add_devices(chip->dev, 0, &usb_devs[0],
			      ARRAY_SIZE(usb_devs), NULL,
			      regmap_irq_chip_get_base(chip->irq_data), NULL);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add usb subdev\n");
		return ret;
	}

	return 0;
}

static int device_vibrator_init(struct pm80x_chip *chip,
				struct pm80x_platform_data *pdata)
{
	int ret;

	vibrator_devs[0].platform_data = pdata->vibrator;
	vibrator_devs[0].pdata_size = sizeof(struct pm80x_vibrator_pdata);

	ret = mfd_add_devices(chip->dev, 0, &vibrator_devs[0],
		ARRAY_SIZE(vibrator_devs), NULL,
		regmap_irq_chip_get_base(chip->irq_data), NULL);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to add vibrator subdev\n");
		return ret;
	}

	return 0;
}

static int device_rtc_init(struct pm80x_chip *chip,
				struct pm80x_platform_data *pdata)
{
	int ret;

	rtc_devs[0].platform_data = pdata->rtc;
	rtc_devs[0].pdata_size =
			pdata->rtc ? sizeof(struct pm80x_rtc_pdata) : 0;
	ret = mfd_add_devices(chip->dev, 0, &rtc_devs[0],
			      ARRAY_SIZE(rtc_devs), NULL,
			      regmap_irq_chip_get_base(chip->irq_data), NULL);
	if (ret) {
		dev_err(chip->dev, "Failed to add rtc subdev\n");
		return ret;
	}

	return 0;
}

static int device_battery_init(struct pm80x_chip *chip,
					   struct pm80x_platform_data *pdata)
{
	int ret;

	ret = mfd_add_devices(chip->dev, 0, &bat_devs[0],
			      ARRAY_SIZE(bat_devs), NULL,
			      regmap_irq_chip_get_base(chip->irq_data), NULL);
	if (ret) {
		dev_err(chip->dev, "Failed to add battery subdev\n");
		return ret;
	}

	return 0;
}

static int device_regulator_init(struct pm80x_chip *chip,
					   struct pm80x_platform_data *pdata)
{
	int ret;

	ret = mfd_add_devices(chip->dev, 0, &regulator_devs[0],
			      ARRAY_SIZE(regulator_devs), NULL,
			      regmap_irq_chip_get_base(chip->irq_data), NULL);
	if (ret) {
		dev_err(chip->dev, "Failed to add regulator subdev\n");
		return ret;
	}

	return 0;
}

static int device_headset_init(struct pm80x_chip *chip,
					   struct pm80x_platform_data *pdata)
{
	int ret;

	switch (chip->type) {
	case CHIP_PM800:
		headset_devs_800[0].resources =
			&headset_resources_800[0];
		headset_devs_800[0].num_resources =
			ARRAY_SIZE(headset_resources_800);
		break;
	case CHIP_PM86X:
	case CHIP_PM822:
		headset_devs_800[0].resources =
			&headset_resources_822[0];
		headset_devs_800[0].num_resources =
			ARRAY_SIZE(headset_resources_822);
		break;
	default:
		return -EINVAL;
	}

	headset_devs_800[0].platform_data = pdata->headset;
	ret = mfd_add_devices(chip->dev, 0, &headset_devs_800[0],
			      ARRAY_SIZE(headset_devs_800), NULL,
			      regmap_irq_chip_get_base(chip->irq_data),
			      NULL);
	if (ret) {
		dev_err(chip->dev, "Failed to add headset subdev\n");
		return ret;
	}

	return 0;
}

static int device_dvc_init(struct pm80x_chip *chip,
					   struct pm80x_platform_data *pdata)
{
	int ret;

	ret = mfd_add_devices(chip->dev, 0, &dvc_devs[0],
			      ARRAY_SIZE(dvc_devs), NULL,
			      regmap_irq_chip_get_base(chip->irq_data), NULL);
	if (ret) {
		dev_err(chip->dev, "Failed to add dvc subdev\n");
		return ret;
	}

	return 0;
}

static int device_debug_init(struct pm80x_chip *chip)
{
	return mfd_add_devices(chip->dev, 0, &debug_devs[0],
			       ARRAY_SIZE(debug_devs), NULL, 0, NULL);
}

static int device_irq_init_800(struct pm80x_chip *chip)
{
	struct regmap *map = chip->regmap;
	unsigned long flags = IRQF_ONESHOT;
	int data, mask, ret = -EINVAL;

	if (!map || !chip->irq) {
		dev_err(chip->dev, "incorrect parameters\n");
		return -EINVAL;
	}

	/*
	 * irq_mode defines the way of clearing interrupt. it's read-clear by
	 * default.
	 */
	mask =
	    PM800_WAKEUP2_INV_INT | PM800_WAKEUP2_INT_CLEAR |
	    PM800_WAKEUP2_INT_MASK;

	data = (chip->irq_mode) ?
		PM800_WAKEUP2_INT_WRITE_CLEAR : PM800_WAKEUP2_INT_READ_CLEAR;
	ret = regmap_update_bits(map, PM800_WAKEUP2, mask, data);

	if (ret < 0)
		goto out;

	ret =
	    regmap_add_irq_chip(chip->regmap, chip->irq, flags, -1,
				chip->regmap_irq_chip, &chip->irq_data);

out:
	return ret;
}

static void device_irq_exit_800(struct pm80x_chip *chip)
{
	regmap_del_irq_chip(chip->irq, chip->irq_data);
}

static struct regmap_irq_chip pm800_irq_chip = {
	.name = "88pm800",
	.irqs = pm800_irqs,
	.num_irqs = ARRAY_SIZE(pm800_irqs),

	.num_regs = 4,
	.status_base = PM800_INT_STATUS1,
	.mask_base = PM800_INT_ENA_1,
	.ack_base = PM800_INT_STATUS1,
	.init_ack_masked = true,
	.mask_invert = 1,
};

static int pm800_pages_init(struct pm80x_chip *chip)
{
	struct pm80x_subchip *subchip;
	struct i2c_client *client = chip->client;

	int ret = 0;

	subchip = chip->subchip;
	if (!subchip || !subchip->power_page_addr || !subchip->gpadc_page_addr)
		return -ENODEV;

	/* PM800 block power page */
	subchip->power_page = i2c_new_dummy(client->adapter,
					    subchip->power_page_addr);
	if (subchip->power_page == NULL) {
		ret = -ENODEV;
		goto out;
	}

	if (chip->type == CHIP_PM800 || chip->type == CHIP_PM86X)
		subchip->regmap_power = devm_regmap_init_i2c(
				subchip->power_page,
				&pm800_power_regmap_config);
	else if (chip->type == CHIP_PM822)
		subchip->regmap_power = devm_regmap_init_i2c(
				subchip->power_page,
				&pm822_power_regmap_config);

	if (IS_ERR(subchip->regmap_power)) {
		ret = PTR_ERR(subchip->regmap_power);
		dev_err(chip->dev,
			"Failed to allocate regmap_power: %d\n", ret);
		goto out;
	}

	i2c_set_clientdata(subchip->power_page, chip);

	/* PM800 block GPADC */
	subchip->gpadc_page = i2c_new_dummy(client->adapter,
					    subchip->gpadc_page_addr);
	if (subchip->gpadc_page == NULL) {
		ret = -ENODEV;
		goto out;
	}

	if (chip->type == CHIP_PM800 || chip->type == CHIP_PM86X)
		subchip->regmap_gpadc = devm_regmap_init_i2c(
				subchip->gpadc_page,
				&pm800_gpadc_regmap_config);
	else if (chip->type == CHIP_PM822)
		subchip->regmap_gpadc = devm_regmap_init_i2c(
				subchip->gpadc_page,
				&pm822_gpadc_regmap_config);

	if (IS_ERR(subchip->regmap_gpadc)) {
		ret = PTR_ERR(subchip->regmap_gpadc);
		dev_err(chip->dev,
			"Failed to allocate regmap_gpadc: %d\n", ret);
		goto out;
	}
	i2c_set_clientdata(subchip->gpadc_page, chip);

	/* PM800 block TEST: i2c addr 0x37 */
	subchip->test_page = i2c_new_dummy(client->adapter,
					    subchip->test_page_addr);
	if (subchip->test_page == NULL) {
		ret = -ENODEV;
		goto out;
	}
	subchip->regmap_test = devm_regmap_init_i2c(
			subchip->test_page,
			&pm80x_test_regmap_config);
	if (IS_ERR(subchip->regmap_test)) {
		ret = PTR_ERR(subchip->regmap_test);
		dev_err(chip->dev,
			"Failed to allocate regmap_test: %d\n", ret);
		goto out;
	}
	i2c_set_clientdata(subchip->test_page, chip);

out:
	return ret;
}

static void pm800_pages_exit(struct pm80x_chip *chip)
{
	struct pm80x_subchip *subchip;

	subchip = chip->subchip;

	if (subchip && subchip->power_page)
		i2c_unregister_device(subchip->power_page);

	if (subchip && subchip->gpadc_page)
		i2c_unregister_device(subchip->gpadc_page);

	if (subchip && subchip->test_page) {
		regmap_exit(subchip->regmap_test);
		i2c_unregister_device(subchip->test_page);
	}
}

static int device_800_init(struct pm80x_chip *chip,
				     struct pm80x_platform_data *pdata)
{
	int ret;
	unsigned int val;

	if (!pdata) {
		dev_warn(chip->dev, "pdata is null!!!\n");
		return -EINVAL;
	}
	/*
	 * alarm wake up bit will be clear in device_irq_init(),
	 * read before that
	 */
	ret = regmap_read(chip->regmap, PM800_RTC_CONTROL, &val);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read RTC register: %d\n", ret);
		goto out;
	}
	if (val & PM800_ALARM_WAKEUP) {
		if (pdata && pdata->rtc)
			pdata->rtc->rtc_wakeup = 1;
	}

	ret = regmap_read(chip->regmap, PM800_USER_DATA6, &val);
	if (ret < 0) {
		dev_err(chip->dev, "Failed to read RTC usr reg: %d\n", ret);
		goto out;
	}
	if (val & (1 << 1)) {
		/* clear */
		regmap_update_bits(chip->regmap, PM800_USER_DATA6,
				(1 << 1), (0 << 1));
		if (pdata && pdata->rtc)
			pdata->rtc->rtc_wakeup = 1;
	}

	ret = device_gpadc_init(chip, pdata);
	if (ret < 0) {
		dev_err(chip->dev, "[%s]Failed to init gpadc\n", __func__);
		goto out;
	}

	chip->regmap_irq_chip = &pm800_irq_chip;
	chip->irq_mode = pdata->irq_mode;

	ret = device_irq_init_800(chip);
	if (ret < 0) {
		dev_err(chip->dev, "[%s]Failed to init pm800 irq\n", __func__);
		goto out;
	}

	ret = device_onkey_init(chip, pdata);
	if (ret) {
		dev_err(chip->dev, "Failed to add onkey subdev\n");
		goto out_dev;
	}

	ret = device_rtc_init(chip, pdata);
	if (ret) {
		dev_err(chip->dev, "Failed to add rtc subdev\n");
		goto out;
	}

	ret = device_battery_init(chip, pdata);
	if (ret) {
		dev_err(chip->dev, "Failed to add battery subdev\n");
		goto out;
	}

	ret = device_regulator_init(chip, pdata);
	if (ret) {
		dev_err(chip->dev, "Failed to add regulators subdev\n");
		goto out;
	}

	ret = device_headset_init(chip, pdata);
	if (ret < 0)
		dev_warn(chip->dev, "Failed to add headset subdev\n");

	ret = device_dvc_init(chip, pdata);
	if (ret)
		dev_warn(chip->dev, "Failed to add dvc subdev\n");

	ret = device_usb_init(chip, pdata);
	if (ret)
		dev_warn(chip->dev, "Failed to add usb subdev\n");

	ret = device_vibrator_init(chip, pdata);
	if (ret)
		dev_warn(chip->dev, "Failed to add vibrator subdev\n");

	ret = device_debug_init(chip);
	if (ret) {
		dev_err(chip->dev, "failed to add debug subdev\n");
		goto out_dev;
	}

	return 0;
out_dev:
	mfd_remove_devices(chip->dev);
	device_irq_exit_800(chip);
out:
	return ret;
}

static int pm800_dt_init(struct device_node *np,
			 struct device *dev,
			 struct pm80x_platform_data *pdata)
{
	pdata->irq_mode =
		!of_property_read_bool(np, "marvell,88pm800-irq-write-clear");

	return 0;
}

/*
 * board specfic configurations also parameters from customers,
 * this parameters are passed form board dts file
 */
static int pmic_board_config(struct pm80x_chip *chip, struct device_node *np)
{
	unsigned int page, reg, mask, data;
	const __be32 *values;
	int size, rows, index;

	values = of_get_property(np, "marvell,pmic-board-cfg", &size);
	if (!values) {
		dev_warn(chip->dev, "no valid property for %s\n", np->name);
		return 0;
	}
	size /= sizeof(*values);	/* Number of elements in array */
	rows = size / 4;
	dev_info(chip->dev, "Proceed PMIC board specific configuration (%d items)\n", rows);
	index = 0;

	while (rows--) {
		page = be32_to_cpup(values + index++);
		reg = be32_to_cpup(values + index++);
		mask = be32_to_cpup(values + index++);
		data = be32_to_cpup(values + index++);
		switch (page) {
		case PM800_BASE_PAGE:
			dev_info(chip->dev, "Base page 0x%02x update 0x%02x mask with value 0x%02x\n", reg, mask, data);
			regmap_update_bits(chip->regmap, reg, mask, data);
			break;
		case PM800_POWER_PAGE:
			dev_info(chip->dev, "Power page 0x%02x update 0x%02x mask with value 0x%02x\n", reg, mask, data);
			regmap_update_bits(chip->subchip->regmap_power, reg, mask, data);
			break;
		case PM800_GPADC_PAGE:
			dev_info(chip->dev, "GPADC page 0x%02x update 0x%02x mask with value 0x%02x\n", reg, mask, data);
			regmap_update_bits(chip->subchip->regmap_gpadc, reg, mask, data);
			break;
		default:
			dev_warn(chip->dev, "Unsupported page for %d\n", page);
			break;
		}
	}

	return 0;
}

void parse_powerup_down_log(struct pm80x_chip *chip)
{
	int powerup, powerdown1, powerdown2, bit;
	char *powerup_name[7] = {
		"ONKEY_WAKEUP    ",
		"CHG_WAKEUP      ",
		"EXTON_WAKEUP    ",
		"RSVD            ",
		"RTC_ALARM_WAKEUP",
		"FAULT_WAKEUP    ",
		"BAT_WAKEUP      "
	};
	char *powerdown1_name[8] = {
		"OVER_TEMP ",
		"UV_VSYS1  ",
		"SW_PDOWN  ",
		"FL_ALARM  ",
		"WD        ",
		"LONG_ONKEY",
		"OV_VSYS   ",
		"RTC_RESET "
	};
	char *powerdown2_name[5] = {
		"HYB_DONE   ",
		"UV_VSYS2   ",
		"HW_RESET   ",
		"PGOOD_PDOWN",
		"LONKEY_RTC "
	};
	/*power up log*/
	regmap_read(chip->regmap, PM800_POWER_UP_LOG, &powerup);
	dev_info(chip->dev, "Powerup log 0x%x: 0x%x\n",
		PM800_POWER_UP_LOG, powerup);
	dev_info(chip->dev, " -------------------------------\n");
	dev_info(chip->dev, "|     name(power up) |  status  |\n");
	dev_info(chip->dev, "|--------------------|----------|\n");
	for (bit = 0; bit < 7; bit++)
		dev_info(chip->dev, "|  %s  |    %x     |\n",
			powerup_name[bit], (powerup >> bit) & 1);
	dev_info(chip->dev, " -------------------------------\n");
	/*power down log1*/
	regmap_read(chip->regmap, PM800_POWER_DOWN_LOG1, &powerdown1);
	dev_info(chip->dev, "PowerDW Log1 0x%x: 0x%x\n",
		PM800_POWER_DOWN_LOG1, powerdown1);
	dev_info(chip->dev, " -------------------------------\n");
	dev_info(chip->dev, "| name(power down1)  |  status  |\n");
	dev_info(chip->dev, "|--------------------|----------|\n");
	for (bit = 0; bit < 8; bit++)
		dev_info(chip->dev, "|    %s      |    %x     |\n",
			powerdown1_name[bit], (powerdown1 >> bit) & 1);
	dev_info(chip->dev, " -------------------------------\n");
	/*power down log2*/
	regmap_read(chip->regmap, PM800_POWER_DOWN_LOG2, &powerdown2);
	dev_info(chip->dev, "PowerDW Log2 0x%x: 0x%x\n",
		PM800_POWER_DOWN_LOG2, powerdown2);
	dev_info(chip->dev, " -------------------------------\n");
	dev_info(chip->dev, "|  name(power down2) |  status  |\n");
	dev_info(chip->dev, "|--------------------|----------|\n");
	for (bit = 0; bit < 5; bit++)
		dev_info(chip->dev, "|    %s     |    %x     |\n",
			powerdown2_name[bit], (powerdown2 >> bit) & 1);
	dev_info(chip->dev, " -------------------------------\n");

	/* write to clear */
	regmap_write(chip->regmap, PM800_POWER_DOWN_LOG1, 0xFF);
	regmap_write(chip->regmap, PM800_POWER_DOWN_LOG2, 0xFF);

	/* mask reserved bits and sleep indication */
	powerdown2 &= 0x1E;

	/* keep globals for external usage */
	chip->powerup = powerup;
	chip->powerdown1 = powerdown1;
	chip->powerdown2 = powerdown2;

}

static int pm800_init_config(struct pm80x_chip *chip, struct device_node *np)
{
	int data;
	if (!chip || !chip->regmap || !chip->subchip
	    || !chip->subchip->regmap_power) {
		pr_err("%s:chip is not availiable!\n", __func__);
		return -EINVAL;
	}

	if (np) /*have board specific parameters need to config*/
		pmic_board_config(chip, np);

	/*base page:reg 0xd0.7 = 1 32kHZ generated from XO */
	regmap_read(chip->regmap, PM800_RTC_CONTROL, &data);
	data |= (1 << 7);
	regmap_write(chip->regmap, PM800_RTC_CONTROL, data);

	/* Set internal digital sleep voltage as 1.2V */
	regmap_read(chip->regmap, PM800_LOW_POWER1, &data);
	data &= ~(0xf << 4);
	regmap_write(chip->regmap, PM800_LOW_POWER1, data);
	/* Enable 32Khz-out-3 low jitter XO_LJ = 1 in pm800
	 * Enable 32Khz-out-2 low jitter XO_LJ = 1 in pm822
	 * they are the same bit
	 */
	regmap_read(chip->regmap, PM800_LOW_POWER2, &data);
	data |= (1 << 5);
	regmap_write(chip->regmap, PM800_LOW_POWER2, data);

	switch (chip->type) {
	case CHIP_PM800:
		/* Enable 32Khz-out-from XO 1, 2, 3 all enabled */
		regmap_write(chip->regmap, PM800_RTC_MISC2, 0x2a);
		break;

	case CHIP_PM822:
		/* select 22pF internal capacitance on XTAL1 and XTAL2*/
		regmap_read(chip->regmap, PM800_RTC_MISC6, &data);
		data |= (0x7 << 4);
		regmap_write(chip->regmap, PM800_RTC_MISC6, data);

		/* Enable 32Khz-out-from XO 1, 2 all enabled */
		regmap_write(chip->regmap, PM800_RTC_MISC2, 0xa);

		/* gps use the LDO13 set the current 170mA  */
		regmap_read(chip->subchip->regmap_power,
					PM822_LDO13_CTRL, &data);
		data &= ~(0x3);
		data |= (0x2);
		regmap_write(chip->subchip->regmap_power,
					PM822_LDO13_CTRL, data);
		/* low power config
		 * 1. base_page 0x21, BK_CKSLP_DIS is gated 1ms after sleep mode entry.
		 * 2. base_page 0x23, REF_SLP_EN reference group enter low power mode.
		 *    REF_UVL_SEL set to be 5.6V
		 * 3. base_page 0x50, 0x55 OSC_CKLCK buck FLL is locked
		 * 4. gpadc_page 0x06, GP_SLEEP_MODE MEANS_OFF scale set to be 8
		 *    MEANS_EN_SLP set to 1, GPADC operates in sleep duty cycle mode.
		 */
		regmap_read(chip->regmap, PM800_LOW_POWER2, &data);
		data |= (1 << 4);
		regmap_write(chip->regmap, PM800_LOW_POWER2, data);

		regmap_read(chip->regmap, PM800_LOW_POWER_CONFIG4, &data);
		data |= (1 << 4);
		data &= ~(0x3 < 2);
		regmap_write(chip->regmap, PM800_LOW_POWER_CONFIG4, data);

		regmap_read(chip->regmap, PM800_OSC_CNTRL1, &data);
		data |= (1 << 0);
		regmap_write(chip->regmap, PM800_OSC_CNTRL1, data);

		regmap_read(chip->regmap, PM800_OSC_CNTRL6, &data);
		data &= ~(1 << 0);
		regmap_write(chip->regmap, PM800_OSC_CNTRL6, data);

		regmap_read(chip->subchip->regmap_gpadc, PM800_GPADC_MISC_CONFIG2, &data);
		data |= (0x7 << 4);
		regmap_write(chip->subchip->regmap_gpadc, PM800_GPADC_MISC_CONFIG2, data);

		/*
		 * Enable LDO sleep mode
		 * TODO: GPS and RF module need to test after enable
		 * ldo3 sleep mode may make emmc not work when resume, disable it
		 */
		regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP1, 0xba);
		regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP2, 0xaa);
		regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP3, 0xaa);
		regmap_write(chip->subchip->regmap_power, PM800_LDO_SLP4, 0x0a);

		break;

	case CHIP_PM86X:
		/* enable buck1 dual phase mode*/
		regmap_read(chip->subchip->regmap_power, PM860_BUCK1_MISC,
				&data);
		data |= BUCK1_DUAL_PHASE_SEL;
		regmap_write(chip->subchip->regmap_power, PM860_BUCK1_MISC,
				data);

		/*xo_cap sel bit(4~6)= 100 12pf register:0xe8*/
		regmap_read(chip->regmap, PM860_MISC_RTC3, &data);
		data |= (0x4 << 4);
		regmap_write(chip->regmap, PM860_MISC_RTC3, data);

		/* configure DVC pins according chip stepping */
		switch (chip->chip_id) {
		case CHIP_PM86X_ID_A0:
			/*set gpio4 and gpio5 to be DVC mode for A0 */
			regmap_read(chip->regmap, PM860_GPIO_4_5_CNTRL, &data);
			data |= PM860_GPIO4_GPIO_MODE(7) | PM860_GPIO5_GPIO_MODE(7);
			regmap_write(chip->regmap, PM860_GPIO_4_5_CNTRL, data);

			/* enable SLP_CNT_HD for 88pm860 A0 chip */
			regmap_update_bits(chip->regmap, PM860_A0_SLP_CNT2,
					   PM860_A0_SLP_CNT_HD, PM860_A0_SLP_CNT_HD);
			/* configure gpio2 as RFPA mode */
			regmap_update_bits(chip->regmap, PM800_GPIO_2_3_CNTRL,
					   PM860_GPIO2_MODE_MASK,
					   PM800_GPIO2_GPIO_MODE(0x2));
			/* configure gpio3 as input */
			regmap_update_bits(chip->regmap, PM800_GPIO_2_3_CNTRL,
					   PM860_GPIO3_MODE_MASK,
					   PM800_GPIO3_GPIO_MODE(0x0));
			break;
		case CHIP_PM86X_ID_Z3:
		default:
			/*set gpio3 and gpio4 to be DVC mode */
			regmap_read(chip->regmap, PM860_GPIO_2_3_CNTRL, &data);
			data |= PM860_GPIO3_GPIO_MODE(7);
			regmap_write(chip->regmap, PM860_GPIO_2_3_CNTRL, data);

			regmap_read(chip->regmap, PM860_GPIO_4_5_CNTRL, &data);
			data |= PM860_GPIO4_GPIO_MODE(7);
			regmap_write(chip->regmap, PM860_GPIO_4_5_CNTRL, data);

			/* configure gpio2 as RFPA mode */
			regmap_update_bits(chip->regmap, PM800_GPIO_2_3_CNTRL,
					   PM860_GPIO2_MODE_MASK,
					   PM800_GPIO2_GPIO_MODE(0x2));
			/* configure gpio5 as input */
			regmap_update_bits(chip->regmap, PM800_GPIO_4_5_CNTRL,
					   PM860_GPIO5_MODE_MASK,
					   PM860_GPIO5_GPIO_MODE(0));
			break;
		}

		/* configure gpio0 as 32khz output buffer mode */
		regmap_update_bits(chip->regmap, PM800_GPIO_0_1_CNTRL,
				   PM860_GPIO0_MODE_MASK,
				   PM800_GPIO0_GPIO_MODE(0x4));
		/* configure gpio1 as 32khz output buffer mode */
		regmap_update_bits(chip->regmap, PM800_GPIO_0_1_CNTRL,
				   PM860_GPIO1_MODE_MASK,
				   PM800_GPIO1_GPIO_MODE(0x4));
		break;

	default:
		dev_err(chip->dev, "Unknown device type: %d\n", chip->type);
		break;
	}

	/*
	 * Block wakeup attempts when VSYS rises above
	 * VSYS_UNDER_RISE_TH1, or power off may fail.
	 * it is set to prevent contimuous attempt to power up
	 * incase the VSYS is above the VSYS_LOW_TH threshold.
	 */
	regmap_read(chip->regmap, PM800_RTC_MISC5, &data);
	data |= 0x1;
	regmap_write(chip->regmap, PM800_RTC_MISC5, data);

	/* Enabele LDO and BUCK clock gating in lpm */
	regmap_read(chip->regmap, PM800_LOW_POWER_CONFIG3, &data);
	data |= (1 << 7);
	regmap_write(chip->regmap, PM800_LOW_POWER_CONFIG3, data);
	/*
	 * Disable reference group sleep mode
	 * - to reduce power fluctuation in suspend
	 */
	regmap_read(chip->regmap, PM800_LOW_POWER_CONFIG4, &data);
	data &= ~(1 << 7);
	regmap_write(chip->regmap, PM800_LOW_POWER_CONFIG4, data);

	/* Enable voltage change in pmic, POWER_HOLD = 1 */
	regmap_read(chip->regmap, PM800_WAKEUP1, &data);
	data |= (1 << 7);
	regmap_write(chip->regmap, PM800_WAKEUP1, data);

	/*
	 * Enable buck sleep mode.
	 * But disable buck2 sleep mode here since it's for ddr/emmc, which
	 * is sensitive to the voltage and the chip may have latency before
	 * enter power save mode.
	 */
	regmap_write(chip->subchip->regmap_power, PM800_BUCK_SLP1, 0xbe);
	regmap_write(chip->subchip->regmap_power, PM800_BUCK_SLP2, 0x2);

	/*set buck2 and buck4 driver selection to be full.
	* this bit is now reserved and default value is 0, if want full
	* drive possibility it should be set to 1.
	* In A1 version it will be set to 1 by default.
	*/
	regmap_read(chip->subchip->regmap_power, 0x7c, &data);
	data |= (1 << 2);
	regmap_write(chip->subchip->regmap_power, 0x7c, data);

	regmap_read(chip->subchip->regmap_power, 0x82, &data);
	data |= (1 << 2);
	regmap_write(chip->subchip->regmap_power, 0x82, data);

	/* dump power up/down log */
	parse_powerup_down_log(chip);
	return 0;
}

static struct pm80x_chip *chip_g;

static int pmic_rw_reg(u8 reg, u8 value)
{
	int ret;
	u8 data, buf[2];

	struct i2c_client *client = chip_g->client;
	struct i2c_msg msgs[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = buf,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = &data,
		},
	};

	/*
	 * I2C pins may be in non-AP pinstate, and __i2c_transfer
	 * won't change it back to AP pinstate like i2c_transfer,
	 * so change i2c pins to AP pinstate explicitly here.
	 */
	i2c_pxa_set_pinstate(client->adapter, "default");

	/*
	 * set i2c to pio mode
	 * for in power off sequence, irq has been disabled
	 */
	i2c_set_pio_mode(client->adapter, 1);

	buf[0] = reg;
	ret = __i2c_transfer(client->adapter, msgs, 2);
	if (ret < 0) {
		pr_err("%s read register fails...\n", __func__);
		WARN_ON(1);
		return ret;
	}
	/* issue SW power down */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf[0] = reg;
	msgs[0].buf[1] = data | value;
	ret = __i2c_transfer(client->adapter, msgs, 1);
	if (ret < 0) {
		pr_err("%s write data fails: ret = %d\n", __func__, ret);
		WARN_ON(1);
	}
	return ret;

}

void sw_poweroff(void)
{
	int ret;

	pr_info("turning off power....\n");
	ret = pmic_rw_reg(PM800_WAKEUP1, PM800_SW_PDOWN);
	if (ret < 0)
		pr_err("%s, turn off power fail", __func__);
}

void pmic_reset(void)
{
	int ret;

	/* pmic reset contain two parts: fault wake up and sw_powerdown */
	pr_info("pmic power down/up reset....\n");
	ret = pmic_rw_reg(PM800_RTC_MISC5, PM800_FAULT_WAKEUP_EN | PM800_FAULT_WAKEUP);
	if (ret < 0) {
		pr_err("%s, enbale pmic fault wakeup fail!", __func__);
		return;
	}
	ret = pmic_rw_reg(PM800_WAKEUP1, PM800_SW_PDOWN);
	if (ret < 0)
		pr_err("%s, turn off power fail", __func__);

}

static int reboot_notifier_func(struct notifier_block *this,
		unsigned long code, void *cmd)
{
	int data;
	struct pm80x_chip *chip;

	pr_info("reboot notifier...\n");

	chip = container_of(this, struct pm80x_chip, reboot_notifier);
	if (cmd && (0 == strcmp(cmd, "recovery"))) {
		pr_info("Enter recovery mode\n");
		regmap_read(chip->regmap, 0xef, &data);
		regmap_write(chip->regmap, 0xef, data | 0x1);

	} else {
		regmap_read(chip->regmap, 0xef, &data);
		regmap_write(chip->regmap, 0xef, data & 0xfe);
	}

	if (code != SYS_POWER_OFF) {
		regmap_read(chip->regmap, 0xef, &data);
		/* this bit is for charger server */
		regmap_write(chip->regmap, 0xef, data | 0x2);
	}

	return 0;
}

static int pm800_probe(struct i2c_client *client,
				 const struct i2c_device_id *id)
{
	int ret = 0;
	struct pm80x_chip *chip;
	struct pm80x_platform_data *pdata = client->dev.platform_data;
	struct device_node *node = client->dev.of_node;
	struct pm80x_subchip *subchip;

	if (IS_ENABLED(CONFIG_OF)) {
		if (!pdata) {
			pdata = devm_kzalloc(&client->dev,
					     sizeof(*pdata), GFP_KERNEL);
			if (!pdata)
				return -ENOMEM;
		}
		ret = pm800_dt_init(node, &client->dev, pdata);
		if (ret)
			return ret;
	} else if (!pdata) {
		return -EINVAL;
	}

	/*
	 * RTC in pmic can run even the core is powered off, and user can set
	 * alarm in RTC. When the alarm is time out, the PMIC will power up
	 * the core, and the whole system will boot up. When PMIC driver is
	 * probed, it will read out some register to find out whether this
	 * boot is caused by RTC timeout or not, and it need pass this
	 * information to RTC driver.
	 * So we need rtc platform data to be existed to pass this information.
	 */
	if (!pdata->rtc) {
		pdata->rtc = devm_kzalloc(&client->dev,
					  sizeof(*(pdata->rtc)), GFP_KERNEL);
		if (!pdata->rtc)
			return -ENOMEM;
	}

	ret = pm80x_init(client);
	if (ret) {
		dev_err(&client->dev, "pm800_init fail\n");
		goto out_init;
	}

	chip = i2c_get_clientdata(client);

	/* init subchip for PM800 */
	subchip =
	    devm_kzalloc(&client->dev, sizeof(struct pm80x_subchip),
			 GFP_KERNEL);
	if (!subchip) {
		ret = -ENOMEM;
		goto err_subchip_alloc;
	}

	/* pm800 has 2 addtional pages to support power and gpadc. */
	subchip->power_page_addr = client->addr + 1;
	subchip->gpadc_page_addr = client->addr + 2;
	subchip->test_page_addr =  client->addr + 7;
	chip->subchip = subchip;

	ret = pm800_pages_init(chip);
	if (ret) {
		dev_err(&client->dev, "pm800_pages_init failed!\n");
		goto err_page_init;
	}

	ret = device_800_init(chip, pdata);
	if (ret) {
		dev_err(chip->dev, "Failed to initialize 88pm800 devices\n");
		goto err_device_init;
	}

	if (pdata && pdata->plat_config)
		pdata->plat_config(chip, pdata);
	/*
	 * config registers for pmic.
	 * common registers is configed in pm800_init_config directly,
	 * board specfic registers and configuration is passed
	 * from board specific dts file.
	 */
	if (IS_ENABLED(CONFIG_OF))
		pm800_init_config(chip, node);
	else
		pm800_init_config(chip, NULL);

	chip->reboot_notifier.notifier_call = reboot_notifier_func;

	chip_g = chip;
	pm_power_off = sw_poweroff;
	register_reboot_notifier(&(chip->reboot_notifier));

	return 0;

err_device_init:
	pm800_pages_exit(chip);
err_page_init:
err_subchip_alloc:
	pm80x_deinit();
out_init:
	return ret;
}

static int pm800_remove(struct i2c_client *client)
{
	struct pm80x_chip *chip = i2c_get_clientdata(client);

	mfd_remove_devices(chip->dev);
	device_irq_exit_800(chip);

	pm800_pages_exit(chip);
	pm80x_deinit();

	return 0;
}

void pm800_shutdown(struct i2c_client *client)
{
	struct pm80x_chip *chip = i2c_get_clientdata(client);
	disable_irq(chip->irq);
}

void buck1_audio_mode_ctrl(int on)
{
	int data;
	static int refcount;

	mutex_lock(&audio_mode_mutex);
	if (on) {
		if (refcount == 0) {
			regmap_read(chip_g->regmap, PM800_LOW_POWER_CONFIG5,
				    &data);
			data |= PM800_AUDIO_MODE_ENA;
			regmap_write(chip_g->regmap, PM800_LOW_POWER_CONFIG5,
				     data);
		}
		refcount++;
	} else {
		if (refcount == 1) {
			regmap_read(chip_g->regmap, PM800_LOW_POWER_CONFIG5,
				    &data);
			data &= ~PM800_AUDIO_MODE_ENA;
			regmap_write(chip_g->regmap, PM800_LOW_POWER_CONFIG5,
				     data);
		}
		refcount--;
	}
	mutex_unlock(&audio_mode_mutex);
}
EXPORT_SYMBOL(buck1_audio_mode_ctrl);

/* set buck1 audio mode voltage as 0.8v*/
void set_buck1_audio_mode_vol(u32 vol)
{
	int data;
	/* the proper range is 0~0x54, corresponding voltage is 0.6v~1.8v */
	if (vol >= 0x55)
		return;

	regmap_read(chip_g->subchip->regmap_power, PM800_AUDIO_MODE_CONFIG1,
		    &data);
	data &= ~(0x7f);
	data |= ((vol & 0x7f) | (0x1 << 7));
	regmap_write(chip_g->subchip->regmap_power, PM800_AUDIO_MODE_CONFIG1,
		     data);
}
EXPORT_SYMBOL(set_buck1_audio_mode_vol);

void buck1_sleepvol_control_for_gps(int on)
{
	int data;
	static int data_old;
	static bool get_data_old;
	/*
	 * Helan2 need to set buck1 sleep voltage tobe 0.95v when power on gps,
	 * and set back to the old sleep voltage when power off gps.
	 * This function provide such an interface to satisfy it.
	 */
	if (!get_data_old) {
		regmap_read(chip_g->subchip->regmap_power, PM800_BUCK1_SLEEP,
				&data_old);
		get_data_old = true;
	}
	if (on) {
		/*
		 * this means gps power on
		 * buck1 sleep voltage is set to be 0.95v
		 */
		data = data_old;
		data &= ~(0x7f);
		data |= 0x1c;
		regmap_write(chip_g->subchip->regmap_power, PM800_BUCK1_SLEEP,
				data);
	} else {
		data = data_old;
		regmap_write(chip_g->subchip->regmap_power, PM800_BUCK1_SLEEP,
				data);
	}
}
EXPORT_SYMBOL(buck1_sleepvol_control_for_gps);

void buck2_sleepmode_control_for_wifi(int on)
{
	int data;

	if (on) {
		/*
		 * Disable VBUCK2's sleep mode, allow big current output
		 * even system enter to suspend
		 * As SD8787 use it as 1.8V supply, it would still work
		 * druing suspend and need much current
		 *
		 */
		regmap_read(chip_g->subchip->regmap_power,
				PM800_BUCK_SLP1, &data);
		data = (data & ~PM800_BUCK2_SLP1_MASK) |
			(PM800_BUCK_SLP_PWR_ACT2 << PM800_BUCK2_SLP1_SHIFT);
		regmap_write(chip_g->subchip->regmap_power, PM800_BUCK_SLP1,
				data);
	} else {
		/*
		 * Enable VBUCK2's sleep mode again (Only 5mA ability)
		 * If SD8787 is power off, VBUCK2 sleep mode has not side
		 * impact to Sd8787, but has benifit to whole power consumption
		 */
		regmap_read(chip_g->subchip->regmap_power,
				PM800_BUCK_SLP1, &data);
		data = (data & ~PM800_BUCK2_SLP1_MASK) |
			(PM800_BUCK_SLP_PWR_LOW << PM800_BUCK2_SLP1_SHIFT);
		regmap_write(chip_g->subchip->regmap_power, PM800_BUCK_SLP1,
				data);
	}

}
EXPORT_SYMBOL(buck2_sleepmode_control_for_wifi);

/* return gpadc voltage */
int get_gpadc_volt(struct pm80x_chip *chip, int gpadc_id)
{
	int ret, volt;
	unsigned char buf[2];
	int gp_meas;

	switch (gpadc_id) {
	case PM800_GPADC0:
		gp_meas = PM800_GPADC0_MEAS1;
		break;
	case PM800_GPADC1:
		gp_meas = PM800_GPADC1_MEAS1;
		break;
	case PM800_GPADC2:
		gp_meas = PM800_GPADC2_MEAS1;
		break;
	case PM800_GPADC3:
		gp_meas = PM800_GPADC3_MEAS1;
		break;
	default:
		dev_err(chip->dev, "get GPADC failed!\n");
		return -EINVAL;

	}
	ret = regmap_bulk_read(chip->subchip->regmap_gpadc,
			       gp_meas, buf, 2);
	if (ret < 0) {
		dev_err(chip->dev, "Attention: failed to get volt!\n");
		return -EINVAL;
	}

	volt = ((buf[0] & 0xff) << 4) | (buf[1] & 0x0f);
	dev_dbg(chip->dev, "%s: volt value = 0x%x\n", __func__, volt);
	/* volt = value * 1.4 * 1000 / (2^12) */
	volt = ((volt & 0xfff) * 7 * 100) >> 11;
	dev_dbg(chip->dev, "%s: voltage = %dmV\n", __func__, volt);

	return volt;
}

/* return voltage via bias current from GPADC */
int get_gpadc_bias_volt(struct pm80x_chip *chip, int gpadc_id, int bias)
{
	int volt, data, gp_bias;

	switch (gpadc_id) {
	case PM800_GPADC0:
		gp_bias = PM800_GPADC_BIAS1;
		break;
	case PM800_GPADC1:
		gp_bias = PM800_GPADC_BIAS2;
		break;
	case PM800_GPADC2:
		gp_bias = PM800_GPADC_BIAS3;
		break;
	case PM800_GPADC3:
		gp_bias = PM800_GPADC_BIAS4;
		break;
	default:
		dev_err(chip->dev, "get GPADC failed!\n");
		return -EINVAL;

	}

	/* get the register value */
	if (bias > 76)
		bias = 76;
	if (bias < 1)
		bias = 1;
	bias = (bias - 1) / 5;

	regmap_read(chip->subchip->regmap_gpadc, gp_bias, &data);
	data &= 0xf0;
	data |= bias;
	regmap_write(chip->subchip->regmap_gpadc, gp_bias, data);

	volt = get_gpadc_volt(chip, gpadc_id);
	if ((volt < 0) || (volt > 1400)) {
		dev_err(chip->dev, "%s return %dmV\n", __func__, volt);
		return -EINVAL;
	}

	return volt;
}

/*
 * used by non-pmic driver
 * TODO: remove later
 */
int extern_get_gpadc_bias_volt(int gpadc_id, int bias)
{
	return get_gpadc_bias_volt(chip_g, gpadc_id, bias);
}
EXPORT_SYMBOL(extern_get_gpadc_bias_volt);

static struct i2c_driver pm800_driver = {
	.driver = {
		.name = "88PM800",
		.owner = THIS_MODULE,
		.pm = &pm80x_pm_ops,
		.of_match_table	= of_match_ptr(pm80x_dt_ids),
		},
	.probe = pm800_probe,
	.remove = pm800_remove,
	.shutdown = pm800_shutdown,
	.id_table = pm80x_id_table,
};

static int __init pm800_i2c_init(void)
{
	return i2c_add_driver(&pm800_driver);
}
subsys_initcall(pm800_i2c_init);

static void __exit pm800_i2c_exit(void)
{
	i2c_del_driver(&pm800_driver);
}
module_exit(pm800_i2c_exit);

MODULE_DESCRIPTION("PMIC Driver for Marvell 88PM800");
MODULE_AUTHOR("Qiao Zhou <zhouqiao@marvell.com>");
MODULE_LICENSE("GPL");
