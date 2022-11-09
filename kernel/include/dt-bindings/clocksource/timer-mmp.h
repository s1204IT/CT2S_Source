#ifndef _DT_BINDING_TIMER_MMP_H
#define _DT_BINDING_TIMER_MMP_H

/* timer flag bit definition */
/* bit[0]: MMP_TIMER_FLAG_SHADOW
 *         Indicate if the timer has shadow registers. If it has,
 *         counter could be read out directly.
 * bit[1]: MMP_TIMER_FLAG_CRSR
 *         Indicate if timer has CRSR register. If it has,
 *         counter could be restarted by directly writing CRSR.
 * bit[2]: MMP_TIMER_FLAG_PLVR
 *         Indicate if tiemr needs preload value when it restarts.
 */
#define MMP_TIMER_FLAG_SHADOW	(1 << 0)
#define MMP_TIMER_FLAG_CRSR	(1 << 1)
#define MMP_TIMER_FLAG_PLVR	(1 << 2)

#define MMP_TIMER_ALL_CPU	(0xFFFFFFFF)

#define MMP_TIMER_COUNTER_NOTUSED	0
#define MMP_TIMER_COUNTER_CLKSRC	(1 << 0)
#define MMP_TIMER_COUNTER_CLKEVT	(1 << 1)
#define MMP_TIMER_COUNTER_DELAY		(1 << 2)
#define MMP_TIMER_USAGE_MSK		(MMP_TIMER_COUNTER_CLKSRC | \
					 MMP_TIMER_COUNTER_CLKEVT | \
					 MMP_TIMER_COUNTER_DELAY)

#endif
