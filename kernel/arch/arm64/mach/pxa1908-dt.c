/*
 *  linux/arch/arm64/mach/pxa1908-dt.c
 *
 *  Copyright (C) 2012 Marvell Technology Group Ltd.
 *  Author: Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/io.h>
#include <linux/of_platform.h>
#include <linux/platform_data/mv_usb.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/features.h>
#include <asm/device_mapping.h>
#include <asm/mach/mach_desc.h>
#include <media/soc_camera.h>
#include <media/mrvl-camera.h>
#include <linux/gpio.h>
#include <linux/memblock.h>
#ifdef CONFIG_SD8XXX_RFKILL
#include <linux/sd8x_rfkill.h>
#endif

#include <linux/regdump_ops.h>
#include <linux/regs-addr.h>
#include "mmpx-dt.h"

#define MHZ_TO_KHZ	1000
#define APB_PHYS_BASE		0xd4000000
#define FREQ_HW_CTRL            0x1
#define CNTCR			0x00	 /* Counter Control Register */
#define CNTCR_EN		(1 << 0) /* The counter is enabled */
#define CNTCR_HDBG		(1 << 1) /* Halt on debug */
#if defined(CONFIG_CRASH_DUMP)
#define PLAT_PHYS_OFFSET        UL(0x06000000)
#elif defined(CONFIG_TZ_HYPERVISOR)
#define PLAT_PHYS_OFFSET        UL(0x01000000)
#else
#define PLAT_PHYS_OFFSET        UL(0x00000000)
#endif

static struct mv_usb_platform_data mmpx_usb_pdata = {
	.mode				= MV_USB_MODE_OTG,
	.extern_attr                    = MV_USB_HAS_VBUS_DETECTION
					| MV_USB_HAS_IDPIN_DETECTION,
	.otg_force_a_bus_req		= 1,
	.disable_otg_clock_gating	= 0,
};

#ifdef CONFIG_SD8XXX_RFKILL
#define WIB_3V3_LDO_EN 36

static void wireless_card_set_power(unsigned int on)
{
	static int enabled;
	int wifi_ldo_en = 0;

	wifi_ldo_en = WIB_3V3_LDO_EN;
	if (gpio_request(wifi_ldo_en, "WIFI LDO EN")) {
		pr_err(KERN_ERR "get wifi_ldo_en failed %s %d\n",
					__func__, __LINE__);
	}
	if (on && !enabled) {
		if (wifi_ldo_en)
			gpio_direction_output(wifi_ldo_en, 1);
		enabled = 1;

	}
	if (!on && enabled) {
		if (wifi_ldo_en)
			gpio_direction_output(wifi_ldo_en, 0);
		enabled = 0;
	}

	buck2_sleepmode_control_for_wifi(on);

	gpio_free(wifi_ldo_en);
	return;
}

struct sd8x_rfkill_platform_data sd8x_rfkill_platdata = {
	.set_power	= wireless_card_set_power,
	};

#endif

