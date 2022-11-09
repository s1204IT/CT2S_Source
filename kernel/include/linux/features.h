#ifndef __LINUX_FEATURES_H
#define __LINUX_FEATURES_H

#if defined(CONFIG_ARM) && defined(CONFIG_ARCH_MMP)
#include <linux/cputype.h>

static inline int has_feat_enable_cti(void)
{
	return cpu_is_pxa1088() || cpu_is_pxa1L88()
		|| cpu_is_pxa1U88();
}

static inline int has_feat_lcd_more_source(void)
{
	return cpu_is_pxa1L88_a0c();
}

/* When GC is enabled, GC2D is enabled. */
static inline int has_feat_gc2d_on_gc_on(void)
{
	return cpu_is_pxa1L88_a0();
}

/* Use gssp for hifi record. */
static inline int has_feat_hifi_gssp_record(void)
{
	return cpu_is_pxa1L88_a0();
}

static inline int has_feat_debug(void)
{
	return cpu_is_pxa1U88();
}

static inline int has_feat_arch_restart(void)
{
	return cpu_is_pxa1088() || cpu_is_pxa1L88()
		|| cpu_is_pxa1U88();
}

static inline int has_feat_gc_shader_release(void)
{
	return cpu_is_pxa1U88() || cpu_is_pxa1L88();
}

static inline int has_feat_local_etb(void)
{
	return cpu_is_pxa1U88();
}

static inline int has_feat_dis_squdyngating(void)
{
	return cpu_is_pxa1U88();
}

static inline int has_feat_suspend_dis_gps_func(void)
{
	return cpu_is_pxa1U88();
}

static inline int has_feat_watchdog_reset(void)
{
	return cpu_is_pxa1L88();
}

static inline int has_feat_force_dpdm(void)
{
	return cpu_is_pxa1L88_a0() || cpu_is_pxa1088() || cpu_is_pxa988();
}

static inline int has_feat_pll2_ssc(void)
{
#if 0
	return cpu_is_pxa1L88_a0c();
#else
	return 0;
#endif
}

static inline int has_feat_pll3_ssc(void)
{
#if 0
	return cpu_is_pxa1L88_a0c();
#else
	return 0;
#endif
}

#else
static inline int has_feat_enable_cti(void) {return 0; }
static inline int has_feat_lcd_more_source(void) {return 0; }
static inline int has_feat_gc2d_on_gc_on(void) {return 0; }
static inline int has_feat_hifi_gssp_record(void) {return 0; }
static inline int has_feat_debug(void) {return 0; }
static inline int has_feat_arch_restart(void) {return 0; }
static inline int has_feat_local_etb(void) {return 0; }
static inline int has_feat_dis_squdyngating(void) {return 0; }
static inline int has_feat_suspend_dis_gps_func(void) {return 0; }
static inline int has_feat_watchdog_reset(void) {return 0; }
static inline int has_feat_force_dpdm(void) {return 0; }
static inline int has_feat_pll2_ssc(void) {return 0; }
static inline int has_feat_pll3_ssc(void) {return 0; }
#endif

#endif /* __LINUX_FEATURES_H */
