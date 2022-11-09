/*
 *  linux/arch/arm/mach-mmp/pxa1928_ddrtable.c
 *
 *  Copyright: (C) 2013 Marvell Internal Ltd.
 *
 *  This software program is licensed subject to the GNU General Public License
 *  (GPL).Version 2,Jun 1991, available at http://www.fsf.org/copyleft/gpl.html
 *
 *  (C) Copyright 2013 Marvell International Ltd.
 *  All Rights Reserved
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/hrtimer.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/cpufreq.h>
#include <linux/pm_qos.h>
#include <mach/regs-apmu.h>
#include <mach/regs-mcu.h>
#include <mach/pxa1928_ddrtable.h>

#define LPM_DDR_REGTABLE_INDEX		10

/* DDR tbl macro */
#define COMMON_DEF(param)						\
	u32 tmpvalue, tab, ent, map;					\
	u32 *base = (u32 *)param;					\
	do {								\
		tab = 0;						\
		ent = 0;						\
		map = 0;						\
		if (__raw_readl(FIXADDR(base, DMCU_MAP_CS0))		\
					& DMCU_MAP_VALID)		\
			map |= DMCU_CMD_CSSEL_CS0;			\
		if (__raw_readl(FIXADDR(base, DMCU_MAP_CS1))		\
					& DMCU_MAP_VALID)		\
			map |= DMCU_CMD_CSSEL_CS1;			\
	} while (0)

#define BEGIN_TABLE(tabidx)						\
	do {								\
		tab = tabidx;						\
		ent = 0;						\
	} while (0)

#define UPDATE_REG(val, reg)	__raw_writel(val, reg);

#define INSERT_ENTRY_EX(reg, b2c, b2s, pause, last)			\
	do {								\
		if (ent >= 32) {					\
			pr_err("INSERT_ENTRY_EX too much entry\n");	\
		}							\
		if (b2c == 0xFFFFFFFF) {				\
			tmpvalue = b2s;					\
		} else {						\
			tmpvalue = __raw_readl(FIXADDR(base, reg));	\
			tmpvalue = (((tmpvalue) & (~(b2c))) | (b2s));	\
		}							\
		UPDATE_REG(tmpvalue, DMCU_HWTDAT0(base));		\
		tmpvalue = reg;						\
		if (pause)						\
			tmpvalue |= DMCU_HWTPAUSE;			\
		if (last)						\
			tmpvalue |= DMCU_HWTEND;			\
		UPDATE_REG(tmpvalue, DMCU_HWTDAT1(base));		\
		tmpvalue = ((tab << 5) + ent) & 0x3ff;			\
		UPDATE_REG(tmpvalue, DMCU_HWTCTRL(base));		\
		ent++;							\
	} while (0)

#define INSERT_ENTRIES(entries, entcount, pause, last)			\
	do {								\
		u32 li;							\
		for (li = 0; li < entcount; li++) {			\
			if ((li + 1) == entcount) {			\
				INSERT_ENTRY_EX(entries[li].reg,	\
						entries[li].b2c,	\
						entries[li].b2s,	\
						pause, last);		\
			} else {					\
				INSERT_ENTRY_EX(entries[li].reg,	\
						entries[li].b2c,	\
						entries[li].b2s,	\
						0, 0);			\
			}						\
		}							\
	} while (0)

#define INSERT_ENTRY(reg, b2c, b2s) INSERT_ENTRY_EX(reg, b2c, b2s, 0, 0)
#define PAUSE_ENTRY(reg, b2c, b2s) INSERT_ENTRY_EX(reg, b2c, b2s, 1, 0)
#define LAST_ENTRY(reg, b2c, b2s) INSERT_ENTRY_EX(reg, b2c, b2s, 1, 1)

#define ALLBITS (0xFFFFFFFF)
#define NONBITS (0x00000000)

