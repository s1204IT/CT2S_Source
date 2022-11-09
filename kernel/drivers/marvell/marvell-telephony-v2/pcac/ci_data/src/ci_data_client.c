/*------------------------------------------------------------
(C) Copyright [2006-2012] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
    Title:       CI Data Client
    Filename:    ci_data_client.c
    Description: Contains the MAIN functionality of CI Data for R7/LTE data path
    Author:      Yossi Gabay
************************************************************************/

#include <linux/wait.h>
#include <linux/kthread.h>
#include <linux/dma-mapping.h>
#include <asm/cacheflush.h>
#include <asm/atomic.h>

#include "osa_kernel.h"
#define _CI_DATA_CLIENT_PARAMS_
#include "ci_data_client.h"
#undef  _CI_DATA_CLIENT_PARAMS_
#include "ci_data_client_shmem.h"
#include "ci_data_client_handler.h"
#include "ci_data_client_api.h"
#include "ci_data_client_ltm.h"
#include "ci_data_client_mem.h"
#include "ci_data_client_loopback.h"
/************************************************************************
 * Internal debug log
 ***********************************************************************/
#include "dbgLog.h"
DBGLOG_DECLARE_VAR(cidata);
#define     CIDATA_DBG_LOG_SIZE  2000	/*reduced from 10000 for Eshel */
#define     CIDATA_DEBUG_LOG2(a, b, c)     DBGLOG_ADD(cidata, a, b, c)

unsigned char CiDataLoopBackModeBCallBack(unsigned int cid, void *packet_address, unsigned int packet_length);

/************************************************************************
 * Internal Prototypes
 ***********************************************************************/
/*shmem spinlock */
/*CI_DATA_LOCK_INIT(gCiDataClientShmemSpinlock);*/
CIData_SpinlockS    gCiDataClientShmemSpinlock = {0, __SPIN_LOCK_UNLOCKED(protect_lock)};

/*for printing the database */
typedef struct {
	unsigned short nextWriteIdx[CI_DATA_UPLINK_TOTAL_QUEUES];
	unsigned short nextReadIdx[CI_DATA_UPLINK_TOTAL_QUEUES];
	unsigned int nextReadCnfIdx[CI_DATA_UPLINK_TOTAL_QUEUES];
	unsigned short nextFreeIdx[CI_DATA_UPLINK_TOTAL_QUEUES];
} CiData_UplinkIndexesS;

static void CiDataKillFpdev(struct fp_net_device *fp_dev);

/************************************************************************
 * Internal Globals
 ***********************************************************************/
extern CiData_UplinkPoolMemS gCiDataUplinkPoolMem;

/*BS - Mutli packet support */
static CiData_DownlinkPointersListS gCiDataDownlinkPointersList;
static CiData_CidUsageCountS cidUsageCount;
static unsigned int ciDataMaxNumOfPacketsForUsb;

static CiData_ActiveCids activeCids;
static CiData_ActiveCids activeCids_Snapshot;

extern netdev_tx_t CiDataClientLtmDownlinkDataSend(void **packetsList, struct fp_net_device *dst_dev);

extern void itm_flush_all_pending_packets(void);

extern TS_DB ts_db;

/***********************************************************************************\
* Functions
************************************************************************************/

/***********************************************************************************\
*   Name:        CiDataClientPrintActiveCids
*
*   Description: Print Active CIDs
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataClientPrintActiveCids(void)
{
	unsigned char i;

	CI_DATA_DEBUG(1, "Active CIDs\n--------------\n");
	for (i = 0; i < activeCids.numActive; i++) {
		CI_DATA_DEBUG(1, "cid=%d, priority=%d\n", activeCids.cid[i],
			      gCiDataClient.cid[activeCids.cid[i]].priority);
	}

}

/***********************************************************************************\
*   Name:        CiDataPrintStatistics
*
*   Description: Print Ci Data various statistics
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataPrintStatistics(unsigned char printAll)
{
	/*int cid = 3; */
	int cid;
	int queueNum;
	/*unsigned int temps[4]; */
	CiData_DownlinkDebugStatisticsS *downlinkStats = &gCiDataDebugStatistics.downlink;
	CiData_UplinkDebugStatisticsS *uplinkStats = &gCiDataDebugStatistics.uplink;

	CI_DATA_DEBUG(0, "CI Data Client: Statistics");

	CI_DATA_DEBUG(0, "");

	CI_DATA_DEBUG(0, "  Downlink Debug Statistics:");
	CI_DATA_DEBUG(0, "    numListBuildFailedOnFirstItem = %d", downlinkStats->numListBuildFailedOnFirstItem);
	CI_DATA_DEBUG(0, "    numListReachedMaxPackets = %d", downlinkStats->numListReachedMaxPackets);
	CI_DATA_DEBUG(0, "    numListReachedMaxSize = %d", downlinkStats->numListReachedMaxSize);
	CI_DATA_DEBUG(0, "    numListReachedNonHostip = %d", downlinkStats->numListReachedNonHostip);
	CI_DATA_DEBUG(0, "    numListReachedOffsetLimit = %d", downlinkStats->numListReachedOffsetLimit);
	CI_DATA_DEBUG(0, "    numListReachedQueueEmpty = %d", downlinkStats->numListReachedQueueEmpty);
	CI_DATA_DEBUG(0, "    averageTotalSize = %d", downlinkStats->averageTotalSize);
	CI_DATA_DEBUG(0, "    averageNumPackets = %d", downlinkStats->averageNumPackets);
	CI_DATA_DEBUG(0, "    numBusyPackets = %d", downlinkStats->numBusyPackets);

	/*CI_DATA_DEBUG(0, "      Downlink CID Info:");
	   for(cid = 0; cid < CI_DATA_TOTAL_CIDS; cid++)
	   {
	   if((printAll == TRUE) || (gCiDataClient.cid[cid].state != CI_DATA_CID_STATE_CLOSED))
	   {
	   CI_DATA_DEBUG(0, "         cid=%d: nextWriteIdx=%d",
	   cid, downlinkIndexes.nextWriteIdx[cid]);
	   CI_DATA_DEBUG(0, "         nextReadIdx=%d, nextReadCnfIdx=%d",
	   downlinkIndexes.nextReadIdx[cid], downlinkIndexes.nextReadCnfIdx[cid]);
	   CI_DATA_DEBUG(0, "         nextFreeIdx=%d,",
	   downlinkIndexes.nextFreeIdx[cid]);
	   }
	   } */

	CI_DATA_DEBUG(0, "  Uplink Debug Statistics:");
	CI_DATA_DEBUG(0, "    maxBytesUsedInQueue = %d", uplinkStats->maxBytesUsedInQueue);
	CI_DATA_DEBUG(0, "    currentBytesUsedInQueue = %d", uplinkStats->currentBytesUsedInQueue);

	for (cid = 0; cid < CI_DATA_TOTAL_CIDS; cid++) {
		if ((printAll == TRUE) || (gCiDataClient.cid[cid].state != CI_DATA_CID_STATE_CLOSED)) {
			CI_DATA_DEBUG(0, "    cid = %d:", cid);

			queueNum = CI_DATA_CID_TO_QUEUE_CONVERT(cid, CI_DATA_QUEUE_PRIORITY_LOW);
			CI_DATA_DEBUG(0, "      Low priority queue:");
			CI_DATA_DEBUG(0, "        maxPacketsPendingPerQueue = %d",
				      uplinkStats->maxPacketsPendingPerQueue[queueNum]);
			CI_DATA_DEBUG(0, "        averagePacketSizePerQueue = %d",
				      uplinkStats->averagePacketSizePerQueue[queueNum]);
			CI_DATA_DEBUG(0, "        numTimesUplinkQueueWasEmpty = %d",
				      uplinkStats->numTimesUplinkQueueWasEmpty[queueNum]);
			CI_DATA_DEBUG(0, "        numPacketsDroppedByTrafficShaping = %d",
				      uplinkStats->numPacketsDroppedByTrafficShaping[cid]);

			CI_DATA_DEBUG(0, "        numPacketsSentPerQueue = %d",
				      uplinkStats->numPacketsSentPerQueue[queueNum]);
			CI_DATA_DEBUG(0, "        numPacketsThrownPerQueue = %d",
				      uplinkStats->numPacketsThrownPerQueue[queueNum]);
			CI_DATA_DEBUG(0, "        numPacketsThrownQosPerQueue = %d",
				      uplinkStats->numPacketsThrownQosPerQueue[queueNum]);
			/*CI_DATA_DEBUG(0, "             numTimesUplinkQueueWasFull=%ld", */
			/*              uplinkStats->numTimesUplinkQueueWasFull[queueNum]); */

			queueNum = CI_DATA_CID_TO_QUEUE_CONVERT(cid, CI_DATA_QUEUE_PRIORITY_HIGH);
			CI_DATA_DEBUG(0, "      High priority queue:");
			CI_DATA_DEBUG(0, "        maxPacketsPendingPerQueue = %d",
				      uplinkStats->maxPacketsPendingPerQueue[queueNum]);
			CI_DATA_DEBUG(0, "        averagePacketSizePerQueue = %d",
				      uplinkStats->averagePacketSizePerQueue[queueNum]);
			CI_DATA_DEBUG(0, "        numTimesUplinkQueueWasEmpty = %d",
				      uplinkStats->numTimesUplinkQueueWasEmpty[queueNum]);
			CI_DATA_DEBUG(0, "        numPacketsDroppedByTrafficShaping = %d",
				      uplinkStats->numPacketsDroppedByTrafficShaping[queueNum]);
			CI_DATA_DEBUG(0, "        numPacketsSentPerQueue = %d",
				      uplinkStats->numPacketsSentPerQueue[queueNum]);
			CI_DATA_DEBUG(0, "        numPacketsThrownPerQueue = %d",
				      uplinkStats->numPacketsThrownPerQueue[queueNum]);
			CI_DATA_DEBUG(0, "        numPacketsThrownQosPerQueue = %d",
				      uplinkStats->numPacketsThrownQosPerQueue[queueNum]);
			/*CI_DATA_DEBUG(0, "             numTimesUplinkQueueWasFull=%ld", */
			/*              uplinkStats->numTimesUplinkQueueWasFull[queueNum]); */

		}
	}

	CI_DATA_DEBUG(0, "  Uplink Mem Pool Debug Statistics:");
	CI_DATA_DEBUG(0, "    numAllocWasNull = %d", gCiDataUplinkPoolMem.numAllocWasNull);
	CI_DATA_DEBUG(0, "    currentNumAllocs = %d", gCiDataUplinkPoolMem.currentNumAllocs);
	CI_DATA_DEBUG(0, "    maxNumAllocs = %d", gCiDataUplinkPoolMem.maxNumAllocs);
	CI_DATA_DEBUG(0, "    numAllocWasNullCount = %d", gCiDataUplinkPoolMem.numAllocWasNullCount);
	CI_DATA_DEBUG(0, "    flowOffCount = %d", gCiDataUplinkPoolMem.flowOffCount);
	CI_DATA_DEBUG(0, "    flowOnCount = %d", gCiDataUplinkPoolMem.flowOnCount);
	CI_DATA_DEBUG(0, "    flowOffMaxTime = %d", gCiDataUplinkPoolMem.flowOffMaxTime);
	CI_DATA_DEBUG(0, "    flowOffSumTime = %d", gCiDataUplinkPoolMem.flowOffSumTime);
}

/***********************************************************************************\
*   Name:        CiDataPrintBasicCidInfo
*
*   Description: Print Basic CID info
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataPrintBasicCidInfo(void)
{
	int cid;
	unsigned int queueNumLow;
	unsigned int queueNumHigh;

	CiData_UplinkIndexesS uplinkIndexes;
	CiData_DownlinkIndexesS downlinkIndexes;

	CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(&gCiDataClient.pSharedArea->uplink,
					     sizeof(gCiDataClient.pSharedArea->uplink));
	CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(&gCiDataClient.pSharedArea->downlink,
					     sizeof(gCiDataClient.pSharedArea->downlink));

	memcpy(&uplinkIndexes, (void *)&(gCiDataClient.pSharedArea->uplink), sizeof(uplinkIndexes));
	memcpy(&downlinkIndexes, (void *)&(gCiDataClient.pSharedArea->downlink), sizeof(downlinkIndexes));

	for (cid = 0; cid < CI_DATA_TOTAL_CIDS; cid++) {
		if (gCiDataClient.cid[cid].state != CI_DATA_CID_STATE_CLOSED) {
			queueNumLow = CI_DATA_CID_TO_QUEUE_CONVERT(cid, CI_DATA_QUEUE_PRIORITY_LOW);
			queueNumHigh = CI_DATA_CID_TO_QUEUE_CONVERT(cid, CI_DATA_QUEUE_PRIORITY_HIGH);

			CI_DATA_DEBUG(0,
				      "cid=%d: LoP-Ps=%d, Pt=%d, Pq=%d, Pp=%d, HiP-Ps=%d, Pt=%d, Pq=%d, Pp=%d, DL-Pr=%d, Pp=%d",
				      cid, gCiDataDebugStatistics.uplink.numPacketsSentPerQueue[queueNumLow],
				      gCiDataDebugStatistics.uplink.numPacketsThrownPerQueue[queueNumLow],
				      gCiDataDebugStatistics.uplink.numPacketsThrownQosPerQueue[queueNumLow],
				      CI_DATA_UPLINK_INDEX_PEDNING_CALC(uplinkIndexes.nextWriteIdx[queueNumLow],
									uplinkIndexes.nextFreeIdx[queueNumLow]),
				      gCiDataDebugStatistics.uplink.numPacketsSentPerQueue[queueNumHigh],
				      gCiDataDebugStatistics.uplink.numPacketsThrownPerQueue[queueNumHigh],
				      gCiDataDebugStatistics.uplink.numPacketsThrownQosPerQueue[queueNumHigh],
				      CI_DATA_UPLINK_INDEX_PEDNING_CALC(uplinkIndexes.nextWriteIdx[queueNumHigh],
									uplinkIndexes.nextFreeIdx[queueNumHigh]),
				      gCiDataDebugStatistics.downlink.totalNumPacketsReceived[cid],
				      CI_DATA_DOWNLINK_INDEX_PEDNING_CALC(downlinkIndexes.nextWriteIdx[cid],
									  downlinkIndexes.nextReadIdx[cid]));
		}
	}
}

/***********************************************************************************\
*   Name:        CiDataPrintDatabase
*
*   Description: Print Ci Data internal database
*
*   Params:     printAll - TRUE=print all cid's, FALSE=only non-closed
*
*   Return:
\***********************************************************************************/
void CiDataPrintDatabase(unsigned char printAll)
{
	int cid;
	unsigned int queueNum;
	CiData_UplinkIndexesS uplinkIndexes;
	CiData_DownlinkIndexesS downlinkIndexes;
	char *cidStateStr[3] = { "closed", "primary", "secondary" };

	CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(&gCiDataClient.pSharedArea->uplink.nextReadIdx,
					     sizeof(gCiDataClient.pSharedArea->uplink.nextReadIdx));
	CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(&gCiDataClient.pSharedArea->uplink.nextReadCnfIdx,
					     sizeof(gCiDataClient.pSharedArea->uplink.nextReadCnfIdx));
	CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(&gCiDataClient.pSharedArea->downlink.nextWriteIdx,
					     sizeof(gCiDataClient.pSharedArea->downlink.nextWriteIdx));
	CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(&gCiDataClient.pSharedArea->downlink.nextFreeIdx,
					     sizeof(gCiDataClient.pSharedArea->downlink.nextFreeIdx));

	memcpy(&uplinkIndexes, (void *)&(gCiDataClient.pSharedArea->uplink), sizeof(uplinkIndexes));
	memcpy(&downlinkIndexes, (void *)&(gCiDataClient.pSharedArea->downlink), sizeof(downlinkIndexes));

	CI_DATA_DEBUG(0, "CI Data Client:");
	CI_DATA_DEBUG(0, "   linkStatusBits=%x, pSharedArea=%x", gCiDataClient.linkStatusBits,
		      (unsigned int)gCiDataClient.pSharedArea);
	CI_DATA_DEBUG(0, "   shmemConnected=%d, dataStatus=%d", gCiDataClient.shmemConnected, gCiDataClient.dataStatus);
	CI_DATA_DEBUG(0, "   uplinkDataStartNotifyRequired=%d", gCiDataClient.uplinkDataStartNotifyRequired);
	CI_DATA_DEBUG(0, "   ltmLocalDestIpAddress=0x%08X", gCiDataClient.mtil_globals_cfg.ltmLocalDestIpAddress);
	CI_DATA_DEBUG(0, "   Uplink buffer: baseAddress=%x, size=%d",
		      gCiDataClient.uplinkInfo.baseAddress, gCiDataClient.uplinkInfo.size);

	/*print only if not NULL */
	if (gCiDataClient.pSharedArea) {
		CI_DATA_DEBUG(0, "   CI Data Client CID's content:");
		for (cid = 0; cid < CI_DATA_TOTAL_CIDS; cid++) {
			/*check if print all, if not then print only non-closed */
			if ((printAll == TRUE) || (gCiDataClient.cid[cid].state != CI_DATA_CID_STATE_CLOSED)) {
				CI_DATA_DEBUG(0, "      CID = %d, state=%s, ipv4 addr=0x%08X",
					      cid, cidStateStr[gCiDataClient.cid[cid].state],
					      gCiDataClient.cid[cid].ipAddrs.ipv4addr);

				queueNum = CI_DATA_CID_TO_QUEUE_CONVERT(cid, CI_DATA_QUEUE_PRIORITY_LOW);
				CI_DATA_DEBUG(0, "        Uplink: LOW priority - queueNum=%d", queueNum);
				CI_DATA_DEBUG(0, "         nextWriteIdx=%d", uplinkIndexes.nextWriteIdx[queueNum]);
				CI_DATA_DEBUG(0, "         nextReadIdx=%d, nextReadCnfIdx=%d",
					      uplinkIndexes.nextReadIdx[queueNum],
					      uplinkIndexes.nextReadCnfIdx[queueNum]);
				CI_DATA_DEBUG(0, "         nextFreeIdx=%d", uplinkIndexes.nextFreeIdx[queueNum]);
				CI_DATA_DEBUG(0,
					      "         numPacketsSentPerQueue=%d, numPacketsThrownPerQueue=%d, numPacketsThrownQosPerQueue=%d",
					      gCiDataDebugStatistics.uplink.numPacketsSentPerQueue[queueNum],
					      gCiDataDebugStatistics.uplink.numPacketsThrownPerQueue[queueNum],
					      gCiDataDebugStatistics.uplink.numPacketsThrownQosPerQueue[queueNum]);

				queueNum = CI_DATA_CID_TO_QUEUE_CONVERT(cid, CI_DATA_QUEUE_PRIORITY_HIGH);
				CI_DATA_DEBUG(0, "        Uplink: HIGH priority - queueNum=%d", queueNum);
				CI_DATA_DEBUG(0, "         nextWriteIdx=%d", uplinkIndexes.nextWriteIdx[queueNum]);
				CI_DATA_DEBUG(0, "         nextReadIdx=%d, nextReadCnfIdx=%d",
					      uplinkIndexes.nextReadIdx[queueNum],
					      uplinkIndexes.nextReadCnfIdx[queueNum]);
				CI_DATA_DEBUG(0, "         nextFreeIdx=%d", uplinkIndexes.nextFreeIdx[queueNum]);
				CI_DATA_DEBUG(0,
					      "         numPacketsSentPerQueue=%d, numPacketsThrownPerQueue=%d, numPacketsThrownQosPerQueue=%d",
					      gCiDataDebugStatistics.uplink.numPacketsSentPerQueue[queueNum],
					      gCiDataDebugStatistics.uplink.numPacketsThrownPerQueue[queueNum],
					      gCiDataDebugStatistics.uplink.numPacketsThrownQosPerQueue[queueNum]);

				CI_DATA_DEBUG(0, "        Downlink: (baseAddress=%x, size=%d)",
					      gCiDataClient.downlinkQueueInfo.baseAddress,
					      gCiDataClient.downlinkQueueInfo.size);
				CI_DATA_DEBUG(0, "         nextWriteIdx=%d", downlinkIndexes.nextWriteIdx[cid]);
				CI_DATA_DEBUG(0, "         nextReadIdx=%d, nextReadCnfIdx=%d",
					      downlinkIndexes.nextReadIdx[cid], downlinkIndexes.nextReadCnfIdx[cid]);
				CI_DATA_DEBUG(0, "         nextFreeIdx=%d", downlinkIndexes.nextFreeIdx[cid]);
			}
		}
	}
}

