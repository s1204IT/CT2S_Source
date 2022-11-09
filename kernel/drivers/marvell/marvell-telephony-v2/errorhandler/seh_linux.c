/*
 * (C) Copyright 2010 Marvell International Ltd.
 * All Rights Reserved
 */
/*******************************************************************
 *
 *  FILE:	 seh_linux.c
 *
 *  DESCRIPTION:	This file either serve as an entry point or a
 *					function for writing or reading to/from
 *					the Linux SEH Device Driver.
 *
 *
 *  HISTORY:
 *					April, 2008 - Rovin Yu
 *
 *
 *******************************************************************/

#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/fs.h>
#include <linux/pagemap.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/aio.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <mach/irqs.h>
#include <linux/version.h>

#ifdef CONFIG_PXA95x_DVFM
#include <mach/dvfm.h>
#endif
#include <mach/hardware.h>
#include <asm/system.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/mtd/mtd.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/spinlock.h>
#include <linux/reboot.h>
#include <asm/pgtable.h>

#ifdef CONFIG_ANDROID_POWER
#include <linux/wakelock.h>
#endif

#ifdef CONFIG_PXA_MIPSRAM
#include <linux/mipsram.h>
#include <linux/time.h>
#endif /* CONFIG_PXA_MIPSRAM */

#define ACI_LNX_KERNEL
#include "eeh_ioctl.h"
#include "seh_linux.h"
#include "loadTable.h"
#include "apcp_mmap.h"
/*#include "seh_assert_notify.h"*/

#include <mach/mfp.h>
#include <linux/gpio.h>
#ifdef CONFIG_PXA_RAMDUMP
#include <linux/ptrace.h>	/*pt_regs */
#include <mach/ramdump.h>
#include <mach/ramdump_defs.h>
#define MMC_SD_DETECT_GPIO             MFP_PIN_GPIO47
#define MMC_SD_DETECT_INSERTED(VAL)   ((0x8000 & VAL) == 0)
/* comes from ramdump h-files above:
   backwards compatibility with older kernel */
#ifdef RAMDUMP_PHASE_1_1
#define SEH_RAMDUMP_ENABLED
#endif
#endif

#ifdef HAWK_ENABLED
static int hawk_fota_active;
module_param(hawk_fota_active, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(hawk_fota_active,
		 "Flag - when equal to 1 - do not respond to ASSERT");
#endif

/*****************************************************************
* Registers used by SEH have bits related to different subsystems
* and therefore should be modified under Disable-IRQ
* but this is the caller's responsibility
*/
#define read_reg(REG) __raw_readl(REG)
#define reg_bit_set(REG, bitno) \
			__raw_writel(((__raw_readl(REG)) | (bitno)), REG);

#define reg_bit_clr(REG, bitno) \
			__raw_writel(((__raw_readl(REG)) & ~(bitno)), REG);

#define REG_BIT_SET(REG, bitno)  { REG |=  (bitno); }
#define REG_BIT_CLR(REG, bitno)  { REG &= ~(bitno); }

/* JIRA: Comm cores fail to reboot after disabling and enabling the Comm
   ESHLTE-2616. Actual for all silicones, but meanwhile apply for ESHEL only */
#if defined(COM_RESET_JIRA_ENA)
#define COM_RESET_ACTIVATE_JIRA() \
	reg_bit_set(seh_dev->gen_reg5, LOCK_CPSS_FW_LATCH)
#define COM_RESET_DEACTIVATE_JIRA() /*do twice just for delay*/ \
	do { \
		reg_bit_clr(seh_dev->gen_reg5, LOCK_CPSS_FW_LATCH); \
		reg_bit_clr(seh_dev->gen_reg5, LOCK_CPSS_FW_LATCH); \
	} while (0);
#else
#define COM_RESET_ACTIVATE_JIRA() /**/
#define COM_RESET_DEACTIVATE_JIRA() /**/
#endif /* JIRA */
unsigned short seh_open_count;
spinlock_t seh_init_lock = __SPIN_LOCK_UNLOCKED(old_style_spin_init);

#ifdef CONFIG_PXA_MIPSRAM
spinlock_t seh_mipsram_lock = __SPIN_LOCK_UNLOCKED(old_style_spin_init);
#endif /* CONFIG_PXA_MIPSRAM */

struct workqueue_struct *seh_int_wq;
struct work_struct seh_int_request;
struct seh_dev *seh_dev;

static int b_stress_test;
static int b_start_recovery;

static int seh_open(struct inode *inode, struct file *filp);
static long seh_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
static void seh_dev_release(struct device *dev);
static int seh_remove(struct platform_device *dev);
static int seh_probe(struct platform_device *dev);
static ssize_t seh_read(struct file *filp,
			char *buf, size_t count, loff_t *f_pos);
static unsigned int seh_poll(struct file *filp, poll_table *wait);
static int seh_mmap(struct file *file, struct vm_area_struct *vma);
static int seh_release(struct inode *inode, struct file *filp);
static void seh_dont_sleep(int dont_sleep_true);

static void tel_assert_util(char *file, char *function, int line, int param);

#ifdef CONFIG_PXA_MIPSRAM
static void mipsram_vma_open(struct vm_area_struct *vma);
static void mipsram_vma_close(struct vm_area_struct *vma);
static int mipsram_mmap(struct file *file, struct vm_area_struct *vma);
#endif /* CONFIG_PXA_MIPSRAM */

static const char *const seh_name = "seh";
static const struct file_operations seh_fops = {
	.open = seh_open,
	.read = seh_read,
	.release = seh_release,
	.unlocked_ioctl = seh_ioctl,
	.poll = seh_poll,
	.mmap = seh_mmap,
	.owner = THIS_MODULE
};

static struct platform_device seh_device = {
	.name = "seh",
	.id = 0,
	.dev = {
		.release = seh_dev_release,
		},
};

static struct platform_driver seh_driver = {
	.probe = seh_probe,
	.remove = seh_remove,
	.driver = {
		   .name = "seh",
		   .owner = THIS_MODULE,
		   },
};

static struct miscdevice seh_miscdev = {
	MISC_DYNAMIC_MINOR,
	"seh",
	&seh_fops,
};

#ifdef CONFIG_PXA_MIPSRAM
/*#define MIPSRAM_DEBUG*/

/* These are mostly for debug: do nothing useful otherwise*/
static struct vm_operations_struct mipsram_vm_ops = {
	.open = mipsram_vma_open,
	.close = mipsram_vma_close
};

static const struct file_operations mipsram_fops = {
	.owner = THIS_MODULE,
	.mmap = mipsram_mmap,
};

static struct miscdevice mipsram_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "mipsram",
	.fops = &mipsram_fops,
};
#endif /* CONFIG_PXA_MIPSRAM */

