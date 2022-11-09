/*------------------------------------------------------------
(C) Copyright [2006-2011] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
	Title:  	 CI Data Client Shmem
	Filename:    ci_data_client_shmem.c
	Description: Contains implementation/interface with SHMEM
				 for R7/LTE data path
	Author: 	 Yossi Gabay
************************************************************************/

#include <linux/dma-mapping.h>
#include <asm/cacheflush.h>
#include <linux/wait.h>

#include "osa_kernel.h"
#include "acipc_data.h"
#include "acipc.h"

#include "ci_data_client.h"
#include "ci_data_client_shmem.h"
#include "ci_data_client_ltm.h"
#include "ci_data_client_mem.h"

extern unsigned int ConvertPhysicalToVirtualAddr(unsigned int PhysicalAddr);

/************************************************************************
 * Internal Prototypes
 ***********************************************************************/

/************************************************************************
 * Internal Globals
 ***********************************************************************/
#define CI_DATA_UPLINK_TIMEOUT  	100
extern CiData_UplinkPoolMemS gCiDataUplinkPoolMem;

/***********************************************************************************\
* Functions
************************************************************************************/

/***********************************************************************************\
*   Name:   	 CiDataClientRegisterItmAllocator
*
*   Description: Register as ITM allocator
*
*   Params:
*
*   Return:
\***********************************************************************************/
static void CiDataClientRegisterItmAllocator(void)
{
/* For now we disable reduce uplink copy optimization until
   We will add support for silent reset */
#if 0
	struct itm_allocator allocator;
	CI_DATA_DEBUG(1, "CiDataClientRegisterItmAllocator called");

	/* Remove uplink (RNDIS->CI && WIFI->CI) optimization support */
	allocator.malloc = CiDataUplinkMemDataAlloc;
	allocator.free = CiDataUplinkMemDataFree;
	itm_register_allocator(&allocator, "SHMEM");
#endif
}