static struct ddr_regtbl mt42l256m32d2lg18_2x156mhz[] = {
	{DMCU_SDRAM_TIMING1, ALLBITS, 0x0030309B},
	{DMCU_SDRAM_TIMING2, ALLBITS, 0x000079E0},
	{DMCU_SDRAM_TIMING3, ALLBITS, 0x18600010},
	{DMCU_SDRAM_TIMING4, ALLBITS, 0x0008009C},
	{DMCU_SDRAM_TIMING5, ALLBITS, 0x000F0039},
	{DMCU_SDRAM_TIMING6, ALLBITS, 0x00150065},
	{DMCU_SDRAM_TIMING7, ALLBITS, 0x30163016},
	{DMCU_SDRAM_TIMING8, ALLBITS, 0x10030202},
	{DMCU_SDRAM_TIMING9, ALLBITS, 0x00000025},
	{DMCU_SDRAM_TIMING10, ALLBITS, 0x080A0307},
	{DMCU_SDRAM_TIMING11, ALLBITS, 0x00403204},
	{DMCU_SDRAM_TIMING12, ALLBITS, 0x20200020},
	{DMCU_SDRAM_TIMING13, ALLBITS, 0x00000403},
	{DMCU_SDRAM_TIMING14, ALLBITS, 0x00000403},
	{DMCU_SDRAM_TIMING15, ALLBITS, 0x00000403},
	{DMCU_SDRAM_TIMING16, ALLBITS, 0x00000403},
};

static struct ddr_regtbl mt42l256m32d2lg18_2x208mhz[] = {
	{DMCU_SDRAM_TIMING1, ALLBITS, 0x0030409B},
	{DMCU_SDRAM_TIMING2, ALLBITS, 0x0000A280},
	{DMCU_SDRAM_TIMING3, ALLBITS, 0x20800015},
	{DMCU_SDRAM_TIMING4, ALLBITS, 0x000B00D0},
	{DMCU_SDRAM_TIMING5, ALLBITS, 0x0013004B},
	{DMCU_SDRAM_TIMING6, ALLBITS, 0x001C0065},
	{DMCU_SDRAM_TIMING7, ALLBITS, 0x401E401E},
	{DMCU_SDRAM_TIMING8, ALLBITS, 0x10040202},
	{DMCU_SDRAM_TIMING9, ALLBITS, 0x00000025},
	{DMCU_SDRAM_TIMING10, ALLBITS, 0x0B0E0409},
	{DMCU_SDRAM_TIMING11, ALLBITS, 0x00504205},
	{DMCU_SDRAM_TIMING12, ALLBITS, 0x30200020},
	{DMCU_SDRAM_TIMING13, ALLBITS, 0x1C140000},
	{DMCU_SDRAM_TIMING14, ALLBITS, 0x1C140000},
	{DMCU_SDRAM_TIMING15, ALLBITS, 0x1C140000},
	{DMCU_SDRAM_TIMING16, ALLBITS, 0x1C140000},
};

static struct ddr_regtbl mt42l256m32d2lg18_2x312mhz[] = {
	{DMCU_SDRAM_TIMING1, ALLBITS, 0x0030509B},
	{DMCU_SDRAM_TIMING2, ALLBITS, 0x0000F3C0},
	{DMCU_SDRAM_TIMING3, ALLBITS, 0x30C00020},
	{DMCU_SDRAM_TIMING4, ALLBITS, 0x00100138},
	{DMCU_SDRAM_TIMING5, ALLBITS, 0x001D0071},
	{DMCU_SDRAM_TIMING6, ALLBITS, 0x00290065},
	{DMCU_SDRAM_TIMING7, ALLBITS, 0x502C502C},
	{DMCU_SDRAM_TIMING8, ALLBITS, 0x10050303},
	{DMCU_SDRAM_TIMING9, ALLBITS, 0x00000025},
	{DMCU_SDRAM_TIMING10, ALLBITS, 0x1014060E},
	{DMCU_SDRAM_TIMING11, ALLBITS, 0x00705307},
	{DMCU_SDRAM_TIMING12, ALLBITS, 0x40200030},
	{DMCU_SDRAM_TIMING13, ALLBITS, 0x1C140000},
	{DMCU_SDRAM_TIMING14, ALLBITS, 0x1C140000},
	{DMCU_SDRAM_TIMING15, ALLBITS, 0x1C140000},
	{DMCU_SDRAM_TIMING16, ALLBITS, 0x1C140000},
};

