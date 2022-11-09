/*
 * linux/arch/arm/mach-mmp/coresight-v7.c
 *
 * Author:	Neil Zhang <zhangwm@marvell.com>
 * Copyright:	(C) 2012 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/delay.h>
#include <linux/percpu.h>
#include <linux/features.h>

#include <asm/io.h>

#include <mach/regs-ciu.h>
#include <mach/regs-coresight.h>

struct coresight_info {
	u32     tpiu_ffcr;	/* offset: 0x304 */
	u32     etb_ffcr;	/* offset: 0x304 */
	u32     etb_ctrl;	/* offset: 0x20 */
	u32     cstf_pcr;	/* offset: 0x4 */
	u32     cstf_fcr;	/* offset: 0x0 */
};

struct ptm_info {
	u32	ptm_ter;	/* offset: 0x8 */
	u32	ptm_teer;	/* offset: 0x20 */
	u32	ptm_tecr;	/* offset: 0x24 */
	u32	ptm_cstidr;	/* offset: 0x200 */
	u32	ptm_mcr;	/* offset: 0x0 */
};

struct local_etb_info {
	u32     etb_ffcr;	/* offset: 0x304 */
	u32     etb_ctrl;	/* offset: 0x20 */
};

void arch_enable_access(u32 cpu)
{
	writel(0xC5ACCE55, DBG_LAR(cpu));
}

void arch_dump_pcsr(u32 cpu)
{
	u32 val;
	int i;

	pr_emerg("======== dump PCSR for cpu%d ========\n", cpu);
	for (i = 0; i < 8; i++) {
		val = readl(DBG_PCSR(cpu));
		pr_emerg("PCSR of cpu%d is 0x%x\n", cpu, val);
		udelay(10);
	}
}

int arch_halt_cpu(u32 cpu)
{
	u32 timeout, val;

	/* Enable Halt Debug and Instruction Transfer */
	val = readl(DBG_DSCR(cpu));
	val |= (0x1 << 14) | (0x1 << 13);
	writel(val, DBG_DSCR(cpu));

	/* Halt the dest cpu */
	writel(0x1, DBG_DRCR(cpu));

	/* Wait the cpu halted */
	timeout = 10000;
	do {
		val = readl(DBG_DSCR(cpu));
		if (val & 0x1)
			break;
	} while (--timeout);

	if (!timeout)
		return -1;

	return 0;
}

void arch_insert_inst(u32 cpu)
{
	u32 timeout, val;

	/* Issue an instruction to change the PC of dest cpu to 0 */
	writel(0xE3A0F000, DBG_ITR(cpu));

	/* Wait until the instruction complete */
	timeout = 10000;
	do {
		val = readl(DBG_DSCR(cpu));
		if (val & (0x1 << 24))
			break;
	} while (--timeout);

	if (!timeout)
		pr_emerg("Cannot execute instructions on cpu%d\n", cpu);

	if (val & (0x1 << 6))
		pr_emerg("Occurred exception in debug state on cpu%d\n", cpu);
}

void arch_restart_cpu(u32 cpu)
{
	u32 timeout, val;

	val = readl(DBG_DSCR(cpu));
	val &= ~((0x1 << 14) | (0x1 << 13));
	writel(val, DBG_DSCR(cpu));

	/* Restart dest cpu */
	writel(0x2, DBG_DRCR(cpu));

	timeout = 10000;
	do {
		val = readl(DBG_DSCR(cpu));
		if (val & (0x1 << 1))
			break;
	} while (--timeout);

	if (!timeout)
		pr_emerg("Cannot restart cpu%d\n", cpu);
}

/* The following CTI related operations are needed by Pixiu */
struct cti_info {
	u32	cti_ctrl;	/* offset: 0x0 */
	u32	cti_en_in1;	/* offset: 0x24 */
	u32	cti_en_out6;	/* offset: 0xb8 */
};

static DEFINE_PER_CPU(struct cti_info, cpu_cti_info);

