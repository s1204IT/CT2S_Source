/*------------------------------------------------------------
(C) Copyright [2006-2008] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************/
/*                                                                      */
/* Title: Application Communication IPC Driver                          */
/*                                                                      */
/* Filename: acipc_data.c                                               */
/*                                                                      */
/* Author:   Ian Levine                                                 */
/*                                                                      */
/* Project, Target, subsystem: Tavor, Arbel & Boerne RTOS, HAL          */
/*                                                                      */
/* Remarks: -                                                           */
/*                                                                      */
/* Created: 4/2/2007                                                    */
/*                                                                      */
/* Modified:                                                            */
/************************************************************************/
#include <linux/wait.h>
#include <linux/timer.h>
#include <linux/hardirq.h>
#include <linux/delay.h>
#if defined (ENV_LINUX)
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <asm/uaccess.h>
#include <linux/list.h>
#include "osa_kernel.h"
/*#include "tel_assert.h" */

#include "com_mem_mapping_kernel.h"
/*#include "aci_debug.h" */
#include "ac_rpc.h"
#include "allocator.h"

#if defined(CONFIG_PXA95x_DVFM)
#include <mach/dvfm.h>
#endif

/*
 *  BS: Enable Power Management support for LINUX only
 */
#define FEATURE_SHMEM_PM

#endif /*ENV_LINUX */

#include "acipc.h"
#include "acipc_data.h"

#define     SHMEM_POOL              "ShmemPool"
#define     SIZEOF_CACHE_LINE       32
#define     CACHE_LINE_MASK         (SIZEOF_CACHE_LINE-1)

#if defined(ENV_LINUX)

#if (!defined OsaApi)
#define     OsaApi		/*  Untill the Osa WINCE CS will enter. */
#endif
#if (!defined OsaIsrApi)
#define     OsaIsrApi		/*  Untill the Osa WINCE CS will enter. */
#endif
#if (!defined OsaMemPoolApi)
#define     OsaMemPoolApi	/*  Untill the Osa WINCE CS will enter. */
#endif

#define     NODIAG
#endif

#if (!defined OsaApi)
#if (!defined OSA_NUCLEUS)
#error  Only in Nucleus SHMEM supports OSA prior to version 4.
#endif

typedef unsigned int OsaRefT;

#define     OsaCriticalSectionEnter(a, b)        disableInterrupts()
#define     OsaCriticalSectionExit(a, b)         restoreInterrupts(a)
#endif

#if (!defined OsaIsrApi)

#if defined(ENV_LINUX)
typedef unsigned int OS_HISR;

#define     OsaIsrNotify(a, b)                   OSAIsrNotify(a)
#else
#include "utils.h"
#include "bsp_hisr.h"

#define     OsaIsrNotify(a, b)                   OS_Activate_HISR((OS_HISR *)a)
#endif

static OS_HISR ACIPCD_DataIndHisrData, ACIPCD_QueueAdrsHisrData;

#endif

#if (!defined OsaMemPoolApi)
#define     OsaMemAlloc(a, b)                    alignMallocPoolX(b)
#define     OsaMemFree(a)                        alignFreePoolX((void *)a)
#define     OsaMemGetUserParam(a)                ((*((unsigned int *)a - 2)))
#define     OsaMemSetUserParam(a, b)             (*((unsigned int *)a - 2) = b)
#endif

/* Include SHMEM debug log header */
#if 1
#include "dbgLog.h"
DBGLOG_DECLARE_VAR(shmem);
#if defined (CONFIG_MACH_ESHELEVB)
#define     SHMEM_DBG_LOG_SIZE  2000	/*reduced from 10000 for Eshel */
#else
#define     SHMEM_DBG_LOG_SIZE  10000
#endif
#define     SHMEM_DEBUG_LOG2(a, b, c)     DBGLOG_ADD(shmem, a, b, c)
#else
#define SHMEM_DEBUG_LOG2(a, b, c)
#define DBGLOG_ENABLE(a, b)
#define DBGLOG_INIT(a, b)
#endif

/*#if defined(OSA_LINUX)
    #define ACI_PRINT   printk
#else
    #define ACI_PRINT   printf
#endif*/

/*----------- Global variable declarations -----------------------------------*/

/*----------- Local definition -----------------------------------------------*/
#define     GLOBAL_HIGH_WATER_MARK         (256-32)
#define     GLOBAL_LOW_WATER_MARK          (256-64)

#define     ABOVE_HIGH_WATER_MARK(nEXTrEAD, nEXTwRIYE, hIGHwM)      ((unsigned char)(nEXTwRIYE-nEXTrEAD) > hIGHwM)
#define     BELOW_LOW_WATER_MARK(nEXTrEAD, nEXTwRIYE, lOWwM)        ((unsigned char)(nEXTwRIYE-nEXTrEAD) < lOWwM)

#define     Q_EMPTY(nEXTrEAD, nEXTwRIYE)    (nEXTrEAD == nEXTwRIYE)
#define     Q_FULL(nEXTrEAD, nEXTwRIYE)     (nEXTrEAD == (unsigned char)(nEXTwRIYE+1))

#define     CACHE_LINE_ADRS(aDRS)   ((unsigned int)aDRS & ~CACHE_LINE_MASK)

#if defined(ENV_LINUX)
#define     WRITE_CACHE(cACHElINE)
#define     INVALIDATE_CACHE(cACHElINE)

#define     CacheInvalidateMemory(a, b)
#define     CacheCleanAndInvalidateMemory(a, b)
#endif

#if defined(ENV_LINUX)
#define MAP_OSA_HIDDEN_ADDR(pAddr)      (pAddr)
#define MAP_HIDDEN_ADDR_TO(pAddr)   (pAddr)
#endif

#define     MAX_SERVICES_PER_INTERFACE      32	/* Max allowed services for each core interface */

#define     MAX_SG_SERVICES                 MAX_SERVICES_PER_INTERFACE	/* Max allowed services for Seagull */
/*#define     MAX_SERVICES            ((unsigned int)ACIPCD_LAST_SERVICE_ID + 1) */

/*boaz.sommer.mrvl - prevent 'dead' RPC on silent reset - start */
#define     ALL_SERVICES_BITS(p)        ((1<<(p)->MaxServices) - 1)
/*boaz.sommer.mrvl - prevent 'dead' RPC on silent reset - start */
#define     MAX_MSA_SERVICES                ((unsigned int)(ACIPCD_MSA_LAST_SERVICE_ID - MAX_SERVICES_PER_INTERFACE) + 1)
#define     FIRST_ITEM(p)              (&(p)->ACIPCD_Database[0])
#define     LAST_ITEM(p)               (&(p)->ACIPCD_Database[(p)->MaxServices /*MAX_SERVICES*/])
#define     CURRENT_ITEM(p, sERVICEiD) ((unsigned int)sERVICEiD < (p)->MaxServices /*MAX_SERVICES*/ ? &(p)->ACIPCD_Database[(unsigned int)sERVICEiD] : NULL)
#define     SERVICE_ID_2_BIT(sERVICEiD)     (1<<(sERVICEiD))
#define     TX_REQ_WAIT_FLAG_MASK           1

#define     SHMEM_DB_FROM_SERVICE_ID(sERVICEiD) ((sERVICEiD < MAX_SG_SERVICES) ? &sgShmemDb : &msaShmemDb)
#define     TX_FAIL_IND             0xFFFF
#define     MAX_U32_CHANNELS        32

/*----------- Local Type definition ------------------------------------------*/
/*BS - limit rx queue read loop before updating ACIPCD_pThisSide->NextRead - start */
/*******************************************************************************/
/* The following definition forces RxInd to update ACIPCD_pThisSide->NextRead  */
/* so that COMM will not "think" that global HWM is reached                    */
/* This value should no be too small, as it will affect data performance       */
/*******************************************************************************/
#define NUM_ALLOWED_ITERATIONS_BETWEEN_NEXT_READ_UPDATE 20
/*BS - limit rx queue read loop before updating ACIPCD_pThisSide->NextRead - end */

#if !defined (FEATURE_SHMEM_PM)
#define CRITICAL_SECTION_GLOBAL_VAR
#define CRITICAL_SECTION_GLOBAL_INIT
#define CRITICAL_SECTION_VARIABLE
#define CRITICAL_SECTION_ENTER
#define CRITICAL_SECTION_LEAVE
#else
/*#define ENABLE_PM_DEBUG_PRINTS */

#define ENABLE_PM_DEBUG_LISTS
/*BS: Critical Section */
#if defined(ACI_LNX_KERNEL)
/* Linux Kernel implementation */

#include <linux/spinlock.h>

#define ACIPCD_CRITICAL_SECTION_GLOBAL_VAR(vARnAME)       \
    unsigned long vARnAME##_flags;                      \
    spinlock_t vARnAME##_protect_lock = __SPIN_LOCK_UNLOCKED(vARnAME##_protect_lock)