/***********************************************************************************\
*   Name:   	 CiDataControl_RxInd
*
*   Description: RX indication for uplink
*
*   Params: 	 ptr - pointer to RX buffer
*   			 len - length of buffer
*
*   Return:
\***********************************************************************************/
static void CiDataControl_RxInd(void *ptr, unsigned int len)
{
	ACIPCD_ReturnCodeE rc;

	CI_DATA_DEBUG(2, "CiDataControl_RxInd ptr(data)=%x, len(opcode)=%d", (unsigned int)ptr, len);

	/*'len' is the 'command-opcode', and ptr is the 'command-content' (if required) */
	switch (len) {
	/*below 0x100 are regular opcodes. 0x100 is the mask for CID command */
	case CI_DATA_SHARED_AREA_ADDRESS_NOTIFY:
#if defined (CI_DATA_FEATURE_DESCRIPTORS_CACHED)
		{
			void *baseAddressCached;

			gCiDataClient.pSharedArea = (CiData_SharedAreaS *) CI_DATA_COMADDR_TO_APPS_PHSYCIAL_ADDR(ptr);
			CI_DATA_DEBUG(2, "CiDataControl_RxInd: set gCiDataClient.pSharedArea=%x (orig ptr=%x)",
					  (unsigned int)gCiDataClient.pSharedArea, (unsigned int)ptr);

			/*set as cache and get new cached address */
			baseAddressCached = __arm_ioremap((unsigned int)gCiDataClient.pSharedArea,
							  sizeof(CiData_SharedAreaS), MT_DEVICE_CACHED);

			CI_DATA_DEBUG(2,
					  "Downlink CI_DATA_SHARED_AREA_ADDRESS_NOTIFY: CACHED descriptors remapped memory location. baseAddress=%x, CACHED baseAddressCached=%x, size=%d",
					  (unsigned int)gCiDataClient.pSharedArea, (unsigned int)baseAddressCached,
					  sizeof(CiData_SharedAreaS));

			/* Return cached address */
			gCiDataClient.pSharedArea = (CiData_SharedAreaS *) baseAddressCached;

			CI_DATA_DEBUG(2, "CiDataControl_RxInd, before memset");
			memset((void *)&gCiDataClient.pSharedArea->uplink, 0,
				   sizeof(CiData_UplinkControlAreaDatabaseS));

			CI_DATA_DEBUG(2, "CiDataControl_RxInd, before FLUSH");
			CI_DATA_DESCRIPTORS_CACHE_FLUSH(&gCiDataClient.pSharedArea->uplink,
							sizeof(CiData_UplinkControlAreaDatabaseS));
			CI_DATA_DEBUG(2, "CiDataControl_RxInd, after FLUSH");
		}
#else
		/*the 'ptr' contain the address of shared area */
		gCiDataClient.pSharedArea = (CiData_SharedAreaS *) CI_DATA_COMADDR_TO_APPADDR(ptr);
		CI_DATA_DEBUG(2, "CiDataControl_RxInd: set gCiDataClient.pSharedArea=%x (orig ptr=%x)",
				  (unsigned int)gCiDataClient.pSharedArea, (unsigned int)ptr);

		memset((void *)&gCiDataClient.pSharedArea->uplink, 0, sizeof(CiData_UplinkControlAreaDatabaseS));
#endif
		/*Must be done after SharedArea has been init due to the task is using downlink as local */
		/*YG:...since the common shared-area is now initialized - we can start the downlink task */
		/*CiDataDownlinkTaskInit(); */
		break;

	case CI_DATA_DOWNLINK_QUEUE_START_ADDRESS_NOTIFY:
		gCiDataClient.downlinkQueueInfo.baseAddress = (unsigned int)ptr;
		CI_DATA_DEBUG(2, "Downlink CI_DATA_DOWNLINK_QUEUE_START_ADDRESS_NOTIFY: ORIG COMM ptr=%x",
				  gCiDataClient.downlinkQueueInfo.baseAddress);
		/*
		   / * gCiDataClient.downlinkQueueInfo.baseAddress = (unsigned int)CI_DATA_COMADDR_TO_APPADDR(ptr); * /
		   gCiDataClient.downlinkQueueInfo.baseAddress = (unsigned int)CI_DATA_COMADDR_TO_APPS_PHSYCIAL_ADDR(ptr);
		   CI_DATA_DEBUG(2, "Downlink CI_DATA_DOWNLINK_QUEUE_START_ADDRESS_NOTIFY: ORIG COMM ptr=%x, APPS PHYS baseAddress=%x",
		   (unsigned int)ptr, gCiDataClient.downlinkQueueInfo.baseAddress); */
		break;

	case CI_DATA_DOWNLINK_QUEUE_SIZE_NOTIFY:
		if (gCiDataClient.downlinkQueueInfo.baseAddress) {  /*make sure address was set properly */
/*#if 1 						//using downlink cache */
#if defined (CI_DATA_FEATURE_CACHED_ENABLED)
			void *baseAddressCached;

			gCiDataClient.downlinkQueueInfo.size = (unsigned int)ptr;

			/*convert to physical address */
			gCiDataClient.downlinkQueueInfo.baseAddress =
				(unsigned int)CI_DATA_COMADDR_TO_APPS_PHSYCIAL_ADDR(gCiDataClient.downlinkQueueInfo.
										baseAddress);

			/*set as cache and get new cached address */
			baseAddressCached = __arm_ioremap(gCiDataClient.downlinkQueueInfo.baseAddress,
							  gCiDataClient.downlinkQueueInfo.size, MT_DEVICE_CACHED);

			CI_DATA_DEBUG(2,
					  "Downlink CI_DATA_DOWNLINK_QUEUE_SIZE_NOTIFY: CACHED remapped memory location. baseAddress=%x, CACHED baseAddressCached=%x, size=%d",
					  gCiDataClient.downlinkQueueInfo.baseAddress, (unsigned int)baseAddressCached,
					  gCiDataClient.downlinkQueueInfo.size);

			/*overwrite original address (//YG: is there a need for the original???) */
			gCiDataClient.downlinkQueueInfo.baseAddress = (unsigned int)baseAddressCached;

#else
			gCiDataClient.downlinkQueueInfo.baseAddress =
				(unsigned int)CI_DATA_COMADDR_TO_APPADDR(gCiDataClient.downlinkQueueInfo.baseAddress);
			CI_DATA_DEBUG(2,
					  "Downlink CI_DATA_DOWNLINK_QUEUE_SIZE_NOTIFY: APPS converted address (not CACHED) baseAddress=%x, size=%d",
					  gCiDataClient.downlinkQueueInfo.baseAddress,
					  gCiDataClient.downlinkQueueInfo.size);
#endif /*using downlink cache */

		} else {
			CI_DATA_ASSERT_MSG_P(0, FALSE, "downlink queue size set, but not baseAddress (length=%d)",
						 (unsigned int)ptr);
		}
		break;

	case CI_DATA_UPLINK_CID_QUEUE_START_ADDRESS_NOTIFY:
		gCiDataClient.uplinkInfo.baseAddress = (unsigned int)ptr;
		if ((((unsigned int)ptr) & CI_DATA_CACHE_LINE_MASK) != 0) {
			CI_DATA_ASSERT_MSG_P(0, FALSE, "Uplink Base address (ptr) not aligned, ptr=%x",
						 (unsigned int)ptr);
		}
		CI_DATA_DEBUG(2, "Uplink CI_DATA_CID_QUEUE_START_ADDRESS_NOTIFY, ORIG COMM address=%x",
				  gCiDataClient.uplinkInfo.baseAddress);
		break;

	case CI_DATA_UPLINK_CID_QUEUE_SIZE_NOTIFY:
		gCiDataClient.uplinkInfo.size = (unsigned int)ptr;

		/*make sure baseAddress was set before */
		if (gCiDataClient.uplinkInfo.baseAddress) { /*make sure address was set properly */
			void *baseAddressCached;
			unsigned int origCommAddress = gCiDataClient.uplinkInfo.baseAddress;

			gCiDataClient.uplinkInfo.baseAddress =
				(unsigned int)CI_DATA_COMADDR_TO_APPS_PHSYCIAL_ADDR(gCiDataClient.uplinkInfo.baseAddress);

			baseAddressCached = __arm_ioremap(gCiDataClient.uplinkInfo.baseAddress,
							  gCiDataClient.uplinkInfo.size, MT_DEVICE_CACHED);

			CI_DATA_DEBUG(2,
					  "Uplink CI_DATA_CID_QUEUE_SIZE_NOTIFY - CACHED remapped memory location. COMM Address=%x, APPS baseAddress=%x, CACHED baseAddressCached=%x, size=%d",
					  origCommAddress, gCiDataClient.uplinkInfo.baseAddress,
					  (unsigned int)baseAddressCached, gCiDataClient.uplinkInfo.size);

			/*overwrite original address */
			gCiDataClient.uplinkInfo.baseAddress = (unsigned int)baseAddressCached;

			CiDataUplinkMemCreatePool((void *)gCiDataClient.uplinkInfo.baseAddress,
						  gCiDataClient.uplinkInfo.size);

			/*register ci-data-buffer-manager onto ITM */
			CiDataClientRegisterItmAllocator();
			gCiDataClient.inSilentReset = FALSE;	/*in case we are in silent-reset - exit */

			/*register ci-data-ltm onto RNDIS Add-on - only AFTER mem-pool init */
			/*CiDataClientLtmRegister(); */
		} else {
			CI_DATA_ASSERT_MSG_P(0, FALSE, "Uplink buffer size set, but not baseAddress (size=%d)",
						 gCiDataClient.uplinkInfo.size);
		}
		break;

	default:
		CI_DATA_ASSERT_MSG_P(0, FALSE, "CiDataControl_RxInd: Unknown command %x", len);
		break;
	}

	rc = ACIPCDRxDoneRsp(ACIPCD_CI_DATA_CONTROL, ptr);
	if (rc != ACIPCD_RC_OK) {
		CI_DATA_ASSERT_MSG_P(0, FALSE, "(rc=%d)", rc);
		/*CI_DATA_ASSERT(rc == ACIPCD_RC_OK); */
	}

}

