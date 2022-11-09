#ifndef __MMPDCSTAT_H
#define __MMPDCSTAT_H

#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/clk-private.h>
#include <linux/clk/dvfs-dvc.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <asm/cputime.h>
#include <asm/div64.h>
#include <linux/kernel_stat.h>
#include <linux/tick.h>

enum lowpower_mode {
	LPM_C1,
	LPM_C2,
#ifdef CONFIG_ARM64
	LPM_MP2,
#endif
	LPM_D1P,
	LPM_D1,
	LPM_D2,
	LPM_D2_UDR,
	MAX_LPM_INDEX = 15,
};

#define MAX_BREAKDOWN_TIME 11
/* use the largest possible number is 10 */
#define MAX_LPM_INDEX_DC  10

struct op_dcstat_info {
	unsigned int ppindex;
	unsigned long pprate;
	struct timespec prev_ts;

	u64 clk_idle_time;		/* ns */
	u64 clk_busy_time;		/* ns */
	u64 pwr_off_time;		/* ns */

	/* used for ddr ticks ratio */
	long ddr_glob_ratio;
	long ddr_idle_ratio;
	long ddr_busy_ratio;
	long ddr_data_ratio;
	long ddr_util_ratio;

};

struct clk_dc_stat_info {
	bool stat_start;
	struct op_dcstat_info *ops_dcstat;
	u32 power_mode;
	unsigned int ops_stat_size;
	unsigned int curopindex;
	unsigned int idle_flag;
	unsigned int online;
	u64 C1_idle_start;
	u64 C1_idle_end;
	u64 C1_op_total[MAX_LPM_INDEX_DC];
	int C1_op_index;
	u64 C1_count[MAX_LPM_INDEX_DC];
	u64 C2_idle_start;
	u64 C2_idle_end;
	u64 C2_op_total[MAX_LPM_INDEX_DC];
	int C2_op_index;
	u64 C2_count[MAX_LPM_INDEX_DC];
	u64 breakdown_start;
	u64 breakdown_end;
	u64 breakdown_time_total[MAX_BREAKDOWN_TIME+1];
	u64 breakdown_time_count[MAX_BREAKDOWN_TIME+1];
	u64 runtime_start;
	u64 runtime_end;
	u64 runtime_op_total[MAX_LPM_INDEX_DC];
	u64 idle_op_total[MAX_LPM_INDEX_DC];
	int runtime_op_index;
};

struct clk_dcstat {
	struct clk *clk;
	struct clk_dc_stat_info clk_dcstat;
	struct list_head node;
};

struct core_dcstat {
	struct clk *clk;
	int cpu_num;
	int *cpu_id;
	struct list_head node;
};


enum clk_stat_msg {
	CLK_STAT_START = 0,
	CLK_STAT_STOP,
	CLK_STATE_ON,
	CLK_STATE_OFF,
	CLK_RATE_CHANGE,
	PWR_ON,
	PWR_OFF,
	CLK_DYNVOL_CHANGE,
	CPU_IDLE_ENTER,
	CPU_IDLE_EXIT,
	CPU_M2_OR_DEEPER_ENTER,
};

struct idle_dcstat_info {
	u64 all_idle_start;
	u64 all_idle_end;
	u64 total_all_idle;
	u64 total_all_idle_count;
	u64 all_active_start;
	u64 all_active_end;
	u64 total_all_active;
	u64 total_all_active_count;
	u64 M2_idle_start;
	u64 M2_idle_total;
	u64 M2_count;
	u64 D1P_idle_start;
	u64 D1P_idle_total;
	u64 D1p_count;
	u64 D1_idle_start;
	u64 D1_idle_total;
	u64 D1_count;
	u64 D2_idle_start;
	u64 D2_idle_total;
	u64 D2_count;
	u64 cal_duration;
	u64 all_idle_op_total[MAX_LPM_INDEX_DC];
	int all_idle_op_index;
	u64 all_idle_count[MAX_LPM_INDEX_DC];
};

#define CLK_DCSTAT_OPS(clk, name)					\
static int name##_dc_show(struct seq_file *seq, void *data)		\
{									\
	if (!clk)							\
		return -ENODEV;						\
									\
	show_dc_stat_info(clk, seq, data);				\
	if (seq->count == PAGE_SIZE)					\
		pr_warn("%s The dump buf is larger than one page!\n",	\
			 __func__);					\
	return 0;							\
}									\
static ssize_t name##_dc_write(struct file *filp,			\
		const char __user *buffer, size_t count, loff_t *ppos)	\
{									\
	unsigned int start;						\
	char buf[10] = { 0 };						\
	size_t ret = 0;							\
									\
	if (!clk)							\
		return -ENODEV;						\
									\
	if (copy_from_user(buf, buffer, count))				\
		return -EFAULT;						\
	sscanf(buf, "%d", &start);					\
	ret = start_stop_dc_stat(clk, start);			\
	if (ret < 0)							\
		return ret;						\
	return count;							\
}									\
static int name##_dc_open(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, name##_dc_show, NULL);			\
}									\
static const struct file_operations name##_dc_ops = {			\
	.owner		= THIS_MODULE,					\
	.open		= name##_dc_open,				\
	.read		= seq_read,					\
	.write		= name##_dc_write,				\
	.llseek		= seq_lseek,					\
	.release	= single_release,				\
};									\

