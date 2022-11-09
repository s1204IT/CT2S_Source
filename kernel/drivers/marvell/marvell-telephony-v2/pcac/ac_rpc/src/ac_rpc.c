/*--------------------------------------------------------------------------------------------------------------------
(C) Copyright 2006, 2007 Marvell DSPC Ltd. All Rights Reserved.
-------------------------------------------------------------------------------------------------------------------*/

/*******************************************************************************
 *                      M O D U L E     B O D Y
 *******************************************************************************
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
 *******************************************************************************
 *
 * Title: Generic RPC implementation over Shared Memory
 *
 * Filename: ac_rpc.c
 *
 * Author:   Boaz Sommer
 *
 * Remarks: -
 *
 * Created: 15/09/2010
 *
 * Notes:
 ******************************************************************************/
#include <linux/hardirq.h>
#include <linux/kthread.h>
#include <linux/kfifo.h>
#include <linux/semaphore.h>

#include	"acipc_data.h"
#include	"ac_rpc.h"
#include	"allocator.h"

#define     SHMEM_POOL              "ShmemPool"

#if defined (DIP_CHANNEL_ENABLE)
#include    "dip_channel_server.h"
#endif

static struct kfifo gpcMsgQ;
static int gpcMsgQ_status;
static spinlock_t gpcMsgQ_lock;
static struct kfifo genRpcMsgQ;
static int genRpcMsgQ_status;
static spinlock_t genRpcMsgQ_lock;
static struct semaphore genRpcMsgQ_sem;

static int genRpcTask_status;

#define AC_RPC_TASK_STACK_SIZE		1024
#define AC_RPC_TASK_PRIORITY		130
#define GPC_HWM_SIZE				10
#define GPC_LWM_SIZE				5

char RPC_TaskStack[AC_RPC_TASK_STACK_SIZE];

#define RPC_MSG_QUEUE_SIZE 256

typedef struct {
	ACIPCD_ServiceIdE ServiceId;
	RPC_DataS *pRpcMsg;
	unsigned long Length;
} RPC_QueueMsgS;

typedef enum {
	eClosed = 0,
	eNotActive,
	eHighWm,
	eActive
} ConnectionStateE;
static unsigned char bRpcConnected;	/* = FALSE; */
static ConnectionStateE eGpcState;	/* = eClosed ; */

static OsaRefT RPC_ShmemRef;

typedef struct {
	RpcFuncType Ptr;
	char Name[MAX_FUNC_NAME_LENGTH];
} RPC_FuncEntry;

typedef struct {
	unsigned char NumEntries;
	RPC_FuncEntry Func[MAX_RPC_FUNC_ENTRIES];
} RPC_FunctionsDbS;

static RPC_FunctionsDbS RpcFunctionsDb;

static int RPCFindFuncIndexByName(char *name)
{
	int i, funcIndex = -1;

	for (i = 0; (i < RpcFunctionsDb.NumEntries) && (funcIndex == -1); i++) {
		if (strcmp(RpcFunctionsDb.Func[i].Name, name) == 0) {
			funcIndex = i;
		}
	}
	return funcIndex;
}

static RpcFuncType RPCFindFuncByName(char *name)
{
	int i;
	RpcFuncType rc = NULL;

	for (i = 0; (i < RpcFunctionsDb.NumEntries) && (rc == NULL); i++) {
		if (strcmp(RpcFunctionsDb.Func[i].Name, name) == 0) {
			rc = RpcFunctionsDb.Func[i].Ptr;
		}
	}
	return rc;
}

