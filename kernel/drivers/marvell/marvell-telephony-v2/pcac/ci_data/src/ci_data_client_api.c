/*------------------------------------------------------------
(C) Copyright [2006-2012] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
    Title:       CI Data Client Api
    Filename:    ci_data_client_api.c
    Description: Contains API implementation for CI Data Client for R7/LTE data path
    Author:      Yossi Gabay
************************************************************************/
#include <asm/cacheflush.h>

#include "osa_kernel.h"
#include "ci_data_client.h"
#include "ci_data_client_shmem.h"
#include "ci_data_client_handler.h"
#include "ci_data_client_api.h"
#include "ci_data_client_loopback.h"
#include "ci_data_client_mem.h"
/*send api spinlock */
/*CI_DATA_LOCK_INIT(gCiDataSendSpinlock);*/
CIData_SpinlockS    gCiDataSendSpinlock = {0, __SPIN_LOCK_UNLOCKED(protect_lock)};

/************************************************************************
 * Internal debug log
 ***********************************************************************/
#include "dbgLog.h"
DBGLOG_EXTERN_VAR(cidata);
#define     CIDATA_DEBUG_LOG2(a, b, c)     DBGLOG_ADD(cidata, a, b, c)

/************************************************************************
 * Internal Prototypes
 ***********************************************************************/

/************************************************************************
 * Internal Globals
 ***********************************************************************/

/***********************************************************************************\
* Functions
************************************************************************************/

void CiDataLockSendLock()
{
	CI_DATA_LOCK(gCiDataSendSpinlock);
}

void CiDataUnlockSendLock()
{
	CI_DATA_UNLOCK(gCiDataSendSpinlock);
}

/* This counter is just for debug, remove it after bug will be solved */
static int checkCounter;

/***********************************************************************************\
* A P I's
\***********************************************************************************/

static inline void CiDataDownlinkDataFindAndMarkAsFree(unsigned int cid, CiData_DownlinkDescriptorS *desc)
{
	CiData_DownlinkControlAreaDatabaseS *downlink = &gCiDataClient.pSharedArea->downlink;
	CiData_DownlinkDescriptorS *next_desc_ptr = NULL;
	unsigned int nextReadCnfIdx = downlink->nextReadCnfIdx[cid];
	unsigned int counter = CI_DATA_DOWNLINK_MAX_PENDING_PACKETS;

	while (desc != next_desc_ptr && counter--) {
		CI_DATA_DOWNLINK_INDEX_INCREMENT(nextReadCnfIdx);
		next_desc_ptr = &(downlink->cidDescriptors[cid][nextReadCnfIdx]);
	}

	if (unlikely(!counter)) {
		CI_DATA_DEBUG(0, "%s:%d: packet was not found on free (p=%p,cid=%d)\n", __func__, __LINE__, desc, cid);
		CI_DATA_ASSERT(FALSE);
		return;
	}

	downlink->cidDescriptors[cid][nextReadCnfIdx].isFree = 1;
	CI_DATA_DESCRIPTORS_CACHE_FLUSH(&downlink->cidDescriptors[cid][nextReadCnfIdx],
					sizeof(CiData_DownlinkDescriptorS));
	checkCounter++;
	CI_DATA_DEBUG(3, "%s:%d: packet marked as free", __func__, __LINE__);
}

