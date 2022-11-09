/*
 * This software program is licensed subject to the GNU General Public License
 * (GPL).Version 2,June 1991, available at http://www.fsf.org/copyleft/gpl.html

 * (C) Copyright 2013 Marvell International Ltd.
 * All Rights Reserved
 */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/aio.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <mach/hardware.h>
#include <mach/irqs.h>
#include <asm/system.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/mount.h>
#include <linux/pxa9xx_acipc_v2.h>
#if defined(CONFIG_PXA95x_DVFM)
#include <mach/dvfm.h>
#include <mach/pxa95x_dvfm.h>
#endif
#ifdef CONFIG_PXA_MIPSRAM
#include <mach/pxa_mips_ram.h>
#else
#define MIPS_RAM_ADD_PM_TRACE(a) {}
#endif
#include <asm/mach-types.h>

#if defined(CONFIG_PXA95x) || defined(CONFIG_PXA93x)
#include <mach/regs-ost.h>
#include <mach/hardware.h>
#endif

#include <linux/of.h>
#include <linux/cputype.h>
struct pxa9xx_acipc {
	struct resource *mem;
	void __iomem *mmio_base;
	int irq[ACIPC_NUMBER_OF_INTERRUPTS];
	char irq_name[ACIPC_NUMBER_OF_INTERRUPTS][20];
	u32 iir_val;
	struct acipc_database db;
	u32 bind_event_arg;
	wait_queue_head_t wait_q;
	int poll_status;
	spinlock_t poll_lock;
	unsigned long flags;
	u32 historical_events;
	struct ddr_status g_ddr_status;
	int dev_available;
	enum acipc_dev_type idx;
	u32 ddr_avail_flag;
	struct miscdevice *misc;
#if defined(CONFIG_PXA95x_DVFM)
	struct acipc_lock *lck;
#endif

};
static struct pxa9xx_acipc *acipc_devs[ACIPC_NUMBER_OF_DEVICES];

#if defined(CONFIG_PXA95x) || defined(CONFIG_PXA93x)

#define ACIPC_DDR_NOT_AVAIL		0
#define ACIPC_DDR_AVAIL			1

u32 set_ddr_avail_flag(enum acipc_dev_type type)
{
	unsigned long flags;
	/*
	 * If there's no CP acipc request should return without
	 * HW handling. SW returns expected value in order to avoid
	 * warnings
	 */
	BUG_ON(type != ACSIPC_DEV);
	if (!acipc_devs[type]->dev_available)
		return 1;

	local_irq_save(flags);

	if (acipc_devs[type]->ddr_avail_flag)
		pr_warn(
		"******EDDR ***WARNING*** shared flag=1 althoght it already on\n");
	/*
	 * TODO - need to create a shadow for WDR
	 * register and implement clear and set Marcos
	 */
	acipc_writel(acipc_devs[type]->idx, IPC_WDR, ACIPC_DDR_AVAIL);
	acipc_devs[type]->ddr_avail_flag = 1;

	local_irq_restore(flags);
	return 1;
}
EXPORT_SYMBOL(set_ddr_avail_flag);
#endif

/* ACIPC registers offsets */
#define IPC_WDR		0x0004
#define IPC_ISRW	0x0008
#define IPC_ICR		0x000C
#define IPC_IIR		0x0010
#define IPC_RDR		0x0014

#define acipc_readl(idx, off)	__raw_readl(acipc_devs[idx]->mmio_base + off)
#define acipc_writel(idx, off, v)\
	__raw_writel((v), acipc_devs[idx]->mmio_base + off)

#if defined(CONFIG_CPU_PXA978)
static const enum acipc_events acipc_priority_table_dkb[ACIPC_NUMBER_OF_EVENTS]
	= {
	ACIPC_DDR_READY_REQ,
	ACIPC_DDR_RELQ_REQ,
	ACIPC_RINGBUF_TX_STOP,
	ACIPC_RINGBUF_TX_RESUME,
	ACIPC_PORT_FLOWCONTROL,
	ACIPC_SPARE,
	ACIPC_SPARE,
	ACIPC_SPARE,
	ACIPC_SHM_PACKET_NOTIFY,
	ACIPC_SHM_PEER_SYNC
};
#endif

#if defined(CONFIG_CPU_PXA910)
static const enum acipc_events
	acipc_priority_table_dkb[ACIPC_NUMBER_OF_EVENTS] = {
	ACIPC_RINGBUF_TX_STOP,
	ACIPC_RINGBUF_TX_RESUME,
	ACIPC_PORT_FLOWCONTROL,
	ACIPC_SPARE,
	ACIPC_SPARE,
	ACIPC_SPARE,
	ACIPC_SPARE,
	ACIPC_SPARE,
	ACIPC_SHM_PACKET_NOTIFY,
	ACIPC_IPM
};
#elif defined(CONFIG_CPU_PXA988) || defined(CONFIG_CPU_PXA1088)
static const enum acipc_events
	acipc_priority_table_dkb[ACIPC_NUMBER_OF_EVENTS] = {
	ACIPC_RINGBUF_TX_STOP,
	ACIPC_RINGBUF_TX_RESUME,
	ACIPC_PORT_FLOWCONTROL,
	ACIPC_MODEM_DDR_UPDATE_REQ,
	ACIPC_RINGBUF_PSD_TX_STOP,
	ACIPC_RINGBUF_PSD_TX_RESUME,
	ACIPC_SHM_PSD_PACKET_NOTIFY,
	ACIPC_SHM_DIAG_PACKET_NOTIFY,
	ACIPC_SHM_PACKET_NOTIFY,
	ACIPC_IPM
};
/* PXA95x/93x/1986 specific define */
#elif defined(CONFIG_PXA95x) || defined(CONFIG_PXA93x) || \
		defined(CONFIG_CPU_PXA1986)