static int GENRpcTask(void *argv)
{
	RPC_QueueMsgS Msg;
	RpcFuncType pCallBack;
	char *pFuncName;
	int ret;

	while (TRUE) {
		if (down_interruptible(&genRpcMsgQ_sem)) {
			/* Waiting is aborted (by destroy telephony)
			 * Exit the thread */
			break;	/*break the loop so can exit nicely */
		}

		ret =
		    kfifo_out_locked(&genRpcMsgQ, &Msg, sizeof(Msg),
				     &genRpcMsgQ_lock);
		ASSERT(ret == sizeof(Msg));
		pFuncName = (char *)Msg.pRpcMsg->Data;

		/* Find RPC function in database */
		pCallBack = RPCFindFuncByName(pFuncName);

		/* If found, call it */
		if (pCallBack) {
			Msg.pRpcMsg->rc =
			    pCallBack(Msg.pRpcMsg->DataInLen,
				      &Msg.pRpcMsg->Data[Msg.pRpcMsg->
							 DataInIndex],
				      Msg.pRpcMsg->DataOutLen,
				      &Msg.pRpcMsg->Data[Msg.pRpcMsg->
							 DataOutIndex]);
		}
		/* Else, return error */
		else {
			Msg.pRpcMsg->rc = (unsigned long)RPC_NO_FUNCTION;
		}

		/*
		 * Let Client Know the Operation Ended.
		 */
		ACIPCDRxDoneRsp(Msg.ServiceId, Msg.pRpcMsg);
	}
	return 0;
}

static void RPCMsgInd(void *pData, unsigned int Length)
{
	RPC_QueueMsgS Msg;
	int ret;

	Msg.ServiceId = ACIPCD_GEN_RPC;
	Msg.pRpcMsg = pData;
	Msg.Length = Length;

	ret = kfifo_in_locked(&genRpcMsgQ, &Msg, sizeof(Msg), &genRpcMsgQ_lock);
	ASSERT(ret == sizeof(Msg));
	up(&genRpcMsgQ_sem);
}

static void RPCLinkStatusIndCallback(unsigned char linkStatus)
{
	/* link Down */
	if (linkStatus == FALSE) {
		bRpcConnected = FALSE;
		/* clean */
		/*cleanData(); */
	} else {		/* link UP */

		/* pass indication to ErrorHandlere about successful com silent reset reqovery */
#if (defined OSA_WINCE)
		HANDLE hTimerEvent =
		    OpenEvent(EVENT_ALL_ACCESS, FALSE, L"SilResTimer");
		if (hTimerEvent) {
			SetEvent(hTimerEvent);
			CloseHandle(hTimerEvent);
		}
#endif

	}
}

#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
static void RPCWatermarkInd(ACIPCD_WatermarkTypeE watermarkType)
{
	ASSERT(FALSE);
}
#endif

static void GPCSendPendingReq(void)
{
	RPC_QueueMsgS Msg;
	int ret;

	while (eGpcState == eActive && !kfifo_is_empty(&gpcMsgQ)) {
		ret =
		    kfifo_out_locked(&gpcMsgQ, &Msg, sizeof(Msg),
				     &gpcMsgQ_lock);
		ASSERT(ret == sizeof(Msg));
		ACIPCDTxReq(Msg.ServiceId, Msg.pRpcMsg, Msg.Length);
	}
}

static void GPCMsgInd(void *pData, unsigned int Length)
{
	RPC_QueueMsgS Msg;
	int ret;

	Msg.ServiceId = ACIPCD_GPC;
	Msg.pRpcMsg = pData;
	Msg.Length = Length;

	ret = kfifo_in_locked(&genRpcMsgQ, &Msg, sizeof(Msg), &genRpcMsgQ_lock);
	ASSERT(ret == sizeof(Msg));
	up(&genRpcMsgQ_sem);
}

static void GPCLinkStatusIndCallback(unsigned char linkStatus)
{
	/* link Down */
	if (linkStatus == FALSE) {
		eGpcState = eNotActive;
		/* clean */
		/*cleanData(); */
	} else {		/* link UP */

		eGpcState = eActive;
		/* pass indication to ErrorHandlere about successful com silent reset reqovery */
#if (defined OSA_WINCE)
		HANDLE hTimerEvent =
		    OpenEvent(EVENT_ALL_ACCESS, FALSE, L"SilResTimer");
		if (hTimerEvent) {
			SetEvent(hTimerEvent);
			CloseHandle(hTimerEvent);
		}
#endif

		GPCSendPendingReq();
	}
}