/***********************************************************************************\
*   Name:        CiDataHandleControlCommand
*
*   Description: Handles control commands sent from terminal
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataHandleControlCommand(char type, char *params)
{
	unsigned int cidIndex;

	switch (type) {
	case 'D':
	case 'd':
		if ((gCiDataClient.dataStatus == CI_DATA_IS_ENABLED) || (gCiDataClient.dataStatus == CI_DATA_IS_DISBALED)) {
			/*force disable data */
			CI_DATA_DEBUG(0, "CiDataHandleControlCommand: Forcing DISABLING global data !!!");
			gCiDataClient.dataStatus = CI_DATA_IS_FORCED_DISABLED;
		} else {
			/*meaning: gCiDataClient.dataStatus == CI_DATA_IS_FORCED_DISABLED */
			/*re-enable data */
			CI_DATA_DEBUG(0, "CiDataHandleControlCommand: Un-Forcing DISABLING global data...");

			/*check if all cid's are disabled */
			for (cidIndex = 0; cidIndex < CI_DATA_TOTAL_CIDS; cidIndex++) {
				if (gCiDataClient.cid[cidIndex].state != CI_DATA_CID_STATE_CLOSED)
					break;
			}

			/*if all are disbaled - data is set to disable */
			if (cidIndex == CI_DATA_TOTAL_CIDS) {
				if (gCiDataClient.mtil_globals_cfg.mode == CI_DATA_HOST)
					CiDataClientLtmUnregister();
				gCiDataClient.dataStatus = CI_DATA_IS_DISBALED;
				CI_DATA_DEBUG(1,
					      "CiDataClientDataStart: Global data is now DISABLED (no active cid's)");
			} else {
				if (gCiDataClient.mtil_globals_cfg.mode == CI_DATA_HOST)
					CiDataClientLtmRegister();
				gCiDataClient.dataStatus = CI_DATA_IS_ENABLED;
				CI_DATA_DEBUG(1,
					      "CiDataClientDataStart: Global data is now ENABLED (some cid's active)");
			}
		}
		break;

	case 'P':
	case 'p':
		{
			int numMultiplePacket;
			sscanf((char *)params, "%d", &numMultiplePacket);
			if (numMultiplePacket == 0) {
				numMultiplePacket = CI_MAX_ITEMS_FOR_USB;	/*Default */
			}
			CI_DATA_DEBUG(0, "CiDataHandleControlCommand: Setting downlink max multiple packets to %d",
				      numMultiplePacket);
			ciDataMaxNumOfPacketsForUsb = numMultiplePacket;
		}
		break;
	default:
		CI_DATA_DEBUG(0, "CiDataHandleControlCommand: control command [%c] not supported", type);
		break;
	}

}

/***********************************************************************************\
*   Name:        CiDataFlushAllQueues
*
*   Description: Flush all Uplink & Downlink queues - for silent reset
*
*   Params:
*
*   Return:
\***********************************************************************************/
/*void CiDataFlushAllQueues(void)
{

}*/

/***********************************************************************************\
*   Name:        CiDataResetAllVariablesForSilentReset
*
*   Description: as the name says...
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataResetAllVariablesForSilentReset(void)
{
	int cid;

	/*reset local database - step by step... */
	gCiDataClient.linkStatusBits = 0;
	gCiDataClient.shmemConnected = 0;
	gCiDataClient.dataStatus = CI_DATA_IS_DISBALED;
	gCiDataClient.uplinkDataStartNotifyRequired = TRUE;
	gCiDataClient.uplinkIgnorePacketsDisabled = FALSE;
	gCiDataClient.downlinkAnalizePackets = 0;
	gCiDataClient.uplinkQosDisabled = FALSE;

	gCiDataClient.loopBackdataBase.status = CI_DATA_LOOPBACK_B_INACTIVE;

	/*reset cid database leftovers */
	for (cid = 0; cid < CI_DATA_TOTAL_CIDS; cid++) {
		ci_data_downlink_unregister(cid);
	}

	memset((void *)&gCiDataClient.pSharedArea->uplink, 0, sizeof(CiData_UplinkControlAreaDatabaseS));
	memset((void *)&gCiDataClient.pSharedArea->downlink, 0, sizeof(CiData_DownlinkControlAreaDatabaseS));

	/*reset statistics */
	memset(&gCiDataDebugStatistics, 0, sizeof(gCiDataDebugStatistics));

	/*Multi packet support */
	memset(&gCiDataDownlinkPointersList, 0, sizeof(gCiDataDownlinkPointersList));
	memset(&activeCids, 0, sizeof(activeCids));
	memset(&activeCids_Snapshot, 0, sizeof(activeCids_Snapshot));

	memset(&cidUsageCount, 0, sizeof(cidUsageCount));
	cidUsageCount.globalUsageCount = 1;

	ciDataMaxNumOfPacketsForUsb = CI_MAX_ITEMS_FOR_USB;

	/*BS - internal debug log */
	DBGLOG_INIT(cidata, CIDATA_DBG_LOG_SIZE);

	/*CiDataTrafficShapingInit(1, 1); */
}

static inline void CiDataFlushAllBusyPackets(void)
{
	int i;

	if (!gCiDataDownlinkPointersList.inProgress)
		return;

	CI_DATA_DEBUG(0, "CiDataFlushAllBusyPackets\n");
	for (i = 0; i < gCiDataDownlinkPointersList.numItemsOnList; i++) {
		fpdev_put(gCiDataDownlinkPointersList.ciDownlinkItemsForUsb[i]->fp_dst_dev);
		fpdev_put(gCiDataDownlinkPointersList.ciDownlinkItemsForUsb[i]->fp_src_dev);
	}
}

/***********************************************************************************\
*   Name:        CiDataHandleLinkDownForSilentReset
*
*   Description: Handle Link-Down to prepare for silent-reset
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataHandleLinkDownForSilentReset(void)
{
	int i;
	/*int     cid; */
	/*unsigned int  queueNum; */
	unsigned int flagBits;
	OSA_STATUS osaStatus;

	CI_DATA_DEBUG(0, "CiDataHandleLinkDownForSilentReset - Silent Reset Started\n");

	/*in silent reset process */
	gCiDataClient.inSilentReset = TRUE;

	/*first - close all open CID's */
	for (i = 0; i < activeCids.numActive; i++) {
		CiDataClientCidDisable(activeCids.cid[i]);
	}

	/*set SILENT_RESET flag for downlink task */
	osaStatus = FLAG_SET(&gCiDataClient.downlinkWaitFlagRef, CI_DATA_DOWNLINK_SILENT_RESET_FLAG_MASK, OSA_FLAG_OR);
	CI_DATA_ASSERT_MSG_P(0, osaStatus == OS_SUCCESS,
			     "CiDataHandleLinkDownForSilentReset: FLAG_SET error, osaStatus=%d", osaStatus);

	CI_DATA_DEBUG(0, "CiDataHandleLinkDownForSilentReset - waiting for task-flag\n");

	/*waiting for downlink-task to finish it's job */
	osaStatus = FLAG_WAIT(&gCiDataClient.silentResetWaitFlagRef, CI_DATA_SILENT_RESET_FLAG_MASK,
			      OSA_FLAG_OR_CLEAR, &flagBits, CI_DATA_SILENT_RESET_WAIT_TIMEOUT);

	CI_DATA_ASSERT_MSG_P(0, osaStatus == OS_SUCCESS,
			     "CiDataHandleLinkDownForSilentReset: Timeout on FLAG_WAIT, osaStatus=%d", osaStatus);

	CI_DATA_DEBUG(0, "CiDataHandleLinkDownForSilentReset - task flag received\n");

	itm_flush_all_pending_packets();

	CiDataFlushAllBusyPackets();

	CiDataResetAllVariablesForSilentReset();

	/*Clear the silent reset flag - just in case */
	osaStatus = FLAG_SET(&gCiDataClient.silentResetWaitFlagRef, 0, OSA_FLAG_AND);

	CiDataLinkStatusHandler(CI_DATA_CHANNEL_UPLINK, 1);	/*  Uplink uses a direct ACIPC event. */

	CI_DATA_DEBUG(0, "CiDataHandleLinkDownForSilentReset - Ended\n");
}

/***********************************************************************************\
*   Name:        CiDataLinkStatusHandler
*
*   Description: Handles the linkStatusInd for both directions (uplink/downlink)
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataLinkStatusHandler(CiData_ChannelTypeE channelType, unsigned char linkStatus)
{
	CI_DATA_DEBUG(2, "CiDataLinkStatusHandler channelType=%d, linkStatus=%d", channelType, linkStatus);

	/*LOCK */
	CI_DATA_LOCK(gCiDataClientShmemSpinlock);

	/*each channel has separated bit: control=0x1, uplink=0x2, downlink=0x4 */
	if (linkStatus) {
		/*LINK UP -> turn ON the relevant bit */
		gCiDataClient.linkStatusBits |= channelType;

		/*UNLOCK */
		CI_DATA_UNLOCK(gCiDataClientShmemSpinlock);

		/*check if all links are UP */
		if (gCiDataClient.linkStatusBits == CI_DATA_CHANNEL_ALL_LINK_UP_MASK) {
			CI_DATA_DEBUG(2, "CiDataLinkStatusHandler - CI Data Driver is connected!!!");

			/*memset((void *)&gCiDataClient.pSharedArea->uplink, 0, sizeof(CiData_ControlAreaDatabaseS)); */

			/*CiDataPathDownlinkTaskStart();//YG: temp remove only - due to the -tea from script */
			gCiDataClient.shmemConnected = TRUE;
		}
	} else {		/*LINK DOWN -> turn OFF the relevant bit */
		gCiDataClient.linkStatusBits &= ~channelType;

		/*UNLOCK */
		CI_DATA_UNLOCK(gCiDataClientShmemSpinlock);

		if (gCiDataClient.shmemConnected) {
			CiDataHandleLinkDownForSilentReset();
		}
		gCiDataClient.shmemConnected = FALSE;
	}
}

/***********************************************************************************\
*   Name:        CiDataUplinkMarkDroppedPacket
*
*   Description: Mark packet as drop - real drop depends on COMM
*
*   Params:      queueNum - queue number of the packet
*                index - index of packet in queue
*
*   Return:
\***********************************************************************************/
void CiDataUplinkMarkDroppedPacket(unsigned int queueNum, unsigned int index)
{
	CiData_UplinkControlAreaDatabaseS *uplink = &gCiDataClient.pSharedArea->uplink;
	CiData_UplinkDebugStatisticsS *uplinkStats = &gCiDataDebugStatistics.uplink;

	CI_DATA_DEBUG(2, "CiDataUplinkMarkDroppedPacket: queueNum=%d, index=%d", queueNum, index);

	if (!gCiDataClient.uplinkIgnorePacketsDisabled) {
		uplink->cidDescriptors[queueNum][index].packetDropped = TRUE;
	} else {
		uplink->cidDescriptors[queueNum][index].packetDropped = FALSE;
	}

	/*uplink->cidDescriptors[queueNum][index].packetDropped = FALSE;//YG: no dropping packets (debug) */
	uplinkStats->numPacketsDroppedByTrafficShaping[queueNum]++;
}

/***********************************************************************************\
*   Name:        CiDataDownlinkAnalizePackets
*
*   Description: Entry point for downlink packets analysis.
*                Note - This function is called after successfully sending
*                packets to USB
*
*   Params:
*
*
*
*   Return:
*
\***********************************************************************************/
static void CiDataDownlinkAnalizePackets(CiData_DownlinkPointersListS *downlinkPointersList)
{
	unsigned int i;

	unsigned int *pBufferStart;
	unsigned short bufferSize;
	unsigned char cid;

	for (i = 0; i < downlinkPointersList->numItemsOnList; i++) {
		cid = downlinkPointersList->ciDownlinkItemsForUsb[i]->cid;
		bufferSize = downlinkPointersList->ciDownlinkItemsForUsb[i]->packet_length;
		pBufferStart =
		    (unsigned int *)(((char *)(downlinkPointersList->ciDownlinkItemsForUsb[i]->buffer.pBufferStart)) +
				     downlinkPointersList->ciDownlinkItemsForUsb[i]->packetOffset);

		if (gCiDataClient.downlinkAnalizePackets
		    && CheckForIcmpv6RouterAdvertisment(pBufferStart, (unsigned int)bufferSize, (unsigned int)cid)) {
			if (gCiDataClient.downlinkAnalizePackets > 0)
				gCiDataClient.downlinkAnalizePackets--;
		}

	}
}

/***********************************************************************************\
*   Name:        CiDataDownlinkDataReceiveDefCB
*
*   Description: Default Downlink Data Receive callback function - for handling
*                packets received for an un-registered CID
*
*   Params:
*
*   Return:
\***********************************************************************************/
unsigned char CiDataDownlinkDataReceiveDefCB(unsigned int cid, void *packet_address, unsigned int packet_length)
{
	CI_DATA_DEBUG(0,
		      "CiDataDownlinkDataReceiveDefCB: ERROR - packet with no CID registered (cid=%d, address=%x, length=%d\n",
		      cid, (unsigned int)packet_address, packet_length);
	return FALSE;
	/*CI_DATA_ASSERT_MSG_P(0, FALSE, "CiDataDownlinkDataReceiveDefCB: ERROR - packet with no CID registered (cid=%d)!!!", cid); */
}

/*
 * Multi packet support
 * Static functions
 */