/* function used for clk duty cycle stat */
static inline u64 ts2ns(struct timespec cur, struct timespec prev)
{
	u64 time_ns = 0;

	if ((cur.tv_sec > prev.tv_sec) ||
	    ((cur.tv_sec == prev.tv_sec) && (cur.tv_nsec > prev.tv_nsec))) {
		time_ns = (u64)(cur.tv_sec - prev.tv_sec);
		time_ns *= NSEC_PER_SEC;
		time_ns += (u64)(cur.tv_nsec - prev.tv_nsec);
	}

	return time_ns;
}

static inline u32 calculate_dc(u32 busy, u32 total, u32 *fraction)
{
	u32 result, remainder;
	u64 busy64, remainder64;

	busy64 = (u64)busy;
	result = div_u64_rem(busy64 * 100, total, &remainder);
	remainder64 = (u64)remainder;
	*fraction = div_u64(remainder64 * 100, total);

	return result;
}

int show_dc_stat_info(struct clk *clk, struct seq_file *seq, void *data);
int start_stop_dc_stat(struct clk *clk,	unsigned int start);
int clk_register_dcstat(struct clk *clk,
	unsigned long *opt, unsigned int opt_size);
typedef int (*powermode)(u32);
#ifdef CONFIG_DEBUG_FS
extern void cpu_dcstat_event(struct clk *clk, unsigned int cpuid,
		enum clk_stat_msg msg, unsigned int tgtop);
extern void clk_dcstat_event(struct clk *clk,
	enum clk_stat_msg msg, unsigned int tgtstate);
extern void clk_dcstat_event_check(struct clk *clk,
	enum clk_stat_msg msg, unsigned int tgtstate);
extern int register_cpu_dcstat(struct clk *clk, unsigned int cpunum,
	unsigned int *op_table, unsigned int opt_size, powermode func);
extern struct dentry *cpu_dcstat_file_create(const char *file_name,
		struct dentry *parent);
extern struct dentry *clk_dcstat_file_create(const char *file_name,
	struct dentry *parent, const struct file_operations *fops);

extern struct clk *cpu_dcstat_clk;
extern void vol_dcstat_event(u32);
extern void vol_ledstatus_event(u32);
extern unsigned int pxa1928_dvfs_map_table[0][MAX_PMIC_LEVEL];
#else
static inline void cpu_dcstat_event(struct clk *clk, unsigned int cpuid,
			  enum clk_stat_msg msg, unsigned int tgtop)
{

}
static inline void clk_dcstat_event(struct clk *clk,
	enum clk_stat_msg msg, unsigned int tgtstate)
{

}
static inline void clk_dcstat_event_check(struct clk *clk,
	enum clk_stat_msg msg, unsigned int tgtstate)
{

}
static int register_cpu_dcstat(struct clk *clk, unsigned int cpunum,
	unsigned int *op_table, unsigned int opt_size, powermode func);
{

}
static struct dentry *cpu_dcstat_file_create(const char *file_name,
		struct dentry *parent);
{

}
static struct dentry *clk_dcstat_file_create(const char *file_name,
	struct dentry *parent, const struct file_operations *fops);
{

}
void vol_dcstat_event(u32) {}
void vol_ledstatus_event(u32) {}
#endif

static inline u64 get_cpu_idle_time_jiffy(unsigned int cpu, u64 *wall)
{
	u64 idle_time;
	u64 cur_wall_time;
	u64 busy_time;

	cur_wall_time = jiffies64_to_cputime64(get_jiffies_64());

	busy_time = kcpustat_cpu(cpu).cpustat[CPUTIME_USER];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SYSTEM];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_IRQ];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_SOFTIRQ];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_STEAL];
	busy_time += kcpustat_cpu(cpu).cpustat[CPUTIME_NICE];

	idle_time = cur_wall_time - busy_time;
	if (wall)
		*wall = jiffies_to_usecs(cur_wall_time);

	idle_time = jiffies_to_usecs(idle_time);

	return idle_time;
}

static inline cputime64_t get_cpu_idle_time(
	unsigned int cpu,
	cputime64_t *wall,
	bool io_is_busy)
{
	u64 idle_time = get_cpu_idle_time_us(cpu, wall);

	if (idle_time == -1ULL)
		idle_time = get_cpu_idle_time_jiffy(cpu, wall);
	else if (!io_is_busy)
		idle_time += get_cpu_iowait_time_us(cpu, wall);

	return idle_time;
}

extern int ddr_profiling_show(struct clk_dc_stat_info *dc_stat_info);
extern int ddr_profiling_store(int start);

#endif /* __MMPDCSTAT_H */