static struct ddr_regtbl mt42l256m32d2lg18_2x528mhz[] = {
	{DMCU_SDRAM_TIMING1, ALLBITS, 0x0030809B},
	{DMCU_SDRAM_TIMING2, ALLBITS, 0x0001A068},
	{DMCU_SDRAM_TIMING3, ALLBITS, 0x53480036},
	{DMCU_SDRAM_TIMING4, ALLBITS, 0x001B0215},
	{DMCU_SDRAM_TIMING5, ALLBITS, 0x003000C0},
	{DMCU_SDRAM_TIMING6, ALLBITS, 0x00460065},
	{DMCU_SDRAM_TIMING7, ALLBITS, 0x804B804B},
	{DMCU_SDRAM_TIMING8, ALLBITS, 0x10080404},
	{DMCU_SDRAM_TIMING9, ALLBITS, 0x00000025},
	{DMCU_SDRAM_TIMING10, ALLBITS, 0x1B220A17},
	{DMCU_SDRAM_TIMING11, ALLBITS, 0x00C0840C},
	{DMCU_SDRAM_TIMING12, ALLBITS, 0x60200040},
	{DMCU_SDRAM_TIMING13, ALLBITS, 0x1C140000},
	{DMCU_SDRAM_TIMING14, ALLBITS, 0x1C140000},
	{DMCU_SDRAM_TIMING15, ALLBITS, 0x1C140000},
	{DMCU_SDRAM_TIMING16, ALLBITS, 0x1C140000},
};

struct ddr_regtbl *mt42l256m32d2lg18_array[] = {
	mt42l256m32d2lg18_2x156mhz,
	mt42l256m32d2lg18_2x208mhz,
	mt42l256m32d2lg18_2x312mhz,
	mt42l256m32d2lg18_2x528mhz,
};


static struct ddr_regtbl lpddr3_2x156mhz[] = {
	{DMCU_SDRAM_TIMING1, ALLBITS, 0x003030AB},
	{DMCU_SDRAM_TIMING2, ALLBITS, 0x000079E0},
	{DMCU_SDRAM_TIMING3, ALLBITS, 0x18600010},
	{DMCU_SDRAM_TIMING4, ALLBITS, 0x0008009C},
	{DMCU_SDRAM_TIMING5, ALLBITS, 0x000F0039},
	{DMCU_SDRAM_TIMING6, ALLBITS, 0x00150065},
	{DMCU_SDRAM_TIMING7, ALLBITS, 0x50165016},
	{DMCU_SDRAM_TIMING8, ALLBITS, 0x10030303},
	{DMCU_SDRAM_TIMING9, ALLBITS, 0x000000A4},
	{DMCU_SDRAM_TIMING10, ALLBITS, 0x080A0307},
	{DMCU_SDRAM_TIMING11, ALLBITS, 0x00404404},
	{DMCU_SDRAM_TIMING12, ALLBITS, 0x20400040},
	{DMCU_SDRAM_TIMING13, ALLBITS, 0x22140000},
	{DMCU_SDRAM_TIMING14, ALLBITS, 0x22140000},
	{DMCU_SDRAM_TIMING15, ALLBITS, 0x22140000},
	{DMCU_SDRAM_TIMING16, ALLBITS, 0x22140000},
};