static unsigned int CiDataDownlinkFindOldestCid(CiData_DownlinkControlAreaDatabaseS *downlink,
						CiData_CidUsageCountS *cidUsageCount,
						CiData_DownlinkPointersListS *downlinkPointersList)
{
	unsigned int oldestCid = CI_DATA_TOTAL_CIDS;
	unsigned int maxDiff = 0;
	unsigned int cid;
	unsigned int lowestOffset = 0xFFFFFFFF;
	unsigned int bufferOffset;
	unsigned int cidOfLowestOffset = CI_DATA_TOTAL_CIDS;
	unsigned int indexDiff;
	unsigned char i;
	unsigned int nextRead, nextWrite;

	CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkFindOldestCid, DBG_LOG_FUNC_START, 0);

	for (i = 0; i < activeCids_Snapshot.numActive; i++) {
		cid = activeCids_Snapshot.cid[i];
		nextWrite = downlink->nextWriteIdx[cid];	/*downlinkPointersList->downlinkIndexes.nextWriteIdx[cid]; */
		nextRead = downlink->nextReadIdx[cid];	/*downlinkPointersList->downlinkIndexes.nextReadIdx[cid]; */
		if (nextWrite != nextRead) {
			/*if CID queue has something in it */
			if (gCiDataClient.cid[cid].state != CI_DATA_CID_STATE_CLOSED) {
				/*normal case */
				CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkFindOldestCid, 0x10, cid);
				CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkFindOldestCid, 0x11, nextRead);
				CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkFindOldestCid, 0x12, nextWrite);

				/*statistics */
				indexDiff = CI_DATA_DOWNLINK_INDEX_PEDNING_CALC(nextWrite, nextRead);
				if (indexDiff > gCiDataDebugStatistics.downlink.maxPacketsPendingPerCid[cid]) {
					gCiDataDebugStatistics.downlink.maxPacketsPendingPerCid[cid] = indexDiff;
				}
				indexDiff = CI_DATA_DOWNLINK_INDEX_PEDNING_CALC(nextWrite, nextRead);
				if (indexDiff > gCiDataDebugStatistics.downlink.maxPacketsNotFreedPerCid[cid]) {
					gCiDataDebugStatistics.downlink.maxPacketsNotFreedPerCid[cid] = indexDiff;
				}

				/*
				 * Must take care of numbers wrapping after 0xFFFFFFFF, so
				 * we subtract countPerCid from globalUsageCount to find maxDiff
				 */
				if (cidUsageCount->globalUsageCount - cidUsageCount->countPerCid[cid] > maxDiff) {
					CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkFindOldestCid, 0x13, cid);
					oldestCid = cid;
					maxDiff = cidUsageCount->globalUsageCount - cidUsageCount->countPerCid[cid];
					CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkFindOldestCid, 0x14, maxDiff);
					cidUsageCount->numItemsInOldestCidList = 0;
					cidUsageCount->oldestCidList[cidUsageCount->numItemsInOldestCidList++] =
					    oldestCid;
				} else if (cidUsageCount->globalUsageCount - cidUsageCount->countPerCid[cid] == maxDiff) {
					CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkFindOldestCid, 0x15, cid);
					cidUsageCount->oldestCidList[cidUsageCount->numItemsInOldestCidList++] = cid;

					CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(&downlink->cidDescriptors[cid][nextRead],
									     sizeof(CiData_DownlinkDescriptorS));
					bufferOffset = downlink->cidDescriptors[cid][nextRead].buffer.bufferOffset;
					if (bufferOffset < lowestOffset) {
						lowestOffset = bufferOffset;
						cidOfLowestOffset = cid;
						CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkFindOldestCid, 0x17, cid);
					}
				}
			} else {	/*CID is closed and packets are pending -> need to clean downlink buffer */
				CI_DATA_DEBUG(1,
					      "CiDataDownlinkFindOldestCid: cid=%d CLOSED but downlink packets pending...cleaning it!",
					      cid);

				/*done in this order on purpose since need to make sure that ReadCnfIdx is not greater than ReadIdx */
				downlink->nextReadIdx[cid] = downlink->nextWriteIdx[cid];
				downlink->nextReadCnfIdx[cid] = downlink->nextReadIdx[cid];

				CI_DATA_DESCRIPTORS_CACHE_FLUSH(&downlink->nextReadIdx, sizeof(downlink->nextReadIdx));	/* Do we need to flush this? */
				CI_DATA_DESCRIPTORS_CACHE_FLUSH(&downlink->nextReadCnfIdx,
								sizeof(downlink->nextReadCnfIdx));

			}
		}
	}

	CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkFindOldestCid, 0x20, 0);
	/*Check - behavior not correct. cidOfLowestOffset might not be correct */
	if (cidUsageCount->numItemsInOldestCidList > 1) {
		oldestCid = cidOfLowestOffset;
		CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkFindOldestCid, 0x21, cidOfLowestOffset);
	}
	cidUsageCount->oldestCid = oldestCid;

	CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkFindOldestCid, DBG_LOG_FUNC_END, oldestCid);

	return oldestCid;
}

static void CiDataUpdateCidUsageCount(unsigned int cid, CiData_CidUsageCountS *cidUsageCount)
{
	CIDATA_DEBUG_LOG2(MDB_CiDataUpdateCidUsageCount, DBG_LOG_FUNC_START, cid);
	cidUsageCount->countPerCid[cid] = cidUsageCount->globalUsageCount;
	cidUsageCount->globalUsageCount++;
	CIDATA_DEBUG_LOG2(MDB_CiDataUpdateCidUsageCount, DBG_LOG_FUNC_END, cidUsageCount->globalUsageCount);
}

static inline unsigned char
CiDataSendPacketToFastPath(CiData_DownlinkControlAreaDatabaseS *downlink,
			   unsigned int cid, CiData_DownlinkDescriptorS *descriptor, struct fp_net_device *fp_dst_dev)
{
	int ret;
	struct fp_packet_details *pkt_details;
	struct fp_net_device *fp_src_dev;
	BUG_ON(!fp_dst_dev || !fp_dst_dev->tx_cb);

	pkt_details = (struct fp_packet_details *)descriptor;
	pkt_details->fp_src_dev = fp_src_dev = gCiDataClient.cid[cid].fp_dev;
	pkt_details->fp_dst_dev = fp_dst_dev;

	ret = fp_dst_dev->tx_cb(pkt_details);

	if (likely(ret == NETDEV_TX_OK)) {
		fp_src_dev->stats.rx_packets++;
		fp_src_dev->stats.rx_bytes += pkt_details->packet_length;
	} else if (ret == NET_XMIT_DROP) {
		fp_src_dev->stats.rx_dropped++;
		CiDataClientDownlinkFreePacket(cid, descriptor);
	} else {
		fp_src_dev->stats.rx_errors++;
		/*TODO: handle this case */
		BUG();
	}
	return TRUE;
}

static unsigned char CiDataSendPacketToSlowPath(CiData_DownlinkControlAreaDatabaseS *downlink,
						unsigned int cid, CiData_DownlinkDescriptorS *descriptor)
{
	unsigned char bufferHandled;
	unsigned char *data_ptr;
	unsigned int primCid;

	primCid = CiDataTftGetPrimaryBySecondary(cid);
	CI_DATA_DEBUG(3, "CiDataDownlinkTask: convert cid=%d to primary cid=%d", cid, primCid);
	CIDATA_DEBUG_LOG2(MDB_CiDataSendPacket, 0x10, primCid);

	data_ptr = descriptor->buffer.pBufferStart + descriptor->packetOffset;

	/*callback the user */
	bufferHandled =
	    gCiDataClient.cid[primCid].downlink_data_receive_cb(primCid, (void *)data_ptr, descriptor->packet_length);

	if (bufferHandled) {
		CIDATA_DEBUG_LOG2(MDB_CiDataSendPacket, 0x21, cid);
		/*packet is read */
		CI_DATA_DOWNLINK_INDEX_INCREMENT(downlink->nextReadIdx[cid]);
		CI_DATA_DESCRIPTORS_CACHE_FLUSH(&downlink->nextReadIdx, sizeof(downlink->nextReadIdx));

		/*in case of NOT HOST_IP - data is copied immediately */
		if (gCiDataClient.cid[cid].serviceType != CI_DATA_SVC_TYPE_PDP_HOSTIP) {
			CIDATA_DEBUG_LOG2(MDB_CiDataSendPacket, 0x25, cid);
			CiDataClientDownlinkFreePacket(cid, descriptor);
		} else {
			BUG();
		}

	} else if (gCiDataClient.loopBackdataBase.status != CI_DATA_LOOPBACK_B_DELAYED) {
		BUG();
	}

	return bufferHandled;
}

static unsigned char CiDataSendPacket(CiData_DownlinkControlAreaDatabaseS *downlink,
				      unsigned int cid, struct fp_net_device *fp_dst_dev)
{
	unsigned char bufferHandled;

	CiData_DownlinkDescriptorS *descriptor;

	CIDATA_DEBUG_LOG2(MDB_CiDataSendPacket, DBG_LOG_FUNC_START, cid);

	descriptor = &downlink->cidDescriptors[cid][downlink->nextReadIdx[cid]];

	CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(descriptor, sizeof(CiData_DownlinkDescriptorS));

	descriptor->buffer.pBufferStart =
	    (void *)(gCiDataClient.downlinkQueueInfo.baseAddress + descriptor->buffer.bufferOffset);

	CI_DATA_DESCRIPTORS_CACHE_FLUSH(descriptor, sizeof(CiData_DownlinkDescriptorS));

	CI_DATA_CACHE_INVALIDATE(descriptor->buffer.pBufferStart + descriptor->packetOffset, descriptor->packet_length);

	CI_DATA_DEBUG(4, "CiDataDownlinkTask: RX packet, cid=%d, address=%p, packet_length=%d",
		      cid, descriptor->buffer.pBufferStart + descriptor->packetOffset, descriptor->packet_length);

	CIDATA_DEBUG_LOG2(MDB_CiDataSendPacket, 1, descriptor->buffer.pBufferStart + descriptor->packetOffset);
	CIDATA_DEBUG_LOG2(MDB_CiDataSendPacket, 2, descriptor->packet_length);

#if (CI_DATA_DEBUG_LEVEL >= 6)
	{
		int i;
		u8 *data_ptr = descriptor->buffer.pBufferStart + descriptor->packetOffset;

		pr_err("\n\nDBG: Downlink buffer cid=%d, ptr=%p, len=%d\n",
		       CiDataTftGetPrimaryBySecondary(cid),
		       descriptor->buffer.pBufferStart + descriptor->packetOffset, descriptor->packet_length);

		for (i = 0; i < descriptor->packet_length; i++) {
			pr_err("%x, ", data_ptr[i]);
		}
		pr_err("\nDBG: Buffer end\n");
	}
#endif /*CI_DATA_DEBUG_LEVEL */

	if (fp_dst_dev == NULL) {
		bufferHandled = CiDataSendPacketToSlowPath(downlink, cid, descriptor);
	} else {
		BUG();		/* TODO: Remove in adifferent commit / *for MiFi demo we use an optimized function call for fastpath * / */
		bufferHandled = CiDataSendPacketToFastPath(downlink, cid, descriptor, fp_dst_dev);
		/*confirm that packet is read */
		CI_DATA_DOWNLINK_INDEX_INCREMENT(downlink->nextReadIdx[cid]);
		CI_DATA_DESCRIPTORS_CACHE_FLUSH(&downlink->nextReadIdx, sizeof(downlink->nextReadIdx));
	}

	CIDATA_DEBUG_LOG2(MDB_CiDataSendPacket, DBG_LOG_FUNC_END, cid);
	return bufferHandled;
}

static inline struct fp_net_device *ci_classify(CiData_DownlinkDescriptorS *desc)
{
	unsigned char *packet;
	unsigned int packetSize;
	struct fp_net_device *dst;

	if (gCiDataClient.inSilentReset)
		return NULL;

	CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(desc, sizeof(*desc));

	if (unlikely(desc->fp_src_dev))
		return desc->fp_dst_dev;

	packet = (unsigned char *)(gCiDataClient.downlinkQueueInfo.baseAddress +
				   desc->buffer.bufferOffset + desc->packetOffset);
	packetSize = desc->packet_length;
	CI_DATA_CACHE_INVALIDATE(packet, packetSize);	/*maybe better to invalidate the header only? */

	BUG_ON(desc->cid > CI_DATA_TOTAL_CIDS);
	BUG_ON(!gCiDataClient.cid[desc->cid].fp_dev);
	dst = gCiDataClient.fp_classify_f(gCiDataClient.cid[desc->cid].fp_dev, packet, packetSize, true);
	desc->fp_dst_dev = dst;
	desc->fp_src_dev = gCiDataClient.cid[desc->cid].fp_dev;
	CI_DATA_DESCRIPTORS_CACHE_FLUSH(desc, sizeof(*desc));
	return dst;
}

/***********************************************************************************\
*   Name:        CiDataDownlinkDataSendPacket
*
*   Description: Send a single packet (through slow path or fast path)
*
*   Params: fp_dst_dev - NULL - send to slow path
*   			 otherwise - send to fastpath output interface
*
*   Return:
\***********************************************************************************/

static unsigned char CiDataDownlinkDataSendPacket(CiData_DownlinkControlAreaDatabaseS *downlink, unsigned int cid,
						  struct fp_net_device *fp_dst_dev)
{
	unsigned char ret = FALSE;

	BUG_ON(gCiDataClient.cid[cid].serviceType == CI_DATA_SVC_TYPE_PDP_HOSTIP);	/* [[TE]] check if needed */

	CI_DATA_DEBUG(2, "serviceType[%d] = %d, send single packet", cid, gCiDataClient.cid[cid].serviceType);

	CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, 0x11, gCiDataClient.cid[cid].serviceType);

	ret = CiDataSendPacket(downlink, cid, fp_dst_dev);
	if (ret == TRUE) {
		/*success */
		CiDataUpdateCidUsageCount(cid, &cidUsageCount);
	} else {
		CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, 0x12, 0);
	}

	return ret;
}

static unsigned int CiDataFindNextItem(CiData_DownlinkControlAreaDatabaseS *downlink,
				       CiData_DownlinkPointersListS *downlinkPointersList,
				       struct fp_net_device *fp_dst_dev_prev)
{
	unsigned int cid;
	unsigned int rc = FOUND_END_ITEM;
	unsigned char i;
	unsigned int nextOffset;
	unsigned int nextRead, nextWrite, bufferOffset;
	unsigned short bufferSize;
	struct fp_net_device *fp_dst_dev;

#if defined (FEATURE_CI_DATA_DOWNLINK_MPACKET_STATISTICS)
	unsigned int debugCidOfNonEmptyQueue = CI_DATA_TOTAL_CIDS;
#endif /*FEATURE_CI_DATA_DOWNLINK_MPACKET_STATISTICS */

	/*just for safekeeping */
	ASSERT(downlinkPointersList->numItemsOnList != 0);

	CIDATA_DEBUG_LOG2(MDB_CiDataFindNextItem, DBG_LOG_FUNC_START, downlinkPointersList->numItemsOnList);

	/* Get next offset */
	nextOffset =
	    downlinkPointersList->ciDownlinkItemsForUsb[downlinkPointersList->numItemsOnList - 1]->buffer.bufferOffset +
	    downlinkPointersList->ciDownlinkItemsForUsb[downlinkPointersList->numItemsOnList - 1]->bufferSize;

	CIDATA_DEBUG_LOG2(MDB_CiDataFindNextItem, 1, nextOffset);