static void eeh_save_error_info(EehErrorInfo *info);
#ifdef SEH_RAMDUMP_ENABLED
static int ramfile_mmap(struct file *file, struct vm_area_struct *vma);

static const struct file_operations ramfile_fops = {
	.owner = THIS_MODULE,
	.mmap = ramfile_mmap,
};

static struct miscdevice ramfile_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ramfile",
	.fops = &ramfile_fops,
};

#endif

#ifdef CONFIG_ANDROID_POWER
#define PM_TIMEOUT                 (60 * 10)	/*sec */
#define SEH_PM_WAKE_LOCK_TIME()	wake_lock_timeout(&eeh_assert_wakelock, (PM_TIMEOUT)*HZ);
#define SEH_PM_WAKE_LOCK()      wake_lock(&eeh_assert_wakelock);
#define SEH_PM_WAKE_UNLOCK()    wake_unlock(&eeh_assert_wakelock);
#define SEH_PM_WAKE_INIT()		wake_lock_init(&eeh_assert_wakelock, WAKE_LOCK_SUSPEND, EEH_DUMP_ASSERT_LOCK_NAME);
#define SEH_PM_WAKE_DESTROY()   wake_lock_destroy(&eeh_assert_wakelock);
static struct wake_lock eeh_assert_wakelock;
#else /*CONFIG_ANDROID_POWER */
#define SEH_PM_WAKE_LOCK_TIME() /**/
#define SEH_PM_WAKE_LOCK() /**/
#define SEH_PM_WAKE_UNLOCK() /**/
#define SEH_PM_WAKE_INIT() /**/
#define SEH_PM_WAKE_DESTROY() /**/
#endif /*CONFIG_ANDROID_POWER */
/*#define DEBUG_SEH_LINUX*/
#ifdef DEBUG_SEH_LINUX
#define DBGMSG(fmt, args...)	pr_debug("SEH: "fmt, ##args)
#define ERRMSG(fmt, args...)	pr_err("SEH:"fmt, ##args)
#define	ENTER()					pr_debug("SEH: ENTER %s\n", __func__)
#define	LEAVE()					pr_debug("SEH: LEAVE %s\n", __func__)
#define	FUNC_EXIT()				pr_debug("SEH: EXIT %s\n", __func__)
#define DPRINT(fmt, args...)	pr_info("SEH:"fmt, ##args)
#else
#define	DBGMSG(fmt, args...)	do {} while (0)
#define ERRMSG(fmt, args...)	pr_err("SEH:"fmt, ##args)
#define	ENTER()					do {} while (0)
#define	LEAVE()					do {} while (0)
#define	FUNC_EXIT()				do {} while (0)
#define DPRINT(fmt, args...)	pr_info("SEH:"fmt, ##args)
#endif
/*=======================================================================
 * base-Kernel Operations to be done upon
 * - Assert   handling begin and end
 * - Recovery handling begin and end
 */
static void seh_irql_set_threshold(unsigned value)
{
	/* Set new value threshold, keep old in buffer
	   New value =0, restore old value from buffer */
#if defined(CONFIG_PXA_32KTIMER) && defined(CONFIG_PXA_TIMER_IRQ_MAX_LATENCY)
	&&(CONFIG_PXA_TIMER_IRQ_MAX_LATENCY > 0)
	static unsigned old_value;
	if (old_value) {
		if (value)
			irql_set_threshold(value);	/* just set, don't save */
		else {
			irql_set_threshold(old_value);	/* restore */
			old_value = 0;
		}
	} else {
		if (value)
			old_value = irql_set_threshold(value);	/* set and save */
		/* else do nothing */
	}
#else
	value = value;
#endif
}

static void seh_pmic_wdt_config(int sec)
{
/*#ifdef PMIC-WDT-ENABLE. Currently not implemented*/
	sec = sec;
}

static void seh_ramdump_prepare_in_advance(int on)
{
#ifdef CONFIG_PXA_RAMDUMP
	if (on)
		ramdump_prepare_in_advance();
	else
		ramdump_rdc_reset();
#endif
}

static void seh_kern_ops_on_assert(void)
{
	seh_irql_set_threshold(30000);
	seh_pmic_wdt_config(16);	/*guaranty SEH/EEH finish the work */
	seh_ramdump_prepare_in_advance(1);
}

static void seh_kern_ops_on_cp_recovery(bool recoveryStarted)
{
	if (recoveryStarted) {
		/*Do not set b_start_recovery here */
		seh_pmic_wdt_config(32);	/*Change short to Recovery-long */
	} else {
		/*Recovery ended with OK */
		b_start_recovery = 0;
		seh_pmic_wdt_config(0);
		seh_irql_set_threshold(0);
		seh_ramdump_prepare_in_advance(0);
	}
}

void seh_cser_cp_reset(bool active)
{
	/* Reset NOT related to the CP-WDT presence
	 * over Communication Subsystem Enable Register CSER
	 * In a difference with  CP-WDT this is full (power alike) reset
	 * Only bit0 is in use, all other reserved and must be ZERO
	 * (Refer CSER_OFF in Kernel)
	 */
	if (active)
		__raw_writel(0, seh_dev->mpmu_com_reset);	/*in reset */
	else
		__raw_writel(MPMU_COM_POR_RESET_BIT, seh_dev->mpmu_com_reset);
}

static void seh_int_assert_simulate_exec(unsigned long d)
{
	pr_debug("KERNEL INTERRUPT ASSERT simulation\n");
	tel_assert_util((char *)__FILE__, (char *)__func__, (int)__LINE__, 0);	/*== ASSERT(0);*/
}

static void seh_int_assert_simulate_init(void)
{
	struct timer_list *ts_timer = kmalloc(sizeof(*ts_timer), GFP_KERNEL);
	init_timer(ts_timer);
	ts_timer->function = seh_int_assert_simulate_exec;
	ts_timer->expires = jiffies + 15;
	ts_timer->data = (unsigned long)0;
	add_timer(ts_timer);
}

