/*-----------------------------------------------------------
(C) Copyright [2006-2012] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
    Title:       CI Data Client header file
    Filename:    ci_data_client.h
    Description: Contains defines/structs/prototypes for the R7/LTE data path
    Author:      Yossi Gabay
************************************************************************/

#if !defined(_CI_DATA_CLIENT_H_)
#define      _CI_DATA_CLIENT_H_

#ifdef __cplusplus
extern "C" {
#endif

/*#include <linux/kthread.h> */
#include <linux/dma-mapping.h>
#include <asm/cacheflush.h>
#include <linux/version.h>
#ifdef __KERNEL__
#include <linux/spinlock.h>
#if !defined (ESHEL)		/*!defined (TEL_CFG_FLAVOR_ITM) */
#include "itm.h"
#else
#include <linux/itm.h>
#endif				/*(LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 36)) */
#endif				/*__KERNEL__ */
#include "ci_data_common.h"
#include "ci_data_client_api.h"
#include "ci_data_client_handler.h"
#include "linux_types.h"

/************************************************************************
 * Defines
 ***********************************************************************/

#define CI_DATA_DEBUG_LEVEL     1	/*0 - 3: 0 - none, 1 - errors, 2 - debug, 3 - info, 4 - more info, 5 - functions enter/exit */

#if CI_DATA_DEBUG_LEVEL > 0
#define CI_DATA_DEBUG(lEVEL, fMT, aRG...)        {if (lEVEL <= CI_DATA_DEBUG_LEVEL) printk("CIDAT: "fMT"\n", ##aRG); }
#else
#define CI_DATA_DEBUG(lEVEL, fMT, aRG...)        do {} while (0)	/*{do nothing} */
#endif				/*CI_DATA_DEBUG */

#if defined (CI_DATA_FEATURE_CACHED_ENABLED)
#define CI_DATA_CACHE_INVALIDATE(sTARTaDDR, sIZE)    dmac_map_area(sTARTaDDR, (((sIZE)+31) & ~31), DMA_FROM_DEVICE);	/* Invalidates cache in the range, make sure start and size are 32-byte aligned */
/*#define CI_DATA_CACHE_INVALIDATE(sTARTaDDR,sIZE)    //nothing */

#define CI_DATA_CACHE_FLUSH(sTARTaDDR, sIZE)           __cpuc_flush_dcache_area(((void *)(sTARTaDDR)), ((unsigned int)sIZE));
/*#define CI_DATA_CACHE_FLUSH(sTARTaDDR,sIZE)         dmac_map_area(sTARTaDDR, (((sIZE)+31) & 0xFFFFFFE0), DMA_TO_DEVICE); / * Cleans cache in the range, make sure start * / */
/*#define CI_DATA_CACHE_FLUSH(sTARTaDDR,sIZE)         //nothing */

#else				/* !CI_DATA_FEATURE_CACHED_ENABLED */
#define CI_DATA_CACHE_INVALIDATE(sTARTaDDR, sIZE)
#define CI_DATA_CACHE_FLUSH(sTARTaDDR, sIZE)
#endif				/*CI_DATA_FEATURE_CACHED_ENABLED */

/* BS - Descriptors cache feature */
#if defined (CI_DATA_FEATURE_DESCRIPTORS_CACHED) && defined (CI_DATA_FEATURE_CACHED_ENABLED)
#define CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(sTARTaDDR, sIZE)    CI_DATA_CACHE_INVALIDATE((void *)sTARTaDDR, sIZE)
#define CI_DATA_DESCRIPTORS_CACHE_FLUSH(sTARTaDDR, sIZE)         CI_DATA_CACHE_FLUSH((void *)sTARTaDDR, sIZE)
#else
#define CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(sTARTaDDR, sIZE)
#define CI_DATA_DESCRIPTORS_CACHE_FLUSH(sTARTaDDR, sIZE)
#endif

/*converting COMM address to APPS address */
#define CI_DATA_COMADDR_TO_APPADDR(aDDR)            ConvertPhysicalToVirtualAddr((unsigned int)(aDDR))

/*#ifdef TEL_CFG_COMM_TO_APPS_ADDR_USE_MASK */
#if defined(ESHEL)
#define CI_DATA_COMADDR_TO_APPS_PHSYCIAL_ADDR(aDDR) ((((unsigned int)aDDR) & 0x0FFFFFFF) | 0x80000000)
#else
/*ADIR */
#define CI_DATA_COMADDR_TO_APPS_PHSYCIAL_ADDR(aDDR) ((((unsigned int)aDDR) & 0x0FFFFFFF))
#endif				/*TEL_CFG_COMM_TO_APPS_ADDR_USE_MASK */

#define CI_DATA_IS_SHARED_MEMORY(aDDR, lEN) ((aDDR >= gCiDataClient.uplinkInfo.baseAddress) && \
		((aDDR + lEN) < (gCiDataClient.uplinkInfo.baseAddress + gCiDataClient.uplinkInfo.size)))