	/*
	 * The following loop searches for the CID with the lowest buffer offset
	 * at its NextRead index
	 */
	for (i = 0; i < activeCids_Snapshot.numActive; i++) {
		cid = activeCids_Snapshot.cid[i];
		nextRead = downlinkPointersList->tempReadIdx[cid];
		nextWrite = downlink->nextWriteIdx[cid];	/*downlinkPointersList->downlinkIndexes.nextWriteIdx[cid]; */

		/*if queue contains something */
		if (nextWrite != nextRead) {
#if defined (FEATURE_CI_DATA_DOWNLINK_MPACKET_STATISTICS)
			debugCidOfNonEmptyQueue = cid;
#endif /*FEATURE_CI_DATA_DOWNLINK_MPACKET_STATISTICS */

			CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(&downlink->cidDescriptors[cid][nextRead],
							     sizeof(CiData_DownlinkDescriptorS));

			bufferOffset = downlink->cidDescriptors[cid][nextRead].buffer.bufferOffset;
			bufferSize = downlink->cidDescriptors[cid][nextRead].bufferSize;

			/*In HOST mode, Only look for items of type CI_DATA_SVC_TYPE_PDP_HOSTIP */
			if ((gCiDataClient.mtil_globals_cfg.mode == CI_DATA_HOST) &&
			    (gCiDataClient.cid[cid].serviceType != CI_DATA_SVC_TYPE_PDP_HOSTIP)) {
				continue;
			}
			/* Packets are added if answer the following: */
			/* - found next packet in a row */
			/* - total size plus found buffer size is less than 16K */
			/* - it's dest_fp_dev is the same as the previous ones */
			/* - number of items is less than ciDataMaxNumOfPacketsForUsb (default is CI_MAX_ITEMS_FOR_USB) */
			if (bufferOffset == nextOffset) {

				fp_dst_dev = ci_classify(&downlink->cidDescriptors[cid][nextRead]);

				if (!fp_dst_dev || (fp_dst_dev->mul_support == FALSE)
				    || (fp_dst_dev_prev != fp_dst_dev))
					break;

				if (downlinkPointersList->totalSize + bufferSize >= CI_MAX_DATA_SIZE_FOR_USB)
					break;

				/* Found LIST item -> add to list */
				downlinkPointersList->ciDownlinkItemsForUsb[downlinkPointersList->numItemsOnList] =
				    &downlink->cidDescriptors[cid][nextRead];
				downlinkPointersList->cidsInList[cid]++;
				downlinkPointersList->totalSize += bufferSize;
				CI_DATA_DOWNLINK_INDEX_INCREMENT(downlinkPointersList->tempReadIdx[cid]);

				downlinkPointersList->numItemsOnList++;

				CIDATA_DEBUG_LOG2(MDB_CiDataFindNextItem, 0x19, downlinkPointersList->numItemsOnList);

				rc = FOUND_LIST_ITEM;
				break;
			}
		}
	}

#if defined (FEATURE_CI_DATA_DOWNLINK_MPACKET_STATISTICS)
	if (rc != FOUND_LIST_ITEM) {
		if (debugCidOfNonEmptyQueue == CI_DATA_TOTAL_CIDS) {
			gCiDataDebugStatistics.downlink.numListReachedQueueEmpty++;
		} else if (downlinkPointersList->totalSize +
			   downlink->cidDescriptors[debugCidOfNonEmptyQueue][downlinkPointersList->tempReadIdx
									     [debugCidOfNonEmptyQueue]].bufferSize >=
			   CI_MAX_DATA_SIZE_FOR_USB) {
			gCiDataDebugStatistics.downlink.numListReachedMaxSize++;
		} else if ((gCiDataClient.mtil_globals_cfg.mode == CI_DATA_HOST) &&
			   (gCiDataClient.cid[debugCidOfNonEmptyQueue].serviceType != CI_DATA_SVC_TYPE_PDP_HOSTIP)) {
			gCiDataDebugStatistics.downlink.numListReachedNonHostip++;
		} else if (downlink->cidDescriptors[debugCidOfNonEmptyQueue]
			   [downlinkPointersList->tempReadIdx[debugCidOfNonEmptyQueue]].buffer.bufferOffset >
			   nextOffset) {
			gCiDataDebugStatistics.downlink.numListReachedOffsetLimit++;
			CI_DATA_DEBUG(2,
				      "CiDataFindNextItem: next offset isn't adjacent to last (current offset = %08X, size = %d, next offset = %08X",
				      downlink->cidDescriptors[debugCidOfNonEmptyQueue][downlinkPointersList->
											tempReadIdx
											[debugCidOfNonEmptyQueue]].
									buffer.bufferOffset,
				      downlink->cidDescriptors[debugCidOfNonEmptyQueue][downlinkPointersList->
											tempReadIdx
											[debugCidOfNonEmptyQueue]].
									bufferSize, nextOffset);
		}
/*		else
		{
			CI_DATA_DEBUG(1, "CiDataFindNextItem: exit loop for unknown reason");
		}*/
	}
#endif /* FEATURE_CI_DATA_DOWNLINK_MPACKET_STATISTICS */

	CIDATA_DEBUG_LOG2(MDB_CiDataFindNextItem, DBG_LOG_FUNC_END, rc);
	return rc;
}

static unsigned int CiDataFindFirstItem(CiData_DownlinkControlAreaDatabaseS *downlink,
					CiData_DownlinkPointersListS *downlinkPointersList,
					struct fp_net_device **fp_dst_dev)
{
	unsigned int rc = FOUND_NONE;
	unsigned int cid;
	unsigned int lowestOffset;
	unsigned int cidOfLowestOffset;
	unsigned int tmpWriteIdx[CI_DATA_TOTAL_CIDS];
	unsigned int tmpReadIdx[CI_DATA_TOTAL_CIDS];
	unsigned char i;
	unsigned int nextRead, nextWrite, bufferOffset;

	CIDATA_DEBUG_LOG2(MDB_CiDataFindFirstItem, DBG_LOG_FUNC_START, 0);

	downlinkPointersList->numItemsOnList = 0;
	downlinkPointersList->inProgress = TRUE;

	memcpy((void *)tmpWriteIdx, (void *)downlink->nextWriteIdx, sizeof(tmpWriteIdx));
	memcpy((void *)tmpReadIdx, (void *)downlink->nextReadIdx, sizeof(tmpReadIdx));

	do {
		lowestOffset = 0xFFFFFFFF;
		cidOfLowestOffset = CI_DATA_TOTAL_CIDS;
		*fp_dst_dev = NULL;
		/*
		 * The following loop searches for the CID with the lowest buffer offset
		 * at its NextRead index
		 */
		for (i = 0; i < activeCids_Snapshot.numActive; i++) {
			cid = activeCids_Snapshot.cid[i];
			nextRead = tmpReadIdx[cid];
			nextWrite = tmpWriteIdx[cid];

			/*first, init temp read indexes */
			downlinkPointersList->tempReadIdx[cid] = nextRead;

			/*In HOST mode, Only look for items of type CI_DATA_SVC_TYPE_PDP_HOSTIP */
			if ((gCiDataClient.mtil_globals_cfg.mode == CI_DATA_HOST) &&
			    (gCiDataClient.cid[cid].serviceType != CI_DATA_SVC_TYPE_PDP_HOSTIP)) {
				continue;
			}

			if (nextWrite != nextRead) {
				CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(&downlink->cidDescriptors[cid][nextRead],
								     sizeof(CiData_DownlinkDescriptorS));
				bufferOffset = downlink->cidDescriptors[cid][nextRead].buffer.bufferOffset;
				if (bufferOffset < lowestOffset) {
					lowestOffset = bufferOffset;
					cidOfLowestOffset = cid;
				}
			}
		}

		CIDATA_DEBUG_LOG2(MDB_CiDataFindFirstItem, 0x10, cidOfLowestOffset);

		if (cidOfLowestOffset == CI_DATA_TOTAL_CIDS)	/* found none */
			break;

		nextRead = tmpReadIdx[cidOfLowestOffset];	/*downlinkPointersList->downlinkIndexes.nextReadIdx[cidOfLowestOffset]; */
		BUG_ON(cidOfLowestOffset != downlink->cidDescriptors[cidOfLowestOffset][nextRead].cid);
		*fp_dst_dev = ci_classify(&downlink->cidDescriptors[cidOfLowestOffset][nextRead]);

		if (*fp_dst_dev && (*fp_dst_dev)->mul_support == TRUE) {
			/* found one */
			CIDATA_DEBUG_LOG2(MDB_CiDataFindFirstItem, 0x19, 0);
			rc = FOUND_LIST_ITEM;
			break;
		} else {
			/*Send the packet to slow/fast path, otherwise it is dropped! */
			/* TODO: revise this - can we send the packet to the fastpath in this case? */
			if (CiDataDownlinkDataSendPacket(downlink, cidOfLowestOffset, *fp_dst_dev) == FALSE) {
				BUG();	/*TODO - handle this */
			}
			/* to prevent endless loop */
			CI_DATA_DOWNLINK_INDEX_INCREMENT(tmpReadIdx[cidOfLowestOffset]);
		}
	} while (1);

	if (rc == FOUND_LIST_ITEM) {
		downlinkPointersList->ciDownlinkItemsForUsb[downlinkPointersList->numItemsOnList] =
		    &downlink->cidDescriptors[cidOfLowestOffset][nextRead];
		downlinkPointersList->cidsInList[cidOfLowestOffset]++;
		downlinkPointersList->totalSize = downlink->cidDescriptors[cidOfLowestOffset][nextRead].bufferSize;
		CI_DATA_DOWNLINK_INDEX_INCREMENT(downlinkPointersList->tempReadIdx[cidOfLowestOffset]);
		downlinkPointersList->numItemsOnList++;
	} else {
		/* rc == FOUND_NONE */
		gCiDataDebugStatistics.downlink.numListBuildFailedOnFirstItem++;
	}

	CIDATA_DEBUG_LOG2(MDB_CiDataFindFirstItem, DBG_LOG_FUNC_END, rc);

	return rc;
}

static inline unsigned int CiDataIsOldestCidInList(CiData_CidUsageCountS *cidUsageCount,
						   CiData_DownlinkPointersListS *downlinkPointersList)
{
	return downlinkPointersList->cidsInList[cidUsageCount->oldestCid];
}

static unsigned int CiDataGetListFromCid(unsigned int oldestCid,
					 CiData_DownlinkControlAreaDatabaseS *downlink,
					 CiData_DownlinkPointersListS *downlinkPointersList)
{

	unsigned int isFound;
	unsigned int nextRead, nextWrite;
	struct fp_net_device *fp_dst_dev;

	nextRead = downlink->nextReadIdx[oldestCid];	/*downlinkPointersList->downlinkIndexes.nextReadIdx[oldestCid]; */
	nextWrite = downlink->nextWriteIdx[oldestCid];	/*downlinkPointersList->downlinkIndexes.nextWriteIdx[oldestCid]; */

	CIDATA_DEBUG_LOG2(MDB_CiDataGetListFromCid, DBG_LOG_FUNC_START, oldestCid);
	if (nextWrite == nextRead) {
		/*ASSERT(FALSE); // is this scenario legal?? */
		CIDATA_DEBUG_LOG2(MDB_CiDataGetListFromCid, 0xF0, oldestCid);
		CIDATA_DEBUG_LOG2(MDB_CiDataGetListFromCid, DBG_LOG_FUNC_END, 0);
		return FOUND_NONE;
	}
	/*New List */
	CIDATA_DEBUG_LOG2(MDB_CiDataGetListFromCid, 0x1, 0);
	memset(downlinkPointersList->cidsInList, 0,
	       sizeof(downlinkPointersList->cidsInList) / sizeof(*downlinkPointersList->cidsInList));
	downlinkPointersList->numItemsOnList = 0;
	downlinkPointersList->tempReadIdx[oldestCid] = nextRead;

	fp_dst_dev = ci_classify(&downlink->cidDescriptors[oldestCid][nextRead]);

	if (!fp_dst_dev || fp_dst_dev->mul_support == FALSE) {
		CIDATA_DEBUG_LOG2(MDB_CiDataGetListFromCid, DBG_LOG_FUNC_END, 0);
		return FOUND_NONE;
	}

	downlinkPointersList->ciDownlinkItemsForUsb[downlinkPointersList->numItemsOnList] =
	    &downlink->cidDescriptors[oldestCid][nextRead];
	downlinkPointersList->cidsInList[oldestCid]++;
	downlinkPointersList->totalSize = downlink->cidDescriptors[oldestCid][nextRead].bufferSize;
	CI_DATA_DOWNLINK_INDEX_INCREMENT(downlinkPointersList->tempReadIdx[oldestCid]);

	downlinkPointersList->numItemsOnList++;

	do {
		isFound = CiDataFindNextItem(downlink, downlinkPointersList, fp_dst_dev);
	} while ((isFound == FOUND_LIST_ITEM) && (downlinkPointersList->numItemsOnList < ciDataMaxNumOfPacketsForUsb));

	CIDATA_DEBUG_LOG2(MDB_CiDataGetListFromCid, DBG_LOG_FUNC_END, downlinkPointersList->numItemsOnList);
	return FOUND_END_ITEM;
}

static inline void CiDataDownlinkUpdateListStatistics(unsigned int total_size, unsigned int numItemsOnList)
{
	gCiDataDebugStatistics.downlink.averageTotalSize =
	    (gCiDataDebugStatistics.downlink.averageTotalSize + total_size) / 2;
	gCiDataDebugStatistics.downlink.averageNumPackets =
	    (gCiDataDebugStatistics.downlink.averageNumPackets + numItemsOnList) / 2;
	CIDATA_DEBUG_LOG2(MDB_CiDataUpdateBufferStartPointersFromOffset, DBG_LOG_FUNC_END, numItemsOnList);
}

/*
 * Prepare multiple packet list for Tx
 *
 * For each item in the list:
 * 1. replace buffer offset with "real" address
 * 2. Update the fastpath source device pointer
 * Point last item to NULL (USB-ETH driver requirement)
 */
static void CiDataUpdateMultiplePacketList(CiData_DownlinkPointersListS *downlinkPointersList)
{
	unsigned int i;

	CIDATA_DEBUG_LOG2(MDB_CiDataUpdateBufferStartPointersFromOffset, DBG_LOG_FUNC_START, 0);
	for (i = 0; i < downlinkPointersList->numItemsOnList; i++) {
		CIDATA_DEBUG_LOG2(MDB_CiDataUpdateBufferStartPointersFromOffset, i + 1,
				  downlinkPointersList->ciDownlinkItemsForUsb[i]->buffer.bufferOffset);

		downlinkPointersList->ciDownlinkItemsForUsb[i]->buffer.pBufferStart =
		    (void *)(gCiDataClient.downlinkQueueInfo.baseAddress +
			     downlinkPointersList->ciDownlinkItemsForUsb[i]->buffer.bufferOffset);
		downlinkPointersList->ciDownlinkItemsForUsb[i]->fp_src_dev =
		    gCiDataClient.cid[downlinkPointersList->ciDownlinkItemsForUsb[i]->cid].fp_dev;

		CI_DATA_DESCRIPTORS_CACHE_FLUSH(downlinkPointersList->ciDownlinkItemsForUsb[i],
						sizeof(CiData_DownlinkDescriptorS));
	}

	/* Not needed - every packet is classified before begin added to the list, hence invalidated. Keaving this invalidate here */
	/* undos all the NAT we did on the packet!! */
	/* CI_DATA_CACHE_INVALIDATE((void *)downlinkPointersList->ciDownlinkItemsForUsb [0]->buffer.pBufferStart, downlinkPointersList->totalSize); */

	/* last item should point to NULL */
	downlinkPointersList->ciDownlinkItemsForUsb[i] = NULL;
}

static void CiDataRestoreTempReadIndexes(CiData_DownlinkControlAreaDatabaseS *downlink,
					 CiData_DownlinkPointersListS *downlinkPointersList)
{
	unsigned int i;
	unsigned int cid;

	CIDATA_DEBUG_LOG2(MDB_CiDataRestoreTempReadIndexes, DBG_LOG_FUNC_START, 0);
	for (i = 0; i < downlinkPointersList->numItemsOnList; i++) {
		cid = downlinkPointersList->ciDownlinkItemsForUsb[i]->cid;
		downlinkPointersList->tempReadIdx[cid] = downlink->nextReadIdx[cid];
	}
	CIDATA_DEBUG_LOG2(MDB_CiDataRestoreTempReadIndexes, DBG_LOG_FUNC_END, 0);
}

/*
 * TODO - CHANGE DESCRIPTION!!!!!!
 *
 * IF oldestCid is CI_DATA_SVC_TYPE_PDP_HOSTIP, then
 * 1. Try to build a list of packets which memory addresses are continuous
 *    as long as it doesn't exeed 16KBytes in total length, contains less than
 *    CI_MAX_ITEMS_FOR_USB items of CI_DATA_SVC_TYPE_PDP_HOSTIP type
 * 2. If the list contains the oldestCid, send as is
 * 3. If the list does not contain the oldestCid, try to build a list of items
 *    that contain one or more packets from oldestCid, as long as it doesn't exeed
 *    16KBytes in total length, contains less than CI_MAX_ITEMS_FOR_USB items
 *    of CI_DATA_SVC_TYPE_PDP_HOSTIP type
 */
