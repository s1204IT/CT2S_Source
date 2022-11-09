/*
 *  linux/arch/arm/mach-mmp/mmpx-dt.c
 *
 *  Copyright (C) 2012 Marvell Technology Group Ltd.
 *  Author: Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

#include <linux/clocksource.h>
#include <linux/clk/mmp.h>
#include <linux/devfreq.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/irqchip.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/slab.h>
#include <linux/usb/phy.h>
#include <linux/usb/mv_usb2_phy.h>
#include <linux/platform_data/mv_usb.h>
#include <linux/platform_data/devfreq-pxa.h>
#include <linux/features.h>
#include <asm/smp_twd.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/time.h>
#include <asm/idmap.h>
#include <mach/irqs.h>
#include <mach/regs-accu.h>
#include <mach/regs-mpmu.h>
#include <mach/regs-timers.h>
#include <mach/addr-map.h>
#include <media/soc_camera.h>
#include <media/mrvl-camera.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdhci.h>
#include <linux/mmc/card.h>
#include <linux/platform_data/pxa_sdhci.h>
#include <dt-bindings/mmc/pxa_sdhci.h>
#include <linux/memblock.h>
#include <linux/pstore_ram.h>
#include <video/mmp_disp.h>
#ifdef CONFIG_GPU_RESERVE_MEM
#include <mach/gpu_mem.h>
#endif
#ifdef CONFIG_SD8XXX_RFKILL
#include <linux/sd8x_rfkill.h>
#endif
#ifdef CONFIG_PXA1986_THERMAL
#include <linux/pxa1986_thermal.h>
#endif

#include "common.h"
#include "reset.h"
#include <linux/regdump_ops.h>
#include <linux/regs-addr.h>

#ifdef CONFIG_PM
#include <linux/kobject.h>
#else
struct kobject *pxa1986_power_kobj;
#endif

#define MHZ_TO_KHZ	1000

static struct mv_usb_platform_data pxa1986_usb_pdata = {
	.mode		= MV_USB_MODE_UDC,
	.extern_attr	= MV_USB_HAS_VBUS_DETECTION
			| MV_USB_HAS_IDPIN_DETECTION,
};

#ifdef CONFIG_DDR_DEVFREQ
static struct devfreq_platform_data mmpx_devfreq_ddr_pdata = {
	.clk_name = "ddr",
};

static struct devfreq_pm_qos_table ddr_freq_qos_table[] = {
	/* list all possible frequency level here */
	{
		.freq = 156000,
		.qos_value = DDR_CONSTRAINT_LVL0,
	},
	{
		.freq = 312000,
		.qos_value = DDR_CONSTRAINT_LVL1,
	},
	{
		.freq = 400000,
		.qos_value = DDR_CONSTRAINT_LVL2,
	},
	{
		.freq = 533000,
		.qos_value = DDR_CONSTRAINT_LVL3,
	},
	{0, 0},
};

static void __init pxa1986_devfreq_ddr_init(void)
{
	unsigned int ddr_freq_num;
	unsigned int *ddr_freq_table;
	unsigned int i;

	ddr_freq_num = get_ddr_op_num();

	ddr_freq_table = kzalloc(sizeof(unsigned int) * ddr_freq_num,
			GFP_KERNEL);
	if (!ddr_freq_table)
		return;

	for (i = 0; i < ddr_freq_num; i++)
		ddr_freq_table[i] = get_ddr_op_rate(i);

	mmpx_devfreq_ddr_pdata.freq_tbl_len = ddr_freq_num;
	mmpx_devfreq_ddr_pdata.freq_table = ddr_freq_table;

	mmpx_devfreq_ddr_pdata.qos_list = ddr_freq_qos_table;
}
#endif

