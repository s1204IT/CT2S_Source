/*
 * (C) Copyright 2006-2011 Marvell International Ltd.
 * All Rights Reserved
 */

/* ===========================================================================
File        : shmem_kernel_to_user_ipc.c
(C) Copyright 2008 Marvell International Ltd. All Rights Reserved.

=========================================================================== */
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/socket.h>
#include <linux/netlink.h>
#include <net/sock.h>

/*#include "tel_assert.h" */
#include "acipc.h"
#include "acipc_data.h"
#include "shmem_kernel_to_user_ipc.h"

/* Include SHMEM debug log header */
#if 1
#include "dbgLog.h"
DBGLOG_EXTERN_VAR(shmem);
#define SHMEM_DEBUG_LOG2(a, b, c) DBGLOG_ADD(shmem, a, b, c)
#else
#define SHMEM_DEBUG_LOG2(a, b, c)
#endif

/*#ifdef DEBUG */
  /*pr_debug is defined in kernel.h */
#undef pr_debug
#define pr_debug(fmt, arg...) printk(fmt, ##arg)
/*#else */
/*  #define pr_debug(fmt, arg...) do { } while(0) */
/*#endif */

typedef void (*ACIPCD_CTRL_CALLBACK_RXIND) (void *, unsigned int);
typedef void (*ACIPCD_CTRL_CALLBACK_LINKSTATUSIND) (unsigned char);
typedef void (*ACIPCD_CTRL_CALLBACK_LINKSTATUSINDEXT) (unsigned char, UINT32);

#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
typedef void (*ACIPCD_CTRL_CALLBACK_WATERMARKIND) (ACIPCD_WatermarkTypeE);
#else /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
typedef void (*ACIPCD_CTRL_CALLBACK_LOWWMIND) (void);
#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */

typedef void (*ACIPCD_CTRL_CALLBACK_TXDONECNF) (void *);
typedef void (*ACIPCD_CTRL_CALLBACK_TXFAILCNF) (void *);

typedef union {
	ACIPCD_CTRL_CALLBACK_RXIND pCallbackRxIndFn;
	ACIPCD_CTRL_CALLBACK_LINKSTATUSIND pCallbackLinkStatusIndFn;
	ACIPCD_CTRL_CALLBACK_LINKSTATUSINDEXT pCallbackLinkStatusIndExtFn;
#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
	ACIPCD_CTRL_CALLBACK_WATERMARKIND pCallbackWatermarkIndFn;
#else				/*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
	ACIPCD_CTRL_CALLBACK_LOWWMIND pCallbackLowWmIndFn;
#endif				/*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
	ACIPCD_CTRL_CALLBACK_TXDONECNF pCallbackTxDoneCnfFn;
	ACIPCD_CTRL_CALLBACK_TXFAILCNF pCallbackTxFailCnfFn;
} ACIPCD_CALLBACK_FN;

typedef struct {
	ACIPCD_CALLBACK_FN kern_callb;
	pid_t pid;
} user_callback_addr;

/************************************************************************
 * External Interfaces
 ***********************************************************************/

/************************************************************************
 * Internal Interfaces
 ***********************************************************************/
static int send_callback(pid_t pid, unsigned int serviceId, unsigned int functionId, SHMEMCallbackContext ptParms);

/************************************************************************
 * External Variables
 ***********************************************************************/

/************************************************************************
 * Internal Variables
 ***********************************************************************/
static struct semaphore g_callb_sem;
static struct sock *nl_callb_sk;
static user_callback_addr callback_per_pid[LAST_CB_TYPE][ACIPCD_LAST_SERVICE_ID];

#define SET_CALLBACK(t, n) callback_per_pid[t][n].kern_callb.pCallback##t##Fn =  callback_##t##n;

#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)

#define CALLBACK_ENTRY_WM_IND_HELPER(n)    \
							void callback_WatermarkInd##n(ACIPCD_WatermarkTypeE watermarkType) \
							{  \
								SHMEMCallbackContext ptParms;\
								ptParms.watermarkTypeParms.type = watermarkType; \
								send_callback(callback_per_pid[WatermarkInd][n].pid, n, WatermarkInd, ptParms); \
							}