static unsigned int CiDataBuildListOfPackets(CiData_DownlinkControlAreaDatabaseS *downlink,
					     CiData_CidUsageCountS *cidUsageCount,
					     CiData_DownlinkPointersListS *downlinkPointersList)
{
	struct fp_net_device *fp_dst_dev;
	unsigned int isFound;

	CIDATA_DEBUG_LOG2(MDB_CiDataBuildListOfPackets, DBG_LOG_FUNC_START, 0);
	/*Find first highest address */
	isFound = CiDataFindFirstItem(downlink, downlinkPointersList, &fp_dst_dev);
	CIDATA_DEBUG_LOG2(MDB_CiDataBuildListOfPackets, 1, isFound);

	if (isFound == FOUND_NONE) {
		CIDATA_DEBUG_LOG2(MDB_CiDataBuildListOfPackets, 0xF0, 0);
		CIDATA_DEBUG_LOG2(MDB_CiDataBuildListOfPackets, DBG_LOG_FUNC_END, 0);
		return 0;
	} else {
		do {
			isFound = CiDataFindNextItem(downlink, downlinkPointersList, fp_dst_dev);
		} while ((isFound == FOUND_LIST_ITEM)
			 && (downlinkPointersList->numItemsOnList < ciDataMaxNumOfPacketsForUsb));

		/* Statistics */
		if (downlinkPointersList->numItemsOnList >= ciDataMaxNumOfPacketsForUsb) {
			gCiDataDebugStatistics.downlink.numListReachedMaxPackets++;
		}
	}

	CIDATA_DEBUG_LOG2(MDB_CiDataBuildListOfPackets, 0x10, 0);

	if (CiDataIsOldestCidInList(cidUsageCount, downlinkPointersList)) {
		CI_DATA_DEBUG(2, "oldest CID is in list");
		CIDATA_DEBUG_LOG2(MDB_CiDataBuildListOfPackets, 0x11, 0);
		CiDataUpdateMultiplePacketList(downlinkPointersList);
		CiDataDownlinkUpdateListStatistics(downlinkPointersList->totalSize,
						   downlinkPointersList->numItemsOnList);
	} else {
		CI_DATA_DEBUG(2, "oldest CID is NOT in list - create list starting from oldest CID");
		CIDATA_DEBUG_LOG2(MDB_CiDataBuildListOfPackets, 0x15, cidUsageCount->oldestCid);
		CiDataRestoreTempReadIndexes(downlink, downlinkPointersList);
		/*List doesn't contain oldestCid, so try to build a list of one or more items from oldestCid */
		isFound = CiDataGetListFromCid(cidUsageCount->oldestCid, downlink, downlinkPointersList);
		CIDATA_DEBUG_LOG2(MDB_CiDataBuildListOfPackets, 0x17, 0);

		if (isFound == FOUND_NONE) {
			CIDATA_DEBUG_LOG2(MDB_CiDataBuildListOfPackets, 0xF1, 0);
			CIDATA_DEBUG_LOG2(MDB_CiDataBuildListOfPackets, DBG_LOG_FUNC_END, 0);
			return 0;
		}

		CIDATA_DEBUG_LOG2(MDB_CiDataBuildListOfPackets, 0x18, 0);
		CiDataUpdateMultiplePacketList(downlinkPointersList);
		CiDataDownlinkUpdateListStatistics(downlinkPointersList->totalSize,
						   downlinkPointersList->numItemsOnList);
	}

	CIDATA_DEBUG_LOG2(MDB_CiDataBuildListOfPackets, DBG_LOG_FUNC_END, downlinkPointersList->numItemsOnList);
	return downlinkPointersList->numItemsOnList;
}

static unsigned int CiDataBuildListOfPacketsFromSingleCid(unsigned int cid,
							  CiData_DownlinkControlAreaDatabaseS *downlink,
							  CiData_DownlinkPointersListS *downlinkPointersList)
{
	unsigned int nextRead, nextWrite;
	unsigned int bufferSize;
	unsigned int nextOffset;
	struct fp_net_device *fp_dst_dev = NULL, *fp_dst_dev_prev = NULL;

	nextRead = downlink->nextReadIdx[cid];
	nextWrite = downlink->nextWriteIdx[cid];

	if (nextRead == nextWrite) {
		return 0;	/*nothing to read */
	}

	downlinkPointersList->numItemsOnList = 0;
	downlinkPointersList->totalSize = 0;
	downlinkPointersList->inProgress = TRUE;
	memset(downlinkPointersList->dataSize, 0, sizeof(downlinkPointersList->dataSize));

	do {
		CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(&downlink->cidDescriptors[cid][nextRead],
						     sizeof(CiData_DownlinkDescriptorS));

		bufferSize = downlink->cidDescriptors[cid][nextRead].bufferSize;

		if (downlinkPointersList->totalSize + bufferSize >= CI_MAX_DATA_SIZE_FOR_USB) {
			break;
		}

		fp_dst_dev = ci_classify(&downlink->cidDescriptors[cid][nextRead]);

		if (!fp_dst_dev || fp_dst_dev->mul_support == FALSE) {
			break;
		}
		if (fp_dst_dev_prev && fp_dst_dev_prev != fp_dst_dev)
			break;
		else
			fp_dst_dev_prev = fp_dst_dev;
		downlinkPointersList->ciDownlinkItemsForUsb[downlinkPointersList->numItemsOnList] =
		    &downlink->cidDescriptors[cid][nextRead];
		downlinkPointersList->totalSize += downlink->cidDescriptors[cid][nextRead].bufferSize;
		downlinkPointersList->dataSize[cid] += downlink->cidDescriptors[cid][nextRead].packet_length;

		nextOffset = downlink->cidDescriptors[cid][nextRead].buffer.bufferOffset +
		    downlink->cidDescriptors[cid][nextRead].bufferSize;

		CI_DATA_DOWNLINK_INDEX_INCREMENT(nextRead);
		downlinkPointersList->numItemsOnList++;
		downlinkPointersList->cidsInList[cid]++;

	} while ((nextRead != nextWrite) && (nextOffset == downlink->cidDescriptors[cid][nextRead].buffer.bufferOffset)
		 && (downlinkPointersList->numItemsOnList < ciDataMaxNumOfPacketsForUsb));

	if (downlinkPointersList->numItemsOnList >= ciDataMaxNumOfPacketsForUsb) {
		gCiDataDebugStatistics.downlink.numListReachedMaxPackets++;
	}

	if (downlinkPointersList->numItemsOnList) {
		downlinkPointersList->tempReadIdx[cid] = nextRead;

		CiDataUpdateMultiplePacketList(downlinkPointersList);
	}

	return downlinkPointersList->numItemsOnList;
}

static void CiDataConvertBufferPtrsToOffsets(CiData_DownlinkPointersListS *downlinkPointersList)
{
	unsigned int i;

	for (i = 0; i < downlinkPointersList->numItemsOnList; i++) {
		downlinkPointersList->ciDownlinkItemsForUsb[i]->buffer.bufferOffset =
		    (unsigned int)(downlinkPointersList->ciDownlinkItemsForUsb[i]->buffer.pBufferStart -
				   gCiDataClient.downlinkQueueInfo.baseAddress);
		CI_DATA_DESCRIPTORS_CACHE_FLUSH(downlinkPointersList->ciDownlinkItemsForUsb[i],
						sizeof(CiData_DownlinkDescriptorS));
	}
}

static void CiDataUpdateReadIndexesAndCidUsageCount(CiData_DownlinkControlAreaDatabaseS *downlink,
						    CiData_DownlinkPointersListS *downlinkPointersList,
						    CiData_CidUsageCountS *cidUsageCount)
{
	unsigned char i;
	unsigned int cid;
	unsigned int indexDiff;

	CIDATA_DEBUG_LOG2(MDB_CiDataUpdateReadIndexesAndCidUsageCount, DBG_LOG_FUNC_START, 0);

	for (i = 0; i < activeCids_Snapshot.numActive; i++) {
		cid = activeCids_Snapshot.cid[i];
		if (downlinkPointersList->cidsInList[cid]) {
			/*for statistics */
			indexDiff = CI_DATA_DOWNLINK_INDEX_PEDNING_CALC(downlinkPointersList->tempReadIdx[cid],
									downlink->nextReadIdx[cid]);
			/*Actually update the nextReadIdx */
			downlink->nextReadIdx[cid] = downlinkPointersList->tempReadIdx[cid];
			cidUsageCount->countPerCid[cid] = cidUsageCount->globalUsageCount;

			/*update statistics */
			gCiDataDebugStatistics.downlink.totalNumPacketsReceived[cid] += indexDiff;

			/*update fp_dev statistics */
			if (!gCiDataClient.cid[cid].fp_dev) {
				CI_DATA_DEBUG(0, "CCINET%d device is NULL\n", cid);
				if (!gCiDataClient.inSilentReset)
					BUG();
			} else {
				gCiDataClient.cid[cid].fp_dev->stats.rx_packets +=
				    downlinkPointersList->cidsInList[cid];
				gCiDataClient.cid[cid].fp_dev->stats.rx_bytes += downlinkPointersList->dataSize[cid];

			}
		}
		downlinkPointersList->cidsInList[cid] = 0;
	}

	CI_DATA_DESCRIPTORS_CACHE_FLUSH(&downlink->nextReadIdx, sizeof(downlink->nextReadIdx));

	cidUsageCount->globalUsageCount++;

	CI_DATA_DEBUG(2, "globalUsageCount = %d", cidUsageCount->globalUsageCount);

	CIDATA_DEBUG_LOG2(MDB_CiDataUpdateReadIndexesAndCidUsageCount, DBG_LOG_FUNC_END,
			  cidUsageCount->globalUsageCount);

}

static unsigned char CiDataDownlinkSendToHost(CiData_DownlinkControlAreaDatabaseS *downlink,
					      CiData_DownlinkPointersListS *downlinkPointersList,
					      CiData_CidUsageCountS *cidUsageCount, struct fp_net_device *fp_dst_dev)
{
	unsigned char packetsPending;
	unsigned int i;
	netdev_tx_t sendRc;
	unsigned int nextReadIdx;
	unsigned int nextReadCnfIdx;
	unsigned int nextWriteIdx;

	CI_DATA_DEBUG(2, "Sending list of %d packets to USB", downlinkPointersList->numItemsOnList);
	/*Send to USB */
	/*Update read indexes */
	/*Update used CIDs and global usage count */
	CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, 0x22, 0);

	if (gCiDataClient.downlinkAnalizePackets) {
		CiDataDownlinkAnalizePackets(downlinkPointersList);
	}

	sendRc = CiDataClientLtmDownlinkDataSend((void *)downlinkPointersList->ciDownlinkItemsForUsb, fp_dst_dev);
	if (sendRc == NETDEV_TX_OK) {
		downlinkPointersList->inProgress = FALSE;
		CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, 0x23, 0);
		CiDataUpdateReadIndexesAndCidUsageCount(downlink, downlinkPointersList, cidUsageCount);
		CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, 0x24, 0);

		packetsPending = TRUE;
	}
	/*
	 * CiDataClientLtmDownlinkDataSend returns FALSE when USB-ETH
	 * is busy. Discard list and wait for event from COMM or USB-ETH
	 */
	else {
		CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, 0x29, 0);
		/*Update statistics */
		gCiDataDebugStatistics.downlink.numBusyPackets += downlinkPointersList->numItemsOnList;

		for (i = 0; i < CI_DATA_TOTAL_CIDS; i++) {
			if (downlinkPointersList->cidsInList[i]) {
				BUG_ON(!gCiDataClient.cid[i].fp_dev);
				gCiDataClient.cid[i].fp_dev->stats.rx_errors += downlinkPointersList->cidsInList[i];
			}
		}

		/*Zero cidsInList array */
		memset(downlinkPointersList->cidsInList, 0, sizeof(downlinkPointersList->cidsInList));

		CiDataConvertBufferPtrsToOffsets(downlinkPointersList);

		packetsPending = FALSE;

		/* If USB returned "CABLE DISCONNECT", need to drop
		 * incoming packet, i.e. set NextRead & NextReadCnf to
		 * NextWrite
		 */
		if (sendRc != NETDEV_TX_BUSY) {
			for (i = 0; i < CI_DATA_TOTAL_CIDS; i++) {
				/* Only set these indexes after USB callback freed
				 * all pending packets
				 */
				nextReadIdx = downlink->nextReadIdx[i];
				nextReadCnfIdx = downlink->nextReadCnfIdx[i];
				nextWriteIdx = downlink->nextWriteIdx[i];

				if (nextReadCnfIdx == nextReadIdx) {
					do {
						downlink->cidDescriptors[i][nextReadIdx].isFree = 0;
						downlink->cidDescriptors[i][nextReadIdx].fp_src_dev = NULL;
						CI_DATA_DESCRIPTORS_CACHE_FLUSH(&downlink->
										cidDescriptors[i][nextReadIdx],
										sizeof(CiData_DownlinkDescriptorS));
						CI_DATA_DOWNLINK_INDEX_INCREMENT(nextReadIdx);
						CI_DATA_DOWNLINK_INDEX_INCREMENT(nextReadCnfIdx);
					} while (nextReadIdx != nextWriteIdx);
				}

				downlink->nextReadCnfIdx[i] = nextReadCnfIdx;
				downlink->nextReadIdx[i] = nextReadIdx;

			}
			CI_DATA_DESCRIPTORS_CACHE_FLUSH(&downlink->nextReadCnfIdx, sizeof(downlink->nextReadCnfIdx));
			CI_DATA_DESCRIPTORS_CACHE_FLUSH(&downlink->nextReadIdx, sizeof(downlink->nextReadIdx));
		}
	}

	return packetsPending;
}

/***********************************************************************************\
*   Name:        CiDataDownlinkTask
*
*   Description: Data downlink task - receive packets and notify the users
*
*   Params:
*
*   Return:
\***********************************************************************************/
int CiDataDownlinkTask(void *argv)
{
	CiData_DownlinkControlAreaDatabaseS *downlink;	/* = &gCiDataClient.pSharedArea->downlink; */
	unsigned char packetsPending;	/*discover when loop didn't find any new pending packet */
	/*unsigned int      cid, primCid; */
	OSA_STATUS osaStatus;
	unsigned int packetsCounter;
	unsigned int flagBits;
	/*unsigned char        bufferHandled; */
	unsigned int tempCid;
	unsigned int numItems;
	struct fp_net_device *fp_dst_dev = NULL;
	CiData_DownlinkDescriptorS *desc = NULL;

	CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, DBG_LOG_FUNC_START, 0);

	while (!kthread_should_stop()) {

		osaStatus = FLAG_WAIT(&gCiDataClient.downlinkWaitFlagRef, CI_DATA_DOWNLINK_APPS_TASK_FLAG_MASK,
				      OSA_FLAG_OR_CLEAR, &flagBits, MAX_SCHEDULE_TIMEOUT);
		CI_DATA_ASSERT(osaStatus == OS_SUCCESS);

		if (gCiDataClient.inSilentReset) {
			osaStatus = FLAG_SET(&gCiDataClient.silentResetWaitFlagRef, CI_DATA_SILENT_RESET_FLAG_MASK,
					     OSA_FLAG_OR);
			CI_DATA_ASSERT_MSG_P(0, osaStatus == OS_SUCCESS,
					     "CiDataDownlinkTask: FLAG_SET for SILENT RESET failed, osaStatus == %d",
					     osaStatus);
			continue;
		}
		/*
		 * CQ00235406 - need to save current active CID status because
		 * it can change in CiDataClientCidDisable
		 */
		CI_DATA_LOCK(gCiDataClientShmemSpinlock);

		activeCids_Snapshot = activeCids;

		CI_DATA_UNLOCK(gCiDataClientShmemSpinlock);

		downlink = &gCiDataClient.pSharedArea->downlink;	/*task could be running before shared area init */

		/*
		 * BS:  Downlink optimization - use local indexes instead of accessing
		 *      shared area
		 */
		/*memcpy(&downlinkPointersList.downlinkIndexes, (void *)downlink, sizeof(downlinkPointersList.downlinkIndexes)); */

		packetsCounter = 0;
		CI_DATA_DEBUG(4, "CiDataDownlinkTask: task wait-flag received");
		CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, 1, 0);
		do {
			/*initially set as no packets are pending */
			packetsPending = FALSE;

			CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(&downlink->nextWriteIdx, sizeof(downlink->nextWriteIdx));

			if (activeCids_Snapshot.numActive == 1) {
				tempCid = activeCids_Snapshot.cid[0];
				/*if CID queue is empty, end loop */
				if (downlink->nextWriteIdx[tempCid] == downlink->nextReadIdx[tempCid]) {
					CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, 0xF1, 0);
					break;
				}
			} else {
				tempCid =
				    CiDataDownlinkFindOldestCid(downlink, &cidUsageCount, &gCiDataDownlinkPointersList);
				CI_DATA_DEBUG(2, "oldestCid = %d", tempCid);
				CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, 0x10, tempCid);
			}

			if (tempCid == CI_DATA_TOTAL_CIDS) {
				CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, 0xF0, 0);
				/* Nothing to read */
				break;
			}

			desc = &downlink->cidDescriptors[tempCid][downlink->nextReadIdx[tempCid]];

			if (unlikely(gCiDataClient.cid[tempCid].serviceType == CI_DATA_SVC_TYPE_PDP_LOOPBACK_MODE_B)) {
				if (gCiDataClient.loopBackdataBase.status == CI_DATA_LOOPBACK_B_DELAYED) {
					CiDataLoopBackModeBHandleDelayedPckt(tempCid);
					break;
				} else {	/* if in loopBack mode B send to slow path */

					fp_dst_dev = NULL;
				}
			} else {
				fp_dst_dev = ci_classify(desc);
			}

			if (unlikely(fp_dst_dev == NULL)) {
				/* Send a single packet to slow path */
				if (CiDataDownlinkDataSendPacket(downlink, tempCid, NULL) == TRUE) {
					packetsCounter++;
					packetsPending = TRUE;
					continue;
				} else {
					BUG();
					packetsCounter = CI_DATA_DOWNLINK_APPS_MAX_PACKETS_IN_A_ROW;	/*wait on flag */
					break;
				}
			} else if (likely(fp_dst_dev->mul_support == FALSE)) {
				/* Fastpath interface that does not support multiple packets is found
				   Send all pending packets from the same CID to this interface directly */
				struct fp_net_device *fp_dst_dev_prev = fp_dst_dev;
				unsigned int count = 0;
				unsigned int tmpNextReadIdx = downlink->nextReadIdx[tempCid];

				do {
					desc = &downlink->cidDescriptors[tempCid][tmpNextReadIdx];
					fp_dst_dev = ci_classify(desc);

					/* destination dev is not the same, end loop */
					if (unlikely(fp_dst_dev != fp_dst_dev_prev)) {
						break;
					}

					desc->buffer.pBufferStart =
					    (void *)(gCiDataClient.downlinkQueueInfo.baseAddress +
						     desc->buffer.bufferOffset);

					CI_DATA_DESCRIPTORS_CACHE_FLUSH(desc, sizeof(CiData_DownlinkDescriptorS));
					if (likely
					    (CiDataSendPacketToFastPath(downlink, tempCid, desc, fp_dst_dev) == TRUE)) {
						count++;
						CI_DATA_DOWNLINK_INDEX_INCREMENT(tmpNextReadIdx);
						/*update usage count (optimized) */
						cidUsageCount.countPerCid[tempCid] = cidUsageCount.globalUsageCount;
						cidUsageCount.globalUsageCount++;
					} else {
						BUG();
					}

				} while (likely(tmpNextReadIdx != downlink->nextWriteIdx[tempCid]));

				/*confirm that packet is read */
				downlink->nextReadIdx[tempCid] = tmpNextReadIdx;
				CI_DATA_DESCRIPTORS_CACHE_FLUSH(&downlink->nextReadIdx, sizeof(downlink->nextReadIdx));
				/*TODO - add statistics (same CID to a different output interface) */
				packetsCounter += count;
				packetsPending = TRUE;
				continue;
			} else {
				/*
				 * Fast path output interface that supports multiple packets was found:
				 *
				 * 1. Try to build a list of packets which memory addresses are continuous
				 *    as long as it doesn't exeed 16KBytes in total length, contains less than
				 *    CI_MAX_ITEMS items for the __same__ output interface
				 * 2. If the list contains the oldestCid, send as is
				 * 3. If the list does not contain the oldestCid, try to build a list of items
				 *    that contain one or more packets from oldestCid, as long as it doesn't exeed
				 *    16KBytes in total length, contains less than CI_MAX_ITEMS items
				 *    for the same output interface
				 */
				CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, 0x20, 0);
				if (activeCids_Snapshot.numActive == 1) {
					numItems = CiDataBuildListOfPacketsFromSingleCid(tempCid, downlink,
											 &gCiDataDownlinkPointersList);
				} else {
					/* [[TE]] ITM support */
					numItems =
					    CiDataBuildListOfPackets(downlink, &cidUsageCount,
								     &gCiDataDownlinkPointersList);
				}
				CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, 0x21, numItems);

				if (numItems) {
					packetsPending =
					    CiDataDownlinkSendToHost(downlink, &gCiDataDownlinkPointersList,
								     &cidUsageCount, fp_dst_dev);
				} else {
					CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, 0x29, 0);
					CI_DATA_DEBUG(2, "No packets to send");
					/*nothing to send */
				}
			}

			/*CACHE FLUSH (CLEAN) - not needed on APPS */

			if (packetsCounter >= CI_DATA_DOWNLINK_APPS_MAX_PACKETS_IN_A_ROW) {
				CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, 0x30, packetsCounter);
				/*Let go the scheduler for some time, to give CPU to others */
				/*schedule(); */
				packetsCounter = 0;
				break;
			}
			CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, 0x40, packetsPending);

		} while (packetsPending);	/*if all bits are zero - no packets are pending - thus can exit loop */
		CI_DATA_DEBUG(4, "CiDataDownlinkTask - task finished processing packets!!!");
		CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, 0x50, 0);
	}			/*while(task) */

	CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkTask, DBG_LOG_FUNC_END, 0);
	return -1;
}