#ifdef CONFIG_VPU_DEVFREQ
static struct devfreq_platform_data devfreq_vpu_pdata = {
	.clk_name = "VPUCLK",
};
static void __init pxa1986_devfreq_vpu_init(void)
{
	unsigned int vpu_freq_num;
	unsigned int *vpu_freq_table;
	unsigned int i;

	struct clk *clk = clk_get_sys(NULL, "VPUCLK");
	if (IS_ERR(clk)) {
		WARN_ON(1);
		return;
	}
	vpu_freq_num =  __clk_periph_get_opnum(clk);

	vpu_freq_table = kzalloc(sizeof(unsigned int) * vpu_freq_num,
				 GFP_KERNEL);
	if (!vpu_freq_table)
		return;
	for (i = 0; i < vpu_freq_num; i++)
		vpu_freq_table[i] = __clk_periph_get_oprate(clk, i) / MHZ_TO_KHZ;
	devfreq_vpu_pdata.freq_tbl_len = vpu_freq_num;
	devfreq_vpu_pdata.freq_table = vpu_freq_table;
}

#endif

#ifdef CONFIG_SD8XXX_RFKILL
#define WIB_3V3_LDO_EN 40
/*
 * PXA1986 uses external ldo for WIFI card 3.3v power supply
 * and this ldo is controlled by gpio40.
 */
static void wireless_card_set_power(unsigned int on)
{
	static int enabled;
	int wifi_ldo_en = 0;

	wifi_ldo_en = WIB_3V3_LDO_EN;
	if (gpio_request(wifi_ldo_en, "WIFI LDO EN")) {
		pr_err("get wifi_ldo_en failed %s %d\n",
				__func__, __LINE__);
		return;
	}
	if (on && !enabled) {
		gpio_direction_output(wifi_ldo_en, 1);
		enabled = 1;

	}
	if (!on && enabled) {
		gpio_direction_output(wifi_ldo_en, 0);
		enabled = 0;
	}

	gpio_free(wifi_ldo_en);
	return;
}

struct sd8x_rfkill_platform_data sd8x_rfkill_platdata = {
	.set_power = wireless_card_set_power,
};
#endif

#ifdef CONFIG_PXA1986_THERMAL
void adir_sdk_fan_set_state(unsigned int on)
{
	static DEFINE_SPINLOCK(fan_lock);
	static int fan_gpio = -1;
	static int enable;
	static unsigned long flags;
	static struct device_node *of;
	spin_lock_irqsave(&fan_lock, flags);
	if (unlikely(!enable)) {
		of = of_find_node_by_path("/cores-fan");
		of_property_read_u32_index(of, "fan-gpio", 1, &fan_gpio);
		pr_info("fan_gpio = %d\n", fan_gpio);
		if (gpio_request(fan_gpio , "fan gpio")) {
			pr_err("%s: gpio request failed\n", __func__);
			goto free_fan_lock;
		}
		if (gpio_direction_output(fan_gpio, 0)) {
			pr_err("%s: gpio_direction_output failed\n", __func__);
			goto free_fan_lock;
		}
		enable = 1;
	}
	if (on) {
		gpio_set_value(fan_gpio, 1);
		pr_info("%s: fan is on\n", __func__);
	} else {
		gpio_set_value(fan_gpio, 0);
		pr_info("%s: fan is off\n", __func__);
	}

free_fan_lock:
	spin_unlock_irqrestore(&fan_lock, flags);
}

struct pxa1986_thermal_cooling_dev_info cool_dev_info[] = {
	{"cpu_cooldev1", adir_sdk_fan_set_state},
	{"cpu_cooldev2", NULL},
	{"gpu_cooldev1", adir_sdk_fan_set_state},
	{"gpu_cooldev2", NULL},
	{"cl_cooldev1", NULL},
	{"cl_cooldev2", NULL},
};

struct pxa1986_thermal_platform_data tsu1_data = {
	.name = "cpu_thermal",
	.trip_points = {
		{ 75, "cpu_cooldev1"},
		{ 100, "cpu_cooldev2"},
		{ 115, ""},
	}
};

struct pxa1986_thermal_platform_data tsu2_data = {
	.name = "gpu_thermal_1",
	.trip_points = {
		{ 75, "gpu_cooldev1"},
		{ 85, "gpu_cooldev2"},
		{ 100, ""},
	}
};

struct pxa1986_thermal_platform_data tsu3_data = {
	.name = "gpu_thermal_2",
	.trip_points = {
		{ 75, "gpu_cooldev1"},
		{ 85, "gpu_cooldev2"},
		{ 100, ""},
	}
};
#endif