#else /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */

#define CALLBACK_ENTRY_WM_IND_HELPER(n)    \
							void callback_LowWmInd##n(void) \
							{  \
								SHMEMCallbackContext ptParms;\
								send_callback(callback_per_pid[LowWmInd][n].pid, n, LowWmInd, ptParms); \
							}

#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */

#define CALLBACK_ENTRY(n)\
							void callback_RxInd##n(void *pData, unsigned int uiSize) \
							{  \
							   SHMEMCallbackContext ptParms;\
							   SHMEM_DEBUG_LOG2(MDB_callback_RxInd_n, DBG_LOG_FUNC_START, n); \
							   SHMEM_DEBUG_LOG2(MDB_callback_RxInd_n, 0x1, pData); \
							   SHMEM_DEBUG_LOG2(MDB_callback_RxInd_n, 0x2, uiSize); \
							   ptParms.rxIndParms.pData = pData; ptParms.rxIndParms.len = uiSize; \
							   send_callback(callback_per_pid[RxInd][n].pid, n, RxInd, ptParms); \
							   SHMEM_DEBUG_LOG2(MDB_callback_RxInd_n, DBG_LOG_FUNC_END, n); \
							}  \
							void callback_LinkStatusInd##n(unsigned char bStatus) \
							{  \
							  SHMEMCallbackContext ptParms;\
							  ptParms.linkStatusParms.LinkStatus = bStatus; \
							  send_callback(callback_per_pid[LinkStatusInd][n].pid, n, LinkStatusInd, ptParms); \
							}  \
							void callback_LinkStatusIndExt##n(unsigned char bStatus, UINT32 param) \
							{  \
								SHMEMCallbackContext ptParms;\
								ptParms.linkStatusParms.LinkStatus = bStatus; \
								ptParms.linkStatusParms.pData = (void *)param; \
								send_callback(callback_per_pid[LinkStatusInd][n].pid, n, LinkStatusInd, ptParms); \
							}  \
							CALLBACK_ENTRY_WM_IND_HELPER(n) \
							void callback_TxDoneCnf##n(void *pData) \
							{  \
								SHMEMCallbackContext ptParms;\
								ptParms.confParms.pData = pData; \
								send_callback(callback_per_pid[TxDoneCnf][n].pid, n, TxDoneCnf, ptParms); \
							}  \
							void callback_TxFailCnf##n(void *pData) \
							{  \
								SHMEMCallbackContext ptParms;\
								ptParms.confParms.pData = pData; \
								send_callback(callback_per_pid[TxFailCnf][n].pid, n, TxFailCnf, ptParms); \
							}  \


CALLBACK_ENTRY(ACIPCD_NVM_RPC)
CALLBACK_ENTRY(ACIPCD_DIAG_CONTROL)
CALLBACK_ENTRY(ACIPCD_DIAG_DATA)
CALLBACK_ENTRY(ACIPCD_RTC)
CALLBACK_ENTRY(ACIPCD_CI_CTRL)
CALLBACK_ENTRY(ACIPCD_AUDIO_DATA)
CALLBACK_ENTRY(ACIPCD_AUDIO_COTNROL)
CALLBACK_ENTRY(ACIPCD_GEN_RPC)
CALLBACK_ENTRY(ACIPCD_AUDIO_VCM_CTRL)
CALLBACK_ENTRY(ACIPCD_AUDIO_VCM_DATA)
CALLBACK_ENTRY(ACIPCD_DIAG_DATA_MSA)
CALLBACK_ENTRY(ACIPCD_DIAG_CONTROL_MSA)
CALLBACK_ENTRY(ACIPCD_TEST)

