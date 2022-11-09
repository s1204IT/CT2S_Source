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
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/kfifo.h>
#include <linux/semaphore.h>

#include "osa_kernel.h"
#include "data_channel.h"
#include "acipc_data.h"
/*#include "pxa_dbg.h" */

#include "ac_rpc.h"
#include "allocator.h"
#include "common_datastub.h"

/* Include SHMEM debug log header */
#if 1
#include "dbgLog.h"
DBGLOG_EXTERN_VAR(shmem);
#define SHMEM_DEBUG_LOG2(a, b, c) DBGLOG_ADD(shmem, a, b, c)
#else
#define SHMEM_DEBUG_LOG2(a, b, c)
#define DBGLOG_ENABLE(a, b)
#define DBGLOG_INIT(a, b)
#endif

#define SHMEM_POOL              "ShmemPool"

#if defined(ENABLE_BUF_MEM_DEBUG)

#define BUF_MEM_SIZE        4000
#define BUF_MEM_ENTRIES     200

typedef enum {
	BUF_MEM_REQUEST = 'R',
	BUF_MEM_CONFIRM = 'C',
	BUF_MEM_INDICATION = 'I',
	BUF_MEM_RESPONSE = 'P'
} BUFMem_OperationE;

#define ADD_REQUEST(pTR, lEN)        bufMemAddOperation(BUF_MEM_REQUEST, (unsigned char *)(pTR), lEN)
#define ADD_CONFIRM(pTR, lEN)        bufMemAddOperation(BUF_MEM_CONFIRM, (unsigned char *)(pTR), lEN)
#define ADD_INDICATION(pTR, lEN)     bufMemAddOperation(BUF_MEM_INDICATION, (unsigned char *)(pTR), lEN)
#define ADD_RESPONSE(pTR, lEN)       bufMemAddOperation(BUF_MEM_RESPONSE, (unsigned char *)(pTR), lEN)

typedef struct {
	unsigned int currIndex;
	unsigned int currLocation;
	BUFMem_OperationE operations[BUF_MEM_ENTRIES];	/*Request, Confirm, Indication, resPonse */
	unsigned int dataLengths[BUF_MEM_ENTRIES];
	unsigned char dataBuffer[BUF_MEM_SIZE];

	unsigned char dataFull;

} BUFMem_DebugS;

BUFMem_DebugS bufMemDebug;

void bufMemAddOperation(BUFMem_OperationE operation, unsigned char *ptr, unsigned int len)
{
	if (bufMemDebug.dataFull || (bufMemDebug.currIndex >= BUF_MEM_ENTRIES)
	    || ((bufMemDebug.currLocation + len) >= BUF_MEM_SIZE)) {
		bufMemDebug.dataFull = TRUE;
		return;
	}

	bufMemDebug.operations[bufMemDebug.currIndex] = operation;
	bufMemDebug.dataLengths[bufMemDebug.currIndex] = len;
	memcpy(bufMemDebug.dataBuffer + bufMemDebug.currLocation, ptr, len);

	bufMemDebug.currIndex++;
	bufMemDebug.currLocation += len;
}

void debugBufMemPrintLog(void)
{
}

#else /*ENABLE_BUF_MEM_DEBUG */
#define ADD_REQUEST(pTR, lEN)
#define ADD_CONFIRM(pTR, lEN)
#define ADD_INDICATION(pTR, lEN)
#define ADD_RESPONSE(pTR, lEN)
#endif /*ENABLE_BUF_MEM_DEBUG */

void CI_SHM_KERN_DataRxInd_IND_RSP(void *ptr, unsigned int len);
void CI_SHM_KERN_txFailCtrlInd(void *ptr);

unsigned char gDlLinkStatusFlag = FALSE;
int dataSvgHandle;
static unsigned char gIsInitialized = FALSE;
static OsaRefT gACIPCD_ShmemRef;

#if defined(ENABLE_BUF_PRINT)
static void BufPrint(unsigned char *ptr, int len)
{
}
#endif /*ENABLE_BUF_PRINT */

void add_to_handle_list(DATAHANDLELIST *newdrvnode)
{
}

int search_handlelist_for_csd_cid(void)
{
	return -1;
}

