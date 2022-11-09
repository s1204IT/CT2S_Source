/*
 * (C) Copyright 2006-2011 Marvell International Ltd.
 * All Rights Reserved
 */

/******************************************************************************
 *
 *  COPYRIGHT (C) 2005 Intel Corporation.
 *
 *  This software as well as the software described in it is furnished under
 *  license and may only be used or copied in accordance with the terms of the
 *  license. The information in this file is furnished for informational use
 *  only, is subject to change without notice, and should not be construed as
 *  a commitment by Intel Corporation. Intel Corporation assumes no
 *  responsibility or liability for any errors or inaccuracies that may appear
 *  in this document or any software that may be provided in association with
 *  this document.
 *  Except as permitted by such license, no part of this document may be
 *  reproduced, stored in a retrieval system, or transmitted in any form or by
 *  any means without the express written consent of Intel Corporation.
 *
 *  FILENAME: data_channel.c
 *  PURPOSE:  data channel handling functions for ACI
 *****************************************************************************/

#include <linux/kernel.h>
#include <linux/kthread.h>
#include "linux_types.h"
#include <linux/socket.h>
#include <linux/netlink.h>
#include <net/sock.h>
#include <linux/sched.h>
#include <linux/console.h>
#include <linux/timer.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/wait.h>

#include <mach/hardware.h>
#include <mach/irqs.h>
/*#include <mach/pxa-regs.h> */
#include <mach/regs-ost.h>
#include <asm/mach-types.h>

#include "osa_kernel.h"
#include "ci_stub.h"
#include "ci_api.h"
#include "common_datastub.h"
#include "data_channel.h"
#include "acipc_data.h"
#include "pxa_dbg.h"
#include "eval_data.h"

#include "ac_rpc.h"

/* Linux Kernel implementation */

#include <linux/spinlock.h>

#include "data_channel_kernel_qos.h"
#include "kernel_fd_manager.h"