/***********************************************************************************\
*   Name:   	 CiDataControl_LinkStatusInd
*
*   Description: indication to link status changes
*
*   Params: 	 linkStatus - list new state (TRUE = OK, FALSE = not OK)
*
*   Return:
\***********************************************************************************/
static void CiDataControl_LinkStatusInd(unsigned char linkStatus)
{
	CI_DATA_DEBUG(1, "CiDataControl_LinkStatusInd = %d", linkStatus);
	CiDataLinkStatusHandler(CI_DATA_CHANNEL_CONTROL, linkStatus);
}

/***********************************************************************************\
*   Name:   	 CiDataControl_WatermarkInd
*
*   Description: watermark state indication
*
*   Params: 	 watermarkType - watermark new level (high/low)
*
*   Return:
\***********************************************************************************/
static void CiDataControl_WatermarkInd(ACIPCD_WatermarkTypeE watermarkType)
{
	CI_DATA_ASSERT_MSG_P(0, FALSE, "CiDataControl_WatermarkInd (watermarkType=%d)", watermarkType);
}

/***********************************************************************************\
*   Name:   	 CiDataControl_TxDoneCnf
*
*   Description: confirm TX packet (RxDoneRsp has been called on RX side)
*
*   Params: 	 ptr - pointer to buffer that its transmission is confirmed
*
*   Return:
\***********************************************************************************/
static void CiDataControl_TxDoneCnf(void *ptr)
{
	/*nothing to do... */
}