int search_handlelist_by_cid(unsigned char cid, DATAHANDLELIST **ppBuf)
{
	return -1;
}

void remove_handle_list(void)
{
}

void remove_handle_by_cid(unsigned char cid)
{
}

int dataChanRemoveFromMsgQList(MsgQRef msgQ)
{
	return -1;
}

unsigned int dataChanFindContextByCei(unsigned int cei)
{
	return (unsigned int)NULL;
}

MsgQRef dataChanFindMsgQByCei(unsigned int cei)
{
	return NULL;
}

MsgQRef dataChanFindMsgQByContext(unsigned int userContext)
{
	/* Do not find */
	return NULL;
}

int dataChannelRelease(unsigned int cei, MsgQRef msgQ)
{
	return PCAC_SUCCESS;
}

int dataChanConnect(unsigned int cei, unsigned int userContext, MsgQRef msgQ)
{
	return PCAC_ERROR;
}

int dataChanActivate(unsigned int cei, MsgQRef msgQ)
{
	return PCAC_ERROR;
}

/************************************************************************************************************************/
/************************************************************************************************************************/
/************************************************************************************************************************/

void CI_DataLinkStatusInd_IND_RSP(unsigned char linkStatus)
{
}

void CI_DataWatermarkInd_IND_RSP(ACIPCD_WatermarkTypeE watermarkType)
{
}

/*this is the callback when an IND packet is received */
void CI_DataRxInd_IND_RSP(void *ptr, unsigned int len)
{
}

void CI_DataReqMaxLimitHandler_REQ_CNF(unsigned int size, unsigned char isTx)
{
}

unsigned long CI_DataReqMaxLimitSet_CONTROL(unsigned int newMaxLimit)
{

	return 1;
}

void CI_DataWatermarkInd_CONTROL(ACIPCD_WatermarkTypeE watermarkType)
{
}

void CI_DataLinkStatusInd_CONTROL(unsigned char linkStatus)
{

}

static void CI_DataReqMaxLimitSendRxDoneRsp_CONTROL(void *pBuff)
{
	ACIPCDRxDoneRsp(ACIPCD_CI_DATA_CONTROL_OLD, pBuff);
}

void CI_DataRxInd_CONTROL(void *ptr, unsigned int len)
{

	if (len != sizeof(unsigned int))
		ASSERT(FALSE);

	CI_DataReqMaxLimitSet_CONTROL((unsigned int)ptr);	/*convert the pointer to actual data */

	CI_DataReqMaxLimitSendRxDoneRsp_CONTROL(ptr);
}

void CI_DataTransmitCommand_CONTROL(void *pData, int iSize)
{

	ACIPCDTxReq(ACIPCD_CI_DATA_CONTROL_OLD, pData, iSize);

	return;
}

void CI_DataLinkStatusInd_REQ_CNF(unsigned char linkStatus)
{
}

void CI_DataWatermarkInd_REQ_CNF(ACIPCD_WatermarkTypeE watermarkType)
{
}

void CI_DataRxInd_REQ_CNF(void *ptr, unsigned int len)
{

}

void CI_DataTxDoneInd_REQ_CNF(void *ptr)
{
}

void CI_DataTxFailInd_REQ_CNF(void *ptr)
{
}

void sendDataReqtoInternalList(unsigned char cid, char *buf, int len)
{
}

void InitDataChannel(unsigned int srcSapi)
{

	if (!gIsInitialized) {
		gACIPCD_ShmemRef = OsaMemGetPoolRef(SHMEM_POOL, NULL, NULL);

		gIsInitialized = TRUE;
	}
}

void DeinitDataChannel(void)
{

#if defined(ENABLE_BUF_MEM_DEBUG)
	memset(&bufMemDebug, 0, sizeof(bufMemDebug));
#endif /*ENABLE_BUF_MEM_DEBUG */

}

typedef void (*data_rx_callback_func) (const unsigned char *packet, unsigned int len, unsigned char cid);

int registerRxCallBack(SVCTYPE pdpTye, data_rx_callback_func callback)
{
	return 0;
}

int unregisterRxCallBack(SVCTYPE pdpTye)
{
	return TRUE;
}