/***********************************************************************************\
*   Name:        CiDataPathDownlinkTaskStart
*
*   Description: Init the downlink task
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataDownlinkTaskInit(void)
{
#if 1
	OSA_STATUS osaStatus;

	/*init the downlink task flag */
	osaStatus = FLAG_INIT(&gCiDataClient.downlinkWaitFlagRef);
	CI_DATA_ASSERT_MSG_P(0, osaStatus == OS_SUCCESS, "OSAFlagCreate failed (osaStatus=%d)", osaStatus);

	osaStatus = FLAG_INIT(&gCiDataClient.silentResetWaitFlagRef);
	CI_DATA_ASSERT_MSG_P(0, osaStatus == OS_SUCCESS, "OSAFlagCreate failed (osaStatus=%d)", osaStatus);

	/*init the task's wait queue */
	/*init_waitqueue_head(&gCiDataClient.downlinkWaitFlag); */

	/*init the downlink task itself */
/*	osaStatus = OSATaskCreate(&gCiDataClient.downlinkTaskRef, &gCiDataClient.downlinkTaskStack,
	CI_DATA_DOWNLINK_APPS_TASK_STACK_SIZE, CI_DATA_DOWNLINK_APPS_TASK_PRIORITY, CI_DATA_DOWNLINK_APPS_TASK_NAME,
	CiDataDownlinkTask, NULL);
	CI_DATA_ASSERT_MSG_P(0, osaStatus == OS_SUCCESS, "OSATaskCreate failed (osaStatus=%d)", osaStatus);
    */
#else

	/*downlink init */
	init_waitqueue_head(&gCiDataClient.downlinkWaitFlagRef);
#endif

	gCiDataClient.downlinkTaskRef = kthread_run(CiDataDownlinkTask, 0, CI_DATA_DOWNLINK_APPS_TASK_NAME);
	if (IS_ERR(gCiDataClient.downlinkTaskRef)) {
		CI_DATA_ASSERT_MSG(0, FALSE, "CiDataPathDownlinkTaskStart: downlinkTaskRef not created!!!");
	}

	CI_DATA_DEBUG(3, "CiDataPathDownlinkTaskStart: Downlink task started....");
}

/***********************************************************************************\
*   Name:        CiDataUplinkDataFree
*
*   Description: run through all queues and free packets if possible
*
*   Params:
*
*   Return:
*
\***********************************************************************************/
void CiDataUplinkDataFree(void)
{
	unsigned int queueNum;
	CiData_UplinkControlAreaDatabaseS *uplink = &gCiDataClient.pSharedArea->uplink;
	/*CiData_UplinkDebugStatisticsS     *uplinkStats = &gCiDataDebugStatistics.uplink; */
	unsigned int tempNextReadCnfIdx[CI_DATA_UPLINK_TOTAL_QUEUES];
	unsigned short tempNextFreeIdx[CI_DATA_UPLINK_TOTAL_QUEUES];

	/*BS [ul_low_tp_fix] */
	CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(&uplink->nextReadCnfIdx, sizeof(uplink->nextReadCnfIdx));

	/*copy to local to use cache */
	memcpy(tempNextReadCnfIdx, (void *)uplink->nextReadCnfIdx, sizeof(uplink->nextReadCnfIdx));
	memcpy(tempNextFreeIdx, (void *)uplink->nextFreeIdx, sizeof(uplink->nextFreeIdx));

	for (queueNum = 0; queueNum < CI_DATA_UPLINK_TOTAL_QUEUES; queueNum++) {
		if (tempNextFreeIdx[queueNum] != tempNextReadCnfIdx[queueNum]) {
			do {
				CiDataUplinkMemDataFree((void *)(gCiDataClient.uplinkInfo.baseAddress +
								 uplink->cidDescriptors[queueNum][tempNextFreeIdx
												  [queueNum]].
								 packetOffset));
				CI_DATA_UPLINK_INDEX_INCREMENT(tempNextFreeIdx[queueNum]);
			} while (tempNextFreeIdx[queueNum] != tempNextReadCnfIdx[queueNum]);

			/*update the shared area index */
			uplink->nextFreeIdx[queueNum] = tempNextFreeIdx[queueNum];
		}
	}
}

static inline void CiDataUplinkThrownPacket(unsigned int queueNum, void *packet_address)
{
	unsigned int tmp;
	unsigned char thrownThreshold = FALSE;
	CiData_UplinkDebugStatisticsS *uplinkStats = &gCiDataDebugStatistics.uplink;

	CI_DATA_DEBUG(5, "CiDataUplinkCopyToSharedMemory: ERROR queueNum=%d, no free index - packet thrown!!!",
		      queueNum);

	uplinkStats->totalNumPacketsThrown++;
	tmp = uplinkStats->numPacketsThrownPerQueue[queueNum]++;

	if (unlikely((tmp == 1) || (tmp == 100))) {
		thrownThreshold = TRUE;
	} else if (likely(tmp > 0x1fff)) {
		/*every 8K packets */
		if ((tmp & 0x1fff) == 0)
			thrownThreshold = TRUE;
	} else if (likely(tmp > 0x3ff)) {
		/*every 1K packets */
		if ((tmp & 0x3ff) == 0)
			thrownThreshold = TRUE;
	}

	if (thrownThreshold == TRUE) {
		CI_DATA_DEBUG(1, "CiDataUplinkCopyToSharedMemory: queueNum=%d, total %d packets thrown", queueNum,
			      uplinkStats->numPacketsThrownPerQueue[queueNum]);
		CiDataUplinkDataStartNotifySend(1);
	}
}

/***********************************************************************************\
*   Name:        CiDataUplinkCopyToSharedMemory
*
*   Description: Send single uplink packet to COMM.
*                Note - assumption is that per CID it's non-reentrant function
*
*   Params:      queueNum - queue number to put packet into
*                pBuffer - buffer to send
*                length - length of the buffer
*
*   Return:      0 = success
*                otherwise - error
\***********************************************************************************/
CiData_ErrorCodesE CiDataUplinkCopyToSharedMemory(unsigned int queueNum, void *packet_address, unsigned int packet_length,
						  unsigned int *pIndexInQueue)
{
	CiData_UplinkControlAreaDatabaseS *uplink = &gCiDataClient.pSharedArea->uplink;
	unsigned int tempWriteIdx = uplink->nextWriteIdx[queueNum];
	unsigned int tempFreeIdx = uplink->nextFreeIdx[queueNum];
	CiData_UplinkDescriptorS *descriptor = &uplink->cidDescriptors[queueNum][tempWriteIdx];
	CiData_UplinkDebugStatisticsS *uplinkStats = &gCiDataDebugStatistics.uplink;
	unsigned int cidNumIndexFree;
	unsigned int tempWriteOffset;
	unsigned char packet_was_copied = false;

	cidNumIndexFree = CI_DATA_UPLINK_INDEX_FREE_CALC(tempWriteIdx, tempFreeIdx);

	/* Check if we need to free from SHMEM */
	if (cidNumIndexFree < CI_DATA_UPLINK_MAX_PENDING_PACKETS - CI_DATA_UPLINK_NUM_PACKETS_BEFORE_FREE) {
		CiDataUplinkDataFree();
		tempFreeIdx = uplink->nextFreeIdx[queueNum];
		cidNumIndexFree = CI_DATA_UPLINK_INDEX_FREE_CALC(tempWriteIdx, tempFreeIdx);
	}

	if (unlikely(cidNumIndexFree == 0)) {
		/* Not enough index */
		CiDataUplinkThrownPacket(queueNum, packet_address);
		return CI_DATA_ERR_ENOMEM;
	}

	if (!CI_DATA_IS_SHARED_MEMORY((unsigned int)packet_address, (unsigned int)packet_length)) {
		/* Packet is not in SHMEM -> Copy it */
		void *tmp;

		CI_DATA_DEBUG(2, "CiDataUplinkCopyToSharedMemory: queueNum=%d, packet_address=%x, packet_length=%d"
			      " is out of uplink buffer range (baseAddress=%x, size=%d) - copy to buffer",
			      queueNum, (unsigned int)packet_address, packet_length, gCiDataClient.uplinkInfo.baseAddress,
			      gCiDataClient.uplinkInfo.size);

		tmp = CiDataUplinkMemDataAlloc(packet_length, 0);
		if (tmp == NULL) {
			CiDataUplinkThrownPacket(queueNum, packet_address);
			return CI_DATA_ERR_ENOMEM;
		}

		/* Copy to SHMEM and free the old referance packet */
		memcpy(tmp, packet_address, packet_length);
		packet_address = tmp;
		packet_was_copied = true;
	}
	/* Found an empty descriptor - send packet */
	CI_DATA_CACHE_FLUSH(packet_address, packet_length);
	/*add the buffer to queue */
	/*for BSR Update - add new packet to accumulate total bytes sent to uplink queue */
	uplink->totalDataBytesSent[queueNum] += packet_length;
	CI_DATA_CACHE_FLUSH(uplink->totalDataBytesSent, sizeof(uplink->totalDataBytesSent));

	tempWriteOffset = (unsigned int)packet_address - gCiDataClient.uplinkInfo.baseAddress;
	/*update descriptors */
	descriptor->packetOffset = tempWriteOffset;
	descriptor->packet_length = packet_length;
	descriptor->packetDropped = FALSE;

	CI_DATA_DESCRIPTORS_CACHE_FLUSH(descriptor, sizeof(CiData_UplinkDescriptorS));

	CI_DATA_DEBUG(4,
		      "CiDataUplinkCopyToSharedMemory: packetOffset=%d, packet_length=%d, uplink->nextWriteIdx[queueNum]=%d",
		      descriptor->packetOffset, packet_length, tempWriteIdx);

	CI_DATA_DEBUG(5,
		      "CiDataUplinkCopyToSharedMemory: packetOffset=%d, packet_length=%d, uplink->nextWriteIdx[queueNum]=%d",
		      tempWriteOffset, packet_length, tempWriteIdx);

	if (tempWriteIdx == tempFreeIdx)
		uplinkStats->numTimesUplinkQueueWasEmpty[queueNum]++;

	/*update index to support packet drop */
	*pIndexInQueue = tempWriteIdx;

	/*update counters */
	CI_DATA_UPLINK_INDEX_INCREMENT(tempWriteIdx);
	uplink->nextWriteIdx[queueNum] = tempWriteIdx;

	CI_DATA_DESCRIPTORS_CACHE_FLUSH(&uplink->nextWriteIdx, sizeof(uplink->nextWriteIdx));

	/*update statistics: */
	uplinkStats->currentNumPacketsPending++;
	if (unlikely(uplinkStats->currentNumPacketsPending > uplinkStats->maxNumPacketsPending))
		uplinkStats->maxNumPacketsPending = uplinkStats->currentNumPacketsPending;

	uplinkStats->currentBytesUsedInQueue += packet_length;
	if (unlikely(uplinkStats->currentBytesUsedInQueue > uplinkStats->maxBytesUsedInQueue))
		uplinkStats->maxBytesUsedInQueue = uplinkStats->currentBytesUsedInQueue;

	uplinkStats->averagePacketSizePerQueue[queueNum] =
	    (uplinkStats->averagePacketSizePerQueue[queueNum] + packet_length) >> 1;

	if (((CI_DATA_UPLINK_MAX_PENDING_PACKETS - 1) - cidNumIndexFree) >
	    uplinkStats->maxPacketsPendingPerQueue[queueNum])
		uplinkStats->maxPacketsPendingPerQueue[queueNum] =
		    (CI_DATA_UPLINK_MAX_PENDING_PACKETS - 1) - cidNumIndexFree;

	uplinkStats->totalNumPacketsSent++;
	uplinkStats->numPacketsSentPerQueue[queueNum]++;

	CiDataUplinkDataStartNotifySend(1);

	return packet_was_copied ? CI_DATA_OK_FREE_IS_NEEDED : CI_DATA_OK;
}

/***********************************************************************************\
*   Name:        CiDataUplinkQueueMarkPacketsAsIgnore
*
*   Description: Mark all non-read packets from queue as ignore
*
*   Notes:
*
*   Params:      queueNum - number of queue
*
*   Return:
\***********************************************************************************/
void CiDataUplinkQueueMarkPacketsAsIgnore(unsigned int queueNum)
{
	CiData_UplinkControlAreaDatabaseS *uplink = &gCiDataClient.pSharedArea->uplink;
	unsigned int tempWriteIdx = uplink->nextWriteIdx[queueNum];
	unsigned int tempReadIdx = uplink->nextReadIdx[queueNum];

	CI_DATA_DEBUG(1, "CiDataUplinkQueueMarkPacketsAsIgnore: queueNum=%d, writeIdx=%d, readIdx=%d",
		      queueNum, tempWriteIdx, tempReadIdx);

	/*if packets were put on queue - mark them as ignore */
	while (tempWriteIdx != tempReadIdx) {
		uplink->cidDescriptors[queueNum][tempReadIdx].packetDropped = TRUE;
		CI_DATA_UPLINK_INDEX_INCREMENT(tempReadIdx);
	}
}

