#ifndef __LINUX_CPUTYPE_H
#define __LINUX_CPUTYPE_H


#include <linux/regs-addr.h>
#include <linux/io.h>
#include <linux/of.h>

#include <asm/cputype.h>

/*
 *  CPU   Stepping   CPU_ID      CHIP_ID
 *
 * PXA168    S0    0x56158400   0x0000C910
 * PXA168    A0    0x56158400   0x00A0A168
 * PXA910    Y1    0x56158400   0x00F2C920
 * PXA910    A0    0x56158400   0x00F2C910
 * PXA910    A1    0x56158400   0x00A0C910
 * PXA920    Y0    0x56158400   0x00F2C920
 * PXA920    A0    0x56158400   0x00A0C920
 * PXA920    A1    0x56158400   0x00A1C920
 * MMP2	     Z0	   0x560f5811   0x00F00410
 * MMP2      Z1    0x560f5811   0x00E00410
 * MMP2      A0    0x560f5811   0x00A0A610
 */

extern unsigned int mmp_chip_id;

static unsigned int get_chip_id(void)
{
	static unsigned int chip_id;
	if (!chip_id)
		chip_id = __raw_readl(get_ciu_base_va());
	return chip_id;
}


#ifdef CONFIG_CPU_PXA168
static inline int cpu_is_pxa168(void)
{
	return (((read_cpuid_id() >> 8) & 0xff) == 0x84) &&
		((mmp_chip_id & 0xfff) == 0x168);
}
#else
#define cpu_is_pxa168()	(0)
#endif

/* cpu_is_pxa910() is shared on both pxa910 and pxa920 */
#ifdef CONFIG_CPU_PXA910
static inline int cpu_is_pxa910(void)
{
	return (((read_cpuid_id() >> 8) & 0xff) == 0x84) &&
		(((mmp_chip_id & 0xfff) == 0x910) ||
		 ((mmp_chip_id & 0xfff) == 0x920));
}
#else
#define cpu_is_pxa910()	(0)
#endif

#ifdef CONFIG_CPU_MMP2
static inline int cpu_is_mmp2(void)
{
	return (((read_cpuid_id() >> 8) & 0xff) == 0x58);
}
#else
#define cpu_is_mmp2()	(0)
#endif

#ifdef CONFIG_CPU_PXA988
static inline int cpu_is_pxa988(void)
{
	return (((read_cpuid_id() >> 4) & 0xfff) == 0xc09) &&
		(((mmp_chip_id & 0xffff) == 0xc988) ||
		((mmp_chip_id & 0xffff) == 0xc928));
}
static inline int cpu_is_pxa986(void)
{
	return (((read_cpuid_id() >> 4) & 0xfff) == 0xc09) &&
		(((mmp_chip_id & 0xffff) == 0xc986) ||
		((mmp_chip_id & 0xffff) == 0xc926));
}
static inline int cpu_is_pxa1088(void)
{
	return (((read_cpuid_id() >> 4) & 0xfff) == 0xc07) &&
		(((mmp_chip_id & 0xffff) == 0x1088));
}
static inline int cpu_is_pxa1L88(void)
{
	return (((read_cpuid_id() >> 4) & 0xfff) == 0xc07) &&
		(((mmp_chip_id & 0xffff) == 0x1188));
}
static inline int cpu_is_pxa1L88_a0(void)
{
	return (((read_cpuid_id() >> 4) & 0xfff) == 0xc07) &&
		(((mmp_chip_id & 0xffffff) == 0xa01188));
}

static inline int cpu_is_pxa1L88_a0c(void)
{
	return (((read_cpuid_id() >> 4) & 0xfff) == 0xc07) &&
		(((mmp_chip_id & 0xffffff) == 0xb01188));
}

static inline int cpu_is_pxa1U88(void)
{
	return (((read_cpuid_id() >> 4) & 0xfff) == 0xc07) &&
		(((mmp_chip_id & 0xffff) == 0x1098));
}

static inline int cpu_is_pxa1U88_z1(void)
{
	return (((read_cpuid_id() >> 4) & 0xfff) == 0xc07) &&
		(((mmp_chip_id & 0xffffff) == 0xa01098));
}

#else
#define cpu_is_pxa988()	(0)
#define cpu_is_pxa986()	(0)
#define cpu_is_pxa1088()	(0)
#define cpu_is_pxa1L88()	(0)
#define cpu_is_pxa1L88_a0()	(0)
#define cpu_is_pxa1L88_a0c()	(0)
#define cpu_is_pxa1U88()	(0)
#define cpu_is_pxa1U88_z1()	(0)
#endif