#define ACIPCD_CRITICAL_SECTION_GLOBAL_INIT(vARnAME) \
	spin_lock_init(&(vARnAME##_protect_lock));

#define ACIPCD_CRITICAL_SECTION_VARIABLE(vARnAME)             \
    extern unsigned long vARnAME##_flags;                     \
    extern spinlock_t vARnAME##_protect_lock	/* not actually referenced */

#define ACIPCD_CRITICAL_SECTION_ENTER(vARnAME)                    \
    do {                                                        \
		spin_lock_irqsave(&(vARnAME##_protect_lock), vARnAME##_flags);  \
    } while (0)

#define ACIPCD_CRITICAL_SECTION_LEAVE(vARnAME)              \
    do {                                        \
		spin_unlock_irqrestore(&(vARnAME##_protect_lock), vARnAME##_flags); \
    } while (0)
#endif

/*ACIPCD Critical Section */
#define CRITICAL_SECTION_GLOBAL_VAR             ACIPCD_CRITICAL_SECTION_GLOBAL_VAR(gACIPCDCriticalSection)
#define CRITICAL_SECTION_GLOBAL_INIT            ACIPCD_CRITICAL_SECTION_GLOBAL_INIT(gACIPCDCriticalSection)
#define CRITICAL_SECTION_VARIABLE               ACIPCD_CRITICAL_SECTION_VARIABLE(gACIPCDCriticalSection)
#define CRITICAL_SECTION_ENTER                  ACIPCD_CRITICAL_SECTION_ENTER(gACIPCDCriticalSection)
#define CRITICAL_SECTION_LEAVE                  ACIPCD_CRITICAL_SECTION_LEAVE(gACIPCDCriticalSection)

#if defined(ENABLE_PM_DEBUG_PRINTS)
#define PM_DBG_MSG(mSG)					printk(" "mSG"\n")
#define PM_DBG_MSG_1P(mSG, p1)			printk(" "mSG"\n", p1)
#define PM_DBG_MSG_2P(mSG, p1, p2)		printk(" "mSG"\n", p1, p2)
#else
#define PM_DBG_MSG(mSG)					{}
#define PM_DBG_MSG_1P(mSG, p1)        	{}
#define PM_DBG_MSG_2P(mSG, p1, p2)     	{}
#endif /*ENABLE_PM_DEBUG_PRINTS */

/*BS: ACIPCD PM */

#define ACIPCD_IDLE_TIMER_TIMEOUT_IN_MSECS     2

/*#define   ACIPCD_PM_DEBUG */

#define LINUX_FREE_RUNNING_32K_REG      __REG(0x40A00040)
#define TS                              LINUX_FREE_RUNNING_32K_REG

#if defined(ENABLE_PM_DEBUG_LISTS)

#define PM_DEBUG_LIST_SIZE      20
typedef struct {
	unsigned int serviceId;
	void *pData;
	unsigned int length;
} ACIPCD_PMDebugListS;

#endif /*ENABLE_PM_DEBUG_LISTS */

typedef void (*sisrRoutine) (void);

typedef struct {
	struct task_struct *hHISRThread;
	struct semaphore hDataSemaphore;
	struct semaphore hCancelSemaphore;
	atomic_t count;
	sisrRoutine sisr_routine;
} sHISRData;

typedef enum {
	ACIPCD_SLEEP_NOT_ALLOW = 0,
	ACIPCD_SLEEP_ALLOW
} ACIPCD_SleepStatesE;

typedef struct {
	unsigned int TxWaitAckCounter;	/*When equals zero, no TX resource are needed, so LPM is allowed. Otherwise, LPM is restricted */
	unsigned int RxWaitRspCounter;	/*When equals zero, no RX resource are needed, so LPM is allowed. Otherwise, LPM is restricted */
	unsigned int HisrWaitCounter;	/*When equals zero, no HISR are waiting so NO resource are needed, so LPM is allowed. Otherwise, LPM is restricted */

	unsigned char CurrentSleepAllow;	/*holding current sleep state */
	unsigned char TimerTicking;	/*is timer currently ticking ??? */

/* Idle Timer */
	struct timer_list IdleTimerRef;
	unsigned int IdleTimerTimeout;

#if defined(ENABLE_PM_DEBUG_LISTS)
	ACIPCD_PMDebugListS pmDebugList[PM_DEBUG_LIST_SIZE];
	unsigned int pmDebugListMisses;
#endif				/*ENABLE_PM_DEBUG_LISTS */

#if defined (ACIPCD_PM_DEBUG)
	unsigned int debugWakeCount;
	unsigned int debugSleepCount;
#endif				/*ACIPCD_PM_DEBUG */
} ACIPCD_PmDbS;

typedef struct {
	int powerDevice;
	struct clk *pClk;
} ACIPCD_LinuxPowerS;

static volatile ACIPCD_PmDbS PmDb;
#if defined(CONFIG_PXA95x_DVFM)
static ACIPCD_LinuxPowerS gACIPCDPmLinux;
#endif

/* Declare Critical Section */
CRITICAL_SECTION_GLOBAL_VAR;

#endif /*(FEATURE_SHMEM_PM) */

/*
 * Keep the NextWrite & NextRead as unsigned char and the Buf as 256.
 * No range checks will be needed for the cyclic buffer.
 */
typedef struct {
	unsigned short ServiceId;
	unsigned short Length;
	void *pData;
} ACIPCD_Data;

typedef struct {
	unsigned char NextRead;
	unsigned char NextWrite;
	unsigned char AboveHighWaterMark;
#ifndef ACIPCD_NO_IRQ_OPT
	unsigned char InRxProcessing;
	unsigned int TxEvent;
	unsigned int RxEvent;
	unsigned int OptTxPackets;
	unsigned char Filler[SIZEOF_CACHE_LINE - 16];	/*  This makes sure that the SharedMemory is cache line aligned. */
#else
	unsigned char Filler[SIZEOF_CACHE_LINE - 3];	/*  This makes sure that the SharedMemory is cache line aligned. */
#endif

	ACIPCD_Data SharedMemory[256];	/*  Cyclic buffer/ */
} ACIPCD_SharedDataS;

#define     FILLER_SIZE     (SIZEOF_CACHE_LINE - (sizeof(ACIPCD_SharedDataS) & CACHE_LINE_MASK))

typedef struct {
	ACIPCD_SharedDataS SideA;
	/*unsigned char               Filler1[FILLER_SIZE] ;     //  Make sure SideB is Cache line aligned. */
	ACIPCD_SharedDataS SideB;
} ACIPCD_SharedMemoryS;

typedef struct ShmemPoolAllocsS {
	void *pData;
	struct list_head node;
} ShmemPoolAllocsS;

static LIST_HEAD(ACIPCD_ShmemPoolAllocs);

typedef enum {
	E_CLOSED,
	E_NEAR_SIDE_READY,
	E_FAR_SIDE_READY,
	E_CONNECTED
} ACIPCD_LinkStateE;

typedef struct {
	ACIPCD_ConfigS Config;
	unsigned char WmHandlerActivated;
	unsigned char AboveHighWm;
	ACIPCD_LinkStateE LinkState;
	unsigned char HighWm;
	unsigned char LowWm;
	unsigned char Send;
	unsigned char Ack;
	ACIPCD_CallBackFuncS CallBackFunc;
	struct {
		unsigned int Send;
		unsigned int SendControl;
		unsigned int TxReq;
		unsigned int RxDoneRsp;
		unsigned int RxFailRsp;
		unsigned int Recv;
		unsigned int RecvControl;
		unsigned int RxInd;
		unsigned int TxDoneCnf;
		unsigned int TxFailCnf;
		unsigned int HighWmCnt;
		unsigned int LowWmCnt;
	} Debug;
} ACIPCD_DatabaseS;

#if 0
static char *acipc_sid_to_name[] = {
	"ACIPCD_INTERNAL",

#if !defined(CI_DATA_USE_OLD_DATA_PATH)	/*SHMEM channels support new Eshel/Nevo DATA path */
	"ACIPCD_CI_DATA_UPLINK"	/*1 */
	    "ACIPCD_CI_DATA_DOWNLINK",	/*2 */
	"ACIPCD_DIAG_DATA",	/*3 */
	"ACIPCD_AUDIO_DATA",	/*4 */
	"ACIPCD_CI_CTRL",	/*5 */
	"ACIPCD_CI_DATA_CONTROL",	/*6 */
	"ACIPCD_DIAG_CONTROL",	/*7 */
	"ACIPCD_AUDIO_COTNROL",	/*8 */
	"ACIPCD_NVM_RPC",	/*9 */
	"ACIPCD_RTC",		/*10 */
	"ACIPCD_GEN_RPC",	/*11 */
	"ACIPCD_GPC",		/*12 */
	"ACIPCD_USBMGR_TUNNEL",	/*13 */
	/*YG: all the below are temp use only - shall be removed */
	"ACIPCD_CI_DATA_REQ_CNF",	/*14 */
	"ACIPCD_CI_DATA_IND_RSP",	/*15 */
	"ACIPCD_CI_DATA_CONTROL_OLD",	/*16 */
	"ACIPCD_AGPS",		/*17 */

#else /*CI_DATA_USE_OLD_DATA_PATH */
	"ACIPCD_CI_DATA_REQ_CNF",	/*1 */
	"ACIPCD_CI_DATA_IND_RSP",	/*2 */
	"ACIPCD_DIAG_DATA",	/*3 */
	"ACIPCD_AUDIO_DATA",	/*4 */
	"ACIPCD_CI_CTRL",	/*5 */
	"ACIPCD_CI_DATA_CONTROL_OLD",	/*6 */
	"ACIPCD_DIAG_CONTROL",	/*7 */
	"ACIPCD_AUDIO_COTNROL",	/*8 */
	"ACIPCD_NVM_RPC",	/*9 */
	"ACIPCD_RTC",		/*10 */
	"ACIPCD_AGPS",		/*11 */
	"ACIPCD_GEN_RPC",	/*12 */
	"ACIPCD_USBMGR_TUNNEL",	/*13 */
	"ACIPCD_GPC",		/*14 */
	"ACIPCD_AUDIO_VCM_CTRL",	/*15 */
	"ACIPCD_AUDIO_VCM_DATA",	/*16 */
#endif /*CI_DATA_USE_OLD_DATA_PATH */

	"ACIPCD_RESERVE_1",	/*  Can add more ... */
	"ACIPCD_LAST_SERVICE_ID"
};

#endif

/*
 * The Shared Memory is defined on the Arbel side and the ReadOnly & WriteOnly
 * pointers are also initialized at the link time.
 * On the Apps side the Shared Memory is only a pointer that is received from
 * the Comm during initialization and therefor the ReadOnly & WriteOnly pointers
 * are also set during initialization phase.
 */
#if 0				/*(defined FLAVOR_APP) */
static unsigned int
#endif
#if (defined FLAVOR_COM)
__align(32)			/*  Cache line alignment. */
static volatile ACIPCD_SharedMemoryS ACIPCD_SharedMemory;
#endif

#if (defined FLAVOR_COM)
static volatile ACIPCD_SharedDataS
    *ACIPCD_pOtherSide = &ACIPCD_SharedMemory.SideA, *ACIPCD_pThisSide = &ACIPCD_SharedMemory.SideB
#endif
    ;

#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
static unsigned char gACIPCDRxIndHisrKick = FALSE;
/*static unsigned int gACIPCD_HWM_POLLING_WORKAROUND_cntr; */
#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */

#if 0				/*SHMEM-MSA */
static OsaRefT ACIPCD_CriticalSectionRef, ACIPCD_DataIndHisrRef, ACIPCD_QueueAdrsHisrRef, ACIPCD_ShmemRef;
#endif

#define WM_IND_FLAG_MASK    0x0007FFFF
#if 0
static OSAFlagRef WmFlagRef;
/*static unsigned int gACIPCDWmIndKick    =   0; */
static OSAFlagRef WmNotifiedFlagRef;
#endif

#define WM_IND_WAIT_FOR_SET     100	/*milliseconds */
static wait_queue_head_t gWaitThreadsQueue;
static unsigned int gWmWaitTimeInJiffifes;

typedef struct ACIPCD_RpcInfoS {
	void *pData;
	ACIPCD_ReturnCodeE rc;
} ACIPCD_RpcInfoS;

#if (defined OSA_WINCE)
typedef struct {
	unsigned int ui1;
	unsigned int ui2;
} ACIPCD_DataStoragS;
#endif

/*
 * The following is a structure definition for all
 * SHMEM global variables.
 *
 */

#define MAX_SHORT_NAME_LENGTH   32

#define GLOBAL_SID_2_LOCAL_SID(sERVICEiD, cOREiD)    (sERVICEiD - (cOREiD)*32)
#define LOCAL_SID_2_GLOBAL_SID(sERVICEiD, cOREiD)    (sERVICEiD + (cOREiD)*32)
#define CONVERT_ENDIAN_SHORT(nUM)   (((nUM) << 8) | ((nUM) >> 8))
#define CONVERT_ENDIAN_LONG(nUM)    ((((nUM)>>24)&0xff) | (((nUM)<<8)&0xff0000) | (((nUM)>>8)&0xff00) | (((nUM)<<24)&0xff000000))

typedef enum CoreTypeE {
	CORE_SEAGULL,
	CORE_MSA
} CoreTypeE;

typedef enum EndianTypeE {
	BIG_ENDIAN,
	LITTLE_ENDIAN
} EndianTypeE;

typedef void (*HandlerPtr) (void);

typedef struct ShmemCriticalSectionInfoS {
	char Name[MAX_SHORT_NAME_LENGTH];
} ShmemCriticalSectionInfoS;

typedef struct ShmemHandlerInfoS {
	HandlerPtr Callback;
	char Name[MAX_SHORT_NAME_LENGTH];
} ShmemHandlerInfoS;

typedef struct ShmemInfoS {
	CoreTypeE CoreId;
	EndianTypeE EndianType;

	acipc_dev_type AcipcType;

	BOOL bLinkDownDONE;
	BOOL bLinkDownPartial;

	OsaRefT ACIPCD_CriticalSectionRef;
	OsaRefT ACIPCD_DataIndHisrRef;
	OsaRefT ACIPCD_QueueAdrsHisrRef;
	OsaRefT ACIPCD_ShmemRef;

	unsigned char MaxServices;
	ACIPCD_Data *ACIPCD_pRxEntry;
	ACIPCD_DatabaseS ACIPCD_Database[MAX_SERVICES_PER_INTERFACE];

	ACIPCD_SharedDataS *ACIPCD_pOtherSide;
	ACIPCD_SharedDataS *ACIPCD_pThisSide;

/*
 * The Shared Memory is defined on the COMM side and the ReadOnly & WriteOnly
 * pointers are also initialized at the link time.
 * On the Apps side the Shared Memory is only a pointer that is received from
 * the Comm during initialization and therefor the ReadOnly & WriteOnly pointers
 * are also set during initialization phase.
 */
#if (defined FLAVOR_APP)
	unsigned int ACIPCD_SharedMemory;
#endif
#if (defined FLAVOR_COM)
	ACIPCD_SharedMemoryS *ACIPCD_SharedMemory;
#endif

	/* Watermark */
#if defined (ENV_LINUX)
	OsaRefT ACIPCD_WmHandlerRef;
	flag_wait_t WmFlagRef;
	flag_wait_t WmNotifiedFlagRef;
	wait_queue_head_t gWaitThreadsQueue;
	unsigned int gWmWaitTimeInJiffifes;
#elif defined (OSA_NUCLEUS)
#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
	unsigned char gACIPCDWmIndKick;
#endif
	OSAFlagRef gSHMEMWmIndFlagRef;
	OSTaskRef gSHMEMWmIndTaskRef;
#endif

	/* RPC */
	ACIPCD_RpcInfoS ACIPCD_RpcInfo[ACIPCD_MAX_RPC];
	flag_wait_t RpcEndedFlagRef;

	ShmemCriticalSectionInfoS CriticalSectionParams;
	ShmemHandlerInfoS RxIndicationHandlerParams;
	ShmemHandlerInfoS AddressIndicationHandlerParams;
#if defined (ENV_LINUX)
	ShmemHandlerInfoS WatermarkHandlerParams;
#endif

	ACIPC_RecEventCB RxIndicationEventHandlerPtr;
	ACIPC_RecEventCB AddressIndicationEventHandlerPtr;

	/* ACIPC interface */
#if 0
	ACIPCEventBindPtr ACIPCEventBind;
	ACIPCDataSendPtr ACIPCDataSend;
	ACIPCEventSetPtr ACIPCEventSet;
#endif
	BOOL early_LinkDown_Notification_DONE;
} ShmemInfoS;

/*
 * sgShmemInterface
 * Hold all global variable for the Seagull interface
 * ('sg' is an abbreviation for Seagull)
 */
static ShmemInfoS sgShmemDb;

/*
 * msaShmemInterface
 * Hold all global variable for the MSA interface
 */
static ShmemInfoS msaShmemDb;

#ifdef FLAVOR_DIAG_MSA_MANAGE
/* Run-time manage capability to disable/enable ACIPC-MSA
 * The default is disable.
 * To enable: set the ACIPCD_msa_enable and call for ACIPCDInitMsaDatabase()
 * or call for ACIPCDInitMsaHandlers() - better.
 *
 * The enable currently is not implemented but may be called by either
 *  ACIPCDTxReqACIPC, ACIPCDComIfReset, ACIPCDComRecoveryIndicate...
 */
/*global*/ int ACIPCD_msa_enable = 1;
#endif

/*----------- Local function prototypes --------------------------------------*/
static void ACIPCDRemoveFromAllocList(void *pShmem);
static void ACIPCDAddToAllocList(void *pShmem);
static unsigned int ACIPCDQueueAdrsIndSgEvent(unsigned int EventsStatus);
static unsigned int ACIPCDQueueAdrsIndMsaEvent(unsigned int EventsStatus);
static unsigned int ACIPCDRxIndSgEvent(unsigned int EventsStatus);
static unsigned int ACIPCDRxIndMsaEvent(unsigned int EventsStatus);
static void ACIPCDRxIndSgHandler(void);
static void ACIPCDRxIndMsaHandler(void);
static void ACIPCDDummyRxInd(void *pData, unsigned int len);
static void ACIPCDDummyLinkStatusInd(unsigned char LinkStatus);
static void ACIPCDDummyTxDoneCnf(void *pData);
static void ACIPCDDummyTxFailCnf(void *pData);
static void ACIPCDRpcEnded(ShmemInfoS *pShmemDb, void *pData, ACIPCD_ReturnCodeE rc);
static void ACIPCDRxInd(ShmemInfoS *pShmemDb);
static ACIPCD_ReturnCodeE ACIPCDSend(ShmemInfoS *pShmemDb,
				     ACIPCD_ServiceIdE ServiceId, void *pData,
				     unsigned short Length, ACIPCD_DatabaseS *pDb, unsigned char dontMakeACIPCirq);

static unsigned int ACIPCD_DummyEvent(unsigned int EventsStatus)
{
	return 0;
}

static void ACIPCD_DummyHandler(void)
{
}

/*----------- Local variable declarations ------------------------------------*/

#if defined (FEATURE_SHMEM_PM)

/*#define FEATURE_SHMEM_PM_BUSY_DEBUG */
#if !defined (FEATURE_SHMEM_PM_BUSY_DEBUG)
#define PM_DIS_CNTR_PPLUS
#define PM_ENA_CNTR_PPLUS
#define ACIPCD_busyDebugStart() /**/
#else /*FEATURE_SHMEM_PM_BUSY_DEBUG */
static OSTimerRef ACIPCD_busyDebugRef;
static unsigned int busyDebug_pmDisCntr[2];
static unsigned int busyDebug_pmEnaCntr[2];
#define PM_DIS_CNTR_PPLUS        (busyDebug_pmDisCntr[0]++)
#define PM_ENA_CNTR_PPLUS        (busyDebug_pmEnaCntr[0]++)

static void ACIPCD_busyDebugTimeout(unsigned int id)
{
	if ((busyDebug_pmDisCntr[0] != busyDebug_pmDisCntr[1])
	    && (busyDebug_pmEnaCntr[0] == busyDebug_pmEnaCntr[1])) {
		printk("SHMEM PM busyDebug %x %x %x %x: (%x || %x || %x)\n",
		       busyDebug_pmDisCntr[0], busyDebug_pmDisCntr[1],
		       busyDebug_pmEnaCntr[0], busyDebug_pmEnaCntr[1],
		       PmDb.TxWaitAckCounter, PmDb.RxWaitRspCounter, PmDb.HisrWaitCounter);
	} else {
		busyDebug_pmDisCntr[1] = busyDebug_pmDisCntr[0];
		busyDebug_pmEnaCntr[1] = busyDebug_pmEnaCntr[0];
	}
}

static void ACIPCD_busyDebugStart(void)
{
	int status;
	if (ACIPCD_busyDebugRef == NULL) {
		setup_timer(&ACIPCD_busyDebugTimer, ACIPCD_busyDebugTimeout, 0);
		mod_timer(&ACIPCD_busyDebugTimer, jiffies + 600000 / 156);
		printk("SHMEM PM busyDebug ACTIVE\n");
	}
}
#endif /*FEATURE_SHMEM_PM_BUSY_DEBUG */

static int DataThread(void *pParam)
{
	sHISRData *pHISRData = (sHISRData *) pParam;

	if (pHISRData == NULL)
		return 0;

	while (1) {
		if (down_trylock(&(pHISRData->hCancelSemaphore)) == 0)
			return 0;
		if (down_interruptible(&(pHISRData->hDataSemaphore))) {
			/* Waiting is aborted (by destroy telephony)
			 * Exit the thread */
			return -1;
		}
		if (down_trylock(&(pHISRData->hCancelSemaphore)) == 0)
			return 0;

		atomic_sub_return(1, &(pHISRData->count));

		/* data is recieved */
		if (pHISRData->sisr_routine)
			(pHISRData->sisr_routine) ();
	}
	return 1;
}

int IsrCreate(void **pRef,	/*  [OT]    Reference. */
	      sisrRoutine pParams	/*  [IN]    Input Parameters (see datatype for details). */
    )
{
	sHISRData *pHISRData = NULL;
	char chHisrName[] = { "HISR0" };
	static unsigned char iHisrNumber;

	chHisrName[4] = '0' + iHisrNumber++;

	if (pParams == NULL)
		return -1;

	pHISRData = kmalloc(sizeof(*pHISRData), GFP_KERNEL);
	if (pHISRData == NULL)
		return -1;

	pHISRData->sisr_routine = pParams;

	sema_init(&(pHISRData->hCancelSemaphore), 0);
	sema_init(&(pHISRData->hDataSemaphore), 0);

	atomic_set(&(pHISRData->count), 0);

	pHISRData->hHISRThread = kthread_run(DataThread, (void *)pHISRData, chHisrName);
	if (IS_ERR(pHISRData->hHISRThread)) {
		/* Do error code translation to generic OS errors */
		ASSERT(FALSE);
		return -1;
	}

	*pRef = pHISRData;

	return 0;
}

/****************************************\
 *  Power related functions
 *
\****************************************/

static void ACIPCDPmResourceControl(unsigned char turnON)
{
#if defined(CONFIG_PXA95x_DVFM)
	PM_DBG_MSG_1P("RC[%d]", turnON);

	if (turnON) {		/*turn ON means disabling low power modes */
		dvfm_disable_lowpower(gACIPCDPmLinux.powerDevice);
	} else {		/*turn OFF means enabling low power modes */
		dvfm_enable_lowpower(gACIPCDPmLinux.powerDevice);
		PM_ENA_CNTR_PPLUS;
	}
#endif
}

static void ACIPCDIdleTimerTimeout(long unsigned int id)
{
	CRITICAL_SECTION_VARIABLE;
	(void)id;		/*unused */
	CRITICAL_SECTION_ENTER;
	PM_DBG_MSG_1P("TMR[%d]", PmDb.TimerTicking);

	if (PmDb.TimerTicking) {	/*make sure this is not a race condition before powering off */
		PM_DBG_MSG("TMR[TO]");
		PmDb.TimerTicking = FALSE;
		ACIPCDPmResourceControl(FALSE);
	} else {		/*cancel the sleep state */
		/*PmDb.CurrentSleepAllow = FALSE; */
		PM_DBG_MSG("TMR[cancel]");
	}
	CRITICAL_SECTION_LEAVE;
}

static void ACIPCDPmIdleTimerStop(void)
{
	PM_DBG_MSG("TMR[stop]");
	PmDb.TimerTicking = FALSE;
	del_timer((struct timer_list *)&PmDb.IdleTimerRef);
}

static void ACIPCDPmIdleTimerStart(void)
{
	PM_DBG_MSG("TMR[start]");
	PmDb.TimerTicking = TRUE;
	mod_timer((struct timer_list *)&PmDb.IdleTimerRef, jiffies + msecs_to_jiffies(PmDb.IdleTimerTimeout));
}

static void ACIPCDInitPm(unsigned char fullInit)
{

	if (fullInit) {		/*power up init */
		CRITICAL_SECTION_GLOBAL_INIT;
		memset((void *)&PmDb, 0, sizeof(PmDb));
		PmDb.IdleTimerTimeout = ACIPCD_IDLE_TIMER_TIMEOUT_IN_MSECS;
		PmDb.CurrentSleepAllow = TRUE;

		setup_timer((struct timer_list *)&PmDb.IdleTimerRef, ACIPCDIdleTimerTimeout, 0);
#ifdef CONFIG_PXA95x_DVFM
		dvfm_register("SHMEM", &gACIPCDPmLinux.powerDevice);
#endif
	} else {		/*silent reset init */
		PmDb.TxWaitAckCounter = 0;
		PmDb.RxWaitRspCounter = 0;
		PmDb.HisrWaitCounter = 0;

		PmDb.CurrentSleepAllow = TRUE;
		PmDb.TimerTicking = FALSE;
		PmDb.IdleTimerTimeout = ACIPCD_IDLE_TIMER_TIMEOUT_IN_MSECS;
	}
	ACIPCD_busyDebugStart();

#if defined(ENABLE_PM_DEBUG_LISTS)
	memset((void *)PmDb.pmDebugList, 0, sizeof(PmDb.pmDebugList));
	PmDb.pmDebugListMisses = 0;
#endif /*ENABLE_PM_DEBUG_LISTS */
}

static void ACIPCDPmResourceHandler(void)
{
	PM_DBG_MSG_1P("RH[Tx,%d]", PmDb.TxWaitAckCounter);
	PM_DBG_MSG_1P("RH[Rx,%d]", PmDb.RxWaitRspCounter);
	PM_DBG_MSG_1P("RH[Hi,%d]", PmDb.HisrWaitCounter);
	PM_DBG_MSG_2P("RH[%d,%d]", PmDb.CurrentSleepAllow, PmDb.TimerTicking);

/*#if defined (ACIPCD_PM_DEBUG)
	printk ("[%d] ACIPCDPmResourceRelease: %s !!!\n",TS,(sleepAllow)?"Sleep":"Wakeup");
#endif*/

	if (PmDb.TxWaitAckCounter || PmDb.RxWaitRspCounter || PmDb.HisrWaitCounter) {	/*sleep not allow - waiting for "something" */
		if (PmDb.CurrentSleepAllow) {	/*state change: sleep allow ==> sleep NOT allow */
			PmDb.CurrentSleepAllow = FALSE;
			ACIPCDPmIdleTimerStop();
			ACIPCDPmResourceControl(TRUE);
		} else {	/*staying at 'sleep not allow' */
			/*do nothing... */
		}
		PM_DIS_CNTR_PPLUS;
	} else {		/*sleep allow - all are zero */
		if (PmDb.CurrentSleepAllow) {	/*staying at 'sleep allow' */
			/*do nothing... */
			PM_DBG_MSG("ERROR: How can this happen?");
		} else {	/*state change: sleep NOT allow ==> sleep allow */
			PmDb.CurrentSleepAllow = TRUE;
			ACIPCDPmIdleTimerStart();
		}
	}

}

static void ACIPCDPmTxSleepControl(ACIPCD_SleepStatesE txSleepAllow)
{
	CRITICAL_SECTION_VARIABLE;

	CRITICAL_SECTION_ENTER;
	PM_DBG_MSG_1P("TxS[%d]", txSleepAllow);

	PmDb.TxWaitAckCounter =
	    (txSleepAllow == ACIPCD_SLEEP_ALLOW) ? (PmDb.TxWaitAckCounter - 1) : (PmDb.TxWaitAckCounter + 1);
	ACIPCDPmResourceHandler();
	CRITICAL_SECTION_LEAVE;

}

/*static  void    ACIPCDPmRxSleepControl(ACIPCD_SleepStatesE rxSleepAllow) */
static void ACIPCDPmRxSleepControl(ACIPCD_SleepStatesE rxSleepAllow, unsigned int ServiceId, void *pData,
				   unsigned int length)
{
	CRITICAL_SECTION_VARIABLE;

	CRITICAL_SECTION_ENTER;
	PM_DBG_MSG_1P("RxS[%d]", rxSleepAllow);

#if defined(ENABLE_PM_DEBUG_LISTS)
	if (rxSleepAllow == ACIPCD_SLEEP_ALLOW) {	/*remove from debug list */
		int i;

		for (i = 0; i < PM_DEBUG_LIST_SIZE; i++) {
			if (PmDb.pmDebugList[i].pData == pData) {
				PmDb.pmDebugList[i].serviceId = 0;
				PmDb.pmDebugList[i].pData = NULL;
				PmDb.pmDebugList[i].length = 0;
				break;
			}
		}
	} else {		/*add to debug list */
		int i;

		/*search for free place */
		for (i = 0; i < PM_DEBUG_LIST_SIZE; i++) {
			if (PmDb.pmDebugList[i].pData == NULL)
				break;
		}
		if (i != PM_DEBUG_LIST_SIZE) {	/*found free place */
			PmDb.pmDebugList[i].serviceId = ServiceId;
			PmDb.pmDebugList[i].pData = pData;
			PmDb.pmDebugList[i].length = length;
		} else {	/*no room to add - just count it */
			PmDb.pmDebugListMisses++;
		}
	}
#endif /*ENABLE_PM_DEBUG_LISTS */
	PmDb.RxWaitRspCounter =
	    (rxSleepAllow == ACIPCD_SLEEP_ALLOW) ? (PmDb.RxWaitRspCounter - 1) : (PmDb.RxWaitRspCounter + 1);
	ACIPCDPmResourceHandler();
	CRITICAL_SECTION_LEAVE;

}

static void ACIPCDPmHisrSleepControl(ACIPCD_SleepStatesE hisrSleepAllow)
{
	CRITICAL_SECTION_VARIABLE;

	CRITICAL_SECTION_ENTER;
	PM_DBG_MSG_1P("His[%d]", hisrSleepAllow);

	PmDb.HisrWaitCounter =
	    (hisrSleepAllow == ACIPCD_SLEEP_ALLOW) ? (PmDb.HisrWaitCounter - 1) : (PmDb.HisrWaitCounter + 1);
	ACIPCDPmResourceHandler();
	CRITICAL_SECTION_LEAVE;

}

#endif /*(FEATURE_SHMEM_PM) */

/******************************************************************************
* Function     :   ACIPCDRpcEnded
*******************************************************************************
*
* Description  :    Set the RPC Ended Event
*
* Parameters   :    ServiceId.
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes           :    None.
******************************************************************************/
static void ACIPCDRpcEnded(ShmemInfoS *pShmemDb, void *pData, ACIPCD_ReturnCodeE rc)
{
	unsigned int i;

	ACIPCD_DatabaseS *pDb = CURRENT_ITEM(pShmemDb, pShmemDb->ACIPCD_pRxEntry->ServiceId);

	SHMEM_DEBUG_LOG2(MDB_ACIPCDRpcEnded, DBG_LOG_FUNC_START, 0);

	if (!pDb || !pDb->Config.RpcTimout)
		return;

	for (i = 0; i < ACIPCD_MAX_RPC; i++)
		if (pShmemDb->ACIPCD_RpcInfo[i].pData == pData)
			break;

	if (i >= ACIPCD_MAX_RPC)
		return;

	pShmemDb->ACIPCD_RpcInfo[i].rc = rc;

	FLAG_SET(&pShmemDb->RpcEndedFlagRef, SERVICE_ID_2_BIT(i), OSA_FLAG_OR);

	SHMEM_DEBUG_LOG2(MDB_ACIPCDRpcEnded, DBG_LOG_FUNC_END, DBG_LOG_FUNC_END);

}

/******************************************************************************
* Function     :   ACIPCDDummyRxInd
*******************************************************************************
*
* Description  :    Used to catch all the an initialized data indications
*                   Notify the other side (the sender) that the Tx Failed.
*
* Parameters   :   Data & Length.
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes           :    None.
******************************************************************************/
static void ACIPCDDummyRxInd(void *pData, unsigned int len)
{
	ACIPCD_DatabaseS *pDb;

	ShmemInfoS *pShmemDb = &sgShmemDb;

	pDb = CURRENT_ITEM(pShmemDb, pShmemDb->ACIPCD_pRxEntry->ServiceId);

	if (pDb) {
		ACIPCDSend(pShmemDb, (ACIPCD_ServiceIdE) pShmemDb->ACIPCD_pRxEntry->ServiceId, pData, TX_FAIL_IND, pDb, 0);	/*  Notify other side that Tx Failed. */
		pDb->Debug.RxFailRsp++;
	}
}

/******************************************************************************
* Function     :   ACIPCDDummyLinkStatusInd
*******************************************************************************
*
* Description  :    Used to catch all the an initialized data indications
*
* Parameters   :    ServiceId.
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes           :    None.
******************************************************************************/
static void ACIPCDDummyLinkStatusInd(unsigned char LinkStatus)
{
	/* remove if 0 when ACIDBG_TRACE_2Pis defined */
#if 0
	ShmemInfoS *pShmemDb = &sgShmemDb;

	ACIDBG_TRACE_2P(1, 1, "ACIPCDDummyLinkStatusInd ServiceId = %d, LinkStatus=%d\n",
			pShmemDb->ACIPCD_pRxEntry->ServiceId, LinkStatus);
#endif
}

/******************************************************************************
* Function     :   ACIPCDDummyTxDoneCnf
*******************************************************************************
*
* Description  :    Used to catch all the an initialized Tx Complete indications
*                   If it is an RPC call set the Event Done Flag.
*
* Parameters   :    ServiceId.
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes           :    None.
******************************************************************************/
static void ACIPCDDummyTxDoneCnf(void *pData)
{
	ShmemInfoS *pShmemDb = &sgShmemDb;

	ACIDBG_TRACE_2P(1, 1, "ACIPCDDummyTxDoneCnf ServiceId = %d, pData=0x%x\n", pShmemDb->ACIPCD_pRxEntry->ServiceId,
			(unsigned int)pData);
	ACIPCDRpcEnded(pShmemDb, pData, ACIPCD_RC_OK);
}

/******************************************************************************
* Function     :   ACIPCDDummyTxFailCnf
*******************************************************************************
*
* Description  :    Used to catch all the an initialized Tx Complete indications
*                   If it is an RPC call set the Event Done Flag.
*
* Parameters   :    ServiceId.
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes           :    None.
******************************************************************************/
static void ACIPCDDummyTxFailCnf(void *pData)
{
	ShmemInfoS *pShmemDb = &sgShmemDb;

	ACIDBG_TRACE_2P(1, 1, "ACIPCDDummyTxFailCnf ServiceId = %d, pData=0x%x\n", pShmemDb->ACIPCD_pRxEntry->ServiceId,
			(unsigned int)pData);
	ACIPCDRpcEnded(pShmemDb, pData, ACIPCD_RC_LINK_DOWN);
}

/******************************************************************************
* Function     :   ACIPCDQueueAdrsIndEvent
*******************************************************************************
*
* Description  :    Queue Address Indication Call Back from ACIPC.
*
* Parameters   :    EventsStatus.
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes           :    None.
******************************************************************************/
static unsigned int ACIPCDQueueAdrsIndSgEvent(unsigned int EventsStatus)
{
	ShmemInfoS *pShmemDb = &sgShmemDb;
	sHISRData *p1_sHISRData = pShmemDb->ACIPCD_QueueAdrsHisrRef;

#if (defined FLAVOR_APP)
	ACIPCDataReadExt(pShmemDb->AcipcType, &pShmemDb->ACIPCD_SharedMemory);
#endif

	atomic_add_return(1, &p1_sHISRData->count);
	up(&p1_sHISRData->hDataSemaphore);

	return 0;
}

static unsigned int ACIPCDQueueAdrsIndMsaEvent(unsigned int EventsStatus)
{
	ShmemInfoS *pShmemDb = &msaShmemDb;
	sHISRData *p1_sHISRData = pShmemDb->ACIPCD_QueueAdrsHisrRef;

#if (defined FLAVOR_APP)
	ACIPCDataReadExt(pShmemDb->AcipcType, &pShmemDb->ACIPCD_SharedMemory);
	/*Workaround for new memory map - convert 0xDxxxxxxx address to 0x4xxxxxxx */
	pShmemDb->ACIPCD_SharedMemory = ADDR_CONVERT_MSA_TO_AP(pShmemDb->ACIPCD_SharedMemory);
	/*pShmemDb->ACIPCD_SharedMemory =  CONVERT_ENDIAN_LONG (pShmemDb->ACIPCD_SharedMemory); */
#endif

	atomic_add_return(1, &p1_sHISRData->count);
	up(&p1_sHISRData->hDataSemaphore);

	return 0;

}

/******************************************************************************
* Function     :   ACIPCDQueueAdrsInd
*******************************************************************************
*
* Description  :    Data Indication HISR.
*
* Parameters   :    None.
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes           :    None.
******************************************************************************/
static void ACIPCDQueueAdrsIndHandler(ShmemInfoS *pShmemDb)
{
	/*
	 * On First Time - Update Database.
	 */
	if (!pShmemDb->ACIPCD_SharedMemory)
		ACIPCEventSetExt(pShmemDb->AcipcType, ACIPC_DATA_Q_ADRS);	/*  Request share memory address. */

	else {
		if (!pShmemDb->ACIPCD_pThisSide) {
			/* map into Linux Kerenl address space */
			pShmemDb->ACIPCD_SharedMemory =
			    (unsigned int)ACIPCD_MAP_COMADDR_TO_APPADDR((void *)pShmemDb->ACIPCD_SharedMemory,
									sizeof(ACIPCD_SharedMemoryS));
			pShmemDb->ACIPCD_pThisSide =
			    (ACIPCD_SharedDataS *) (pShmemDb->ACIPCD_SharedMemory +
						    offsetof(ACIPCD_SharedMemoryS, SideA));
			pShmemDb->ACIPCD_pOtherSide =
			    (ACIPCD_SharedDataS *) (pShmemDb->ACIPCD_SharedMemory +
						    offsetof(ACIPCD_SharedMemoryS, SideB));
		}
#ifndef ACIPCD_NO_IRQ_OPT
		pShmemDb->ACIPCD_pThisSide->InRxProcessing = FALSE;
		pShmemDb->ACIPCD_pThisSide->OptTxPackets = 0;
#endif
		pShmemDb->ACIPCD_pThisSide->NextRead = 0;
		pShmemDb->ACIPCD_pThisSide->NextWrite = 0;
		pShmemDb->ACIPCD_pThisSide->AboveHighWaterMark = 0;
		WRITE_CACHE(pShmemDb->ACIPCD_pThisSide);

		/*com silent reset client recovery indication */
		if (pShmemDb->bLinkDownDONE == TRUE) {
			ACIPCD_DatabaseS *pDb;

			pShmemDb->bLinkDownDONE = FALSE;
			pShmemDb->bLinkDownPartial = FALSE;

/*boaz.sommer.mrvl - prevent 'dead' RPC on silent reset - start */
/* Clear all RPC flags before restoring from silent reset */
			FLAG_SET(&pShmemDb->RpcEndedFlagRef, 0, OSA_FLAG_AND);
/*boaz.sommer.mrvl - prevent 'dead' RPC on silent reset - end */

			for (pDb = FIRST_ITEM(pShmemDb); pDb < LAST_ITEM(pShmemDb); pDb++) {
				if (pDb->LinkState == E_CONNECTED) {
					pDb->CallBackFunc.LinkStatusIndCB(TRUE);
				}
			}
		}
	}
}

static void ACIPCDQueueAdrsIndSgHandler(void)
{
/* This function is an indication from Seagull-ACIPC Driver
 * pShmemDb points to Seagull SHMEM Database structure.
 */
	ShmemInfoS *pShmemDb = &sgShmemDb;
	ACIPCDQueueAdrsIndHandler(pShmemDb);

	RPCPhase2Init();
}

static void ACIPCDQueueAdrsIndMsaHandler(void)
{
/* This function is an indication from MSA-ACIPC Driver
 * pShmemDb points to MSA SHMEM Database structure.
 */
	ShmemInfoS *pShmemDb = &msaShmemDb;
	ACIPCDQueueAdrsIndHandler(pShmemDb);
}

/******************************************************************************
* Function     :   ACIPCDHandleWatermarks
*******************************************************************************
*
* Description  :    Check all watermark handling options (High & Low) and handle them
*
* Parameters   :    None.
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes        :    None.
******************************************************************************/
void ACIPCDHandleWatermarks(ShmemInfoS *pShmemDb, unsigned int serviceIdFlags)
{
	ACIPCD_DatabaseS *pTempDb;
	unsigned int ServiceId;

	unsigned char otherSideNextRead;

	SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, DBG_LOG_FUNC_START, 0);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, 1, serviceIdFlags);

	serviceIdFlags >>= 1;	/* first service entry is reserved, so skip it */

	pShmemDb->ACIPCD_pThisSide->AboveHighWaterMark = TRUE;	/*assume this is true */
	otherSideNextRead = pShmemDb->ACIPCD_pOtherSide->NextRead;
	pShmemDb->ACIPCD_pThisSide->AboveHighWaterMark =
	    ABOVE_HIGH_WATER_MARK(otherSideNextRead, pShmemDb->ACIPCD_pThisSide->NextWrite, GLOBAL_HIGH_WATER_MARK);

	for (ServiceId = 1; (serviceIdFlags != 0) && (ServiceId < pShmemDb->MaxServices);
	     ServiceId++, serviceIdFlags >>= 1) {
		SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, 0x10, ServiceId);
		SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, 0x11, serviceIdFlags);
		if (serviceIdFlags & 1) {
			pTempDb = &pShmemDb->ACIPCD_Database[ServiceId];

			pTempDb->WmHandlerActivated = FALSE;

			SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, 0x31, ServiceId);

			/* - Check state of service WM - start */
			if (!pTempDb->AboveHighWm) {	/*in HWM - check for low */

				SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, 0x50, pTempDb->AboveHighWm);
				SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, 0x51, pTempDb->Ack);
				SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, 0x52, pTempDb->Send);
				SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, 0x53, pTempDb->HighWm);
				if (ABOVE_HIGH_WATER_MARK(pTempDb->Ack, pTempDb->Send, pTempDb->HighWm)) {
					pTempDb->AboveHighWm = TRUE;
					pTempDb->Debug.HighWmCnt++;
					pTempDb->CallBackFunc.WatermarkIndCB(ACIPCD_WATERMARK_HIGH);
					SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, 0x59, pTempDb->AboveHighWm);
				}
			}
			if (pTempDb->AboveHighWm) {	/*in HWM - check for low */

				SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, 0x40, pTempDb->AboveHighWm);
				SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, 0x41, pTempDb->Ack);
				SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, 0x42, pTempDb->Send);
				SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, 0x43, pTempDb->LowWm);
				if (BELOW_LOW_WATER_MARK(pTempDb->Ack, pTempDb->Send, pTempDb->LowWm)) {
					pTempDb->AboveHighWm = FALSE;
					pTempDb->Debug.LowWmCnt++;
					pTempDb->CallBackFunc.WatermarkIndCB(ACIPCD_WATERMARK_LOW);
					SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, 0x49, pTempDb->AboveHighWm);
				}
			}

			if (pShmemDb->ACIPCD_pThisSide->AboveHighWaterMark) {
				if (pTempDb->AboveHighWm == FALSE) {
					pTempDb->AboveHighWm = TRUE;
					pTempDb->Debug.HighWmCnt++;
					pTempDb->CallBackFunc.WatermarkIndCB(ACIPCD_WATERMARK_HIGH);
					SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, 0x34, 0);
				}
				SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, 0x35, pTempDb->AboveHighWm);
			}
			/* - Check state of service WM - end */
		}
	}
	SHMEM_DEBUG_LOG2(MDB_ACIPCDHandleWatermarks, DBG_LOG_FUNC_END, DBG_LOG_FUNC_END);
}