/*
#define CI_DATA_LOCK_INIT(sPINLOCK) \
	CIData_SpinlockS    sPINLOCK = {0, __SPIN_LOCK_UNLOCKED(protect_lock)}*/
#define CI_DATA_LOCK(sPINLOCK)                      spin_lock_irqsave(&(sPINLOCK.protect_lock), sPINLOCK.flags);
#define CI_DATA_UNLOCK(sPINLOCK)                    spin_unlock_irqrestore(&(sPINLOCK.protect_lock), sPINLOCK.flags);

/*Uplink unified memory & remove copy */
#define CI_DATA_UPLINK_NUM_PACKETS_BEFORE_FREE      32

/*BS - Mutli packet support */
#define CI_TOTAL_ITEMS_FOR_USB      256
#define CI_MAX_ITEMS_FOR_USB        (CI_TOTAL_ITEMS_FOR_USB - 2)
#define CI_MAX_DATA_SIZE_FOR_USB    0x4000	/*16K */
#define CI_DATA_MAX_ROUTER_SOLICITATIONS    5

/* Uplink QOS */
#define CI_DATA_CID_DEFAULT_PRIORITY    9

/************************************************************************
 * Types
 ***********************************************************************/
/*BS - internal debug*/
/*ICAT EXPORTED ENUM */
	typedef enum {
		CIDATA_DBG_EVENT_EMPTY = 0,

		MDB_CiDataDownlinkTask,	/*1 */
		MDB_CiDataDownlinkFindOldestCid,	/*2 */
		MDB_CiDataSendPacket,	/*3 */
		MDB_CiDataUpdateCidUsageCount,	/*4 */
		MDB_CiDataBuildListOfPackets,	/*5 */
		MDB_CiDataFindFirstItem,	/*6 */
		MDB_CiDataFindNextItem,	/*7 */
		MDB_CiDataUpdateBufferStartPointersFromOffset,	/*8 */
		MDB_CiDataRestoreTempReadIndexes,	/*9 */
		MDB_CiDataGetListFromCid,	/*10 */
		MDB_CiDataClientLtmDownlinkDataSend,	/*11 */
		MDB_CiDataUpdateReadIndexesAndCidUsageCount,	/*12 */
		MDB_CiDataDownlinkDataReadCnf,	/*13 */

		/*Must be Last Event */
		MDB_CidataDebugLastEntry = 0x7FFFFFFF
	} CIDATA_EventTypeE;

	typedef enum {
		CI_DATA_CID_STATE_CLOSED = 0,
		CI_DATA_CID_STATE_PRIMARY,
		CI_DATA_CID_STATE_SECONDARY
	} CiData_CidStateE;

	typedef enum {
		FOUND_NONE,
		FOUND_SINGLE_ITEM,
		FOUND_LIST_ITEM,
		FOUND_END_ITEM
	} CiDataDlListFindResultE;

	typedef enum {
		CI_DATA_IS_DISBALED = 0,
		CI_DATA_IS_ENABLED,
		CI_DATA_IS_FORCED_DISABLED,
		CI_DATA_IS_LOOPBACK_OPEN,
		CI_DATA_IS_TEST_MODE
	} CiData_DataStatusE;

/*spinlock */
	typedef struct {
		unsigned long flags;
		spinlock_t protect_lock;
	} CIData_SpinlockS;

	typedef struct {
		unsigned int ipv4addr;	/* 0 is invalid */
		unsigned int ipv6addr[4];	/* 0 is invalid */
	} CiData_CidIPInfoS;

	typedef struct {	/* used by ioctl form mtil server */
		unsigned int cid;
		CiData_CidIPInfoS ipAddrs;
	} CiData_CidIPInfoComandS;

	typedef struct {
		unsigned int cid;
		CiData_ServiceTypeE serviceType;
		unsigned char connectionType;	/*YG: is this really required? for what? */
		unsigned char priority;
	} CiData_CidInfoS;

	typedef struct {
		unsigned int cid;
		unsigned int bytesToSend;
	} CiData_GcfCmdS;