#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
static void GPCWatermarkInd(ACIPCD_WatermarkTypeE watermarkType)
{
	if (watermarkType == ACIPCD_WATERMARK_HIGH)
		eGpcState = eHighWm;
	else {
		eGpcState = eActive;
		GPCSendPendingReq();
	}
}
#endif

static void GPCTxDoneCnf(void *p)
{
	OsaMemFree(p);
	GPCSendPendingReq();
}

static void GPCTxFailCnf(void *p)
{
	/*WARNING(0); */
	GPCTxDoneCnf(p);
}

void RegisterWithShmem(void)
{
	ACIPCD_CallBackFuncS CallBackFunc;
	ACIPCD_ConfigS Config;
	ACIPCD_ReturnCodeE rc;

	if (!bRpcConnected) {
		bRpcConnected = TRUE;

		memset(&CallBackFunc, 0, sizeof(CallBackFunc));
		CallBackFunc.RxIndCB = RPCMsgInd;
		CallBackFunc.LinkStatusIndCB = RPCLinkStatusIndCallback;
#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
		CallBackFunc.WatermarkIndCB = RPCWatermarkInd;
#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */

		memset(&Config, 0, sizeof(Config));
		Config.RpcTimout = 30000;	/*2000; */
		rc = ACIPCDRegister(ACIPCD_GEN_RPC, &CallBackFunc, &Config);	/*-+ CQ00181489 21-Sep-2011 +- */
		ASSERT(rc == ACIPCD_RC_OK);	/*-+ CQ00181489 21-Sep-2011 +- */
	}

	if (eGpcState == eClosed) {
		eGpcState = eNotActive;

		memset(&CallBackFunc, 0, sizeof(CallBackFunc));
		CallBackFunc.RxIndCB = GPCMsgInd;
		CallBackFunc.LinkStatusIndCB = GPCLinkStatusIndCallback;
#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
		CallBackFunc.WatermarkIndCB = GPCWatermarkInd;
#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
		CallBackFunc.TxDoneCnfCB = GPCTxDoneCnf;
		CallBackFunc.TxFailCnfCB = GPCTxFailCnf;

		memset(&Config, 0, sizeof(Config));
		Config.HighWm = GPC_HWM_SIZE;
		Config.LowWm = GPC_LWM_SIZE;
		rc = ACIPCDRegister(ACIPCD_GPC, &CallBackFunc, &Config);
		ASSERT(rc == ACIPCD_RC_OK);
	}
#if !defined (FLAVOR_COM)
	if (!RPC_ShmemRef)
		RPC_ShmemRef = OsaMemGetPoolRef(SHMEM_POOL, NULL, NULL);
#endif

}

void RPCPhase1Init(void)
{
	memset(&RpcFunctionsDb, 0, sizeof(RpcFunctionsDb));
#if defined (DIP_CHANNEL_ENABLE)
	DipChanPhase1Init();
#endif
}

void RPCPhase2Init(void)
{
	int alloc_size =
	    roundup_pow_of_two(RPC_MSG_QUEUE_SIZE * sizeof(RPC_QueueMsgS));
	int Status;

	if (!genRpcMsgQ_status) {	/* CQ00148212: don't re-create every silent reset */
		Status = kfifo_alloc(&genRpcMsgQ, alloc_size, GFP_KERNEL);
		ASSERT(!Status);
		spin_lock_init(&genRpcMsgQ_lock);
		sema_init(&genRpcMsgQ_sem, 0);
		genRpcMsgQ_status = 1;
	}

	if (!genRpcTask_status) {	/* CQ00148212: don't re-create every silent reset */
		kthread_run(GENRpcTask, NULL, "GENRpc");
		genRpcTask_status = 1;
	}

	if (!gpcMsgQ_status) {
		Status = kfifo_alloc(&gpcMsgQ, alloc_size, GFP_KERNEL);
		ASSERT(!Status);
		spin_lock_init(&gpcMsgQ_lock);
		gpcMsgQ_status = 1;
	}
	RegisterWithShmem();

	/*let the generic RPC users to register themselved */
	{
		extern void RPCUsersRegister(void);
		RPCUsersRegister();
	}
#if defined(AC_RPC_TEST_ENABLE)
	{
		extern void AC_GPC_TestRegister(void);
		AC_GPC_TestRegister();	/*YANM test to be deleted */
	}
#endif
#if defined (DIP_CHANNEL_ENABLE)
	DipChanPhase2Init();
#endif
}

