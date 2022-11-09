#ifndef __MMP_H
#define __MMP_H
#include <linux/clk.h>
#include <linux/clk-private.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>

#define DUMMY_VL_TO_KHZ(level) (((level)+1)*1000)
#define DUMMY_VL_TO_HZ(level) (((level)+1)*1000000)

enum {
	CORE_1p2G = 1183,
	CORE_1p25G = 1248,
	CORE_1p5G = 1482,
};

extern unsigned long max_freq;

extern unsigned int get_chipprofile(void);
extern unsigned int get_iddq_105(void);
extern unsigned int get_iddq_130(void);
unsigned int get_ddr_op_num(void);
unsigned int get_ddr_op_rate(unsigned int index);
unsigned int __clk_periph_get_opnum(struct clk *clk);
unsigned long __clk_periph_get_oprate(struct clk *clk, unsigned int index);

unsigned int pxa1928_get_vpu_op_num(unsigned int vpu_type);
unsigned int pxa1928_get_vpu_op_rate(unsigned int vpu_type, unsigned int index);
u32 pxa1928_get_ddr_op_num(void);
u32 pxa1928_get_ddr_op_rate(u32 rate);
int get_gcu_freqs_table(unsigned long *gcu_freqs_table,
	unsigned int *item_counts, unsigned int max_item_counts);
int get_gcu2d_freqs_table(unsigned long *gcu_freqs_table,
	unsigned int *item_counts, unsigned int max_item_counts);
int get_gc_shader_freqs_table(unsigned long *gcu_freqs_table,
	unsigned int *item_counts, unsigned int max_item_counts);
extern int get_gc2d_freqs_table(unsigned long *gcu2d_freqs_table,
	unsigned int *item_counts, unsigned int max_item_counts);
extern int get_gc3d_sh_freqs_table(unsigned long *gcu3d_sh_freqs_table,
	unsigned int *item_counts, unsigned int max_item_counts);
extern int get_gc3d_freqs_table(unsigned long *gcu3d_freqs_table,
	unsigned int *item_counts, unsigned int max_item_counts);

extern int sdh_tunning_updatefreq(unsigned int dvfs_level);
extern int sdh_tunning_savefreq(void);
extern int sdh_tunning_restorefreq(void);
extern void plat_set_vl_min(u32 vl_num);
extern void plat_set_vl_max(u32 vl_num);
extern unsigned int plat_get_vl_min(void);
extern unsigned int plat_get_vl_max(void);
#ifdef CONFIG_PXA1928_THERMAL
extern void pmic_reconfigure(int direction);
extern void svc_thermal_enable(void);
extern int dvfs_thermal_update_level(int level);
#endif
#endif /* __MMP_H */