void seh_cp_assert_simulate(void)
{
	/*Set bit in ISRW */
	resource_size_t ACIPC_ADDR = ACIPC_INT_SET_REG;
	volatile void __iomem *va_acipc;
	va_acipc =
	    ioremap_nocache((resource_size_t) ACIPC_ADDR,
			    2 * (sizeof(unsigned long)));
	if (va_acipc == NULL)
		pr_err("COMM ASSERT simulation - FAILED to map ACIPC\n");
	else {
		pr_err("COMM ASSERT simulation over ACIPC_%x=%x\n", ACIPC_ADDR,
		       ACIPC_ASSERT_REQ);
		__raw_writel(ACIPC_ASSERT_REQ, va_acipc);
		iounmap((void *)va_acipc);
	}
}
EXPORT_SYMBOL(seh_cp_assert_simulate);

static void seh_mipsram_mark_end_of_log(void)
{
#ifdef CONFIG_PXA_MIPSRAM
	struct timeval tv;

	/* Mark end of log + time of day - this function is unsafe to call from ISR */
	do_gettimeofday(&tv);
	MIPS_RAM_MARK_END_OF_LOG(MIPSRAM_get_descriptor(),
				 (unsigned long)tv.tv_sec);
#endif /*CONFIG_PXA_MIPSRAM */
}

static void seh_mipsram_stop(bool sign_log)
{
#ifdef CONFIG_PXA_MIPSRAM
	unsigned long flags;

	spin_lock_irqsave(&seh_mipsram_lock, flags);

	/* Mark end of log */
	MIPSRAM_Trace(MIPSRAM_LOG_END_MARK_EVENT);

	/* stop MIPSRAM logger */
	MIPSRAM_get_descriptor()->buffer = NULL;

	if (sign_log)
		seh_mipsram_mark_end_of_log();

	spin_unlock_irqrestore(&seh_mipsram_lock, flags);
#endif /* CONFIG_PXA_MIPSRAM */
}

static void seh_mipsram_start(void)
{
#ifdef CONFIG_PXA_MIPSRAM
	/* Start MIPSRAM buffer */
	MIPSRAM_clear();
	/* start MIPSRAM logger */
	MIPSRAM_get_descriptor()->buffer = MIPSRAM_get_descriptor()->bufferVirt;
#endif /*CONFIG_PXA_MIPSRAM */
}

void seh_mipsram_add_trace(unsigned long trace)
{
#ifdef CONFIG_PXA_MIPSRAM
	unsigned long flags;

	spin_lock_irqsave(&seh_mipsram_lock, flags);

	/* Add custom trace */
	MIPSRAM_Trace(trace);

	spin_unlock_irqrestore(&seh_mipsram_lock, flags);
#endif /*CONFIG_PXA_MIPSRAM */
}

/* ==== INIT only =======================
* Set CWSBR to capture WDT as interrupt
* ENABLE_COMM_WDT_INTERRUPT
* capturing the interrupt on BICU and not generating reset to Comm
*  -------------------------
* CWSBR[CWSB] = 0
* CWSBR[CWRE] = 0
*/
static void comm_wdt_interrupt_set(void)
{
	unsigned long flags;
	ENTER();
	spin_lock_irqsave(&seh_init_lock, flags);
	reg_bit_set(seh_dev->cwsbr, XLLP_CWSBR_CWSB);
	enable_irq(IRQ_COMM_WDT);
	COM_RESET_WDT_CLR();	/*disable next Com WDT int to generate auto comm reset */
	reg_bit_clr(seh_dev->cwsbr, XLLP_CWSBR_CWSB);	/*enable src GB WDT int */
	spin_unlock_irqrestore(&seh_init_lock, flags);
	LEAVE();
}

/* ==== Run-Time =======================
* Clear CWSBR from capture WDT as interrupt , make it ready for next event
* Should be called after Interrupt is handled
* CLEAR_REENABLE_COMM_WDT_INTERRUPT after serving (handling)
*/
static void comm_wdt_interrupt_clear_during_silent_reset(void)
{
	unsigned long flags;
	ENTER();
	spin_lock_irqsave(&seh_init_lock, flags);
	reg_bit_set(seh_dev->cwsbr, XLLP_CWSBR_CWSB);	/*clr GB WDT int src */
	enable_irq(IRQ_COMM_WDT);
	COM_RESET_WDT_CLR();	/*disable next Com WDT int to generate auto comm reset */
	reg_bit_clr(seh_dev->cwsbr, XLLP_CWSBR_CWSB);	/*enable src GB WDT int */
	b_start_recovery = 0;	/*the "-1" may be also used */
	spin_unlock_irqrestore(&seh_init_lock, flags);
	LEAVE();
}

/*
 * The top part for SEH interrupt handler.
 */
irqreturn_t seh_int_handler_low(int irq, void *dev_id)
{				/*, struct pt_regs *regs */
	/*disable_irq(IRQ_COMM_WDT); */
	disable_irq_nosync(IRQ_COMM_WDT);

	/* stop MIPSRAM */
	seh_mipsram_stop(FALSE);
	queue_work(seh_int_wq, &seh_int_request);
	/*SEH_PM_WAKE_LOCK(); *//*SEH_PM_WAKE_LOCK_TIME(); */
	return IRQ_HANDLED;
}

/*
 * The bottom part for SEH interrupt handler
 */
void seh_int_handler_high(struct work_struct *data)
{
	(void)data;		/*unused */

	if (seh_dev) {
		if (down_interruptible(&seh_dev->read_sem)) {
			ERRMSG("%s: fail to down semaphore\n", __func__);
			enable_irq(IRQ_COMM_WDT);
			return;
		}
		SEH_PM_WAKE_LOCK();	/*SEH_PM_WAKE_LOCK_TIME(); */

		/* Mark MIPS_RAM end of log with EPOCH timestamp */
		seh_mipsram_mark_end_of_log();
		seh_kern_ops_on_assert();
		seh_dev->msg.msgId = EEH_WDT_INT_MSG;
		up(&seh_dev->read_sem);
		wake_up_interruptible(&seh_dev->readq);
	}

}