RpcFuncType RPCFuncRegister(char *funcName, RpcFuncType funcPtr)
{
	RpcFuncType lastRpcfuncPtr;
	int existingFuncEntry;
	RPC_LOCK();

	if (strlen(funcName) >= MAX_FUNC_NAME_LENGTH) {
		RPC_UNLOCK();
		RPC_ERR_PRINT("RPC function name is longer than allowed\n");
		ASSERT(FALSE);

	}

	lastRpcfuncPtr = RPCFindFuncByName(funcName);
	if (lastRpcfuncPtr == NULL) {
		if (RpcFunctionsDb.NumEntries >= MAX_RPC_FUNC_ENTRIES) {
			RPC_UNLOCK();
			RPC_ERR_PRINT("RPC max entries reached\n");
			ASSERT(FALSE);
		}

		if (strlen(funcName) > MAX_FUNC_NAME_LENGTH - 1) {
			RPC_ERR_PRINT
			    ("Registered RPC function name is longer than allowed\n");
			ASSERT(FALSE);
		}
		/* New function, add entry */
		strncpy(RpcFunctionsDb.Func[RpcFunctionsDb.NumEntries].Name,
			funcName,
			sizeof(RpcFunctionsDb.Func[RpcFunctionsDb.NumEntries].
			       Name) - 1);
		RpcFunctionsDb.Func[RpcFunctionsDb.NumEntries].Name[sizeof(RpcFunctionsDb.Func[RpcFunctionsDb.NumEntries].Name) - 1] = '\0';	/*just in case... */
		RpcFunctionsDb.Func[RpcFunctionsDb.NumEntries].Ptr = funcPtr;
		RpcFunctionsDb.NumEntries++;
	} else {
		/*find existing function's entry */
		existingFuncEntry = RPCFindFuncIndexByName(funcName);
		if (existingFuncEntry > -1) {
			/*replace existing function entry with new one (could be NULL, meaning deregister function) */
			RpcFunctionsDb.Func[RpcFunctionsDb.NumEntries].Ptr =
			    funcPtr;
		}
	}
	RPC_UNLOCK();

	return lastRpcfuncPtr;
}