static inline void cti_enable_access(void)
{
	writel_relaxed(0xC5ACCE55, CTI_LOCK);
}

static void coresight_cti_save(void)
{
	struct cti_info *p_cti_info;
	p_cti_info = &per_cpu(cpu_cti_info, smp_processor_id());

	cti_enable_access();
	p_cti_info->cti_ctrl = readl_relaxed(CTI_REG(CTI_CTRL_OFFSET));
	p_cti_info->cti_en_in1 = readl_relaxed(CTI_REG(CTI_EN_IN1_OFFSET));
	p_cti_info->cti_en_out6 = readl_relaxed(CTI_REG(CTI_EN_OUT6_OFFSET));
}

static void coresight_cti_restore(void)
{
	struct cti_info *p_cti_info;
	p_cti_info = &per_cpu(cpu_cti_info, smp_processor_id());

	cti_enable_access();
	writel_relaxed(p_cti_info->cti_en_in1, CTI_REG(CTI_EN_IN1_OFFSET));
	writel_relaxed(p_cti_info->cti_en_out6, CTI_REG(CTI_EN_OUT6_OFFSET));
	writel_relaxed(p_cti_info->cti_ctrl, CTI_REG(CTI_CTRL_OFFSET));
}

/* The following code is needed because our PMU interrupt is routed by CTI */
static void __init mmp_cti_enable(u32 cpu)
{
	void __iomem *cti_base = CTI_CORE0_VIRT_BASE + 0x1000 * cpu;
	u32 tmp;

	/* Unlock CTI */
	writel_relaxed(0xC5ACCE55, cti_base + CTI_LOCK_OFFSET);

	/*
	 * Enables a cross trigger event to the corresponding channel.
	 */
	tmp = readl_relaxed(cti_base + CTI_EN_IN1_OFFSET);
	tmp &= ~CTI_EN_MASK;
	tmp |= 0x1 << cpu;
	writel_relaxed(tmp, cti_base + CTI_EN_IN1_OFFSET);

	tmp = readl_relaxed(cti_base + CTI_EN_OUT6_OFFSET);
	tmp &= ~CTI_EN_MASK;
	tmp |= 0x1 << cpu;
	writel_relaxed(tmp, cti_base + CTI_EN_OUT6_OFFSET);

	/* Enable CTI */
	writel_relaxed(0x1, cti_base + CTI_CTRL_OFFSET);
}

static int __init mmp_cti_init(void)
{
	int cpu;
	if (!has_feat_enable_cti())
		return 1;

	for (cpu = 0; cpu < nr_cpu_ids; cpu++)
		mmp_cti_enable(cpu);

	return 0;
}

void mmp_pmu_ack(void)
{
	writel_relaxed(0x40, CTI_REG(CTI_INTACK_OFFSET));
}
EXPORT_SYMBOL(mmp_pmu_ack);

#ifdef CONFIG_CORESIGHT_TRACE_SUPPORT

static u32 etm_enable_mask = (1 << CONFIG_NR_CPUS) - 1;
static int __init __init_etm_trace(char *arg)
{
	u32 cpu_mask;

	if (!get_option(&arg, &cpu_mask))
		return 0;

	etm_enable_mask &= cpu_mask;

	return 1;
}
__setup("etm_trace=", __init_etm_trace);

static DEFINE_PER_CPU(struct ptm_info, cpu_ptm_info);

static struct coresight_info cst_info;

/* The following operations are needed by XDB */
static inline void ptm_enable_access(void)
{
	writel_relaxed(0xC5ACCE55, PTM_LOCK);
}

static inline void ptm_disable_access(void)
{
	writel_relaxed(0x0, PTM_LOCK);
}