/******************************************************************************
* Function     :   ACIPCDWmHandler
*******************************************************************************
*
* Description  :    Watermark Handler Task
*
* Parameters   :    None.
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes        :    None.
******************************************************************************/
static void ACIPCDWmHandler(ShmemInfoS *pShmemDb)
{
	OSA_STATUS osaStatus = 0;

	unsigned int Flag = 0;

	do {
		/*BS - Remember to enter OSA_KERNEL osa_flag fix */
		osaStatus =
		    FLAG_WAIT(&pShmemDb->WmFlagRef, WM_IND_FLAG_MASK, OSA_FLAG_OR_CLEAR, &Flag, MAX_SCHEDULE_TIMEOUT);
		ASSERT(osaStatus == OS_SUCCESS);

		ACIPCDHandleWatermarks(pShmemDb, Flag);
		/*BS: Release waiting thread (waiting in ACIPCDTxReq) */
		FLAG_SET(&pShmemDb->WmNotifiedFlagRef, Flag, OSA_FLAG_OR);
	} while (1);

}

static void ACIPCDSgWmHandler(void)
{
	ShmemInfoS *pShmemDb = &sgShmemDb;

	ACIPCDWmHandler(pShmemDb);
}

static void ACIPCDMsaWmHandler(void)
{
	ShmemInfoS *pShmemDb = &msaShmemDb;
	ACIPCDWmHandler(pShmemDb);
}