static inline unsigned int CiDataDownlinkFindNumOfFreePackets(unsigned int cid)
{
	CiData_DownlinkControlAreaDatabaseS *downlink = &gCiDataClient.pSharedArea->downlink;
	unsigned int nextReadCnfIdx = downlink->nextReadCnfIdx[cid];
	unsigned int num_to_free = 1;

	downlink->cidDescriptors[cid][nextReadCnfIdx].isFree = 0;
	downlink->cidDescriptors[cid][nextReadCnfIdx].fp_src_dev = NULL;
	CI_DATA_DESCRIPTORS_CACHE_FLUSH(&downlink->cidDescriptors[cid][nextReadCnfIdx],
					sizeof(CiData_DownlinkDescriptorS));
	do {
		CI_DATA_DOWNLINK_INDEX_INCREMENT(nextReadCnfIdx);

		if (downlink->cidDescriptors[cid][nextReadCnfIdx].isFree) {
			num_to_free++;	/*Next descriptor is also free */
			downlink->cidDescriptors[cid][nextReadCnfIdx].isFree = 0;
			downlink->cidDescriptors[cid][nextReadCnfIdx].fp_src_dev = NULL;
			CI_DATA_DESCRIPTORS_CACHE_FLUSH(&downlink->cidDescriptors[cid][nextReadCnfIdx],
							sizeof(CiData_DownlinkDescriptorS));
		} else
			break;
	} while (num_to_free <= CI_DATA_DOWNLINK_MAX_PENDING_PACKETS);

	BUG_ON(num_to_free > CI_DATA_DOWNLINK_MAX_PENDING_PACKETS);

	if (unlikely(num_to_free - 1 > checkCounter)) {
		int i;
		pr_err("%s:%d: num_to_free = %d - checkCounter = %d != 1,cid=%d)\n",
		       __func__, __LINE__, num_to_free, checkCounter, cid);
		nextReadCnfIdx = downlink->nextReadCnfIdx[cid];
		for (i = 0; i < num_to_free; i++) {
			pr_err("packet %d: buf_start = %p, offset = %d, len= %d\n",
			       i, downlink->cidDescriptors[cid][nextReadCnfIdx].buffer.pBufferStart,
			       downlink->cidDescriptors[cid][nextReadCnfIdx].packetOffset,
			       downlink->cidDescriptors[cid][nextReadCnfIdx].packet_length);
			CI_DATA_DOWNLINK_INDEX_INCREMENT(nextReadCnfIdx);
		}
		BUG();
	}
	checkCounter -= (num_to_free - 1);
	CI_DATA_DEBUG(3, "%s:%d: num of free packets=%d", __func__, __LINE__, num_to_free);
	return num_to_free;
}

static inline void CiDataDownlinkDataReadCnf(unsigned int cid, unsigned int num)
{
	CiData_DownlinkControlAreaDatabaseS *downlink = &gCiDataClient.pSharedArea->downlink;
	unsigned int nextReadCnfIdx = downlink->nextReadCnfIdx[cid];

	CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkDataReadCnf, DBG_LOG_FUNC_START, cid);

	CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkDataReadCnf, 1, nextReadCnfIdx);
	/*confirm packet is read - increment index */
	CI_DATA_DOWNLINK_INDEX_ADD(nextReadCnfIdx, num);
	downlink->nextReadCnfIdx[cid] = nextReadCnfIdx;
	CI_DATA_DESCRIPTORS_CACHE_FLUSH(&downlink->nextReadCnfIdx, sizeof(downlink->nextReadCnfIdx));

	CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkDataReadCnf, 1, nextReadCnfIdx);

	CIDATA_DEBUG_LOG2(MDB_CiDataDownlinkDataReadCnf, DBG_LOG_FUNC_END, cid);
}

static DEFINE_SPINLOCK(free_lock);

/***********************************************************************************\
*   Name:        CiDataClientDownlinkFreePacket
*
*   Description: Confirm packet read for CID
*
*   Params:      cid - connection ID
*                data_ptr - pointer to original data
*
*   Return:
\***********************************************************************************/
void CiDataClientDownlinkFreePacket(unsigned int cid, CiData_DownlinkDescriptorS *desc)
{
	CiData_DownlinkControlAreaDatabaseS *downlink = &gCiDataClient.pSharedArea->downlink;
	CiData_DownlinkDescriptorS *head_desc_ptr;
	unsigned int nextReadCnfIdx;
	unsigned int num_to_free;
	unsigned long flags;

	spin_lock_irqsave(&free_lock, flags);
	nextReadCnfIdx = downlink->nextReadCnfIdx[cid];
	head_desc_ptr = &(downlink->cidDescriptors[cid][nextReadCnfIdx]);

	if (desc != head_desc_ptr) {
		CiDataDownlinkDataFindAndMarkAsFree(cid, desc);
	} else {
		num_to_free = CiDataDownlinkFindNumOfFreePackets(cid);
		BUG_ON(!num_to_free);
		CiDataDownlinkDataReadCnf(cid, num_to_free);
	}

	/*if cid was closed & no more packets 'around' - free all pending packets */
	if (unlikely(gCiDataClient.cid[cid].state == CI_DATA_CID_STATE_CLOSED)) {
		/*YG: what is happening if DownlinkTask is still running while getting to here???? */
		CI_DATA_DEBUG(2, "%s: cid=%d was closed while packets were handled...", __func__, cid);

		if (downlink->nextReadIdx[cid] == downlink->nextReadCnfIdx[cid]) {
			CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(&downlink->nextWriteIdx, sizeof(downlink->nextWriteIdx));
			if (downlink->nextWriteIdx[cid] != downlink->nextReadIdx[cid]) {
				/*some packets are pending - flush them!!! */
				CI_DATA_DEBUG(1, "%s: cid=%d, nextWriteIdx=%d, nextReadIdx=%d",
					      __func__, cid, downlink->nextWriteIdx[cid],
					      downlink->nextReadIdx[cid]);
				downlink->nextReadIdx[cid] = downlink->nextWriteIdx[cid];
				downlink->nextReadCnfIdx[cid] = downlink->nextReadIdx[cid];
				CI_DATA_DESCRIPTORS_CACHE_FLUSH(&downlink->nextReadIdx, sizeof(downlink->nextReadIdx));
				CI_DATA_DESCRIPTORS_CACHE_FLUSH(&downlink->nextReadCnfIdx,
								sizeof(downlink->nextReadCnfIdx));
			}
		}
	}
	spin_unlock_irqrestore(&free_lock, flags);
}