/* PXA1908 */
static const struct of_dev_auxdata pxa1908_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("mrvl,mmp-uart", 0xd4017000, "pxa2xx-uart.0", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-uart", 0xd4018000, "pxa2xx-uart.1", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-uart", 0xd4036000, "pxa2xx-uart.2", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4011000, "pxa2xx-i2c.0", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4010800, "pxa2xx-i2c.1", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4037000, "pxa2xx-i2c.2", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4013800, "pxa2xx-i2c.3", NULL),
	OF_DEV_AUXDATA("marvell,pxa25x-pwm", 0xd401a000, "mmp-pwm.0", NULL),
	OF_DEV_AUXDATA("marvell,pxa25x-pwm", 0xd401a400, "mmp-pwm.1", NULL),
	OF_DEV_AUXDATA("marvell,pxa25x-pwm", 0xd401a800, "mmp-pwm.2", NULL),
	OF_DEV_AUXDATA("marvell,pxa25x-pwm", 0xd401ac00, "mmp-pwm.3", NULL),
	OF_DEV_AUXDATA("marvell,mmp-gpio", 0xd4019000, "mmp-gpio", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-edge-wakeup", 0xd4019800, "mmp-edge-wakeup", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-rtc", 0xd4010000, "sa1100-rtc", NULL),
	OF_DEV_AUXDATA("marvell,pdma-1.0", 0xd4000000, "mmp-pdma", NULL),
	OF_DEV_AUXDATA("marvell,pxa27x-keypad", 0xd4012000, "pxa27x-keypad", NULL),
	OF_DEV_AUXDATA("marvell,usb2-phy-40lp", 0xd4207000,
			"mv-usb-phy", NULL),
	OF_DEV_AUXDATA("marvell,usb2-phy-28lp", 0xd4207000,
			"mv-usb-phy", NULL),
	OF_DEV_AUXDATA("marvell,mv-udc", 0xd4208100, "mv-udc", &mmpx_usb_pdata),
	OF_DEV_AUXDATA("marvell,pxa-u2oehci", 0xd4208100, "pxa-u2oehci", &mmpx_usb_pdata),
	OF_DEV_AUXDATA("marvell,mv-otg", 0xd4208100, "mv-otg", &mmpx_usb_pdata),
	OF_DEV_AUXDATA("mrvl,mmp-sspa-dai", 0xD128dc00, "mmp-sspa-dai.0", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-sspa-dai", 0xD128dd00, "mmp-sspa-dai.1", NULL),
	OF_DEV_AUXDATA("marvell,adma-1.0", 0xD128D800, "mmp-adma.0", NULL),
	OF_DEV_AUXDATA("marvell,adma-1.0", 0xD128D900, "mmp-adma.1", NULL),
	OF_DEV_AUXDATA("mrvl,pxav3-mmc-v2", 0xd4280000, "sdhci-pxav3.0", NULL),
	OF_DEV_AUXDATA("mrvl,pxav3-mmc-v2", 0xd4280800, "sdhci-pxav3.1", NULL),
	OF_DEV_AUXDATA("mrvl,pxav3-mmc-v2", 0xd4281000, "sdhci-pxav3.2", NULL),
#ifdef CONFIG_VIDEO_MV_SC2_MMU
	OF_DEV_AUXDATA("marvell,mmp-sc2mmu", 0xd420F000, "mv_sc2_mmu.0", NULL),
#endif
#ifdef CONFIG_VIDEO_MV_SC2_CCIC
	OF_DEV_AUXDATA("marvell,mmp-sc2ccic", 0xd420a000, "mv_sc2_ccic.0", NULL),
	OF_DEV_AUXDATA("marvell,mmp-sc2ccic", 0xd420a800, "mv_sc2_ccic.1", NULL),
#else
	OF_DEV_AUXDATA("marvell,mmp-ccic", 0xd420a000, "mmp-camera.0", NULL),
	OF_DEV_AUXDATA("marvell,mmp-ccic", 0xd420a800, "mmp-camera.1", NULL),
#endif
	OF_DEV_AUXDATA("marvell,mmp-disp", 0xd420b000, "mmp-disp", NULL),
	OF_DEV_AUXDATA("marvell,mmp-fb", 0, "mmp-fb", NULL),
	OF_DEV_AUXDATA("marvell,mmp-fb-overlay", 0, "mmp-fb-overlay.0", NULL),
	OF_DEV_AUXDATA("marvell,mmp-fb-overlay", 1, "mmp-fb-overlay.1", NULL),
	OF_DEV_AUXDATA("marvell,mmp-fb-overlay", 2, "mmp-fb-overlay.2", NULL),
	OF_DEV_AUXDATA("marvell,mmp-nt35565", 0, "mmp-nt35565", NULL),
	OF_DEV_AUXDATA("marvell,mmp-lg4591", 0, "mmp-lg4591", NULL),
	OF_DEV_AUXDATA("marvell,mmp-r63311", 0, "mmp-r63311", NULL),
	OF_DEV_AUXDATA("marvell,mmp-dsi", 0xd420b800, "mmp-dsi", NULL),
	OF_DEV_AUXDATA("marvell,pxa910-squ", 0xd42a0800, "pxa910-squ", NULL),
	OF_DEV_AUXDATA("mrvl,pxa910-ssp", 0xd401b000, "pxa988-ssp.0", NULL),
	OF_DEV_AUXDATA("mrvl,pxa910-ssp", 0xd42a0c00, "pxa988-ssp.1", NULL),
	OF_DEV_AUXDATA("mrvl,pxa910-ssp", 0xd4039000, "pxa988-ssp.4", NULL),
	OF_DEV_AUXDATA("mrvl,pxa-ssp-dai", 1, "pxa-ssp-dai.1", NULL),
	OF_DEV_AUXDATA("mrvl,pxa-ssp-dai", 4, "pxa-ssp-dai.2", NULL),
	OF_DEV_AUXDATA("marvell,pxa-88pm805-snd-card", 0, "sound", NULL),
	OF_DEV_AUXDATA("marvell,pxa28nm-thermal", 0xd4013300,
			"pxa28nm-thermal", NULL),
	OF_DEV_AUXDATA("pxa-ion", 0, "pxa-ion", NULL),
#ifdef CONFIG_PXA_THERMAL
	OF_DEV_AUXDATA("marvell,pxa-thermal", 0xd4013200, "pxa-thermal", NULL),
#endif
#ifdef CONFIG_SD8XXX_RFKILL
	OF_DEV_AUXDATA("mrvl,sd8x-rfkill", 0, "sd8x-rfkill",
		       &sd8x_rfkill_platdata),
#endif
	OF_DEV_AUXDATA("marvell,mmp-gps", 0, "mmp-gps", NULL),
	{}
};