static void ACIPCDWmInd(ShmemInfoS *pShmemDb, unsigned int serviceId)
{
	OSA_STATUS osaStatus;

	unsigned int serviceIdFlag;

	if (WM_IND_FLAG_MASK == serviceId)
		serviceIdFlag = serviceId;
	else
		serviceIdFlag = (1 << serviceId);

	osaStatus = FLAG_SET(&pShmemDb->WmFlagRef, serviceIdFlag, OSA_FLAG_OR);
	ASSERT(osaStatus == OS_SUCCESS);

}

void ACIPCDDispatchInterfaceEvents(ShmemInfoS *pShmemDb)
{
	ACIPCD_DatabaseS *pDb;

	unsigned int eventFlag;
	unsigned int serviceId;

	INVALIDATE_CACHE(pShmemDb->ACIPCD_pOtherSide);
	eventFlag = pShmemDb->ACIPCD_pThisSide->RxEvent ^ pShmemDb->ACIPCD_pOtherSide->TxEvent;
	pShmemDb->ACIPCD_pThisSide->RxEvent ^= eventFlag;
	WRITE_CACHE(pShmemDb->ACIPCD_pThisSide);

	eventFlag >>= 1;	/*skip over ServiceId = 0 */

	for (serviceId = 1; (serviceId < pShmemDb->MaxServices) && (eventFlag != 0); serviceId++, eventFlag >>= 1) {
		if (eventFlag & 1) {
			pDb = CURRENT_ITEM(pShmemDb, serviceId);
			if (pDb) {
				/*if (pDb->Config.AllowEvents) */
				{
					pDb->CallBackFunc.RxIndCB((void *)0xFFFFFFFF, 0xFFFFFFFF);
				}
			}
		}
	}
}

/******************************************************************************
* Function     :   ACIPCDDispatchEvents
*******************************************************************************
*
* Description  :    Send Service Event to Service callback.
*
* Parameters   :    None.
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes        :    None.
******************************************************************************/
void ACIPCDDispatchEvents(void)
{
	ACIPCDDispatchInterfaceEvents(&sgShmemDb);
	/*ACIPCDDispatchInterfaceEvents(&msaShmemDb); */

}

/******************************************************************************
* Function     :   ACIPCDRxInd
*******************************************************************************
*
* Description  :    Data Indication HISR.
*
* Parameters   :    None.
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes        :    None.
******************************************************************************/
static void ACIPCDRxInd(ShmemInfoS *pShmemDb)
{
	ACIPCD_ServiceIdE ServiceId;

	ACIPCD_DatabaseS *pDb;

	unsigned char NextWrite, NextRead;

	unsigned short Length;

	unsigned int CacheLineAddress;

	void
	*pData, *pShmem;

	unsigned int pDataAsUnsignedLong;

/*BS - limit rx queue read loop before updating ACIPCD_pThisSide->NextRead - start */
	unsigned int numIterationsBeforeUpdatingThisSideNextRead;
/*BS - limit rx queue read loop before updating ACIPCD_pThisSide->NextRead - end */

	sHISRData *p_sHISRData;

#if (defined OSA_WINCE)
	RefT CriticalSectionRef;
#endif

	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, DBG_LOG_FUNC_START, 0);

	if (pShmemDb->ACIPCD_pThisSide == NULL) {
		/*
		 *  CQ00252218 -
		 *  fix race condition leading to wrong values of NextRead and NextWrite
		 *  Don't call QueueAdrsHandler from here - it is called from event.
		 *  Only send request for shared memory
		 */
		if (!pShmemDb->ACIPCD_SharedMemory)
			ACIPCEventSetExt(pShmemDb->AcipcType, ACIPC_DATA_Q_ADRS);	/*  Request share memory address. */

		p_sHISRData = pShmemDb->ACIPCD_DataIndHisrRef;
		atomic_add_return(1, &p_sHISRData->count);
		up(&p_sHISRData->hDataSemaphore);
		/*YANM: Do not call     ACIPCDPmHisrSleepControl(ACIPCD_SLEEP_ALLOW); */
		return;
	}

	/*
	 *   Read events from other core
	 */
	ACIPCDDispatchEvents();

	/*
	 *   Prepare for Read
	 */
	NextRead = pShmemDb->ACIPCD_pThisSide->NextRead;
	INVALIDATE_CACHE(pShmemDb->ACIPCD_pOtherSide);
	NextWrite = pShmemDb->ACIPCD_pOtherSide->NextWrite;

	/*BS - limit rx queue read loop before updating ACIPCD_pThisSide->NextRead - start */
	numIterationsBeforeUpdatingThisSideNextRead = NUM_ALLOWED_ITERATIONS_BETWEEN_NEXT_READ_UPDATE;
	/*BS - limit rx queue read loop before updating ACIPCD_pThisSide->NextRead - end */

#ifndef ACIPCD_NO_IRQ_OPT
	while (!Q_EMPTY(NextRead, NextWrite))	/*  Check if Q is empty. */
#else
	if (!Q_EMPTY(NextRead, NextWrite))	/*  Check if Q is empty. */
#endif
	{
		SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x20, 0);
		SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x21, NextRead);
		SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x22, NextWrite);
		SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x23, pShmemDb->ACIPCD_pOtherSide->NextRead);
		SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x24, pShmemDb->ACIPCD_pThisSide->NextWrite);

		/*
		 * Loop on all waiting data
		 */
		do {
			SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x30, 0);

			/*
			 * Get Rx Entry
			 */
			pShmemDb->ACIPCD_pRxEntry = &pShmemDb->ACIPCD_pOtherSide->SharedMemory[NextRead];
			CacheLineAddress = CACHE_LINE_ADRS(pShmemDb->ACIPCD_pRxEntry);
			INVALIDATE_CACHE(CacheLineAddress);
			ServiceId = (ACIPCD_ServiceIdE) pShmemDb->ACIPCD_pRxEntry->ServiceId;

			if (pShmemDb->EndianType == LITTLE_ENDIAN) {
				ServiceId = CONVERT_ENDIAN_SHORT(ServiceId);
			}
			/* Get actual service ID for specific interface */
			ServiceId = GLOBAL_SID_2_LOCAL_SID(ServiceId, pShmemDb->CoreId);

			if (ServiceId >= MAX_SERVICES_PER_INTERFACE) {
				ASSERT(FALSE);
				continue;	/*Klocwork */
			}

			pDb = CURRENT_ITEM(pShmemDb, ServiceId);

			SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x31, ServiceId);
			SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x32, pDb);

			if (!pDb) {
				NextRead++;
				continue;
			}
			pDb->Debug.Recv++;
			Length = pShmemDb->ACIPCD_pRxEntry->Length;
			pShmem = pShmemDb->ACIPCD_pRxEntry->pData;
			if (pShmemDb->EndianType == LITTLE_ENDIAN) {
				Length = CONVERT_ENDIAN_SHORT(Length);
				pDataAsUnsignedLong = (unsigned int)pShmem;
				pShmem = (void *)CONVERT_ENDIAN_LONG(pDataAsUnsignedLong);
			}

			/*
			 * Link Is Up
			 */
			if (!pShmem) {
				SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x40, pDb->LinkState);

				pDb->Debug.RecvControl++;

				pDb->HighWm = (unsigned char)(Length >> 8);
				pDb->LowWm = (unsigned char)Length;

				if (pDb->LinkState == E_CLOSED) {
					SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x41, 0);
					pDb->LinkState = E_FAR_SIDE_READY;
				} else if (pDb->LinkState == E_NEAR_SIDE_READY) {
					SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x42, 0);
					pDb->LinkState = E_CONNECTED;
					Length = ((unsigned short)pDb->Config.HighWm << 8) | pDb->Config.LowWm;
					ACIPCDSend(pShmemDb, ServiceId, NULL, Length, pDb, 0);	/*  Open the Link. */
					pDb->CallBackFunc.LinkStatusIndCB(TRUE);
					pDb->Debug.SendControl++;
					SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x43, 0);
				}
			}
			/*BS - improved WM handling - start */
			/* If other side sent internal message, it means that this side was in global HWM */
			/* and other side finished reading through shared memory. */
			else if (ServiceId == ACIPCD_INTERNAL) {
				ACIPCDWmInd(pShmemDb, WM_IND_FLAG_MASK);
				pDb->Debug.Recv++;
			}
			/*BS - improved WM handling - end */
			/*
			 * Data Arrived
			 */
			else {
				SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x50, Length);

				SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x51, pShmem);
				/*Workaround for new memory map - convert 0xDxxxxxxx address to 0x4xxxxxxx */
				if (pShmemDb->CoreId == CORE_MSA) {
					pShmem = (void *)ADDR_CONVERT_MSA_TO_AP(pShmem);
				}
#if 0
#if (defined FLAVOR_APP)
				if (pDb->Config.RxAction != ACIPCD_USE_ADDR_AS_PARAM) {
					pShmem = (void *)ACIPCD_COMADDR_TO_APPADDR(pShmem);
				}
				SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x52, pShmem);
#endif
#endif /*if 0 */
				if (Length && Length < TX_FAIL_IND) {
#if (defined FLAVOR_APP)
					if (pDb->Config.RxAction != ACIPCD_USE_ADDR_AS_PARAM) {
						pShmem = (void *)ACIPCD_COMADDR_TO_APPADDR(pShmem);
						SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x52, pShmem);
					}
#endif

					if (pDb->Config.RxAction == ACIPCD_HANDLE_CACHE)
						CacheInvalidateMemory(pShmem, (unsigned int)Length);

					pDb->Debug.RxInd++;

#if defined (FEATURE_SHMEM_PM)
					ACIPCDPmRxSleepControl(ACIPCD_SLEEP_NOT_ALLOW,
							       LOCAL_SID_2_GLOBAL_SID(ServiceId, pShmemDb->CoreId),
							       pShmem, Length);

#endif /*(FEATURE_SHMEM_PM) */

#if 0
					if (ServiceId != ACIPCD_DIAG_DATA)
						sendPacketToACAT(0, ServiceId, pShmem, Length);
#endif

					pDb->CallBackFunc.RxIndCB(pShmem, Length);
					SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x5F, pShmem);
				}

				/*
				 * Handle Ack / Nak
				 */
				else {
					pShmem = (void *)ACIPCD_COMADDR_TO_APPADDR(pShmem);
					SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x60, pShmem);

					pDb->Ack++;
					SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x61, pDb->Ack);

					if (pDb->Config.TxAction == ACIPCD_COPY) {
						ACIPCDRemoveFromAllocList(pShmem);
						pData = (void *)OsaMemGetUserParam(pShmem);	/*  Get the real address. */
						OsaMemFree(MAP_HIDDEN_ADDR_TO(pShmem));	/*  Free the memory that was allocated in ACIPCDTxReq */
					} else
						pData = pShmem;

					if (!Length) {
						SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x70, 0);

						pDb->Debug.TxDoneCnf++;

						if (pDb->Config.TxCnfAction == ACIPCD_TXCNF_FREE_MEM) {
							SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x75, ServiceId);
							OsaMemFree(pData);
							pData = NULL;
						}
						pDb->CallBackFunc.TxDoneCnfCB(pData);	/*  Tx Completed */
					} else {
						SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x80, 0);
						if (pDb->LinkState == E_CONNECTED) {
							pDb->LinkState = E_NEAR_SIDE_READY;
							pDb->CallBackFunc.LinkStatusIndCB(FALSE);
						}

						pDb->Debug.TxFailCnf++;
						pDb->CallBackFunc.TxFailCnfCB(pData);	/*  Tx Failed */
					}
					/*BS - improved WM handling - start */
					/*BS - check if fell below LWM */
					if ((pDb->AboveHighWm)
					    && (BELOW_LOW_WATER_MARK(pDb->Ack, pDb->Send, pDb->LowWm))) {
						SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x85, 0);
						if (!pDb->WmHandlerActivated) {
							pDb->WmHandlerActivated = TRUE;
							ACIPCDWmInd(pShmemDb, ServiceId);
						}
					}
					/*BS - improved WM handling - end */
#if defined (FEATURE_SHMEM_PM)
					ACIPCDPmTxSleepControl(ACIPCD_SLEEP_ALLOW);