/***********************************************************************************\
*   Name:        ci_data_uplink_data_send
*
*   Description: Send single uplink packet to COMM.
*                Note - assumption is that per CID it's non-reentrant function
*
*   Params:      cid - connection ID
*                packet_address - buffer to send
*                packet_length - length of the buffer
*
*   Return:      0 = success
*                otherwise - error
\***********************************************************************************/
CiData_ErrorCodesE ci_data_uplink_data_send(unsigned int cid, void *packet_address, unsigned int packet_length)
{
	unsigned int origCid = cid;
	unsigned int queueNum;
	unsigned int result = 0;
	unsigned int indexInQueue;
	CiData_QueuePriorityE queuePriority;
	unsigned char sendPacket;
	pTCP_SESSION pUsedTcpSession;

	CI_DATA_DEBUG(4, "ci_data_uplink_data_send: cid=%d, packet_address (original)=%x, packet_length=%d",
		      cid, (unsigned int)packet_address, packet_length);

	BUG_ON(cid >= CI_DATA_TOTAL_CIDS);
	BUG_ON((packet_length == 0) || (packet_address == NULL));

	if (gCiDataClient.dataStatus != CI_DATA_IS_ENABLED) {
		if (!gCiDataClient.inSilentReset)
			CI_DATA_DEBUG(1, "ci_data_uplink_data_send: WARNING-uplink"
				      " data while GLOBAL data is DISABLED"
				      " (cid=%d, packet_length=%d , packet_address=%x",
				      cid, packet_length, (unsigned int)packet_address);
		return CI_DATA_ERR_DATA_DISABLED;
	}

	if (gCiDataClient.loopBackdataBase.status != CI_DATA_LOOPBACK_B_INACTIVE) {
		CI_DATA_DEBUG(3, "ci_data_uplink_data_send: packet drop loopback on");
		return CI_DATA_ERR_DATA_DISABLED;
	}
	/*process packet: traffic shaping & convert primary cid to secondary (TFT) */
	sendPacket = CiDataUplinkProcessPacket(&cid, packet_address, packet_length, &queuePriority, &pUsedTcpSession);

	if (sendPacket == FALSE) {
		return CI_DATA_ERR_PKT_SHOULD_DROP;
	}

	if (gCiDataClient.cid[cid].state == CI_DATA_CID_STATE_CLOSED) {
		if (!gCiDataClient.inSilentReset)
			CI_DATA_DEBUG(1, "ci_data_uplink_data_send: WARNING-uplink"
				      " data while cid is CLOSED (cid=%d,"
				      " packet_length=%d , packet_address=%x",
				      cid, packet_length, (unsigned int)packet_address);
		return CI_DATA_ERR_CID_DISABLED;
	}

	queueNum = CI_DATA_CID_TO_QUEUE_CONVERT(cid, queuePriority);
	CI_DATA_DEBUG(3, "ci_data_uplink_data_send: origCid=%d, cid=%d, queuePriority=%d, queueNum=%d",
		      origCid, cid, queuePriority, queueNum);

	/*TODO: Check if we need locking here */
	CI_DATA_LOCK(gCiDataSendSpinlock);

	result = CiDataUplinkCopyToSharedMemory(queueNum, packet_address, packet_length, &indexInQueue);

	if (result == CI_DATA_OK || result == CI_DATA_OK_FREE_IS_NEEDED)
		CiDataUplinkSetPacketInfo(pUsedTcpSession, queueNum, indexInQueue);

	CI_DATA_UNLOCK(gCiDataSendSpinlock);

	return result;
}