static const enum acipc_events acipc_priority_table[ACIPC_NUMBER_OF_EVENTS] = {
	ACIPC_DDR_READY_REQ,
	ACIPC_DDR_260_READY_REQ,
	ACIPC_DDR_RELQ_REQ,
	ACIPC_DDR_260_RELQ_REQ,
	ACIPC_DATA_Q_ADRS,
	ACIPC_DATA_IND,
	ACIPC_MSL_WAKEUP_REQ,
	ACIPC_MSL_WAKEUP_ACK,
	ACIPC_MSL_SLEEP_ALLOW,
	ACIPC_SPARE_1
};

/* write ICR/ISRW (may require dummy access)*/
#define acipc_writel_withdummy(idx , off, v)	\
do {	\
	/*superss compiler optimization*/ \
	unsigned long *dummy; \
	spin_lock_irqsave(&acipc_devs[idx]->poll_lock, \
			  acipc_devs[idx]->flags); \
	acipc_writel(idx, (off), (v)); \
	/* A dummy read is required after write */ \
	dummy = (unsigned long *)(acipc_readl(idx, off));\
	(void)dummy; /* prevent unused variable warning */ \
	spin_unlock_irqrestore(&acipc_devs[idx]->poll_lock, \
			       acipc_devs[idx]->flags); \
} while	(0)
#endif

/* PXA910 specific define */
#if defined(CONFIG_CPU_PXA910) || defined(CONFIG_CPU_PXA988) \
		|| defined(CONFIG_CPU_PXA1088)
#define acipc_writel_withdummy(idx, off, v)	acipc_writel(idx, off, (v))
#endif


/* read IIR*/
#define ACIPC_IIR_READ(idx, iir_val)		\
{						\
	/* dummy write to latch the IIR value*/	\
	acipc_writel(idx, IPC_IIR, 0x0);	\
	barrier();				\
	(iir_val) = acipc_readl(idx, IPC_IIR);	\
}

#if defined(CONFIG_CPU_PXA1986) || defined(CONFIG_PXA95x)
static u32 acipc_default_callback(u32 status, enum acipc_dev_type idx)
{

	IPC_ENTER();
	/* getting here means that the client didn't yet bind his callback.
	 * we will save the event until the bind occur
	 */

	acipc_devs[idx]->db.historical_event_status |= status;

	IPC_LEAVE();
	return 0;
}
#else
static u32 acipc_default_callback(u32 status)
{

	IPC_ENTER();
	enum acipc_dev_type idx = ACSIPC_DEV;
	/* getting here means that the client didn't yet bind his callback.
	 * we will save the event until the bind occur
	 */

	acipc_devs[idx]->db.historical_event_status |= status;

	IPC_LEAVE();
	return 0;
}
#endif

static enum acipc_return_code acipc_event_set(enum acipc_dev_type idx,
					      enum acipc_events user_event)
{
	IPC_ENTER();
	acipc_writel_withdummy(idx, IPC_ISRW, user_event);
	IPCTRACE("acipc_event_set: device %d userEvent 0x%x\n",
		 idx, user_event);

	IPC_LEAVE();
	return ACIPC_RC_OK;
}

static void acipc_int_enable(enum acipc_dev_type idx, enum acipc_events event)
{
	IPCTRACE("acipc_int_enable: device %d, event 0x%x\n", idx, event);

	if (event & ACIPC_INT0_EVENTS) {
		/*
		 * for the first 8 bits we enable the INTC_AC_IPC_0
		 * only if this the first event that has been binded
		 */
		if (!(acipc_devs[idx]->db.int0_events_cnt))
			enable_irq(acipc_devs[idx]->irq[0]);

		acipc_devs[idx]->db.int0_events_cnt |= event;
		return;
	}
	if (event & ACIPC_INT1_EVENTS) {
		enable_irq(acipc_devs[idx]->irq[1]);
		return;
	}
	if (event & ACIPC_INT2_EVENTS) {
		enable_irq(acipc_devs[idx]->irq[2]);
		return;
	}
}

static void acipc_int_disable(enum acipc_dev_type idx, enum acipc_events event)
{

	IPC_ENTER();

	IPCTRACE("acipc_int_disable: device %d, event 0x%x\n", idx, event);
	if (event & ACIPC_INT0_EVENTS) {
		if (!acipc_devs[idx]->db.int0_events_cnt)
			return;

		acipc_devs[idx]->db.int0_events_cnt &= ~event;
		/*
		 * for the first 8 bits we disable INTC_AC_IPC_0
		 * only if this is the last unbind event
		 */
		if (!(acipc_devs[idx]->db.int0_events_cnt))
			disable_irq(acipc_devs[idx]->irq[0]);
		return;
	}
	if (event & ACIPC_INT1_EVENTS) {
		disable_irq(acipc_devs[idx]->irq[1]);
		return;
	}
	if (event & ACIPC_INT2_EVENTS) {
		disable_irq(acipc_devs[idx]->irq[2]);
		return;
	}
}

#if defined(CONFIG_PXA95x) || defined(CONFIG_PXA93x)
/*driver state should be removed- dead code*/
static void acipc_change_driver_state(enum acipc_dev_type idx, int is_ddr_ready)
{
	IPC_ENTER();

	IPCTRACE("acipc_change_driver_state isDDRReady %d\n", is_ddr_ready);
	if (is_ddr_ready)
		acipc_devs[idx]->db.driver_mode = ACIPC_CB_NORMAL;
	else
		acipc_devs[idx]->db.driver_mode = ACIPC_CB_ALWAYS_NO_DDR;

	IPC_LEAVE();
}
#endif

static enum acipc_return_code acipc_data_send(enum acipc_dev_type idx,
					      enum acipc_events user_event,
					      acipc_data data)
{
	IPC_ENTER();
	IPCTRACE("acipc_data_send: device %d, userEvent 0x%x, data 0x%x\n", idx,
		 user_event, data);
	/* writing the data to WDR */
	acipc_writel(idx, IPC_WDR, data);

	/*
	 * fire the event to the other subsystem
	 * to indicate the data is ready for read
	 */
	acipc_writel_withdummy(idx, IPC_ISRW, user_event);

	IPC_LEAVE();
	return ACIPC_RC_OK;
}