/***********************************************************************************\
*   Name:        CiDataClientSortCidsByPriority
*
*   Description: bubble sort active CID array according to each CID priority
*
*   Params:
*
*   Return:
\***********************************************************************************/
static void CiDataClientSortCidsByPriority(void)
{
	unsigned char i, j;
	unsigned char tempCid;

	for (i = 0; i < activeCids.numActive - 1; i++) {
		for (j = i; j < activeCids.numActive; j++) {
			if (gCiDataClient.cid[activeCids.cid[j]].priority <
			    gCiDataClient.cid[activeCids.cid[i]].priority) {
				/*swap */
				tempCid = activeCids.cid[i];
				activeCids.cid[i] = activeCids.cid[j];
				activeCids.cid[j] = tempCid;
			}
		}
	}
}

/***********************************************************************************\
*   Name:        CiDataClientCidDisable
*
*   Description: Mark all non-read packets from cid (high & low) as ignore
*
*   Notes:
*
*   Params:      cid - number of cid to flush
*
*   Return:
\***********************************************************************************/
void CiDataUplinkFlushCidPackets(unsigned int cid)
{
	unsigned int queueNum;

	queueNum = CI_DATA_CID_TO_QUEUE_CONVERT(cid, CI_DATA_QUEUE_PRIORITY_LOW);
	CiDataUplinkQueueMarkPacketsAsIgnore(queueNum);

	queueNum = CI_DATA_CID_TO_QUEUE_CONVERT(cid, CI_DATA_QUEUE_PRIORITY_HIGH);
	CiDataUplinkQueueMarkPacketsAsIgnore(queueNum);
}

/***********************************************************************************\
*   Name:        CiDataClientDataControl
*
*   Description: control the data service - manage the global flag
*
*   Params:      enable - TRUE to enable data,
*
*   Return:
\***********************************************************************************/
static void CiDataClientDataControl(unsigned char enable)
{
	unsigned int cidIndex;

	CI_DATA_DEBUG(2, "CiDataClientDataStart: enable=%d, (current dataStatus=%d)", enable, gCiDataClient.dataStatus);

	if (enable) {		/*CID is enabled */
		/*Enable global data only if current status is disabled (first CID enabled) */
		if (gCiDataClient.dataStatus == CI_DATA_IS_DISBALED) {
			if (gCiDataClient.mtil_globals_cfg.mode == CI_DATA_HOST)
				CiDataClientLtmRegister();
			gCiDataClient.dataStatus = CI_DATA_IS_ENABLED;
			CI_DATA_DEBUG(1, "CiDataClientDataStart: Global data is ENABLED");
		}
	} else {		/*CID is disabled */
		/*if current data status ENABLED - check if need to disable it */
		if (gCiDataClient.dataStatus == CI_DATA_IS_ENABLED) {
			/*check if all cid's are disabled */
			for (cidIndex = 0; cidIndex < CI_DATA_TOTAL_CIDS; cidIndex++) {
				if (gCiDataClient.cid[cidIndex].state != CI_DATA_CID_STATE_CLOSED)
					break;
			}
			pr_err("%s:%d: TEST index = %d\n", __func__, __LINE__, cidIndex);
			/*if all are disbaled - disable the data */
			if (cidIndex == CI_DATA_TOTAL_CIDS) {
				CI_DATA_DEBUG(1, "CiDataClientDataStart: Global data is DISABLED");
				gCiDataClient.dataStatus = CI_DATA_IS_DISBALED;
				if (gCiDataClient.mtil_globals_cfg.mode == CI_DATA_HOST)
					CiDataClientLtmUnregister();

			}
		}
	}
}

static int CiDataUplinkDataSendPacket(struct fp_packet_details *p_details)
{
	unsigned int cid;
	CiData_ErrorCodesE ret;
	struct fp_net_device *fp_dev;

	BUG_ON(!p_details || !p_details->fp_dst_dev || !p_details->fp_dst_dev->private_data);

	cid = ((CIData_CidDatabaseS *) (p_details->fp_dst_dev->private_data))->cid;

	ret = ci_data_uplink_data_send(cid, p_details->buffer.start + p_details->packet_offset, p_details->packet_length);

	fp_dev = gCiDataClient.cid[cid].fp_dev;
	BUG_ON(!fp_dev);

	if (ret == CI_DATA_OK || ret == CI_DATA_OK_FREE_IS_NEEDED) {

		fp_dev->stats.tx_packets++;
		fp_dev->stats.tx_bytes += p_details->packet_length;

		if (ret == CI_DATA_OK_FREE_IS_NEEDED) {
			p_details->fp_src_dev->free_cb(p_details);
		} else {
			fpdev_put(p_details->fp_src_dev);
			kfree(p_details);
		}

		fpdev_put(fp_dev);
		return NETDEV_TX_OK;
	} else if (ret == CI_DATA_ERR_ENOMEM ||
		   ret == CI_DATA_ERR_CID_DISABLED ||
		   ret == CI_DATA_ERR_DATA_DISABLED || ret == CI_DATA_ERR_PKT_SHOULD_DROP) {
		fp_dev->stats.tx_dropped++;
		p_details->fp_src_dev->free_cb(p_details);
		fpdev_put(fp_dev);
		return NET_XMIT_DROP;
	} else {		/*ERROR */
		fp_dev->stats.tx_errors++;
		CI_DATA_DEBUG(1, "%s:%d: ret = %x", __func__, __LINE__, ret);	/*handle this */
		BUG();
	}
}

static inline struct net_device *alloc_dummy_netdev(const char *name)
{
	struct net_device *dev;

	dev = kzalloc(sizeof(struct net_device), GFP_KERNEL);
	if (!dev)
		return NULL;

	dev->reg_state = NETREG_DUMMY;

	BUG_ON(strlen(name) >= sizeof(dev->name));
	strcpy(dev->name, name);

	return dev;
}

static inline void free_dummy_netdev(struct net_device *dev)
{
	kfree(dev);
}

/***********************************************************************************\
*   Name:        CiDataEnableFastPathNetDevice
*
*   Description: Enable net device based on CID channel
*
*   Notes:
*
*   Params:      cid - number of cid to enable
*
*   Return:
\***********************************************************************************/
static void CiDataEnableFastPathNetDevice(unsigned int cid)
{
	CIData_CidDatabaseS *pCidDb;
	char intfname[20];

	BUG_ON(cid >= CI_DATA_TOTAL_CIDS);

	pCidDb = &gCiDataClient.cid[cid];

	if (pCidDb->fp_dev) {
		CI_DATA_DEBUG(1, "%s:%d: fastpath device for cid=%d already enabled", __func__, __LINE__, cid);
		return;
	}

	pCidDb->fp_dev = kzalloc(sizeof(struct fp_net_device), GFP_KERNEL);
	BUG_ON(pCidDb->fp_dev == NULL);

	pCidDb->fp_dev->tx_cb = CiDataUplinkDataSendPacket;
	pCidDb->fp_dev->tx_mul_cb = NULL;
	pCidDb->fp_dev->free_cb = CiDataClientLtmDownlinkFreePacket;
	pCidDb->fp_dev->mul_support = FALSE;
	pCidDb->fp_dev->private_data = (void *)pCidDb;
	pCidDb->fp_dev->die_cb = &CiDataKillFpdev;
	atomic_set(&pCidDb->fp_dev->refcnt, 0);

	/* For Primary CID, register to ITM, for secondery just get the primery device */
	if (pCidDb->state == CI_DATA_CID_STATE_PRIMARY) {
		snprintf(intfname, sizeof(intfname) - 1, "ccinet%d", cid);
		intfname[sizeof(intfname) - 1] = '\0';
		pCidDb->fp_dev->dev = dev_get_by_name(&init_net, intfname);

		/* On HOST mode the net_device will not be allocated by ccinet -> create a dummy one */
		if (pCidDb->fp_dev->dev == NULL)
			pCidDb->fp_dev->dev = alloc_dummy_netdev(intfname);
		else
			dev_put(pCidDb->fp_dev->dev);

		gCiDataClient.fp_classify_f = itm_register(pCidDb->fp_dev);
	} else {		/* Set secondary PDP device to point at primary */
		unsigned int prim_cid = CiDataTftGetPrimaryBySecondary(cid);
		pCidDb->fp_dev->dev = gCiDataClient.cid[prim_cid].fp_dev->dev;
		BUG_ON(pCidDb->fp_dev->dev == NULL);
	}

}

/***********************************************************************************\
*   Name:        CiDataDisableFastPathNetDevice
*
*   Description: Disable net device based on CID channel
*
*   Notes:
*
*   Params:      cid - number of cid to disable
*
*   Return:
\***********************************************************************************/
static void CiDataDisableFastPathNetDevice(unsigned int cid)
{
	CIData_CidDatabaseS *pCidDb;

	BUG_ON(cid >= CI_DATA_TOTAL_CIDS);

	pCidDb = &gCiDataClient.cid[cid];

	if (pCidDb->fp_dev == NULL) {
		/* device already disabled */
		CI_DATA_DEBUG(1, "%s:%d: fastpath device for cid=%d already disabled", __func__, __LINE__, cid);
		return;
	}

	if (pCidDb->state == CI_DATA_CID_STATE_PRIMARY)
		itm_unregister(pCidDb->fp_dev);
}

static void CiDataKillFpdev(struct fp_net_device *fp_dev)
{
	unsigned char cid;

	BUG_ON(!fp_dev);

	cid = ((CIData_CidDatabaseS *) fp_dev->private_data)->cid;
	BUG_ON(cid >= CI_DATA_TOTAL_CIDS);

	fp_dev = gCiDataClient.cid[cid].fp_dev;
	BUG_ON(!fp_dev);

	if (fp_dev->dev && fp_dev->dev->reg_state == NETREG_DUMMY)
		free_dummy_netdev(fp_dev->dev);
	kfree(fp_dev);
	gCiDataClient.cid[cid].fp_dev = NULL;
	CI_DATA_DEBUG(1, "ccinet%d fastpath device is down!\n", cid);
}

/***********************************************************************************\
*   Name:        CiDataClientCidDisable
*
*   Description: Disable CID channel
*
*   Notes:       When CID is disabled need to flush all UPLINK & DOWNLINK packets
*                for downlink - flushing is done in downlink task, for uplink - in COMM
*
*   Params:      cid - number of cid to disable
*
*   Return:
\***********************************************************************************/
void CiDataClientCidDisable(unsigned int cid)
{
	unsigned char i;
	CI_DATA_DEBUG(1, "CiDataClientCidDisable: cid=%d", cid);

	CiDataDisableFastPathNetDevice(cid);

	gCiDataClient.cid[cid].state = CI_DATA_CID_STATE_CLOSED;
	if (gCiDataClient.loopBackdataBase.status == CI_DATA_LOOPBACK_B_DELAYED
	    || gCiDataClient.loopBackdataBase.status == CI_DATA_LOOPBACK_B_ACTIVE) {
		gCiDataClient.cid[cid].serviceType = gCiDataClient.loopBackdataBase.serviceType_original[cid];
		ci_data_downlink_register(cid,
				       (ci_data_downlink_data_receive_cb) gCiDataClient.loopBackdataBase.
				       downlinkDataReceiveCB_original[cid]);
	}

	memset(&gCiDataClient.cid[cid].ipAddrs, 0, sizeof(CiData_CidIPInfoS));
	gCiDataClient.cid[cid].ipV6PrefixLen = 0;
	gCiDataClient.cid[cid].ipV6RouterSolicitations = 0;

	/*
	 * CQ00235406 - need to save current active CID status because
	 * it can change in CiDataClientCidDisable
	 */
	CI_DATA_LOCK(gCiDataClientShmemSpinlock);

	for (i = 0; i < activeCids.numActive; i++) {
		if (activeCids.cid[i] == cid) {
			activeCids.cid[i] = activeCids.cid[--activeCids.numActive];
			CiDataClientSortCidsByPriority();
			CiDataClientPrintActiveCids();
			break;
		}
	}

	CI_DATA_UNLOCK(gCiDataClientShmemSpinlock);
	CI_DATA_DEBUG(1, "CiDataClientCidDisable: number of active CIDs = %d", activeCids.numActive);
	/*disable the data (global) */
	CiDataClientDataControl(FALSE);

	CiDataUplinkFlushCidPackets(cid);
}

/***********************************************************************************\
*   Name:        CiDataClientCidEnable
*
*   Description: Enable CID channel
*
*   Params:      pCidInfo - CidInfo to enable
*
*   Return:
\***********************************************************************************/
void CiDataClientCidEnable(CiData_CidInfoS *pCidInfo, unsigned char isPrimary)
{
	CI_DATA_DEBUG(1, "CiDataClientCidEnable: cid=%d, isPrimary=%d, serviceType=%d, connectionType=%d",
		      pCidInfo->cid, isPrimary, pCidInfo->serviceType, pCidInfo->connectionType);

	if (pCidInfo->cid >= CI_DATA_TOTAL_CIDS) {
		CI_DATA_ASSERT_MSG_P(0, FALSE, "CiDataClientCidEnable: cid error (cid=%d)", pCidInfo->cid);
	}
	/*update cid status */
	gCiDataClient.cid[pCidInfo->cid].cid = pCidInfo->cid;
	gCiDataClient.cid[pCidInfo->cid].state = (isPrimary) ? CI_DATA_CID_STATE_PRIMARY : CI_DATA_CID_STATE_SECONDARY;
	gCiDataClient.cid[pCidInfo->cid].serviceType = pCidInfo->serviceType;
	gCiDataClient.cid[pCidInfo->cid].connectionType = pCidInfo->connectionType;

	/*Uplink QOS - set priority to default */
	gCiDataClient.cid[pCidInfo->cid].priority = CI_DATA_CID_DEFAULT_PRIORITY;

	if (gCiDataClient.loopBackdataBase.status == CI_DATA_LOOPBACK_B_DELAYED
	    || gCiDataClient.loopBackdataBase.status == CI_DATA_LOOPBACK_B_ACTIVE) {
		gCiDataClient.loopBackdataBase.serviceType_original[pCidInfo->cid] =
		    gCiDataClient.cid[pCidInfo->cid].serviceType;
		gCiDataClient.cid[pCidInfo->cid].serviceType = CI_DATA_SVC_TYPE_PDP_LOOPBACK_MODE_B;
		gCiDataClient.loopBackdataBase.downlinkDataReceiveCB_original[pCidInfo->cid] =
		    gCiDataClient.cid[pCidInfo->cid].downlink_data_receive_cb;
		ci_data_downlink_register(pCidInfo->cid, (ci_data_downlink_data_receive_cb) CiDataLoopBackModeBCallBack);
	}
	/* BS: for multi-packet support */
	/*
	 * CQ00235406 - need to save current active CID status because
	 * it can change in CiDataClientCidDisable
	 */
	CI_DATA_LOCK(gCiDataClientShmemSpinlock);

	activeCids.cid[activeCids.numActive++] = pCidInfo->cid;
	CiDataClientSortCidsByPriority();
	CiDataClientPrintActiveCids();

	CI_DATA_UNLOCK(gCiDataClientShmemSpinlock);

	CI_DATA_DEBUG(1, "CiDataClientCidEnable: number of active CIDs = %d", activeCids.numActive);
	/*enable the data (global) */
	CiDataClientDataControl(TRUE);
	/*enable the cid's fast path device */
	CiDataEnableFastPathNetDevice(pCidInfo->cid);
}

/***********************************************************************************\
*   Name:        CiDataClientCidSetPriority
*
*   Description: Set CID priority. Also call active CIDs sorter
*
*   Params:      pCidInfo - CidInfo to enable
*
*   Return:
\***********************************************************************************/
void CiDataClientCidSetPriority(CiData_CidInfoS *pCidInfo)
{
	CI_DATA_DEBUG(1, "CiDataClientCidSetPriority: cid=%d, priority=%d", pCidInfo->cid, pCidInfo->priority);

	if (pCidInfo->cid >= CI_DATA_TOTAL_CIDS) {
		CI_DATA_ASSERT_MSG_P(0, FALSE, "CiDataClientCidSetPriority: cid error (cid=%d)", pCidInfo->cid);
	}

	if (gCiDataClient.cid[pCidInfo->cid].state == CI_DATA_CID_STATE_CLOSED) {
		CI_DATA_DEBUG(1, "CiDataClientCidSetPriority: CID is closed");
		return;
	}

	if (pCidInfo->priority == gCiDataClient.cid[pCidInfo->cid].priority) {
		CI_DATA_DEBUG(1, "CiDataClientCidSetPriority: This CID already has this priority");
		return;
	}

	CI_DATA_LOCK(gCiDataClientShmemSpinlock);
	gCiDataClient.cid[pCidInfo->cid].priority = pCidInfo->priority;
	CiDataClientSortCidsByPriority();
	CI_DATA_UNLOCK(gCiDataClientShmemSpinlock);

	CiDataClientPrintActiveCids();
}