static void coresight_ptm_save(void)
{
	struct ptm_info *p_ptm_info;
	p_ptm_info = &per_cpu(cpu_ptm_info, smp_processor_id());

	ptm_enable_access();
	p_ptm_info->ptm_ter = readl_relaxed(PTM_REG(0x8));
	p_ptm_info->ptm_teer = readl_relaxed(PTM_REG(0x20));
	p_ptm_info->ptm_tecr = readl_relaxed(PTM_REG(0x24));
	p_ptm_info->ptm_cstidr = readl_relaxed(PTM_REG(0x200));
	p_ptm_info->ptm_mcr = readl_relaxed(PTM_REG(0x0));
	ptm_disable_access();
}

static void coresight_ptm_restore(void)
{
	struct ptm_info *p_ptm_info;
	p_ptm_info = &per_cpu(cpu_ptm_info, smp_processor_id());

	ptm_enable_access();

	if (readl_relaxed(PTM_REG(0x0)) != p_ptm_info->ptm_mcr) {
		writel_relaxed(0x400, PTM_REG(0x0));
		writel_relaxed(p_ptm_info->ptm_ter, PTM_REG(0x8));
		writel_relaxed(p_ptm_info->ptm_teer, PTM_REG(0x20));
		writel_relaxed(p_ptm_info->ptm_tecr, PTM_REG(0x24));
		writel_relaxed(p_ptm_info->ptm_cstidr, PTM_REG(0x200));
		writel_relaxed(p_ptm_info->ptm_mcr, PTM_REG(0x0));
	}

	ptm_disable_access();
}

static DEFINE_PER_CPU(struct local_etb_info, local_etb_info);

static inline void local_etb_enable_access(void)
{
	writel_relaxed(0xC5ACCE55, LOCAL_ETB_REG(0xFB0));
}

static inline void local_etb_disable_access(void)
{
	writel_relaxed(0x0, LOCAL_ETB_REG(0xFB0));
}

static void coresight_local_etb_save(void)
{
	struct local_etb_info *p_local_etb_info;
	p_local_etb_info = &per_cpu(local_etb_info, raw_smp_processor_id());

	local_etb_enable_access();
	p_local_etb_info->etb_ffcr = readl_relaxed(LOCAL_ETB_REG(0x304));
	p_local_etb_info->etb_ctrl = readl_relaxed(LOCAL_ETB_REG(0x20));
	local_etb_disable_access();
}

static void coresight_local_etb_restore(void)
{
	struct local_etb_info *p_local_etb_info;
	p_local_etb_info = &per_cpu(local_etb_info, raw_smp_processor_id());

	local_etb_enable_access();
	writel_relaxed(p_local_etb_info->etb_ffcr, LOCAL_ETB_REG(0x304));
	writel_relaxed(p_local_etb_info->etb_ctrl, LOCAL_ETB_REG(0x20));
	local_etb_disable_access();
}

static inline void coresight_enable_access(void)
{
	writel_relaxed(0xC5ACCE55, CSTF_LOCK);
	writel_relaxed(0xC5ACCE55, TPIU_LOCK);
	writel_relaxed(0xC5ACCE55, ETB_LOCK);
}

static inline void coresight_disable_access(void)
{
	writel_relaxed(0x0, CSTF_LOCK);
	writel_relaxed(0x0, TPIU_LOCK);
	writel_relaxed(0x0, ETB_LOCK);
}

static void coresight_save_mpinfo(void)
{
	coresight_enable_access();
	cst_info.tpiu_ffcr = readl_relaxed(TPIU_REG(0x304));
	cst_info.etb_ffcr = readl_relaxed(ETB_REG(0x304));
	cst_info.cstf_pcr = readl_relaxed(CSTF_REG(0x4));
	cst_info.cstf_fcr = readl_relaxed(CSTF_REG(0x0));
	cst_info.etb_ctrl = readl_relaxed(ETB_REG(0x20));
	coresight_disable_access();
}

static void coresight_restore_mpinfo(void)
{
	coresight_enable_access();
	writel_relaxed(cst_info.tpiu_ffcr, TPIU_REG(0x304));
	writel_relaxed(cst_info.etb_ffcr, ETB_REG(0x304));
	writel_relaxed(cst_info.cstf_pcr, CSTF_REG(0x4));
	writel_relaxed(cst_info.cstf_fcr, CSTF_REG(0x0));
	writel_relaxed(cst_info.etb_ctrl, ETB_REG(0x20));
	coresight_disable_access();
}