static struct ddr_regtbl lpddr3_2x208mhz[] = {
	{DMCU_SDRAM_TIMING1, ALLBITS, 0x003060AB},
	{DMCU_SDRAM_TIMING2, ALLBITS, 0x0000A280},
	{DMCU_SDRAM_TIMING3, ALLBITS, 0x20800015},
	{DMCU_SDRAM_TIMING4, ALLBITS, 0x000B00D0},
	{DMCU_SDRAM_TIMING5, ALLBITS, 0x0013004B},
	{DMCU_SDRAM_TIMING6, ALLBITS, 0x001C0065},
	{DMCU_SDRAM_TIMING7, ALLBITS, 0x501E501E},
	{DMCU_SDRAM_TIMING8, ALLBITS, 0x10040303},
	{DMCU_SDRAM_TIMING9, ALLBITS, 0x000000A4},
	{DMCU_SDRAM_TIMING10, ALLBITS, 0x0B0E0409},
	{DMCU_SDRAM_TIMING11, ALLBITS, 0x00504405},
	{DMCU_SDRAM_TIMING12, ALLBITS, 0x30400040},
	{DMCU_SDRAM_TIMING13, ALLBITS, 0x22140000},
	{DMCU_SDRAM_TIMING14, ALLBITS, 0x22140000},
	{DMCU_SDRAM_TIMING15, ALLBITS, 0x22140000},
	{DMCU_SDRAM_TIMING16, ALLBITS, 0x22140000},
};

static struct ddr_regtbl lpddr3_2x312mhz[] = {
	{DMCU_SDRAM_TIMING1, ALLBITS, 0x003060AB},
	{DMCU_SDRAM_TIMING2, ALLBITS, 0x0000F3C0},
	{DMCU_SDRAM_TIMING3, ALLBITS, 0x30C00020},
	{DMCU_SDRAM_TIMING4, ALLBITS, 0x00100138},
	{DMCU_SDRAM_TIMING5, ALLBITS, 0x001D0071},
	{DMCU_SDRAM_TIMING6, ALLBITS, 0x00290065},
	{DMCU_SDRAM_TIMING7, ALLBITS, 0x502C502C},
	{DMCU_SDRAM_TIMING8, ALLBITS, 0x10050303},
	{DMCU_SDRAM_TIMING9, ALLBITS, 0x000000A4},
	{DMCU_SDRAM_TIMING10, ALLBITS, 0x1014060E},
	{DMCU_SDRAM_TIMING11, ALLBITS, 0x00705407},
	{DMCU_SDRAM_TIMING12, ALLBITS, 0x40400040},
	{DMCU_SDRAM_TIMING13, ALLBITS, 0x22140000},
	{DMCU_SDRAM_TIMING14, ALLBITS, 0x22140000},
	{DMCU_SDRAM_TIMING15, ALLBITS, 0x22140000},
	{DMCU_SDRAM_TIMING16, ALLBITS, 0x22140000},
};

static struct ddr_regtbl lpddr3_2x528mhz[] = {
	{DMCU_SDRAM_TIMING1, ALLBITS, 0x003080AB},
	{DMCU_SDRAM_TIMING2, ALLBITS, 0x0001A068},
	{DMCU_SDRAM_TIMING3, ALLBITS, 0x53480036},
	{DMCU_SDRAM_TIMING4, ALLBITS, 0x001B0215},
	{DMCU_SDRAM_TIMING5, ALLBITS, 0x003000C0},
	{DMCU_SDRAM_TIMING6, ALLBITS, 0x00460065},
	{DMCU_SDRAM_TIMING7, ALLBITS, 0x804B804B},
	{DMCU_SDRAM_TIMING8, ALLBITS, 0x10080404},
	{DMCU_SDRAM_TIMING9, ALLBITS, 0x000000A4},
	{DMCU_SDRAM_TIMING10, ALLBITS, 0x1B220A17},
	{DMCU_SDRAM_TIMING11, ALLBITS, 0x00C0840C},
	{DMCU_SDRAM_TIMING12, ALLBITS, 0x60400040},
	{DMCU_SDRAM_TIMING13, ALLBITS, 0x22140000},
	{DMCU_SDRAM_TIMING14, ALLBITS, 0x22140000},
	{DMCU_SDRAM_TIMING15, ALLBITS, 0x22140000},
	{DMCU_SDRAM_TIMING16, ALLBITS, 0x22140000},
};

