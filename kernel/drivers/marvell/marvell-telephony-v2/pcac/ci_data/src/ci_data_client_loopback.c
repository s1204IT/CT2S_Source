/*------------------------------------------------------------
(C) Copyright [2006-2013] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
    Title:       CI Data Client LoopBack
    Filename:    ci_data_client_loopBack.c
    Description: Contains API implementation for CI Data Client loopBack mode.
    Author:      Zohar sefti
************************************************************************/
#include "osa_kernel.h"
#include "ci_data_client.h"
#include "ci_data_client_shmem.h"
#include "ci_data_client_handler.h"
#include "ci_data_client_api.h"
#include "ci_data_client_loopback.h"

/************************************************************************
 * Internal debug log
 ***********************************************************************/
#include "dbgLog.h"
DBGLOG_EXTERN_VAR(cidata);
#define     CIDATA_DEBUG_LOG2(a, b, c)     DBGLOG_ADD(cidata, a, b, c)

/************************************************************************
 * Internal Prototypes
 ***********************************************************************/

DEFINE_TIMER(LoopBackTypeBModeTimer, CIDataLoopBackModeBTimerCallBack, 0, 0);
/************************************************************************
 * Internal Globals
 ***********************************************************************/

/***********************************************************************************\
* Functions
************************************************************************************/
void CiDataLoopBackModeBStartTimer(void)
{
	init_timer(&LoopBackTypeBModeTimer);
	LoopBackTypeBModeTimer.expires = jiffies + msecs_to_jiffies(gCiDataClient.loopBackdataBase.delay * 1000);
	add_timer(&LoopBackTypeBModeTimer);
	CI_DATA_DEBUG(1, "CiDataLoopBackModeBStartTimer: delay %d secs", gCiDataClient.loopBackdataBase.delay);
	gCiDataClient.loopBackdataBase.timerExpired = FALSE;	/*timer is on...*/
}

/***********************************************************************************\
*   Name:        CiDataLoopBackModeBInit
*
*   Description: Initialize LoopBack type B mode
*
*   Params:    pointer to CiData_LoopBackTypeBContainer recieved from MTIL
*
*   Return:
\***********************************************************************************/
void CiDataLoopBackModeBEnable(CiData_LoopBackActivationS *pLoopBackActivationParams)
{
	int cid = 0;
	CiData_DownlinkControlAreaDatabaseS *downlink;	/* = &gCiDataClient.pSharedArea->downlink;*/
	downlink = &gCiDataClient.pSharedArea->downlink;	/*task could be running before shared area init*/
	/*per cid data lient changes required...*/
	for (cid = 0; cid < CI_DATA_TOTAL_CIDS; cid++) {
		if (gCiDataClient.cid[cid].state == CI_DATA_CID_STATE_PRIMARY ||
		    gCiDataClient.cid[cid].state == CI_DATA_CID_STATE_SECONDARY) {
			gCiDataClient.loopBackdataBase.serviceType_original[cid] = gCiDataClient.cid[cid].serviceType;
			gCiDataClient.cid[cid].serviceType = CI_DATA_SVC_TYPE_PDP_LOOPBACK_MODE_B;

			gCiDataClient.loopBackdataBase.downlinkDataReceiveCB_original[cid] =
			    gCiDataClient.cid[cid].downlink_data_receive_cb;
			CiDataLoopBackModeBRegister(cid);
			/* need to clean downlink buffer
			done in this order on purpose since need to make sure that
			ReadCnfIdx is not greater than ReadIdx */
			downlink->nextReadIdx[cid] = downlink->nextWriteIdx[cid];
			downlink->nextReadCnfIdx[cid] = downlink->nextReadIdx[cid];
		}

		CI_DATA_DEBUG(2,
			      "CiDataLoopBackModeBEnable: origCb %x origService %d, Service %d, delay %d, origDataStatus %d, DataStatus %d, LBStatus %d, cid %d",
			      (unsigned int)(gCiDataClient.loopBackdataBase.downlinkDataReceiveCB_original[cid]),
			      (int)gCiDataClient.loopBackdataBase.serviceType_original[cid],
			      gCiDataClient.cid[cid].serviceType, gCiDataClient.loopBackdataBase.delay,
			      gCiDataClient.loopBackdataBase.dataStatus_original, gCiDataClient.dataStatus,
			      gCiDataClient.loopBackdataBase.status, cid);
	}
	/* general data client changes required...*/
	gCiDataClient.loopBackdataBase.firstpacketArrived = FALSE;
	gCiDataClient.loopBackdataBase.delay = pLoopBackActivationParams->delay;
	gCiDataClient.loopBackdataBase.timerExpired = TRUE;	/*timerExpired - indicates whether timer is on or off...*/
	gCiDataClient.dataStatus = CI_DATA_IS_ENABLED;
	CI_DATA_DEBUG(1, "delay %d, firstpacketArrived %d, dataStatus, %d", gCiDataClient.loopBackdataBase.delay,
		      gCiDataClient.loopBackdataBase.firstpacketArrived, gCiDataClient.dataStatus);
	if (gCiDataClient.loopBackdataBase.delay > 0) {
		gCiDataClient.loopBackdataBase.status = CI_DATA_LOOPBACK_B_DELAYED;
	} else {
		gCiDataClient.loopBackdataBase.status = CI_DATA_LOOPBACK_B_ACTIVE;
	}
}