/***********************************************************************************\
*   Name:   	 CiDataControl_TxDoneCnf
*
*   Description: packet transmission has failed
*
*   Params: 	 ptr - pointer to buffer that its transmission has failed
*
*   Return:
\***********************************************************************************/
static void CiDataControl_TxFailCnf(void *ptr)
{
	CI_DATA_ASSERT_MSG_P(0, FALSE, "CiDataControl_TxDoneCnf (ptr=%x)", (unsigned int)ptr);
}

/***********************************************************************************\
*   Name:   	 CiDataDownlink_RxInd
*
*   Description: RX indication for downlink
*
*   Params: 	 ptr - pointer to RX buffer
*   			 len - length of buffer
*
*   Return:
\***********************************************************************************/
static void CiDataDownlink_RxInd(void *ptr, unsigned int len)
{
	/*ACIPCD_ReturnCodeE rc; */
	OSA_STATUS osaStatus;

	CI_DATA_DEBUG(3, "CiDataDownlink_RxInd called, ptr=%x, len=%d", (unsigned int)ptr, len);

	/*'len' is the command-opcode */
	/*if((len == (unsigned int)CI_DATA_DOWNLINK_DATA_START_NOTIFY) || */
	if ((len ^ (unsigned int)ptr) == 0) {
		/*Shmem Event - ptr == len == 0xFFFF */
		CI_DATA_DEBUG(5, "CiDataDownlink_RxInd CI_DATA_DOWNLINK_DATA_START_NOTIFY received");

		/*
		   rc = ACIPCDRxDoneRsp(ACIPCD_CI_DATA_DOWNLINK, ptr);
		   if(rc != ACIPCD_RC_OK)
		   {
		   CI_DATA_ASSERT_MSG_P(0, rc == ACIPCD_RC_OK, "ACIPCDRxDoneRsp returned error (rc=%d)", rc);
		   }
		 */
		CI_DATA_DEBUG(5, "CiDataDownlink_RxInd: Packet received, dataStatus=%d", gCiDataClient.dataStatus);

		/*make sure data is enabled (downlink task is up & running) */
		/*YG: maybe need to throw packets when data is not enabled - read & throw */
		if (gCiDataClient.dataStatus == CI_DATA_IS_ENABLED) {
			/*kick downlink task */
			osaStatus =
				FLAG_SET(&gCiDataClient.downlinkWaitFlagRef, CI_DATA_DOWNLINK_PACKET_PENDING_FLAG_MASK,
					 OSA_FLAG_OR);
			CI_DATA_ASSERT(osaStatus == OS_SUCCESS);
		}
	} else {		/*unknown command */
		CI_DATA_ASSERT(FALSE);
	}
}