/* PXA1986 */
static const struct of_dev_auxdata pxa1986_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("marvell,pdma-1.0", 0xd4000000, "mmp-pdma.0", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-uart", 0xd4030000, "pxa2xx-uart.0", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-uart", 0xd4017000, "pxa2xx-uart.1", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd430b000, "pxa910-i2c.0", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4011000, "pxa910-i2c.1", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd401b000, "pxa910-i2c.2", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4032000, "pxa910-i2c.3", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4033000, "pxa910-i2c.4", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-twsi", 0xd4034000, "pxa910-i2c.5", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-rtc", 0xd4086000, "sa1100-rtc", NULL),
	OF_DEV_AUXDATA("marvell,pxa27x-keypad", 0xd4085000,
			"pxa27x-keypad", NULL),
	OF_DEV_AUXDATA("mrvl,mmp-sspa-dai", 0xc0204000, "mmp-sspa-dai.0", NULL),
	OF_DEV_AUXDATA("marvell,mmp-disp", 0xd420b000, "mmp-disp", NULL),
	OF_DEV_AUXDATA("marvell,mmp-fb", 0, "mmp-fb", NULL),
	OF_DEV_AUXDATA("marvell,mmp-fb-overlay", 0, "mmp-fb-overlay", NULL),
	OF_DEV_AUXDATA("marvell,mmp-tft-10801920-1e", 0, "mmp-tft-10801920-1e", NULL),
	OF_DEV_AUXDATA("marvell,mmp-dsi", 0xd420b800, "mmp-dsi", NULL),
	OF_DEV_AUXDATA("marvell,adma-1.0", 0xc0200000, "mmp-adma.0", NULL),
	OF_DEV_AUXDATA("marvell,mmp-88pm805-snd-card", 0, "sound", NULL),
#ifdef CONFIG_MV_USB2_PHY
	OF_DEV_AUXDATA("marvell,usb2-phy-28lp", 0xd4220000,
			"pxa1986-usb-phy", NULL),
#endif
#ifdef CONFIG_USB_MV_UDC
	OF_DEV_AUXDATA("marvell,mv-udc", 0xd4208100, "mv-udc", &pxa1986_usb_pdata),
#endif
#ifdef CONFIG_USB_MV_OTG
	OF_DEV_AUXDATA("marvell,mv-otg", 0xd4208100, "mv-otg", &pxa1986_usb_pdata),
#endif
#ifdef CONFIG_VPU_DEVFREQ
	OF_DEV_AUXDATA("marvell,devfreq-vpu", 0xf0400000,
			"devfreq-vpu.0", (void *)&pxa1928_devfreq_vpu_pdata[0]),
	OF_DEV_AUXDATA("marvell,devfreq-vpu", 0xf0400800,
			"devfreq-vpu.1", (void *)&pxa1928_devfreq_vpu_pdata[1]),
#endif
#ifdef CONFIG_SOC_CAMERA_S5K8AA
	OF_DEV_AUXDATA("soc-camera-pdrv", 0, "soc-camera-pdrv.0", &s5k8aa_desc),
#endif
	OF_DEV_AUXDATA("marvell,mmp-ccic", 0xd420a800, "mmp-camera.1", NULL),
	OF_DEV_AUXDATA("pxa-ion", 0, "pxa-ion", NULL),
#ifdef CONFIG_MMC_SDHCI_PXAV3
	OF_DEV_AUXDATA("mrvl,pxav3-mmc", 0xd4280000, "sdhci-pxav3.0", NULL),
	OF_DEV_AUXDATA("mrvl,pxav3-mmc", 0xd4281000, "sdhci-pxav3.1", NULL),
	OF_DEV_AUXDATA("mrvl,pxav3-mmc", 0xd4281800, "sdhci-pxav3.2", NULL),
#endif
#ifdef CONFIG_PXA1986_THERMAL
	OF_DEV_AUXDATA("mrvl,pxa1986-thermal", 0xd4013200,
			"pxa1986-thermal.0", &tsu1_data),
	OF_DEV_AUXDATA("mrvl,pxa1986-thermal", 0xd4013218,
			"pxa1986-thermal.1", &tsu2_data),
	OF_DEV_AUXDATA("mrvl,pxa1986-thermal", 0xd4013230,
			"pxa1986-thermal.2", &tsu3_data),