#define ACIPCD_CRITICAL_SECTION_VARIABLE(vARnAME)             \
    unsigned long vARnAME##_flags;                     \
    DEFINE_SPINLOCK(vARnAME##_protect_lock)

#define ACIPCD_CRITICAL_SECTION_ENTER(vARnAME)                    \
    do {                                                        \
		spin_lock_irqsave(&(vARnAME##_protect_lock), vARnAME##_flags);  \
    } while (0)

#define ACIPCD_CRITICAL_SECTION_LEAVE(vARnAME)              \
    do {                                        \
		spin_unlock_irqrestore(&(vARnAME##_protect_lock), vARnAME##_flags); \
    } while (0)

ACIPCD_CRITICAL_SECTION_VARIABLE(gDataChannelKernelShmemLockQOS);

#define TXBUFLIST_LOCK()       ACIPCD_CRITICAL_SECTION_ENTER(gDataChannelKernelShmemLockQOS)
#define TXBUFLIST_UNLOCK()     ACIPCD_CRITICAL_SECTION_LEAVE(gDataChannelKernelShmemLockQOS)

#define AL24(_sZ_)  (((_sZ_)+3)&(~3))
#define PACKET_BYTES_TRAIL 16
#define MAX_HP_PACKETS_IN_ROW 15
#define MAX_TX_IN_ONE_LOOP	  20
#define CLEANUP_TIME		  60000
#define QOS_DEBUG_PERIOD_MSEC 30000
#define QOS_DEBUG_PERIOD_MSEC_SOON 1000

#define IP_VERLEN_OFFSET		0
#define IP_VER_MASK				0xF0
#define IPV4_MASK_VAL			0x40
#define IP_HDR_MIN_LEN			20
#define IPV4_HDR_LEN_MASK		0x0F
#define TCP_HDR_MIN_LEN			20
#define IPV4_PROTO_OFFSET		9
#define IPV4_TCP_PROTO			6
#define IPV4_HDR_IPID_OFFSET	4
#define IPV4_HDR_IPID_LEN		2
#define IPV4_HDR_DESTIP_OFFSET	16
#define IPV4_HDR_SRCIP_OFFSET	12
#define IPV4_HDR_IPADDR_LEN		4
#define IPV4_HDR_FLAGS_OFFSET   6
#define IPV4_HDR_FRGAS_OFFSET   7
#define IPV4_HDR_FLAGS_MASK	    0x1F
#define TCP_HDR_SEQ_OFFSET		4
#define TCP_HDR_ACK_OFFSET		8
#define TCP_HDR_LEN_OFFSET		12
#define TCP_HDR_LEN_MASK		0xF0
#define TCP_FLAGS_OFFSET		13
#define TCP_FLAGS_MASK			0x3F
#define TCP_FLAGS_ACK_ONLY		0x10

#if defined  (USE_DEBUG_TIMER) || defined (ENABLE_DEBUG_THREAD)
static void PeriodicDebug(unsigned long param);
#endif
static void FreeAllSessions(void);

static void CalcTcpDlTP(void);

#ifdef USE_DEBUG_TIMER
DEFINE_TIMER(QOSDebugTimer, PeriodicDebug, 0, 0);
#endif

void dumpFDDB(void);

/* returns 1 if data len is 0, then it fills seq and ack numbers */
#define TAKE_LONG_N_TO_H(_pkt) ((unsigned long)((*((unsigned char *)(_pkt))) << 24)|*((unsigned char *)(_pkt)+3) | (*((unsigned char *)(_pkt)+1) << 16) | (*((unsigned char *)(_pkt)+2) << 8))

static volatile PDCK_PACKET_NODE QosInputList;
static volatile PDCK_PACKET_NODE QosInputListLast;

extern flag_wait_t dataChanLowWatermarkFlagRef;
extern flag_wait_t dataChanTxBuffersAvailFlagRef;
static int gQOSTaskQuitFlag;
#if defined  (USE_DEBUG_TIMER) || defined (ENABLE_DEBUG_THREAD)
static int FQextendDebug = FALSE;
#endif
#ifdef ENABLE_DEBUG_THREAD
static OSTaskRef FQTxDebugTaskRef;
#endif
volatile int TimerRequestedDiff = 2800;	/* to be close to 1 msec */
volatile int HploopFactor;
volatile int lpBytesLimitPerTxLoop = 4000;
static unsigned int FlowOnTimerTimeout = 20;
volatile int MaxUlTp;
static unsigned int TcpDlBytes;
static unsigned int TcpDlTP;
static int moreHpLoopFacotr;

#define OOM_CHECK(_p_t_r_)					\
	do {									\
		if ((_p_t_r_) == NULL) {			\
			if (printk_ratelimit()) {		\
				printk(KERN_ERR "Drop packet OOM line=%d\n", __LINE__);\
			}								\
			return;							\
		}									\
	} while (0)

#define OOM_CHECK_ASSERT(_p_t_r_)			\
	do {
		if ((_p_t_r_) == NULL) {\
			if (printk_ratelimit()) {\
				printk(KERN_ERR "Drop packet OOM line=%d\n", __LINE__);\
			} \
			BUG_ON(1); \
		} \
	} while (0)

#define ACK_AFTER_MAX_WINDOW		(1024 * 1024)
#define IS_ACK_AFTER(_ack_last_, _ack_prev_)  ((int)((_ack_last_)-(_ack_prev_)) > 0)

extern void kdiag_print(char *fmt, ...);

#define dumpFQLog()
#define dump_p_with_time(args...)

static TcpSessionHash SessionsHash;

TxFQPacketsContainer FQDB;

FQTxConfig TxConfig = {
	0,			/* DoQos */
	5000,			/*TcpSessionTO= */
	200,			/*TcpVHPSessionTO */
	10,			/*MaxTxSleepTime= */
	256,			/*MaxPktPerSession= */
	8,			/*HPSessionMinPktNum= */
	1,			/*UseAvgPSize=0, */
	5,			/*HPInterval, */
	128,			/*SmallPacketSize=80, */
	100,			/* IPGThreshold=100, */
	300,			/*IPGThresholdOut=300, */
	150,			/*MaxOtherPackets=60, */
	400,			/* UlDlRsvdBytes */
};

static unsigned int GetTickCount(void)
{
	unsigned int tick;
	struct timeval tv;

	do_gettimeofday(&tv);

	tick = tv.tv_sec * 1000 + tv.tv_usec / 1000;
	return tick;
}

#define FQ_DEBUG_ERROR			1
#define FQ_DEBUG_PERIODIC		(1<<1)
#define FQ_DEBUG_XMIT2			(1<<2)
#define FQ_DEBUG_XFER_MAP		(1<<3)
#define FQ_DEBUG_BUF_LIST		(1<<4)
#define FQ_DEBUG_BUF_LIST_PULL	(1<<5)
#define FQ_DEBUG_SESSION_DEL	(1<<6)
#define FQ_DEBUG_PTIME			(1<<7)
#define FQ_DEBUG_IP_FRAG		(1<<8)
#define FQ_DEBUG_LATENCY		(1<<9)
#define FQ_DEBUG_TP_CONFIG		(1<<10)
#define FQ_DEBUG_TP_CONFIG_WB	(1<<11)
#define FQ_DEBUG_XMIT			(1<<12)
#define FQ_DEBUG_TO				(1<<13)
#define FQ_DEBUG_LM				(1<<14)

/*static int inhere=0; */

unsigned int FqDebugMask = FQ_DEBUG_ERROR | FQ_DEBUG_PERIODIC
/* |    FQ_DEBUG_TP_DELAY */
/* |    FQ_DEBUG_XFER_MAP */
/* |    FQ_DEBUG_BUF_LIST */
/* |    FQ_DEBUG_BUF_LIST_PULL */
/* |    FQ_DEBUG_SESSION_DEL */
/* |    FQ_DEBUG_PTIME */
/* |    FQ_DEBUG_IP_FRAG */
/* |    FQ_DEBUG_LATENCY */
/* |    FQ_DEBUG_TP_CONFIG */
/* |    FQ_DEBUG_TP_DELAY2 */
/* |    FQ_DEBUG_BUF_LIST_PULL */
/* |    FQ_DEBUG_BUF_LIST_PULL */
/* |    FQ_DEBUG_TP_DELAY2 */
/* |    FQ_DEBUG_BUF_LIST_PULL */
/* |    FQ_DEBUG_BUF_LIST_PULL */
/* | FQ_DEBUG_XMIT */
/* | FQ_DEBUG_TO */
    ;

void SetQOSDebugMask(unsigned int newMask)
{
	FqDebugMask = newMask;
}

#define FQD(type) ((type) & FqDebugMask)

#define LogMsg(fmt, args...)    do {if (FQD(FQ_DEBUG_LM)) kdiag_print(fmt, ##args); } while (0)

/*#define OPT_DEBUG */
#ifdef OPT_DEBUG
void dumpTcpPacket(unsigned char *pkt)
{
	struct iphdr *pip = (struct iphdr *)pkt;
	struct tcphdr *ptcp = (struct tcphdr *)(pip + 1);

	printk("id = %d len= %d seq=%08x ack=%08x flags = %02x\n",
	       __cpu_to_be16(pip->id), __cpu_to_be16(pip->tot_len), __cpu_to_be32(ptcp->seq),
	       __cpu_to_be32(ptcp->ack_seq), (unsigned char)*((unsigned char *)(&ptcp->ack_seq) + 5));
}
#endif

#ifdef USE_PXA_TIMER
int request_irq_status = -1;
static irqreturn_t pxa_timer_interrupt3(int irq, void *dev_id)
{
	OIER &= (~(1 << 3));	/*disable timer */

	OSSR = (1 << 3);	/* clear interrupt */

	FLAG_SET(&FQDB.FlowOn, 0x01, OSA_FLAG_OR);
	return IRQ_HANDLED;
}

static void setup_wkup(unsigned int ticks)
{
	unsigned int flags = 0;
	OSA_STATUS osaStatus;
	static int lastt;
#define DEBUG_PXATIMER 1
#ifdef DEBUG_PXATIMER
	static int okays, bads, timeo, ntmo, lastprint;
	unsigned int t2, t1 = GetTickCount();
	static int hist[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
#endif
	if (request_irq_status == 0) {
		FLAG_SET(&FQDB.FlowOn, 0, OSA_FLAG_AND);

		TXBUFLIST_LOCK();
		OIER = OIER | (1 << 3);	/* enable timer */
		OSMR3 = (ticks) + OSCR;	/* setwkup */
		TXBUFLIST_UNLOCK();

		osaStatus = FLAG_WAIT(&FQDB.FlowOn, 0x01, OSA_FLAG_AND_CLEAR, &flags, FlowOnTimerTimeout);
		/*osaStatus=OSAFlagWait(FQDB.FlowOn, 0x01, OSA_FLAG_AND_CLEAR, &flags,OSA_SUSPEND); */
		if (ticks != lastt) {
			printk("ticks changed %d %d\n", lastt, ticks);
			lastt = ticks;
		}
#ifdef DEBUG_PXATIMER
		if (osaStatus == OS_SUCCESS)
			ntmo++;
		else
			timeo++;

		t2 = GetTickCount();

		t2 -= t1;
		if ((int)t2 > 1) {
			if (t2 > 9)
				bads++;
			else
				hist[t2]++;
		} else {
			okays++;
		}

		if ((bads > 1) && (GetTickCount() - lastprint > 10000)) {
			lastprint = GetTickCount();
			printk(KERN_ERR
			       " pxa timer stats ntimo=%d timo=%d ok=%d bad=%d h2=%d h3=%d h4=%d h5=%d h6=%d h7=%d h8=%d h9=%d\n",
			       ntmo, timeo, okays, bads, hist[2], hist[3], hist[4], hist[5], hist[6], hist[7], hist[8],
			       hist[9]);

			ntmo = timeo = okays = bads = 0;
			memset(hist, 0, sizeof(hist));

		}
#endif

	}
}
#else
#define setup_wkup(x)
#endif

#define ACK_OPT
#ifdef ACK_OPT
/* returns 1 if data len is 0, then it fills seq and ack numbers */
static int GetAndTcpInfo(PDCK_PACKET_NODE pnode, unsigned long *seq, unsigned long *ack)
{
	unsigned char tcp_hdr_offset;

	tcp_hdr_offset = (unsigned char)(pnode->data[IP_VERLEN_OFFSET] & IPV4_HDR_LEN_MASK) << 2;

	if ((unsigned short)pnode->length ==
	    (unsigned short)((pnode->data[tcp_hdr_offset + TCP_HDR_LEN_OFFSET] & TCP_HDR_LEN_MASK) >> 2) +
	    tcp_hdr_offset) {
		if ((pnode->data[tcp_hdr_offset + TCP_FLAGS_OFFSET] & TCP_FLAGS_MASK) == TCP_FLAGS_ACK_ONLY) {
			*seq = TAKE_LONG_N_TO_H(pnode->data + tcp_hdr_offset + TCP_HDR_SEQ_OFFSET);
			*ack = TAKE_LONG_N_TO_H(pnode->data + tcp_hdr_offset + TCP_HDR_ACK_OFFSET);
			return 1;
		}

	}
	return 0;
}
#endif

void QOSGetTCPDLBytes(unsigned char *dataptr, unsigned int len)
{
	if (len < IP_HDR_MIN_LEN + TCP_HDR_MIN_LEN) {
		return;
	}

	if ((dataptr[IP_VERLEN_OFFSET] & IP_VER_MASK) != IPV4_MASK_VAL) {	/*0x40 support only ipv4 */
		return;
	}

	TXBUFLIST_LOCK();
	TcpDlBytes += len;
	TXBUFLIST_UNLOCK();
}

static void CalcTcpDlTP(void)
{
	static unsigned int last_time;
	unsigned int now = GetTickCount();
	unsigned int bytes, diff;

	diff = now - last_time;

	if (diff < 100)
		return;
	else if (diff < 1000) {
		TXBUFLIST_LOCK();
		bytes = TcpDlBytes;
		TcpDlBytes = 0;
		TXBUFLIST_UNLOCK();
		TcpDlTP = (bytes * 8 * 1000) / diff;	/* bps */
		last_time = now;

		switch (MaxUlTp) {
		case ULTP_384KB:
			if (TcpDlTP < 1000000)
				moreHpLoopFacotr = 0;
			else if (TcpDlTP < 5000000)
				moreHpLoopFacotr = 1;
			else if (TcpDlTP < 10000000)
				moreHpLoopFacotr = 2;
			else
				moreHpLoopFacotr = 6;
			break;
		case ULTP_2M:
			if (TcpDlTP < 1000000)
				moreHpLoopFacotr = 0;
			else if (TcpDlTP < 5000000)
				moreHpLoopFacotr = 1;
			else if (TcpDlTP < 10000000)
				moreHpLoopFacotr = 2;
			else if (TcpDlTP < 14000000)
				moreHpLoopFacotr = 4;
			else
				moreHpLoopFacotr = 7;

			break;
		case ULTP_5_7M:
			if (TcpDlTP > 3000000)
				moreHpLoopFacotr = 1;
			else
				moreHpLoopFacotr = 0;
			break;
		default:
			moreHpLoopFacotr = 0;
			break;
		}
#ifdef DEBUG_DL_TP
		{
			static int dump;

			if (dump++ > 20) {
				dump = 0;
				printk("mxultp=%d TcpDlTP=%d more=%d", (int)MaxUlTp, (int)TcpDlTP,
				       (int)moreHpLoopFacotr);
			}
		}
#endif

	} else {
		TXBUFLIST_LOCK();
		TcpDlBytes = 0;
		TXBUFLIST_UNLOCK();
		last_time = now;
		TcpDlTP = 0;
		moreHpLoopFacotr = 0;
	}
}

static pdbll dbllInit(void)
{
	pdbll pl;

	pl = (pdbll) kmalloc(sizeof(dbll), GFP_ATOMIC);
	OOM_CHECK_ASSERT(pl);
	pl->count = 0;
	pl->elems = NULL;
	return pl;
}

static int dbllAdd(pdbll pl, void *item)
{

	LogMsg("dblladd list=%x elems=%d item=%x cnt=%d\n", pl, pl->elems, item, pl->count + 1);

	((pilink) item)->next = pl->elems;
	((pilink) item)->prev = NULL;

	if (pl->elems) {
		((pilink) (pl->elems))->prev = item;
	}
	pl->elems = item;
	pl->count++;

	return 0;
}

static int dbllRemove(pdbll pl, void *item)
{

	void *i = pl->elems;

	/* verify item is in this list */
	while (i) {
		if (i == item)
			break;
		i = ((pilink) i)->next;
	}

	if (i != item) {
		printk(KERN_ERR " error removeing %x is not in %x\r\n", (unsigned int)item, (unsigned int)pl);
		return -1;
	}

	if (item == pl->elems) {
		pl->elems = ((pilink) item)->next;
	} else {
		((pilink) (((pilink) item)->prev))->next = ((pilink) item)->next;
	}

	if (((pilink) item)->next) {
		((pilink) (((pilink) item)->next))->prev = ((pilink) item)->prev;
	}

	pl->count--;
	LogMsg("dbllremove list=%x elems=%d item=%x cnt=%d\n", pl, pl->elems, item, pl->count);
	return 0;
}

static void *dbllEnum(pdbll pl, pEnumPf pf, void *param)
{
	void *pnext, *tmp;
	for (tmp = pl->elems; tmp; tmp = pnext) {
		pnext = ((pilink) tmp)->next;
		if (pf(tmp, param)) {
			LogMsg("dbllenum list=%x elems=%d return=%x cnt=%d\n", pl, pl->elems, tmp, pl->count);
			return tmp;
		}
	}

	LogMsg("dbllenum list=%x elems=%d return NULL cnt=%d\n", pl, pl->elems, pl->count);
	return NULL;
}

static pIPPacketListItem IPPacketListItemConstruct(PDCK_PACKET_NODE node)
{

	pIPPacketListItem p;

	p = (pIPPacketListItem) kmalloc(sizeof(IPPacketListItem), GFP_ATOMIC);
	OOM_CHECK_ASSERT(p);

	p->txnode = node;
	p->next = NULL;
	p->last = p;
	p->count = 1;

	LogMsg("IPPacketListItemConstruct n=%x len=%d\n", node, node->length);
	return p;

}

static void QPacket(pIPPacketListItem cur, PDCK_PACKET_NODE node)
{
	cur->last->next = IPPacketListItemConstruct(node);
	cur->last = cur->last->next;
	cur->count++;

	LogMsg("QPacket cur=%x cur->last=%x cnt=%d node=%x\n", cur, cur->last, cur->count, node);
}

static pIPPacketListItem DQPacket(pIPPacketListItem cur, PDCK_PACKET_NODE *rp)
{
	pIPPacketListItem n = cur->next;

	*rp = cur->txnode;
	cur->next = NULL;
	cur->txnode = NULL;
	if (n) {
		n->last = cur->last;
		n->count = cur->count - 1;
	}

	LogMsg("DQPacket cur=%x ret=%x cnt=%d node=%x len=%d last=%x\n", cur, n, n ? n->count : 0, *rp, (*rp)->length,
	       cur->last);
	return n;
}

static void IPPacketListItemDestruct(pIPPacketListItem p)
{
/*
	if (p->txnode){
		LogMsg("IPPacketListItemDestruct p=%x txnode=%x data=%x",p,p->txnode,p->txnode->data);
		kfree(p->txnode);
	}
	if (p->next){
		LogMsg("IPPacketListItemDestruct next=%x",p->next);
		IPPacketListItemDestruct(p->next);
	}

*/
	pIPPacketListItem tmp;

	while (p) {
		kfree(p->txnode);
		tmp = p;
		p = p->next;
		kfree(tmp);

	}
	LogMsg("IPPacketListItemDestruct free p=%x\n", p);
}

static void TcpSessionHashInit(pTcpSessionHash ph)
{
	memset(ph->table, 0, sizeof(ph->table));
}

static unsigned long HashSessionFunc(pTcpSessionId pid)
{
	return (pid->wDstPort ^ pid->wSrcPort) & (SESSION_NUM_HASH_ENT - 1);
}

static void dumpIpSessionInfo(pIPSession p)
{
	printk(KERN_ERR
	       "next=%x prev=%x plist=%x maxInp=%ld id.sp=%d id.dp=%d id.dip=%lx id.hdrid=%x \r\n ttb=%ld maxp=%ld maxpsz=%ld pin=%ld pout=%ld hp=%d nh=%x ih=%ld opts=%ld\n",
	       (UINT) p->next, (UINT) p->prev, (UINT) p->plist, p->maxInP, p->id.wSrcPort, p->id.wDstPort,
	       p->id.dwDestIP, p->id.wIpHdrId, p->TotalBytes, p->maxPackets, p->maxPacketSize, p->pIn, p->pOut, p->HP,
	       (UINT) p->NextInHash, p->IndexInHash, p->pAckOpt);

}

struct __IPSession *FindSession(pTcpSessionHash ph, pTcpSessionId pid, unsigned long *index)
{
	pIPSession pSession;

	*index = HashSessionFunc(pid);

	pSession = ph->table[*index];

	while (pSession) {
		if (pid->dwDestIP == pSession->id.dwDestIP &&
		    pid->wSrcPort == pSession->id.wSrcPort && pid->wDstPort == pSession->id.wDstPort) {
			LogMsg("FindSession found at %d ph=%x dip=%x sp=%d dp=%d session=%x\n", *index, ph,
			       pid->dwDestIP, pid->wSrcPort, pid->wDstPort, pSession);
			return pSession;
		}
		pSession = pSession->NextInHash;
	}

	LogMsg("FindSession NOT found at %d ph=%x dip=%x sp=%d dp=%d session=%x\n", *index, ph, pid->dwDestIP,
	       pid->wSrcPort, pid->wDstPort, pSession);
	return NULL;
}

void AddSessionAtIndex(pTcpSessionHash ph, pIPSession pSession, unsigned long index)
{
	pSession->NextInHash = ph->table[index];
	ph->table[index] = pSession;
	pSession->IndexInHash = index;

	LogMsg("AddSession found at %d ph=%x nh=%x session=%x\n", index, pSession->NextInHash, pSession);
}

void RemoveSession(pTcpSessionHash ph, pIPSession pSession)
{
	pIPSession prev;

	LogMsg("Remove session %x ph=%x index=%ld\n", pSession, ph, pSession->IndexInHash);

	if (pSession->IndexInHash >= SESSION_NUM_HASH_ENT) {
		printk(KERN_ERR "INVALID session index \r\n");
		dumpIpSessionInfo(pSession);
		dumpFQLog();
		return;
	}

	prev = ph->table[pSession->IndexInHash];

	if (prev == NULL) {
		printk(KERN_ERR "INVALID session hash \r\n");
		dumpIpSessionInfo(pSession);
		dumpFQLog();
		return;
	}

	if (prev == pSession) {
		ph->table[pSession->IndexInHash] = pSession->NextInHash;
		LogMsg("Remove session first at index %ld next=%ld\n", pSession->IndexInHash, pSession->NextInHash);
	} else {

		while (prev && prev->NextInHash != pSession) {
			prev = prev->NextInHash;
		}
		if (prev) {
			prev->NextInHash = pSession->NextInHash;
			LogMsg("Remove session later at index %ld prev=%x next=%ld\n", pSession->IndexInHash, (int)prev,
			       pSession->NextInHash);

		} else {
			printk(KERN_ERR "INVALID session hash2 \r\n");
			dumpIpSessionInfo(pSession);
			return;
		}
	}

}

pIPSession IPSessionConstrcut(pTcpSessionId pid, PDCK_PACKET_NODE p, unsigned long hashIndex)
{
	pIPSession pips = (pIPSession) kmalloc(sizeof(IPSession), GFP_ATOMIC);

	OOM_CHECK_ASSERT(pips);

	pips->next = NULL;
	pips->prev = NULL;
	pips->id.dwDestIP = pid->dwDestIP;
	pips->id.wSrcPort = pid->wSrcPort;
	pips->id.wDstPort = pid->wDstPort;
	pips->id.wIpHdrId = pid->wIpHdrId;
	pips->pOut = 0;
	pips->pDrop = 0;
	pips->pAckOpt = 0;
	pips->lastAckOptTime = 0;
	pips->maxPackets = TxConfig.MaxPktPerSession;

	pips->plist = IPPacketListItemConstruct(p);

	LogMsg("IPSessionCon pl=%x p=%x len=%x dip=%x sp=%d dp=%d hid=%d idx=%ld\n", pips->plist, p, p->length,
	       pid->dwDestIP, pid->wSrcPort, pid->wDstPort, pid->wIpHdrId, hashIndex);
	pips->dwLastActivityTime = GetTickCount();
	pips->pIn = 1;
	pips->TotalBytes = (unsigned long)p->length;
	pips->avgPSize = (unsigned long)p->length;
	pips->maxInP = 1;
	pips->maxPacketSize = p->length;
	/*IPG=new SlidingAverage(0); */

	/* this will also update pips->IndexInHash */
	AddSessionAtIndex(&SessionsHash, pips, hashIndex);
	return pips;

}

static pIPSession IPSessionConstrcutDefault(void)
{
	pIPSession pips = (pIPSession) kmalloc(sizeof(IPSession), GFP_ATOMIC);

	OOM_CHECK_ASSERT(pips);

	pips->next = NULL;
	pips->prev = NULL;
	pips->id.dwDestIP = 0;
	pips->id.wSrcPort = 0;
	pips->id.wDstPort = 0;
	pips->pOut = 0;
	pips->pDrop = 0;
	pips->maxPackets = TxConfig.MaxPktPerSession;
	pips->plist = NULL;
	pips->pIn = 0;
	pips->TotalBytes = 0;
	pips->avgPSize = 0;
	pips->maxInP = 0;
	pips->maxPacketSize = 0;
	pips->pAckOpt = 0;
	pips->lastAckOptTime = 0;
/*      pips->IPG=NULL; */

	return pips;
}

void IPSessionDestruct(pIPSession pips)
{

	LogMsg("IPSessionDestruct pips=%x plist=%x\n", pips, pips->plist);

	RemoveSession(&SessionsHash, pips);

	if (FQD(FQ_DEBUG_SESSION_DEL)) {
		printk(KERN_ERR "Delete Session pIn=%ld pOut=%ld pDrop=%ld pRemain=%d maxInP=%ld ttb=%ld avpsz=%ld pOpts=%ld HP=%d sport=%d dport=%d\r\n", pips->pIn, pips->pOut, pips->pDrop, pips->plist ? pips->plist->count : 0, pips->maxInP, pips->TotalBytes, pips->avgPSize, pips->pAckOpt,	/*IPG->avg, */
		       pips->HP,
		       (pips->id.wSrcPort >> 8) | ((pips->id.wSrcPort & 0xFF) << 8),
		       (pips->id.wDstPort >> 8) | ((pips->id.wDstPort & 0xFF) << 8));
	}

	if (pips->plist) {
		IPPacketListItemDestruct(pips->plist);
	}
	if (pips->lastAckOptTime) {
		if (FQDB.numVHPSessions)
			FQDB.numVHPSessions--;
		else {
			printk("error numvho = %ld\n", FQDB.numVHPSessions);
		}
	}

	kfree(pips);
	/*delete IPG; */
}

int MatchByPacketID(void *pItem, void *param)
{
	pIPSession pSession;
	pTcpSessionId pid;

	pSession = (pIPSession) pItem;
	pid = (pTcpSessionId) param;

	LogMsg("MatchByPacketID ps=%x pid=%x dip=%x sp=%d dp=%d hid=%d\n", pSession, pid, pid->dwDestIP, pid->wSrcPort,
	       pid->wDstPort, pid->wIpHdrId);

	if (pItem == NULL) {
		return 0;
	}

	if (pid->wIpHdrId == pSession->id.wIpHdrId && pid->dwDestIP == pSession->id.dwDestIP) {
		LogMsg("MatchByPacketID match\n");
		return 1;
	}

	LogMsg("MatchByPacketID No match\n");
	return 0;

}

PDCK_PACKET_NODE GetIPSessionPacket(pIPSession pips)
{
	PDCK_PACKET_NODE rp;
	pIPPacketListItem i = pips->plist;

	if (!i) {
		LogMsg("GetIPSessionPacket NULL");
		return NULL;
	}

	pips->pOut++;

	pips->plist = DQPacket(pips->plist, &rp);

	LogMsg("GetIPSessionPacket pl=%x willfree=%x node=%x len=%d cnt=%d\n", pips->plist, i, rp, rp->length,
	       pips->plist ? pips->plist->count : 0);
	IPPacketListItemDestruct(i);

	return rp;
}

int AddIPSessionPacket(pIPSession pips, PDCK_PACKET_NODE p)
{
	unsigned long ack = 0, seq = 0, ackt = 0, seqt = 0;
	int didOpt = 0;
	pips->pIn++;

	if (pips->plist) {
		LogMsg("addipsp pips=%x p=%x len=%x cnt=%d\n", pips, p, p->length, pips->plist->count);
		if (pips->plist->count == pips->maxPackets) {

			TXBUFLIST_LOCK();
			gUplinkPendingData.queuePendingData -= p->length;
			CI_UPLINK_CALC_TOTAL_PENDING_DATA;
			TXBUFLIST_UNLOCK();

			kfree(p);
			pips->pDrop++;
			return 0;
		}
#ifdef ACK_OPT
		if (pips->HP && pips->pIn > TxConfig.HPSessionMinPktNum && pips->plist->count == 1) {
			if (GetAndTcpInfo(p, &seq, &ack)) {
				/* check if we can drop all packet */
				pIPPacketListItem ptmp = pips->plist->last;

				if (ptmp /* && ptmp->txnode->length==p->length */) {
					if (GetAndTcpInfo(ptmp->txnode, &seqt, &ackt)) {
						if ((int)(ack - ackt) > 0 && seq == seqt) {

#ifdef OPT_DEBUG
							static int dump = 5;

							if (dump-- > 0) {

								printk
								    ("\nOpt: Count=%d ack=%08lx ackt=%08lx seq%08lx seqt=%08lx\n",
								     pips->plist->count, ack, ackt, seq, seqt);
								dumpTcpPacket(p->data);
								dumpTcpPacket(ptmp->txnode->data);
								printk("======================\n");
							}
#endif
							pips->pAckOpt++;
							if (pips->lastAckOptTime == 0)
								FQDB.numVHPSessions++;
							pips->lastAckOptTime = GetTickCount();
							FQDB.numHPPackets--;
							FQDB.CurrentBytesIn -= ptmp->txnode->length;
							FQDB.numPackets--;
							FQDB.TotalPOut++;

							TXBUFLIST_LOCK();
							gUplinkPendingData.queuePendingData -= ptmp->txnode->length;
							CI_UPLINK_CALC_TOTAL_PENDING_DATA;
							TXBUFLIST_UNLOCK();

							kfree(ptmp->txnode);
							ptmp->txnode = p;
							p->next = NULL;
							/*IPPacketListItemDestruct(pips->plist); */
							/*pips->plist=IPPacketListItemConstruct(p); */
							didOpt = 1;
						}
					}
				}
			}
		}
		if (!didOpt)
#endif /*ACK_OPT */
		{
			QPacket(pips->plist, p);
		}

		if (pips->plist->count > pips->maxInP) {
#ifdef OPT_DEBUG
			static int dump = 10;
#endif
			pips->maxInP = pips->plist->count;
#ifdef OPT_DEBUG
			if (pips->HP && pips->plist->count > 2 && pips->pIn > 8 && dump-- > 0) {
				pIPPacketListItem ptmp = pips->plist;
				int i = 1;
				printk("\nDump: Count=%d ack=%08lx ackt=%08lx seq%08lx seqt=%08lx\n",
				       pips->plist->count, ack, ackt, seq, seqt);
				while (ptmp) {
					printk("p %d:\n", i++);
					dumpTcpPacket(ptmp->txnode->data);
					ptmp = ptmp->next;
				}
				printk("=======================================================\n");
			}
#endif
		}
	} else {
		LogMsg("addipsp new pips=%x p=%x len=%x\n", pips, p, p->length);
		pips->plist = IPPacketListItemConstruct(p);
	}

	pips->TotalBytes += p->length;

	if ((unsigned long)p->length > pips->maxPacketSize)
		pips->maxPacketSize = (unsigned long)p->length;

	if (TxConfig.UseAvgPSize)
		pips->avgPSize = pips->TotalBytes / pips->pIn;
	else
		pips->avgPSize = pips->maxPacketSize;

	pips->dwLastActivityTime = GetTickCount();
	return 1;

}

int GetPacketInfo(PDCK_PACKET_NODE p, pTcpSessionId id)
{
	unsigned long iphdrlen;
	unsigned char *dataptr = p->data;

	if (p->length < IP_HDR_MIN_LEN) {	/* minimum IP hdr size */
		return FALSE;
	}

	if ((dataptr[IP_VERLEN_OFFSET] & IP_VER_MASK) != IPV4_MASK_VAL) {	/*0x40 support only ipv4 */
		return FALSE;
	}

	iphdrlen = (dataptr[IP_VERLEN_OFFSET] & IPV4_HDR_LEN_MASK) << 2;

	if (iphdrlen < IP_HDR_MIN_LEN) {	/* unknown ip v4 header */
		/* NOT IPv4 */
		return FALSE;
	}
	/*TBD: add TFT Handling */
	if (dataptr[IPV4_PROTO_OFFSET] != IPV4_TCP_PROTO) {	/* 0x6 TCP protocol */
		/* NOT TCP */
		return FALSE;
	}
	/* copy IP hdr id */
	memcpy((void *)&id->wIpHdrId, &dataptr[IPV4_HDR_IPID_OFFSET], IPV4_HDR_IPID_LEN);

	/* copy dest IP */
	memcpy((void *)&id->dwDestIP, &dataptr[IPV4_HDR_DESTIP_OFFSET], IPV4_HDR_IPADDR_LEN);

	/*iphdrflags=(dataptr[6] & 0xe0)>>5; //3 MSB */

	if ((dataptr[IPV4_HDR_FLAGS_OFFSET] & IPV4_HDR_FLAGS_MASK) == 0 && (dataptr[IPV4_HDR_FRGAS_OFFSET] == 0)) {	/* if no frags or first frag -> offset == 0 */

		if (p->length < IP_HDR_MIN_LEN + TCP_HDR_MIN_LEN)	/* minimum iphdr+tcphdr */
			return FALSE;

		/* copy src Port */
		memcpy((void *)&id->wSrcPort, &dataptr[iphdrlen], 2);

		/* copy dst Port */
		memcpy((void *)&id->wDstPort, &dataptr[iphdrlen + 2], 2);

		if (id->wDstPort || id->wSrcPort || id->dwDestIP) {
			LogMsg("GetPacketInfo IP true\n");
			return TRUE;
		}
	} else {		/* fragment - tcp header is missing */
		if (id->dwDestIP) {
			id->wDstPort = 0;
			id->wSrcPort = 0;
			LogMsg("GetPacketInfo IP frag true\n");
			return TRUE;
		}
		LogMsg("GetPacketInfo IP frag false\n");
	}

	LogMsg("GetPacketInfo IP False (too short)\n");
	return FALSE;
}

int QueuePacket(TxFQPacketsContainer *pfqdb, PDCK_PACKET_NODE p)
{
	int rc;
	TcpSessionId id;
	IPSession *pSession;
	int AddOk = TRUE;
	unsigned long hashIndex = 0xffffffff;
	int cid = 0;

	LogMsg("QueuePacket p=%x\n", p);

	rc = GetPacketInfo(p, &id);

	pfqdb->TxQueueState = 2;

	/*TXBUFLIST_LOCK(lock_flags); */

	if (rc) {

		/*DumpDataHeader(); */
		if (id.wSrcPort || id.wDstPort) {
			/* not a fragment use hash */
			pSession = FindSession(&SessionsHash, &id, &hashIndex);
		} else {

			LogMsg("QueuePacket p=%x FRG\n", p);
			pfqdb->TxQueueState = 3;
			/* a fragment - try to find session by destIP and ip hdr ID */
			pSession = dbllEnum(pfqdb->HPTcpSessionsList, MatchByPacketID, (void *)&id);

			if (!pSession) {
				pfqdb->TxQueueState = 4;
				pSession = dbllEnum(pfqdb->LPSessionsList, MatchByPacketID, (void *)&id);
			} else {
				LogMsg("QueuePacket p=%x FRG found HP\n", p);
			}

			if (!pSession) {
				if (FQD(FQ_DEBUG_IP_FRAG)) {
					printk(KERN_ERR "Error: frag session not found\r\n");
				}
				LogMsg("QueuePacket p=%x FRG not found\n", p);
				goto other_packtes;
			} else {
				LogMsg("QueuePacket p=%x FRG found LP\n", p);
			}
		}

		if (pSession) {	/* add packet to existing session */
			pfqdb->TxQueueState = 5;
			AddOk = AddIPSessionPacket(pSession, p);
			pfqdb->TxQueueState = 6;
			if (AddOk) {

				LogMsg("QueuePacket p=%x ps=%x ADDOK HP=%ld\n", p, pSession, pSession->HP);
				pSession->id.wIpHdrId = id.wIpHdrId;

				if (pSession->HP) {
					if (pSession->avgPSize > TxConfig.SmallPacketSize
					    /*|| pSession->IPG->avg>RilTxConfig::IPGThresholdOut */) {
						/* was high priority but not any more! move to normal list */
						LogMsg("QueuePacket p=%x ps=%x HP->LP last=%x\n", p, pSession,
						       pfqdb->LastHPSession);
						pSession->HP = FALSE;
						if (pSession == pfqdb->LastHPSession) {
							pfqdb->LastHPSession = ((IPSession *) pSession)->next;
						}
						if (dbllRemove(pfqdb->HPTcpSessionsList, pSession) == -1) {
							dumpIpSessionInfo(pSession);
						}
						dbllAdd(pfqdb->LPSessionsList, pSession);
						/* current packet is counted in pSession->plist->count */
						/* but not yet in pfqdb->numHPPackets */
						/* this is why we do -=(count-1) */
						pfqdb->numHPPackets -= (pSession->plist->count - 1);
						pfqdb->numLPPackets += pSession->plist->count;
						dump_p_with_time("queue tcp HP->LP", (void *)&pSession->id.wIpHdrId);
					} else {
						pfqdb->numHPPackets++;
						dump_p_with_time("queue tcp HP", (void *)&pSession->id.wIpHdrId);
					}
				} else {
					if (pSession->avgPSize <= TxConfig.SmallPacketSize
					    && pSession->pIn > TxConfig.HPSessionMinPktNum
					    /*&& pSession->IPG->avg<RilTxConfig::IPGThreshold */) {
						/* was low priority but not any more! move to high priority list */
						LogMsg("QueuePacket p=%x ps=%x LP->HP last=%x\n", p, pSession,
						       pfqdb->LastSession);
						pSession->HP = TRUE;
						if (pSession == pfqdb->LastSession) {
							pfqdb->LastSession = ((IPSession *) pSession)->next;
						}
						if (dbllRemove(pfqdb->LPSessionsList, pSession) == -1) {
							dumpIpSessionInfo(pSession);
						}
						dbllAdd(pfqdb->HPTcpSessionsList, pSession);
						pfqdb->numHPPackets += pSession->plist->count;
						pfqdb->numLPPackets -= (pSession->plist->count - 1);
						dump_p_with_time("queue tcp LP->HP", (void *)&pSession->id.wIpHdrId);
					} else {
						pfqdb->numLPPackets++;
						dump_p_with_time("queue tcp LP", (void *)&pSession->id.wIpHdrId);
					}
				}
			} else {
				LogMsg("QueuePacket p=%x ps=%x NOK HP=%ld\n", p, pSession, pSession->HP);
			}

		} else {

			/* create new session and add packet */
			if (hashIndex == 0xffffffff) {
				printk(KERN_ERR "OOOOOPPPPPPPPPPSSSSSSSSSS on hash index\r\n");
			}
			pSession = IPSessionConstrcut(&id, p, hashIndex);

			if (p->length > TxConfig.SmallPacketSize) {
				LogMsg("QueuePacket NEW LP p=%x ps=%x\n", p, pSession);
				pSession->HP = FALSE;
				dbllAdd(pfqdb->LPSessionsList, pSession);
				pfqdb->numLPPackets++;
				dump_p_with_time("queue tcp new LP\n", (void *)&pSession->id.wIpHdrId);
			} else {
				LogMsg("QueuePacket NEW HP p=%x ps=%x\n", p, pSession);
				pSession->HP = TRUE;
				dbllAdd(pfqdb->HPTcpSessionsList, pSession);
				pfqdb->numHPPackets++;
				dump_p_with_time("queue tcp new HP\n", (void *)&pSession->id.wIpHdrId);
			}

			AddOk = TRUE;
		}

	} else {
other_packtes:

		pfqdb->TxQueueState = 7;
		/* add to OtherPackets List */
		/*KW: removed p->cid >= 0 ; Comparison of unsigned value against 0 is always true */
		if (p->cid < MAX_QOS_CIDS)
			cid = p->cid;

		AddOk = AddIPSessionPacket(pfqdb->OtherPackets[cid], p);

		if (AddOk) {
			pfqdb->numLPPackets++;
			LogMsg("QueuePacket Others p=%x other=%x len=%d\n", p, pfqdb->OtherPackets[cid], p->length);
			dump_p_with_time("queue other %x\n", (void *)(p->data + 6));
		} else {
			LogMsg("QueuePacket Others NOT OK!!! other=%x\n", pfqdb->OtherPackets[cid]);
		}
	}

	pfqdb->TxQueueState = 8;
	if (AddOk) {
		pfqdb->numPackets++;
		pfqdb->TotalPIn++;
		pfqdb->CurrentBytesIn += (unsigned long)p->length;
	}
	/* verify we are sane */
	if (pfqdb->numPackets != pfqdb->numHPPackets + pfqdb->numLPPackets || (int)pfqdb->numPackets < 0
	    || (int)pfqdb->numHPPackets < 0 || (int)pfqdb->numLPPackets < 0) {
		if (FQD(FQ_DEBUG_ERROR)) {
			printk(KERN_ERR "***********   error %ld %ld %ld ******\r\n", pfqdb->numPackets,
			       pfqdb->numHPPackets, pfqdb->numLPPackets);
		}
	}

	pfqdb->TxQueueState = 0;
	return TRUE;
}

void QOSProcessUplinkBuffer(unsigned char cid, char *buf, int len)
{
	PDCK_PACKET_NODE p;

	if (cid < CSD_CALL_ID_OFFSET) {
		qosSetupDebugTimer(0);
		if ((FQDB.BytesInTxList + FQDB.CurrentBytesIn) > MAX_QUEUED_BYTES_IN_DEV) {
			/* drop */
			FQDB.GlobalDrops++;
			return;
		}
	}

	p = (PDCK_PACKET_NODE) kmalloc(AL24(sizeof(*p)) + len, GFP_ATOMIC);

	OOM_CHECK(p)

	    p->data = ((char *)p) + AL24(sizeof(*p));

	memcpy(p->data, buf, len);
	p->length = len;
	p->cid = cid;
	p->next = NULL;

	TXBUFLIST_LOCK();
	FDMSetUplinkPacketQueueEmpty(0);
	if (QosInputList == NULL) {
		QosInputList = QosInputListLast = p;
	} else {
		QosInputListLast->next = p;
		QosInputListLast = p;
	}

	FQDB.BytesInTxList += len;

	if (cid < CSD_CALL_ID_OFFSET) {
		gUplinkPendingData.queuePendingData += len;
		CI_UPLINK_CALC_TOTAL_PENDING_DATA;
	}

	TXBUFLIST_UNLOCK();

	FLAG_SET(&dataChanTxBuffersAvailFlagRef, TX_BUFFERS_ARE_AVAILABLE_MASK, OSA_FLAG_OR);
/*      QOSReleaseMissedTimer(); */
}

/* returns >0 if need to go to loop begin */
void ProcessQosBufList(void)
{
	PDCK_PACKET_NODE tmp, node;
	TXSTATUS txrc;
	unsigned long len = 0;

	TXBUFLIST_LOCK();
	tmp = QosInputList;
	QosInputList = QosInputListLast = NULL;
	TXBUFLIST_UNLOCK();

	while (tmp != NULL) {
		node = tmp;
		len += node->length;
		tmp = tmp->next;
		node->next = NULL;
		if (node->cid < CSD_CALL_ID_OFFSET) {
			QueuePacket(&FQDB, node);
		} else {
			/* CSD packet should be transmitted immediately */
			txrc = CI_DataSendReqPrim_REQ_CNF(node->cid, node->data, node->length);

			if (txrc != TX_SUCCESS) {
				if (txrc == NO_RX_BUFS || txrc == NO_SHMEM_MEMORY_AVAIL) {
					kfree(node);	/*  no use to save this CSD buffer for later just drop it */
					/* we must relax our thread a bit ... */
					msleep_interruptible(1);
					TXBUFLIST_LOCK();
					FQDB.BytesInTxList -= len;
					TXBUFLIST_UNLOCK();

					continue;
				} else if (txrc != NO_CID) {
					/*assert on any other failure */
					/*BUG_ON(1); */
				}
			}
			/*free the node+buffer as this is allocated in a single block */
			kfree(node);
		}

	}

	if (len) {
		TXBUFLIST_LOCK();
		FQDB.BytesInTxList -= len;
		if ((int)FQDB.BytesInTxList < 0) {
			printk("inb<0 ???? %ld %ld\n", FQDB.BytesInTxList, len);
			FQDB.BytesInTxList = 0;
		}
		TXBUFLIST_UNLOCK();
	}

}

void dumpFDDB()
{
	pIPSession tmp;
	int i;

	printk("\n********************************************************\n");
	printk("tpin=%ld tpout=%ld txs=%ld tqs=%ld ttlBIn=%ld ttlBInL=%ld nump=%ld numlp=%ld numhp=%ld\n",
	       FQDB.TotalPIn,
	       FQDB.TotalPOut,
	       FQDB.TxThreadState,
	       FQDB.TxQueueState,
	       FQDB.CurrentBytesIn, FQDB.BytesInTxList, FQDB.numPackets, FQDB.numHPPackets, FQDB.numLPPackets);
	printk("** other[0]=%p LastHP=%p LastLP=%p **\n", FQDB.OtherPackets[0], FQDB.LastHPSession, FQDB.LastSession);
	printk("**** walk hp sessions num=%d ***\n", FQDB.HPTcpSessionsList->count);
	tmp = (pIPSession) FQDB.HPTcpSessionsList->elems;
	i = 1;
	while (tmp != NULL) {
		printk("hp %d  ->", i);
		dumpIpSessionInfo(tmp);
		i++;
		tmp = tmp->next;
	}
	printk("**** *** *** *** *** *** *** ***\n");
	printk("**** walk lp sessions num=%d ***\n", FQDB.LPSessionsList->count);

	tmp = (pIPSession) FQDB.LPSessionsList->elems;
	i = 1;
	while (tmp != NULL) {
		int j;
		for (j = 0; j < MAX_QOS_CIDS; j++)
			if (tmp == FQDB.OtherPackets[j])
				printk("OTHER %d", j);
		printk(" lp %d  ->", i);
		dumpIpSessionInfo(tmp);
		i++;
		tmp = tmp->next;
	}
	printk("**** *** *** *** *** *** *** ***\n");

}

#if defined  (USE_DEBUG_TIMER) || defined (ENABLE_DEBUG_THREAD)
static void PeriodicDebug(unsigned long param)
{
	unsigned long prevInp = 0;
	unsigned long prevOutp = 0;

#ifdef USE_DEBUG_TIMER
	if (param == 0) {
		/* meaning we were called from timer and not from debug thread */
		del_timer(&QOSDebugTimer);
	}
#endif
	printk(KERN_ERR "\n***\nnum=%ld hp=%ld lp=%ld vhps=%ld drops=%ld foff=%u\n",
	       FQDB.numPackets,
	       FQDB.numHPPackets,
	       FQDB.numLPPackets, FQDB.numVHPSessions, FQDB.GlobalDrops, gDataPacketsLimitDatabase.FlowOffCnt);

	printk(KERN_ERR "Totals: InP=%ld OutP=%ld HPS=%d LPS=%d bIn=%ld gBytesA=%d lbIn=%ld\r\n", FQDB.TotalPIn, FQDB.TotalPOut, FQDB.HPTcpSessionsList->count, FQDB.LPSessionsList->count - MAX_QOS_CIDS,	/*LPSessionsList always contains 8 session for non-tcp per cid */
	       FQDB.CurrentBytesIn, gDataPacketsLimitDatabase.remainingSize, FQDB.BytesInTxList);

	if (FQextendDebug == FALSE && prevInp && prevInp != FQDB.TotalPIn && prevOutp == FQDB.TotalPOut) {
		FQextendDebug = TRUE;
	} else
		FQextendDebug = FALSE;

	printk(KERN_ERR "TXS=%ld TQS=%ld\r\n", FQDB.TxThreadState, FQDB.TxQueueState);

	if (prevInp && prevInp != FQDB.TotalPIn && prevOutp == FQDB.TotalPOut) {
		dumpFQLog();
		printk(KERN_ERR "num=%ld hp=%ld lp=%ld next=%ld\r\n",
		       FQDB.numPackets, FQDB.numHPPackets, FQDB.numLPPackets, FQDB.NextTime);

		printk(KERN_ERR "Totals: InP=%ld OutP=%ld HPS=%d LPS=%d bIn=%ld gBytesA=%d\r\n", FQDB.TotalPIn, FQDB.TotalPOut, FQDB.HPTcpSessionsList->count, FQDB.LPSessionsList->count - MAX_QOS_CIDS,	/*LPSessionsList always contains 1 session for non-tcp */
		       FQDB.CurrentBytesIn, gDataPacketsLimitDatabase.remainingSize);

		printk(KERN_ERR "TXS=%ld TQS=%ld\r\n", FQDB.TxThreadState, FQDB.TxQueueState);

		TXBUFLIST_LOCK();
		printk(KERN_ERR "ILP=%p ILP->next=%p\r\n", QosInputList,
		       QosInputList != NULL ? QosInputList->next : NULL);
		TXBUFLIST_UNLOCK();

	}
	prevOutp = FQDB.TotalPOut;
	prevInp = FQDB.TotalPIn;
}
#endif

#ifdef ENABLE_DEBUG_THREAD
void FQTxDebugThread(void *param)
{

	printk("************   starting FQ DEbug Thread ***************");

	while ((!kthread_should_stop()) && (TxConfig.DoQos == 1)) {
		msleep_interruptible(20000);
		if (FQD(FQ_DEBUG_PERIODIC)) {
			PeriodicDebug(1);
		}
	}
}
#endif

#ifdef USE_DEBUG_TIMER
void qosSetupDebugTimer(int soon)
{
	if (!timer_pending(&QOSDebugTimer)) {
		init_timer(&QOSDebugTimer);
		if (soon)
			QOSDebugTimer.expires = jiffies + msecs_to_jiffies(QOS_DEBUG_PERIOD_MSEC_SOON);
		else
			QOSDebugTimer.expires = jiffies + msecs_to_jiffies(QOS_DEBUG_PERIOD_MSEC);
		add_timer(&QOSDebugTimer);
	}
}
#endif

void InitFQDB(void)
{
#if defined (ENABLE_DEBUG_THREAD)
	char dbgTaskStack[128];	/* nobody is using this stack */
#endif

	OSA_STATUS status;
	static int once;
	int i;

	FQDB.numPackets = 0;
	FQDB.TotalPIn = 0;
	FQDB.TotalPOut = 0;
	FQDB.MaxInPackets = 0;
	FQDB.numHPPackets = 0;
	FQDB.numLPPackets = 0;
	FQDB.numVHPSessions = 0;
	FQDB.TxThreadState = 0;	/* debug: use to detect deadlocks/endless loops */
	FQDB.TxQueueState = 0;	/* debug: use to detect deadlocks/endless loops */
	FQDB.LastHPSession = NULL;
	FQDB.LastSession = NULL;
	FQDB.CurrentBytesIn = 0;
	FQDB.PrevNumPackets = 0;
	FQDB.PrevNumHPPackets = 0;
	FQDB.BytesInTxList = 0;
	FQDB.GlobalDrops = 0;

	if (once == 0) {
		status = FLAG_INIT(&FQDB.FlowOn);
		status = FLAG_SET(&FQDB.FlowOn, 0, OSA_FLAG_AND);
		FQDB.HPTcpSessionsList = dbllInit();
		printk(KERN_ERR "FQDB.HPTcpSessionsList = %x\r\n", (unsigned int)FQDB.HPTcpSessionsList);
		FQDB.LPSessionsList = dbllInit();
		printk(KERN_ERR "FQDB.LPSessionsList = %x\r\n", (unsigned int)FQDB.LPSessionsList);
		for (i = 0; i < MAX_QOS_CIDS; i++) {
			FQDB.OtherPackets[i] = IPSessionConstrcutDefault();

			printk(KERN_ERR "Other packets[%d] = %x\r\n", i, (unsigned int)FQDB.OtherPackets[i]);

			dbllAdd(FQDB.LPSessionsList, FQDB.OtherPackets[i]);

			FQDB.OtherPackets[i]->maxPackets = TxConfig.MaxOtherPackets;
		}

		TcpSessionHashInit(&SessionsHash);

		printk(KERN_ERR "sessionhash= %x\r\n", (unsigned int)&SessionsHash);
		FlowOnTimerTimeout = msecs_to_jiffies(15);

#ifdef USE_PXA_TIMER

		OSSR |= (1 << 3);
		OIER &= (~(1 << 3));	/*disable timer */

		request_irq_status = request_irq(IRQ_OST3, pxa_timer_interrupt3, 0, "acipcddrv", NULL);

		printk("requested irq %d", request_irq_status);
#endif
		once = 1;

/*      sema_init(&FQDB.fqlock, 1); */

#ifdef ENABLE_DEBUG_THREAD
		kthread_run(FQTxDebugThread, NULL, "FQTxDebugThread");
#endif

	}

}

#define CAN_XMIT(_limit_) ((!gDataChannelDatabase.aboveHighWatermarkState) && (!(gDataPacketsLimitDatabase.limitedState)) && \
				((gDataPacketsLimitDatabase.remainingSize > (_limit_))	|| (gDataPacketsLimitDatabase.limitSizeEnabled == FALSE)))
static void CleanupAll(void)
{
	printk("QOS Cleanup ALL\n");
	TXBUFLIST_LOCK();
	FreeAllSessions();
	InitFQDB();
	TXBUFLIST_UNLOCK();
}

int QOS_CI_DataSendTask_REQ_CNF(void *data)
{

	unsigned int cleanup_time = GetTickCount();
	unsigned int flags;
	TXSTATUS txrc;
	OSA_STATUS osaStatus;
	int totalTxInRow = 0;
	int totalLPByesTxInRow;
	int maxLPBytesInRow;
	pIPSession pSession, tmp;
	unsigned char more;

	InitFQDB();

	gQOSTaskQuitFlag = 1;

	while (!kthread_should_stop()) {
		FQDB.TxThreadState = 1;
		/*if state is HIGH-WATERMARK then wait here for LOW-WATERMARK */
xmit_loop_begin:
		osaStatus =
		    FLAG_WAIT(&dataChanLowWatermarkFlagRef,
			      TX_LOW_WATERMARK_STATE_MASK | TX_REQ_MAX_DATA_PACKET_LIMIT_MASK, OSA_FLAG_AND, &flags,
			      MAX_SCHEDULE_TIMEOUT);

		if (gQOSTaskQuitFlag > 1) {
			FQDB.TxThreadState = 99;
			break;
		}

		FQDB.TxThreadState = 2;

		/*if no buffers are available (list is empty) - wait here as well */
		osaStatus =
		    FLAG_WAIT(&dataChanTxBuffersAvailFlagRef, TX_BUFFERS_ARE_AVAILABLE_MASK, OSA_FLAG_AND_CLEAR, &flags,
			      MAX_SCHEDULE_TIMEOUT);

		FQDB.TxThreadState = 3;

		/* check and do cleanup of old sessions */
		FQDB.PrevTime = GetTickCount();

		ProcessQosBufList();

		if (FQDB.numPackets == 0) {
			if ((int)(cleanup_time - FQDB.PrevTime) > CLEANUP_TIME) {
				/* cleanup old sessions */

				FQDB.TxThreadState = 33;

				cleanup_time = FQDB.PrevTime;

				pSession = (pIPSession) (FQDB.HPTcpSessionsList->elems);
				while (pSession) {
					tmp = pSession;
					pSession = (pIPSession) (pSession->next);
					if ((int)(cleanup_time - tmp->dwLastActivityTime) >
					    (int)TxConfig.TcpSessionTO / 2) {
						if (dbllRemove(FQDB.HPTcpSessionsList, tmp) != -1) {
							IPSessionDestruct(tmp);
						}
					}
				}
				pSession = (pIPSession) (FQDB.LPSessionsList->elems);
				while (pSession) {
					int j;
					tmp = pSession;
					pSession = (pIPSession) (pSession->next);

					for (j = 0; j < MAX_QOS_CIDS; j++) {
						if (tmp == FQDB.OtherPackets[j]) {
							break;
						}
					}
					if (j == MAX_QOS_CIDS) {
						if ((int)(cleanup_time - tmp->dwLastActivityTime) >
						    (int)TxConfig.TcpSessionTO) {
							dbllRemove(FQDB.LPSessionsList, tmp);
							IPSessionDestruct(tmp);
						}
					}
				}
			}

			continue;
		}

		FQDB.TxThreadState = 4;

		totalTxInRow = 0;
		totalLPByesTxInRow = 0;
		if (gDataPacketsLimitDatabase.limitSizeEnabled) {

			if (FQDB.numVHPSessions && MaxUlTp == ULTP_UNK)
				maxLPBytesInRow = lpBytesLimitPerTxLoop;
			else
				maxLPBytesInRow = 15000;
		} else {
			maxLPBytesInRow = 4000;
			/* GSM mode */
		}

		{
			int temp;
			PDCK_PACKET_NODE p;

			if (gQOSTaskQuitFlag > 1) {
				break;
			}
			FQDB.TxThreadState = 6;

			more = FALSE;
			temp = 0;

			CalcTcpDlTP();

			if (HploopFactor && FQDB.numVHPSessions) {
				static unsigned int lastTimeInHere;
				unsigned int now = GetTickCount();
				if ((now - lastTimeInHere) >= HploopFactor + moreHpLoopFacotr) {
					lastTimeInHere = now;
					temp = 0;
				} else {
					temp = 1;
				}
			}
			if (temp == 0) {
				LogMsg("Xmit HP last=%x elems=%x\n", FQDB.LastHPSession, FQDB.HPTcpSessionsList->elems);
				if (FQDB.LastHPSession == NULL) {
					pSession = (pIPSession) (FQDB.HPTcpSessionsList->elems);
				} else {
					pSession = FQDB.LastHPSession;
					FQDB.LastHPSession = NULL;
					if (pSession != (pIPSession) (FQDB.HPTcpSessionsList->elems))
						more = TRUE;	/* if we will not break due to BW limit we must go back to list head */
				}
			} else {
				/* skipping HP sessions !!! */
				pSession = NULL;
			}

			while (pSession) {
				if ((pSession->plist != NULL)) {
					if (temp++ < MAX_HP_PACKETS_IN_ROW) {
						if (!CAN_XMIT(-1500)) {
							FQDB.LastHPSession = pSession;
							/*OSAFlagSet(dataChanTxBuffersAvailFlagRef, TX_BUFFERS_ARE_AVAILABLE_MASK, OSA_FLAG_OR); */

							/*goto xmit_loop_begin; */
							goto setup_next;
						}
						if (totalTxInRow > MAX_TX_IN_ONE_LOOP * 2) {
							FQDB.LastHPSession = pSession;
							goto setup_next;
						}
						/* try to send */
						txrc =
						    CI_DataSendReqPrim_REQ_CNF(pSession->plist->txnode->cid,
									       pSession->plist->txnode->data,
									       pSession->plist->txnode->length);
						if (txrc != TX_SUCCESS) {
							if (txrc == NO_RX_BUFS || txrc == NO_SHMEM_MEMORY_AVAIL) {
								/* we must relax out thread a bit ... */
								FQDB.LastHPSession = pSession;
								printk("****** slp_h ********\n");
								msleep_interruptible(1);
								FLAG_SET(&dataChanTxBuffersAvailFlagRef,
									 TX_BUFFERS_ARE_AVAILABLE_MASK, OSA_FLAG_OR);
								goto xmit_loop_begin;
							} else if (txrc != NO_CID) {
								/*assert on any other failure */
								/*BUG_ON(1); */
								CleanupAll();
								goto xmit_loop_begin;
							}
						}

						totalTxInRow++;

						FQDB.CurrentBytesIn -= pSession->plist->txnode->length;
						p = GetIPSessionPacket(pSession);
						FQDB.numPackets--;
						FQDB.numHPPackets--;
						FQDB.TotalPOut++;

						if (FQD(FQ_DEBUG_XMIT)) {
							int j;
							int sumo = 0;

							for (j = 0; j < MAX_QOS_CIDS; j++)
								sumo += (FQDB.OtherPackets[j]->plist != NULL);

							printk(KERN_ERR
							       "xmit hp hps= %d lps= %d numhpp= %ld numlpp= %ld bin= %ld\r\n",
							       FQDB.HPTcpSessionsList->count,
							       FQDB.LPSessionsList->count - MAX_QOS_CIDS + sumo,
							       FQDB.numHPPackets, FQDB.numLPPackets,
							       FQDB.CurrentBytesIn);
						}
						if (pSession->plist != NULL) {
							LogMsg("Xmit HP more psession=%x p->plist=%x hp->elems=%x\n",
							       pSession, pSession->plist,
							       FQDB.HPTcpSessionsList->elems);
							more = TRUE;
						}
						TXBUFLIST_LOCK();
						gUplinkPendingData.queuePendingData -= (int)p->length;
						gDataPacketsLimitDatabase.remainingSize += (int)p->length;
						CI_UPLINK_CALC_TOTAL_PENDING_DATA;
						TXBUFLIST_UNLOCK();

						kfree(p);

					} else {
						LogMsg("Xmit HP BLOCK psession=%x p->plist=%x hp->elems=%x\n", pSession,
						       pSession->plist, FQDB.HPTcpSessionsList->elems);
						FQDB.LastHPSession = pSession;

						FQDB.NextTime = TxConfig.HPInterval;
						break;	/* exit HP Sessions */
					}
					pSession = (pIPSession) (pSession->next);
					LogMsg("Xmit HP next psession=%x hp->elems=%x\n", pSession,
					       FQDB.HPTcpSessionsList->elems);

				} else if ((int)(FQDB.PrevTime - pSession->dwLastActivityTime) >
					   (int)TxConfig.TcpSessionTO) {
					tmp = pSession;
					pSession = (pIPSession) (pSession->next);
					/*LogMsg("Xmit HP TO rm=%x next=%x hp->elems=%x",tmp,pSession,FQDB.HPTcpSessionsList->elems); */
					if (FQD(FQ_DEBUG_TO)) {
						printk(KERN_ERR "Xmit HP TO rm=%p next=%p hp->elems=%p\n", tmp,
						       pSession, FQDB.HPTcpSessionsList->elems);
					}
					dbllRemove(FQDB.HPTcpSessionsList, tmp);
					IPSessionDestruct(tmp);
				} else if (pSession->lastAckOptTime && FQDB.numVHPSessions
					   && (int)(FQDB.PrevTime - pSession->lastAckOptTime) >
					   TxConfig.TcpVHPSessionTO) {
					pSession->lastAckOptTime = 0;
					FQDB.numVHPSessions--;
				} else {
					pSession = (pIPSession) (pSession->next);
					LogMsg("Xmit HP No TO next psession=%x hp->elems=%x\n", pSession,
					       FQDB.HPTcpSessionsList->elems);
				}

				if (pSession == NULL && more == TRUE) {
					pSession = (pIPSession) (FQDB.HPTcpSessionsList->elems);
					LogMsg("Xmit HP return head psession=%x hp->elems=%x\n", pSession,
					       FQDB.HPTcpSessionsList->elems);
					more = FALSE;
				}
			}

			FQDB.TxThreadState = 7;

			LogMsg("Xmit HP Done. LP last=%x elems=%x\n", FQDB.LastSession, FQDB.LPSessionsList->elems);

			more = FALSE;
			if (FQDB.LastSession == NULL) {
				pSession = (pIPSession) (FQDB.LPSessionsList->elems);
			} else {
				pSession = FQDB.LastSession;
				FQDB.LastSession = NULL;

				if (pSession != (pIPSession) (FQDB.LPSessionsList->elems))
					more = TRUE;	/* if we didn't break due to BW limit we must go back to list head */

			}
			while (pSession) {
				FQDB.TxThreadState = 8;

				if (pSession->plist != NULL) {
					if ((gDataChannelDatabase.aboveHighWatermarkState)
					    || (gDataPacketsLimitDatabase.limitedState)
					    || ((gDataPacketsLimitDatabase.remainingSize < 0)
						&& (gDataPacketsLimitDatabase.limitSizeEnabled))) {
						FQDB.LastSession = pSession;
						/*OSAFlagSet(dataChanTxBuffersAvailFlagRef, TX_BUFFERS_ARE_AVAILABLE_MASK, OSA_FLAG_OR); */

						goto setup_next;
					}
/*
					if (gDataPacketsLimitDatabase.limitSizeEnabled &&
						(gDataPacketsLimitDatabase.remainingSize<(pSession->plist->txnode->length+TxConfig.UlDlRsvdBytes) ) &&
						(FQDB.numVHPSessions)) {
						FQDB.LastSession=pSession;
						//OSAFlagSet(dataChanTxBuffersAvailFlagRef, TX_BUFFERS_ARE_AVAILABLE_MASK, OSA_FLAG_OR);

						goto setup_next;
					}
*/
					if ((totalTxInRow <= MAX_TX_IN_ONE_LOOP)
					    && (totalLPByesTxInRow + pSession->plist->txnode->length < maxLPBytesInRow)) {

						/* try to send */
						txrc =
						    CI_DataSendReqPrim_REQ_CNF(pSession->plist->txnode->cid,
									       pSession->plist->txnode->data,
									       pSession->plist->txnode->length);
						if (txrc != TX_SUCCESS) {
							if (txrc == NO_RX_BUFS || txrc == NO_SHMEM_MEMORY_AVAIL) {
								/* we must relax out thread a bit ... */
								FQDB.LastSession = pSession;
								printk("*********slp******\n");
								msleep_interruptible(1);
								FLAG_SET(&dataChanTxBuffersAvailFlagRef,
									 TX_BUFFERS_ARE_AVAILABLE_MASK, OSA_FLAG_OR);
								goto xmit_loop_begin;
							} else if (txrc != NO_CID) {
								/*assert on any other failure */
								/*BUG_ON(1); */
								CleanupAll();
								goto xmit_loop_begin;
							}
						}
						totalTxInRow++;
						totalLPByesTxInRow += pSession->plist->txnode->length;
						FQDB.CurrentBytesIn -= pSession->plist->txnode->length;
						/*      addxfermap(pSession->plist->Length,0,BitsAllowedOther>>3); */
						p = GetIPSessionPacket(pSession);
						FQDB.numPackets--;
						FQDB.numLPPackets--;
						FQDB.TotalPOut++;
						/*TXBUFLIST_UNLOCK(lock_flags); */

						if (FQD(FQ_DEBUG_XMIT)) {
							int j;
							int sumo = 0;

							for (j = 0; j < MAX_QOS_CIDS; j++)
								sumo += (FQDB.OtherPackets[j]->plist != NULL);

							printk(KERN_ERR
							       "xmit lp hps= %d lps= %d numhpp= %ld numlpp= %ld bin= %ld\r\n",
							       FQDB.HPTcpSessionsList->count,
							       FQDB.LPSessionsList->count - MAX_QOS_CIDS + sumo,
							       FQDB.numHPPackets, FQDB.numLPPackets,
							       FQDB.CurrentBytesIn);
						}
						if (pSession->plist != NULL && pSession->plist->count > 0) {
							more = TRUE;
							LogMsg("Xmit LP more psession=%x p->plist=%x lp->elems=%x\n",
							       pSession, pSession->plist, FQDB.LPSessionsList->elems);
						}

						TXBUFLIST_LOCK();
						gUplinkPendingData.queuePendingData -= p->length;
						CI_UPLINK_CALC_TOTAL_PENDING_DATA;
						TXBUFLIST_UNLOCK();

						kfree(p);

					} else {
						FQDB.LastSession = pSession;
						LogMsg("Xmit LP BLOCK psession=%x p->plist=%x lp->elems=%x\n", pSession,
						       pSession->plist, FQDB.LPSessionsList->elems);
/*
						if (FQDB.NextTime==CLEANUP_TIME){
						   FQDB.NextTime=TxConfig.MaxTxSleepTime;
						}
*/
						/*break; // exit Other Sessions */
						goto setup_next;
					}
					pSession = (pIPSession) (pSession->next);
					LogMsg("Xmit LP next psession=%x lp->elems=%x\n", pSession,
					       FQDB.LPSessionsList->elems);

				} else {
					int j;
					for (j = 0; j < MAX_QOS_CIDS; j++) {
						if (pSession == FQDB.OtherPackets[j])
							break;
					}

					if ((j == MAX_QOS_CIDS)
					    && ((int)(FQDB.PrevTime - pSession->dwLastActivityTime) >
						(int)TxConfig.TcpSessionTO)) {
						tmp = pSession;
						pSession = (pIPSession) (pSession->next);
						/*LogMsg("Xmit LP TO rm=%x next psession=%x lp->elems=%x",tmp, pSession,FQDB.LPSessionsList->elems); */
						if (FQD(FQ_DEBUG_TO)) {
							printk(KERN_ERR
							       "Xmit LP TO rm=%p next psession=%p lp->elems=%p\n", tmp,
							       pSession, FQDB.LPSessionsList->elems);
						}
						dbllRemove(FQDB.LPSessionsList, tmp);
						IPSessionDestruct(tmp);
					} else {

						pSession = (pIPSession) (pSession->next);

						LogMsg("Xmit LP NO TO next psession=%x lp->elems=%x", pSession,
						       FQDB.LPSessionsList->elems);
					}
				}

				if (pSession == NULL && more == TRUE) {
					pSession = (pIPSession) (FQDB.LPSessionsList->elems);
					LogMsg("Xmit LP MORE  next psession=%x lp->elems=%x\n", pSession,
					       FQDB.LPSessionsList->elems);
					more = FALSE;
				}
			}

			FQDB.PrevNumPackets = FQDB.numPackets;
			FQDB.PrevNumHPPackets = FQDB.numHPPackets;

			LogMsg("XMIT LP loop done nump=%ld numhp=%ld will sleep=%ld\n", FQDB.PrevNumPackets,
			       FQDB.PrevNumHPPackets, FQDB.NextTime);

		}

setup_next:
		more = FALSE;
		FQDB.NextTime = 0;
		if (FQDB.numPackets || FQDB.BytesInTxList) {
			more = TRUE;
		}

		if (more) {
			FLAG_SET(&dataChanTxBuffersAvailFlagRef, TX_BUFFERS_ARE_AVAILABLE_MASK, OSA_FLAG_OR);
		}

		if (TimerRequestedDiff)
/*                      if (  ( (gDataPacketsLimitDatabase.limitSizeEnabled && gDataPacketsLimitDatabase.remainingSize<8000) || */
/*                        (gDataPacketsLimitDatabase.limitSizeEnabled ==FALSE) ) */
			if (more || TimerRequestedDiff != 2800)

				setup_wkup(TimerRequestedDiff);
	}

	gQOSTaskQuitFlag = 3;
	return 0;
}

static void FreeAllSessions(void)
{
	pIPSession pSession, tmp;
	PDCK_PACKET_NODE tmp2;

	pSession = (pIPSession) (FQDB.HPTcpSessionsList->elems);
	while (pSession) {
		tmp = pSession;
		pSession = (pIPSession) (pSession->next);
		if (dbllRemove(FQDB.HPTcpSessionsList, tmp) != -1) {
			IPSessionDestruct(tmp);
		}
	}
	pSession = (pIPSession) (FQDB.LPSessionsList->elems);
	while (pSession) {
		int j;
		tmp = pSession;
		pSession = (pIPSession) (pSession->next);

		for (j = 0; j < MAX_QOS_CIDS; j++)
			if (tmp == FQDB.OtherPackets[j])
				break;

		if (j == MAX_QOS_CIDS) {
			dbllRemove(FQDB.LPSessionsList, tmp);
			IPSessionDestruct(tmp);
		} else {
			if (tmp->plist) {
				printk("QOS free Other packets\n");
				IPPacketListItemDestruct(tmp->plist);
				tmp->plist = NULL;
			}
		}
	}

	while (QosInputList != NULL) {
		tmp2 = QosInputList;
		QosInputList = QosInputList->next;
		kfree(tmp2);
	}

	QosInputList = QosInputListLast = NULL;
}

void StopQOS_CI_DataSendTask()
{

	while (gQOSTaskQuitFlag == 0) {
		/* wait for task to start */
		msleep_interruptible(1);
	}
	gQOSTaskQuitFlag = 2;
	msleep_interruptible(1);
	if (gQOSTaskQuitFlag == 2) {
		FLAG_SET(&dataChanTxBuffersAvailFlagRef, TX_BUFFERS_ARE_AVAILABLE_MASK, OSA_FLAG_OR);
		FLAG_SET(&dataChanLowWatermarkFlagRef, TX_LOW_WATERMARK_STATE_MASK | TX_REQ_MAX_DATA_PACKET_LIMIT_MASK,
			 OSA_FLAG_OR);
	}
	while (gQOSTaskQuitFlag == 2) {
		msleep_interruptible(1);
	}
	gQOSTaskQuitFlag = 0;
	printk("QOS task Stoped\n");

	TXBUFLIST_LOCK();
	TxConfig.DoQos = 0;
	FreeAllSessions();

	TXBUFLIST_UNLOCK();
}