/***********************************************************************************\
*   Name:        CiDataClientDropLowestPriorityPackets
*
*   Description: for each CID with the lowset priority, do:
*                1. Check if it's lowest priority queue has data
*                1a. If data exist, mark the packet in nextReadIdx as dropped
*                2. If not, check if it's highest priority queue has data
*                2a. If data exist, mark the packet in nextReadIdx as dropped
*                3. Do the same for all CIDs with same priority
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataClientDropLowestPriorityPackets(void)
{
	CiData_UplinkControlAreaDatabaseS *uplink = &gCiDataClient.pSharedArea->uplink;
	CiData_UplinkDebugStatisticsS *uplinkStats = &gCiDataDebugStatistics.uplink;
	unsigned char priority;
	unsigned char activeCidIndex;
	unsigned char i;
	unsigned short tempNextReadIdx[CI_DATA_UPLINK_TOTAL_QUEUES], readIdx;
	unsigned short tempNextWriteIdx[CI_DATA_UPLINK_TOTAL_QUEUES], writeIdx;
	int queueNum;
	unsigned char loopEnd;
	CiData_ActiveCids snapshotActiveCids;
	unsigned int currentCid;
	unsigned int numCidsContainingData = 0;

	if (gCiDataClient.uplinkQosDisabled) {
		return;
	}
	CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(&uplink->nextReadIdx, sizeof(uplink->nextReadIdx));

	/*copy to local to use cache */
	memcpy(tempNextReadIdx, (void *)uplink->nextReadIdx, sizeof(uplink->nextReadIdx));
	memcpy(tempNextWriteIdx, (void *)uplink->nextWriteIdx, sizeof(uplink->nextWriteIdx));

	CI_DATA_LOCK(gCiDataClientShmemSpinlock);

	snapshotActiveCids = activeCids;

	CI_DATA_UNLOCK(gCiDataClientShmemSpinlock);

	/*
	 * BS: continue only if more than one active
	 * CID has data in either of its queues
	 */
	for (i = 0; i < snapshotActiveCids.numActive; i++) {
		currentCid = snapshotActiveCids.cid[i];
		queueNum = CI_DATA_CID_TO_QUEUE_CONVERT(currentCid, CI_DATA_QUEUE_PRIORITY_LOW);
		readIdx = tempNextReadIdx[queueNum];
		writeIdx = tempNextWriteIdx[queueNum];
		if (readIdx != writeIdx) {
			numCidsContainingData++;
		} else {
			queueNum = CI_DATA_CID_TO_QUEUE_CONVERT(currentCid, CI_DATA_QUEUE_PRIORITY_HIGH);
			readIdx = tempNextReadIdx[queueNum];
			writeIdx = tempNextWriteIdx[queueNum];
			if (readIdx != writeIdx) {
				numCidsContainingData++;
			}
		}
	}

	/* If only one CID contains data (in either of its queues), don't use QOS algorithm */
	if (numCidsContainingData <= 1) {
		CI_DATA_DEBUG(2,
			      "CiDataClientDropLowestPriorityPackets: less than two PDP's containing data, don't use QOS");
		return;
	}

	/*
	 * BS: activeCids is sorted from highest priority cid
	 * (lowest index in array) to lowest priority cid
	 * (highest index in array)
	 */
	activeCidIndex = snapshotActiveCids.numActive - 1;
	/*CI_DATA_DEBUG(2, "CiDataClientDropLowestPriorityPackets: activeCidIndex = %d", activeCidIndex); */

	/*Save lowest priority value - for checking if other cid's have same priority */
	priority = gCiDataClient.cid[snapshotActiveCids.cid[activeCidIndex]].priority;
	/*CI_DATA_DEBUG(2, "CiDataClientDropLowestPriorityPackets: priority = %d", priority); */

	for (i = snapshotActiveCids.numActive; i > 0; i--, activeCidIndex--) {
		currentCid = snapshotActiveCids.cid[activeCidIndex];
		/*CI_DATA_DEBUG(2, "CiDataClientDropLowestPriorityPackets: cid[%d] = %d, priority = %d", activeCidIndex, activeCids.cid[activeCidIndex],priority); */
		if (gCiDataClient.cid[currentCid].priority == priority) {
			loopEnd = FALSE;

			for (queueNum = CI_DATA_CID_TO_QUEUE_CONVERT(currentCid, CI_DATA_QUEUE_PRIORITY_LOW); (!loopEnd)
			     && (queueNum <= CI_DATA_CID_TO_QUEUE_CONVERT(currentCid, CI_DATA_QUEUE_PRIORITY_HIGH));
			     queueNum++) {
				readIdx = tempNextReadIdx[queueNum];
				writeIdx = tempNextWriteIdx[queueNum];
				CI_DATA_DEBUG(2,
					      "CiDataClientDropLowestPriorityPackets: queueNum = %d, nextReadIdx = %d, nextWriteIdx = %d",
					      queueNum, readIdx, writeIdx);
				while (readIdx != writeIdx) {
					CI_DATA_DEBUG(2, "CiDataClientDropLowestPriorityPackets: packeDropped = %d",
						      uplink->cidDescriptors[queueNum][readIdx].packetDropped);
					if (!uplink->cidDescriptors[queueNum][readIdx].packetDropped) {
						uplink->cidDescriptors[queueNum][readIdx].packetDropped = TRUE;
						uplinkStats->numPacketsThrownQosPerQueue[queueNum]++;

						CI_DATA_DESCRIPTORS_CACHE_FLUSH(&uplink->cidDescriptors[queueNum]
										[readIdx],
										sizeof(CiData_UplinkDescriptorS));
						loopEnd = TRUE;
						break;
					}
					CI_DATA_UPLINK_INDEX_INCREMENT(readIdx);
				}
				/*CI_DATA_DEBUG(2, "CiDataClientDropLowestPriorityPackets: loopEnd = %d", loopEnd); */
			}
		}
	}

	/*CI_DATA_DEBUG(2, "CiDataClientDropLowestPriorityPackets: Exit"); */
}

/***********************************************************************************\
*   Name:        CiDataClientCidSetIpAddrs
*
*   Description: add Ip Addesses to CID channel
*
*   Params:      ipInfoCommand - cid and ipaddresse info from user space
*
*   Return:
\***********************************************************************************/
void CiDataClientCidSetIpAddress(CiData_CidIPInfoComandS *ipInfoCommand)
{
	CI_DATA_DEBUG(1, "%s: cid=%d, v4addr=%08X v6addr=%08X:%08X:%08X:%08X",
		      __func__, ipInfoCommand->cid, ipInfoCommand->ipAddrs.ipv4addr,
		      ipInfoCommand->ipAddrs.ipv6addr[0], ipInfoCommand->ipAddrs.ipv6addr[1],
		      ipInfoCommand->ipAddrs.ipv6addr[2], ipInfoCommand->ipAddrs.ipv6addr[3]);

	if (ipInfoCommand->cid >= CI_DATA_TOTAL_CIDS) {
		CI_DATA_ASSERT_MSG_P(0, FALSE, "CiDataClientCidSetIpAddrs: cid error (cid=%d)", ipInfoCommand->cid);
	}
	memcpy(&gCiDataClient.cid[ipInfoCommand->cid].ipAddrs, &ipInfoCommand->ipAddrs, sizeof(CiData_CidIPInfoS));

	gCiDataClient.cid[ipInfoCommand->cid].ipV6PrefixLen = 0;

	if (ipInfoCommand->ipAddrs.ipv6addr[3] ||
	    ipInfoCommand->ipAddrs.ipv6addr[2] ||
	    ipInfoCommand->ipAddrs.ipv6addr[1] || ipInfoCommand->ipAddrs.ipv6addr[0]) {
		gCiDataClient.downlinkAnalizePackets++;
	}
}

/***********************************************************************************\
*   Name:        CiDataClientCidGetIpAddress
*
*   Description: returns the Ip Addesses of CID channel
*
*   Params:      ipInfoCommand - cid and ipaddresse info to user space
*
*   Return:
\***********************************************************************************/
void CiDataClientCidGetIpAddress(CiData_CidIPInfoComandS *ipInfoCommand)
{
	static unsigned int router_solicit[16];

	if (ipInfoCommand->cid >= CI_DATA_TOTAL_CIDS) {
		CI_DATA_ASSERT_MSG_P(0, FALSE, "CiDataClientCidGetIpAddress: cid error (cid=%d)", ipInfoCommand->cid);
	}
	memcpy(&ipInfoCommand->ipAddrs, &gCiDataClient.cid[ipInfoCommand->cid].ipAddrs, sizeof(CiData_CidIPInfoS));

	if (((gCiDataClient.cid[ipInfoCommand->cid].ipAddrs.ipv6addr[0] | gCiDataClient.cid[ipInfoCommand->cid].ipAddrs.
	      ipv6addr[1]) == 0)
	    || gCiDataClient.cid[ipInfoCommand->cid].ipV6RouterSolicitations++ < CI_DATA_MAX_ROUTER_SOLICITATIONS) {
		/* NO PREFIX YET - send router solicitation */
		ConstructIcmpv6RouterSolicit(router_solicit, gCiDataClient.cid[ipInfoCommand->cid].ipAddrs.ipv6addr);
		ci_data_uplink_data_send(ipInfoCommand->cid, (void *)router_solicit, 48);
		CI_DATA_DEBUG(0, "%s sent RS\n", __func__);
	}
}

/***********************************************************************************\
*   Name:
*
*   Description: copy DL statistics for CIDs
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataClientCidGetStat(CiData_PdpsStat *pdpStats)
{
	unsigned int numTft = 0;
	int primaryCid;
	int statCid;
	unsigned int numCids;
	unsigned int i;
	CiData_TFTFilterNodeS *pCidTftNode = NULL;
	int noTcpSessions;
	int maxHashColl;

	/*DL */
	memcpy(&pdpStats->downlinkStat, &gCiDataDebugStatistics.downlink, sizeof(pdpStats->downlinkStat));

	pdpStats->uplinkStat.currentBytesUsedInQueue = gCiDataDebugStatistics.uplink.currentBytesUsedInQueue;
	pdpStats->uplinkStat.currentNumPacketsPending = gCiDataDebugStatistics.uplink.currentNumPacketsPending;
	pdpStats->uplinkStat.maxBytesUsedInQueue = gCiDataDebugStatistics.uplink.maxBytesUsedInQueue;
	pdpStats->uplinkStat.maxNumPacketsPending = gCiDataDebugStatistics.uplink.maxNumPacketsPending;
	pdpStats->uplinkStat.totalNumPacketsSent = gCiDataDebugStatistics.uplink.totalNumPacketsSent;
	pdpStats->uplinkStat.totalNumPacketsThrown = gCiDataDebugStatistics.uplink.totalNumPacketsThrown;
	/*get TS_DB statistics */
	CiDataUpGetTsDbStat(&noTcpSessions, &maxHashColl);
	pdpStats->uplinkStat.totalNumTcpSessions = noTcpSessions;
	pdpStats->uplinkStat.tcpSesionMaxHashTableCollision = maxHashColl;

	/*check if there is a specific CID, or we need statistics for all */
	if (pdpStats->cid != -1) {
		if (pdpStats->cid >= CI_DATA_TOTAL_CIDS) {
			return;
		}
		numCids = 1;
		statCid = pdpStats->cid;
	} else {
		numCids = CI_DATA_TOTAL_CIDS;
		statCid = 0;
	}

	for (i = 0; i < numCids; i++) {
		/*get CID type */
		pdpStats->uplinkStat.CidStat[statCid].state = gCiDataClient.cid[statCid].state;
		/*get primary */
		primaryCid = CiDataTftGetPrimaryBySecondary(statCid);

		/*get UL queue statistics */
		pdpStats->uplinkStat.CidStat[statCid].averagePacketSizePerQueue[0] =
		    gCiDataDebugStatistics.uplink.averagePacketSizePerQueue[statCid *
									    CI_DATA_UPLINK_NUM_QUEUES_PER_CID];
		pdpStats->uplinkStat.CidStat[statCid].averagePacketSizePerQueue[1] =
		    gCiDataDebugStatistics.uplink.averagePacketSizePerQueue[statCid *
									    CI_DATA_UPLINK_NUM_QUEUES_PER_CID + 1];
		pdpStats->uplinkStat.CidStat[statCid].maxPacketsPendingPerQueue[0] =
		    gCiDataDebugStatistics.uplink.maxPacketsPendingPerQueue[statCid *
									    CI_DATA_UPLINK_NUM_QUEUES_PER_CID];
		pdpStats->uplinkStat.CidStat[statCid].maxPacketsPendingPerQueue[1] =
		    gCiDataDebugStatistics.uplink.maxPacketsPendingPerQueue[statCid *
									    CI_DATA_UPLINK_NUM_QUEUES_PER_CID + 1];
		pdpStats->uplinkStat.CidStat[statCid].numPacketsDroppedByTrafficShaping[0] =
		    gCiDataDebugStatistics.uplink.numPacketsDroppedByTrafficShaping[statCid *
										    CI_DATA_UPLINK_NUM_QUEUES_PER_CID];
		pdpStats->uplinkStat.CidStat[statCid].numPacketsDroppedByTrafficShaping[1] =
		    gCiDataDebugStatistics.uplink.numPacketsDroppedByTrafficShaping[statCid *
										    CI_DATA_UPLINK_NUM_QUEUES_PER_CID +
										    1];
		pdpStats->uplinkStat.CidStat[statCid].numPacketsSentPerQueue[0] =
		    gCiDataDebugStatistics.uplink.numPacketsSentPerQueue[statCid * CI_DATA_UPLINK_NUM_QUEUES_PER_CID];
		pdpStats->uplinkStat.CidStat[statCid].numPacketsSentPerQueue[1] =
		    gCiDataDebugStatistics.uplink.numPacketsSentPerQueue[statCid * CI_DATA_UPLINK_NUM_QUEUES_PER_CID +
									 1];
		pdpStats->uplinkStat.CidStat[statCid].numPacketsThrownPerQueue[0] =
		    gCiDataDebugStatistics.uplink.numPacketsThrownPerQueue[statCid * CI_DATA_UPLINK_NUM_QUEUES_PER_CID];
		pdpStats->uplinkStat.CidStat[statCid].numPacketsThrownPerQueue[1] =
		    gCiDataDebugStatistics.uplink.numPacketsThrownPerQueue[statCid * CI_DATA_UPLINK_NUM_QUEUES_PER_CID +
									   1];
		pdpStats->uplinkStat.CidStat[statCid].numTimesUplinkQueueWasEmpty[0] =
		    gCiDataDebugStatistics.uplink.numTimesUplinkQueueWasEmpty[statCid *
									      CI_DATA_UPLINK_NUM_QUEUES_PER_CID];
		pdpStats->uplinkStat.CidStat[statCid].numTimesUplinkQueueWasEmpty[1] =
		    gCiDataDebugStatistics.uplink.numTimesUplinkQueueWasEmpty[statCid *
									      CI_DATA_UPLINK_NUM_QUEUES_PER_CID + 1];

		/*get TFT filters */
		pCidTftNode = gCiDataClient.cid[primaryCid].pTftFilterList;
		while (pCidTftNode != NULL) {
			/*check if this filter belongs to this CID */
			if (pCidTftNode->filter.cid == statCid) {
				/*if the filter belongs to this CID - copy to the statistics */
				pdpStats->uplinkStat.CidStat[statCid].Tft[numTft].tftFilterId =
				    pCidTftNode->filter.evaluate_precedence_index;
				pdpStats->uplinkStat.CidStat[statCid].Tft[numTft].matches = pCidTftNode->matches;
				pdpStats->uplinkStat.CidStat[statCid].Tft[numTft].noMatches = pCidTftNode->no_matches;

				numTft++;
			}

			pCidTftNode = pCidTftNode->pNext;

		}
		pdpStats->uplinkStat.CidStat[statCid].numOfTft = numTft;
		statCid++;
		numTft = 0;
	}

}

/***********************************************************************************\
*   Name:        CiDataClientInit
*
*   Description: Initialize the data channel - CLIENT
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataClientInit(void)
{
	int cid;

	/*reset local database */
	memset(&gCiDataClient, 0, sizeof(gCiDataClient));

	/*reset statistics */
	memset(&gCiDataDebugStatistics, 0, sizeof(gCiDataDebugStatistics));

	/*Multi packet support */
	memset(&gCiDataDownlinkPointersList, 0, sizeof(gCiDataDownlinkPointersList));
	memset(&activeCids, 0, sizeof(activeCids));
	memset(&activeCids_Snapshot, 0, sizeof(activeCids_Snapshot));

	memset(&cidUsageCount, 0, sizeof(cidUsageCount));
	cidUsageCount.globalUsageCount = 1;

	ciDataMaxNumOfPacketsForUsb = CI_MAX_ITEMS_FOR_USB;

	/*BS - internal debug log */
	DBGLOG_INIT(cidata, CIDATA_DBG_LOG_SIZE);

	/*initially, uplink data start notify is required */
	gCiDataClient.uplinkDataStartNotifyRequired = TRUE;

	CiDataTrafficShapingInit(1, 1);

	/*set default downlink rx callback functions for all cid's */
	for (cid = 0; cid < CI_DATA_TOTAL_CIDS; cid++) {
		ci_data_downlink_register(cid, CiDataDownlinkDataReceiveDefCB);
	}

	CiDataDownlinkTaskInit();

	/*register ci-data onto shmem */
	CiDataClientShmemRegister();
}