struct ddr_regtbl *lpddr3_array[] = {
	lpddr3_2x156mhz,
	lpddr3_2x208mhz,
	lpddr3_2x312mhz,
	lpddr3_2x528mhz,
};

static struct ddr_regtbl_info info[] = {
	{
		.array_size = ARRAY_SIZE(mt42l256m32d2lg18_array),
		.timing_cnt = ARRAY_SIZE(mt42l256m32d2lg18_2x156mhz),
	},
	{
		.array_size = ARRAY_SIZE(lpddr3_array),
		.timing_cnt = ARRAY_SIZE(lpddr3_2x156mhz),
	},
};

static u32 curr_ddr_type = DDR_TYPE_INVALID;

void pxa1928_ddrhwt_select_array(u32 ind)
{
	curr_ddr_type = ind;
}

int pxa1928_ddrhwt_get_freqnr(void)
{
	if (curr_ddr_type == DDR_TYPE_INVALID)
		return -EINVAL;
	return info[curr_ddr_type].array_size;
}

int pxa1928_ddrhwt_get_timingcnt(void)
{
	if (curr_ddr_type == DDR_TYPE_INVALID)
		return -EINVAL;
	return info[curr_ddr_type].timing_cnt;
}

void pxa1928_ddrhwt_lpddr2_h2l(u32 *dmcu, struct ddr_regtbl *newtiming,
				u32 timing_cnt, u32 table_index)
{
	COMMON_DEF(dmcu);
	/*
	 * 1 PMU asserts 'mc_sleep_req' on type 'mc_sleep_type'; MC4 enters
	 *    self-refresh mode and hold scheduler for DDR access
	 * 2 Frequency change
	 *
	 * Step 1-2 should be triggered by PMU upon DDR change request
	 *
	 * Step 3-6 programmed in the 1st table
	 */
	BEGIN_TABLE(table_index);
	/* Halt MC4 scheduler*/
	INSERT_ENTRY(DMCU_CTRL_0, ALLBITS, 0x00010003);
	/* 3 update timing, we use the boot timing which is for high clock */
	INSERT_ENTRIES(newtiming, timing_cnt, 0, 0);

	/* 4 reset master DLL */
	INSERT_ENTRY(DMCU_PHY_CTRL9, ALLBITS, 0x20000000);
	/* 5 update master DLL */
	INSERT_ENTRY(DMCU_PHY_CTRL9, ALLBITS, 0x40000000);
	/* 6. synchronize 2x clock */
	PAUSE_ENTRY(DMCU_PHY_CTRL9, ALLBITS, 0x80000000);

	/*
	 * 7 wake up SDRAM; when first table done (acked) PMU will de-assert
	 *    mc_sleep_req to wake up SDRAM from self-refresh
	 *
	 * 8 update SDRAM mode register, programmed in 2nd table
	 */
	INSERT_ENTRY(DMCU_CTRL_0, ALLBITS, 0x00010002);
	/* 9 do a long ZQ Cal */
	INSERT_ENTRY(DMCU_USER_COMMAND1, ALLBITS, (map | 0x10020001));
	INSERT_ENTRY(DMCU_USER_COMMAND1, ALLBITS, (map | 0x10020002));
	INSERT_ENTRY(DMCU_USER_COMMAND1, ALLBITS, (map | 0x10020003));
	INSERT_ENTRY(DMCU_USER_COMMAND0, ALLBITS, (map | 0x10001000));
	/* resume scheduler*/
	LAST_ENTRY(DMCU_CTRL_0, ALLBITS, 0);
}