static enum acipc_return_code acipc_data_read(enum acipc_dev_type idx,
					      acipc_data *data)
{
	IPC_ENTER();
	/* reading the data from RDR */
	*data = acipc_readl(idx, IPC_RDR);
	IPC_LEAVE();

	return ACIPC_RC_OK;
}

static enum acipc_return_code acipc_event_status_get(enum acipc_dev_type idx,
						     u32 user_event,
						     u32 *status)
{
	u32 iir_local_val;

	IPC_ENTER();
	/* reading the status from IIR */
	ACIPC_IIR_READ(idx, iir_local_val);

	/* clear the events hw status */
	acipc_writel_withdummy(idx, IPC_ICR, user_event);

	/*
	 * verify that this event will be cleared from the global IIR variable.
	 * for cases this API is called from user callback
	 */
	acipc_devs[idx]->iir_val &= ~(user_event);

	*status = iir_local_val & user_event;

	IPC_LEAVE();
	return ACIPC_RC_OK;
}

static enum acipc_return_code acipc_event_bind(enum acipc_dev_type idx,
					       u32 user_event,
					       acipc_rec_event_callback cb,
					       enum acipc_callback_mode cb_mode,
					       u32 *historical_event_status)
{
	u32 i;
	struct acipc_database_cell *p_event_db;
	IPC_ENTER();

	for (i = 0; i < ACIPC_NUMBER_OF_EVENTS; i++) {
		p_event_db = &acipc_devs[idx]->db.event_db[i];
		if (p_event_db->iir_bit & user_event) {
			if (p_event_db->cb != acipc_default_callback)
				return ACIPC_EVENT_ALREADY_BIND;
			p_event_db->cb = cb;
			p_event_db->mode = cb_mode;
			p_event_db->mask = user_event & p_event_db->iir_bit;
			acipc_int_enable(idx, p_event_db->iir_bit);
		}
	}
	/* if there were historical events */
	if (acipc_devs[idx]->db.historical_event_status & user_event) {
		if (historical_event_status) {
			*historical_event_status = user_event &
			    acipc_devs[idx]->db.historical_event_status;
		}
		/* clear the historical events from the database */
		acipc_devs[idx]->db.historical_event_status &= ~user_event;
		return ACIPC_HISTORICAL_EVENT_OCCUR;
	}

	IPC_LEAVE();
	return ACIPC_RC_OK;
}

static enum acipc_return_code acipc_event_unbind(enum acipc_dev_type idx,
						 u32 user_event)
{
	u32 i;
	struct acipc_database_cell *p_event_db;
	IPC_ENTER();

	for (i = 0; i < ACIPC_NUMBER_OF_EVENTS; i++) {
		p_event_db = &acipc_devs[idx]->db.event_db[i];
		if (p_event_db->mask & user_event) {
			if (p_event_db->iir_bit & user_event) {
				acipc_int_disable(idx, p_event_db->iir_bit);
				p_event_db->cb = acipc_default_callback;
				p_event_db->mode = ACIPC_CB_NORMAL;
				p_event_db->mask = 0;
			}
			/*clean this event from other event's mask */
			p_event_db->mask &= ~user_event;
		}
	}

	IPC_LEAVE();
	return ACIPC_RC_OK;
}

static irqreturn_t acipc_interrupt_handler(int irq, void *dev_id)
{
	u32 i, on_events;

	struct pxa9xx_acipc *acipc = (struct pxa9xx_acipc *)dev_id;
	IPC_ENTER();
	ACIPC_IIR_READ(acipc->idx, acipc->iir_val);	/* read the IIR */
	/*using ACIPCEventStatusGet might cause getting here with IIR=0 */
	if (!acipc->iir_val) {
		IPC_LEAVE();
		return IRQ_HANDLED;
	}
	for (i = 0; i < ACIPC_NUMBER_OF_EVENTS; i++) {
		if (!((acipc->db.event_db[i].iir_bit & acipc->iir_val)
		      && (acipc->db.event_db[i].mode == ACIPC_CB_NORMAL)))
			continue;

		on_events = (acipc->iir_val) & (acipc->db.event_db[i].mask);

		/* clean the event(s) */
		acipc_writel_withdummy(acipc->idx, IPC_ICR, on_events);

		/*
		 * call the user callback with the status of
		 * other events as defined when the user called
		 * ACIPCEventBind
		 */
#if defined(CONFIG_CPU_PXA1986) || defined(CONFIG_PXA95x)
		acipc->db.event_db[i].cb(on_events, acipc->idx);
#else
		acipc->db.event_db[i].cb(on_events);
#endif
		/*
		 * if more then one event exist we clear
		 * the rest of the bits from the global
		 * iir_val so user callback will be called
		 * only once.
		 */
		acipc->iir_val &= (~on_events);
	}
	IPC_LEAVE();

	return IRQ_HANDLED;
}

#if defined(CONFIG_CPU_PXA1986) || defined(CONFIG_PXA95x)
static u32 user_callback(u32 events_status, enum acipc_dev_type idx)
{
	acipc_devs[idx]->bind_event_arg = events_status;
	acipc_devs[idx]->poll_status = 1;
	wake_up_interruptible(&acipc_devs[idx]->wait_q);

	return 0;
}
#else
static u32 user_callback(u32 events_status)
{
	enum acipc_dev_type idx = ACSIPC_DEV;
	acipc_devs[idx]->bind_event_arg = events_status;
	acipc_devs[idx]->poll_status = 1;
	wake_up_interruptible(&acipc_devs[idx]->wait_q);
}
#endif

#if defined(CONFIG_PXA95x_DVFM)

/*
 * The limitation is that constraint is added in initialization.
 * It will be removed at the first DDR request constraint.
 * Notice: At here, ddr208_cnt won't be updated.
 */