#endif
				}
			}
			NextRead++;

			/*BS - limit rx queue read loop before updating ACIPCD_pThisSide->NextRead - start */
			if (--numIterationsBeforeUpdatingThisSideNextRead == 0) {
				pShmemDb->ACIPCD_pThisSide->NextRead = NextRead;
				WRITE_CACHE(ACIPCD_pThisSide);
				numIterationsBeforeUpdatingThisSideNextRead =
				    NUM_ALLOWED_ITERATIONS_BETWEEN_NEXT_READ_UPDATE;
			}
			/*BS - limit rx queue read loop before updating ACIPCD_pThisSide->NextRead - end */

		} while (!Q_EMPTY(NextRead, NextWrite));	/*  Loop on all pending data. */

		SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0x90, 0);

#ifndef ACIPCD_NO_IRQ_OPT
		pShmemDb->ACIPCD_pThisSide->InRxProcessing = FALSE;
#endif
		pShmemDb->ACIPCD_pThisSide->NextRead = NextRead;
		WRITE_CACHE(pShmemDb->ACIPCD_pThisSide);

		INVALIDATE_CACHE(pShmemDb->ACIPCD_pOtherSide);
		NextWrite = pShmemDb->ACIPCD_pOtherSide->NextWrite;
	}

	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0xA0, pShmemDb->ACIPCD_pOtherSide->AboveHighWaterMark);

	/*
	 * If needed - Let other side know that we are now below the Low Water Mark.
	 */
	if (pShmemDb->ACIPCD_pOtherSide->AboveHighWaterMark /*&&  BELOW_LOW_WATER_MARK(NextRead,NextWrite,LOW_WATER_MARK)*/) {
		unsigned char param = 0;
		ACIPCDTxReq(LOCAL_SID_2_GLOBAL_SID(ACIPCD_INTERNAL, pShmemDb->CoreId), (void *)&param, 1);	/*  Tell other side it should check for global HWM. */
		/*ACIPCEventSet( ACIPC_DATA_IND ) ;   //  Tell other side to check the shared memory. */
		SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0xA1, pShmemDb->CoreId);
	}
#ifndef ACIPCD_NO_IRQ_OPT
	if (pShmemDb->ACIPCD_pThisSide->InRxProcessing) {	/*  This can happen when we enter this function but the Q was already empty. */
		pShmemDb->ACIPCD_pThisSide->InRxProcessing = FALSE;
		WRITE_CACHE(pShmemDb->ACIPCD_pThisSide);
		SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0xA2, 0);
	}
#endif

	/*
	 *   Read events from other core
	 *   We call this method here again
	 *   in case other side didn't set an event
	 *   because of ACIPCD_pThisSide->InRxProcessing read race condition
	 */
	ACIPCDDispatchEvents();

#if defined (FEATURE_SHMEM_PM)
	ACIPCDPmHisrSleepControl(ACIPCD_SLEEP_ALLOW);
#endif /*(FEATURE_SHMEM_PM) */

/* Tested only on WinCE platform yet */
#if (defined OSA_WINCE)
	CriticalSectionRef = OsaCriticalSectionEnter(ACIPCD_CriticalSectionRef, NULL);
#endif
	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, 0xB0, 0);

#if (defined OSA_WINCE)
	OsaCriticalSectionExit(CriticalSectionRef, NULL);
#endif

	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxInd, DBG_LOG_FUNC_END, 0);

}

/******************************************************************************
* Function     :   ACIPCDRxIndSgHandler
*******************************************************************************
*
* Description  :    Seagull Data Indication Handler.
*
* Parameters   :    None.
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes        :    None.
******************************************************************************/
static void ACIPCDRxIndSgHandler(void)
{
	ShmemInfoS *pShmemDb = &sgShmemDb;

	ACIPCDRxInd(pShmemDb);
}

/******************************************************************************
* Function     :   ACIPCDRxIndMsaHandler
*******************************************************************************
*
* Description  :    MSA Data Indication Handler.
*
* Parameters   :    None.
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes        :    None.
******************************************************************************/
static void ACIPCDRxIndMsaHandler(void)
{
	ShmemInfoS *pShmemDb = &msaShmemDb;

	ACIPCDRxInd(pShmemDb);
}

/******************************************************************************
* Function     :   ACIPCDCheckForRxInd
*******************************************************************************
*
* Description  :    Poll on the Data Indication HISR.
*
* Parameters   :    None.
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes           :    None.
******************************************************************************/
void ACIPCDCheckForRxInd(void)
{
	ShmemInfoS *pShmemDb = &sgShmemDb;
	SHMEM_DEBUG_LOG2(MDB_ACIPCDCheckForRxInd, DBG_LOG_FUNC_START, 0);
	ACIPCDRxInd(pShmemDb);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDCheckForRxInd, DBG_LOG_FUNC_END, 0);
}

/******************************************************************************
* Function     :   ACIPCDRxIndEvent
*******************************************************************************
*
* Description  :    Data Indication Call Back from ACIPC.
*
* Parameters   :    EventsStatus.
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes           :    None.
******************************************************************************/
static unsigned int ACIPCDRxIndEvent(ShmemInfoS *pShmemDb, unsigned int EventsStatus)
{
	sHISRData *p1_sHISRData;

	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxIndEvent, DBG_LOG_FUNC_START, EventsStatus);

#if !(defined OSA_WINCE)
#ifndef ACIPCD_NO_IRQ_OPT
	/* in WinCE case kerenel already sets this value */
	if (pShmemDb->ACIPCD_pThisSide) {
		pShmemDb->ACIPCD_pThisSide->InRxProcessing = TRUE;
		WRITE_CACHE(pShmemDb->ACIPCD_pThisSide);
	}
#endif
#endif

#if (defined FLAVOR_APP)
	if (!pShmemDb->ACIPCD_SharedMemory)
		ACIPCEventSetExt(pShmemDb->AcipcType, ACIPC_DATA_Q_ADRS);	/*  Request share memory address. */
	/*else   //YG - maybe need to be real else and not in comment */
	else
#endif
	{
#if defined (FEATURE_SHMEM_PM)
		ACIPCDPmHisrSleepControl(ACIPCD_SLEEP_NOT_ALLOW);
#endif /*(FEATURE_SHMEM_PM) */

#if (defined OSA_WINCE)
		ACIPCDRxInd(pShmemDb);
#else
		p1_sHISRData = pShmemDb->ACIPCD_DataIndHisrRef;
		atomic_add_return(1, &p1_sHISRData->count);
		up(&p1_sHISRData->hDataSemaphore);
#endif
	}

	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxIndEvent, DBG_LOG_FUNC_END, 0);

	return 0;

}

static unsigned int ACIPCDRxIndMsaEvent(unsigned int EventsStatus)
{
	ShmemInfoS *pShmemDb = &msaShmemDb;
	return ACIPCDRxIndEvent(pShmemDb, EventsStatus);
}

static unsigned int ACIPCDRxIndSgEvent(unsigned int EventsStatus)
{
	ShmemInfoS *pShmemDb = &sgShmemDb;

	return ACIPCDRxIndEvent(pShmemDb, EventsStatus);

}

/******************************************************************************
* Function     :   ACIPCDLinkStatus
*******************************************************************************
*
* Description  :    Returns the status of the link.
*
* Parameters   :    ServiceId
*
* Output Param :    None.
*
* Return value :    TRUE=Up, FALSE=Down.
*
* Notes           :    None.
******************************************************************************/
unsigned char ACIPCDLinkStatus(ACIPCD_ServiceIdE ServiceId)
{
	ACIPCD_DatabaseS *pDb;
	ShmemInfoS *pShmemDb = &sgShmemDb;

	if (ServiceId >= MAX_SG_SERVICES) {
		pShmemDb = &msaShmemDb;
		ServiceId -= MAX_SERVICES_PER_INTERFACE;
	}
	pDb = CURRENT_ITEM(pShmemDb, ServiceId);

	if (!pDb)
		return FALSE;

	return (unsigned char)(pDb->LinkState == E_CONNECTED);
}

/******************************************************************************
* Function     :   ACIPCDSend
*******************************************************************************
*
* Description  :    Send Data by SHMEM updating
*                   finally makes interrupt ACIPCEventSet(ACIPC_DATA_IND)
*                    or skips it upon parameter dontMakeACIPCirq
*
******************************************************************************/
static ACIPCD_ReturnCodeE ACIPCDSend(ShmemInfoS *pShmemDb,
				     ACIPCD_ServiceIdE ServiceId, void *pData,
				     unsigned short Length, ACIPCD_DatabaseS *pDb, unsigned char dontMakeACIPCirq)
{
	OsaRefT CriticalSectionRef;

	unsigned int CacheLineAddress;

	unsigned char NextWrite, NextRead;

	unsigned int pDataAsLongInt;

	SHMEM_DEBUG_LOG2(MDB_ACIPCDSend, DBG_LOG_FUNC_START, ServiceId);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDSend, 1, Length);
/*
 * At this point, a message is going to be sent to the other core.
 * Service ID number is zero-based, so we need to make it
 * global again.
 * Additionally, we need to make sure the service ID and length values
 * are in the correct format - big or little endian, based on what the
 * other core supports.
 */
	ServiceId = LOCAL_SID_2_GLOBAL_SID(ServiceId, pShmemDb->CoreId);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDSend, 4, pShmemDb->CoreId);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDSend, 5, ServiceId);
/*
 * Check if the Shared Memory is full.
 */
	CriticalSectionRef = OsaCriticalSectionEnter(pShmemDb->ACIPCD_CriticalSectionRef, NULL);
	INVALIDATE_CACHE(pShmemDb->ACIPCD_pOtherSide);
	NextRead = pShmemDb->ACIPCD_pOtherSide->NextRead;
	NextWrite = pShmemDb->ACIPCD_pThisSide->NextWrite;
	SHMEM_DEBUG_LOG2(MDB_ACIPCDSend, 0x10, NextRead);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDSend, 0x11, NextWrite);

	if (Q_FULL(NextRead, NextWrite)) {

		OsaCriticalSectionExit(CriticalSectionRef, NULL);

		ACIDBG_TRACE_3P(1, 1, "ACIPCDSend: ERROR Q_FULL, NextRead=%d, NextWrite=%d, ServiceId=%d", NextRead,
				NextWrite, ServiceId);
		SHMEM_DEBUG_LOG2(MDB_ACIPCDSend, DBG_LOG_FUNC_END_ERROR, ServiceId);

		return ACIPCD_RC_Q_FULL;
	}
/*
 * Write Data to the Shared Memory.
 */

	if (pShmemDb->EndianType == LITTLE_ENDIAN) {
		ServiceId = CONVERT_ENDIAN_SHORT(ServiceId);
		Length = CONVERT_ENDIAN_SHORT(Length);
		pDataAsLongInt = (unsigned int)pData;
		pData = (void *)CONVERT_ENDIAN_LONG(pDataAsLongInt);
	}
	pShmemDb->ACIPCD_pThisSide->SharedMemory[NextWrite].ServiceId = ServiceId;
	pShmemDb->ACIPCD_pThisSide->SharedMemory[NextWrite].Length = Length;

#if (defined FLAVOR_APP)
	pData = (void *)ACIPCD_APPADDR_TO_COMADDR(pData);
#if 0
	if (pDb->Config.RxAction != ACIPCD_USE_ADDR_AS_PARAM) {
		pData = (void *)ACIPCD_APPADDR_TO_COMADDR(pData);
	}
#endif
#endif

	if (pShmemDb->CoreId == CORE_MSA) {
		/*Workaround for new memory map - convert 0x4xxxxxxx address to 0xDxxxxxxx */
		pData = (void *)ADDR_CONVERT_AP_TO_MSA(pData);
	}
	pShmemDb->ACIPCD_pThisSide->SharedMemory[NextWrite].pData = pData;

	CacheLineAddress = CACHE_LINE_ADRS(&pShmemDb->ACIPCD_pThisSide->SharedMemory[NextWrite]);

	WRITE_CACHE(CacheLineAddress);

	pShmemDb->ACIPCD_pThisSide->NextWrite = ++NextWrite;
	WRITE_CACHE(pShmemDb->ACIPCD_pThisSide);

	OsaCriticalSectionExit(CriticalSectionRef, NULL);

	pDb->Debug.Send++;

#ifndef ACIPCD_NO_IRQ_OPT
	INVALIDATE_CACHE(pShmemDb->ACIPCD_pOtherSide);
	if (pShmemDb->ACIPCD_pOtherSide->InRxProcessing == TRUE) {
		pShmemDb->ACIPCD_pThisSide->OptTxPackets++;
	} else {
		if (!dontMakeACIPCirq) {
			/*
			 * BS: For power purposes, need to make sure COMM is running
			 * before sending message to MSA
			 */
			if (pShmemDb->AcipcType == ACMIPC)
				ACIPCEventSetExt(ACSIPC, ACIPC_DATA_IND);
			ACIPCEventSetExt(pShmemDb->AcipcType, ACIPC_DATA_IND);	/*  Tell other side to check the shared memory. */
		}
	}
#else
	ACIPCEventSetExt(pShmemDb->AcipcType, ACIPC_DATA_IND);	/*  Tell other side to check the shared memory. */
#endif

	SHMEM_DEBUG_LOG2(MDB_ACIPCDSend, DBG_LOG_FUNC_END, 0);

	return ACIPCD_RC_OK;
}

/******************************************************************************
* Function     :   ACIPCDRxDoneRsp
*******************************************************************************
*
* Description  :    Rx Processing is DONE (Other side Tx ended, other side can use pData.)
*
* Parameters   :    None
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes        :    None.
******************************************************************************/
ACIPCD_ReturnCodeE ACIPCDRxDoneRspACIPC(ACIPCD_ServiceIdE ServiceId, void *pData, unsigned char dontMakeACIPCirq)
{
	ACIPCD_ReturnCodeE rc;

	ACIPCD_DatabaseS *pDb;
#if (defined OSA_WINCE)
	OsaRefT CriticalSectionRef;
#endif
	/* this is done in order to solve the bug where: ACIPCD_pThisSide->NextWrite was copied to the CPU register (for the macro),
	   then a context switch happened and RxDoneRsp (or Tx) was sent, so the other side read the message and updated
	   his NextRead, and then ACIPCD_pOtherSide->NextRead is copied to CPU register and looked like a High-Watermark happned */
	unsigned char otherSideNextRead;	/* = ACIPCD_pOtherSide->NextRead; */
	unsigned char aboveHighWaterMark;
	ShmemInfoS *pShmemDb;
	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxDoneRsp, DBG_LOG_FUNC_START, ServiceId);

	/* Point to Shmem DB */
	pShmemDb = SHMEM_DB_FROM_SERVICE_ID(ServiceId);
	ASSERT(pShmemDb != NULL);

	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxDoneRsp, 2, (unsigned int)pShmemDb);

	/* Get Zero based Service Id */
	ServiceId = GLOBAL_SID_2_LOCAL_SID(ServiceId, pShmemDb->CoreId);

	if (ServiceId >= MAX_SERVICES_PER_INTERFACE) {
		ASSERT(FALSE);
		return ACIPCD_RC_ILLEGAL_PARAM;	/*Klocwork */
	}

	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxDoneRsp, 3, pShmemDb->CoreId);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxDoneRsp, 4, ServiceId);

	pDb = CURRENT_ITEM(pShmemDb, ServiceId);

/*
 *  Check if OK to Send.
 */
	if (!pDb || !pData) {
		ASSERT(FALSE);
		return ACIPCD_RC_ILLEGAL_PARAM;
	}
	if (pDb->LinkState != E_CONNECTED) {
		pr_err
		    ("\nSHMEM: ACIPCDRxDoneRspACIPC cancelled sending RxDoneRsp due to ServiceId=%d is in LinkState=%d\n\n",
		     ServiceId, pDb->LinkState);
		return ACIPCD_RC_LINK_DOWN;
	}
/*
 * Write Data to the Shared Memory.
 */
	rc = ACIPCDSend(pShmemDb, ServiceId, pData, 0, pDb, dontMakeACIPCirq);

	if (rc) {
		ASSERT(FALSE);
#if defined (FEATURE_SHMEM_PM)
		ACIPCDPmRxSleepControl(ACIPCD_SLEEP_ALLOW, LOCAL_SID_2_GLOBAL_SID(ServiceId, pShmemDb->CoreId), pData,
				       0);

#endif /*(FEATURE_SHMEM_PM) */
		return rc;
	}

	pDb->Debug.RxDoneRsp++;

#if defined (FEATURE_SHMEM_PM)
/*    ACIPCDPmRxSleepControl(ACIPCD_SLEEP_ALLOW); - handle ABOVE_HIGH_WATER_MARK first */
#endif /*(FEATURE_SHMEM_PM) */

	otherSideNextRead = pShmemDb->ACIPCD_pOtherSide->NextRead;

/*
 * Check if above the High Water Mark and return.
 */

	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxDoneRsp, 0x10, gACIPCDRxIndHisrKick);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxDoneRsp, 0x11, pShmemDb->ACIPCD_pThisSide->NextWrite);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxDoneRsp, 0x12, otherSideNextRead);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxDoneRsp, 0x13, pDb->Send);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxDoneRsp, 0x14, pDb->Ack);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxDoneRsp, 0x15, pDb->HighWm);