void pxa1928_ddrhwt_lpddr2_l2h(u32 *dmcu, struct ddr_regtbl *newtiming,
				u32 timing_cnt, u32 table_index)
{
	COMMON_DEF(dmcu);
	/*
	 * 1 PMU asserts 'mc_sleep_req' on type 'mc_sleep_type'; MC4 enters
	 *    self-refresh mode and hold scheduler for DDR access
	 * 2 Frequency change
	 *
	 * Step 1-2 should be triggered by PMU upon DDR change request
	 *
	 * Step 3-6 programmed in the 1st table
	 */
	BEGIN_TABLE(table_index);
	/* Halt MC4 scheduler*/
	INSERT_ENTRY(DMCU_CTRL_0, ALLBITS, 0x00010003);
	/* 3 update timing, we use the boot timing which is for high clock */
	INSERT_ENTRIES(newtiming, timing_cnt, 1, 0);

	/* 4 reset master DLL */
	INSERT_ENTRY(DMCU_PHY_CTRL9, ALLBITS, 0x20000000);
	/* 5 update master DLL */
	INSERT_ENTRY(DMCU_PHY_CTRL9, ALLBITS, 0x40000000);
	/* 6. synchronize 2x clock */
	PAUSE_ENTRY(DMCU_PHY_CTRL9, ALLBITS, 0x80000000);

	/*
	 * 7 wake up SDRAM; when first table done (acked) PMU will de-assert
	 *    mc_sleep_req to wake up SDRAM from self-refresh
	 *
	 * 8 update SDRAM mode register, programmed in 2nd table
	 */
	INSERT_ENTRY(DMCU_CTRL_0, ALLBITS, 0x00010002);
	/* 9 do a long ZQ Cal */
	INSERT_ENTRY(DMCU_USER_COMMAND1, ALLBITS, (map | 0x10020001));
	INSERT_ENTRY(DMCU_USER_COMMAND1, ALLBITS, (map | 0x10020002));
	INSERT_ENTRY(DMCU_USER_COMMAND1, ALLBITS, (map | 0x10020003));
	INSERT_ENTRY(DMCU_USER_COMMAND0, ALLBITS, (map | 0x10001000));
	/* resume scheduler*/
	LAST_ENTRY(DMCU_CTRL_0, ALLBITS, 0);
}

void pxa1928_ddrhwt_lpddr2_h2l_lowfreq(u32 *dmcu, struct ddr_regtbl *newtiming,
				u32 timing_cnt, u32 table_index)
{
	COMMON_DEF(dmcu);
	/*
	 * 1 PMU asserts 'mc_sleep_req' on type 'mc_sleep_type'; MC4 enters
	 *    self-refresh mode and hold scheduler for DDR access
	 * 2 Frequency change
	 *
	 * Step 1-2 should be triggered by PMU upon DDR change request
	 *
	 * Step 3-6 programmed in the 1st table
	 */
	BEGIN_TABLE(table_index);
	/* Halt MC4 scheduler*/
	INSERT_ENTRY(DMCU_CTRL_0, ALLBITS, 0x00010003);
	/* 3 update timing, we use the boot timing which is for high clock */
	INSERT_ENTRIES(newtiming, timing_cnt, 0, 0);

	/* 4 reset master DLL */
	/* INSERT_ENTRY(DMCU_PHY_CTRL9, ALLBITS, 0x20000000); */
	/* 5 update master DLL */
	INSERT_ENTRY(DMCU_PHY_CTRL9, ALLBITS, 0x40000000);
	/* 6. synchronize 2x clock */
	PAUSE_ENTRY(DMCU_PHY_CTRL9, ALLBITS, 0x80000000);

	/*
	 * 7 wake up SDRAM; when first table done (acked) PMU will de-assert
	 *    mc_sleep_req to wake up SDRAM from self-refresh
	 *
	 * 8 update SDRAM mode register, programmed in 2nd table
	 */
	INSERT_ENTRY(DMCU_CTRL_0, ALLBITS, 0x00010002);
	/* 9 do a long ZQ Cal */
	INSERT_ENTRY(DMCU_USER_COMMAND1, ALLBITS, (map | 0x10020001));
	INSERT_ENTRY(DMCU_USER_COMMAND1, ALLBITS, (map | 0x10020002));
	INSERT_ENTRY(DMCU_USER_COMMAND1, ALLBITS, (map | 0x10020003));
	INSERT_ENTRY(DMCU_USER_COMMAND0, ALLBITS, (map | 0x10001000));
	/* resume scheduler*/
	LAST_ENTRY(DMCU_CTRL_0, ALLBITS, 0);
}