static void set_constraint(enum acipc_dev_type idx)
{
	spin_lock_irqsave(&acipc_devs[idx]->lck->lock,
			  acipc_devs[idx]->lck->flags);
	if (cpu_is_pxa95x()) {
		dvfm_disable_op_name("156M_HF", acipc_devs[idx]->lck->dev_idx);
		/*
		 * dvfm_disable_op_name("208M_HF",
		 * acipc_devs[idx]->lck->dev_idx);
		 */
	}
	dvfm_disable_op_name("D0CS", acipc_devs[idx]->lck->dev_idx);
	dvfm_disable_op_name("D1", acipc_devs[idx]->lck->dev_idx);
	dvfm_disable_op_name("D2", acipc_devs[idx]->lck->dev_idx);
	acipc_devs[idx]->lck->init = 0;
	spin_unlock_irqrestore(&acipc_devs[idx]->lck->lock,
			       acipc_devs[idx]->lck->flags);
}

static u32 acipc_kernel_callback(u32 events_status, enum acipc_dev_type idx)
{
	unsigned int received_data, rc = 0;
	IPC_ENTER();

	spin_lock_irqsave(&acipc_devs[idx]->lck->lock,
			  acipc_devs[idx]->lck->flags);

	if (events_status & ACIPC_DDR_READY_REQ) {
		MIPS_RAM_ADD_PM_TRACE(ACIPC_DDR_REQ_RECEIVED_MIPS_RAM);
		if (acipc_devs[idx]->lck->ddr208_cnt++ == 0) {
			dvfm_disable_op_name("D1",
					     acipc_devs[idx]->lck->dev_idx);
			dvfm_disable_op_name("D2",
					     acipc_devs[idx]->lck->dev_idx);
			MIPS_RAM_ADD_PM_TRACE(ACIPC_DVFM_CONSTRAITNS_SET);
		}
		if (acipc_devs[idx]->lck->init == 0)
			acipc_devs[idx]->lck->init = 1;
		acipc_event_set(idx, ACIPC_DDR_READY_ACK);
		if (!acipc_devs[idx]->ddr_avail_flag)
			set_ddr_avail_flag(idx);
	}
	if (events_status & ACIPC_DDR_RELQ_REQ) {
		/*
		 * DDR_RELQ_ACK shouldn't be responsed here because COMM
		 * will enter D2 without DDR_RELQ_ACK.
		 * If DDR_RELQ_ACK is sent, it will wake COMM immediately.
		 * acipc_event_set(ACIPC_DDR_RELQ_ACK);
		 * geeting the relative time form the comm
		 * side via ACIPC and notyfing dvfm when the
		 * next comm wakup will be.
		 */
		acipc_data_read(idx, &received_data);
		dvfm_notify_next_comm_wakeup_time(received_data);
		MIPS_RAM_ADD_PM_TRACE(ACIPC_DDR_REL_RECEIVED_MIPS_RAM);
		if (acipc_devs[idx]->lck->ddr208_cnt == 0) {
			pr_warn("%s: constraint was removed.n", __func__);
		} else if (--acipc_devs[idx]->lck->ddr208_cnt == 0) {
			dvfm_enable_op_name("D2",
					    acipc_devs[idx]->lck->dev_idx);
			dvfm_enable_op_name("D1",
					    acipc_devs[idx]->lck->dev_idx);
		}
	}
	if (events_status & ACIPC_DDR_260_READY_REQ) {
		/*
		 * Set the DDR to the highest frequency
		 * by disabling all PPs that dmcfs < 312MTPS
		 */
		if (acipc_devs[idx]->lck->ddr260_cnt++ == 0) {
			MIPS_RAM_ADD_PM_TRACE
			    (ACIPC_HF_DDR_REQ_RECEIVED_MIPS_RAM);
			rc = dvfm_disable_op_name("104M",
						  acipc_devs[idx]->
						  lck->dev_idx);
			if (rc) {
				pr_err("failed: %s (%d): error = %d\n",
				       __func__, __LINE__, rc);
				rc = -1;
			}
		}
		acipc_event_set(idx, ACIPC_DDR_260_READY_ACK);
		MIPS_RAM_ADD_PM_TRACE(ACIPC_HF_DDR_REQ_HANDHELD_MIPS_RAM);
	}
	if (events_status & ACIPC_DDR_260_RELQ_REQ) {
		acipc_event_set(idx, ACIPC_DDR_260_RELQ_ACK);
		if (acipc_devs[idx]->lck->ddr260_cnt == 0) {
			pr_warn("%s: constraint was removed.\n", __func__);
		} else if (--acipc_devs[idx]->lck->ddr260_cnt == 0) {
			MIPS_RAM_ADD_PM_TRACE
			    (ACIPC_HF_DDR_REL_RECEIVED_MIPS_RAM);
			rc = dvfm_enable_op_name("104M",
						 acipc_devs[idx]->lck->dev_idx);
			if (rc) {
				pr_err("failed: %s (%d): error = %d\n",
				       __func__, __LINE__, rc);
				rc = -1;
			}
		}
		MIPS_RAM_ADD_PM_TRACE(ACIPC_HF_DDR_REL_HANDHELD_MIPS_RAM);
	}
	if (acipc_devs[idx]->lck->ddr208_cnt ||
	    acipc_devs[idx]->lck->ddr260_cnt)
		acipc_change_driver_state(idx, 1);
	else
		acipc_change_driver_state(idx, 0);
	spin_unlock_irqrestore(&acipc_devs[idx]->lck->lock,
			       acipc_devs[idx]->lck->flags);

	IPC_LEAVE();
	return rc;
}

#else /* !CONFIG_PXA95x_DVFM */

static void set_constraint(enum acipc_dev_type idx)
{
}