int seh_api_ioctl_handler(unsigned long arg)
{
	unsigned long flags;
	EehApiParams params;
	EEH_STATUS status = EEH_SUCCESS;

	if (copy_from_user(&params, (EehApiParams *) arg, sizeof(EehApiParams)))
		return -EFAULT;

	DPRINT("seh_api_ioctl_handler %d\n ", params.eehApiId);

	switch (params.eehApiId) {	/* specific EEH API handler */
	case _EehInit:
		DBGMSG("Kernel Space EehInit Params:No params\n");
		if (copy_to_user
		    (&((EehApiParams *) arg)->status, &status,
		     sizeof(unsigned int)))
			return -EFAULT;
		break;

	case _EehDeInit:
		DBGMSG("Kernel Space EehDeInit Params:No params\n");
		if (copy_to_user
		    (&((EehApiParams *) arg)->status, &status,
		     sizeof(unsigned int)))
			return -EFAULT;
		break;

	case _EehExecRFICReset:
#if defined EEH_FEATURE_RFIC	/*not used in kernel 3.10 */
		{
			/* Reset RF module */
			int u_delay = 1000;
			DBGMSG("Resetting RFIC...\n");
			pxa_rfic_reset(0, &u_delay, 0, NULL);
		}
#endif
		break;

	case _EehInsertComm2Reset:
		{
			EehInsertComm2ResetParam reset_params;

			if (copy_from_user
			    (&reset_params, params.params,
			     sizeof(EehInsertComm2ResetParam)))
				return -EFAULT;

			DBGMSG
			    ("Kernel Space EehInsertComm2Reset asserttype: %d\n",
			     reset_params.AssertType);

			seh_kern_ops_on_cp_recovery(TRUE);
			spin_lock_irqsave(&seh_init_lock, flags);
			COM_RESET_ACTIVATE_JIRA();
			spin_unlock_irqrestore(&seh_init_lock, flags);

			if (reset_params.AssertType == EEH_CP_WDT) {
				LoadTableType *p_load_table;
				p_load_table =
				    ioremap_nocache((unsigned int)
						    ADDR_CONVERT
						    (LOAD_TABLE_FIX_ADDR),
						    sizeof(LoadTableType));
				if (p_load_table != NULL) {
					unsigned int *resetTypeAddr =
					    (unsigned int *)&(p_load_table->
							      apps2commDataBuf);
					*resetTypeAddr = RESET_BASIC_2;
					iounmap((void *)p_load_table);
					/* go ahead like EEH_CP_ASSERT reset */
					COM_RESET_WDT_SET();
				}
				return 0;
			}
			/* verify if !PENDING in bit_2 */
			spin_lock_irqsave(&seh_init_lock, flags);

			if (b_stress_test
			    || reset_params.AssertType == EEH_AP_ASSERT)
				disable_irq(IRQ_COMM_WDT);
			COM_RESET_WDT_SET();
			b_start_recovery = 1;
			spin_unlock_irqrestore(&seh_init_lock, flags);

			/* CP-WDT reset keeps some Power-related subsystems not-reset
			 * We would like to start CP like after power
			 * so force the also the CSER reset active and inactive
			 */
			msleep(10);
			seh_cser_cp_reset(TRUE);

			if (copy_to_user
			    (&((EehApiParams *) arg)->status, &status,
			     sizeof(unsigned int)))
				return -EFAULT;
#if defined EEH_FEATURE_RFIC	/*not used in kernel 3.10 */
			{
				/* Reset RF module */
				int u_delay = 1000;
				DBGMSG("Resetting RFIC...\n");
				pxa_rfic_reset(0, &u_delay, 0, NULL);
			}
#endif
			break;
		}

	case _EehReleaseCommFromReset:

		DBGMSG
		    ("Kernel Space EehReleaseCommFromReset Params:No params\n");

		spin_lock_irqsave(&seh_init_lock, flags);
		COM_RESET_DEACTIVATE_JIRA();
		spin_unlock_irqrestore(&seh_init_lock, flags);

		comm_wdt_interrupt_clear_during_silent_reset();
		msleep(10);
		seh_cser_cp_reset(FALSE);

		if (copy_to_user
		    (&((EehApiParams *) arg)->status, &status,
		     sizeof(unsigned int)))
			return -EFAULT;

		break;

	case _EehExecReset:
		seh_ramdump_prepare_in_advance(0);
		kernel_restart("");
		/*Should never be here */
		return -EFAULT;
		break;

	case _EehGetTid:
		if ((((EehApiParams *) arg)->params == NULL)
		    || copy_to_user(((EehApiParams *) arg)->params,
				    &current->pid, sizeof(long)))
			return -EFAULT;
		break;

	case _EehTestAssertKernel:
		BUG_ON(params.params);
		tel_assert_util((char *)__FILE__, (char *)__func__,
				(int)__LINE__, 0);
		break;

	case _EehTestAssertKernelIRQ:
		seh_int_assert_simulate_init();
		break;

	case _EehTestCpAssert:
		seh_cp_assert_simulate();
		break;

	case _EehCopyCommImageFromFlash:
	default:
		ERRMSG("WRONG Api = %d (params.eehApiId)\n", params.eehApiId);
		return -EFAULT;
	}
	return 0;
}

static int seh_cpwdt_init(void)
{
	int status;
	status =
	    request_irq(IRQ_COMM_WDT, seh_int_handler_low, 0, seh_name, NULL);
	if (status) {
		DBGMSG("seh_open: cannot register the COMM WDT interrupt\n");
		return status;
	}
	disable_irq(IRQ_COMM_WDT);
	comm_wdt_interrupt_set();
	return 0;
}