static void __init coresight_mp_init(u32 enable_mask)
{
	coresight_enable_access();
	writel_relaxed(0x1000, TPIU_REG(0x304));
	writel_relaxed(0x1, ETB_REG(0x304));
	writel_relaxed(0x0, CSTF_REG(0x4));
	writel_relaxed(enable_mask, CSTF_REG(0x0));
	writel_relaxed(0x1, ETB_REG(0x20));
	coresight_disable_access();
}

static void __init coresight_local_etb_init(u32 cpu)
{
	void __iomem *local_etb_base = ETB_CORE0_VIRT_BASE + 0x1000 * cpu;

	writel_relaxed(0xC5ACCE55, (local_etb_base + 0xFB0));
	writel_relaxed(0x1, local_etb_base + 0x304);
	writel_relaxed(0x1, local_etb_base + 0x20);
	writel_relaxed(0x0, (local_etb_base + 0xFB0));
}

static void __init coresight_ptm_enable(u32 cpu)
{
	void __iomem *ptm_base = PTM_CORE0_VIRT_BASE + 0x1000 * cpu;

	/* enable PTM access first */
	writel_relaxed(0xC5ACCE55, (ptm_base + 0xFB0));

	if (cpu_is_ca7())
		writel_relaxed(0xFFFFFFFF, (ptm_base + 0x300));

	writel_relaxed(0x400, (ptm_base + 0x0));
	writel_relaxed(0x406f, (ptm_base + 0x8));
	writel_relaxed(0x6f, (ptm_base + 0x20));
	writel_relaxed(0x1000000, (ptm_base + 0x24));
	writel_relaxed((cpu + 0x1), (ptm_base + 0x200));

	if (cpu_is_ca7())
		writel_relaxed(0xc940, (ptm_base + 0x0));
	else
		writel_relaxed(0xc000, (ptm_base + 0x0));

	/* disable PTM access */
	writel_relaxed(0x0, (ptm_base + 0xFB0));
}

static void __init coresight_percore_init(u32 cpu)
{
	if (has_feat_local_etb())
		coresight_local_etb_init(cpu);

	coresight_ptm_enable(cpu);

	dsb();
	isb();
}

void __init arch_enable_trace(u32 enable_mask)
{
	int cpu;

	coresight_mp_init(enable_mask);

	for_each_possible_cpu(cpu)
		if (test_bit(cpu, (void *)&enable_mask))
			coresight_percore_init(cpu);
}
#endif

void arch_save_coreinfo(void)
{
	if (has_feat_enable_cti())
		coresight_cti_save();

#ifdef CONFIG_CORESIGHT_TRACE_SUPPORT
	if (has_feat_local_etb())
		coresight_local_etb_save();

	coresight_ptm_save();
#endif
}

void arch_restore_coreinfo(void)
{
	if (has_feat_enable_cti())
		coresight_cti_restore();

#ifdef CONFIG_CORESIGHT_TRACE_SUPPORT
	if (has_feat_local_etb())
		coresight_local_etb_restore();

	coresight_ptm_restore();
	dsb();
	isb();
#endif
}

void arch_save_mpinfo(void)
{
#ifdef CONFIG_CORESIGHT_TRACE_SUPPORT
	coresight_save_mpinfo();
#endif
}

void arch_restore_mpinfo(void)
{
#ifdef CONFIG_CORESIGHT_TRACE_SUPPORT
	coresight_restore_mpinfo();
#endif
}

void __init arch_coresight_init(void)
{
	/* It is needed on emei/helan serials to route PMU interrupt */
	mmp_cti_init();

#ifdef CONFIG_CORESIGHT_TRACE_SUPPORT
	/* enable etm trace by default */
	arch_enable_trace(etm_enable_mask);
#endif

	return;
}