#endif
#ifdef CONFIG_SD8XXX_RFKILL
	OF_DEV_AUXDATA("mrvl,sd8x-rfkill", 0, "sd8x-rfkill",
				&sd8x_rfkill_platdata),
#endif
#ifdef CONFIG_MMP_GPS
	OF_DEV_AUXDATA("marvell,mmp-gps", 0, "mmp-gps", NULL),
#endif
	{}
};

static void __init pxa1986_dt_irq_init(void)
{
	irqchip_init();
}

static ssize_t cp_store(struct kobject *kobj, struct kobj_attribute *attr,
			const char *buf, size_t len)
{
/*TEMP WA for release comm from reset*/
#define CR5_BASE_ADDR	(0x2c00)
#define MSA_RESET_BIND	(0x2c08)

	u32 mpmu_cpctl;
	u32 cr5_addr;
	u32 msa_bind;
	char command[10];

	sscanf(buf, "%s", &command[0]);
	mpmu_cpctl = readl_relaxed(get_mpmu_base_va() + MPMU_CPCTL);
	cr5_addr = 0x41300000;
	msa_bind = 0x0;

	if (!strncmp(command, "REL", 3))
		mpmu_cpctl |= 0x3;
	else if (!strncmp(command, "SS_RST", 6))
		mpmu_cpctl &= ~0x12;
	else if (!strncmp(command, "POR_RST", 7))
		mpmu_cpctl &= ~0x11;
	else {
		pr_err("unknown command!!! error\n");
		return -1;
	}
	/* Set cr5 reset address */
	writel_relaxed(cr5_addr, get_mccu_base_va() + CR5_BASE_ADDR);
	/* Set GB reset to be aligned with cr5 reset */
	writel_relaxed(msa_bind, get_mccu_base_va() + MSA_RESET_BIND);
	/* Release cp from reset */
	writel_relaxed(mpmu_cpctl, get_mpmu_base_va() + MPMU_CPCTL);

	return len;
}

static struct kobj_attribute cp_attr = {
	.attr = {
		.name = __stringify(cp),
		.mode = 0200,
	},
	.store = cp_store,
};

static struct attribute *g[] = {
	&cp_attr.attr,
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = g,
};

void pxa1986_release_comm(void)
{
#ifdef CONFIG_PM
	if (sysfs_create_group(power_kobj, &attr_group))
		return;
#else
	pxa1986_power_kobj = kobject_create_and_add("power", NULL);
	if (!pxa1986_power_kobj)
		return;
	if (sysfs_create_group(pxa1986_power_kobj, &attr_group))
		return;
#endif
}

static void __init pxa1986_dt_init_machine(void)
{
#if 0
#ifdef CONFIG_DDR_DEVFREQ
	pxa1986_devfreq_ddr_init();
#endif

#ifdef CONFIG_VPU_DEVFREQ
	pxa1986_devfreq_vpu_init();
#endif
#endif

#ifdef CONFIG_GPU_RESERVE_MEM
	pxa_add_gpu();
#endif

	pxa1986_clk_init();

#ifdef CONFIG_PXA1986_THERMAL
	pxa1986_thermal_cooling_devices_register(cool_dev_info,
		ARRAY_SIZE(cool_dev_info));
#endif

	of_platform_populate(NULL, of_default_bus_match_table,
			     pxa1986_auxdata_lookup, &platform_bus);

	pxa1986_release_comm();
}

#ifdef CONFIG_ARM_ARCH_TIMER
static void enable_ts_fast_cnt(void)
{
	u32 val;

	/* Enable timestamp fast counter: used by generic timer */
	val = __raw_readl(TIMESTAMP_VIRT_BASE + TIMESTAMP_CTRL);
	__raw_writel(val | (1 << 1), TIMESTAMP_VIRT_BASE + TIMESTAMP_CTRL);

	/* Cannot do this: SEC write only, undef exception in NS.
		Will be done in SEC CPU init. Dynamic changes are TBD. */
	/* Update system counter frequency register - otherwise Generic Timer
	 * initializtion fails.
	 * By default(after reset) fast couter is configured to 3.25 MHz.
	 * If fast counter clock will be chnaged, value below should be
	 * modified as well.
	 */
	asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r" (val));
	if (!val) {
		/* Not set by secure init, so assume we are in secure state */
		val = 3250000;
		asm volatile("mcr p15, 0, %0, c14, c0, 0" : : "r" (val));
	}
	pr_info("Timestamp fast counter is enabled\n");
}
#endif