int getKernelCBFromUser(ACIPCD_ServiceIdE ServiceId, ACIPCD_CallBackFuncS *cbKernel, pid_t pid)
{
	int rc;
	rc = down_interruptible(&g_callb_sem);
	if (rc)
		return rc;	/* Waiting is aborted (by destroy telephony) */

	callback_per_pid[RxInd][ServiceId].pid = pid;
	callback_per_pid[LinkStatusInd][ServiceId].pid = pid;
#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
	callback_per_pid[WatermarkInd][ServiceId].pid = pid;
#else /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
	callback_per_pid[LowWmInd][ServiceId].pid = pid;
#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
	callback_per_pid[TxDoneCnf][ServiceId].pid = pid;
	callback_per_pid[TxFailCnf][ServiceId].pid = pid;

	cbKernel->RxIndCB = callback_per_pid[RxInd][ServiceId].kern_callb.pCallbackRxIndFn;
	cbKernel->LinkStatusIndCB = callback_per_pid[LinkStatusInd][ServiceId].kern_callb.pCallbackLinkStatusIndFn;
	cbKernel->LinkStatusIndCBext = callback_per_pid[LinkStatusIndExt][ServiceId].kern_callb.pCallbackLinkStatusIndExtFn;
#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
	cbKernel->WatermarkIndCB = callback_per_pid[WatermarkInd][ServiceId].kern_callb.pCallbackWatermarkIndFn;
#else /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
	cbKernel->LowWmIndCB = callback_per_pid[LowWmInd][ServiceId].kern_callb.pCallbackLowWmIndFn;
#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
	cbKernel->TxDoneCnfCB = callback_per_pid[TxDoneCnf][ServiceId].kern_callb.pCallbackTxDoneCnfFn;
	cbKernel->TxFailCnfCB = callback_per_pid[TxFailCnf][ServiceId].kern_callb.pCallbackTxFailCnfFn;

	up(&g_callb_sem);
	return 0;
}

static int send_callback(pid_t pid, unsigned int serviceId, unsigned int functionId, SHMEMCallbackContext ptParms)
{
	struct sk_buff *skb = NULL;
	struct nlmsghdr *nlh;
	char *pcur = NULL;
	int ret;
	static unsigned int isCounter;

	skb = alloc_skb(SHMEM_CALLBACK_PAYLOAD, GFP_ATOMIC);
	if (skb == NULL) {
		printk("send_callback shmem: alloc_skb error\n");
		return -1;
	}

	nlh = (struct nlmsghdr *)skb->data;
	nlh->nlmsg_len = SHMEM_CALLBACK_PAYLOAD;
	nlh->nlmsg_pid = 0;	/* from kernel */
	nlh->nlmsg_flags = 0;

	pcur = NLMSG_DATA(nlh);

	memcpy(pcur, &serviceId, sizeof(unsigned int));
	skb_put(skb, sizeof(struct nlmsghdr) + sizeof(unsigned int));
	pcur += sizeof(unsigned int);

	memcpy(pcur, &functionId, sizeof(unsigned int));
	skb_put(skb, sizeof(unsigned int));
	pcur += sizeof(unsigned int);

	memcpy(pcur, &isCounter, sizeof(unsigned int));
	skb_put(skb, sizeof(unsigned int));
	pcur += sizeof(unsigned int);

	memcpy(pcur, (void *)&ptParms, sizeof(ptParms));
	skb_put(skb, sizeof(ptParms));
	pcur += sizeof(ptParms);

	ret = netlink_unicast(nl_callb_sk, skb, pid, MSG_DONTWAIT);	/*MSG_DONTWAIT); */
	if (ret < 0) {

		printk("send_callback shmem: netlink_unicast error %d, pid=%d, serviceId=%d\n", ret, pid, serviceId);
		printk("send_callback shmem: netlink_unicast error %d, pid=%d, serviceId=%d\n", ret, pid, serviceId);
		/* BS:  ASSERT here, because netlink_unicast failed and that means something
		 *      went wrong with the receiving process.
		 */
		ASSERT(FALSE);
	}

	isCounter++;
	return ret;
}

void shmem_print_log_callback(void)
{
	int n = 0;		/*general service id */
	SHMEMCallbackContext ptParms;

	send_callback(callback_per_pid[RxInd][ACIPCD_NVM_RPC].pid, n, SHMEM_PrintUserLogCB, ptParms);	/*ACIPCD_CI_CTRL */
}