#define APMU_SDH0      0x54
static void __init pxa988_sdhc_reset_all(void)
{
	unsigned int reg_tmp;

	/* use bit0 to reset all 3 sdh controls */
	reg_tmp = __raw_readl(get_apmu_base_va() + APMU_SDH0);
	__raw_writel(reg_tmp & (~1), get_apmu_base_va() + APMU_SDH0);
	usleep_range(10, 11);
	__raw_writel(reg_tmp | (1), get_apmu_base_va() + APMU_SDH0);
}

void __init pxa1908_dt_init_machine(void)
{
	helan2_clk_init();
	pxa988_sdhc_reset_all();
	of_platform_populate(NULL, of_default_bus_match_table,
			     pxa1908_auxdata_lookup, &platform_bus);
}

static void __init pxa_enable_external_agent(void __iomem *addr)
{
	u32 tmp;

	tmp = readl_relaxed(addr);
	tmp |= 0x100000;
	writel_relaxed(tmp, addr);
}

static int __init pxa_external_agent_init(void)
{
	/* if enable TrustZone, move core config to TZSW. */
	void __iomem *ciu_cpu_core0_conf = axi_virt_base + 0x82cd0;
	void __iomem *ciu_cpu_core1_conf = axi_virt_base + 0x82ce0;
	void __iomem *ciu_cpu_core2_conf = axi_virt_base + 0x82cf0;
	void __iomem *ciu_cpu_core3_conf = axi_virt_base + 0x82cf8;
#ifndef CONFIG_TZ_HYPERVISOR
	if (has_feat_enable_cti()) {
		/* enable access CTI registers for core */
		pxa_enable_external_agent((void __iomem *)ciu_cpu_core0_conf);
		pxa_enable_external_agent((void __iomem *)ciu_cpu_core1_conf);
		pxa_enable_external_agent((void __iomem *)ciu_cpu_core2_conf);
		pxa_enable_external_agent((void __iomem *)ciu_cpu_core3_conf);
	}
#endif

	return 0;
}
core_initcall(pxa_external_agent_init);


#define MPMU_APRR		(0x1020)
#define MPMU_WDTPCR		(0x0200)
/* wdt and cp use the clock */
void enable_pxawdt_clock(void)
{
	void __iomem *mpmu_base;
	void __iomem *mpmu_wdtpcr;
	void __iomem *mpmu_aprr;
	mpmu_base = ioremap(APB_PHYS_BASE + 0x50000, SZ_4K);
	mpmu_aprr = mpmu_base + MPMU_APRR;
	mpmu_wdtpcr = mpmu_base + MPMU_WDTPCR;

	/* reset/enable WDT clock */
	writel(0x7, mpmu_wdtpcr);
	readl(mpmu_wdtpcr);
	writel(0x3, mpmu_wdtpcr);
	return;
}