static __init void pxa1986_timer_init(void)
{
	uint32_t clk_rst;

	/* Select the configurable timer clock source to be 6.5MHz */
	writel_relaxed(ACCU_APBCLK | ACCU_RST, get_accu_base_va() + ACCU_APPS_TIMERS1_CLK_CNTRL_REG);
	clk_rst = ACCU_APBCLK | ACCU_FNCLK | SET_ACCU_RATIO(4);
	writel_relaxed(clk_rst, get_accu_base_va() + ACCU_APPS_TIMERS1_CLK_CNTRL_REG);

#ifdef CONFIG_ARM_ARCH_TIMER
	enable_ts_fast_cnt();
#endif
	clocksource_of_init();
}

#define CP_MEM_MAX_SEGMENTS 2
unsigned _cp_area_addr[CP_MEM_MAX_SEGMENTS];
unsigned _cp_area_size[CP_MEM_MAX_SEGMENTS+1]; /* last entry 0 */
/* This must be early_param: reservation is done by MACHINE_DESC.reserve
	(see below), invoked by arch/arm/mm/.c:arm_memblock_init() called in
	arch/arm/kernel/setup.c:setup_arch()
	after parse_early_param() and before paging_init(). */
static int __init setup_cpmem(char *p)
{
	unsigned long size, start = 0xa7000000;
	int seg;

	size  = memparse(p, &p);
	if (*p == '@')
		start = memparse(p + 1, &p);
	for (seg = 0; seg < CP_MEM_MAX_SEGMENTS; seg++)
		if (!_cp_area_size[seg])
			break;
	BUG_ON(seg == CP_MEM_MAX_SEGMENTS);
	_cp_area_addr[seg] = (unsigned)start;
	_cp_area_size[seg] = (unsigned)size;
	return 0;
}
early_param("cpmem", setup_cpmem);

/* Exported for telephony use */
unsigned cp_area_addr(void)
{
	/* _cp_area_addr[] contain actual CP region addresses for reservation.
	This function returns the address of the first region, which is
	the main one used for AP-CP interface, aligned to 16MB.
	The AP-CP interface code takes care of the offsets inside the region,
	including the non-CP area at the beginning of the 16MB aligned range. */
	return _cp_area_addr[0]&0xFF000000;
}
EXPORT_SYMBOL(cp_area_addr);

static void __init pxa1986_reserve_cpmem(void)
{
	int seg;

	/* Built-in default: the legacy 16MB option */
	if (!_cp_area_size[0]) {
		_cp_area_addr[0] = 0x0;
		_cp_area_size[0] = 0x01000000;
	}
	for (seg = 0; _cp_area_size[seg]; seg++) {
		/* Reserve 32MB memory for CP */
		unsigned addr = _cp_area_addr[seg];
		unsigned size = _cp_area_size[seg];
		BUG_ON(memblock_reserve(addr, size) != 0);
		memblock_free(addr, size);
		memblock_remove(addr, size);
		pr_info("Reserved CP memory: 0x%x@0x%x\n", size, addr);
	}
}

static void __init pxa1986_reserve(void)
{
	pxa1986_reserve_cpmem();

#ifdef CONFIG_MMP_DISP
	mmp_reserve_fbmem();
#endif

#ifdef CONFIG_GPU_RESERVE_MEM
	pxa_reserve_gpu_memblock();
#endif
}