#ifdef CONFIG_CPU_PXA1986
static inline int cpu_is_pxa1986(void)
{
	return (((((read_cpuid_id() >> 4) & 0xfff) == 0xc0f) ||
		(((read_cpuid_id() >> 4) & 0xfff) == 0xc07)) &&
		((mmp_chip_id & 0xe000) == 0x8000));
}

static inline int cpu_is_pxa1986_z1(void)
{
	/*
	 * found some chips with id 0x8000 instead of 0x8001
	 * z2 chip is same as z1 with new comm features. No need for
	 * special function at the moment.
	 */
	return (cpu_is_pxa1986() && (((mmp_chip_id & 0xffff) == 0x8001) ||
				((mmp_chip_id & 0xffff) == 0x8000) ||
				((mmp_chip_id & 0xffff) == 0x8002)));
}

static inline int cpu_is_pxa1986_a0(void)
{
	return (cpu_is_pxa1986() && ((mmp_chip_id & 0xffff) == 0x8020));
}
#else
#define cpu_is_pxa1986()	(0)
#define cpu_is_pxa1986_z1()	(0)
#define cpu_is_pxa1986_a0()	(0)
#endif

static inline bool cpu_is_ca9(void)
{
	if ((read_cpuid_id() & 0xfff0) == 0xc090)
		return true;

	return false;
}

static inline bool cpu_is_ca7(void)
{
	if ((read_cpuid_id() & 0xfff0) == 0xc070)
		return true;

	return false;
}

static inline bool cpu_is_ca15(void)
{
	if ((read_cpuid_id() & 0xfff0) == 0xc0f0)
		return true;

	return false;
}
#if defined(CONFIG_ARM64)
static inline int cpu_is_pxa1928_b0(void)
{
	return ((get_chip_id() & 0xffffff) == 0xb0c198);
}

static inline int cpu_is_pxa1928_a0(void)
{
	return ((get_chip_id() & 0xffffff) == 0xa0c198);
}

static inline int cpu_is_pxa1908(void)
{
	return ((get_chip_id() & 0xffff) == 0x1908);
}

#define cpu_is_pxa1928_zx()		(0)

#define cpu_is_pxa1928_z1()		(0)
#define cpu_is_pxa1928_z2()		(0)

static inline int cpu_is_pxa1928(void)
{
	return cpu_is_pxa1928_b0() || cpu_is_pxa1928_a0()
		|| cpu_is_pxa1928_zx();
}

#elif defined(CONFIG_ARM)

#define cpu_is_pxa1928_b0()		(0)
#define cpu_is_pxa1928_a0()		(0)

static inline int cpu_is_pxa1928_zx(void)
{
	return ((get_chip_id() & 0xffffff) == 0xf0c192);
}

static inline int cpu_is_pxa1928(void)
{
	return cpu_is_pxa1928_a0() || cpu_is_pxa1928_zx();
}

static inline int cpu_is_pxa1928_z1(void)
{
	return ((get_chip_id() & 0xffffff) == 0xf0c192) && ((read_cpuid_id() & 0xf) == 0x3);
}

static inline int cpu_is_pxa1928_z2(void)
{
	return ((get_chip_id() & 0xffffff) == 0xf0c192) && ((read_cpuid_id() & 0xf) == 0x5);
}
#endif

static inline int pxa1928_is_a0(void)
{
	struct device_node *np;
	const char *str = NULL;
	static int is_a0 = -1;

	if (is_a0 != -1)
		return is_a0;

	np = of_find_node_by_name(NULL, "chip_version");
	if (!np)
		is_a0 = 0;

	if (np && !of_property_read_string(np, "version", &str)) {
		if (!strcmp(str, "a0"))
			is_a0 = 1;
		else
			is_a0 = 0;
	}

	return is_a0;
}

#define PXA1926_2L_DISCRETE		0x0
#define PXA1928_POP			0x1
#define PXA1928_4L_DISCRETE		0x2

static inline int get_chip_type(void)
{
	struct device_node *np;
	static int chip_type = -1;

	if (chip_type != -1)
		return chip_type;

	np = of_find_node_by_name(NULL, "chip_type");
	if (np)
		of_property_read_u32(np, "type", &chip_type);

	return chip_type;
}

static inline int cpu_is_pxa1926_2l_discrete(void)
{
	return (get_chip_type() & 0xf) == PXA1926_2L_DISCRETE;
}

static inline int cpu_is_pxa1928_pop(void)
{
	return (get_chip_type() & 0xf) == PXA1928_POP;
}

static inline int cpu_is_pxa1928_4l_discrete(void)
{
	return (get_chip_type() & 0xf) == PXA1928_4L_DISCRETE;
}

#endif /* __LINUX_CPUTYPE_H */