unsigned long RPCTxReq(char *funcName, unsigned short inDataLength,
		       void *inData, unsigned short outDataLength,
		       void *outData)
{
	RPC_QueueMsgS Msg;
	RPC_DataS *rpcData;
	unsigned long rpcDataLength, rc;
	ACIPCD_ReturnCodeE acipcd_rc;
	unsigned long funcNameAlignedLength, inDataAlignedLength,
	    outDataAlignedLength;
	int ret;

	if (strlen(funcName) >= MAX_FUNC_NAME_LENGTH) {
		RPC_ERR_PRINT("RPC function name is longer than allowed\n");
		ASSERT(FALSE);
	}

	funcNameAlignedLength = RPC_ALIGNED_LENGTH((strlen(funcName) + 1));
	inDataAlignedLength = RPC_ALIGNED_LENGTH(inDataLength);
	outDataAlignedLength = RPC_ALIGNED_LENGTH(outDataLength);

	rpcDataLength =
	    sizeof(RPC_DataS) + funcNameAlignedLength + inDataAlignedLength +
	    outDataAlignedLength;
#if !defined (FLAVOR_COM)
	if (!RPC_ShmemRef)
		RPC_ShmemRef = OsaMemGetPoolRef(SHMEM_POOL, NULL, NULL);
#endif
	rpcData = OsaMemAlloc(RPC_ShmemRef, rpcDataLength);
	ASSERT(rpcData != NULL);

	rpcData->rc = 0;
	strncpy((char *)rpcData->Data, funcName, rpcDataLength - sizeof(RPC_DataS));	/*-+ CQ00181489 21-Sep-2011 +- */
	rpcData->DataInIndex = funcNameAlignedLength;
	rpcData->DataInLen = inDataLength;
	memcpy(&rpcData->Data[rpcData->DataInIndex], inData, inDataLength);
	rpcData->DataOutIndex = funcNameAlignedLength + inDataAlignedLength;
	rpcData->DataOutLen = outDataLength;
	/*memcpy        (&rpcData->Data[rpcData->DataOutIndex] , outData , outDataLength); */

	if (outDataLength) {
		acipcd_rc = ACIPCDTxReq(ACIPCD_GEN_RPC, rpcData, rpcDataLength);

/*boaz.sommer.mrvl - prevent 'dead' RPC on silent reset - start */
		if (acipcd_rc != ACIPCD_RC_OK) {
			rc = -1;
		} else {
			rc = rpcData->rc;
		}
/*boaz.sommer.mrvl - prevent 'dead' RPC on silent reset - end */
		if ((int)rc >= 0) {
			memcpy(outData, &rpcData->Data[rpcData->DataOutIndex],
			       outDataLength);
		}

		OsaMemFree((void *)rpcData);
	} else {
		Msg.ServiceId = ACIPCD_GPC;
		Msg.pRpcMsg = rpcData;
		Msg.Length = rpcDataLength;

		ret =
		    kfifo_in_locked(&gpcMsgQ, &Msg, sizeof(Msg), &gpcMsgQ_lock);
		ASSERT(ret == sizeof(Msg));
		GPCSendPendingReq();
		rc = 0;
	}
	return rc;
}

#if defined(AC_RPC_TEST_ENABLE)
/************************  GPC Test commands ***********************************/
unsigned long AC_GPC_TestReceive(unsigned short InBufLen, void *pInBuf,
				 unsigned short OutBufLen, void *pOutBuf)
{
	pr_info("\n  AC_GPC_TestReceive() executed\n\n");
	return 0;		/*=OK */
}

unsigned long AC_RPC_TestReceive(unsigned short InBufLen, void *pInBuf,
				 unsigned short OutBufLen, void *pOutBuf)
{
	pr_info("\n  AC_RPC_TestReceive() executed\n\n");
	strncpy(pOutBuf, "RPC success", OutBufLen - 1);
	((char *)pOutBuf)[OutBufLen - 1] = '\0';	/*just in case... */
	return 0;		/*=OK */
}

void AC_GPC_TestRegister(void)
{
	RPC_FUNC_REGISTER(AC_RPC_TestReceive);	/*RPCFuncRegister() */
	RPC_FUNC_REGISTER(AC_GPC_TestReceive);	/*RPCFuncRegister() */
	pr_info("\n AC  RPC/GPC test Register done.\n\n");
}

void AC_GPC_TestSend(void)
{
	char outBuf[80];
	char *strBuf = "AC_GPC_TestSend";
	if (RPC_CALL("AC_RPC_TestReceive", (strlen(strBuf) + 1), strBuf, 80, outBuf) == 0) {	/*RPCTxReq() */
		pr_info("AC_RPC_TestReceive result %s", outBuf);
	}
	if (RPC_SEND_AND_FORGET("AC_GPC_TestReceive", (strlen(strBuf) + 1), strBuf) == 0) {	/*RPCTxReq() */
		pr_info("AC_GPC_TestSend to OtherSide");
	}
}
#endif /*PLATFORM_ONLY || APPS-RTOS */