#if defined(CONFIG_PXA95x) || defined(CONFIG_PXA93x)
static u32 acipc_kernel_callback(u32 events_status, enum acipc_dev_type idx)
{
#ifndef CONFIG_CPU_PXA910
	IPC_ENTER();

	if (events_status & ACIPC_DDR_READY_REQ) {
		acipc_event_set(idx, ACIPC_DDR_READY_ACK);
		if (!acipc_devs[idx]->ddr_avail_flag)
			set_ddr_avail_flag(idx);
		/* Remove incorrect bit */
		if (events_status & ACIPC_DDR_RELQ_REQ)
			events_status &= ~ACIPC_DDR_RELQ_REQ;
		acipc_change_driver_state(idx, 1);
	}
	if (events_status & ACIPC_DDR_RELQ_REQ) {
		acipc_event_set(idx, ACIPC_DDR_RELQ_ACK);
		acipc_change_driver_state(idx, 0);
	}
	if (events_status & ACIPC_DDR_260_READY_REQ) {
		acipc_event_set(idx, ACIPC_DDR_260_READY_ACK);
		acipc_change_driver_state(idx, 1);
	}

	if (events_status & ACIPC_DDR_260_RELQ_REQ) {
		acipc_event_set(idx, ACIPC_DDR_260_RELQ_ACK);
		acipc_change_driver_state(idx, 0);
	}
	IPC_LEAVE();
#endif
	return 0;
}

#endif /* CONFIG_PXA95x || CONFIG_PXA93x */
#endif /* CONFIG_PXA95X_DVFM */

#if defined(CONFIG_CPU_PXA910) || defined(CONFIG_CPU_PXA988) || \
	defined(CONFIG_CPU_PXA1088) || defined(CONFIG_CPU_PXA1986)
static void register_pm_events(enum acipc_dev_type type
			       __attribute__ ((unused)))
{
	return;
}
#else
static void register_pm_events(enum acipc_dev_type type)
{
	BUG_ON(type != ACSIPC_DEV);
	acipc_event_bind(type, ACIPC_DDR_RELQ_REQ | ACIPC_DDR_260_RELQ_REQ |
			 ACIPC_DDR_260_READY_REQ | ACIPC_DDR_READY_REQ,
			 acipc_kernel_callback, ACIPC_CB_NORMAL,
			 &acipc_devs[type]->historical_events);
}
#endif /* CONFIG_CPU_PXA910 */

static long acipc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct acipc_ioctl_arg acipc_arg;
	u32 status;
	int ret = 0;
	IPC_ENTER();

	if (copy_from_user(&acipc_arg,
			   (void __user *)arg, sizeof(struct acipc_ioctl_arg)))
		return -EFAULT;

	switch (cmd) {
	case ACIPC_SET_EVENT:
		acipc_event_set(acipc_arg.idx, acipc_arg.set_event);
		break;
	case ACIPC_GET_EVENT:
		acipc_event_status_get(acipc_arg.idx, acipc_arg.arg, &status);
		acipc_arg.arg = status;
		break;
	case ACIPC_SEND_DATA:
		acipc_data_send(acipc_arg.idx, acipc_arg.set_event,
				acipc_arg.arg);
		break;
	case ACIPC_READ_DATA:
		acipc_data_read(acipc_arg.idx, &acipc_arg.arg);
		break;
	case ACIPC_BIND_EVENT:
		acipc_event_bind(acipc_arg.idx, acipc_arg.arg, user_callback,
				 acipc_arg.cb_mode, &status);
		acipc_arg.arg = status;
		break;
	case ACIPC_UNBIND_EVENT:
		acipc_event_unbind(acipc_arg.idx, acipc_arg.arg);
		break;
	case ACIPC_GET_BIND_EVENT_ARG:
		acipc_arg.arg = acipc_devs[acipc_arg.idx]->bind_event_arg;
		break;
	default:
		ret = -1;
		break;
	}

	if (copy_to_user((void __user *)arg, &acipc_arg,
			 sizeof(struct acipc_ioctl_arg)))
		return -EFAULT;

	IPC_LEAVE();

	return ret;
}

static unsigned int acipc_poll(struct file *file, poll_table *wait)
{
	int i, dev_minor;
	enum acipc_dev_type idx;
	IPC_ENTER();

	idx = 0;
	dev_minor = iminor(file->f_path.dentry->d_inode) & 0xff;
	for (i = 0; i < ACIPC_NUMBER_OF_DEVICES; i++) {
		if (dev_minor == acipc_devs[i]->misc->minor) {
			idx = i;
			break;
		}
	}
	poll_wait(file, &acipc_devs[idx]->wait_q, wait);

	IPC_LEAVE();

	if (acipc_devs[idx]->poll_status == 0) {
		return 0;
	} else {
		acipc_devs[idx]->poll_status = 0;
		return POLLIN | POLLRDNORM;
	}
}

static const struct file_operations acipc_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = acipc_ioctl,
	.poll = acipc_poll,
};

static ssize_t acipc_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	acipc_data data;
	struct platform_device *pdev = to_platform_device(dev);
	struct pxa9xx_acipc *acipc = platform_get_drvdata(pdev);

	dev_info(&pdev->dev, "acipc: In show interface\n");
	pdev = to_platform_device(dev);
	/* dummy write to latch the IIR value */
	acipc_writel(acipc->idx, IPC_IIR, 0x0);
	barrier();
	acipc_data_read(acipc->idx, &data);
	return sprintf(buf, "IIR of acipc.%d = %x\n", acipc->idx, data);
}

static ssize_t acipc_store(struct device *dev,
			   struct device_attribute *attr, const char *buf,
			   size_t count)
{
	u32 event;
	struct platform_device *pdev = to_platform_device(dev);
	struct pxa9xx_acipc *acipc = platform_get_drvdata(pdev);

	sscanf(buf, "%x", &event);
	pr_info("acipc.%d: setting event: %x\n", acipc->idx, event);
	acipc_event_set(acipc->idx, event);
	return count;
}

static DEVICE_ATTR(acipc_debug, S_IRUGO, acipc_show, acipc_store);

