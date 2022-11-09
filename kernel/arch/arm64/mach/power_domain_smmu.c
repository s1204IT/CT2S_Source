/*
 * IOMMU API for ARM architected SMMU implementations.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) 2013 Marvell
 *
 * Author: Dongjiu Geng <djgeng@marvell.com>
 *
 */
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/cputype.h>
#include <linux/of_platform.h>
#include <linux/uio_coda7542.h>
#include <linux/uio_hantro.h>

bool vpu_pwr_is_on(void)
{
	/* hantro and coda7542 can not work together */
	if (hantro_pwr_refcnt || coda7542_pwr_refcnt)
		return true;
	else
		return false;
}

void enable_smmu_power_domain(struct device *dev, int enable, int flag)
{
	u32 pd_index = 0;
	static struct clk *vpuclk;

	if (cpu_is_pxa1928()) {
		if (of_property_read_u32(dev->of_node, "pd_index", &pd_index)) {
			pr_err("missing pd_index property");
			return;
		}

		if (pd_index != 1)
			return;
		if (unlikely(!vpuclk)) {
			vpuclk = clk_get(NULL, "VPU_AXI_CLK");
			if (unlikely(IS_ERR_OR_NULL(vpuclk))) {
				pr_err("%s: can't get vpu clock\n", __func__);
				return;
			}
		}

		/* first disable vpu axi clock, before power off  */
		if (!enable)
			clk_disable_unprepare(vpuclk);

		hantro_power_switch(enable, flag);

		/* vpu axi clock need to be enabled after power on*/
		if (enable)
			clk_prepare_enable(vpuclk);
	} else if (cpu_is_pxa1908()) {
		if (unlikely(!vpuclk)) {
			vpuclk = clk_get(NULL, "VPUCLK");
			if (unlikely(IS_ERR_OR_NULL(vpuclk))) {
				pr_err("%s: can't get vpu clock\n", __func__);
				return;
			}
		}
		if (enable) {
			clk_prepare_enable(vpuclk);
			coda7542_power_switch(enable, flag);
		} else {
			clk_disable_unprepare(vpuclk);
			coda7542_power_switch(enable, flag);
		}

	}
}
EXPORT_SYMBOL(enable_smmu_power_domain);