/*downlink queue info */
	typedef volatile struct {
		unsigned int baseAddress;
		unsigned int size;
	} CiData_DownlinkInfoS;

/*uplink queue info */
	typedef struct {
		unsigned int baseAddress;
		unsigned int size;
	} CIData_UplinkInfoS;

/*cid Databases */
	typedef struct {
		unsigned int cid;
		CiData_CidStateE state;

		CiData_ServiceTypeE serviceType;
		unsigned char connectionType;	/*YG: is this really required? for what? */
		CiData_TFTFilterNodeS *pTftFilterList;

		ci_data_downlink_data_receive_cb downlink_data_receive_cb;

		CiData_CidIPInfoS ipAddrs;
		unsigned char ipV6PrefixLen;	/* filled from router advertisement */
		unsigned char ipV6RouterSolicitations;
		unsigned char priority;
		struct fp_net_device *fp_dev;	/*fastpath net device for this cid */
	} CIData_CidDatabaseS;

	typedef enum {
		CI_DATA_LOOPBACK_B_INACTIVE = 0,
		CI_DATA_LOOPBACK_B_DELAYED,
		CI_DATA_LOOPBACK_B_ACTIVE
	} CiDataLoopBackStatusE;

	typedef struct {
		CiDataLoopBackStatusE status;
		unsigned int delay;
		ci_data_downlink_data_receive_cb
		    downlinkDataReceiveCB_original[CI_DATA_TOTAL_CIDS];
		CiData_ServiceTypeE serviceType_original[CI_DATA_TOTAL_CIDS];
		CiData_DataStatusE dataStatus_original;
		unsigned char timerExpired;
		unsigned int bufferIpPdusCounter[CI_DATA_TOTAL_CIDS];
		unsigned char firstpacketArrived;
	} CiData_LoopBackS;

#ifdef __KERNEL__
	typedef struct {
		/*general */
		unsigned int linkStatusBits;
		unsigned char shmemConnected;
		CiData_DataStatusE dataStatus;
		unsigned char inSilentReset;
		int downlinkAnalizePackets;

		/*uplink/downlink common */
		CiData_SharedAreaS *pSharedArea;

		unsigned char volatile uplinkDataStartNotifyRequired;

		/*cid/queue info */
		CIData_CidDatabaseS cid[CI_DATA_TOTAL_CIDS];

		CIData_UplinkInfoS uplinkInfo;
		CiData_DownlinkInfoS downlinkQueueInfo;

		/*os */
		struct task_struct *downlinkTaskRef;
		flag_wait_t downlinkWaitFlagRef;
		struct timer_list uplinkStartNotifyTimeoutTimer;
		flag_wait_t silentResetWaitFlagRef;

		unsigned char uplinkIgnorePacketsDisabled;

		unsigned char uplinkQosDisabled;

		CiData_LoopBackS loopBackdataBase;

		classifier_f fp_classify_f;	/*fast path classify function */
		CiData_GlobalConfigS mtil_globals_cfg;
	} CiData_ClientDatabaseS;
#endif

	typedef struct {
		unsigned int nextWriteIdx[CI_DATA_TOTAL_CIDS];
		unsigned int nextReadIdx[CI_DATA_TOTAL_CIDS];
		unsigned int nextReadCnfIdx[CI_DATA_TOTAL_CIDS];
		unsigned int nextFreeIdx[CI_DATA_TOTAL_CIDS];
	} CiData_DownlinkIndexesS;

/*BS - Mutli-packet support */
	typedef struct {
		unsigned int countPerCid[CI_DATA_TOTAL_CIDS];	/*Each CID that is in the LTM list gets the value of globalUsageCount */
		unsigned int oldestCidList[CI_DATA_TOTAL_CIDS];	/*If several CIDs have same number... */
		unsigned int numItemsInOldestCidList;
		unsigned int globalUsageCount;	/*incremented by one for each LTM transmit */
		unsigned int oldestCid;
	} CiData_CidUsageCountS;

	typedef struct {
		unsigned char cid[CI_DATA_TOTAL_CIDS];
		unsigned char numActive;
	} CiData_ActiveCids;