/***********************************************************************************\
*   Name:   	 CiDataDownlink_LinkStatusInd
*
*   Description: indication to link status changes
*
*   Params: 	 linkStatus - list new state (TRUE = OK, FALSE = not OK)
*
*   Return:
\***********************************************************************************/
static void CiDataDownlink_LinkStatusInd(unsigned char linkStatus)
{
	CI_DATA_DEBUG(1, "CiDataDownlink_LinkStatusInd = %d", linkStatus);
	CiDataLinkStatusHandler(CI_DATA_CHANNEL_DOWNLINK, linkStatus);
}

/***********************************************************************************\
*   Name:   	 CiDataDownlink_WatermarkInd
*
*   Description: watermark state indication
*
*   Params: 	 watermarkType - watermark new level (high/low)
*
*   Return:
\***********************************************************************************/
static void CiDataDownlink_WatermarkInd(ACIPCD_WatermarkTypeE watermarkType)
{
	/*CI_DATA_ASSERT(FALSE); */

	if (watermarkType == ACIPCD_WATERMARK_HIGH) {
		CI_DATA_DEBUG(1, "*** CiDataDownlink_WatermarkInd - HIGH !!!!\n");
	} else {
		CI_DATA_DEBUG(1, "*** CiDataDownlink_WatermarkInd - LOW...\n");
	}

}

/***********************************************************************************\
*   Name:   	 CiDataDownlink_TxDoneCnf
*
*   Description: confirm TX packet (RxDoneRsp has been called on RX side)
*
*   Params: 	 ptr - pointer to buffer that its transmission is confirmed
*
*   Return:
\***********************************************************************************/
static void CiDataDownlink_TxDoneCnf(void *ptr)
{
}

/***********************************************************************************\
*   Name:   	 CiDataDownlink_TxFailCnf
*
*   Description: packet transmission has failed
*
*   Params: 	 ptr - pointer to buffer that its transmission has failed
*
*   Return:
\***********************************************************************************/
static void CiDataDownlink_TxFailCnf(void *ptr)
{
	CI_DATA_ASSERT(FALSE);
}

/***********************************************************************************\
*   Name:   	 CiDataUplinkDataStartNotifySend
*
*   Description: if required -> Send CI_DATA_UPLINK_DATA_START_NOTIFY to COMM
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataUplinkDataStartNotifySend(unsigned char kickTimer)
{
	CI_DATA_CACHE_INVALIDATE((void *)&gCiDataClient.pSharedArea->uplink.ciDataServerActive,
				 sizeof(gCiDataClient.pSharedArea->uplink.ciDataServerActive));

	if (!gCiDataClient.pSharedArea->uplink.ciDataServerActive) {
		ACIPCEventSetExt(ACSIPC, ACIPC_CI_DATA_UPLINK);
	}
	/* in case that COMM is not active and there is data waiting in the queues, */
	/* start a timer to prevent long delay on low throughputs */
	if (kickTimer == TRUE) {
		mod_timer(&gCiDataClient.uplinkStartNotifyTimeoutTimer,
			  jiffies + msecs_to_jiffies(CI_DATA_UPLINK_TIMEOUT));
	}
}

/***********************************************************************************\
*   Name:   	 CiDataUplinkTimeoutTimer
*
*   Description: Send CI_DATA_UPLINK_DATA_START_NOTIFY to COMM
*
*   Params:
*
*   Return:
\***********************************************************************************/
static void CiDataUplinkTimeoutTimer(unsigned long data)
{
	unsigned int queueNum;
	CiData_UplinkControlAreaDatabaseS *uplink = &gCiDataClient.pSharedArea->uplink;

	CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(&uplink->nextReadIdx, sizeof(uplink->nextReadIdx));

	for (queueNum = 0; queueNum < CI_DATA_UPLINK_TOTAL_QUEUES; queueNum++) {
		if (uplink->nextReadIdx[queueNum] != uplink->nextWriteIdx[queueNum]) {
			CI_DATA_DEBUG(5, "*** CiDataUplinkTimeoutTimer - Notify COMM !!!!\n");
			CiDataUplinkDataStartNotifySend(0);
			break;
		}
	}
}