static int seh_probe(struct platform_device *dev)
{
	int ret;

	ENTER();

	seh_int_wq = create_workqueue("seh_rx_wq");

	INIT_WORK(&seh_int_request, seh_int_handler_high);

	seh_dev = kzalloc(sizeof(*seh_dev), GFP_KERNEL);
	if (seh_dev == NULL) {
		ERRMSG("seh_probe: unable to allocate memory\n");
		return -ENOMEM;
	}

	seh_dev->cwsbr = ioremap_nocache(CWSBR, IORESOURCE_IO);
	seh_dev->mpmu_com_reset =
	    ioremap_nocache(MPMU_COM_RESET, IORESOURCE_IO);
#if defined(COM_RESET_JIRA_ENA)
	seh_dev->gen_reg5 = ioremap_nocache(GEN_REG5, IORESOURCE_IO);
	if (!seh_dev->cwsbr || !seh_dev->mpmu_com_reset || !seh_dev->gen_reg5) {
#else
	if (!seh_dev->cwsbr || !seh_dev->mpmu_com_reset) {
#endif
		ERRMSG("seh_probe: failed to ioremap registers\n");
		kfree(seh_dev);
		return -ENXIO;
	}

	init_waitqueue_head(&(seh_dev->readq));
	sema_init(&seh_dev->read_sem, 1);
	seh_dev->dev = dev;

	if (seh_cpwdt_init()) {
		iounmap(seh_dev->cwsbr);
		kfree(seh_dev);
		return -ENXIO;
	}
	ret = misc_register(&seh_miscdev);
	if (ret)
		ERRMSG("seh_probe: failed to call misc_register\n");
	LEAVE();

	return ret;
}

static int __init seh_init(void)
{
	int ret;

	ENTER();
	SEH_PM_WAKE_INIT();
	ret = platform_device_register(&seh_device);
	if (!ret) {
		ret = platform_driver_register(&seh_driver);
		if (ret) {
			ERRMSG("Cannot register SEH platform driver\n");
			platform_device_unregister(&seh_device);
		}
	} else {
		ERRMSG("Cannot register SEH platform device\n");
	}

#ifdef CONFIG_PXA_MIPSRAM
	/* register MIPSRAM device */
	ret = misc_register(&mipsram_miscdev);
	if (ret)
		pr_err("%s can't register device, err=%d\n", __func__, ret);
	else
		pr_debug("mipsram init successful\n");
#endif /* CONFIG_PXA_MIPSRAM */

#ifdef SEH_RAMDUMP_ENABLED
	/* register MIPSRAM device */
	ret = misc_register(&ramfile_miscdev);
	if (ret)
		pr_err("%s can't register device, err=%d\n", __func__, ret);
#endif /* SEH_RAMDUMP_ENABLED */

	LEAVE();

	return ret;
}

static void __exit seh_exit(void)
{
	ENTER();

	platform_driver_unregister(&seh_driver);
	platform_device_unregister(&seh_device);

	SEH_PM_WAKE_DESTROY();
	LEAVE();

}

static int seh_open(struct inode *inode, struct file *filp)
{
	unsigned long flags;
	ENTER();

	/*  Save the device pointer */
	if (seh_dev)
		filp->private_data = seh_dev;
	else
		return -ERESTARTSYS;
	/*
	 * Only to prevent kernel preemption.
	 */
	spin_lock_irqsave(&seh_init_lock, flags);

	/* if seh driver is opened already. Just increase the count */
	if (seh_open_count) {
		seh_open_count++;
		spin_unlock_irqrestore(&seh_init_lock, flags);
		return 0;
	}

	seh_open_count = 1;

	spin_unlock_irqrestore(&seh_init_lock, flags);

	LEAVE();
	return 0;
}

static ssize_t seh_read(struct file *filp, char *buf, size_t count,
			loff_t *f_pos)
{
	struct seh_dev *dev;
	/*static int client_count =0; - to be used for the CLIENT: "Assert Message Display" */

	ENTER();

	/*client_count ++; */
	dev = (struct seh_dev *)filp->private_data;
	if (dev == NULL)
		return -ERESTARTSYS;

	if (down_interruptible(&dev->read_sem))
		return -ERESTARTSYS;

	while (dev->msg.msgId == EEH_INVALID_MSG) {	/* nothing to read */
		up(&dev->read_sem);	/* release the lock */
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		DPRINT("\"%s\" reading: going to sleep\n", current->comm);
		if (wait_event_interruptible
		    (dev->readq, (dev->msg.msgId != EEH_INVALID_MSG)))
			return -ERESTARTSYS;	/* signal: tell the fs layer to handle it */
		/* otherwise loop, but first reacquire the lock */
		if (down_interruptible(&dev->read_sem))
			return -ERESTARTSYS;
	}

	/* ok, data is there, return something */
	count = min(count, sizeof(EehMsgStruct));
	if (copy_to_user(buf, (void *)&(dev->msg), count)) {
		up(&dev->read_sem);
		return -EFAULT;
	}

	DPRINT("\"%s\" did read %li bytes, msgId: %d\n", current->comm,
	       (long)count, dev->msg.msgId);
	/*--client_count;*/

	/*if(client_count ==0) { */
	/* reset the msg info */
	memset(&(dev->msg), 0, sizeof(EehMsgStruct));
	/*} else {
	   wake_up_interruptible(&seh_dev->readq);
	   } */
	up(&dev->read_sem);
	LEAVE();
	return count;
}

static long seh_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int ret;
	struct seh_dev *dev;

	if (_IOC_TYPE(cmd) != SEH_IOC_MAGIC) {
		ERRMSG("seh_ioctl: seh magic number is wrong!\n");
		return -ENOTTY;
	}

	dev = (struct seh_dev *)filp->private_data;
	if (dev == NULL)
		return -EFAULT;

	DBGMSG("seh_ioctl,cmd=0x%x\n", cmd);

	ret = 0;

	switch (cmd) {
	case SEH_IOCTL_API:
		ret = seh_api_ioctl_handler(arg);
		break;
	case SEH_IOCTL_TEST:
		DPRINT("SEH_IOCTL_TEST\n");
		if (down_interruptible(&seh_dev->read_sem))
			return -EFAULT;

		b_stress_test = 1;

		seh_dev->msg.msgId = EEH_WDT_INT_MSG;
		up(&seh_dev->read_sem);
		wake_up_interruptible(&seh_dev->readq);
		break;
	case SEH_IOCTL_ATC_OK:
		DPRINT("Receive seh_IOCTL_ATC_OK\n");
		seh_kern_ops_on_cp_recovery(FALSE);
		if (down_interruptible(&seh_dev->read_sem))
			return -EFAULT;

		seh_dev->msg.msgId = EEH_ATC_OK_MSG;
		up(&seh_dev->read_sem);
		wake_up_interruptible(&seh_dev->readq);
		break;
	case SEH_IOCTL_APP_ASSERT:
		{
			EehAppAssertParam param;

			/* First of all, stop MIPSRAM logging */
			seh_mipsram_stop(TRUE);
			seh_kern_ops_on_assert();

			DPRINT("Receive SEH_IOCTL_APP_ASSERT\n");
			memset(&param, 0, sizeof(EehAppAssertParam));
			if (copy_from_user
			    (&param, (EehAppAssertParam *) arg,
			     sizeof(EehAppAssertParam)))
				return -EFAULT;

			if (down_interruptible(&seh_dev->read_sem))
				return -EFAULT;

			seh_dev->msg.msgId = EEH_AP_ASSERT_MSG;
			strncpy(seh_dev->msg.msgDesc, param.msgDesc,
				sizeof(seh_dev->msg.msgDesc));
			seh_dev->msg.msgDesc[sizeof(seh_dev->msg.msgDesc) - 1] =
			    0;
			/* Print description into kbuf */
			pr_err("SEH: %s\n", seh_dev->msg.msgDesc);
			up(&seh_dev->read_sem);
			wake_up_interruptible(&seh_dev->readq);

			break;
		}
	case SEH_IOCTL_AUDIOSERVER_REL_COMP:
		DPRINT("Receive SEH_IOCTL_AUDIOSERVER_REL_COMP\n");
		if (down_interruptible(&seh_dev->read_sem))
			return -EFAULT;

		seh_dev->msg.msgId = EEH_AUDIOSERVER_REL_COMP_MSG;
		up(&seh_dev->read_sem);
		wake_up_interruptible(&seh_dev->readq);
		break;

	case SEH_IOCTL_MIPSRAM_START:
		seh_mipsram_start();
		break;

	case SEH_IOCTL_MIPSRAM_STOP:
		seh_mipsram_stop(TRUE);
		break;

	case SEH_IOCTL_MIPSRAM_ADD_TRACE:
		seh_mipsram_add_trace(arg);
		break;

	case SEH_IOCTL_MIPSRAM_DUMP:
		/* First of all, stop MIPS_RAM logging */
		seh_mipsram_stop(TRUE);

		/* Trigger dump */
		seh_dev->msg.msgId = EEH_MIPSRAM_DUMP;
		up(&seh_dev->read_sem);
		wake_up_interruptible(&seh_dev->readq);
		break;

	case SEH_IOCTL_DONT_SLEEP:
		seh_dont_sleep(TRUE);
		break;
	case SEH_IOCTL_SLEEP_ALLOWED:
		seh_dont_sleep(FALSE);
		break;
	case SEH_IOCTL_EEH_TIMER_EXPIRED:
		/*Msg from EEH-timer obtained
		   Send it back into the EEH-Task for the handling */
		if (down_interruptible(&seh_dev->read_sem))
			return -EFAULT;
		seh_dev->msg.msgId = EEH_TIMER_EXPIRED_MSG;
		seh_dev->msg.msgDesc[0] = 77;
		up(&seh_dev->read_sem);
		wake_up_interruptible(&seh_dev->readq);
		break;
	case SEH_IOCTL_EMULATE_PANIC:
		panic("ASSERT goto panic\n");
		break;
	case SEH_IOCTL_SET_ERROR_INFO:
		{
			EehErrorInfo param;
			if (copy_from_user
			    (&param, (EehErrorInfo *) arg,
			     sizeof(EehErrorInfo)))
				return -EFAULT;
			eeh_save_error_info(&param);
			break;
		}

	case SEH_IOCTL_RAMDUMP_IN_ADVANCE:
		seh_ramdump_prepare_in_advance(1);
		break;

	default:
		ret = -ENOTTY;
		break;
	}

	return ret;
}