/*BS - improved WM handling - start */
	aboveHighWaterMark =
	    ABOVE_HIGH_WATER_MARK(otherSideNextRead, pShmemDb->ACIPCD_pThisSide->NextWrite, GLOBAL_HIGH_WATER_MARK);
	if (aboveHighWaterMark) {
		/*Activate WM handler to check if HWM is still on for ServiceId */
		ACIPCDWmInd(pShmemDb, ServiceId);
	}
/*BS - improved WM handling - end */

#if defined (FEATURE_SHMEM_PM)
	ACIPCDPmRxSleepControl(ACIPCD_SLEEP_ALLOW, LOCAL_SID_2_GLOBAL_SID(ServiceId, pShmemDb->CoreId), pData, 0);
#endif
	SHMEM_DEBUG_LOG2(MDB_ACIPCDRxDoneRsp, DBG_LOG_FUNC_END, ServiceId);

	return ACIPCD_RC_OK;
}

ACIPCD_ReturnCodeE ACIPCDRxDoneRsp(ACIPCD_ServiceIdE ServiceId, void *pData)
{
	return ACIPCDRxDoneRspACIPC(ServiceId, pData, 0);
}

/******************************************************************************
* Function     :   ACIPCDTxReq
*******************************************************************************
*
* Description  :    Send Data.
*
* Parameters   :    None
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes           :    None.
******************************************************************************/
ACIPCD_ReturnCodeE ACIPCDTxReqACIPC(ACIPCD_ServiceIdE ServiceId, void *pData, unsigned short Length,
				    unsigned char dontMakeACIPCirq)
{
	OsaRefT CriticalSectionRef;

	ACIPCD_ReturnCodeE rc;

	ACIPCD_DatabaseS *pDb;

	OSA_STATUS osaStatus;

	unsigned int Flag, i = 0;	/*  Avoid Warning. */
	/* this is done in order to solve the bug where: ACIPCD_pThisSide->NextWrite was copied to the CPU register (for the macro),
	   then a context switch happened and RxDoneRsp (or Tx) was sent, so the other side read the message and updated
	   his NextRead, and then ACIPCD_pOtherSide->NextRead is copied to CPU register and looked like a High-Watermark happned */
	unsigned char otherSideNextRead;	/* = ACIPCD_pOtherSide->NextRead; */

	unsigned char aboveHighWaterMark;
	ShmemInfoS *pShmemDb;
	SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, DBG_LOG_FUNC_START, ServiceId);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 1, Length);

	/* Point to Shmem DB */
	pShmemDb = SHMEM_DB_FROM_SERVICE_ID(ServiceId);
	ASSERT(pShmemDb != NULL);

	SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 3, pShmemDb);
	/* Get Zero based Service Id */
	ServiceId = GLOBAL_SID_2_LOCAL_SID(ServiceId, pShmemDb->CoreId);
	if (ServiceId >= MAX_SERVICES_PER_INTERFACE) {
		ASSERT(FALSE);
		return ACIPCD_RC_ILLEGAL_PARAM;	/*Klocwork */
	}

	SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 4, pShmemDb->CoreId);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 5, ServiceId);

	pDb = CURRENT_ITEM(pShmemDb, ServiceId);

/*
 *  Check if OK to Send.
 */
	if (!pDb || !pData || !Length) {
		ASSERT(0);
		return ACIPCD_RC_ILLEGAL_PARAM;
	}

	if (pDb->LinkState != E_CONNECTED)
		return ACIPCD_RC_LINK_DOWN;

#if 0
	/*for debug purposes only */
	if (ServiceId == ACIPCD_CI_DATA) {
		static int cnt;

		/*if((++cnt % 100) == 0)
		   ACI_PRINT("ACIPCDTxReq: ACIPCD_CI_DATA, len = %d\n", Length); */
		if ((++cnt % 1000) == 0) {
			ACI_PRINT("Pkt, ");
		}

	}
#endif /*for debug */
/*
 * If RPC - Get an Event Flag
 */
	if (pDb->Config.RpcTimout) {
		while (TRUE) {
			CriticalSectionRef = OsaCriticalSectionEnter(pShmemDb->ACIPCD_CriticalSectionRef, NULL);

			for (i = 0; i < ACIPCD_MAX_RPC; i++)
				if (!pShmemDb->ACIPCD_RpcInfo[i].pData)
					break;

			if (i < ACIPCD_MAX_RPC) {
				pShmemDb->ACIPCD_RpcInfo[i].pData = pData;
				OsaCriticalSectionExit(CriticalSectionRef, NULL);
				break;
			}

			OsaCriticalSectionExit(CriticalSectionRef, NULL);
			/*  if we got here it meens that there are 32 tasks waiting for a RPC response. */
			osaStatus =
			    FLAG_WAIT(&pShmemDb->RpcEndedFlagRef, ~0, OSA_FLAG_OR_CLEAR, &Flag,
				      msecs_to_jiffies(pDb->Config.RpcTimout));
			if (osaStatus != OS_SUCCESS)
				return ACIPCD_RC_ILLEGAL_PARAM;

/*boaz.sommer.mrvl - prevent 'dead' RPC on silent reset - start */

			if (pShmemDb->bLinkDownDONE) {
				pr_err("ACIPCDTxReq(Rpc Send): COMM in reset\n");
				return ACIPCD_RC_LINK_DOWN;
			}
/*boaz.sommer.mrvl - prevent 'dead' RPC on silent reset - end */

		}

		pShmemDb->ACIPCD_RpcInfo[i].rc = ACIPCD_RC_OK;
	}

	SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x10, ServiceId);
	/*PM_DBG_MSG_2P("TxBS[%x,%d]", pData, Length); */

	/*
	 * Write Data to the Shared Memory.
	 */
	if (pDb->Config.TxAction == ACIPCD_HANDLE_CACHE)
		CacheCleanAndInvalidateMemory(pData, (unsigned int)Length);
	else if (pDb->Config.TxAction == ACIPCD_COPY) {
		unsigned int len = (unsigned int)(Length + CACHE_LINE_MASK) & ~CACHE_LINE_MASK;	/*  Round up to a full cache line. */

		void		/*  Alloc memory from the shared memory. */
		*pShmem = MAP_OSA_HIDDEN_ADDR(OsaMemAlloc(pShmemDb->ACIPCD_ShmemRef, len));

		if (!pShmem)
			return ACIPCD_RC_NO_MEMORY;

		/*
		 * The following array is for saving memory pointers that were
		 * allocated by SHMEM. This array is used in silent reset to
		 * release all allocated memory
		 */
		ACIPCDAddToAllocList(pShmem);

		OsaMemSetUserParam(pShmem, (unsigned int)pData);
		memcpy(pShmem, pData, (unsigned int)Length);	/*  Copy to shmem. */
		CacheCleanAndInvalidateMemory(pShmem, len);
		pData = pShmem;
	}
#if defined (FEATURE_SHMEM_PM)
	if (ServiceId != ACIPCD_INTERNAL)
		ACIPCDPmTxSleepControl(ACIPCD_SLEEP_NOT_ALLOW);
#endif

#if 0
	if (ServiceId != ACIPCD_DIAG_DATA)
		sendPacketToACAT(1, ServiceId, pData, Length);
#endif

	pDb->Debug.TxReq++;
	pDb->Send++;
	rc = ACIPCDSend(pShmemDb, ServiceId, pData, Length, pDb, dontMakeACIPCirq);

	if (rc != ACIPCD_RC_OK) {
		ASSERT(FALSE);
#if defined (FEATURE_SHMEM_PM)
		/*BS: PM - Tx did not take place, so no TxDoneCnf. Signal sleep allowed here. */
		/*ASSERT! ACIPCDPmTxSleepControl(ACIPCD_SLEEP_ALLOW); */
#endif /*(FEATURE_SHMEM_PM) */
	}

	if (rc)
		return rc;

/*
 * If RPC - Wait on the Event Flag
 */
	if (pDb->Config.RpcTimout) {
		osaStatus =
		    FLAG_WAIT(&pShmemDb->RpcEndedFlagRef, 1 << i, OSA_FLAG_OR_CLEAR, &Flag,
			      msecs_to_jiffies(pDb->Config.RpcTimout));

/*boaz.sommer.mrvl - prevent 'dead' RPC on silent reset - start */
		if (pShmemDb->bLinkDownDONE) {
			pr_err("ACIPCDTxReq(Rpc WaitAck): COMM in reset\n");
			return ACIPCD_RC_LINK_DOWN;
		}

		if (osaStatus == OS_SUCCESS) {
			if (pDb->Config.TxAction == ACIPCD_HANDLE_CACHE)
				CacheInvalidateMemory(pData, (unsigned int)Length);

			else if (pDb->Config.TxAction == ACIPCD_COPY) {
				void
				*pShmem = pData;

				pData = (void *)OsaMemGetUserParam(pShmem);
				memcpy(pData, pShmem, (unsigned int)Length);	/*  Copy from shmem. */
			}
			rc = pShmemDb->ACIPCD_RpcInfo[i].rc;
		} else if (osaStatus == OS_TIMEOUT) {
			rc = ACIPCD_RC_LINK_DOWN;
		} else {
			ASSERT(osaStatus == OS_SUCCESS);
		}

		pShmemDb->ACIPCD_RpcInfo[i].pData = NULL;

		if (rc)
			return rc;
	}

	SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x20, gACIPCDRxIndHisrKick);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x21, pShmemDb->ACIPCD_pOtherSide->AboveHighWaterMark);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x22, pShmemDb->ACIPCD_pThisSide->AboveHighWaterMark);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x30, ServiceId);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x31, pShmemDb->ACIPCD_pThisSide->NextWrite);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x32, pShmemDb->ACIPCD_pOtherSide->NextRead);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x33, pDb->Send);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x34, pDb->Ack);
	SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x35, pDb->HighWm);
	/*SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq , 0x21, ((OS_Task *)ACIPCD_DataIndHisrRef)->refCheck); */
	/*SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq , 0x22, ((OS_Task *)ACIPCD_DataIndHisrRef)->tlsPtr); */
/*
 * Check if above the High Water Mark and return.
 */

	/*BS - improved WM handling - start */
	otherSideNextRead = pShmemDb->ACIPCD_pOtherSide->NextRead;
	aboveHighWaterMark =
	    ABOVE_HIGH_WATER_MARK(otherSideNextRead, pShmemDb->ACIPCD_pThisSide->NextWrite, GLOBAL_HIGH_WATER_MARK);
	if ((ABOVE_HIGH_WATER_MARK(pDb->Ack, pDb->Send, pDb->HighWm)) || (aboveHighWaterMark)) {
		pDb->WmHandlerActivated = TRUE;
		ACIPCDWmInd(pShmemDb, ServiceId);
		if (!pDb->WmHandlerActivated) {
			SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x41,
					 ABOVE_HIGH_WATER_MARK(pDb->Ack, pDb->Send, pDb->HighWm));
			SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x4F, ServiceId);
			ACIDBG_TRACE_3P(1, 3, "ACIPCDTxReq: High WaterMark occurred (g=%d, s=%d), ServiceId = %d",
					ABOVE_HIGH_WATER_MARK(otherSideNextRead, pShmemDb->ACIPCD_pThisSide->NextWrite,
							      GLOBAL_HIGH_WATER_MARK), ABOVE_HIGH_WATER_MARK(pDb->Ack,
													     pDb->Send,
													     pDb->HighWm),
					ServiceId);

			return ACIPCD_RC_HIGH_WM;
		} else {
			/*Heavy loading requires additional HWM_POLLING_WORKAROUND */
			SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x60, ServiceId);

			osaStatus =
			    FLAG_WAIT(&pShmemDb->WmNotifiedFlagRef, (1 << ServiceId), OSA_FLAG_OR_CLEAR, &Flag,
				      gWmWaitTimeInJiffifes);
			ASSERT(osaStatus == OS_SUCCESS);
			/*SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq , 0x61, gACIPCDRxIndHisrKick); */
			/*BS: In case we notified watermark handler and handler wasn't activated yet - wait for activation before continue */
			/*timeout = wait_event_timeout(gWaitThreadsQueue, (!(gACIPCDWmIndKick & (1<<ServiceId))), gWmWaitTimeInJiffifes); */

			/*BS: we want to assert if wait for event did not arrive after x millisecond. */
			/*ASSERT(timeout != 0); */
#if 0
		/************************************************************************/
			/* Original WA tested for timeout and then tried to do the following:   */
			/* 1. Kick other side's RxInd                                           */
			/* 2. Kick this side's RxInd again.                                     */
			/* 3. Wait for event again                                              */
			/* This WA is obsolete but kept for reference in case such scenario     */
			/* reoccurs.                                                            */
		/************************************************************************/
			do {
				timeout =
				    wait_event_timeout(gWaitThreadsQueue, (!(gACIPCDWmIndKick & (1 << ServiceId))),
						       gWmWaitTimeInJiffifes);
				if (timeout == 0) {
					/*HWM_POLLING_WORKAROUND */
					printk("5 ");
					ACIPCEventSet(ACIPC_DATA_IND);	/* First, force-kick to OtherSide we have TX */
					atomic_add_return(1, &p_sHISRData->count);
					up(&p_sHISRData->hDataSemaphore);
					gACIPCD_HWM_POLLING_WORKAROUND_cntr++;
					SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x61, gACIPCD_HWM_POLLING_WORKAROUND_cntr);
					SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x62, countDown);
					printk("BS: wait_event_timeout timeout, loop %d\n", countDown);
				}
			} while ((timeout == 0) && (--countDown > 0));

			/* If after countDown times RxInd still didn't kick, someting is VERY wrong - assert! */
			ASSERT(countDown > 0);
			SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x63, timeout);
#endif

			SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, 0x6F, ServiceId);
			/*always go over while(ABOVE_HIGH_WATER_MARK checking) */
		}

	}
	/*BS - improved WM handling - end */

	SHMEM_DEBUG_LOG2(MDB_ACIPCDTxReq, DBG_LOG_FUNC_END, ServiceId);

	return ACIPCD_RC_OK;
}

ACIPCD_ReturnCodeE ACIPCDTxReq(ACIPCD_ServiceIdE ServiceId, void *pData, unsigned short Length)
{
	return ACIPCDTxReqACIPC(ServiceId, pData, Length, 0);
}

/******************************************************************************
* Function     :   ACIPCDAddToAllocList
*******************************************************************************
*
* Description  :    Add allocated shared memory to list of allocations
*
*
* Parameters   :    None
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes           :    None.
******************************************************************************/
static void ACIPCDAddToAllocList(void *pShmem)
{
	ShmemPoolAllocsS *new_node;

	SHMEM_DEBUG_LOG2(MDB_ACIPCDAddToAllocList, DBG_LOG_FUNC_START, pShmem);

	new_node = kzalloc(sizeof(ShmemPoolAllocsS), GFP_KERNEL);
	ASSERT(new_node != NULL);

	new_node->pData = pShmem;

	SHMEM_DEBUG_LOG2(MDB_ACIPCDAddToAllocList, 0x10, new_node);

	list_add_tail(&new_node->node, &ACIPCD_ShmemPoolAllocs);

	SHMEM_DEBUG_LOG2(MDB_ACIPCDAddToAllocList, DBG_LOG_FUNC_END, pShmem);
}

/******************************************************************************
* Function     :   ACIPCDRemoveFromAllocList
*******************************************************************************
*
* Description  :    Remove allocated shared memory from list of allocations
*
*
* Parameters   :    None
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes           :    None.
******************************************************************************/
static void ACIPCDRemoveFromAllocList(void *pShmem)
{
	ShmemPoolAllocsS *head, *tmp;

	SHMEM_DEBUG_LOG2(MDB_ACIPCDRemoveFromAllocList, DBG_LOG_FUNC_START, pShmem);

	list_for_each_entry_safe(head, tmp, &ACIPCD_ShmemPoolAllocs, node) {
		if (head->pData == pShmem) {
			SHMEM_DEBUG_LOG2(MDB_ACIPCDRemoveFromAllocList, 0x10, pShmem);
			SHMEM_DEBUG_LOG2(MDB_ACIPCDRemoveFromAllocList, 0x11, head);
			list_del(&head->node);
			kfree(head);
			break;
		}
	}
	SHMEM_DEBUG_LOG2(MDB_ACIPCDRemoveFromAllocList, DBG_LOG_FUNC_END, pShmem);

}