static inline void CiDataUplinkTimeoutTimerInit(void)
{
	init_timer_deferrable(&gCiDataClient.uplinkStartNotifyTimeoutTimer);
	gCiDataClient.uplinkStartNotifyTimeoutTimer.function = CiDataUplinkTimeoutTimer;
}

/***********************************************************************************\
*   Name:   	 CiDataShmemRegister
*
*   Description: register on SHMEM services
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataClientShmemRegister(void)
{
	ACIPCD_CallBackFuncS callBackFuncs;
	ACIPCD_ConfigS config;
	ACIPCD_ReturnCodeE rc;

	/* ACIPCD_CI_DATA_CONTROL registration */
	memset(&callBackFuncs, 0, sizeof(callBackFuncs));
	callBackFuncs.RxIndCB = CiDataControl_RxInd;
	callBackFuncs.LinkStatusIndCB = CiDataControl_LinkStatusInd;
	callBackFuncs.WatermarkIndCB = CiDataControl_WatermarkInd;
	callBackFuncs.TxDoneCnfCB = CiDataControl_TxDoneCnf;
	callBackFuncs.TxFailCnfCB = CiDataControl_TxFailCnf;

	memset(&config, 0, sizeof(config));
	config.HighWm = CI_DATA_CONTROL_APPS_HIGH_WATERMARK;
	config.LowWm = CI_DATA_CONTROL_APPS_LOW_WATERMARK;
	config.RxAction = ACIPCD_USE_ADDR_AS_PARAM;
	config.TxAction = ACIPCD_USE_ADDR_AS_PARAM;
	/*YG: need to change to structure transfer */
	/*config.RxAction   				= ACIPCD_HANDLE_CACHE; */
	/*config.TxAction   				= ACIPCD_COPY; */
	config.BlockOnRegister = FALSE;

	rc = ACIPCDRegister(ACIPCD_CI_DATA_CONTROL, &callBackFuncs, &config);
	CI_DATA_ASSERT_MSG_P(0, rc == ACIPCD_RC_OK, "ACIPCD_CI_DATA_CONTROL init failed (rc=%d)", rc);

	CiDataLinkStatusHandler(CI_DATA_CHANNEL_UPLINK, 1); /*  Uplink uses a direct ACIPC event. */
	CiDataUplinkTimeoutTimerInit();

	/* ACIPCD_CI_DATA_DOWNLINK registration */
	memset(&callBackFuncs, 0, sizeof(callBackFuncs));
	callBackFuncs.RxIndCB = CiDataDownlink_RxInd;
	callBackFuncs.LinkStatusIndCB = CiDataDownlink_LinkStatusInd;
	callBackFuncs.WatermarkIndCB = CiDataDownlink_WatermarkInd;
	callBackFuncs.TxDoneCnfCB = CiDataDownlink_TxDoneCnf;
	callBackFuncs.TxFailCnfCB = CiDataDownlink_TxFailCnf;

	memset(&config, 0, sizeof(config));
	config.HighWm = CI_DATA_DOWNLINK_APPS_HIGH_WATERMARK;
	config.LowWm = CI_DATA_DOWNLINK_APPS_LOW_WATERMARK;
	config.RxAction = ACIPCD_USE_ADDR_AS_PARAM;
	config.TxAction = ACIPCD_USE_ADDR_AS_PARAM;
	config.BlockOnRegister = FALSE;

	rc = ACIPCDRegister(ACIPCD_CI_DATA_DOWNLINK, &callBackFuncs, &config);
	CI_DATA_ASSERT_MSG_P(0, rc == ACIPCD_RC_OK, "ACIPCD_CI_DATA_DOWNLINK init failed (rc=%d)", rc);
}
