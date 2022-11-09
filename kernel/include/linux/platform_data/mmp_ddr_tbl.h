/*
 * pxa1928 core clock framework source file
 *
 * Copyright (C) 2013 Marvell
 * Yifan Zhang <zhangyf@marvell.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */


enum dclk_sel {
	DDR_PLL1_416 = 0,
	DDR_PLL1_624,
	DDR_PLL1_1248,
	DDR_PLL5,
	/* DDR PLL4 no div, fake one to allign w/ pp table form */
	DDR_PLL4,
};

struct pxa1928_ddr_opt {
	unsigned int dclk;		/* ddr clock */
	unsigned int ddr_tbl_index;	/* ddr FC table index */
	enum dclk_sel ddr_clk_sel;	/* ddr src sel val */
	unsigned int dclk_div;		/* ddr clk divider */
	unsigned int src_freq;
	unsigned int ddr_freq_level;
	unsigned int rtcwtc;
	unsigned int rtcwtc_lvl;
	unsigned int axi_constraint;
};