/* PXA1986 restart implementation */
#define PXA1986_TIMERS1_PHYS_BASE	0xd4081000
#define PXA1986_MC_PHYS_BASE		0xd4400000
#define PXA1986_ACCU_TIMERS1_REG	0xd407f0b0
#define PXA1986_CCI_PHYS_BASE		0xd0090000
#define MCK5_CMD0_REG			0x20
#define MCK5_STATUS_REG			0x8
#define MCK5_STATUS_MASK		0x440044 /* SR on CS0/1 CH0/1 */
#define MCK5_CONTROL0_REG		0x44
#define MCK5_CONTROL0_BLOCKALL	0x10000
#define MCK5_SR_CMD				0x40
#define MCK5_DPD_CMD			0x10000
#define MCK5_ST_SR_DPD			0xcccccccc
#define MCK5_ST_INIT			0x11111111
/* Bit numbers in MCK5_STATUS, 4 per CS */
enum {
	mck_st_init,
	mck_st_pd,
	mck_st_sr,
	mck_st_dpd
};
#define WDT_FAR_VAL				0xbaba
#define WDT_SAR_VAL				0xeb10
#define MPMU_MCKCR_REG			0xd4309030
#define	MPMU_MCKCR_CKE			1
#define CCI_CONTROL_OVRD_REG	PXA1986_CCI_PHYS_BASE
#define CCI_ALL_BARRIERS_TERM	8
/*
 * mck_cs_bits()
 * status:	MCK5_STATUS_REG value;
 * bit:		[0-3]: bit number in each CS nibble.
 * Returns: bit mask, one bit per CS; 8 bits total for both ch#0 and 1.
 */
static inline u32 mck_cs_bits(u32 status, int bit)
{
	u32 cs = 0;
	int i;
	status >>= bit;
	for (i = 0; status; i++, status >>= 4)
		cs |= (status & 1) << i;
	return cs;
}

/*
 * __pxa1986_cpu_reset()
 * This is the function that actually puts DDR into SR/DPD
 * per given parameter (MCK5_STATUS_REG format), sets
 * APMU_MCKCR accordingly, and then performs WDT reset.
 * The function should be copied into SRAM and run from there
 * with MMU disabled and interrupts disabled: DDR cannot be accessed.
 */
static void __attribute__ ((noreturn)) __pxa1986_cpu_reset(void)
{
	/*
	 * Important: must be a noreturn function, otherwise compiler
	 * would generate a stackframe, and since we don't switch
	 * stack to physical, this would attempt to access virtual sp
	 * address and crash.
	 */
	u32 status;
	void __iomem *tmr = (void __iomem *)PXA1986_TIMERS1_PHYS_BASE;
	void __iomem *p_mc = (void __iomem *)PXA1986_MC_PHYS_BASE;
	u32 v;
	u32 mckcr = MPMU_MCKCR_CKE;

	/*
	 * Set barrier termination in CCI, otherwise once we
	 * block all DDR accesses, any dsb would hang.
	 */
	__raw_writel(__raw_readl((void __iomem *)CCI_CONTROL_OVRD_REG)
		| CCI_ALL_BARRIERS_TERM, (void __iomem *)CCI_CONTROL_OVRD_REG);

	/* For now, all CS's that are initialized enter SR */
	status = (__raw_readl(p_mc + MCK5_STATUS_REG) & MCK5_ST_INIT)
				<< mck_st_sr;
	/* Block all ports: otherwise any DDR access results in SR exit */
	__raw_writel(__raw_readl(p_mc + MCK5_CONTROL0_REG)
		| MCK5_CONTROL0_BLOCKALL,
		p_mc + MCK5_CONTROL0_REG);
	/* Enter DPD on ch0 and ch1 as requested */
	v = mck_cs_bits(status, mck_st_dpd); /* DPD */
	if (v&0xf) /* CH#0 */
		__raw_writel(MCK5_DPD_CMD | (1 << 28) | ((v&0xf) << 24),
				p_mc + MCK5_CMD0_REG);
	/* NOTE: MCK5 has up to 4 CS's, but MCKCR has 2 bits only */
	mckcr |= (v&3) << 4;

	v >>= 4;
	if (v&0xf) /* CH#1 */
		__raw_writel(MCK5_DPD_CMD | (1 << 29) | ((v&0xf) << 24),
				p_mc + MCK5_CMD0_REG);
	mckcr |= (v&3) << 6;

	/* Enter SR on ch0 and ch1 as requested */
	v = mck_cs_bits(status, mck_st_sr); /* SR */
	if (v&0xf) /* CH#0 */
		__raw_writel(MCK5_SR_CMD | (1 << 28) | ((v&0xf) << 24),
				p_mc + MCK5_CMD0_REG);
	mckcr |= (v&3) << 8;

	v >>= 4;
	if (v&0xf) /* CH#1 */
		__raw_writel(MCK5_SR_CMD | (1 << 29) | ((v&0xf) << 24),
				p_mc + MCK5_CMD0_REG);
	mckcr |= (v&3) << 10;

	/* Wait until MCK5 status reflects all the commands sent */
	while ((__raw_readl(p_mc + MCK5_STATUS_REG) ^ status)
		& MCK5_ST_SR_DPD)
		;

	/* Make sure TIMER1 is enabled and accessible */
	v = __raw_readl((void __iomem *)PXA1986_ACCU_TIMERS1_REG);
	v |= ACCU_APBCLK | ACCU_FNCLK | ACCU_RST;
	__raw_writel(v, (void __iomem *)PXA1986_ACCU_TIMERS1_REG);

	/* Set MCKCR */
	__raw_writel(mckcr, (void __iomem *)MPMU_MCKCR_REG);
	/* Now load WDT and trigger a reset */
	__raw_writel(WDT_FAR_VAL, tmr + TMR_WFAR);
	__raw_writel(WDT_SAR_VAL, tmr + TMR_WSAR);
	__raw_writel(3, tmr + TMR_WMER);/* enable, generate reset */
	__raw_writel(WDT_FAR_VAL, tmr + TMR_WFAR);
	__raw_writel(WDT_SAR_VAL, tmr + TMR_WSAR);
	__raw_writel(1, tmr + TMR_WMR); /* shortest */
	__raw_writel(WDT_FAR_VAL, tmr + TMR_WFAR);
	__raw_writel(WDT_SAR_VAL, tmr + TMR_WSAR);
	__raw_writel(1, tmr + TMR_WCR); /* reset to 0, will  expire at 1 */
	while (1)
		;
}