void shmem_save_log_callback(void)
{
	int n = 0;		/*general service id */
	SHMEMCallbackContext ptParms;

	send_callback(callback_per_pid[RxInd][ACIPCD_NVM_RPC].pid, n, SHMEM_SaveUserLogCB, ptParms);	/*ACIPCD_CI_CTRL */
}

static DEFINE_MUTEX(nl_data_mutex);

static void nl_data_ready(struct sk_buff *skb)
{
	mutex_lock(&nl_data_mutex);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
	wake_up_interruptible(sk_sleep(skb->sk));
#else
	wake_up_interruptible(skb->sk->sk_sleep);
#endif
	mutex_unlock(&nl_data_mutex);
}

void clean_callback(void)
{
	sock_release(nl_callb_sk->sk_socket);
}

int init_callback(void)
{
	struct netlink_kernel_cfg cfg;
	sema_init(&g_callb_sem, 1);

	memset(callback_per_pid, 0, sizeof(callback_per_pid));
	memset(&cfg, 0, sizeof(cfg));

	cfg.input = nl_data_ready;
	cfg.cb_mutex = &nl_data_mutex;

	nl_callb_sk = netlink_kernel_create(&init_net, NETLINK_CALLBACK, &cfg);
	if (!nl_callb_sk) {
		printk("netlink_kernel_create failed\n");
		return -1;
	}

	SET_CALLBACK(RxInd, ACIPCD_NVM_RPC);
	SET_CALLBACK(RxInd, ACIPCD_DIAG_CONTROL);
	SET_CALLBACK(RxInd, ACIPCD_DIAG_DATA);
	SET_CALLBACK(RxInd, ACIPCD_RTC);
	SET_CALLBACK(RxInd, ACIPCD_CI_CTRL);
	SET_CALLBACK(RxInd, ACIPCD_AUDIO_DATA);
	SET_CALLBACK(RxInd, ACIPCD_AUDIO_COTNROL);
	SET_CALLBACK(RxInd, ACIPCD_AUDIO_VCM_CTRL);
	SET_CALLBACK(RxInd, ACIPCD_AUDIO_VCM_DATA);
	SET_CALLBACK(RxInd, ACIPCD_DIAG_DATA_MSA);
	SET_CALLBACK(RxInd, ACIPCD_DIAG_CONTROL_MSA);

	SET_CALLBACK(RxInd, ACIPCD_TEST);

	SET_CALLBACK(LinkStatusInd, ACIPCD_NVM_RPC);
	SET_CALLBACK(LinkStatusInd, ACIPCD_DIAG_CONTROL);
	SET_CALLBACK(LinkStatusInd, ACIPCD_DIAG_DATA);
	SET_CALLBACK(LinkStatusInd, ACIPCD_RTC);
	SET_CALLBACK(LinkStatusInd, ACIPCD_CI_CTRL);
	SET_CALLBACK(LinkStatusInd, ACIPCD_AUDIO_DATA);
	SET_CALLBACK(LinkStatusInd, ACIPCD_AUDIO_COTNROL);
	SET_CALLBACK(LinkStatusInd, ACIPCD_AUDIO_VCM_CTRL);
	SET_CALLBACK(LinkStatusInd, ACIPCD_AUDIO_VCM_DATA);
	SET_CALLBACK(LinkStatusInd, ACIPCD_DIAG_DATA_MSA);
	SET_CALLBACK(LinkStatusInd, ACIPCD_DIAG_CONTROL_MSA);
	SET_CALLBACK(LinkStatusInd, ACIPCD_TEST);

#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
	SET_CALLBACK(WatermarkInd, ACIPCD_NVM_RPC);
	SET_CALLBACK(WatermarkInd, ACIPCD_DIAG_CONTROL);
	SET_CALLBACK(WatermarkInd, ACIPCD_DIAG_DATA);
	SET_CALLBACK(WatermarkInd, ACIPCD_RTC);
	SET_CALLBACK(WatermarkInd, ACIPCD_CI_CTRL);
	SET_CALLBACK(WatermarkInd, ACIPCD_AUDIO_DATA);
	SET_CALLBACK(WatermarkInd, ACIPCD_AUDIO_COTNROL);
	SET_CALLBACK(WatermarkInd, ACIPCD_AUDIO_VCM_CTRL);
	SET_CALLBACK(WatermarkInd, ACIPCD_AUDIO_VCM_DATA);
	SET_CALLBACK(WatermarkInd, ACIPCD_DIAG_DATA_MSA);
	SET_CALLBACK(WatermarkInd, ACIPCD_DIAG_CONTROL_MSA);
	SET_CALLBACK(WatermarkInd, ACIPCD_TEST);
#else /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
	SET_CALLBACK(LowWmInd, ACIPCD_NVM_RPC);
	SET_CALLBACK(LowWmInd, ACIPCD_DIAG_CONTROL);
	SET_CALLBACK(LowWmInd, ACIPCD_DIAG_DATA);
	SET_CALLBACK(LowWmInd, ACIPCD_RTC);
	SET_CALLBACK(LowWmInd, ACIPCD_CI_CTRL);
	SET_CALLBACK(LowWmInd, ACIPCD_AUDIO_DATA);
	SET_CALLBACK(LowWmInd, ACIPCD_AUDIO_COTNROL);
	SET_CALLBACK(LowWmInd, ACIPCD_DIAG_DATA_MSA);
	SET_CALLBACK(LowWmInd, ACIPCD_DIAG_CONTROL_MSA);
	SET_CALLBACK(LowWmInd, ACIPCD_TEST);
#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */

	SET_CALLBACK(TxDoneCnf, ACIPCD_NVM_RPC);
	SET_CALLBACK(TxDoneCnf, ACIPCD_DIAG_CONTROL);
	SET_CALLBACK(TxDoneCnf, ACIPCD_DIAG_DATA);
	SET_CALLBACK(TxDoneCnf, ACIPCD_RTC);
	SET_CALLBACK(TxDoneCnf, ACIPCD_CI_CTRL);
	SET_CALLBACK(TxDoneCnf, ACIPCD_AUDIO_DATA);
	SET_CALLBACK(TxDoneCnf, ACIPCD_AUDIO_COTNROL);
	SET_CALLBACK(TxDoneCnf, ACIPCD_AUDIO_VCM_CTRL);
	SET_CALLBACK(TxDoneCnf, ACIPCD_AUDIO_VCM_DATA);
	SET_CALLBACK(TxDoneCnf, ACIPCD_DIAG_DATA_MSA);
	SET_CALLBACK(TxDoneCnf, ACIPCD_DIAG_CONTROL_MSA);
	SET_CALLBACK(TxDoneCnf, ACIPCD_TEST);

	SET_CALLBACK(TxFailCnf, ACIPCD_NVM_RPC);
	SET_CALLBACK(TxFailCnf, ACIPCD_DIAG_CONTROL);
	SET_CALLBACK(TxFailCnf, ACIPCD_DIAG_DATA);
	SET_CALLBACK(TxFailCnf, ACIPCD_RTC);
	SET_CALLBACK(TxFailCnf, ACIPCD_CI_CTRL);
	SET_CALLBACK(TxFailCnf, ACIPCD_AUDIO_DATA);
	SET_CALLBACK(TxFailCnf, ACIPCD_AUDIO_COTNROL);
	SET_CALLBACK(TxFailCnf, ACIPCD_AUDIO_VCM_CTRL);
	SET_CALLBACK(TxFailCnf, ACIPCD_AUDIO_VCM_DATA);
	SET_CALLBACK(TxFailCnf, ACIPCD_DIAG_DATA_MSA);
	SET_CALLBACK(TxFailCnf, ACIPCD_DIAG_CONTROL_MSA);
	SET_CALLBACK(TxFailCnf, ACIPCD_TEST);

	return 0;
}