static unsigned int seh_poll(struct file *filp, poll_table *wait)
{
	struct seh_dev *dev = filp->private_data;
	unsigned int mask = 0;

	ENTER();

	poll_wait(filp, &dev->readq, wait);

	if (dev->msg.msgId != EEH_INVALID_MSG)	/* read finished */
		mask |= POLLIN | POLLRDNORM;

	LEAVE();

	return mask;
}

/* device memory map method */
static void seh_vma_open(struct vm_area_struct *vma)
{
	DBGMSG("SEH OPEN 0x%lx -> 0x%lx (0x%lx)\n", vma->vm_start,
	       vma->vm_pgoff << PAGE_SHIFT, vma->vm_page_prot);
}

static void seh_vma_close(struct vm_area_struct *vma)
{
	DBGMSG("SEH CLOSE 0x%lx -> 0x%lx\n", vma->vm_start,
	       vma->vm_pgoff << PAGE_SHIFT);
}

static struct vm_operations_struct vm_ops = {
	.open = seh_vma_open,
	.close = seh_vma_close
};

/*
 vma->vm_end, vma->vm_start : specify the user space process address range assigned when mmap has been called;
 vma->vm_pgoff - is the physical address supplied by user to mmap in the last argument (off)
 However, mmap restricts the offset, so we pass this shifted 12 bits right.
 */
static int seh_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long pa =
	    __phys_to_pfn((unsigned long)
			  ADDR_CONVERT(__pfn_to_phys(vma->vm_pgoff)));

	vma->vm_pgoff = pa;

	/* we do not want to have this area swapped out, lock it */
	vma->vm_flags |= (VM_DONTEXPAND | VM_DONTDUMP | VM_IO);
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (io_remap_pfn_range(vma, vma->vm_start, pa,	/* physical page index */
			       size, vma->vm_page_prot)) {
		ERRMSG("remap page range failed\n");
		return -ENXIO;
	}
	vma->vm_ops = &vm_ops;
	seh_vma_open(vma);
	return 0;
}

static int seh_release(struct inode *inode, struct file *filp)
{
	ENTER();
	/* Just release the resource when seh_open_count is 0 */
	return 0;
}

static void seh_dev_release(struct device *dev)
{
	return;
}

static int seh_remove(struct platform_device *dev)
{
	ENTER();

	misc_deregister(&seh_miscdev);
	if (seh_dev->cwsbr)
		iounmap(seh_dev->cwsbr);
	if (seh_dev->mpmu_com_reset)
		iounmap(seh_dev->mpmu_com_reset);
	if (seh_dev->gen_reg5)
		iounmap(seh_dev->gen_reg5);
	kfree(seh_dev);
	destroy_workqueue(seh_int_wq);

	LEAVE();
	return 0;
}