static int pxa9xx_acipc_probe(struct platform_device *pdev)
{
	struct resource *res;
	int size;
	int ret = -EINVAL, irq;
	char *irq_name;
	int i;
	struct device_node *np = pdev->dev.of_node;
	enum acipc_dev_type idx = 0;
	char *misc_name;
#if defined(CONFIG_PXA95x_DVFM)
	char dvfm_name[20];
#endif

	/*(-1) means only one instance -> remain with index 0 */
	of_property_read_u32(np, "mrvl,acipc-idx", &pdev->id);
	if (pdev->id > 0)
		idx = pdev->id;
	acipc_devs[idx] = kzalloc(sizeof(struct pxa9xx_acipc), GFP_KERNEL);
	if (acipc_devs[idx] == NULL) {
		dev_err(&pdev->dev,
			"failed to allocate for acipc_device %d\n", idx);
		goto failed_acipc_device_alloc;
	}
	acipc_devs[idx]->idx = idx;
	acipc_devs[idx]->misc = kzalloc(sizeof(struct miscdevice), GFP_KERNEL);
	if (acipc_devs[idx]->misc == NULL) {
		dev_err(&pdev->dev, "failed to allocate miscdevice\n");
		goto free_acipc_dev;
	}
	misc_name = kzalloc(sizeof(char) * 20, GFP_KERNEL);
	if (!misc_name)
		goto failed_free_misc;
	sprintf(misc_name, "acipc.%d", idx);
	acipc_devs[idx]->misc->name = misc_name;
	acipc_devs[idx]->misc->minor = MISC_DYNAMIC_MINOR;
	acipc_devs[idx]->misc->fops = &acipc_fops;
	ret = misc_register(acipc_devs[idx]->misc);
	if (ret < 0)
		goto free_and_deregmisc;
#if defined(CONFIG_PXA95x_DVFM)
	acipc_devs[idx]->lck = kzalloc(sizeof(struct acipc_lock), GFP_KERNEL);
	if (acipc_devs[idx]->lck == NULL) {
		dev_err(&pdev->dev, "failed to allocate an acipc_lock %d\n",
			idx);
		goto free_and_deregmisc;
	}
	sprintf(dvfm_name, "acipc.%d", idx);
	acipc_devs[idx]->lck->lock = __SPIN_LOCK_UNLOCKED();
	acipc_devs[idx]->lck->dev_idx = -1;
	acipc_devs[idx]->lck->ddr208_cnt = 0;
	acipc_devs[idx]->lck->ddr260_cnt = 0;
	acipc_devs[idx]->lck->init = 0;
	dvfm_register((const char *)dvfm_name,
		      &(acipc_devs[idx]->lck->dev_idx));
#endif

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to get platform resource\n");
		ret = -ENODEV;
		goto free_acipc_dev_dvfm_lck;
	}

	size = resource_size(res);
	acipc_devs[idx]->mem = request_mem_region(res->start, size, pdev->name);
	if (acipc_devs[idx]->mem == NULL) {
		dev_err(&pdev->dev, "failed to request register memory\n");
		ret = -EBUSY;
		goto free_acipc_dev_dvfm_lck;
	}

	acipc_devs[idx]->mmio_base = ioremap_nocache(res->start, size);
	if (acipc_devs[idx]->mmio_base == NULL) {
		dev_err(&pdev->dev,
			"failed to ioremap registers. dev %d\n", idx);
		ret = -ENXIO;
		goto free_resource_and_mem;
	}
#if defined(CONFIG_CPU_PXA910) || defined(CONFIG_CPU_PXA988) || \
	defined(CONFIG_CPU_PXA1088)
	if (idx == ACSIPC_DEV)
		set_constraint(idx);
#endif
	/*init device database */
	for (i = 0; i < ACIPC_NUMBER_OF_EVENTS; i++) {
#if defined(CONFIG_CPU_PXA910) || defined(CONFIG_CPU_PXA988) || \
		defined(CONFIG_CPU_PXA1088)
		acipc_devs[idx]->db.event_db[i].iir_bit =
		    acipc_priority_table_dkb[i];
		acipc_devs[idx]->db.event_db[i].mask =
		    acipc_priority_table_dkb[i];
#elif defined(CONFIG_PXA95x) || defined(CONFIG_PXA93x) || \
		defined(CONFIG_CPU_PXA1986)
		acipc_devs[idx]->db.event_db[i].iir_bit =
		    acipc_priority_table[i];
		acipc_devs[idx]->db.event_db[i].mask = acipc_priority_table[i];
#endif
		acipc_devs[idx]->db.event_db[i].cb = acipc_default_callback;
		acipc_devs[idx]->db.event_db[i].mode = ACIPC_CB_NORMAL;
	}
	acipc_devs[idx]->db.driver_mode = ACIPC_CB_NORMAL;
	acipc_devs[idx]->db.int0_events_cnt = 0;

	init_waitqueue_head(&acipc_devs[idx]->wait_q);
	spin_lock_init(&acipc_devs[idx]->poll_lock);

	platform_set_drvdata(pdev, acipc_devs[idx]);

	for (i = 0; i < ACIPC_NUMBER_OF_INTERRUPTS; i++) {
		irq = platform_get_irq(pdev, i);
		acipc_devs[idx]->irq[i] = -1;
		irq_name = acipc_devs[idx]->irq_name[i];
		memset(irq_name, 0, 20);
		sprintf(irq_name, "pxa9xx-ACIPC%d", i);
		if (irq >= 0) {
			ret = devm_request_irq(&pdev->dev, irq,
					  acipc_interrupt_handler,
					  IRQF_NO_SUSPEND | IRQF_DISABLED |
					  IRQF_TRIGGER_HIGH, irq_name,
					  acipc_devs[idx]);
			disable_irq(irq);
		}
		if (irq < 0 || ret < 0) {
			ret = -ENXIO;
			goto failed_freeirqs;
		}
		acipc_devs[idx]->irq[i] = irq;
	}
	if (idx == ACSIPC_DEV)
		register_pm_events(idx);
	ret = sysfs_create_file(&pdev->dev.kobj, &dev_attr_acipc_debug.attr);
	if (ret != 0)
		goto failed_freeirqs;

	pr_info("pxa9xx AC-IPC.%d initialized!\n", idx);
	return 0;

failed_freeirqs:
	for (i = 0; i < ACIPC_NUMBER_OF_INTERRUPTS; i++) {
		if (acipc_devs[idx]->irq[i] >= 0)
			free_irq(acipc_devs[idx]->irq[i], acipc_devs[idx]);
	}
	iounmap(acipc_devs[idx]->mmio_base);
free_resource_and_mem:
	release_resource(acipc_devs[idx]->mem);
	kfree(acipc_devs[idx]->mem);