void pxa1928_ddrhwt_lpddr2_l2h_lowfreq(u32 *dmcu, struct ddr_regtbl *newtiming,
				u32 timing_cnt, u32 table_index)
{
	COMMON_DEF(dmcu);
	/*
	 * 1 PMU asserts 'mc_sleep_req' on type 'mc_sleep_type'; MC4 enters
	 *    self-refresh mode and hold scheduler for DDR access
	 * 2 Frequency change
	 *
	 * Step 1-2 should be triggered by PMU upon DDR change request
	 *
	 * Step 3-6 programmed in the 1st table
	 */
	BEGIN_TABLE(table_index);
	/* Halt MC4 scheduler*/
	INSERT_ENTRY(DMCU_CTRL_0, ALLBITS, 0x00010003);
	/* 3 update timing, we use the boot timing which is for high clock */
	INSERT_ENTRIES(newtiming, timing_cnt, 1, 0);

	/* 4 reset master DLL */
	/* INSERT_ENTRY(DMCU_PHY_CTRL9, ALLBITS, 0x20000000); */
	/* 5 update master DLL */
	INSERT_ENTRY(DMCU_PHY_CTRL9, ALLBITS, 0x40000000);
	/* 6. synchronize 2x clock */
	PAUSE_ENTRY(DMCU_PHY_CTRL9, ALLBITS, 0x80000000);

	/*
	 * 7 wake up SDRAM; when first table done (acked) PMU will de-assert
	 *    mc_sleep_req to wake up SDRAM from self-refresh
	 *
	 * 8 update SDRAM mode register, programmed in 2nd table
	 */
	INSERT_ENTRY(DMCU_CTRL_0, ALLBITS, 0x00010002);
	/* 9 do a long ZQ Cal */
	INSERT_ENTRY(DMCU_USER_COMMAND1, ALLBITS, (map | 0x10020001));
	INSERT_ENTRY(DMCU_USER_COMMAND1, ALLBITS, (map | 0x10020002));
	INSERT_ENTRY(DMCU_USER_COMMAND1, ALLBITS, (map | 0x10020003));
	INSERT_ENTRY(DMCU_USER_COMMAND0, ALLBITS, (map | 0x10001000));
	/* resume scheduler*/
	LAST_ENTRY(DMCU_CTRL_0, ALLBITS, 0);
}


void pxa1928_register_table_lpddr2_dll_calibration(u32 *dmcu)
{
	COMMON_DEF(dmcu);

	BEGIN_TABLE(LPM_DDR_REGTABLE_INDEX);

	INSERT_ENTRY(DMCU_CTRL_0, ALLBITS, 0x00000002);

	/* reset master DLL */
	INSERT_ENTRY(DMCU_PHY_CTRL9, ALLBITS, 0x20000000);
	/* update master DLL */
	INSERT_ENTRY(DMCU_PHY_CTRL9, ALLBITS, 0x40000000);
	/* synchronize 2x clock */
	PAUSE_ENTRY(DMCU_PHY_CTRL9, ALLBITS, 0x80000000);

	INSERT_ENTRY(DMCU_USER_COMMAND1, ALLBITS, (map | 0x10020001));
	INSERT_ENTRY(DMCU_USER_COMMAND1, ALLBITS, (map | 0x10020002));

	/* resume scheduler*/
	LAST_ENTRY(DMCU_CTRL_0, (0x1 << 1), 0);
}

void pxa1928_enable_lpm_ddrdll_recalibration(void)
{
	int val;

	val = __raw_readl(APMU_MC_CLK_RES_CTRL);
	val &= ~(0xf << 15);
	val |= (LPM_DDR_REGTABLE_INDEX << 15);
	__raw_writel(val, APMU_MC_CLK_RES_CTRL);
}