/***********************************************************************************\
*   Name:        CiDataLoopBackTestActivation
*
*   Description: disable all data untill loop is closed
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataLoopBackTestActivation(void)
{
	/* this is the only general (i.e., not relate to specific cid -
	   which we, currently, don't know) (and relevant) parameter to changed in this stage... */
	CI_DATA_DEBUG(1, "LoopBack Test mode activated waiting for loop close request, status %d, data status %d",
		      gCiDataClient.loopBackdataBase.status, gCiDataClient.dataStatus);
	gCiDataClient.loopBackdataBase.dataStatus_original = gCiDataClient.dataStatus;
	gCiDataClient.dataStatus = CI_DATA_IS_DISBALED;	/*prevent regular UL while in test mode, the tester need to prevent the DL*/
	gCiDataClient.loopBackdataBase.status = CI_DATA_LOOPBACK_B_INACTIVE;
}

/***********************************************************************************\
*   Name:        CiDataLoopBackTestTermination
*
*   Description: revert relevant data client to original settings
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataLoopBackTestTermination(void)
{
	int cid = 0;
	for (cid = 0; cid < CI_DATA_TOTAL_CIDS; cid++) {
		if (gCiDataClient.cid[cid].state == CI_DATA_CID_STATE_PRIMARY) {
			gCiDataClient.dataStatus = CI_DATA_IS_ENABLED;
			return;
		}
	}
	gCiDataClient.dataStatus = CI_DATA_IS_DISBALED;
}

/***********************************************************************************\
*   Name:        CiDataLoopBackModeBRegister
*
*   Description: Initialize LoopBack type B mode
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataLoopBackModeBRegister(int cid)
{
	CI_DATA_DEBUG(2, "CiDataLoopBackModeBRegister: cid %d, Cb %x", cid, (unsigned int)CiDataLoopBackModeBCallBack);
	/*registering on Data channel for downlink*/
	ci_data_downlink_register(cid, (ci_data_downlink_data_receive_cb) CiDataLoopBackModeBCallBack);
}

/***********************************************************************************\
*   Name:        CiDataLoopBackModeBDisable
*
*   Description: Initialize LoopBack type B mode
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataLoopBackModeBDisable(void)
{
	unsigned char DlpendingPacket = FALSE;	/*KW*/
	int cid = 0;
	OSA_STATUS osaStatus;

	gCiDataClient.loopBackdataBase.status = CI_DATA_LOOPBACK_B_INACTIVE;
	gCiDataClient.dataStatus = CI_DATA_IS_DISBALED;

	/*first delete the timer if any */
	if (gCiDataClient.loopBackdataBase.status == CI_DATA_LOOPBACK_B_DELAYED
	    && !(gCiDataClient.loopBackdataBase.timerExpired)) {
		del_timer(&LoopBackTypeBModeTimer);
	}

	for (cid = 0; cid < CI_DATA_TOTAL_CIDS; cid++) {
		if (gCiDataClient.cid[cid].state != CI_DATA_CID_STATE_CLOSED) {
			/*check if DL buffer has no packets - then toggel to prevous pdp settings */
			if (gCiDataClient.loopBackdataBase.bufferIpPdusCounter[cid] == 0) {
				/*check if no packets in DL buffer and toggel back to original configuration... */
				gCiDataClient.cid[cid].serviceType =
				    gCiDataClient.loopBackdataBase.serviceType_original[cid];
				ci_data_downlink_register(cid,
						       gCiDataClient.
						       loopBackdataBase.downlinkDataReceiveCB_original[cid]);
			} else {
				DlpendingPacket = TRUE;
			}
		}
	}
	/*if we have pending packets - start clearing them... */
	if (DlpendingPacket == TRUE) {
		osaStatus = FLAG_SET(&gCiDataClient.downlinkWaitFlagRef, CI_DATA_DOWNLINK_PACKET_PENDING_FLAG_MASK, OSA_FLAG_OR);	/*clean DL buffer using original downlink CB */
		CI_DATA_ASSERT(osaStatus == OS_SUCCESS);
	}

}