void GetCallbacksKernelFn(ACIPCD_CallBackFuncS *pCallBackFunc, unsigned int callbackType)	/*callbackType=0 for REQ_ONF, callbackType=1 for IND_RSP */
{
	memset(pCallBackFunc, 0, sizeof(ACIPCD_CallBackFuncS));

	switch (callbackType) {
	case 0:		/*ACIPCD_CI_DATA_REQ_CNF */
		pCallBackFunc->RxIndCB = CI_DataRxInd_REQ_CNF;
		pCallBackFunc->LinkStatusIndCB = CI_DataLinkStatusInd_REQ_CNF;

		pCallBackFunc->WatermarkIndCB = CI_DataWatermarkInd_REQ_CNF;

		pCallBackFunc->TxDoneCnfCB = CI_DataTxDoneInd_REQ_CNF;
		pCallBackFunc->TxFailCnfCB = CI_DataTxFailInd_REQ_CNF;
		break;

	case 1:		/*ACIPCD_CI_DATA_IND_RSP */
		pCallBackFunc->RxIndCB = CI_DataRxInd_IND_RSP;
		pCallBackFunc->LinkStatusIndCB = CI_DataLinkStatusInd_IND_RSP;

		pCallBackFunc->WatermarkIndCB = CI_DataWatermarkInd_IND_RSP;

		/*pCallBackFunc->TxDoneCnfCB      = &CI_SHM_KERN_txDoneInd; not needed here */
		/*pCallBackFunc->TxFailCnfCB      = &CI_SHM_KERN_txFailInd; not needed here */
		break;

	case 2:		/*ACIPCD_CI_DATA_CONTROL_OLD */
		pCallBackFunc->RxIndCB = CI_DataRxInd_CONTROL;
		pCallBackFunc->LinkStatusIndCB = CI_DataLinkStatusInd_CONTROL;

		pCallBackFunc->WatermarkIndCB = CI_DataWatermarkInd_CONTROL;
		printk("\nYG:Kernel pCallBackFunc->WatermarkIndCB %p, LinkStatusIndCB %p\n",
		       pCallBackFunc->WatermarkIndCB, pCallBackFunc->LinkStatusIndCB);

		/*pCallBackFunc->TxDoneCnfCB      = &CI_SHM_KERN_txDoneInd; not needed here */
		/*pCallBackFunc->TxFailCnfCB      = &CI_SHM_KERN_txFailInd; not needed here */
		break;

	default:
		ASSERT(FALSE);
		break;
	}
}

void UpdatePendingDataBuffersPtr(unsigned int *ptr)
{
}

void SetPendingDataBufferValue(unsigned int value)
{
}

void DataChannelKernelPrintInfo(void)
{
#if defined(ENABLE_BUF_MEM_DEBUG)
	debugBufMemPrintLog();
#else /*ENABLE_BUF_MEM_DEBUG */
	printk("\n\n**** WARNING: DataChannelKernelPrintInfo not yet implemented (YG) !!!! ****\n\n");
#endif /*ENABLE_BUF_MEM_DEBUG */

}

/*****************************************************************************************************\
 * CSD                                                                                               *
\*****************************************************************************************************/

EXPORT_SYMBOL(InitDataChannel);
EXPORT_SYMBOL(DeinitDataChannel);

EXPORT_SYMBOL(registerRxCallBack);
EXPORT_SYMBOL(unregisterRxCallBack);

EXPORT_SYMBOL(dataChanConnect);
EXPORT_SYMBOL(dataChanActivate);

EXPORT_SYMBOL(sendDataReqtoInternalList);
EXPORT_SYMBOL(GetCallbacksKernelFn);

EXPORT_SYMBOL(dataChanFindMsgQByContext);
EXPORT_SYMBOL(dataChanFindMsgQByCei);

EXPORT_SYMBOL(dataSvgHandle);
EXPORT_SYMBOL(gDlLinkStatusFlag);
EXPORT_SYMBOL(add_to_handle_list);
EXPORT_SYMBOL(search_handlelist_by_cid);
EXPORT_SYMBOL(remove_handle_list);
EXPORT_SYMBOL(remove_handle_by_cid);
EXPORT_SYMBOL(search_handlelist_for_csd_cid);