#define SRAM_SPACE		0xd101fe00
#define SRAM_FUNC_SIZE	0x200

/*
 * pxa1986_arch_reset()
 * This implements SoC reset and should be used in pxa1986 based
 * machines as .restart in MACHINE_DESC
 * The implementation is different from mmp_arch_reset() because
 * of the procedure to enter DDR SR/DPD, specific to pxa1986,
 * which also requires to turn MMU off.
 */
void __attribute__ ((noreturn)) pxa1986_arch_reset(char mode, const char *cmd)
{
	void (*phys_reset)(unsigned long);
	void *saddr = ioremap_nocache(SRAM_SPACE, SRAM_FUNC_SIZE);
	unsigned faddr = (unsigned)__pxa1986_cpu_reset;
	unsigned long flags;

	local_irq_save(flags);
	setup_mm_for_reboot();

	BUG_ON(!saddr);
	/* Function address bit [0] might be 1 if function is in THUMB */
	memcpy(saddr, (void *)(faddr & ~1), SRAM_FUNC_SIZE);
	smp_wmb();
	phys_reset = (void (*)(unsigned long))
			(unsigned long)virt_to_phys(cpu_reset);
	asm volatile (
		"mrc	p15, 0, ip, c1, c0, 0\n"
		"bic	ip, ip, #0x1800 @ clear Z,I bits\n"
		"bic	ip, ip, #0x4 @ clear C bit\n"
		"mcr	p15, 0, ip, c1, c0, 0\n"
		"dsb\n"
		"isb\n"
		: : : "ip");
	phys_reset(SRAM_SPACE | (faddr & 1));
	while (1)
		;
}

static const char *pxa1986_dt_board_compat[] __initdata = {
	"mrvl,pxa1986sdk",
	NULL,
};

DT_MACHINE_START(PXA1986_DT, "PXA1986")
	.smp_init	= smp_init_ops(mmp_smp_init_ops),
	.map_io		= mmp_map_io,
	.init_irq	= pxa1986_dt_irq_init,
	.init_time	= pxa1986_timer_init,
	.init_machine	= pxa1986_dt_init_machine,
	.dt_compat	= pxa1986_dt_board_compat,
	.reserve	= pxa1986_reserve,
	.restart	= pxa1986_arch_reset,
MACHINE_END