/*
 *  List of pointers to downlink descriptors.
 *  CiDataDownlinkTask builds a list of pointers to consecutive
 *  downlink packets in SBM buffer. This list is then transfered to USB
 *
 *  Important - last item must point to NULL
 */
	typedef struct {
		CiData_DownlinkIndexesS downlinkIndexes;
		CiData_DownlinkDescriptorS
		    *ciDownlinkItemsForUsb[CI_TOTAL_ITEMS_FOR_USB];
		unsigned int tempReadIdx[CI_DATA_TOTAL_CIDS];
		unsigned char cidsInList[CI_DATA_TOTAL_CIDS];	/*which CIDs are in the list */
		unsigned int numItemsOnList;
		unsigned int totalSize;
		unsigned int dataSize[CI_DATA_TOTAL_CIDS];	/*for RNDIS statistics */
		unsigned char inProgress;
	} CiData_DownlinkPointersListS;

	typedef struct {
		unsigned int numListReachedMaxSize;
		unsigned int numListReachedMaxPackets;
		unsigned int numListReachedOffsetLimit;
		unsigned int numListReachedNonHostip;
		unsigned int numListReachedQueueEmpty;
		unsigned int numListBuildFailedOnFirstItem;
		unsigned int averageTotalSize;
		unsigned int averageNumPackets;
		unsigned int numBusyPackets;

		unsigned int totalNumPacketsReceived[CI_DATA_TOTAL_CIDS];
		unsigned int maxPacketsPendingPerCid[CI_DATA_TOTAL_CIDS];
		unsigned int maxPacketsNotFreedPerCid[CI_DATA_TOTAL_CIDS];
		/*unsigned int                      lastDataSize[CI_DATA_TOTAL_CIDS];//for RNDIS statistics */
	} CiData_DownlinkDebugStatisticsS;

/*values for calculation average packet size that gives bigger packets
higher weight compared to current average value. curently using base of 1024 */
#define CI_DATA_STATS_UPLINK_BIGGER_PREV_AVERAGE_WEIGHT     512
#define CI_DATA_STATS_UPLINK_BIGGER_NEW_PACKET_WEIGHT       512
#define CI_DATA_STATS_UPLINK_SMALLER_PREV_AVERAGE_WEIGHT    32
#define CI_DATA_STATS_UPLINK_SMALLER_NEW_PACKET_WEIGHT      992

	typedef struct {
		/*global */
		unsigned int currentBytesUsedInQueue;
		unsigned int maxBytesUsedInQueue;
		unsigned int currentNumPacketsPending;
		unsigned int maxNumPacketsPending;
		unsigned int totalNumPacketsSent;
		unsigned int totalNumPacketsThrown;
		/*unsigned int                      numTimesUplinkQueueWasFull[CI_DATA_UPLINK_TOTAL_QUEUES]; */

		/*debug & statistics per queue/cid */
		unsigned int
		    numPacketsSentPerQueue[CI_DATA_UPLINK_TOTAL_QUEUES];
		unsigned int
		    numPacketsThrownPerQueue[CI_DATA_UPLINK_TOTAL_QUEUES];
		unsigned int
		    numPacketsThrownQosPerQueue[CI_DATA_UPLINK_TOTAL_QUEUES];

		unsigned int
		    maxPacketsPendingPerQueue[CI_DATA_UPLINK_TOTAL_QUEUES];
		unsigned int
		    averagePacketSizePerQueue[CI_DATA_UPLINK_TOTAL_QUEUES];
		unsigned int
		    numTimesUplinkQueueWasEmpty[CI_DATA_UPLINK_TOTAL_QUEUES];
		unsigned int
		    numPacketsDroppedByTrafficShaping
		    [CI_DATA_UPLINK_TOTAL_QUEUES];
		/*unsigned int                      numTimesUplinkQueueWasFull[CI_DATA_UPLINK_TOTAL_QUEUES]; */
	} CiData_UplinkDebugStatisticsS;

	typedef struct {
		CiData_DownlinkDebugStatisticsS downlink;
		CiData_UplinkDebugStatisticsS uplink;

	} CiData_DebugStatisticsS;

/*read stats for MTSD clients */
/*TS_DB statistics */

#if 0
	typedef struct {
		int currTcpSessions;	/*number of TCP sessions currently open */
		int maxHashColls;	/*number of hash collisions */
	} CiData_TsDbStats;