/******************************************************************************
* Function     :   ACIPCDCleanupSharedMemory
*******************************************************************************
*
* Description  :    Clean allocated memory in shared memory.
*                   Called during Silent Reset
*
* Parameters   :    None
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes           :    None.
******************************************************************************/
static void ACIPCDCleanupSharedMemory(void)
{
	ShmemPoolAllocsS *head, *tmp;

	SHMEM_DEBUG_LOG2(MDB_ACIPCDCleanupSharedMemory, DBG_LOG_FUNC_START, 0);

	list_for_each_entry_safe(head, tmp, &ACIPCD_ShmemPoolAllocs, node) {
		SHMEM_DEBUG_LOG2(MDB_ACIPCDCleanupSharedMemory, 0x10, head->pData);
		SHMEM_DEBUG_LOG2(MDB_ACIPCDCleanupSharedMemory, 0x11, head);

		OsaMemFree(MAP_HIDDEN_ADDR_TO(head->pData));
		list_del(&head->node);
		kfree(head);
	}
	SHMEM_DEBUG_LOG2(MDB_ACIPCDCleanupSharedMemory, DBG_LOG_FUNC_END, 0);
}

/******************************************************************************
* Function: ACIPCD_AssertNotification - COMM assert; Notify NVMS to report
* Function: ACIPCDComIfDownIndicate  - COMM assert; Notify Clients LINK_DOWN
* Function: ACIPCDComIfReset         - COMM assert; reset the interface
*******************************************************************************
* Description:  COMM under assert or Silent Reset, execute Notify & Reset
*               split onto TWO separated steps
* 1. Notify clients LINK_DOWN as soon as possible to prevent new requests
*  but NOT RESET the interface. The original ACIPC/SHMEM information logging
*  is under processing now.
* 2. Reset the interface to be ready to go to UP with Silent-Reset
******************************************************************************/
static void ACIPCD_AssertNotification(ShmemInfoS *pShmemDb)
{
	ACIPCD_DatabaseS *pDb;
	/*	As "platform-service" the NVMServer may assist to EEH
		Send "tricky notification" about SHMEM-RESET */
	pDb = CURRENT_ITEM(pShmemDb, ACIPCD_NVM_RPC);
	if (pDb && pDb->CallBackFunc.LinkStatusIndCB &&
		!pShmemDb->early_LinkDown_Notification_DONE) {
			if (pDb->CallBackFunc.LinkStatusIndCBext)
				pDb->CallBackFunc.LinkStatusIndCBext(FALSE,
					ACIPCD_LINK_STATUS_NOTIFY_ONLY);
			else
				pDb->CallBackFunc.LinkStatusIndCB((BOOL)(-1));
			pShmemDb->early_LinkDown_Notification_DONE = TRUE;
	}
}

static void ACIPCDComIfDownIndicateInterface(ShmemInfoS *pShmemDb, UINT32 fullExecReq)
{
	ACIPCD_DatabaseS *pDb;
	UINT32 param;
	BOOL simpleCbAlreadyDone;

	simpleCbAlreadyDone = pShmemDb->bLinkDownDONE || pShmemDb->bLinkDownPartial;
	pShmemDb->bLinkDownDONE = TRUE;

	if (fullExecReq) {
		param = ACIPCD_LINK_STATUS_FULL_EXEC_REQ;
		pShmemDb->bLinkDownPartial = FALSE;
	} else {
		param = ACIPCD_LINK_STATUS_STAGE1_ONLY;
		pShmemDb->bLinkDownPartial = TRUE;
	}

	for (pDb = FIRST_ITEM(pShmemDb); pDb < LAST_ITEM(pShmemDb); pDb++) {
		if (pDb->LinkState == E_CONNECTED) {
			if (pDb->CallBackFunc.LinkStatusIndCBext)
				pDb->CallBackFunc.LinkStatusIndCBext(FALSE, param);
			else if (!simpleCbAlreadyDone)
				pDb->CallBackFunc.LinkStatusIndCB(FALSE);
		}
	}
}

void ACIPCDComIfDownIndicate(UINT32 fullExecReq)
{
	/* notify/partial requested */
	ACIPCD_AssertNotification(&sgShmemDb);
	ACIPCDComIfDownIndicateInterface(&sgShmemDb, fullExecReq);
	ACIPCDComIfDownIndicateInterface(&msaShmemDb, TRUE);
}

static void ACIPCDComIfResetInterface(ShmemInfoS *pShmemDb)
{
	ACIPCD_DatabaseS *pDb;
	BOOL downIndNeeded;

	downIndNeeded = !pShmemDb->bLinkDownDONE || pShmemDb->bLinkDownPartial;
	pShmemDb->bLinkDownDONE = TRUE;

	/*Release RPC waiting requests (if any)*/
	FLAG_SET(&pShmemDb->RpcEndedFlagRef, ALL_SERVICES_BITS(pShmemDb), OSA_FLAG_OR);

	if (downIndNeeded)
		ACIPCDComIfDownIndicateInterface(pShmemDb, TRUE);

	pShmemDb->bLinkDownPartial = FALSE;

	ACIPCD_AssertNotification(pShmemDb);
	pShmemDb->early_LinkDown_Notification_DONE = FALSE; /*prepare to next reset*/

	for (pDb = FIRST_ITEM(pShmemDb); pDb < LAST_ITEM(pShmemDb); pDb++) {
		/*pDb->LowWmIndNeeded = FALSE; */
		pDb->WmHandlerActivated = FALSE;
		pDb->AboveHighWm = FALSE;
		pDb->HighWm = 0;
		pDb->LowWm = 0;
		pDb->Send = 0;
		pDb->Ack = 0;
		/*
		 * CQ00255160
		 * After link down, only services that
		 * were in E_CONNECTED state should be
		 * set to E_NEAR_SIDE_READY
		 */
		if (pDb->LinkState == E_CONNECTED) {
			pDb->LinkState = E_NEAR_SIDE_READY;
		}
	}

	/*
	 * CQ00261383 - fix kernel oops when shared memory
	 * address was not given when COMM WDT occured.
	 */
	if (pShmemDb->ACIPCD_pThisSide) {
#ifndef ACIPCD_NO_IRQ_OPT
		pShmemDb->ACIPCD_pThisSide->InRxProcessing = FALSE;
#endif
		pShmemDb->ACIPCD_pThisSide->NextRead = 0;
		pShmemDb->ACIPCD_pThisSide->NextWrite = 0;
		pShmemDb->ACIPCD_pThisSide->AboveHighWaterMark = 0;
		WRITE_CACHE(pShmemDb->ACIPCD_pThisSide);
	}
}

void ACIPCDComIfReset(UINT32 realExecReq)
{
	if (!realExecReq) {
		/* notify/partial requested*/
		ACIPCD_AssertNotification(&sgShmemDb);
		return;
	}
	ACIPCDComIfResetInterface(&sgShmemDb);
	ACIPCDComIfResetInterface(&msaShmemDb);

	/* Problem: during TxReq, TxCopy type,
	 *   SHMEM driver allocates memory for each message.                                        ,
	 *   This memory is usually free when other side acks the message.
	 *   If silent reset occurs, this memory isn't freed.
	 *   Need to free it during SR
	 */
	ACIPCDCleanupSharedMemory();

#if defined (FEATURE_SHMEM_PM)
	ACIPCDInitPm(FALSE);
#endif
}

/******************************************************************************
* Function     :   ACIPCDComRecoveryIndicate
*******************************************************************************
*
* Description  :    ACIPCDComRecoveryIndicate
*
* Parameters   :    None
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes        :    None.
******************************************************************************/
void ACIPCDComRecoveryIndicate(void)
{
	ACIDBG_TRACE(1, 1, "ACIPCDComRecoveryIndicate called - nothing to do !!!!");
}

/******************************************************************************
* Function     :   ACIPCDRegister
*******************************************************************************
*
* Description  :    Register to the callback functions..
*
* Parameters   :    None
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes        :    None.
******************************************************************************/
ACIPCD_ReturnCodeE ACIPCDRegister(ACIPCD_ServiceIdE ServiceId, ACIPCD_CallBackFuncS *pCbFunc, ACIPCD_ConfigS *pConfig)
{
	ACIPCD_DatabaseS *pDb;

	ShmemInfoS *pShmemDb;

	/* Point to Shmem DB */
	pShmemDb = SHMEM_DB_FROM_SERVICE_ID(ServiceId);
	ASSERT(pShmemDb != NULL);
	/* Get Zero based Service Id */
	ServiceId = GLOBAL_SID_2_LOCAL_SID(ServiceId, pShmemDb->CoreId);
	if (ServiceId >= MAX_SERVICES_PER_INTERFACE) {
		ASSERT(FALSE);
		return ACIPCD_RC_ILLEGAL_PARAM;
	}
	pDb = CURRENT_ITEM(pShmemDb, ServiceId);
/*
 * Check Parameters
 */
	if (!pDb)
		return ACIPCD_RC_ILLEGAL_PARAM;

#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
	/* Coverity CID=68 */
	if (pCbFunc == NULL) {
		ACIDBG_TRACE_2P(1, 1, "ACIPCDRegister: ERROR ServiceId = %d, pCbFunc = %x", ServiceId, pCbFunc);
		ASSERT(FALSE);
	}

	if (pCbFunc->WatermarkIndCB == NULL) {
		ACIDBG_TRACE_3P(1, 1, "ACIPCDRegister: ERROR ServiceId %d, pCbFunc = %x, pCbFunc->WatermarkIndCB = %x",
				ServiceId, pCbFunc, pCbFunc->WatermarkIndCB);
		ASSERT(FALSE);
	}
#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */

	if (pConfig) {
		if (pConfig->RpcTimout && (pCbFunc->TxDoneCnfCB || pCbFunc->TxFailCnfCB))
			return ACIPCD_RC_ILLEGAL_PARAM;

		if (pConfig->RxAction == ACIPCD_COPY)	/*  Currently not supported. */
			return ACIPCD_RC_ILLEGAL_PARAM;

		if (pConfig->TxAction == ACIPCD_COPY && !pShmemDb->ACIPCD_ShmemRef)
			return ACIPCD_RC_NO_MEMORY;

#if (defined OSA_WINCE)
		if (pConfig->TxAction == ACIPCD_HANDLE_CACHE || pConfig->RxAction == ACIPCD_HANDLE_CACHE)
			return ACIPCD_RC_ILLEGAL_PARAM;	/*  Cache handling is not supported in WinCE. */
#endif
	}

	printk
	    ("\nACIPCD Kernel: ACIPCDRegister - service=%d, RxInd=%x, LinkStatus=%x, Watermark=%x, TxDone=%x, TxFail=%x\n",
	     ServiceId, (unsigned int)pCbFunc->RxIndCB, (unsigned int)pCbFunc->LinkStatusIndCB,
	     (unsigned int)pCbFunc->WatermarkIndCB, (unsigned int)pCbFunc->TxDoneCnfCB,
	     (unsigned int)pCbFunc->TxFailCnfCB);

/*
 * Set Callback Functions
 */
	if (pCbFunc->LinkStatusIndCB == NULL) {
		printk("\n\n\nACIPCD Kernel: ERROR THIS LinkStatusIndCB CAN'T ALLOWED NULL \n\n\n");
		/*LinkStatusIndCB is required for Link-Up on system startup - currently there is no way to register
		   without this function */
		ASSERT(FALSE);
	}
	/*if ( pCbFunc ) */
	{
		if (pCbFunc->RxIndCB)
			pDb->CallBackFunc.RxIndCB = pCbFunc->RxIndCB;

		if (pCbFunc->LinkStatusIndCB)
			pDb->CallBackFunc.LinkStatusIndCB = pCbFunc->LinkStatusIndCB;
		if (pCbFunc->LinkStatusIndCBext)
			pDb->CallBackFunc.LinkStatusIndCBext = pCbFunc->LinkStatusIndCBext;
#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
		if (pCbFunc->WatermarkIndCB)
			pDb->CallBackFunc.WatermarkIndCB = pCbFunc->WatermarkIndCB;
#else /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
		if (pCbFunc->LowWmIndCB)
			pDb->CallBackFunc.LowWmIndCB = pCbFunc->LowWmIndCB;
#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */

		if (pCbFunc->TxDoneCnfCB)
			pDb->CallBackFunc.TxDoneCnfCB = pCbFunc->TxDoneCnfCB;

		if (pCbFunc->TxFailCnfCB)
			pDb->CallBackFunc.TxFailCnfCB = pCbFunc->TxFailCnfCB;
	}

/*
 * Set Configuration
 */
	if (pConfig)
		pDb->Config = *pConfig;

	pDb->Config.LowWm--;
	pDb->Config.HighWm--;

	if (pDb->Config.LowWm > pDb->Config.HighWm)
		pDb->Config.LowWm = pDb->Config.HighWm;

/*
 * If far side ready - notify the user
 */
	if (pDb->LinkState == E_FAR_SIDE_READY) {
		pDb->LinkState = E_CONNECTED;
	} else {
		pDb->LinkState = E_NEAR_SIDE_READY;
	}

/*
 * Let Other Side Know Your Water Mark Level.
 */
#if (defined FLAVOR_APP)
	if (pShmemDb->ACIPCD_SharedMemory)
#endif
	{
		unsigned short WaterMark = ((unsigned short)pDb->Config.HighWm << 8) | pDb->Config.LowWm;

		ACIPCDSend(pShmemDb, ServiceId, NULL, WaterMark, pDb, 0);	/*  Open the Link. */
		pDb->Debug.SendControl++;
	}
	/*If CONNECTED - notify user */
	if (pDb->LinkState == E_CONNECTED) {
		pDb->CallBackFunc.LinkStatusIndCB(TRUE);
	}

/*
 * If needed - wait for other side
 */
	if (pConfig && pConfig->BlockOnRegister) {
		unsigned int i;

		for (i = 0; pDb->LinkState != E_CONNECTED; i++)
			msleep_interruptible(10);

	}

	return ACIPCD_RC_OK;
}

/******************************************************************************
* Function     :   ACIPCDInitMsaDatabase and utility ACIPCDInitMsaHandlers
*******************************************************************************
* Description  :    "Constructor" for MSA interface database.
* Parameters   :    None
* Output Param :    None.
* Return value :    None.
* Notes        :    None.
******************************************************************************/
static void ACIPCDInitMsaHandlers(int enable)
{
	ShmemInfoS *pShmemDb = &msaShmemDb;

	BUG_ON(pShmemDb->CoreId != CORE_MSA);	/* not initialized */

	if (enable) {
		/* Rx Indication Low Level Handler */
		pShmemDb->RxIndicationEventHandlerPtr = &ACIPCDRxIndMsaEvent;
		pShmemDb->AddressIndicationEventHandlerPtr = &ACIPCDQueueAdrsIndMsaEvent;
		/* Rx Indication High Level Handler */
		pShmemDb->RxIndicationHandlerParams.Callback = &ACIPCDRxIndMsaHandler;
		pShmemDb->AddressIndicationHandlerParams.Callback = &ACIPCDQueueAdrsIndMsaHandler;
	} else {
		/* NULL is prohibited, use the Dummy */
		pShmemDb->RxIndicationEventHandlerPtr = &ACIPCD_DummyEvent;
		pShmemDb->AddressIndicationEventHandlerPtr = &ACIPCD_DummyEvent;
		pShmemDb->RxIndicationHandlerParams.Callback = &ACIPCD_DummyHandler;
		pShmemDb->AddressIndicationHandlerParams.Callback = &ACIPCD_DummyHandler;
	}
}

static ShmemInfoS *ACIPCDInitMsaDatabase(void)
{
	ShmemInfoS *pShmemDb = &msaShmemDb;

	memset(pShmemDb, 0, sizeof(ShmemInfoS));

	pShmemDb->CoreId = CORE_MSA;
	pShmemDb->EndianType = BIG_ENDIAN;

	pShmemDb->AcipcType = ACMIPC;

	pShmemDb->MaxServices = MAX_MSA_SERVICES;

	/* Critical Section: */
	strcpy(pShmemDb->CriticalSectionParams.Name, "ACMIPC_Data");
	strcpy(pShmemDb->RxIndicationHandlerParams.Name, "ACMIPCDHISR");
	strcpy(pShmemDb->AddressIndicationHandlerParams.Name, "ACMsIPCDQHISR");

	/* Watermark */
#if defined (ENV_LINUX)
	pShmemDb->WatermarkHandlerParams.Callback = &ACIPCDMsaWmHandler;
	strcpy(pShmemDb->WatermarkHandlerParams.Name, "ACMIPCDWM");
#endif
	/* Point to ACIPC functions */
#if 0
	pShmemDb->ACIPCEventBind = &ACIPCEventBind;
	pShmemDb->ACIPCDataSend = &ACIPCDataSend;
	pShmemDb->ACIPCEventSet = &ACIPCEventSet;
#endif

#ifdef FLAVOR_DIAG_MSA_MANAGE
	ACIPCDInitMsaHandlers(ACIPCD_msa_enable);
#else
	ACIPCDInitMsaHandlers(TRUE);
#endif
	return pShmemDb;

}