int seh_draw_panic_text(const char *panic_text, size_t panic_len, int do_timer)
{
	int status = 0;
	/* stop MIPSRAM logging */
	seh_mipsram_stop(TRUE);
	pr_debug("Enter %s\n", __func__);
	pr_crit("%s:%d:%d\n", panic_text, panic_len, do_timer);

	{
		/*Customer should add draw_panic_text function here */
	}

	seh_dev->msg.msgId = EEH_AP_ASSERT_MSG;
	strncpy(seh_dev->msg.msgDesc, panic_text, sizeof(seh_dev->msg.msgDesc));
	seh_dev->msg.msgDesc[sizeof(seh_dev->msg.msgDesc) - 1] = 0;
	pr_err("SEH: %s\n", seh_dev->msg.msgDesc);
	up(&seh_dev->read_sem);
	wake_up_interruptible(&seh_dev->readq);

	if (in_interrupt() || in_irq())
		pr_debug("ASSERT on INT or IRQ context/n");
	else {
		while (!b_start_recovery)
			msleep_interruptible(999);
	}
	return status;
}
EXPORT_SYMBOL(seh_draw_panic_text);

/* Kernel mode callstack implementation
* Depends also upon CONFIG_FRAME_POINTER and CONFIG_PRINTK presence
 */
#include <linux/kallsyms.h>

struct str_desc {
	char *string;
	int len;
	int index;
};

/* Overloaded implementation is to feed the output into a string buffer (original code does printk)*/
/* The original code is in linux/lib/vsprintf.c */
int print_bk(struct str_desc *sd, const char *fmt, ...)
{
	va_list args;
	int i;
	va_start(args, fmt);
	i = vsnprintf(&sd->string[sd->index], sd->len - sd->index, fmt, args);
	va_end(args);
	sd->index += i;
	if (sd->index > sd->len)
		sd->index = sd->len;
	return i;
}

/* The original code is linux/kernel/kallsyms.c */
static void print_symbol_bk(struct str_desc *sd, const char *fmt,
			    unsigned long address)
{
	char buffer[KSYM_SYMBOL_LEN];

	sprint_symbol(buffer, address);
	print_bk(sd, fmt, buffer);
}

/* The original code is linux/arch/arm/kernel/traps.c */
void dump_backtrace_entry_bk(struct str_desc *sd, unsigned long where,
			     unsigned long from, unsigned long frame)
{
#ifdef CONFIG_KALLSYMS
	print_bk(sd, "[<%08lx>] ", where);
	print_symbol_bk(sd, "(%s) ", where);
	print_bk(sd, "from [<%08lx>] ", from);
	print_symbol_bk(sd, "(%s)\n", from);
#else
	print_bk(sd, "Function entered at [<%08lx>] from [<%08lx>]\n", where,
		 from);
#endif
}

void tel_assert_util(char *file, char *function, int line, int param)
{
	char buffer[512];
	char *shfile;
	struct str_desc sd = { buffer, sizeof(buffer), 0 };

	/* Remove the path from file name: normally long, not mandatory, and costs missing backtrace info */
	shfile = strrchr(file, '/');
	if (shfile != NULL)
		file = shfile + 1;	/* next char after '/' */

	print_bk(&sd, "KERNEL ASSERT at %s #%d (%s)\nin tid [%d]\n", file, line,
		 function, current ? current->pid : 0);
	back_trace_k(&sd);
#ifdef HAWK_ENABLED
	if (hawk_fota_active) {
		pr_err
		    ("KERNEL ASSERT disregarded since FOTA process activated.\n");
		return;
	}
#endif
	seh_draw_panic_text(buffer, strlen(buffer), 0);
}
EXPORT_SYMBOL(tel_assert_util);

static void seh_dont_sleep(int dont_sleep_true)
{
#ifdef CONFIG_PXA95x_DVFM
	static int seh_power_handler = (int)NULL;
	if (seh_power_handler == (int)NULL) {
		dvfm_register("EEH", &seh_power_handler);
		if (seh_power_handler == (int)NULL) {
			pr_debug("PXA Power driver - fail to init EEH\n\r");
			return;
		}
	}
#endif

	if (dont_sleep_true == TRUE) {
#ifdef CONFIG_PXA95x_DVFM
		dvfm_disable_lowpower(seh_power_handler);
#endif
		SEH_PM_WAKE_LOCK();
	} else {
		SEH_PM_WAKE_UNLOCK();
#ifdef CONFIG_PXA95x_DVFM
		dvfm_enable_lowpower(seh_power_handler);
#endif
	}
}

#ifdef CONFIG_PXA_MIPSRAM
static void mipsram_vma_open(struct vm_area_struct *vma)
{
#if defined(MIPSRAM_DEBUG)
	pr_debug("MIPSRAM OPEN 0x%lx -> 0x%lx (0x%lx)\n",
		 vma->vm_start, vma->vm_pgoff << PAGE_SHIFT, vma->vm_page_prot);
#endif
}

static void mipsram_vma_close(struct vm_area_struct *vma)
{
#if defined(MIPSRAM_DEBUG)
	pr_debug("MIPSRAM CLOSE 0x%lx -> 0x%lx\n", vma->vm_start,
		 vma->vm_pgoff << PAGE_SHIFT);
#endif
}

/* device memory map method */
/*
 *	vma->vm_end, vma->vm_start : specify the user space process address
 *	range assigned when mmap has been called;
 *	vma->vm_pgoff - is the physical address supplied by user to
 *	mmap in the last argument (off)
 *	However, mmap restricts the offset, so we pass this
 *	shifted 12 bits right.
 */
int mipsram_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret_val = 0;
	unsigned long size = 0;
	unsigned long pfn;

	/* we do not want to have this area swapped out, lock it */
	vma->vm_flags |= (VM_DONTEXPAND | VM_DONTDUMP | VM_IO);
	/* see linux/drivers/char/mem.c */
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	/* TBD: without this vm_page_prot=0x53 and write seem to not
	 * reach the destination:
	 *   - no fault on write
	 *   - read immediately (same user process) return the new value
	 *   - read after a second by another user process instance
	 *     return original value
	 * Why PROT_WRITE specified by mmap caller does not take effect?
	 * MAP_PRIVATE used by app results in copy-on-write behaviour, which is
	 * irrelevant for this application
	 * vma->vm_page_prot|=L_PTE_WRITE; */

	/* Map MIPSRAM buffer to userspace */
	pfn = MIPSRAM_get_descriptor()->buffer_phys_ptr;
	size = (MIPS_RAM_BUFFER_SZ * sizeof(unsigned int));

	/* re-map page to user-space, we assume the following:
	 * - MIPSRAM buffer is consecutive in physical memory */
	ret_val =
	    remap_pfn_range(vma, (unsigned int)vma->vm_start,
			    (pfn >> PAGE_SHIFT), size, vma->vm_page_prot);
	if (ret_val < 0) {
		pr_debug("%s failed to re-map!\n", __func__);
		return ret_val;
	}

	vma->vm_ops = &mipsram_vm_ops;
	mipsram_vma_open(vma);
	return 0;
}
#endif /* CONFIG_PXA_MIPSRAM */