/* Timer expired... */
void CIDataLoopBackModeBTimerCallBack(unsigned long val)
{
	OSA_STATUS osaStatus;
	CI_DATA_DEBUG(1, "CIDataLoopBackModeBTimerCallBack: timer expired!");
	gCiDataClient.loopBackdataBase.timerExpired = TRUE;
	gCiDataClient.loopBackdataBase.delay = 0;
	/*kick downlink task */
	gCiDataClient.loopBackdataBase.status = CI_DATA_LOOPBACK_B_ACTIVE;
	osaStatus = FLAG_SET(&gCiDataClient.downlinkWaitFlagRef, CI_DATA_DOWNLINK_PACKET_PENDING_FLAG_MASK,
			     OSA_FLAG_OR);
}

void CiDataLoopBackModeBHandleDelayedPckt(unsigned int cid)
{
	gCiDataClient.loopBackdataBase.bufferIpPdusCounter[cid]++;
	/*PTKCQ00268259 - start */
	if (gCiDataClient.loopBackdataBase.firstpacketArrived == FALSE) {
		CI_DATA_DEBUG(1,
			      "CiDataLoopBackModeBHandleDelayedPckt: calling CiDataLoopBackModeBStartTimer, firstpacketArrived flag %d",
			      gCiDataClient.loopBackdataBase.firstpacketArrived);
		CiDataLoopBackModeBStartTimer();	/*Starts timer only after first packet arrives */
		gCiDataClient.loopBackdataBase.firstpacketArrived = TRUE;
	}
	/*PTKCQ00268259 - end */
}

unsigned char CiDataLoopBackModeBCallBack(unsigned int cid, void *packet_address, unsigned int packet_length)
{
	unsigned int retVal;
	CI_DATA_DEBUG(2, "CiDataLoopBackModeBCallBack: cid %d, delay %d, Packet# %d, status %d",
		      cid,
		      gCiDataClient.loopBackdataBase.delay,
		      gCiDataClient.loopBackdataBase.bufferIpPdusCounter[cid], gCiDataClient.loopBackdataBase.status);

	/*no delay - send DL packet back to NW... */
	retVal = CiDataLoopBackModeBUplinkDataSend(cid, packet_address, packet_length);
	CI_DATA_DEBUG(2, "CiDataLoopBackModeBCallBack: CiDataLoopBackModeBUplinkDataSend retval %d", retVal);
	if (retVal == CI_DATA_OK_FREE_IS_NEEDED || retVal == CI_DATA_OK) {
		CI_DATA_DEBUG(2,
			      "CiDataLoopBackModeBCallBack: gCiDataClient.loopBackdataBase.bufferIpPdusCounter[%d] %d",
			      cid, gCiDataClient.loopBackdataBase.bufferIpPdusCounter[cid]);
		if (gCiDataClient.loopBackdataBase.bufferIpPdusCounter[cid] > 0) {
			/*check if BUFFER_IP_PDUs */
			gCiDataClient.loopBackdataBase.bufferIpPdusCounter[cid]--;
		}
		CI_DATA_DEBUG(2, "CiDataLoopBackModeBCallBack: gCiDataClient.loopBackdataBase.status %d",
			      gCiDataClient.loopBackdataBase.status);
		if (gCiDataClient.loopBackdataBase.status == CI_DATA_LOOPBACK_B_INACTIVE) {
			/*we were disabled and timer exipred */
			CI_DATA_DEBUG(1,
				      "CiDataLoopBackModeBCallBack: gCiDataClient.loopBackdataBase.bufferIpPdusCounter[%d] %d",
				      cid, gCiDataClient.loopBackdataBase.bufferIpPdusCounter[cid]);
			if (gCiDataClient.loopBackdataBase.bufferIpPdusCounter[cid] == 0) {
				/*check if last packet and toggel back to original configuration... */
				gCiDataClient.cid[cid].serviceType =
				    gCiDataClient.loopBackdataBase.serviceType_original[cid];
				ci_data_downlink_register(cid,
						       gCiDataClient.
						       loopBackdataBase.downlinkDataReceiveCB_original[cid]);
			}
		}

		CI_DATA_DEBUG(2, "CiDataLoopBackModeBCallBack: returned TRUE!");
		return true;
	}
	CI_DATA_DEBUG(2, "CiDataLoopBackModeBCallBack: returned FALSE!");
	return false;
}