/******************************************************************************
* Function     :   ACIPCDInitSeagullDatabase
*******************************************************************************
*
* Description  :    "Constructor" for Seagull interface database.
*
* Parameters   :    None
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes           :    None.
******************************************************************************/
static ShmemInfoS *ACIPCDInitSeagullDatabase(void)
{
	ShmemInfoS *pShmemDb = &sgShmemDb;

	memset(pShmemDb, 0, sizeof(ShmemInfoS));

	pShmemDb->CoreId = CORE_SEAGULL;
	pShmemDb->EndianType = BIG_ENDIAN;

	pShmemDb->AcipcType = ACSIPC;

	pShmemDb->MaxServices = ACIPCD_SG_LAST_SERVICE_ID;

	/* Critical Section: */
	strcpy(pShmemDb->CriticalSectionParams.Name, "ACIPC_Data");

	/* Rx Indication: */

	/* Low Level Handler */
	pShmemDb->RxIndicationEventHandlerPtr = &ACIPCDRxIndSgEvent;

	/* High Level Handler */
	pShmemDb->RxIndicationHandlerParams.Callback = &ACIPCDRxIndSgHandler;
	strcpy(pShmemDb->RxIndicationHandlerParams.Name, "ACIPCDHISR");

	/* Shared Memory Address: */

	/* Low Level Handler */
	pShmemDb->AddressIndicationEventHandlerPtr = &ACIPCDQueueAdrsIndSgEvent;

	/* High Level Handler */
	pShmemDb->AddressIndicationHandlerParams.Callback = &ACIPCDQueueAdrsIndSgHandler;
	strcpy(pShmemDb->AddressIndicationHandlerParams.Name, "ACIPCDQHISR");

	/* Watermark */
#if defined (ENV_LINUX)
	pShmemDb->WatermarkHandlerParams.Callback = &ACIPCDSgWmHandler;
	strcpy(pShmemDb->WatermarkHandlerParams.Name, "ACSIPCDWM");
#endif

	/* Point to ACIPC functions */
#if 0
	pShmemDb->ACIPCEventBind = &ACIPCEventBind;
	pShmemDb->ACIPCDataSend = &ACIPCDataSend;
	pShmemDb->ACIPCEventSet = &ACIPCEventSet;
#endif

	return pShmemDb;
}

/******************************************************************************
* Function     :   ACIPCDPhase2Init
*******************************************************************************
*
* Description  :    Create HISR and Register on the ACIPC.
*
* Parameters   :    None
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes           :    None.
******************************************************************************/
static void ACIPCDInterfacePhase2Init(ShmemInfoS *pShmemDb)
{
	unsigned int acipcHistoricEvent;

	sHISRData *p_WmHandlerRef;
	OSA_STATUS osaStatus;

#if (!defined OsaIsrApi)
	Manitoba_Create_HISR(&ACIPCD_DataIndHisrData, "ACIPCDHISR", ACIPCDRxInd, HISR_PRIORITY_1);
	ACIPCD_DataIndHisrRef = (RefT *) &ACIPCD_DataIndHisrData;
	Manitoba_Create_HISR(&ACIPCD_QueueAdrsHisrData, "ACIPCDQHISR", ACIPCDQueueAdrsInd, HISR_PRIORITY_2);
	ACIPCD_QueueAdrsHisrRef = (RefT *) &ACIPCD_QueueAdrsHisrData;
	ACIPCD_ShmemRef = ~0;
#else
	OsaCriticalSectionCreateParamsT CriticalSectionCreateParams;

	sisrRoutine IsrCreateParams;

/*
 * Create the Critical Section Handle
 */
	memset((void *)&CriticalSectionCreateParams, 0, sizeof(CriticalSectionCreateParams));
	CriticalSectionCreateParams.name = pShmemDb->CriticalSectionParams.Name;
	CriticalSectionCreateParams.bSharedForIpc = TRUE;

	pShmemDb->ACIPCD_CriticalSectionRef = OsaCriticalSectionCreate(&CriticalSectionCreateParams);

	pShmemDb->ACIPCD_ShmemRef = OsaMemGetPoolRef(SHMEM_POOL, NULL, NULL);

#if (!defined OsaMemPoolApi)
	if (!ACIPCD_ShmemRef) {
		extern OSPoolRef PoolXRef;
		ACIPCD_ShmemRef = (OsaRefT) PoolXRef;
	}
#endif
/*
 * Create HISR & Register on the ACIPC
 */
	memset((void *)&IsrCreateParams, 0, sizeof(IsrCreateParams));

	IsrCreateParams = pShmemDb->RxIndicationHandlerParams.Callback;
	IsrCreate(&pShmemDb->ACIPCD_DataIndHisrRef, IsrCreateParams);

	IsrCreateParams = pShmemDb->AddressIndicationHandlerParams.Callback;
	IsrCreate(&pShmemDb->ACIPCD_QueueAdrsHisrRef, IsrCreateParams);
/*BS - WM handling task - start */
	gWmWaitTimeInJiffifes = msecs_to_jiffies(WM_IND_WAIT_FOR_SET);

	memset((void *)&IsrCreateParams, 0, sizeof(IsrCreateParams));

	IsrCreateParams = pShmemDb->WatermarkHandlerParams.Callback;
	IsrCreate(&pShmemDb->ACIPCD_WmHandlerRef, IsrCreateParams);

	osaStatus = FLAG_INIT(&pShmemDb->WmFlagRef);
	ASSERT(osaStatus == OS_SUCCESS);

	osaStatus = FLAG_INIT(&pShmemDb->WmNotifiedFlagRef);
	ASSERT(osaStatus == OS_SUCCESS);

	/*BS: Create wait queue for tasks that are in HWM during TxReq */
	init_waitqueue_head(&gWaitThreadsQueue);

	p_WmHandlerRef = pShmemDb->ACIPCD_WmHandlerRef;
	atomic_add_return(1, &p_WmHandlerRef->count);
	up(&p_WmHandlerRef->hDataSemaphore);
/*BS - WM handling task - end */

	ACIPCEventBindExt(pShmemDb->AcipcType, ACIPC_DATA_Q_ADRS, pShmemDb->AddressIndicationEventHandlerPtr,
			  ACIPC_CB_NORMAL, &acipcHistoricEvent);
	ACIPCEventBindExt(pShmemDb->AcipcType, ACIPC_DATA_IND, pShmemDb->RxIndicationEventHandlerPtr, ACIPC_CB_NORMAL,
			  &acipcHistoricEvent);
/*
 * Create Event Flag Group for the RPC Handling
 */
	osaStatus = FLAG_INIT(&pShmemDb->RpcEndedFlagRef);
	ASSERT(osaStatus == OS_SUCCESS);

/*
 * Send/Request the Shared Memory Address
 */
#if (defined FLAVOR_APP)
	if (pShmemDb->CoreId == CORE_SEAGULL)
		ACIPCEventSetExt(pShmemDb->AcipcType, ACIPC_DATA_Q_ADRS);	/*  Request share memory address. */
#endif
#if (defined FLAVOR_COM)
	ACIPCDataSend(0 /*ACIPC_ENTRY */ , ACIPC_DATA_Q_ADRS, (acipc_data) & ACIPCD_SharedMemory);	/*  Send Apps the Shared Memory Address. */
#endif

#endif
}

void ACIPCDPhase2Init(void)
{
	ShmemInfoS *pShmemDb = &sgShmemDb;

#if defined (FEATURE_SHMEM_PM)
	ACIPCDInitPm(TRUE);
#endif /*(FEATURE_SHMEM_PM) */

	ACIPCDInterfacePhase2Init(pShmemDb);

	pShmemDb = &msaShmemDb;
	ACIPCDInterfacePhase2Init(pShmemDb);

}

/******************************************************************************
* Function     :   ACIPCDPhase1Init
*******************************************************************************
*
* Description  :    Initialize database.
*
* Parameters   :    None
*
* Output Param :    None.
*
* Return value :    None.
*
* Notes           :    None.
******************************************************************************/
void ACIPCDInterfacePhase1Init(ShmemInfoS *pShmemDb)
{
	ACIPCD_DatabaseS *pDb;

	for (pDb = FIRST_ITEM(pShmemDb); pDb < LAST_ITEM(pShmemDb); pDb++) {
		pDb->CallBackFunc.RxIndCB = ACIPCDDummyRxInd;
		pDb->CallBackFunc.LinkStatusIndCB = ACIPCDDummyLinkStatusInd;
		pDb->CallBackFunc.TxDoneCnfCB = ACIPCDDummyTxDoneCnf;
		pDb->CallBackFunc.TxFailCnfCB = ACIPCDDummyTxFailCnf;

#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
		pDb->CallBackFunc.WatermarkIndCB = NULL;
#else /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
		pDb->CallBackFunc.LowWmIndCB = NULL;
#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */

		memset(&pDb->Config, 0, sizeof(pDb->Config));
/*        pDb->LowWmIndNeeded                 = FALSE ; */
		pDb->WmHandlerActivated = FALSE;
		pDb->AboveHighWm = FALSE;
		pDb->LinkState = E_CLOSED;
		pDb->HighWm = 0;
		pDb->LowWm = 0;
		pDb->Send = 0;
		pDb->Ack = 0;
		memset(&pDb->Debug, 0, sizeof(pDb->Debug));
	}

	FIRST_ITEM(pShmemDb)->LinkState = E_CONNECTED;

#if (defined FLAVOR_APP)
	pShmemDb->ACIPCD_pOtherSide = pShmemDb->ACIPCD_pThisSide = NULL;	/*  Data will arrive ACIPC. */
	pShmemDb->ACIPCD_SharedMemory = 0;
#endif

#if (defined FLAVOR_COM)
	pShmemDb->ACIPCD_SharedMemory = &ACIPCD_SharedMemory;
#ifndef ACIPCD_NO_IRQ_OPT
	pShmemDb->ACIPCD_pThisSide->InRxProcessing = FALSE;
	pShmemDb->ACIPCD_pThisSide->OptTxPackets = 0;
#endif
	pShmemDb->ACIPCD_pThisSide->NextRead = 0;
	pShmemDb->ACIPCD_pThisSide->NextWrite = 0;
	pShmemDb->ACIPCD_pThisSide->AboveHighWaterMark = 0;

	WRITE_CACHE(pShmemDb->ACIPCD_pThisSide);
#endif

	memset(pShmemDb->ACIPCD_RpcInfo, 0, sizeof(ACIPCD_RpcInfoS));

}

void ACIPCDPhase1Init(void)
{
	ShmemInfoS *pShmemDb;
	pShmemDb = ACIPCDInitSeagullDatabase();
	ACIPCDInterfacePhase1Init(pShmemDb);

	pShmemDb = ACIPCDInitMsaDatabase();
	ACIPCDInterfacePhase1Init(pShmemDb);

	DBGLOG_INIT(shmem, SHMEM_DBG_LOG_SIZE);

	RPCPhase1Init();
}

void printStatistic(void)
{
#if 0
#ifndef ACIPCD_NO_IRQ_OPT
#if (defined OSA_LINUX)
	ShmemInfoS *pShmemDb = &sgShmemDb;
	printk("COM drop/send %d/%d\n", pShmemDb->ACIPCD_pOtherSide->OptTxPackets, pShmemDb->ACIPCD_pOtherSide->Debug1);
#endif

#if (defined OSA_WINCE)
	printf("COM drop/send %d/%d\n", ACIPCD_pOtherSide->OptTxPackets, ACIPCD_pOtherSide->Debug1);
#endif
#endif
#endif
}

void ACIPCDPrintPowerDebugDatabase(void)
{
	int i;

	printk("\n\nACIPCD Pm RSP List:\n");
	/*search for free place */
	for (i = 0; i < PM_DEBUG_LIST_SIZE; i++) {
		if (PmDb.pmDebugList[i].pData != NULL) {
			printk("item=%d, serviceId=%d, pData=%p, length=%d\n",
			       i, PmDb.pmDebugList[i].serviceId, PmDb.pmDebugList[i].pData, PmDb.pmDebugList[i].length);
		}
	}

}

void ACIPCDPrintPowerDatabase(void)
{
	printk("\nACIPCD Power Database:\n");
	printk("    TxWaitAckCounter  = %d\n", PmDb.TxWaitAckCounter);
	printk("    RxWaitRspCounter  = %d\n", PmDb.RxWaitRspCounter);
	printk("    HisrWaitCounter   = %d\n", PmDb.HisrWaitCounter);
	printk("    CurrentSleepAllow = %d\n", PmDb.CurrentSleepAllow);
	printk("    TimerTicking      = %d\n", PmDb.TimerTicking);
	printk("    IdleTimerTimeout  = %d\n", PmDb.IdleTimerTimeout);
#if defined (ACIPCD_PM_DEBUG)
	printk("    debugWakeCount    = %d\n", PmDb.debugWakeCount);
	printk("    debugSleepCount   = %d\n", PmDb.debugSleepCount);
#endif /*ACIPCD_PM_DEBUG */
}

void ACIPCDPrintDatabase(void)
{
	ShmemInfoS *pShmemDb = &sgShmemDb;

	ACIPCD_DatabaseS *pTempDb;
	int i;

	if ((!pShmemDb->ACIPCD_pThisSide) || (!pShmemDb->ACIPCD_pOtherSide)) {
		ACIDBG_TRACE(1, 1, "No CP, No DB\n");

		return;
	}

	ACIDBG_TRACE(1, 1, "ACIPCD Database Info:");
	ACIDBG_TRACE_2P(1, 1, "ACIPCD_pThisSide->NextRead = %d, ACIPCD_pThisSide->NextWrite = %d",
			pShmemDb->ACIPCD_pThisSide->NextRead, pShmemDb->ACIPCD_pThisSide->NextWrite);
	ACIDBG_TRACE_2P(1, 1, "ACIPCD_pThisSide->AboveHighWaterMark = %d, ACIPCD_pThisSide->InRxProcessing = %d",
			pShmemDb->ACIPCD_pThisSide->AboveHighWaterMark, pShmemDb->ACIPCD_pThisSide->InRxProcessing);
	ACIDBG_TRACE_2P(1, 1, "ACIPCD_pOtherSide->NextRead = %d, ACIPCD_pOtherSide->NextWrite = %d",
			pShmemDb->ACIPCD_pOtherSide->NextRead, pShmemDb->ACIPCD_pOtherSide->NextWrite);
	ACIDBG_TRACE_2P(1, 1, "ACIPCD_pOtherSide->AboveHighWaterMark = %d, ACIPCD_pOtherSide->InRxProcessing = %d",
			pShmemDb->ACIPCD_pOtherSide->AboveHighWaterMark, pShmemDb->ACIPCD_pOtherSide->InRxProcessing);

	for (pTempDb = FIRST_ITEM(pShmemDb), i = 0; i < ACIPCD_RESERVE_1; pTempDb++, i++) {
		ACIDBG_TRACE_1P(1, 1, "Service = %2d: ", i);
		ACIDBG_TRACE_2P(1, 1, "    AboveHighWm = %d, LinkState=%d", pTempDb->AboveHighWm, pTempDb->LinkState);
		ACIDBG_TRACE_2P(1, 1, "    Send = %d, Ack = %d", pTempDb->Send, pTempDb->Ack);
		ACIDBG_TRACE_2P(1, 1, "    HighWm = %d, LowWm = %d", pTempDb->HighWm, pTempDb->LowWm);
		ACIDBG_TRACE_2P(1, 1, "    DEBUG: Total Send = %d, Total Recv = %d", pTempDb->Debug.Send,
				pTempDb->Debug.Recv);
		ACIDBG_TRACE_2P(1, 1, "    DEBUG: SendControl = %d, RecvControl = %d", pTempDb->Debug.SendControl,
				pTempDb->Debug.RecvControl);
		ACIDBG_TRACE_3P(1, 1, "    DEBUG: TxReq = %d, TxDoneCnf = %d, TxFailCnf = %d", pTempDb->Debug.TxReq,
				pTempDb->Debug.TxDoneCnf, pTempDb->Debug.TxFailCnf);
		ACIDBG_TRACE_3P(1, 1, "    DEBUG: RxInd = %d, RxDoneRsp = %d, RxFailRsp = %d", pTempDb->Debug.RxInd,
				pTempDb->Debug.RxDoneRsp, pTempDb->Debug.RxFailRsp);
		ACIDBG_TRACE_2P(1, 1, "    DEBUG: LowWmCnt = %d, HighWmCnt = %d", pTempDb->Debug.LowWmCnt,
				pTempDb->Debug.HighWmCnt);
	}

	/*DbgLogPrint(DBGLOG_GET_INFO(shmem)); */
}

extern void seh_cp_assert_simulate(void);

void ACIPCDPrintShmemDebugLog(void)
{
	CRITICAL_SECTION_ENTER;

#if 0
	seh_cp_assert_simulate();
#endif
	DBGLOG_PRINT(shmem);

	CRITICAL_SECTION_LEAVE;
}

EXPORT_SYMBOL(ACIPCDRxDoneRsp);
EXPORT_SYMBOL(ACIPCDTxReq);
EXPORT_SYMBOL(ACIPCDRegister);