#ifdef SEH_RAMDUMP_ENABLED
#include <linux/swap.h>		/*nr_free_buffer_pages and similiar */
#include <linux/vmstat.h>	/*global_page_state */

/*#define RAMFILE_DEBUG*/
static void ramfile_vma_close(struct vm_area_struct *vma);
static struct vm_operations_struct ramfile_vm_ops = {
	.close = ramfile_vma_close
};

#define RAMFILE_LOW_WATERMARK 0x40000
#define RAMFILE_USE_VMALLOC
/* NOTE: the size requested by user already accounts for ramfile header */
static int ramfile_mmap(struct file *file, struct vm_area_struct *vma)
{
	int ret_val = 0;
	unsigned long usize = vma->vm_end - vma->vm_start;
	void *pbuf;

	/* Check we don't exhaust all system memory to prevent crash before EEH
	   is done with saving logs. Use the total free for now */

	unsigned int avail_mem = global_page_state(NR_FREE_PAGES) * PAGE_SIZE;
	pr_err("ramfile_mmap(0x%lx), available 0x%x\n", usize, avail_mem);
	if (avail_mem < RAMFILE_LOW_WATERMARK) {
		pr_err("Rejected\n");
		return -ENOMEM;
	}
#ifndef RAMFILE_USE_VMALLOC
	/* Note: kmalloc allocates physically continous memory.
	   vmalloc would allocate potentially physically discontinuous memory.
	   The advantage of vmalloc is that it would be able to allocate more
	   memory when physical memory available is fragmented */
	pbuf = kmalloc(usize, GFP_KERNEL);
#else
	/* vmalloc() is not suitable - cannot be mapped to user space, see VM_USERMAP */
	pbuf = vmalloc_user(usize);
#endif

#ifdef RAMFILE_DEBUG
	pr_err("ramfile_mmap(0x%x): ka=%.8x ua=%.8x\n",
	       (unsigned)usize, (unsigned)pbuf, (unsigned int)vma->vm_start);
#endif
	if (!pbuf)
		return -ENOMEM;

	vma->vm_flags |= (VM_DONTEXPAND | VM_DONTDUMP | VM_IO);
	/* DO NOT map as non-cached, otherwise dirty data present for the
	   kernel side mapping will corrupt the data written in user-space.
	   Such dirty data is likely present with vmalloc_user(),
	   which zeroes the entire area to prevent exposure of random
	   kernel data to user space. Caches are usually read-allocate,
	   but if valid lines are present in caches for the relevant physical
	   addresses, zero'ing will make them dirty.
	   vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	 */

	/* Allocated. Map this to user space and let it fill in the data.
	   We do not want to waste a whole page for the ramfile_desc header,
	   so we map all the buffer to user space, which should reserved the
	   header area.
	   We will fill the header and link it into the ramdump when user
	   space is done and calls unmap. This way user mistake corrupting
	   the header will not compromise the kernel operation.
	   vm_pgoff is not used or changed (only for file mappins) */
	vma->vm_pgoff = (unsigned)pbuf;
#ifndef RAMFILE_USE_VMALLOC
	ret_val =
	    remap_pfn_range(vma, (unsigned int)vma->vm_start, vma->vm_pgoff,
			    usize, vma->vm_page_prot);
	if (ret_val < 0) {
		kfree(pbuf);
		return ret_val;
	}
#else
	ret_val = remap_vmalloc_range(vma, pbuf, 0);
	if (ret_val < 0) {
		vfree(pbuf);
		return ret_val;
	}
#endif

	vma->vm_ops = &ramfile_vm_ops;
	return 0;
}

static void ramfile_vma_close(struct vm_area_struct *vma)
{
	struct ramfile_desc *prf;
	unsigned long usize = vma->vm_end - vma->vm_start;

	/* Fill in the ramfile desc (header) */
	prf = (struct ramfile_desc *)vma->vm_pgoff;
#ifndef RAMFILE_USE_VMALLOC
	prf->flags = RAMFILE_PHYCONT;
#else
	prf->flags = 0;
#endif
	prf->payload_size = usize - sizeof(struct ramfile_desc);
	memset((void *)&prf->reserved[0], 0, sizeof(prf->reserved));
	ramdump_attach_ramfile(prf);
#ifdef RAMFILE_DEBUG
	pr_err("ramfile close 0x%x - linked into RDC\n", (unsigned)prf);
#endif
}
#endif /*SEH_RAMDUMP_ENABLED */

/* eeh_save_error_info: save the error ID/string into ramdump */
static void eeh_save_error_info(EehErrorInfo *info)
{
#if !defined(SEH_RAMDUMP_ENABLED)
#define RAMDUMP_ERR_STR_LEN 100
#define RAMDUMP_ERR_EEH_CP 0x80000000
#define RAMDUMP_ERR_EEH_AP 0x80000100
#endif
	char str[RAMDUMP_ERR_STR_LEN];
	char *s = 0;
	struct pt_regs regs;
	struct pt_regs *p = 0;
	unsigned err;
	err =
	    (info->err == ERR_EEH_CP) ? RAMDUMP_ERR_EEH_CP : RAMDUMP_ERR_EEH_AP;

	if (info->str && !copy_from_user(str, info->str, sizeof(str))) {
		str[sizeof(str) - 1] = 0;
		s = strchr(str, '\n');
		if (s != NULL)
			*s = 0;
		s = &str[0];
	}
	memset(&regs, 0, sizeof(struct pt_regs));
#if defined(SEH_RAMDUMP_ENABLED)
	if (info->regs
	    && !copy_from_user(&regs, (struct pt_regs *)info->regs,
			       sizeof(regs)))
		p = &regs;

	ramdump_save_dynamic_context(s, (int)err, NULL, p);
	pr_err("SEH saved error info: %.8x (%s)\n", err, s);
#else
	p = p;			/*unused */
	pr_err("SEH saved error info: %.8x (%s)\n", err, s);
	pr_err("RAMDUMP is not configured\n");
#endif
}

module_init(seh_init);
module_exit(seh_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marvell");
MODULE_DESCRIPTION("Marvell System Error Handler.");
