/*
 * (C) Copyright 2010 Marvell International Ltd.
 * All Rights Reserved
*/

#ifndef _SEH_LINUX_H
#define _SEH_LINUX_H

/* Ordinary SEH works under bad-system conditions
 * For robustness try to minimize resource & driver using and is platform-depend
 * Currently we have 2 platforms - ESHEL and ADIR/CONFIG_CPU_PXA1986
 * Use #ifdef #ifNdef ESHEL to switch between them
 */

/* Regidster Addresses */
#if defined(ESHEL)
#define MPMU_COM_RESET     0x40f5001c /* (CSER) for COMM Silent Reset */
#define ACIPC_INT_SET_REG  0x42403008 /* for low-level COMM ASSERT emulation */
#define CWSBR              0x42404008 /* Comm Sticky Bit Reg (CWSBR) */
#else
#define MPMU_COM_RESET     0xD430905C
#define ACIPC_INT_SET_REG  0xD4303008
#define CWSBR              0xD430C0BC /* Comm Sticky Bit Reg (CR5WSBR) */
#endif

#define MPMU_COM_POR_RESET_BIT     1
#define ACIPC_ASSERT_REQ        0x00000010

#if defined(ESHEL)
/* CWSB: 0--ready to capture Comm WDT event;
 * 1-- held in reset and Sticky bit's output is cleared
 */
#define XLLP_CWSBR_CWSB        (1u << 1)
/* CWRE: 0 --once Comm WDT IRQ asserted, not reset the Comm;
   1--once Comm WDT IRQ asserted, reset the Comm */
#define XLLP_CWSBR_CWRE        (1u << 0)
#define COM_RESET_WDT_SET()    reg_bit_set(seh_dev->cwsbr, XLLP_CWSBR_CWRE)
#define COM_RESET_WDT_CLR()    reg_bit_clr(seh_dev->cwsbr, XLLP_CWSBR_CWRE)
#else
/* CWSB: 0--ready to capture Comm WDT event;
   1-- held in reset and Sticky bit's output is cleared */
#define XLLP_CWSBR_CWSB        (1u << 3)
#define COM_RESET_WDT_SET()    seh_cser_cp_reset(1)
#define COM_RESET_WDT_CLR()	/* */
/* WDT alike reset versus POR (Power Off Reset) */
/* #define MPMU_COM_SYS_RESET_BIT (1<<1) */
#endif

#if defined(ESHEL)
#define COM_RESET_JIRA_ENA
#endif

#if defined COM_RESET_JIRA_ENA
/*JIRA: Comm cores fail to reboot after disabling and enabling the Comm
* ESHLTE-2616
* WORKAROUND:
*  set   LOCK_CPSS_FW_LATCH before COM-Reset assertion
*  clear LOCK_CPSS_FW_LATCH after  COM-Reset assertion before de-assertion
*/
#define GEN_REG5	         0x42404084
#define LOCK_CPSS_FW_LATCH   (1u << 0)
#endif

#if defined(ESHEL)
/*      IRQ_COMM_WDT        defined in irqs.h */
#define pxa_rfic_reset      pxa9xx_platform_rfic_reset
#else
#define IRQ_COMM_WDT        IRQ_PXA168_WDT
/*      pxa_rfic_reset      - generic name used in KERNEL */
#endif

struct seh_dev {
	EehMsgStruct msg;
	struct platform_device *dev;
	struct semaphore read_sem;	/* mutual exclusion semaphore */
	wait_queue_head_t readq;	/* read queue */
	void __iomem *cwsbr;
	void __iomem *mpmu_com_reset;
	void __iomem *gen_reg5;
};

/*
 * Externs
 */
#if defined EEH_FEATURE_RFIC
/* not used in kernel 3.10 */
extern void pxa_rfic_reset(unsigned short in_len,
						   void *in_buf,
						   unsigned short out_len,
						   void *out_buf);
#endif

#if defined(CONFIG_PXA_32KTIMER) && defined(CONFIG_PXA_TIMER_IRQ_MAX_LATENCY)
	&&(CONFIG_PXA_TIMER_IRQ_MAX_LATENCY > 0)
extern unsigned irql_set_threshold(unsigned value);
#endif

#if defined(BACKTRACE_TEL_ENABLE)
extern void back_trace_k(StrDesc *sd);
#else
#define back_trace_k(X) /**/
#endif

#endif /* _SEH_LINUX_H */