free_acipc_dev_dvfm_lck:
#if defined(CONFIG_PXA95x_DVFM)
	kfree(acipc_devs[idx]->lck);
#endif
free_and_deregmisc:
	kfree(misc_name);
	misc_deregister(acipc_devs[idx]->misc);
failed_free_misc:
	kfree(acipc_devs[idx]->misc);
free_acipc_dev:
	kfree(acipc_devs[idx]);
failed_acipc_device_alloc:
	return ret;
}

static int pxa9xx_acipc_remove(struct platform_device *pdev)
{
	struct pxa9xx_acipc *acipc = platform_get_drvdata(pdev);
#if defined(CONFIG_PXA95x_DVFM)
	char dvfm_name[8];
#endif
	sysfs_remove_file(&pdev->dev.kobj, &dev_attr_acipc_debug.attr);
	iounmap(acipc->mmio_base);
	release_resource(acipc->mem);
	kfree(acipc->mem);
#if defined(CONFIG_PXA95x_DVFM)
	sprintf(dvfm_name, "acipc.%d", acipc->idx);
	dvfm_unregister((const char *)dvfm_name, &(acipc->lck->dev_idx));
#endif
	misc_deregister(acipc->misc);
	kfree(acipc->misc->name);
	kfree(acipc->misc);
#if defined(CONFIG_PXA95x_DVFM)
	kfree(acipc->lck);
#endif
	kfree(acipc);
	return 0;
}

static struct of_device_id pxa9xx_acipc_dt_ids[] = {
	{ .compatible = "mrvl,mmp-acipc_v2", },
	{ .compatible = "mrvl,pxa-acipc_v2", },
	{}
};

static struct platform_driver pxa9xx_acipc_driver = {
	.driver = {
		   .name = "pxa9xx-acipc",
		   .of_match_table = pxa9xx_acipc_dt_ids,
		   },
	.probe = pxa9xx_acipc_probe,
	.remove = pxa9xx_acipc_remove
};

static int __init pxa9xx_acipc_init(void)
{
	return platform_driver_register(&pxa9xx_acipc_driver);
}

static void __exit pxa9xx_acipc_exit(void)
{
	platform_driver_unregister(&pxa9xx_acipc_driver);
}

#if defined(CONFIG_PXA95x) || defined(CONFIG_PXA93x) || \
	defined(CONFIG_CPU_PXA1986)
int set_acipc_cp_enable(enum acipc_dev_type idx, unsigned int pm_cp)
{
	acipc_devs[idx]->dev_available = pm_cp;
	return 0;
}
EXPORT_SYMBOL(set_acipc_cp_enable);

#ifdef CONFIG_CPU_PXA1986	/* DDR is shared between cores */
u32 clear_ddr_avail_flag(enum acipc_dev_type type)
{
	return 1;	/* returns true that is the logic here */
}
u32 set_ddr_avail_flag(enum acipc_dev_type type)
{
	return 1;	/* returns true that is the logic here */
}
#else /* !CONFIG_CPU_PXA1986 */
u32 clear_ddr_avail_flag(enum acipc_dev_type type)
{
	unsigned long flags, iir_val;
	/*
	 * if there's no CP acipc request should return without HW handling.
	 * SW returns expected value in order to avoid warnings
	 */
	BUG_ON(type != ACSIPC_DEV);
	if (!acipc_devs[type]->dev_available)
		return 1;

	local_irq_save(flags);

	/*this should never be true */
	if (acipc_devs[type]->ddr_avail_flag == 0) {
		pr_warn("****WARNING - attempting to clear ddr_avail_flag");
		pr_warn("although it is allreday cleared - ");
		pr_warn("this should not occur!!!\n");
		local_irq_restore(flags);
		return acipc_devs[type]->ddr_avail_flag;	/*returning 0 */
	}
	/* critical section ... */
	ACIPC_IIR_READ(acipc_devs[type]->idx, iir_val);
	if (!(iir_val & ACIPC_DDR_READY_REQ)) {
		/*
		 * TODO - need to create a shadow for WDR
		 * register and implement clear and set Marcos
		 */
		acipc_writel(acipc_devs[type]->idx,
			     IPC_WDR, ACIPC_DDR_NOT_AVAIL);
		acipc_devs[type]->ddr_avail_flag = 0;
		udelay(5);	/* ACIPC propagation delay */
		/*
		 * verify that DDR request did not arrive
		 * after we clear the shared flag
		 */
		ACIPC_IIR_READ(acipc_devs[type]->idx, iir_val);
		if (iir_val & ACIPC_DDR_READY_REQ) {
			pr_warn("******EDDR DDR_req arrive while trying to ");
			pr_warn("clear shared flag. should be rare\n");
			acipc_writel(acipc_devs[type]->idx, IPC_WDR,
				     ACIPC_DDR_AVAIL);
			/*
			 * restore shared DDR since comm
			 * DDR request was pending
			 */
			acipc_devs[type]->ddr_avail_flag = 1;
		}
	} else {
		if (acipc_devs[type]->ddr_avail_flag == 0)
			/*
			 * this can happen if ddr_req was pending while
			 * entering D0CS (not D2 because apps-comm sync) in
			 * that case comm is waiting for ddr_ack. since the
			 * PM state machine is not design to abort D0CS entry,
			 * in that case i rather ddr_req to take us out of
			 * D0CS. this behavior will not hurt the efficiency
			 * of this feature since we are setting the flag in
			 * D0CS exit this should happen only if ddr_request
			 * arrive between disable interrupt in the beginning
			 * of pxa3xx_set_op, and the call to
			 * clear_ddr_avail_flag so it is fairy rare case.
			 */
			pr_info("******EDDR DDR_req=1, shared flag=0 rare, ");
			pr_info("not risky\n");
		else
			/*
			 * this can happen if ddr_req was pending while
			 * entering D0CS (not D2 because apps-comm sync) in
			 * that case comm is *not* waiting for ddr_ack.
			 * returning the state of the flag, which in that case
			 * true, will prevent D0CS. this should happen only if
			 * ddr_request arrive between disable interrupt in the
			 * beginning of pxa3xx_set_op, and the call to
			 * clear_ddr_avail_flag so it is fairy rare case.
			 */
			pr_info("******EDDR DDR_req=1, shared flag=1 rare ");
			pr_info("and risky\n");
	}
	local_irq_restore(flags);

	return acipc_devs[type]->ddr_avail_flag;
}
#endif /* CONFIG_CPU_PXA1986 */