static __init void enable_arch_timer(void)
{
	u32 tmp;
	void __iomem *apbc_counter_clk_sel = apb_virt_base + 0x15064;
	void __iomem *generic_counter_virt_base = apb_virt_base + 0x101000;
	tmp = readl(apbc_counter_clk_sel);
	/* Default is 26M/32768 = 0x319 */
	if ((tmp >> 16) != 0x319) {
		pr_warn("Generic Counter step of Low Freq is not right\n");
		return;
	}
	/* bit0 = 1: Generic Counter Frequency control by hardware VCTCXO_EN
	   VCTCXO_EN = 1, Generic Counter Frequency is 26Mhz;
	   VCTCXO_EN = 0, Generic Counter Frequency is 32KHz */
	writel(tmp | FREQ_HW_CTRL, apbc_counter_clk_sel);

	/* NOTE: can not read CNTCR before write, otherwise write will fail
	   Halt on debug;
	   start the counter */
	writel(CNTCR_HDBG | CNTCR_EN, (generic_counter_virt_base + CNTCR));
}

void __init pxa1908_arch_timer_init(void)
{
	enable_pxawdt_clock();

#ifdef CONFIG_ARM_ARCH_TIMER
	enable_arch_timer();
#endif
	clocksource_of_init();
}

/* For HELANLTE CP memeory reservation, 32MB by default */
static u32 cp_area_size = 0x02000000;
static u32 cp_area_addr = 0x06000000;

static int __init early_cpmem(char *p)
{
	char *endp;

	cp_area_size = memparse(p, &endp);
	if (*endp == '@')
		cp_area_addr = memparse(endp + 1, NULL);

	return 0;
}
early_param("cpmem", early_cpmem);

static void pxa_reserve_cp_memblock(void)
{
	/* Reserve memory for CP */
	BUG_ON(memblock_reserve(cp_area_addr, cp_area_size) != 0);
	pr_info("Reserved CP memory: 0x%x@0x%x\n", cp_area_size, cp_area_addr);
}

static u32 m3shm_area_size;
static u32 m3shm_area_addr;

static int __init early_m3shmmem(char *p)
{
	char *endp;

	m3shm_area_size = memparse(p, &endp);
	if (*endp == '@')
		m3shm_area_addr = memparse(endp + 1, NULL);

	return 0;
}
early_param("m3shmmem", early_m3shmmem);

static void pxa_reserve_m3shm_memblock(void)
{
	if (m3shm_area_size) {
		/* Reserve memory for m3shm */
		BUG_ON(memblock_reserve(m3shm_area_addr, m3shm_area_size) != 0);
		pr_info("Reserved m3shm memory: 0x%x@0x%x\n",
			m3shm_area_size, m3shm_area_addr);
	}
}

static void pxa_reserve_obmmem(void)
{
	/* Reserve 1MB memory for obm */
	BUG_ON(memblock_reserve(PLAT_PHYS_OFFSET, 0x100000) != 0);
	memblock_free(PLAT_PHYS_OFFSET, 0x100000);
	memblock_remove(PLAT_PHYS_OFFSET, 0x100000);
	pr_info("Reserved OBM memory: 0x%x@0x%lx\n",
			0x100000, PLAT_PHYS_OFFSET);
}

static u32 m3acq_area_size;
static u32 m3acq_area_addr;

static int __init early_m3acqmem(char *p)
{
	char *endp;

	m3acq_area_size = memparse(p, &endp);
	if (*endp == '@')
		m3acq_area_addr = memparse(endp + 1, NULL);

	return 0;
}
early_param("m3acqmem", early_m3acqmem);

static void pxa_reserve_m3acq_memblock(void)
{
	if (m3acq_area_size) {
		BUG_ON(memblock_reserve(m3acq_area_addr, m3acq_area_size) != 0);
		pr_info("Reserved m3acq memory: 0x%x@0x%x\n",
				m3acq_area_size, m3acq_area_addr);
	}
}

void __init pxa1908_reserve(void)
{
	pxa_reserve_obmmem();

	pxa_reserve_cp_memblock();
	pxa_reserve_m3acq_memblock();
	pxa_reserve_m3shm_memblock();
}