#endif

/*TFT stats */
	typedef struct {
		int tftFilterId;
		unsigned int matches;
		unsigned int noMatches;
	} CiData_TftStats;

/*struct statistics to be reported to MTIL*/
	typedef struct {

		CiData_CidStateE state;	/*closed/secondery/primery CID */

		unsigned int numPacketsSentPerQueue[2];	/*packets sent */

		unsigned int numPacketsThrownPerQueue[2];	/*packets thrown */

		unsigned int maxPacketsPendingPerQueue[2];	/*packets pending */

		unsigned int averagePacketSizePerQueue[2];	/*packets size high */

		unsigned int numTimesUplinkQueueWasEmpty[2];	/*empty queue */

		unsigned int numPacketsDroppedByTrafficShaping[2];	/*packets dropped dueu to traffic shaping */

		unsigned int numOfTft;	/*the number of TFTs associated with the TFT` */

		CiData_TftStats Tft[16];	/* the TFTs associated with the CID */

	} CiData_UlCidStat;

	typedef struct {
		/*global */
		unsigned int currentBytesUsedInQueue;
		unsigned int maxBytesUsedInQueue;
		unsigned int currentNumPacketsPending;
		unsigned int maxNumPacketsPending;
		unsigned int totalNumPacketsSent;
		unsigned int totalNumPacketsThrown;
		unsigned int totalNumTcpSessions;	/*total number of TCP sessions currently open in the UL */
		unsigned int tcpSesionMaxHashTableCollision;	/*maximum number of collisions on the TCP session table */

		/*CID */
		CiData_UlCidStat CidStat[CI_DATA_TOTAL_CIDS];

	} CiData_UplinkStat;

	typedef struct {
		int cid;
		CiData_DownlinkDebugStatisticsS downlinkStat;
		CiData_UplinkStat uplinkStat;

	} CiData_PdpsStat;

#ifdef __KERNEL__
#if defined(_CI_DATA_CLIENT_PARAMS_)

	CiData_ClientDatabaseS gCiDataClient;
	CiData_DebugStatisticsS gCiDataDebugStatistics;

#else				/*_CI_DATA_CLIENT_PARAMS_ */

	extern CiData_ClientDatabaseS gCiDataClient;
	extern CiData_DebugStatisticsS gCiDataDebugStatistics;
#endif				/*_CI_DATA_CLIENT_PARAMS_ */
#endif

/************************************************************************
 * Function Prototypes
 ***********************************************************************/

	void CiDataClientInit(void);
	void CiDataLinkStatusHandler(CiData_ChannelTypeE channelType,
				     unsigned char linkStatus);

	void CiDataPrintBasicCidInfo(void);
	void CiDataPrintDatabase(unsigned char printAll);
	void CiDataPrintStatistics(unsigned char printAll);
	void CiDataHandleControlCommand(char type, char *params);

	void CiDataClientCidEnable(CiData_CidInfoS *pCidInfo,
				   unsigned char isPrimary);
	void CiDataClientCidDisable(unsigned int cid);
	void CiDataClientCidSetIpAddress(CiData_CidIPInfoComandS *
					 ipInfoCommand);
	void CiDataClientCidGetIpAddress(CiData_CidIPInfoComandS *
					 ipInfoCommand);
	void CiDataClientCidSetPriority(CiData_CidInfoS *pCidInfo);
	void CiDataClientPrintActiveCids(void);

	unsigned char CiDataDownlinkDataReceiveDefCB(unsigned int cid,
						     void *packet_address,
						     unsigned int
						     packet_length);
	unsigned int CiDataUplinkCopyToInternalMemory(unsigned int cid,
						      void *packet_address,
						      unsigned int
						      packet_length);
	unsigned int CiDataUplinkCopyToSharedMemory(unsigned int cid,
						    void *packet_address,
						    unsigned int packet_length,
						    unsigned int
						    *pIndexInQueue);
	void CiDataUplinkMarkDroppedPacket(unsigned int queueNum,
					   unsigned int index);
	void CiDataUplinkDataFree(void);
	void CiDataClientDropLowestPriorityPackets(void);

	void CiDataClientCidGetStat(CiData_PdpsStat *pdpStats);

#ifdef __cplusplus
}
#endif				/*__cplusplus */
#endif				/* _CI_DATA_CLIENT_H_ */