unsigned int CiDataLoopBackModeBUplinkDataSend(unsigned int origCid, void *packet_address, unsigned int packet_length)
{

	unsigned int result = 0;
	unsigned int cid = origCid;
	unsigned int queueNum;
	CiData_QueuePriorityE queuePriority;
	unsigned char sendPacket;
	char *buf;
	unsigned int dummyIndex = 0;

	CI_DATA_DEBUG(2, "CiDataLoopBackModeBUplinkDataSend: cid=%d, packet_address (original)=%x, packet_length=%d",
		      cid, (unsigned int)packet_address, packet_length);

	/*sanity checks */
	if (cid >= CI_DATA_TOTAL_CIDS) {
		CI_DATA_ASSERT_MSG_P(1, FALSE,
				     "CiDataLoopBackModeBUplinkDataSend: ERROR cid >= CI_DATA_TOTAL_CID (cid=%d)", cid);
		return CI_DATA_ERR_CID_INVALID;
	}
	if ((packet_length == 0) || (packet_address == NULL)) {
		CI_DATA_DEBUG(1, "CiDataLoopBackModeBUplinkDataSend: ERROR cid=%d, packet_length=%d , packet_address=%x",
			      cid, packet_length, (unsigned int)packet_address);
		CI_DATA_ASSERT_MSG_P(0, FALSE, "CiDataLoopBackModeBUplinkDataSend: ERROR cid=%d", cid);
		return CI_DATA_ERR_PARAMS;
	}
	if (gCiDataClient.dataStatus != CI_DATA_IS_ENABLED) {
		CI_DATA_DEBUG(1,
			      "CiDataLoopBackModeBUplinkDataSend: WARNING-loopBack data while GLOBAL data is not close LOOPBACK (cid=%d, packet_length=%d , packet_address=%x",
			      cid, packet_length, (unsigned int)packet_address);

		return CI_DATA_ERR_DATA_DISABLED;
	}

	buf = kmalloc(packet_length, GFP_KERNEL);
	if (!buf) {
		CI_DATA_DEBUG(1, "CiDataLoopBackModeBUplinkDataSend: Out Of Memory");
		return CI_DATA_ERR_PARAMS;
	}

	memcpy((void *)buf, (void *)packet_address, packet_length);

	sendPacket = CiDataUplinkProcessPacket(&cid, buf, packet_length, &queuePriority, NULL);
	CI_DATA_DEBUG(2, "CiDataLoopBackModeBUplinkDataSend: CiDataUplinkProcessPacket SendPacket = %d", sendPacket);

	if (sendPacket) {
		if (gCiDataClient.cid[cid].state == CI_DATA_CID_STATE_CLOSED) {
			CI_DATA_DEBUG(1,
				      "CiDataLoopBackModeBUplinkDataSend: WARNING-LoopBack data while cid is CLOSED (cid=%d, packet_length=%d , packet_address=%x",
				      cid, packet_length, (unsigned int)packet_address);

			return CI_DATA_ERR_CID_DISABLED;
		}

		queueNum = CI_DATA_CID_TO_QUEUE_CONVERT(cid, queuePriority);
		CI_DATA_DEBUG(2, "CiDataLoopBackModeBUplinkDataSend: origCid=%d, cid=%d, queuePriority=%d, queueNum=%d",
			      origCid, cid, queuePriority, queueNum);

		result = CiDataUplinkCopyToSharedMemory(queueNum, buf, packet_length, &dummyIndex);

		CI_DATA_DEBUG(2, "CiDataLoopBackModeBUplinkDataSend: CiDataUplinkCopyToSharedMemory result = %d",
			      (int)result);
	} else {
		/*PTKCQ00278439, PTKCQ00277866 */
		kfree(buf);	/*if sendpacket =1 then CiDataUplinkCopyToSharedMemory frees buf... */
		gCiDataDebugStatistics.uplink.numPacketsDroppedByTrafficShaping[cid]++;
	}
	return result;
}