#if defined(CONFIG_PXA95x_DVFM)
void acipc_set_constraint_no_op_change(enum acipc_dev_type type)
{
	BUG_ON(type != ACSIPC_DEV);
	dvfm_disable_op_name_no_change("D0CS", acipc_devs[type]->lck->dev_idx);
	dvfm_disable_op_name_no_change("D1", acipc_devs[type]->lck->dev_idx);
	dvfm_disable_op_name_no_change("D2", acipc_devs[type]->lck->dev_idx);
}

void acipc_disable_d0cs_no_change(enum acipc_dev_type type)
{
	BUG_ON(type != ACSIPC_DEV);
	dvfm_disable_op_name_no_change("D0CS", acipc_devs[type]->lck->dev_idx);
}

void acipc_unset_constraint_no_op_change(enum acipc_dev_type type)
{
	dvfm_enable_op_name_no_change("D0CS", acipc_devs[type]->lck->dev_idx);
	dvfm_enable_op_name_no_change("D1", acipc_devs[type]->lck->dev_idx);
	dvfm_enable_op_name_no_change("D2", acipc_devs[type]->lck->dev_idx);
}

void acipc_disable_d0cs(enum acipc_dev_type type)
{
	BUG_ON(type != ACSIPC_DEV);
	dvfm_disable_op_name("D0CS", acipc_devs[type]->lck->dev_idx);
}
#endif /* CONFIG_PXA95x_DVFM */

void acipc_start_cp_constraints(enum acipc_dev_type type)
{
	BUG_ON(type != ACSIPC_DEV);
	set_constraint(type);
	set_ddr_avail_flag(type);
}
EXPORT_SYMBOL(acipc_start_cp_constraints);

u32 get_ddr_avail_state(enum acipc_dev_type type)
{
	BUG_ON(type != ACSIPC_DEV);
	return acipc_devs[type]->ddr_avail_flag;
}

u32 get_acipc_pending_events(enum acipc_dev_type type)
{
	u32 iir_val;
	BUG_ON(type != ACSIPC_DEV);
	ACIPC_IIR_READ(acipc_devs[type]->idx, iir_val);
	return iir_val;
}
#endif

/* ACIPC new (Ext) APIs*/
enum acipc_return_code ACIPCDataSendExt(enum acipc_dev_type type,
					enum acipc_events user_event,
					acipc_data data)
{
	return acipc_data_send(type, user_event, data);
}
EXPORT_SYMBOL(ACIPCDataSendExt);

enum acipc_return_code ACIPCDataReadExt(enum acipc_dev_type type,
					acipc_data *data)
{
	return acipc_data_read(type, data);
}
EXPORT_SYMBOL(ACIPCDataReadExt);

enum acipc_return_code ACIPCEventStatusGetExt(enum acipc_dev_type type,
					      u32 user_event, u32 *status)
{
	return acipc_event_status_get(type, user_event, status);
}
EXPORT_SYMBOL(ACIPCEventStatusGetExt);

enum acipc_return_code ACIPCEventBindExt(enum acipc_dev_type idx,
					 u32 user_event,
					 acipc_rec_event_callback cb,
					 enum acipc_callback_mode cb_mode,
					 u32 *historical_event_status)
{
	return acipc_event_bind(idx, user_event, cb, cb_mode,
				historical_event_status);
}
EXPORT_SYMBOL(ACIPCEventBindExt);

enum acipc_return_code ACIPCEventUnBindExt(enum acipc_dev_type type,
					   u32 user_event)
{
	return acipc_event_unbind(type, user_event);
}
EXPORT_SYMBOL(ACIPCEventUnBindExt);

enum acipc_return_code ACIPCEventSetExt(enum acipc_dev_type type,
					enum acipc_events user_event)
{
	return acipc_event_set(type, user_event);
}
EXPORT_SYMBOL(ACIPCEventSetExt);

/* old API */
enum acipc_return_code ACIPCEventBind(u32 user_event,
				      acipc_rec_event_callback cb,
				      enum acipc_callback_mode cb_mode,
				      u32 *historical_event_status)
{
	return acipc_event_bind(ACSIPC_DEV, user_event, cb, cb_mode,
				historical_event_status);
}
EXPORT_SYMBOL(ACIPCEventBind);

enum acipc_return_code ACIPCEventUnBind(u32 user_event)
{
	return acipc_event_unbind(ACSIPC_DEV, user_event);
}
EXPORT_SYMBOL(ACIPCEventUnBind);

enum acipc_return_code ACIPCEventSet(enum acipc_events user_event)
{
	return acipc_event_set(ACSIPC_DEV, user_event);
}
EXPORT_SYMBOL(ACIPCEventSet);

enum acipc_return_code ACIPCDataSend(enum acipc_events user_event,
				     acipc_data data)
{
	return acipc_data_send(ACSIPC_DEV, user_event, data);
}
EXPORT_SYMBOL(ACIPCDataSend);

enum acipc_return_code ACIPCDataRead(acipc_data *data)
{
	return acipc_data_read(ACSIPC_DEV, data);
}
EXPORT_SYMBOL(ACIPCDataRead);

enum acipc_return_code ACIPCEventStatusGet(u32 user_event, u32 *status)
{
	return acipc_event_status_get(ACSIPC_DEV, user_event, status);
}
EXPORT_SYMBOL(ACIPCEventStatusGet);

module_init(pxa9xx_acipc_init);
module_exit(pxa9xx_acipc_exit);
MODULE_AUTHOR("MARVELL");
MODULE_DESCRIPTION("AC-IPC driver");
MODULE_LICENSE("GPL");