/***********************************************************************************\
*   Name:        ci_data_uplink_data_send_gcf
*
*   Description: Send single uplink packet to COMM filled by for GCF purposes.
*                Note - assumption is that per CID it's non-reentrant function
*
*   Params:      cid - connection ID
*                packet_length - length of the buffer to send
*
*   Return:      0 = success
*                otherwise - error
\***********************************************************************************/
unsigned int ci_data_uplink_data_send_gcf(unsigned int Cid, unsigned int packet_length)
{
	unsigned int result = 0;
	unsigned int queueNum;
	unsigned int index = 0;
	void *packet_address;

	/*sanity checks */

	if ((Cid >= CI_DATA_TOTAL_CIDS) || (gCiDataClient.cid[Cid].state == CI_DATA_CID_STATE_CLOSED)) {
		return CI_DATA_ERR_CID_INVALID;
	}

	if (packet_length == 0) {
		return CI_DATA_ERR_PARAMS;
	}

	packet_address = kmalloc(packet_length, GFP_KERNEL);

	if (packet_address == NULL) {
		return CI_DATA_ERR_PARAMS;
	}

	memset(packet_address, '*', packet_length);

	queueNum = CI_DATA_CID_TO_QUEUE_CONVERT(Cid, CI_DATA_QUEUE_PRIORITY_LOW);

	CI_DATA_LOCK(gCiDataSendSpinlock);
	result = CiDataUplinkCopyToSharedMemory(queueNum, packet_address, packet_length, &index);
	CI_DATA_UNLOCK(gCiDataSendSpinlock);

	kfree(packet_address);

	return result;
}

/***********************************************************************************\
*   Name:        ci_data_downlink_unregister
*
*   Description: Unregister CID downlink callback function (set to default CB func)
*
*   Params:      cid - connection ID
*                dlDataReceiveCB - downlink data receive callback function
*
*   Return:
\***********************************************************************************/
void ci_data_downlink_unregister(unsigned int cid)
{
	CI_DATA_DEBUG(3, "CiDataCiDataDownlinkUnregister: Unregistering cid %d", cid);

	if (cid >= CI_DATA_TOTAL_CIDS) {
		CI_DATA_ASSERT_MSG_P(0, FALSE, "ci_data_downlink_unregister: cid error (cid=%d)", cid);
	}

	gCiDataClient.cid[cid].downlink_data_receive_cb = CiDataDownlinkDataReceiveDefCB;
}

/***********************************************************************************\
*   Name:        ci_data_downlink_register
*
*   Description: Register CID downlink callback function
*
*   Params:      cid - connection ID
*                downlink_data_receive_cb - downlink data receive callback function
*
*   Return:
\***********************************************************************************/
void ci_data_downlink_register(unsigned int cid, ci_data_downlink_data_receive_cb downlink_data_receive_cb)
{
	CI_DATA_DEBUG(4, "ci_data_downlink_register: Registering cid %d, func=%x", cid,
		      (unsigned int)downlink_data_receive_cb);

	if (cid >= CI_DATA_TOTAL_CIDS) {
		CI_DATA_ASSERT_MSG_P(0, FALSE, "CiDataPathDownlinkRegister: cid error (cid=%d)", cid);
	}

	gCiDataClient.cid[cid].downlink_data_receive_cb = downlink_data_receive_cb;
}

EXPORT_SYMBOL(ci_data_uplink_data_send);
EXPORT_SYMBOL(ci_data_uplink_data_send_gcf);
EXPORT_SYMBOL(ci_data_downlink_register);
EXPORT_SYMBOL(ci_data_downlink_unregister);
