/*------------------------------------------------------------
(C) Copyright [2006-2008] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/*=========================================================================

  File Name:    ci_client.c

  Description:  This is the implementation of CI Interface functions

  Revision:     Phillip Cho, 0.1

INTEL CONFIDENTIAL
Copyright 2006 Intel Corporation All Rights Reserved.
The source code contained or described herein and all documents related to the source code ("Material") are owned
by Intel Corporation or its suppliers or licensors. Title to the Material remains with Intel Corporation or
its suppliers and licensors. The Material contains trade secrets and proprietary and confidential information of
Intel or its suppliers and licensors. The Material is protected by worldwide copyright and trade secret laws and
treaty provisions. No part of the Material may be used, copied, reproduced, modified, published, uploaded, posted,
transmitted, distributed, or disclosed in any way without Intel's prior express written permission.

No license under any patent, copyright, trade secret or other intellectual property right is granted to or
conferred upon you by disclosure or delivery of the Materials, either expressly, by implication, inducement,
estoppel or otherwise. Any license under such intellectual property rights must be express and approved by
Intel in writing.

Unless otherwise agreed by Intel in writing, you may not remove or alter this notice or any other notice embedded
in Materials by Intel or Intel's suppliers or licensors in any way.
  ======================================================================== */
#if defined(ENV_LINUX)
#include <linux/string.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include "ci_api_types.h"
#include "ci_api.h"
#include "ci_stub.h"
#include "ci_mem.h"
#include "ci_trace.h"
#include "ci_sim.h"
#include "ci_msg.h"
#include "ci_pb.h"

#include "ci_dat.h"
#include "ci_opt_dat_types.h"

#include "ci_trace.h"
#include "ci_err.h"

#include "acipc_data.h"
#include "diag.h"

#include "aci_debug.h"

#if defined(OSA_WINCE)
#include "commemmapping.h"
#endif

#if defined(ENV_LINUX)
#include "com_mem_mapping_user.h"
#include "common_datastub.h"
#include <stdio.h>

#endif
#include "ci_syswrap.h"

/*#include "dbgLog.h"
DBGLOG_EXTERN_VAR(shmem);
#define SHMEM_DEBUG_LOG2(a,b,c) DBGLOG_ADD(shmem,a,b,c)
*/

#if !defined(DATA_STUB_IN_KERNEL)
#error "ERROR: For NON User-Kernel systems (e.g. WinCE) need to implement the REQ_CNF and IND_RSP split, this is currently not supported on those systems"
#endif /* !DATA_STUB_IN_KERNEL */

#ifndef UNUSEDPARAM
#define UNUSEDPARAM(param)

#endif

#if defined(DATA_STUB_IN_KERNEL)	/* taken out for Linux build - ci-data run in kernel */
#include <sys/ioctl.h>
#include <fcntl.h>
int cidata_fd;
#endif /* DATA_STUB_IN_KERNEL */

/*CI Control channel watermark level - notified to COMM */
#define CI_CTRL_HIGH_WATERMARK      50
#define CI_CTRL_LOW_WATERMARK       10

/*CI DATA channel watermark level - for REQ & CNF channel - notified to COMM - for CNF */
#define CI_DATA_HIGH_WATERMARK_REQ_CNF      40
#define CI_DATA_LOW_WATERMARK_REQ_CNF       20

/*CI DATA channel watermark level - for IND & RSP channel - notified to COMM - for IND */
#define CI_DATA_HIGH_WATERMARK_IND_RSP      40
#define CI_DATA_LOW_WATERMARK_IND_RSP       20

/*CI DATA CONTROL channel watermark level - this is special channel for misc controlling data channel */
#define CI_DATA_CONTROL_HIGH_WATERMARK      15
#define CI_DATA_CONTROL_LOW_WATERMARK       5

#define SHMEM_POOL              "ShmemPool"

#if defined(ENV_LINUX)
/*YG: currently required by telatci.c under linux - need to check */
CiServiceHandle ciATciSvgHandle[CI_SG_NUMIDS + 1];

extern CrsmCciAction *head;
void CiSetSvgGroupHandle(unsigned char svgId, unsigned int shcnfhandler)
{
	ciATciSvgHandle[svgId] = shcnfhandler;
}				/* it needs to notify Borqs. */
#endif

unsigned char g_bCCILinkDown = FALSE;
/*-------------------------------------------------------------------------*
 * External Global Variables
 *-------------------------------------------------------------------------*/
unsigned char gDlLinkStatusFlag = FALSE;
unsigned char gIsDataChannelReady = FALSE;
CiDatPrimSendDataOptCnf *gCiDatConfParam;
volatile unsigned char gCIDataAboveWatermark = FALSE;
volatile unsigned char gCICtrlAboveWatermark = FALSE;
unsigned char gAvailRxBuffers = 1;	/* for RIL */

static OsaRefT gCi_CriticalSectionRef;

/* returns kernel data callbacks ptr */
#if defined(DATA_STUB_IN_KERNEL)
extern void GetCallbacksKernelFn(ACIPCD_CallBackFuncS *pCallBackFunc, unsigned int callbackType);
#endif

/*-------------------------------------------------------------------------*
 * Internal Global Variables
 *-------------------------------------------------------------------------*/
/* Free memory call-back that is used by the CI client Stub to free memory allocated
by the Ril for the requested shell, request and respond operation */
static CiShFreeReqMem gClientCiShFreeReqMemFuncPtr;
static CiShOpaqueHandle gOpShFreeHandle = 1;	/* No meaning value */
static CiServiceHandle gCiServiceHandle[CI_SG_ID_NEXTAVAIL];
static CiSgOpaqueHandle gCiSgOpaqueReqHandle[CI_SG_ID_NEXTAVAIL];
static CiSgOpaqueHandle gCiSgOpaqueRspHandle[CI_SG_ID_NEXTAVAIL];
static CiSgFreeReqMem gCiSgFreeReqMemFuncTable[CI_SG_ID_NEXTAVAIL];
static CiSgFreeReqMem gCiSgFreeRspMemFuncTable[CI_SG_ID_NEXTAVAIL];

/* CI ShConfirm, CiConfirm, and CiIndicate call-back supply by Ril and that is going to be called by CI client Stub */
static CiShConfirm gCiShConfirmFuncPtr;
static CiConfirm gCiDefConfirmFuncPtr;
static CiIndicate gCiDefIndicateFuncPtr;
static CiConfirm gCiConfirmFuncPtr[CI_SG_ID_NEXTAVAIL];
static CiIndicate gCiIndicateFuncPtr[CI_SG_ID_NEXTAVAIL];

CiSgOpaqueHandle gOpSgCnfHandle[CI_SG_ID_NEXTAVAIL];
CiSgOpaqueHandle gOpSgIndHandle[CI_SG_ID_NEXTAVAIL];

static CiShOpaqueHandle gCiShOpaqueHandle;
#if defined(OSA_WINCE)
static OsaRefT gACIPCD_ShmemRef;
#endif

CiClientStubStatusIndPtr g_pCiStatusCallbackFn;

/* callback for Free memory operation. called by CI client user (e.g. RIL) */
static void clientCiShFreeCnfMem(CiShOpaqueHandle opShFreeHandle, CiShOper oper, void *cnfParas);
static void clientCiSgFreeCnfMem(CiSgOpaqueHandle opSgFreeHandle,
				 CiServiceGroupID id, CiPrimitiveID primId, void *cnfParas);
static void clientCiSgFreeIndMem(CiSgOpaqueHandle opSgFreeHandle,
				 CiServiceGroupID id, CiPrimitiveID primId, void *paras);
static void clientRegisterCiSgFreeFunction(CiServiceGroupID id,
					   CiSgOpaqueHandle reqHandle,
					   CiSgOpaqueHandle rspHandle,
					   CiSgFreeReqMem ciSgFreeReqMem, CiSgFreeRspMem ciSgFreeRspMem);

CiServiceGroupID findCiSgId(CiServiceHandle handle);

/* SHMEM callbacks */
static unsigned char ShmemClientCIStubInit();

static void stubClientFreeCiApiPara(CiApiPara *pCiApiPara);

static unsigned char SendCtrlPacket(void *pData, int iSize);
static void OnFreeCtrlPacket(void *pData);

static void OnFreeDataPacket(void *pData);

/* primitive/control packets */
static void process_rxControlInd(unsigned char *inData, unsigned int dataSize);
static void *SHMEMAlloc(int iSize);
static void SHMEMFree(void *pData);

/************************************************************************************
 * callback functions
 **********************************************************************************/

#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
static void WaterMarkCiIndication(_CiDatPduType pduType, ACIPCD_WatermarkTypeE watermarkType)
#else /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
static void WaterMarkCiIndication(_CiDatPduType pduType, unsigned char bSetHighWM)
#endif				/*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
{

	OsaRefT CriticalSectionRef;

	CriticalSectionRef = OsaCriticalSectionEnter(gCi_CriticalSectionRef, NULL);

#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
	ACIDBG_TRACE_2P(1, 3, "WaterMarkCiIndication entered: pduType=0x%x, watermarkType=%d\n", pduType,
			watermarkType);
#else /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
	ACIDBG_TRACE_2P(1, 1, "WaterMarkCiIndication entered: pduType=0x%x, bSetHighWM=%d\n", pduType, bSetHighWM);
#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */

	if (pduType == CI_DATA_PDU) {
#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
		gCIDataAboveWatermark = (watermarkType == ACIPCD_WATERMARK_HIGH) ? TRUE : FALSE;
#else /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
		static unsigned char bIgnoreNextHW = FALSE;

		if (bIgnoreNextHW) {
			bIgnoreNextHW = FALSE;
			OsaCriticalSectionExit(CriticalSectionRef, NULL);

			ACIDBG_TRACE(1, 1, "WaterMarkCiIndication: bIgnoreNextHW = TRUE already - exit function!!!!\n");
			return;
		}
		/* YG: check if lowDataWmInd received before handled the HighWM in this function.
		 * If yes then sleep for a while and check again
		 * YG: This is a workaround until Low/High watermarks will be fixed */
		if ((bSetHighWM == FALSE) && (gCIDataAboveWatermark == FALSE)) {
			bIgnoreNextHW = TRUE;
			OsaCriticalSectionExit(CriticalSectionRef, NULL);

			ACIDBG_TRACE(1, 1, "WaterMarkCiIndication: LOW received before HIGH !!!!\n");
			return;
		}

		gCIDataAboveWatermark = bSetHighWM;
#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */

		/* in case of LOW Watermark - Send confirm data for upper layer */
#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
		if (watermarkType == ACIPCD_WATERMARK_HIGH)
#else /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
		if (bSetHighWM == TRUE)
#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
		{
			gAvailRxBuffers = 0;
		} else {
			gAvailRxBuffers = 1;

			gCiConfirmFuncPtr[CI_SG_ID_DAT] (0, CI_SG_ID_DAT, CI_DAT_PRIM_SEND_DATA_OPT_CNF, 0, NULL);
		}
	} else {		/*CI_CTRL_PDU */
		ACIDBG_TRACE(1, 2, "\nCI WARNING WARNING - Control Watermark occured !!!\n");
		/*DBGLOG_DELAYED_STOP_DISABLE(shmem, 2000); */

#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
		ACIDBG_TRACE_1P(1, 2, "CI_CONTROL Watermark received, watermarkType = %d !!!", watermarkType);
		gCICtrlAboveWatermark = (watermarkType == ACIPCD_WATERMARK_HIGH) ? TRUE : FALSE;
#else /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
		ACIDBG_TRACE_1P(1, 1, "CI_CONTROL Watermark received, bSetHighWM = %d !!!", bSetHighWM);
		gCICtrlAboveWatermark = bSetHighWM;
#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
	}
	OsaCriticalSectionExit(CriticalSectionRef, NULL);
}

static void CI_SHM_txDoneCtrlInd(void *ptr)
{
	if (ptr != NULL) {
		SHMEMFree(ptr);
	}
}

static void CI_SHM_CtrlRxInd(void *ptr, unsigned int len)
{
	process_rxControlInd(ptr, len);
}

#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)

static void CI_SHM_CtrlWatermarkInd(ACIPCD_WatermarkTypeE watermarkType)
{
	ACIDBG_TRACE_1P(1, 2, "CI_SHM_CtrlWatermarkInd reached, type = %x", watermarkType);

	WaterMarkCiIndication(CI_CTRL_PDU, watermarkType);
}

#else /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */

static void CI_SHM_lowCtrlWmInd(void)
{
	ACIDBG_TRACE(1, 2, "CI_SHM_lowCtrlWmInd reached");

	WaterMarkCiIndication(CI_CTRL_PDU, FALSE);
}

#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */

static void CI_SHM_LinkStatusDataInd(unsigned char LinkStatus)
{
	gCIDataAboveWatermark = FALSE;
	/* link up */
	if (LinkStatus) {
		if (g_pCiStatusCallbackFn != NULL) {
			if (g_bCCILinkDown == TRUE) {
				g_bCCILinkDown = FALSE;
				(g_pCiStatusCallbackFn) (3);	/*CISW_INTERLINK_UP */
			} else
				(g_pCiStatusCallbackFn) (0);
		}

		gIsDataChannelReady = TRUE;
		gAvailRxBuffers = 1;	/* for RIL */
	} else {
		/*link down */
		if (g_pCiStatusCallbackFn != NULL)
			(g_pCiStatusCallbackFn) (2);	/*CISW_INTERLINK_DOWN */

		gIsDataChannelReady = FALSE;
		gAvailRxBuffers = 0;	/* for RIL */
		g_bCCILinkDown = TRUE;
	}
}

static void CI_SHM_LinkStatusControlInd(unsigned char LinkStatus)
{
	gCICtrlAboveWatermark = FALSE;
	/* link up */
	if (LinkStatus) {
		gDlLinkStatusFlag = LINK_CONNECT;
		clientSendCiLinkUpInd();
	} else {
		/*link down */
		gDlLinkStatusFlag = LINK_DIS_CONNECT;
		gIsDataChannelReady = FALSE;

		if (gCiDatConfParam)
			ASSERT(FALSE);	/* YG: need to free instead of assert */

		gCiDatConfParam = NULL;
		gCIDataAboveWatermark = FALSE;
		gCICtrlAboveWatermark = FALSE;
		gAvailRxBuffers = 0;	/* for RIL */

		clientSendCiLinkDownInd();
	}

	CI_SHM_LinkStatusDataInd(LinkStatus);

}

static void CI_SHM_txFailCtrlInd(void *ptr)
{
	OsaMemFree(ptr);
}

extern void SendPendingBuffersPtrToKernel(unsigned int *pendingBuffersAddress);

static void process_rxControlInd(unsigned char *inData, unsigned int dataSize)
{
	CiApiCallbackPara *pCiApiCallbackPara = (CiApiCallbackPara *) inData;

	UNUSEDPARAM(dataSize);

	/*-------------- invoke callback function based on the callback ID -------------*/
	switch (pCiApiCallbackPara->cbId) {
	case CiShConfirmCbId:
		{
			CiShConfirmArgs *ciShConfirmArgs =
			    (CiShConfirmArgs *) ((unsigned char *)pCiApiCallbackPara + sizeof(CiApiCallbackPara));
			/* retrieves Oper ID to dispatch to the proper function */
			ciShConfirmArgs->cnfParas = ((unsigned char *)ciShConfirmArgs + sizeof(CiShConfirmArgs));
			/* retrieves All args except callback id */
			clientCiShConfirmCallback(ciShConfirmArgs);
		}
		break;

	case CiConfirmCbId:
		{
			CiConfirmArgs *ciConfirmArgs =
			    (CiConfirmArgs *) ((unsigned char *)pCiApiCallbackPara + sizeof(CiApiCallbackPara));
			/* retrieves  Service Group Id */
			ciConfirmArgs->paras = ((unsigned char *)ciConfirmArgs + sizeof(CiConfirmArgs));
			/* retrieves  All confirm Args except Callback ID */
			clientCiConfirmCallback(ciConfirmArgs);
		}
		break;

	case CiConfirmDefCbId:
		{
			CiConfirmArgs *ciConfirmArgs =
			    (CiConfirmArgs *) ((unsigned char *)pCiApiCallbackPara + sizeof(CiApiCallbackPara));
			/* retrieves  Service Group Id */
			ciConfirmArgs->paras = ((unsigned char *)ciConfirmArgs + sizeof(CiConfirmArgs));
			/* retrieves  All confirm Args except Callback ID */
			clientCiDefConfirmCallback(ciConfirmArgs);
		}
		break;

	case CiIndicateDefCbId:
		{
			CiIndicateArgs *ciIndicateArgs =
			    (CiIndicateArgs *) ((unsigned char *)pCiApiCallbackPara + sizeof(CiApiCallbackPara));
			/* retrieves  Service Group Id */
			ciIndicateArgs->paras = ((unsigned char *)ciIndicateArgs + sizeof(CiIndicateArgs));
			/* retrieves  All confirm Args except Callback ID */
			clientCiDefIndicateCallback(ciIndicateArgs);
		}
		break;

	case CiIndicateCbId:
		{
			CiIndicateArgs *ciIndicateArgs =
			    (CiIndicateArgs *) ((unsigned char *)pCiApiCallbackPara + sizeof(CiApiCallbackPara));
			ciIndicateArgs->paras = ((unsigned char *)ciIndicateArgs + sizeof(CiIndicateArgs));
			/* retrieves  All confirm Args except Callback ID */
			clientCiIndicateCallback(ciIndicateArgs);
		}
		break;

	case CiTotalPendingBuffersId:
		{
			/*ACIPCD_CallBackFuncS *pCallBackFunc; */

			/*ASSERT(FALSE); */
			printf("YG: Total Pending Data COMM Ptr %x\n", (int)pCiApiCallbackPara->argPtr);
			/*pCallBackFunc->RxIndCB = (void *)pCiApiCallbackPara->argPtr; */
			/*GetCallbacksKernelFn(pCallBackFunc, 10); */
			SendPendingBuffersPtrToKernel((unsigned int *)pCiApiCallbackPara->argPtr);

			/*send RxDoneRsp to COMM (fix D2 power issue) */
			OnFreeCtrlPacket(inData);
		}
		break;

	default:
		break;

	}
}

/* static stub call-back that is going to be called by the CI client task
when the client task receives callback PDU with CI Callback Id 1 to  5 */
void clientCiShConfirmCallback(CiShConfirmArgs *pArg)
{
	CiShOperRegisterShCnf *pCiShOperRegisterShCnf;
	CiShOperDeregisterShCnf *pCiShOperDeregisterShCnf;
	CiShOperRegisterSgCnf *pCiShOperRegisterSgCnf;
	CiShOperRegisterDefaultSgCnf *pCiShOperRegisterDefaultSgCnf;
	CiShOperDeregisterSgCnf *pCiShOperDeregisterSgCnf;

	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO, "clientCiShConfirmCallback()");

	/* Replace CI Client stub callback for free conf para */
	if (pArg->oper == CI_SH_OPER_REGISTERSH) {
		pCiShOperRegisterShCnf = (CiShOperRegisterShCnf *) pArg->cnfParas;
		pCiShOperRegisterShCnf->opShFreeHandle = gOpShFreeHandle;
		pCiShOperRegisterShCnf->ciShFreeCnfMem = clientCiShFreeCnfMem;
	}

	if (pArg->oper == CI_SH_OPER_REGISTERDEFSG) {
		pCiShOperRegisterDefaultSgCnf = (CiShOperRegisterDefaultSgCnf *) pArg->cnfParas;
		pCiShOperRegisterDefaultSgCnf->opSgFreeDefCnfHandle = gOpShFreeHandle;
		pCiShOperRegisterDefaultSgCnf->opSgFreeDefIndHandle = gOpShFreeHandle;
		pCiShOperRegisterDefaultSgCnf->ciSgFreeDefCnfMem = clientCiSgFreeCnfMem;
		pCiShOperRegisterDefaultSgCnf->ciSgFreeDefIndMem = clientCiSgFreeIndMem;
	}

	if (pArg->oper == CI_SH_OPER_REGISTERSG) {
		pCiShOperRegisterSgCnf = (CiShOperRegisterSgCnf *) pArg->cnfParas;
		gCiServiceHandle[pCiShOperRegisterSgCnf->id] = pCiShOperRegisterSgCnf->handle;
		pCiShOperRegisterSgCnf->opSgFreeCnfHandle = gOpShFreeHandle;
		pCiShOperRegisterSgCnf->opSgFreeIndHandle = gOpShFreeHandle;
		pCiShOperRegisterSgCnf->ciSgFreeCnfMem = clientCiSgFreeCnfMem;
		pCiShOperRegisterSgCnf->ciSgFreeIndMem = clientCiSgFreeIndMem;
	}

	/* call real callback function that register by RIL */
	/*SCR 2000769 */
	if (pArg->oper == CI_SH_OPER_DEREGISTERSG) {
		pCiShOperDeregisterSgCnf = (CiShOperDeregisterSgCnf *) pArg->cnfParas;
		gCiIndicateFuncPtr[pCiShOperDeregisterSgCnf->id] = NULL;
		gCiConfirmFuncPtr[pCiShOperDeregisterSgCnf->id] = NULL;
		gCiServiceHandle[pCiShOperDeregisterSgCnf->id] = 0;
	}

	gCiShConfirmFuncPtr(gCiShOpaqueHandle, pArg->oper, pArg->cnfParas, pArg->opHandle);

	/* In case of CI_SH_OPER_DEREGISTERSH we need to deregister confirmationcallback */
	if (pArg->oper == CI_SH_OPER_DEREGISTERSH) {
		pCiShOperDeregisterShCnf = (CiShOperDeregisterShCnf *) pArg->cnfParas;

		if (pCiShOperDeregisterShCnf->rc == CIRC_SUCCESS) {
			gCiShConfirmFuncPtr = NULL;
		}
	}
}

/*--------------------------------------------------------------------
 *  Description: Request to register confirmation call-back for shell operations
 *	Input Paras:
 *		ciShConfirm: confirmation call back for shell operation requests
 *		opShHandle: opaque handle identifies the user who call the ciShRegisterReq
 *	Returns:
 *		CIRC_FAIL, if ciShConfirm is NULL; CIRC_INTERLINK_FAIL, if the link is broken, CIRC_SUCCESS, if otherwise.
 *-------------------------------------------------------------------- */
CiReturnCode ciShRegisterReq_1(CiShConfirm ciShConfirm,
			       CiShOpaqueHandle opShHandle,
			       CiShFreeReqMem ciShFreeReqMem, CiShOpaqueHandle opShFreeHandle)
{
	CiApiPara *pCiApiPara;
	CiShRegisterReqArgs *pArgs;

	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO, "ciShRegisterReq_1()");

	/* Check Link status flag first */
	if (gDlLinkStatusFlag == LINK_DIS_CONNECT) {
		CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_ERROR, "CIRC_INTERLINK_FAIL");
		return CIRC_INTERLINK_FAIL;
	}

	if (gCICtrlAboveWatermark == TRUE) {
		return CIRC_FAIL;
	}

	pCiApiPara = (CiApiPara *) SHMEMAlloc(sizeof(CiApiPara) + sizeof(CiShRegisterReqArgs));
	ASSERT(pCiApiPara != NULL);

	/* mem for Client task message parameters */
	pArgs = (CiShRegisterReqArgs *) ((unsigned char *)pCiApiPara + sizeof(CiApiPara));

	/* register ciShConfirm and Mem free callback  Fn */
	gCiShConfirmFuncPtr = ciShConfirm;
	gCiShOpaqueHandle = opShHandle;

	/* If func prt is Null, then client has responsibe to freeing meme */
	if (ciShFreeReqMem != NULL) {
		gClientCiShFreeReqMemFuncPtr = ciShFreeReqMem;
		gOpShFreeHandle = opShFreeHandle;
	} else {
		CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO, " Client uses Static Mem for paras");
	}

	/* Assemble CI API Message */
	pArgs->ciShConfirm = ciShConfirm;
	pArgs->opShHandle = opShHandle;
	pArgs->ciShFreeReqMem = ciShFreeReqMem;
	pArgs->ciShOpaqueHandle = opShFreeHandle;

	pCiApiPara->prodId = CiShRegisterReqProcId;
	pCiApiPara->argPtr = (unsigned char *)pArgs;
	pCiApiPara->argDataSize = sizeof(CiShRegisterReqArgs);

	/* send a message. */

	SendCtrlPacket(pCiApiPara, sizeof(CiApiPara) + sizeof(CiShRegisterReqArgs));
	stubClientFreeCiApiPara(pCiApiPara);

	return CIRC_SUCCESS;
}

/*--------------------------------------------------------------------
 *  Description: Request to de-register confirmation call-back function for shell operations
 *	Returns:
 *		CIRC_FAIL,if ciShConfirm is NULL;CIRC_INTERLINK_FAIL, if the link is broken, CIRC_SUCCESS, if otherwise.
 *-------------------------------------------------------------------- */
CiReturnCode ciShDeregisterReq_1(CiShHandle handle, CiShOpaqueHandle opShHandle)
{

	CiApiPara *pCiApiPara;
	CiShDeregisterReqArgs *pArgs;

	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO, "ciShDeregisterReq()");

	/* Check Link status flag first */
	if (gDlLinkStatusFlag == LINK_DIS_CONNECT) {
		CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_ERROR, "CIRC_INTERLINK_FAIL");
		return CIRC_INTERLINK_FAIL;
	}

	if (gCICtrlAboveWatermark == TRUE) {
		return CIRC_FAIL;
	}

	pCiApiPara = (CiApiPara *) SHMEMAlloc(sizeof(CiApiPara) + sizeof(CiShDeregisterReqArgs));
	ASSERT(pCiApiPara != NULL);

	pArgs = (CiShDeregisterReqArgs *) ((unsigned char *)pCiApiPara + sizeof(CiApiPara));

	/* Put Message to the stub task Queue */
	pArgs->handle = handle;
	pArgs->opShHandle = opShHandle;

	pCiApiPara->prodId = CiShDeregisterReqProcId;
	pCiApiPara->argPtr = (unsigned char *)pArgs;
	pCiApiPara->argDataSize = sizeof(CiShDeregisterReqArgs);

	/* send a message. */
	SendCtrlPacket(pCiApiPara, sizeof(CiApiPara) + sizeof(CiShDeregisterReqArgs));
	stubClientFreeCiApiPara(pCiApiPara);

	return CIRC_SUCCESS;
}

/*--------------------------------------------------------------------
 *  Description:  Send shell operation request from application subsystem to cellular subsystem
 *	Input Parameters:
 *	Output Parameter: None
 * 	Returns:
 *		CIRC_FAIL, if ciShConfirm is NULL;CIRC_INTERLINK_FAIL, if the link is broken, CIRC_SUCCESS, if otherwise.
 *-------------------------------------------------------------------- */
CiReturnCode ciShRequest_1(CiShHandle handle, CiShOper oper, void *reqParas, CiShRequestHandle opHandle)
{
	CiApiPara *pCiApiPara;
	CiShRequestArgs *pArgs;
	unsigned int primDataSize;
	CiShOperRegisterDefaultSgReq *pCiShOperRegisterDefaultSgReq;
	CiShOperRegisterSgReq *pCiShOperRegisterSgReq;

	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO, "ciShRequest_1()");

	/* Check Link status flag first */
	if (gDlLinkStatusFlag == LINK_DIS_CONNECT) {
		CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_ERROR, "CIRC_INTERLINK_FAIL");
		return CIRC_INTERLINK_FAIL;
	}

	if (gCICtrlAboveWatermark == TRUE) {
		return CIRC_FAIL;
	}

	primDataSize = cimem_GetCiShReqDataSize(oper);

	pCiApiPara = (CiApiPara *) SHMEMAlloc(sizeof(CiApiPara) + sizeof(CiShRequestArgs) + primDataSize);
	ASSERT(pCiApiPara != NULL);

	pArgs = (CiShRequestArgs *) ((unsigned char *)pCiApiPara + sizeof(CiApiPara));
	memcpy((void *)((unsigned char *)pArgs + sizeof(CiShRequestArgs)), reqParas, primDataSize);

	pArgs->handle = handle;
	pArgs->oper = oper;
	pArgs->opHandle = opHandle;
	pArgs->reqParas = reqParas;	/* must be passed reqParas and not a reqParasToSend for the memory deallocation !!! */

	/* register callback Fn if oper is SG registeration */
    /*=========== DEFAULT for all service groups =============*/
	if (oper == CI_SH_OPER_REGISTERDEFSG) {
		pCiShOperRegisterDefaultSgReq = (CiShOperRegisterDefaultSgReq *) reqParas;
		gCiDefConfirmFuncPtr = pCiShOperRegisterDefaultSgReq->ciCnfDef;
		gCiDefIndicateFuncPtr = pCiShOperRegisterDefaultSgReq->ciIndDef;
	}

	if (oper == CI_SH_OPER_DEREGISTERDEFSG) {
		gCiDefConfirmFuncPtr = NULL;	/* de-register default confirmation and indication callback */
		gCiDefIndicateFuncPtr = NULL;
	}

    /*=========== PER service group =============*/
	if (oper == CI_SH_OPER_REGISTERSG) {
		pCiShOperRegisterSgReq = (CiShOperRegisterSgReq *) reqParas;
		gCiIndicateFuncPtr[pCiShOperRegisterSgReq->id] = pCiShOperRegisterSgReq->ciIndicate;
		gCiConfirmFuncPtr[pCiShOperRegisterSgReq->id] = pCiShOperRegisterSgReq->ciConfirm;
		gOpSgCnfHandle[pCiShOperRegisterSgReq->id] = pCiShOperRegisterSgReq->opSgCnfHandle;
		gOpSgIndHandle[pCiShOperRegisterSgReq->id] = pCiShOperRegisterSgReq->opSgIndHandle;

		clientRegisterCiSgFreeFunction(pCiShOperRegisterSgReq->id,
					       pCiShOperRegisterSgReq->opSgFreeReqHandle,
					       pCiShOperRegisterSgReq->opSgFreeRspHandle,
					       pCiShOperRegisterSgReq->ciSgFreeReqMem,
					       pCiShOperRegisterSgReq->ciSgFreeRspMem);
	}

	pCiApiPara->prodId = CiShRequestProcId;
	pCiApiPara->argPtr = (unsigned char *)pArgs;
	pCiApiPara->argDataSize = sizeof(CiShRequestArgs) + primDataSize;

	/* send a message to Stub task. */

	SendCtrlPacket(pCiApiPara, sizeof(CiApiPara) + sizeof(CiShRequestArgs) + primDataSize);
	stubClientFreeCiApiPara(pCiApiPara);

	return CIRC_SUCCESS;
}

/*--------------------------------------------------------------------
 *  Description: Send request service primitive from application subsystem into cellular subsystem
 *	Input Parameters:
 *  Output Parameter: None
 *	Returns:
 *		CIRC_FAIL, if ciShConfirm is NULL;CIRC_INTERLINK_FAIL, if the link is broken, CIRC_SUCCESS, if otherwise.
 *
 * Note: CI_DAT request service primitives are processed by ciRequest_Dat
 *-------------------------------------------------------------------- */
CiReturnCode ciRequest_1(CiServiceHandle handle, CiPrimitiveID primId, CiRequestHandle reqHandle, void *paras)
{
	CiApiPara *pCiApiPara;
	CiRequestArgs *pArgs;
	unsigned int primDataSize = 0;

/*BS: fix for mtsd sig11 - start */
	CiRequestArgs localArgs;
	CiApiPara localCiApiParaSave;
/*BS: fix for mtsd sig11 - end */

	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO, "ciRequest_1()");

	/* Check Link status flag first */
	if (gDlLinkStatusFlag == LINK_DIS_CONNECT) {
		CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_ERROR, "CIRC_INTERLINK_FAIL");
		return CIRC_INTERLINK_FAIL;

	}
#if !defined(DATA_STUB_IN_KERNEL)
	/* Call the CI_DAT data plane primitive process procedure */
	if (gCiServiceHandle[CI_SG_ID_DAT] == handle && CI_DAT_PRIM_SEND_DATA_OPT_REQ == primId) {
		/*input buffer */
		CiDatPrimSendDataOptReq *reqParam = (CiDatPrimSendDataOptReq *) paras;

		CiDatPduInfo *p_ciPdu;
		CiStubInfo *p_header;
		CiConfirmArgs cnf_args;
		CiRequestArgs ciReqArgs;
		void *pShmem;
		int iPacketSize;

		/* Check if data channel is ready */
		if (gIsDataChannelReady == FALSE) {
			CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, MSL_TRACE_ERROR, "CI DATA CHANNEL NOT READY");
			ACIDBG_TRACE(1, 1, "ciRequest_1: gIsDataChannelReady is FALSE");
			return CIRC_FAIL;
		}

		if (gCIDataAboveWatermark == TRUE) {
			ACIDBG_TRACE(1, 2,
				     "ciRequest_1: DATA in HighWatermark state - gCIDataAboveWatermark is TRUE\n");
			return CIRC_FAIL;
		}

		if ((reqParam->sendPdu).len > CI_STUB_DAT_BUFFER_SIZE - sizeof(CiDatPduInfo)) {
			ACIDBG_TRACE_1P(1, 1, "ciRequest_1: Buffer too long: reqParam->sendPdu).len = %d",
					(reqParam->sendPdu).len);
			/*CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, MSL_TRACE_ERROR,"CI_DAT_PRIM_SEND_DATA_OPT_REQ: DATA size bigger than buffer size"); */
			return CIRC_FAIL;
		}

		iPacketSize = sizeof(CiDatPduInfo) + (reqParam->sendPdu).len;
		pShmem = SHMEMAlloc(iPacketSize);
		if (pShmem == NULL) {
			ACIDBG_TRACE(1, 1, "CI SHM: ERROR - SHMEMAlloc IS NULL !!!");
			ASSERT(pShmem != NULL);
		}

		p_ciPdu = (CiDatPduInfo *) pShmem;
		p_header = (CiStubInfo *) &p_ciPdu->aciHeader;

		p_header->info.type = CI_DATA_PDU;
		p_header->info.param1 = CI_DAT_INTERNAL_BUFFER;
		p_header->info.param2 = 0;
		p_header->info.param3 = 0;
		p_header->gHandle.svcHandle = handle;
		p_header->cHandle.reqHandle = reqHandle;

		(p_ciPdu->ciHeader).connId = (reqParam->connInfo).id;
		(p_ciPdu->ciHeader).connType = (reqParam->connInfo).type;
		(p_ciPdu->ciHeader).datType = (reqParam->sendPdu).type;
		(p_ciPdu->ciHeader).isLast = (reqParam->sendPdu).isLast;
		(p_ciPdu->ciHeader).seqNo = (reqParam->sendPdu).seqNo;
		(p_ciPdu->ciHeader).datLen = (reqParam->sendPdu).len;
		(p_ciPdu->ciHeader).pPayload = NULL;

		memcpy((unsigned char *)pShmem + sizeof(CiDatPduInfo), (reqParam->sendPdu).data,
		       (p_ciPdu->ciHeader).datLen);

		gCiDatConfParam = (CiDatPrimSendDataOptCnf *) CI_MEM_ALLOC(sizeof(CiDatPrimSendDataOptCnf));
		ASSERT(gCiDatConfParam != NULL);
		gCiDatConfParam->connInfo = reqParam->connInfo;
		gCiDatConfParam->rc = CIRC_DAT_SUCCESS;

		/* Call DL API to send data */
		cnf_args.id = CI_SG_ID_DAT;
		cnf_args.primId = CI_DAT_PRIM_SEND_DATA_OPT_CNF;
		cnf_args.paras = (void *)gCiDatConfParam;
		cnf_args.reqHandle = reqHandle;
		cnf_args.opSgCnfHandle = handle;
		gCiDatConfParam = NULL;

		SendDataPacket(pShmem, iPacketSize);

		CCI_TRACE1(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO, "ciRequest_1() bufs avail handle=%d",
			   reqHandle);

		clientCiConfirmCallback(&cnf_args);

		ciReqArgs.handle = handle;
		ciReqArgs.primId = primId;
		ciReqArgs.reqHandle = reqHandle;
		ciReqArgs.paras = paras;

		clientCiSgFreeReqMem((CiRequestArgs *) &ciReqArgs);

		return CIRC_SUCCESS;
	}
#endif /*DATA_STUB_IN_KERNEL */

	if (gCICtrlAboveWatermark == TRUE) {
		ACIDBG_TRACE(1, 2, "ciRequest_1: CTRL in HighWatermark state - gCIDataAboveWatermark is TRUE\n");
		return CIRC_FAIL;
	}

	primDataSize = cimem_GetCiPrimDataSize(handle, primId, paras);

	pCiApiPara = (CiApiPara *) SHMEMAlloc(sizeof(CiApiPara) + sizeof(CiRequestArgs) + primDataSize);
	ASSERT(pCiApiPara != NULL);

	pArgs = (CiRequestArgs *) ((unsigned char *)pCiApiPara + sizeof(CiApiPara));
	memcpy(((unsigned char *)pArgs + sizeof(CiRequestArgs)), paras, primDataSize);

	/* Put Message to the stub task Queue */
	pArgs->handle = handle;
	pArgs->primId = primId;
	pArgs->reqHandle = reqHandle;
	pArgs->paras = paras;	/* must be pointed to real address for memory dealocation */

	/*BS: fix for mtsd sig11 - start */
	localArgs.handle = handle;
	localArgs.primId = primId;
	localArgs.reqHandle = reqHandle;
	localArgs.paras = paras;	/* must be pointed to real address for memory dealocation */
	localCiApiParaSave.argPtr = (unsigned char *)&localArgs;

	/*BS: fix coverity - start */
	localCiApiParaSave.argDataSize = sizeof(localArgs);	/*Coverity issue - unused param */
	localCiApiParaSave.prodId = CiRequestProcId;	/*Save prodId */
	/*BS: fix coverity - end */

	/*BS: fix for mtsd sig11 - end */

	pCiApiPara->prodId = CiRequestProcId;
	pCiApiPara->argPtr = (unsigned char *)pArgs;
	pCiApiPara->argDataSize = sizeof(CiRequestArgs) + primDataSize;

	/* send a message */
	SendCtrlPacket(pCiApiPara, sizeof(CiApiPara) + sizeof(CiRequestArgs) + primDataSize);
	/*BS: fix for mtsd sig11 - start */
	stubClientFreeCiApiPara(&localCiApiParaSave);
/*BS: fix for mtsd sig11 - end */

	return CIRC_SUCCESS;
}

void clientCiDefConfirmCallback(CiConfirmArgs *pArg)
{
	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO, "clientCiDefConfirmCallback()");

	/* call real callback function that register by RIL */
	if (gCiDefConfirmFuncPtr != NULL) {
		gCiDefConfirmFuncPtr(pArg->opSgCnfHandle, pArg->id, pArg->primId, pArg->reqHandle, pArg->paras);
	} else {
		CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_ERROR,
			  " CiDefConfirmCallback Function Pointer Error");
		ASSERT(FALSE);
	}
}

/* fix CRSM assert - start */
extern int CiStubUsesDynMem;
/* fix CRSM assert - end */

void clientCiConfirmCallback(CiConfirmArgs *pArg)
{
#ifdef ENV_LINUX
	CrsmCciAction *tem = head;

	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO, "clientCiConfirmCallback()");

	if (head != NULL) {
		while (tem != NULL && tem->crsm_reshandle != pArg->reqHandle) {
			tem = tem->next;
		}
	}

	/* call real callback function that register by RIL */
	if (gCiConfirmFuncPtr[pArg->id] != NULL) {
		if (tem != NULL) {
			clientCiConfirmCallback_transfer(pArg, tem);
			/* fix CRSM assert */
			/* we must call this for CP to free the confirm buffer - as the real confirm is manipulated internally */
			if (CiStubUsesDynMem)
				clientCiSgFreeCnfMem(pArg->opSgCnfHandle, pArg->id, pArg->primId, pArg->paras);
			/* fix CRSM assert */
		} else
			gCiConfirmFuncPtr[pArg->id] (pArg->opSgCnfHandle, pArg->id, pArg->primId, pArg->reqHandle,
						     pArg->paras);
	} else {
		CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_ERROR,
			  " CiConfirmCallback Function Pointer Error");
		ASSERT(FALSE);
	}

#else
	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO, "clientCiConfirmCallback()");

	if (gCiConfirmFuncPtr[pArg->id] != NULL) {
		gCiConfirmFuncPtr[pArg->id] (pArg->opSgCnfHandle, pArg->id, pArg->primId, pArg->reqHandle, pArg->paras);
	} else {
		CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_ERROR,
			  " CiConfirmCallback Function Pointer Error");
		ASSERT(FALSE);
	}

#endif

}

/*--------------------------------------------------------------------
 *  Description: Send response service primitive from application subsystem into cellular subsystem in response to
 *  indication service primitive received
 *	Input Parameters:
 *	Output Paramwter: None
 *
 *	Returns:
 *	 CIRC_FAIL, if ciShConfirm is NULL;CIRC_INTERLINK_FAIL, if the link is broken, CIRC_SUCCESS, if otherwise.
 *-------------------------------------------------------------------- */
CiReturnCode ciRespond_1(CiServiceHandle handle, CiPrimitiveID primId, CiIndicationHandle indHandle, void *paras)
{
	CiApiPara *pCiApiPara;
	CiRespondArgs *pArgs;
	unsigned int primDataSize;

	/*BS: fix for mtsd sig11 - start */
	CiRespondArgs localArgs;
	CiApiPara localCiApiParaSave;
	/*BS: fix for mtsd sig11 - end */

	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO, "ciRespond_1()");

	/* Check Link status flag first */
	if (gDlLinkStatusFlag == LINK_DIS_CONNECT) {
		CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_ERROR, "CIRC_INTERLINK_FAIL");
		return CIRC_INTERLINK_FAIL;
	}
#if !defined(DATA_STUB_IN_KERNEL)
	/* ignore all the ciConfirm for the CiServiceGroupID = CI_SG_ID_DAT */
	if (gCiServiceHandle[CI_SG_ID_DAT] == handle && primId == CI_DAT_PRIM_RECV_DATA_OPT_RSP) {
		CiRespondArgs ciRespondArgs;

		ciRespondArgs.handle = handle;
		ciRespondArgs.primId = primId;
		ciRespondArgs.indHandle = indHandle;
		ciRespondArgs.paras = paras;

		/*Ignore these primitives and free their memory */
		clientCiSgFreeRspMem(paras);

		return CIRC_SUCCESS;
	}
#endif /*DATA_STUB_IN_KERNEL */

	if (gCICtrlAboveWatermark == TRUE) {
		return CIRC_FAIL;
	}

	primDataSize = cimem_GetCiPrimDataSize(handle, primId, paras);

	pCiApiPara = (CiApiPara *) SHMEMAlloc(sizeof(CiApiPara) + sizeof(CiRespondArgs) + primDataSize);
	ASSERT(pCiApiPara != NULL);

	pArgs = (CiRespondArgs *) ((unsigned char *)pCiApiPara + sizeof(CiApiPara));

	/* Put Message to the stub task Queue */
	pArgs->handle = handle;
	pArgs->primId = primId;
	pArgs->indHandle = indHandle;
	pArgs->paras = paras;	/* must be pointed to real address for memory dealocations */

	/*BS: fix for mtsd sig11 - start */
	localArgs.handle = handle;
	localArgs.primId = primId;
	localArgs.indHandle = indHandle;
	localArgs.paras = paras;	/* must be pointed to real address for memory dealocation */
	localCiApiParaSave.argPtr = (unsigned char *)&localArgs;

	/*BS: fix coverity - start */
	localCiApiParaSave.argDataSize = sizeof(localArgs);	/*Coverity issue - unused param */
	localCiApiParaSave.prodId = CiRespondProcId;	/*Save prodId */
	/*BS: fix coverity - end */

	/*BS: fix for mtsd sig11 - end */

	pCiApiPara->prodId = CiRespondProcId;
	pCiApiPara->argPtr = (unsigned char *)pArgs;
	pCiApiPara->argDataSize = sizeof(CiRespondArgs) + primDataSize;

	memcpy(((unsigned char *)pArgs) + sizeof(CiRespondArgs), paras, primDataSize);

	/* send a message */
	SendCtrlPacket(pCiApiPara, sizeof(CiApiPara) + sizeof(CiRespondArgs) + primDataSize);
	/*BS: fix for mtsd sig11 - start */
	stubClientFreeCiApiPara(&localCiApiParaSave);
	/*BS: fix for mtsd sig11 - end */

	return CIRC_SUCCESS;
}

void clientCiDefIndicateCallback(CiIndicateArgs *pArg)
{
	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO, "clientCiDefIndicateCallback()");

	/* call real callback function that register by RIL */
	if (gCiDefIndicateFuncPtr != NULL) {
		gCiDefIndicateFuncPtr(pArg->opSgIndHandle, pArg->id, pArg->primId, pArg->indHandle, pArg->paras);
	} else {
		CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO,
			  "No CiDefInicateCallback Function Pointer defined");

		/* Just free the allocated memory */
		cimem_CiSgFreeMem(pArg->opSgIndHandle, pArg->id, pArg->primId, pArg->paras);
	}
}

void clientCiIndicateCallback(CiIndicateArgs *pArg)
{
	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO, "clientCiIndicateCallback()");

	/* call real callback function that register by RIL */
	if (gCiIndicateFuncPtr[pArg->id] != NULL) {
		gCiIndicateFuncPtr[pArg->id] (pArg->opSgIndHandle, pArg->id, pArg->primId, pArg->indHandle,
					      pArg->paras);
	} else {
		CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_ERROR,
			  " CiInicateCallback Function Pointer Error");
		ASSERT(FALSE);
	}
}

/* static stub call-back that is going to be called by the CI stub task for free Sh request parameter */
void clientCiShFreeReqMem(CiShRequestArgs *pCiShRequestArgs)
{
	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO, "clientCiShFreeReqMemCallback()");

	/* If ReqMemFuncPtr is NULL then Client has responsible to free the memory */
	if (pCiShRequestArgs == NULL) {
		return;
	}
	if (gClientCiShFreeReqMemFuncPtr != NULL)
		gClientCiShFreeReqMemFuncPtr(gOpShFreeHandle, pCiShRequestArgs->oper, pCiShRequestArgs->reqParas);

}

/* static stub call-back that is going to be called by the CI user (e.g. RIL) for free Sh cnf parameter */
/*CiShRequestProcId */
void clientCiShFreeCnfMem(CiShOpaqueHandle opShFreeHandle, CiShOper oper, void *cnfParas)
{
	UNUSEDPARAM(opShFreeHandle);
	UNUSEDPARAM(oper);

	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO,
		  "CI user calls ShFreeCnfMemCallback for freeing mem ");

	if (cnfParas != NULL)
		OnFreeCtrlPacket((unsigned char *)cnfParas - (sizeof(CiApiCallbackPara) + sizeof(CiShConfirmArgs)));
}

/* Free memory call-back that is used by the CI user service group to free memory allocated by the Client Stub
for the confirm service primitive */
static void clientCiSgFreeCnfMem(CiSgOpaqueHandle opSgFreeHandle, CiServiceGroupID id, CiPrimitiveID primId,
				 void *cnfParas)
{
	UNUSEDPARAM(opSgFreeHandle);

	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO,
		  "CI user calls SgFreeCnfMemCallback for freeing mem");

	if (id == CI_SG_ID_DAT && primId == CI_DAT_PRIM_SEND_DATA_OPT_CNF) {
		CI_MEM_FREE(cnfParas);
	} else if (cnfParas != NULL)
		OnFreeCtrlPacket((unsigned char *)cnfParas - (sizeof(CiApiCallbackPara) + sizeof(CiConfirmArgs)));
}

/* Free memory call-back that is used by the CI user service group to free memory allocated by the server service group
for the indicate service primitive */
static void clientCiSgFreeIndMem(CiSgOpaqueHandle opSgFreeHandle, CiServiceGroupID id, CiPrimitiveID primId,
				 void *paras)
{
	UNUSEDPARAM(opSgFreeHandle);

	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_INFO,
		  "CI user calls SgFreeIndMemCallback for freeing mem");
	if (id == CI_SG_ID_DAT && primId == CI_DAT_PRIM_RECV_DATA_OPT_IND) {
		CiDatPrimRecvDataOptInd *pDatRecvInd = (CiDatPrimRecvDataOptInd *) paras;
		printf("\n\n\n\nYG: ERROR -DAT- NOT ALLOWED HERE !!!!!!!!!\n\n\n\n");
		ASSERT(FALSE);	/*YG: WE SHALL NEVER GET HERE ON USER SPACE !!!! */
		ACIDBG_TRACE_2P(1, 3, "clientCiSgFreeIndMem DATA: paras = %x, pDatRecvInd->recvPdu.data = %x",
				(unsigned int)paras, (unsigned int)pDatRecvInd->recvPdu.data);

#if defined(CCI_OPT_SAC_USE_SHMEM_DIRECT)

		/*YG: this (probably) not required since the COMM doesn't use this info from IND */
		pDatRecvInd->recvPdu.data = (void *)ACIPCD_APPADDR_TO_COMADDR((unsigned int)pDatRecvInd->recvPdu.data);

		ACIDBG_TRACE_2P(1, 3,
				"clientCiSgFreeIndMem DATA: freePtr = %x, converted pDatRecvInd->recvPdu.data = %x",
				((unsigned int)paras - sizeof(CiStubInfo)), pDatRecvInd->recvPdu.data);

		OnFreeDataPacket((void *)((unsigned int)paras - sizeof(CiStubInfo)));
#else /*CCI_OPT_SAC_USE_SHMEM_DIRECT */
		OnFreeDataPacket((unsigned char *)((*pDatRecvInd).recvPdu).data -
				 (sizeof(CiStubInfo) + sizeof(CiDatPrimRecvDataOptInd)));
#endif /*CCI_OPT_SAC_USE_SHMEM_DIRECT */
	} else if (paras != NULL) {
		OnFreeCtrlPacket((unsigned char *)paras - (sizeof(CiApiCallbackPara) + sizeof(CiIndicateArgs)));
	}
}

/* service group Free memory call-back that is used by the CI Server task to service group to free memory allocated
by the server service group for the indicate service primitive */
static void clientRegisterCiSgFreeFunction(CiServiceGroupID id,
					   CiSgOpaqueHandle reqHandle,
					   CiSgOpaqueHandle rspHandle,
					   CiSgFreeReqMem ciSgFreeReqMem, CiSgFreeRspMem ciSgFreeRspMem)
{
	if (id >= CI_SG_ID_NEXTAVAIL) {
		CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_ERROR, "Invalid service ID");
		ASSERT(FALSE);
		return;
	}
	/* Can be Null If client uses a static memory */
	if (ciSgFreeReqMem != NULL) {
		gCiSgOpaqueReqHandle[id] = reqHandle;
		gCiSgFreeReqMemFuncTable[id] = ciSgFreeReqMem;
	}

	if (ciSgFreeRspMem != NULL) {
		gCiSgOpaqueRspHandle[id] = rspHandle;
		gCiSgFreeRspMemFuncTable[id] = ciSgFreeRspMem;
	}
}

/* static stub call-back that is going to be called by the CI stub task for free CI Svc Group request parameter */
void clientCiSgFreeReqMem(CiRequestArgs *pCiRequestArgs)
{
	CiServiceGroupID id;

	if (pCiRequestArgs == NULL)	/* true for */
		return;

	id = findCiSgId(pCiRequestArgs->handle);
	if (id == 0) {
		CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_ERROR,
			  " Can't Find Ci Service group FreeReqMem Function ");
		return;
	}

	if (gCiSgFreeReqMemFuncTable[id]) {
		gCiSgFreeReqMemFuncTable[id] (gCiSgOpaqueReqHandle[id],
					      id, pCiRequestArgs->primId, pCiRequestArgs->paras);
	}

}

/* static stub call-back that is going to be called by the CI stub task for free CI Svc Group respond parameter */
void clientCiSgFreeRspMem(CiRespondArgs *pCiRespondArgs)
{
	CiServiceGroupID id;

	id = findCiSgId(pCiRespondArgs->handle);
	if (id == 0) {
		CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_ERROR,
			  " Can't Find Ci Service group FreeRspMem Function ");
		return;
	}
	if (gCiSgFreeRspMemFuncTable[id]) {
		gCiSgFreeRspMemFuncTable[id] (gCiSgOpaqueRspHandle[id],
					      id, pCiRespondArgs->primId, pCiRespondArgs->paras);
	}
}

void clientStubCleanup(void)
{
	unsigned char sgIdIndex;

	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT_TASK, __LINE__, CCI_TRACE_INFO,
		  "clientStubCleanup: Clear shell and service handle information and callback functions");

	/* clear the shell and default callbacks and handle information */
	gClientCiShFreeReqMemFuncPtr = NULL;
	gCiShConfirmFuncPtr = NULL;
	gCiDefConfirmFuncPtr = NULL;

	gCiShOpaqueHandle = 0;
	gOpShFreeHandle = 1;

	/* NOTE: the gCiDefIndicateFuncPtr is not reset as we need this to send back the link up indications */

	/* Reset the registered callbacks and handle information for each service group */
	for (sgIdIndex = 0; sgIdIndex < CI_SG_ID_NEXTAVAIL; sgIdIndex++) {
		gCiConfirmFuncPtr[sgIdIndex] = NULL;
		gCiIndicateFuncPtr[sgIdIndex] = NULL;
		gCiServiceHandle[sgIdIndex] = 0;
		gCiSgOpaqueReqHandle[sgIdIndex] = 0;
		gCiSgOpaqueRspHandle[sgIdIndex] = 0;
		gCiSgFreeReqMemFuncTable[sgIdIndex] = NULL;
		gCiSgFreeRspMemFuncTable[sgIdIndex] = NULL;
	}
}

void clientSendCiLinkUpInd(void)
{
	CiIndicateArgs ciIndicateArgs;

	ciIndicateArgs.id = 0;
	ciIndicateArgs.indHandle = 0;
	ciIndicateArgs.opSgIndHandle = 0;
	ciIndicateArgs.primId = CI_ERR_PRIM_INTERLINKUP_IND;
	ciIndicateArgs.paras = cimem_CiSgCnfIndAllocMem(0, CI_ERR_PRIM_INTERLINKUP_IND);

	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT_TASK, __LINE__, CCI_TRACE_ERROR, " Client Stub Link UP Indication");

	clientCiDefIndicateCallback(&ciIndicateArgs);
}

void clientSendCiLinkDownInd(void)
{
	CiIndicateArgs ciIndicateArgs;
	CiErrPrimInterlinkDownInd *pLinkDownInd;

	ciIndicateArgs.id = 0;
	ciIndicateArgs.indHandle = 0;
	ciIndicateArgs.opSgIndHandle = 0;
	ciIndicateArgs.primId = CI_ERR_PRIM_INTERLINKDOWN_IND;
	ciIndicateArgs.paras = cimem_CiSgCnfIndAllocMem(0, CI_ERR_PRIM_INTERLINKDOWN_IND);

	/* Set the cause for the link down */
	pLinkDownInd = (CiErrPrimInterlinkDownInd *) ciIndicateArgs.paras;
	pLinkDownInd->cause = CIERR_INTERLINK_DOWN;

	CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT_TASK, __LINE__, CCI_TRACE_ERROR, " Client Stub Link DOWN Indication");

	clientCiDefIndicateCallback(&ciIndicateArgs);
}

static void stubClientFreeCiApiPara(CiApiPara *pCiApiPara)
{
	/* Free the memory for CI API parameter */
	switch (pCiApiPara->prodId) {
	case CiShRegisterReqProcId:
		break;

	case CiShDeregisterReqProcId:
		break;

	case CiShRequestProcId:
		clientCiShFreeReqMem((CiShRequestArgs *) pCiApiPara->argPtr);
		break;

	case CiRequestProcId:
		clientCiSgFreeReqMem((CiRequestArgs *) pCiApiPara->argPtr);
		break;

	case CiRespondProcId:
		clientCiSgFreeRspMem((CiRespondArgs *) pCiApiPara->argPtr);
		break;

	default:
		CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT_TASK, __LINE__, CCI_TRACE_ERROR,
			  " Failed: stubClientFreeCiApiPara");
		break;
	}			/* end switch */
	/* Free the memory for CI API message parameter */
}

CiServiceGroupID findCiSgId(CiServiceHandle handle)
{
	unsigned char i;
	for (i = CI_SG_ID_FIRST; i < CI_SG_ID_NEXTAVAIL; i++) {
		if (handle == gCiServiceHandle[i]) {
			return i;
		}
	}

	return 0;
}

static unsigned char SendCtrlPacket(void *pData, int iSize)
{
	ACIPCD_ReturnCodeE rc;

	rc = ACIPCDTxReq(ACIPCD_CI_CTRL, pData, iSize);
	if (rc == ACIPCD_RC_Q_FULL || rc == ACIPCD_RC_ILLEGAL_PARAM || rc == ACIPCD_RC_NO_MEMORY) {
		ASSERT(0);
	}
#if !defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
	else if (rc == ACIPCD_RC_HIGH_WM) {
		ACIDBG_TRACE(1, 1, "SendCtrlPacket: Got High Watermark!!!\n");

		/*CCI_ASSERT(0); */
		WaterMarkCiIndication(CI_CTRL_PDU, TRUE);
		return FALSE;
	}
#endif /*!ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */

	return TRUE;
}

static void OnFreeCtrlPacket(void *pData)
{
	ACIPCD_ReturnCodeE rc;

	rc = ACIPCDRxDoneRsp(ACIPCD_CI_CTRL, pData);
	if (rc == ACIPCD_RC_Q_FULL || rc == ACIPCD_RC_ILLEGAL_PARAM || rc == ACIPCD_RC_NO_MEMORY)
		ASSERT(0);
}

static void OnFreeDataPacket(void *pData)
{
	ACIPCD_ReturnCodeE rc;

	rc = ACIPCDRxDoneRsp(ACIPCD_CI_DATA_IND_RSP, pData);
	if (rc == ACIPCD_RC_Q_FULL || rc == ACIPCD_RC_ILLEGAL_PARAM || rc == ACIPCD_RC_NO_MEMORY)
		ASSERT(0);
}

/*
 * SHMEMAlloc
 * This function is modified in the following way:
 * 1. Call SHMEMMallocReq
 * 2. If returned value is NULL, sleep for 100 ms
 *    and then try allocating memory again
 * 3. After 50 tries (equaling 5 seconds), if sill
 *    no memory, ASSERT.
 */

#define	CI_SHMEM_ALLOC_WAIT_TIME	100	/*mseconds */
#define	CI_SHMEM_ALLOC_MAX_TRIES	50

static void *SHMEMAlloc(int iSize)
{
	void *ptr;
	unsigned int numTries = 0;

	do {
		ptr = SHMEMMallocReq(iSize);	/*call to kernel ioctl */
		if (ptr == NULL) {
			usleep(CI_SHMEM_ALLOC_WAIT_TIME);
		}
	} while ((ptr == NULL) && (numTries++ < CI_SHMEM_ALLOC_MAX_TRIES));

	return ptr;
}

static void SHMEMFree(void *pData)
{
	SHMEMFreeReq(pData);	/*call to kernel ioctl */
}

static unsigned char ShmemClientCIStubInit()
{
	ACIPCD_CallBackFuncS CallBackFunc;
	ACIPCD_ConfigS Config;
	ACIPCD_ReturnCodeE rc;

	OsaCriticalSectionCreateParamsT CriticalSectionCreateParams;

	/*printf("\nYG DBG: ShmemClientCIStubInit enter\n"); */
	memset((void *)&CriticalSectionCreateParams, 0, sizeof(CriticalSectionCreateParams));
	CriticalSectionCreateParams.name = "CI_Data";
	CriticalSectionCreateParams.bSharedForIpc = TRUE;

	gCi_CriticalSectionRef = OsaCriticalSectionCreate(&CriticalSectionCreateParams);

	/* Check if the client stub has been started already */
	if (gDlLinkStatusFlag == LINK_CONNECT) {
		return FALSE;
	}
#if defined(OSA_WINCE)
	gACIPCD_ShmemRef = OsaMemGetPoolRef(SHMEM_POOL, NULL, NULL);
#endif

	memset(&CallBackFunc, 0, sizeof(CallBackFunc));
	GetCallbacksKernelFn(&CallBackFunc, 0);

	/*printf("\nYG DBG: ShmemClientCIStubInit REQ_CNF linkcb=%x\n", CallBackFunc.LinkStatusIndCB); */

	memset(&Config, 0, sizeof(Config));
	Config.HighWm = 200;	/*CI_DATA_HIGH_WATERMARK_REQ_CNF; */
	Config.LowWm = 30;	/*CI_DATA_LOW_WATERMARK_REQ_CNF; */
	Config.RxAction = ACIPCD_DO_NOTHING;
	Config.TxAction = ACIPCD_DO_NOTHING;
	Config.BlockOnRegister = FALSE;

	/*printf("\nYG:User ShmemClientCIStubInit (service=%d) %x\n", ACIPCD_CI_DATA_REQ_CNF, CallBackFunc.LinkStatusIndCB); */

	rc = ACIPCDRegister(ACIPCD_CI_DATA_REQ_CNF, &CallBackFunc, &Config);
	ASSERT(rc == ACIPCD_RC_OK);

	/*ACIPCD_CI_DATA_IND_RSP */
	memset(&CallBackFunc, 0, sizeof(CallBackFunc));
	GetCallbacksKernelFn(&CallBackFunc, 1);

	/*printf("\nYG DBG: ShmemClientCIStubInit IND_RSP linkcb=%x\n", CallBackFunc.LinkStatusIndCB); */

	/*printf("\nYG:User ShmemClientCIStubInit (service=%d) %x\n", ACIPCD_CI_DATA_IND_RSP, CallBackFunc.LinkStatusIndCB); */

	memset(&Config, 0, sizeof(Config));
	Config.HighWm = CI_DATA_HIGH_WATERMARK_IND_RSP;
	Config.LowWm = CI_DATA_LOW_WATERMARK_IND_RSP;
	Config.RxAction = ACIPCD_DO_NOTHING;
	Config.TxAction = ACIPCD_DO_NOTHING;
	Config.BlockOnRegister = FALSE;

	rc = ACIPCDRegister(ACIPCD_CI_DATA_IND_RSP, &CallBackFunc, &Config);
	ASSERT(rc == ACIPCD_RC_OK);

	/*ACIPCD_CI_DATA_CONTROL_OLD */
	printf("\nYG: registering ACIPCD_CI_DATA_CONTROL_OLD\n");
	memset(&CallBackFunc, 0, sizeof(CallBackFunc));
	GetCallbacksKernelFn(&CallBackFunc, 2);
	printf
	    ("\nYG: Data CONTROL OLD: CallBackFunc.LinkStatusIndCB %p, CallBackFunc.RxIndCB %p, CallBackFunc.TxDoneCnfCB %p, CallBackFunc.TxFailCnfCB %p, CallBackFunc.WatermarkIndCB %p\n",
	     CallBackFunc.LinkStatusIndCB, CallBackFunc.RxIndCB, CallBackFunc.TxDoneCnfCB, CallBackFunc.TxFailCnfCB,
	     CallBackFunc.WatermarkIndCB);

	memset(&Config, 0, sizeof(Config));
	Config.HighWm = CI_DATA_CONTROL_HIGH_WATERMARK;
	Config.LowWm = CI_DATA_CONTROL_LOW_WATERMARK;
	Config.RxAction = ACIPCD_USE_ADDR_AS_PARAM;
	Config.TxAction = ACIPCD_COPY;
	Config.BlockOnRegister = FALSE;

	rc = ACIPCDRegister(ACIPCD_CI_DATA_CONTROL_OLD, &CallBackFunc, &Config);
	ASSERT(rc == ACIPCD_RC_OK);

	/* CI Control channel */
	memset(&CallBackFunc, 0, sizeof(CallBackFunc));
	CallBackFunc.RxIndCB = CI_SHM_CtrlRxInd;
	CallBackFunc.LinkStatusIndCB = CI_SHM_LinkStatusControlInd;

#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
	CallBackFunc.WatermarkIndCB = CI_SHM_CtrlWatermarkInd;
#else /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
	CallBackFunc.LowWmIndCB = CI_SHM_lowCtrlWmInd;
#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */

	CallBackFunc.TxDoneCnfCB = CI_SHM_txDoneCtrlInd;
	CallBackFunc.TxFailCnfCB = CI_SHM_txFailCtrlInd;

	memset(&Config, 0, sizeof(Config));
	Config.HighWm = CI_CTRL_HIGH_WATERMARK;
	Config.LowWm = CI_CTRL_LOW_WATERMARK;
	Config.RxAction = ACIPCD_DO_NOTHING;
	Config.TxAction = ACIPCD_DO_NOTHING;
	Config.BlockOnRegister = FALSE;
	Config.TxCnfAction = ACIPCD_TXCNF_FREE_MEM;

	rc = ACIPCDRegister(ACIPCD_CI_CTRL, &CallBackFunc, &Config);
	ASSERT(rc == ACIPCD_RC_OK);

	gDlLinkStatusFlag = LINK_CONNECT;
	/*printf("\n\nYG DBG: ShmemClientCIStubInit exit\n\n"); */

	return TRUE;
}

unsigned char ciClientStubInit(CiClientStubStatusIndPtr pStatusIndCb)
{
	g_pCiStatusCallbackFn = pStatusIndCb;

	if (ShmemClientCIStubInit()) {
		return 0;
	}

	return 1;
}

void ciClientStubDeinit(void)
{
	ASSERT(FALSE);		/*not supported yet */
}

/* Ido: commnted this for MTIL CI trace */
/*#ifdef CI_DATA_DUMP */

#if defined(ENV_LINUX)
static char *sCsMsgName[] = {
	"",
	"CI_CC_PRIM_GET_NUMBERTYPE_REQ",
	"CI_CC_PRIM_GET_NUMBERTYPE_CNF",
	"CI_CC_PRIM_SET_NUMBERTYPE_REQ",
	"CI_CC_PRIM_SET_NUMBERTYPE_CNF",
	"CI_CC_PRIM_GET_SUPPORTED_CALLMODES_REQ",
	"CI_CC_PRIM_GET_SUPPORTED_CALLMODES_CNF",
	"CI_CC_PRIM_GET_CALLMODE_REQ",
	"CI_CC_PRIM_GET_CALLMODE_CNF",
	"CI_CC_PRIM_SET_CALLMODE_REQ",
	"CI_CC_PRIM_SET_CALLMODE_CNF",
	"CI_CC_PRIM_GET_SUPPORTED_DATA_BSTYPES_REQ",
	"CI_CC_PRIM_GET_SUPPORTED_DATA_BSTYPES_CNF",
	"CI_CC_PRIM_GET_DATA_BSTYPE_REQ",
	"CI_CC_PRIM_GET_DATA_BSTYPE_CNF",
	"CI_CC_PRIM_SET_DATA_BSTYPE_REQ",
	"CI_CC_PRIM_SET_DATA_BSTYPE_CNF",
	"CI_CC_PRIM_GET_AUTOANSWER_ACTIVE_REQ",
	"CI_CC_PRIM_GET_AUTOANSWER_ACTIVE_CNF",
	"CI_CC_PRIM_SET_AUTOANSWER_ACTIVE_REQ",
	"CI_CC_PRIM_SET_AUTOANSWER_ACTIVE_CNF",
	"CI_CC_PRIM_LIST_CURRENT_CALLS_REQ",
	"CI_CC_PRIM_LIST_CURRENT_CALLS_CNF",
	"CI_CC_PRIM_GET_CALLINFO_REQ",
	"CI_CC_PRIM_GET_CALLINFO_CNF",
	"CI_CC_PRIM_MAKE_CALL_REQ",
	"CI_CC_PRIM_MAKE_CALL_CNF",
	"CI_CC_PRIM_CALL_PROCEEDING_IND",
	"CI_CC_PRIM_MO_CALL_FAILED_IND",
	"CI_CC_PRIM_ALERTING_IND",
	"CI_CC_PRIM_CONNECT_IND",
	"CI_CC_PRIM_DISCONNECT_IND",
	"CI_CC_PRIM_INCOMING_CALL_IND",
	"CI_CC_PRIM_CALL_WAITING_IND",
	"CI_CC_PRIM_HELD_CALL_IND",
	"CI_CC_PRIM_ANSWER_CALL_REQ",
	"CI_CC_PRIM_ANSWER_CALL_CNF",
	"CI_CC_PRIM_REFUSE_CALL_REQ",
	"CI_CC_PRIM_REFUSE_CALL_CNF",
	"CI_CC_PRIM_MT_CALL_FAILED_IND",
	"CI_CC_PRIM_HOLD_CALL_REQ",
	"CI_CC_PRIM_HOLD_CALL_CNF",
	"CI_CC_PRIM_RETRIEVE_CALL_REQ",
	"CI_CC_PRIM_RETRIEVE_CALL_CNF",
	"CI_CC_PRIM_SWITCH_ACTIVE_HELD_REQ",
	"CI_CC_PRIM_SWITCH_ACTIVE_HELD_CNF",
	"CI_CC_PRIM_CALL_DEFLECT_REQ",
	"CI_CC_PRIM_CALL_DEFLECT_CNF",
	"CI_CC_PRIM_EXPLICIT_CALL_TRANSFER_REQ",
	"CI_CC_PRIM_EXPLICIT_CALL_TRANSFER_CNF",
	"CI_CC_PRIM_RELEASE_CALL_REQ",
	"CI_CC_PRIM_RELEASE_CALL_CNF",
	"CI_CC_PRIM_RELEASE_ALL_CALLS_REQ",
	"CI_CC_PRIM_RELEASE_ALL_CALLS_CNF",
	"CI_CC_PRIM_SWITCH_CALLMODE_REQ",
	"CI_CC_PRIM_SWITCH_CALLMODE_CNF",
	"CI_CC_PRIM_ESTABLISH_MPTY_REQ",
	"CI_CC_PRIM_ESTABLISH_MPTY_CNF",
	"CI_CC_PRIM_ADD_TO_MPTY_REQ",
	"CI_CC_PRIM_ADD_TO_MPTY_CNF",
	"CI_CC_PRIM_HOLD_MPTY_REQ",
	"CI_CC_PRIM_HOLD_MPTY_CNF",
	"CI_CC_PRIM_RETRIEVE_MPTY_REQ",
	"CI_CC_PRIM_RETRIEVE_MPTY_CNF",
	"CI_CC_PRIM_SPLIT_FROM_MPTY_REQ",
	"CI_CC_PRIM_SPLIT_FROM_MPTY_CNF",
	"CI_CC_PRIM_SHUTTLE_MPTY_REQ",
	"CI_CC_PRIM_SHUTTLE_MPTY_CNF",
	"CI_CC_PRIM_RELEASE_MPTY_REQ",
	"CI_CC_PRIM_RELEASE_MPTY_CNF",
	"CI_CC_PRIM_START_DTMF_REQ",
	"CI_CC_PRIM_START_DTMF_CNF",
	"CI_CC_PRIM_STOP_DTMF_REQ",
	"CI_CC_PRIM_STOP_DTMF_CNF",
	"CI_CC_PRIM_GET_DTMF_PACING_REQ",
	"CI_CC_PRIM_GET_DTMF_PACING_CNF",
	"CI_CC_PRIM_SET_DTMF_PACING_REQ",
	"CI_CC_PRIM_SET_DTMF_PACING_CNF",
	"CI_CC_PRIM_SEND_DTMF_STRING_REQ",
	"CI_CC_PRIM_SEND_DTMF_STRING_CNF",
	"CI_CC_PRIM_CLIP_INFO_IND",
	"CI_CC_PRIM_COLP_INFO_IND",
	"CI_CC_PRIM_CCM_UPDATE_IND",
	"CI_CC_PRIM_GET_CCM_VALUE_REQ",
	"CI_CC_PRIM_GET_CCM_VALUE_CNF",
	"CI_CC_PRIM_AOC_WARNING_IND",
	"CI_CC_PRIM_SSI_NOTIFY_IND",
	"CI_CC_PRIM_SSU_NOTIFY_IND",
	"CI_CC_PRIM_LOCALCB_NOTIFY_IND",
	"CI_CC_PRIM_GET_ACM_VALUE_REQ",
	"CI_CC_PRIM_GET_ACM_VALUE_CNF",
	"CI_CC_PRIM_RESET_ACM_VALUE_REQ",
	"CI_CC_PRIM_RESET_ACM_VALUE_CNF",
	"CI_CC_PRIM_GET_ACMMAX_VALUE_REQ",
	"CI_CC_PRIM_GET_ACMMAX_VALUE_CNF",
	"CI_CC_PRIM_SET_ACMMAX_VALUE_REQ",
	"CI_CC_PRIM_SET_ACMMAX_VALUE_CNF",
	"CI_CC_PRIM_GET_PUCT_INFO_REQ",
	"CI_CC_PRIM_GET_PUCT_INFO_CNF",
	"CI_CC_PRIM_SET_PUCT_INFO_REQ",
	"CI_CC_PRIM_SET_PUCT_INFO_CNF",
	"CI_CC_PRIM_GET_BASIC_CALLMODES_REQ",
	"CI_CC_PRIM_GET_BASIC_CALLMODES_CNF",
	"CI_CC_PRIM_GET_CALLOPTIONS_REQ",
	"CI_CC_PRIM_GET_CALLOPTIONS_CNF",
	"CI_CC_PRIM_GET_DATACOMP_CAP_REQ",
	"CI_CC_PRIM_GET_DATACOMP_CAP_CNF",
	"CI_CC_PRIM_GET_DATACOMP_REQ",
	"CI_CC_PRIM_GET_DATACOMP_CNF",
	"CI_CC_PRIM_SET_DATACOMP_REQ",
	"CI_CC_PRIM_SET_DATACOMP_CNF",
	"CI_CC_PRIM_GET_RLP_CAP_REQ",
	"CI_CC_PRIM_GET_RLP_CAP_CNF",
	"CI_CC_PRIM_GET_RLP_CFG_REQ",
	"CI_CC_PRIM_GET_RLP_CFG_CNF",
	"CI_CC_PRIM_SET_RLP_CFG_REQ",
	"CI_CC_PRIM_SET_RLP_CFG_CNF",
	"CI_CC_PRIM_DATA_SERVICENEG_IND",
	"CI_CC_PRIM_ENABLE_DATA_SERVICENEG_IND_REQ",
	"CI_CC_PRIM_ENABLE_DATA_SERVICENEG_IND_CNF",
	"CI_CC_PRIM_SET_UDUB_REQ",
	"CI_CC_PRIM_SET_UDUB_CNF",
	"CI_CC_PRIM_GET_SUPPORTED_CALLMAN_OPS_REQ",
	"CI_CC_PRIM_GET_SUPPORTED_CALLMAN_OPS_CNF",
	"CI_CC_PRIM_MANIPULATE_CALLS_REQ",
	"CI_CC_PRIM_MANIPULATE_CALLS_CNF",
	"CI_CC_PRIM_LIST_CURRENT_CALLS_IND",
	"CI_CC_PRIM_CALL_DIAGNOSTIC_IND",
	"CI_CC_PRIM_DTMF_EVENT_IND",
	"CI_CC_PRIM_CLEAR_BLACK_LIST_REQ",
	"CI_CC_PRIM_CLEAR_BLACK_LIST_CNF",
	"CI_CC_PRIM_SET_CTM_STATUS_REQ",
	"CI_CC_PRIM_SET_CTM_STATUS_CNF",
	"CI_CC_PRIM_CTM_NEG_REPORT_IND",
	"CI_CC_PRIM_CDIP_INFO_IND",	/*Michal Bukai - CDIP support */
	/*Michal Bukai - ALS support - START */
	"CI_CC_PRIM_GET_LINE_ID_REQ",
	"CI_CC_PRIM_GET_LINE_ID_CNF",
	"CI_CC_PRIM_SET_LINE_ID_REQ",
	"CI_CC_PRIM_SET_LINE_ID_CNF",
	/*Michal Bukai - ALS support - END */
	"CI_CC_PRIM_READY_STATE_IND",
	"CI_CC_PRIM_SET_CECALL_REQ",
	"CI_CC_PRIM_SET_CECALL_CNF",
	"CI_CC_PRIM_GET_CECALL_REQ",
	"CI_CC_PRIM_GET_CECALL_CNF",
	"CI_CC_PRIM_GET_CECALL_CAP_REQ",
	"CI_CC_PRIM_GET_CECALL_CAP_CNF",
	"CI_CC_PRIM_LAST_COMMON_PRIM"
};

static char *sPsMsgName[] = {
	"",
	"CI_PS_PRIM_SET_ATTACH_STATE_REQ",
	"CI_PS_PRIM_SET_ATTACH_STATE_CNF",
	"CI_PS_PRIM_GET_ATTACH_STATE_REQ",
	"CI_PS_PRIM_GET_ATTACH_STATE_CNF",
	"CI_PS_PRIM_DEFINE_PDP_CTX_REQ",
	"CI_PS_PRIM_DEFINE_PDP_CTX_CNF",
	"CI_PS_PRIM_DELETE_PDP_CTX_REQ",
	"CI_PS_PRIM_DELETE_PDP_CTX_CNF",
	"CI_PS_PRIM_GET_PDP_CTX_REQ",
	"CI_PS_PRIM_GET_PDP_CTX_CNF",
	"CI_PS_PRIM_GET_PDP_CTX_CAPS_REQ",
	"CI_PS_PRIM_GET_PDP_CTX_CAPS_CNF",
	"CI_PS_PRIM_SET_PDP_CTX_ACT_STATE_REQ",
	"CI_PS_PRIM_SET_PDP_CTX_ACT_STATE_CNF",
	"CI_PS_PRIM_GET_PDP_CTXS_ACT_STATE_REQ",
	"CI_PS_PRIM_GET_PDP_CTXS_ACT_STATE_CNF",
	"CI_PS_PRIM_ENTER_DATA_STATE_REQ",
	"CI_PS_PRIM_ENTER_DATA_STATE_CNF",
	"CI_PS_PRIM_MT_PDP_CTX_ACT_MODIFY_IND",
	"CI_PS_PRIM_MT_PDP_CTX_ACT_MODIFY_RSP",
	"CI_PS_PRIM_MT_PDP_CTX_ACTED_IND",
	"CI_PS_PRIM_SET_GSMGPRS_CLASS_REQ",
	"CI_PS_PRIM_SET_GSMGPRS_CLASS_CNF",
	"CI_PS_PRIM_GET_GSMGPRS_CLASS_REQ",
	"CI_PS_PRIM_GET_GSMGPRS_CLASS_CNF",
	"CI_PS_PRIM_GET_GSMGPRS_CLASSES_REQ",
	"CI_PS_PRIM_GET_GSMGPRS_CLASSES_CNF",
	"CI_PS_PRIM_ENABLE_NW_REG_IND_REQ",
	"CI_PS_PRIM_ENABLE_NW_REG_IND_CNF",
	"CI_PS_PRIM_NW_REG_IND",
	"CI_PS_PRIM_SET_QOS_REQ",
	"CI_PS_PRIM_SET_QOS_CNF",
	"CI_PS_PRIM_DEL_QOS_REQ",
	"CI_PS_PRIM_DEL_QOS_CNF",
	"CI_PS_PRIM_GET_QOS_REQ",
	"CI_PS_PRIM_GET_QOS_CNF",
	"CI_PS_PRIM_ENABLE_POWERON_AUTO_ATTACH_REQ",
	"CI_PS_PRIM_ENABLE_POWERON_AUTO_ATTACH_CNF",
	"CI_PS_PRIM_MT_PDP_CTX_REJECTED_IND",
	"CI_PS_PRIM_PDP_CTX_DEACTED_IND",
	"CI_PS_PRIM_PDP_CTX_REACTED_IND",
	"CI_PS_PRIM_DETACHED_IND",
	"CI_PS_PRIM_GPRS_CLASS_CHANGED_IND",
	"CI_PS_PRIM_GET_DEFINED_CID_LIST_REQ",
	"CI_PS_PRIM_GET_DEFINED_CID_LIST_CNF",
	"CI_PS_PRIM_GET_NW_REG_STATUS_REQ",
	"CI_PS_PRIM_GET_NW_REG_STATUS_CNF",
	"CI_PS_PRIM_GET_QOS_CAPS_REQ",
	"CI_PS_PRIM_GET_QOS_CAPS_CNF",
	"CI_PS_PRIM_ENABLE_EVENTS_REPORTING_REQ",
	"CI_PS_PRIM_ENABLE_EVENTS_REPORTING_CNF",
	/* SCR #1401348: 3G Quality of Service (QoS) primitives */
	"CI_PS_PRIM_GET_3G_QOS_REQ",
	"CI_PS_PRIM_GET_3G_QOS_CNF",
	"CI_PS_PRIM_SET_3G_QOS_REQ",
	"CI_PS_PRIM_SET_3G_QOS_CNF",
	"CI_PS_PRIM_DEL_3G_QOS_REQ",
	"CI_PS_PRIM_DEL_3G_QOS_CNF",
	"CI_PS_PRIM_GET_3G_QOS_CAPS_REQ",
	"CI_PS_PRIM_GET_3G_QOS_CAPS_CNF",
	/* SCR #1438547: Secondary PDP Context primitives */
	"CI_PS_PRIM_DEFINE_SEC_PDP_CTX_REQ",
	"CI_PS_PRIM_DEFINE_SEC_PDP_CTX_CNF",
	"CI_PS_PRIM_DELETE_SEC_PDP_CTX_REQ",
	"CI_PS_PRIM_DELETE_SEC_PDP_CTX_CNF",
	"CI_PS_PRIM_GET_SEC_PDP_CTX_REQ",
	"CI_PS_PRIM_GET_SEC_PDP_CTX_CNF",
	/* SCR #1438547: Traffic Flow Template (TFT) primitives */
	"CI_PS_PRIM_DEFINE_TFT_FILTER_REQ",
	"CI_PS_PRIM_DEFINE_TFT_FILTER_CNF",
	"CI_PS_PRIM_DELETE_TFT_REQ",
	"CI_PS_PRIM_DELETE_TFT_CNF",
	"CI_PS_PRIM_GET_TFT_REQ",
	"CI_PS_PRIM_GET_TFT_CNF",
	/* SCR TBD: PDP Context Modify primitives */
	"CI_PS_PRIM_MODIFY_PDP_CTX_REQ",
	"CI_PS_PRIM_MODIFY_PDP_CTX_CNF",
	"CI_PS_PRIM_GET_ACTIVE_CID_LIST_REQ",
	"CI_PS_PRIM_GET_ACTIVE_CID_LIST_CNF",
	"CI_PS_PRIM_REPORT_COUNTER_REQ",
	"CI_PS_PRIM_REPORT_COUNTER_CNF",
	"CI_PS_PRIM_RESET_COUNTER_REQ",
	"CI_PS_PRIM_RESET_COUNTER_CNF",
	"CI_PS_PRIM_COUNTER_IND",
	"CI_PS_PRIM_SEND_DATA_REQ",
	"CI_PS_PRIM_SEND_DATA_CNF",
/* Michal Bukai & Boris Tsatkin  AT&T Smart Card support - Start */
		    /*** AT&T- Smart Card  CI_PS_PRIM_ ACL SERVICE: LIST , SET , EDIT   -BT6 */
	"CI_PS_PRIM_SET_ACL_SERVICE_REQ",
	"CI_PS_PRIM_SET_ACL_SERVICE_CNF",
	"CI_PS_PRIM_GET_ACL_SIZE_REQ",
	"CI_PS_PRIM_GET_ACL_SIZE_CNF",
	"CI_PS_PRIM_READ_ACL_ENTRY_REQ",
	"CI_PS_PRIM_READ_ACL_ENTRY_CNF",
	"CI_PS_PRIM_EDIT_ACL_ENTRY_REQ",
	"CI_PS_PRIM_EDIT_ACL_ENTRY_CNF",
/* Michal Bukai & Boris Tsatkin  AT&T Smart Card support - End*/

/* Michal Bukai – PDP authentication - Start*/
	"CI_PS_PRIM_AUTHENTICATE_REQ",
	"CI_PS_PRIM_AUTHENTICATE_CNF",
/* Michal Bukai – PDP authentication - End*/
/* Michal Bukai – Fast Dormancy - Start*/
	"CI_PS_PRIM_FAST_DORMANT_REQ",
	"CI_PS_PRIM_FAST_DORMANT_CNF",
/* Michal Bukai – Fast Dormancy - End*/

/*Michal Bukai - AutoAttach Configuration - Samsung - START*/
	"CI_PS_PRIM_GET_POWERON_AUTO_ATTACH_STATUS_REQ",
	"CI_PS_PRIM_GET_POWERON_AUTO_ATTACH_STATUS_CNF",
/*Michal Bukai - AutoAttach Configuration - Samsung - END*/
/*Michal Bukai - HSPA configuration - START*/
	"CI_PS_PRIM_SET_HSPA_CONFIG_REQ",
	"CI_PS_PRIM_SET_HSPA_CONFIG_CNF",
/*Michal Bukai - HSPA configuration - END*/

	"CI_PS_PRIM_DATACOMP_REPORTING_REQ",
	"CI_PS_PRIM_DATACOMP_REPORTING_CNF",
	"CI_PS_PRIM_DATACOMP_IND",
	/*Michal Bukai - HSPA configuration - START */
	"CI_PS_PRIM_GET_HSPA_CONFIG_REQ",
	"CI_PS_PRIM_GET_HSPA_CONFIG_CNF",
	/*Michal Bukai - HSPA configuration - END */
	"CI_PS_PRIM_GET_EPS_NW_REG_STATUS_REQ",
	"CI_PS_PRIM_GET_EPS_NW_REG_STATUS_CNF",
	"CI_PS_PRIM_ENABLE_EPS_NW_REG_IND_REQ",
	"CI_PS_PRIM_ENABLE_EPS_NW_REG_IND_CNF",
	"CI_PS_PRIM_EPS_NW_REG_IND",
	"CI_PS_PRIM_EPS_GET_TFT_DYN_PARAM_REQ",
	"CI_PS_PRIM_EPS_GET_TFT_DYN_PARAM_CNF",
	"CI_PS_PRIM_EPS_SET_PDP_CTX_QOS_REQ",
	"CI_PS_PRIM_EPS_SET_PDP_CTX_QOS_CNF",
	"CI_PS_PRIM_EPS_DEL_PDP_CTX_QOS_REQ",
	"CI_PS_PRIM_EPS_DEL_PDP_CTX_QOS_CNF",
	"CI_PS_PRIM_EPS_GET_PDP_CTX_QOS_STATUS_REQ",
	"CI_PS_PRIM_EPS_GET_PDP_CTX_QOS_STATUS_CNF",
	"CI_PS_PRIM_EPS_GET_PDP_CTX_QOS_CAPS_REQ",
	"CI_PS_PRIM_EPS_GET_PDP_CTX_QOS_CAPS_CNF",
	"CI_PS_PRIM_EPS_SET_UE_MODE_REQ",
	"CI_PS_PRIM_EPS_SET_UE_MODE_CNF",
	"CI_PS_PRIM_EPS_GET_UE_MODE_REQ",
	"CI_PS_PRIM_EPS_GET_UE_MODE_CNF",
	"CI_PS_PRIM_SET_VOICE_DOMAIN_PREFERENCE_REQ",
	"CI_PS_PRIM_SET_VOICE_DOMAIN_PREFERENCE_CNF",
	"CI_PS_PRIM_GET_VOICE_DOMAIN_PREFERENCE_REQ",
	"CI_PS_PRIM_GET_VOICE_DOMAIN_PREFERENCE_CNF",
	"CI_PS_PRIM_SET_EPS_USAGE_SETTING_REQ",
	"CI_PS_PRIM_SET_EPS_USAGE_SETTING_CNF",
	"CI_PS_PRIM_GET_EPS_USAGE_SETTING_REQ",
	"CI_PS_PRIM_GET_EPS_USAGE_SETTING_CNF",
	"CI_PS_PRIM_SET_IMS_VOICE_CALL_AVAILABILITY_REQ",
	"CI_PS_PRIM_SET_IMS_VOICE_CALL_AVAILABILITY_CNF",
	"CI_PS_PRIM_GET_IMS_VOICE_CALL_AVAILABILITY_REQ",
	"CI_PS_PRIM_GET_IMS_VOICE_CALL_AVAILABILITY_CNF",
	"CI_PS_PRIM_SET_IMS_SMS_AVAILABILITY_REQ",
	"CI_PS_PRIM_SET_IMS_SMS_AVAILABILITY_CNF",
	"CI_PS_PRIM_GET_IMS_SMS_AVAILABILITY_REQ",
	"CI_PS_PRIM_GET_IMS_SMS_AVAILABILITY_CNF",
	"CI_PS_PRIM_SET_MM_IMS_VOICE_TERMINATION_REQ",
	"CI_PS_PRIM_SET_MM_IMS_VOICE_TERMINATION_CNF",
	"CI_PS_PRIM_GET_MM_IMS_VOICE_TERMINATION_REQ",
	"CI_PS_PRIM_GET_MM_IMS_VOICE_TERMINATION_CNF",
	"CI_PS_PRIM_SUSPEND_IND",
	"CI_PS_PRIM_MT_PDP_CTX_MODIFIED_IND",
	"CI_PS_PRIM_SET_TFT_PARAMETERS_LIST_REQ",
	"CI_PS_PRIM_SET_TFT_PARAMETERS_LIST_CNF",
	"CI_PS_PRIM_GET_TFT_PARAMETERS_LIST_REQ",
	"CI_PS_PRIM_GET_TFT_PARAMETERS_LIST_CNF",
	"CI_PS_PRIM_TEST_MODE_IND",
	"CI_PS_PRIM_TEST_MODE_RSP",
	"CI_PS_PRIM_ESM_SM_REJECT_CAUSE_IND",
	"CI_PS_PRIM_LAST_COMMON_PRIM"
	    /* ADD NEW COMMON PRIMITIVES HERE, BEFORE 'CI_PS_PRIM_LAST_COMMON_PRIM' */
	    /* END OF COMMON PRIMITIVES LIST */
};

static char *sSmsMsgName[] = {
	"",
	"CI_MSG_PRIM_GET_SUPPORTED_SERVICES_REQ",
	"CI_MSG_PRIM_GET_SUPPORTED_SERVICES_CNF",
	"CI_MSG_PRIM_SELECT_SERVICE_REQ",
	"CI_MSG_PRIM_SELECT_SERVICE_CNF",
	"CI_MSG_PRIM_GET_CURRENT_SERVICE_INFO_REQ",
	"CI_MSG_PRIM_GET_CURRENT_SERVICE_INFO_CNF",
	"CI_MSG_PRIM_GET_SUPPORTED_STORAGES_REQ",
	"CI_MSG_PRIM_GET_SUPPORTED_STORAGES_CNF",
	"CI_MSG_PRIM_SELECT_STORAGE_REQ",
	"CI_MSG_PRIM_SELECT_STORAGE_CNF",
	"CI_MSG_PRIM_GET_CURRENT_STORAGE_INFO_REQ",
	"CI_MSG_PRIM_GET_CURRENT_STORAGE_INFO_CNF",
	"CI_MSG_PRIM_READ_MESSAGE_REQ",
	"CI_MSG_PRIM_READ_MESSAGE_CNF",
	"CI_MSG_PRIM_DELETE_MESSAGE_REQ",
	"CI_MSG_PRIM_DELETE_MESSAGE_CNF",
	"CI_MSG_PRIM_SEND_MESSAGE_REQ",
	"CI_MSG_PRIM_SEND_MESSAGE_CNF",
	"CI_MSG_PRIM_WRITE_MESSAGE_REQ",
	"CI_MSG_PRIM_WRITE_MESSAGE_CNF",
	"CI_MSG_PRIM_SEND_COMMAND_REQ",
	"CI_MSG_PRIM_SEND_COMMAND_CNF",
	"CI_MSG_PRIM_SEND_STORED_MESSAGE_REQ",
	"CI_MSG_PRIM_SEND_STORED_MESSAGE_CNF",
	"CI_MSG_PRIM_CONFIG_MSG_IND_REQ",
	"CI_MSG_PRIM_CONFIG_MSG_IND_CNF",
	"CI_MSG_PRIM_NEWMSG_INDEX_IND",
	"CI_MSG_PRIM_NEWMSG_IND",
	"CI_MSG_PRIM_NEWMSG_RSP",
	"CI_MSG_PRIM_GET_SMSC_ADDR_REQ",
	"CI_MSG_PRIM_GET_SMSC_ADDR_CNF",
	"CI_MSG_PRIM_SET_SMSC_ADDR_REQ",
	"CI_MSG_PRIM_SET_SMSC_ADDR_CNF",
	"CI_MSG_PRIM_GET_MOSMS_SERVICE_CAP_REQ",
	"CI_MSG_PRIM_GET_MOSMS_SERVICE_CAP_CNF",
	"CI_MSG_PRIM_GET_MOSMS_SERVICE_REQ",
	"CI_MSG_PRIM_GET_MOSMS_SERVICE_CNF",
	"CI_MSG_PRIM_SET_MOSMS_SERVICE_REQ",
	"CI_MSG_PRIM_SET_MOSMS_SERVICE_CNF",
	"CI_MSG_PRIM_GET_CBM_TYPES_CAP_REQ",
	"CI_MSG_PRIM_GET_CBM_TYPES_CAP_CNF",
	"CI_MSG_PRIM_GET_CBM_TYPES_REQ",
	"CI_MSG_PRIM_GET_CBM_TYPES_CNF",
	"CI_MSG_PRIM_SET_CBM_TYPES_REQ",
	"CI_MSG_PRIM_SET_CBM_TYPES_CNF",
	"CI_MSG_PRIM_SELECT_STORAGES_REQ",
	"CI_MSG_PRIM_SELECT_STORAGES_CNF",
	"CI_MSG_PRIM_WRITE_MSG_WITH_INDEX_REQ",
	"CI_MSG_PRIM_WRITE_MSG_WITH_INDEX_CNF",
	"CI_MSG_PRIM_MESSAGE_WAITING_IND",
	"CI_MSG_PRIM_STORAGE_STATUS_IND",
	/*Michal Bukai - SMS Memory Full Feature - start */
	"CI_MSG_PRIM_RESET_MEMCAP_FULL_REQ",
	"CI_MSG_PRIM_RESET_MEMCAP_FULL_CNF",
	/*Michal Bukai - SMS Memory Full Feature - end */
	"CI_MSG_PRIM_GET_MSG_IND_CONFIG_REQ",
	"CI_MSG_PRIM_GET_MSG_IND_CONFIG_CNF",
	"CI_MSG_PRIM_GET_SMSC_PARAMS_REQ",
	"CI_MSG_PRIM_GET_SMSC_PARAMS_CNF",
	"CI_MSG_PRIM_SET_SMSC_PARAMS_REQ",
	"CI_MSG_PRIM_SET_SMSC_PARAMS_CNF",

	"CI_MSG_PRIM_LAST_COMMON_PRIM"
	    /* ADD NEW COMMON PRIMITIVES HERE, BEFORE 'CI_MSG_PRIM_LAST_COMMON_PRIM' */
	    /* END OF COMMON PRIMITIVES LIST */
};

static char *sPbMsgName[] = {
	"",
	"CI_PB_PRIM_GET_SUPPORTED_PHONEBOOKS_REQ",
	"CI_PB_PRIM_GET_SUPPORTED_PHONEBOOKS_CNF",
	"CI_PB_PRIM_GET_PHONEBOOK_INFO_REQ",
	"CI_PB_PRIM_GET_PHONEBOOK_INFO_CNF",
	"CI_PB_PRIM_SELECT_PHONEBOOK_REQ",
	"CI_PB_PRIM_SELECT_PHONEBOOK_CNF",
	"CI_PB_PRIM_READ_FIRST_PHONEBOOK_ENTRY_REQ",
	"CI_PB_PRIM_READ_FIRST_PHONEBOOK_ENTRY_CNF",
	"CI_PB_PRIM_READ_NEXT_PHONEBOOK_ENTRY_REQ",
	"CI_PB_PRIM_READ_NEXT_PHONEBOOK_ENTRY_CNF",
	"CI_PB_PRIM_FIND_FIRST_PHONEBOOK_ENTRY_REQ",
	"CI_PB_PRIM_FIND_FIRST_PHONEBOOK_ENTRY_CNF",
	"CI_PB_PRIM_FIND_NEXT_PHONEBOOK_ENTRY_REQ",
	"CI_PB_PRIM_FIND_NEXT_PHONEBOOK_ENTRY_CNF",
	"CI_PB_PRIM_ADD_PHONEBOOK_ENTRY_REQ",
	"CI_PB_PRIM_ADD_PHONEBOOK_ENTRY_CNF",
	"CI_PB_PRIM_DELETE_PHONEBOOK_ENTRY_REQ",
	"CI_PB_PRIM_DELETE_PHONEBOOK_ENTRY_CNF",
	"CI_PB_PRIM_REPLACE_PHONEBOOK_ENTRY_REQ",
	"CI_PB_PRIM_REPLACE_PHONEBOOK_ENTRY_CNF",
	"CI_PB_PRIM_READ_PHONEBOOK_ENTRY_REQ",
	"CI_PB_PRIM_READ_PHONEBOOK_ENTRY_CNF",
	"CI_PB_PRIM_GET_IDENTITY_REQ",
	"CI_PB_PRIM_GET_IDENTITY_CNF",
	"CI_PB_PRIM_PHONEBOOK_READY_IND",
	"CI_PB_PRIM_GET_ALPHA_NAME_REQ",
	"CI_PB_PRIM_GET_ALPHA_NAME_CNF",
/* Michal Bukai & Boris Tsatkin  AT&T Smart Card support - Start */
	/*AT&T- Smart Card  <CDR-SMCD-911> PB     ENHANCED_PHONEBOOK_ENTRY -BT1 */
	"CI_PB_PRIM_READ_ENH_PBK_ENTRY_REQ",
	"CI_PB_PRIM_READ_ENH_PBK_ENTRY_CNF",
	"CI_PB_PRIM_EDIT_ENH_PBK_ENTRY_REQ",
	"CI_PB_PRIM_EDIT_ENH_PBK_ENTRY_CNF",

	/*AT&T- Smart Card  <CDR-SMCD-941> PB     CI_PB CategoryName- EFAas -BT2  */
	"CI_PB_PRIM_READ_CATEGORY_NAME_REQ",
	"CI_PB_PRIM_READ_CATEGORY_NAME_CNF",
	"CI_PB_PRIM_EDIT_CATEGORY_NAME_REQ",
	"CI_PB_PRIM_EDIT_CATEGORY_NAME_CNF",

	/*AT&T- Smart Card  <CDR-SMCD-941> PB     CI_PB CategoryName- EFAas -BT3 */
	"CI_PB_PRIM_READ_GROUP_NAME_REQ",
	"CI_PB_PRIM_READ_GROUP_NAME_CNF",
	"CI_PB_PRIM_EDIT_GROUP_NAME_REQ",
	"CI_PB_PRIM_EDIT_GROUP_NAME_CNF",
/* Michal Bukai & Boris Tsatkin  AT&T Smart Card support - End*/
	"CI_PB_PRIM_GET_PHONEBOOK_CAPA_REQ",
	"CI_PB_PRIM_GET_PHONEBOOK_CAPA_CNF",

	"CI_PB_PRIM_LAST_COMMON_PRIM"
	    /* ADD NEW COMMON PRIMITIVES HERE, BEFORE 'CI_PB_PRIM_LAST_COMMON_PRIM' */
	    /* END OF COMMON PRIMITIVES LIST */
};

static char *sSimMsgName[] = {
	"",
	"CI_SIM_PRIM_EXECCMD_REQ",
	"CI_SIM_PRIM_EXECCMD_CNF",
	"CI_SIM_PRIM_DEVICE_IND",
	"CI_SIM_PRIM_PERSONALIZEME_REQ",
	"CI_SIM_PRIM_PERSONALIZEME_CNF",
	"CI_SIM_PRIM_OPERCHV_REQ",
	"CI_SIM_PRIM_OPERCHV_CNF",
	"CI_SIM_PRIM_DOWNLOADPROFILE_REQ",
	"CI_SIM_PRIM_DOWNLOADPROFILE_CNF",
	"CI_SIM_PRIM_ENDATSESSION_IND",
	"CI_SIM_PRIM_PROACTIVE_CMD_IND",
	"CI_SIM_PRIM_PROACTIVE_CMD_RSP",
	"CI_SIM_PRIM_ENVELOPE_CMD_REQ",
	"CI_SIM_PRIM_ENVELOPE_CMD_CNF",
	"CI_SIM_PRIM_GET_SUBSCRIBER_ID_REQ",
	"CI_SIM_PRIM_GET_SUBSCRIBER_ID_CNF",
	"CI_SIM_PRIM_GET_PIN_STATE_REQ",
	"CI_SIM_PRIM_GET_PIN_STATE_CNF",
	"CI_SIM_PRIM_GET_TERMINALPROFILE_REQ",
	"CI_SIM_PRIM_GET_TERMINALPROFILE_CNF",
	"CI_SIM_PRIM_ENABLE_SIMAT_INDS_REQ",
	"CI_SIM_PRIM_ENABLE_SIMAT_INDS_CNF",
	"CI_SIM_PRIM_LOCK_FACILITY_REQ",
	"CI_SIM_PRIM_LOCK_FACILITY_CNF",
	"CI_SIM_PRIM_GET_FACILITY_CAP_REQ",
	"CI_SIM_PRIM_GET_FACILITY_CAP_CNF",
	"CI_SIM_PRIM_GET_SIMAT_NOTIFY_CAP_REQ",
	"CI_SIM_PRIM_GET_SIMAT_NOTIFY_CAP_CNF",
	"CI_SIM_PRIM_GET_CALL_SETUP_ACK_IND",
	"CI_SIM_PRIM_GET_CALL_SETUP_ACK_RSP",
	/* Service Provider Name */
	"CI_SIM_PRIM_GET_SERVICE_PROVIDER_NAME_REQ",
	"CI_SIM_PRIM_GET_SERVICE_PROVIDER_NAME_CNF",
	/* Message Waiting Information */
	"CI_SIM_PRIM_GET_MESSAGE_WAITING_INFO_REQ",
	"CI_SIM_PRIM_GET_MESSAGE_WAITING_INFO_CNF",
	"CI_SIM_PRIM_SET_MESSAGE_WAITING_INFO_REQ",
	"CI_SIM_PRIM_SET_MESSAGE_WAITING_INFO_CNF",
	/* SIM Service Table */
	"CI_SIM_PRIM_GET_SIM_SERVICE_TABLE_REQ",
	"CI_SIM_PRIM_GET_SIM_SERVICE_TABLE_CNF",
	/* CPHS Customer Service Profile */
	"CI_SIM_PRIM_GET_CUSTOMER_SERVICE_PROFILE_REQ",
	"CI_SIM_PRIM_GET_CUSTOMER_SERVICE_PROFILE_CNF",
	/* Display Alpha and Icon Identifiers */
	"CI_SIM_PRIM_SIMAT_DISPLAY_INFO_IND",
	/* Default Language */
	"CI_SIM_PRIM_GET_DEFAULT_LANGUAGE_REQ",
	"CI_SIM_PRIM_GET_DEFAULT_LANGUAGE_CNF",
	/* Generic SIM commands */
	"CI_SIM_PRIM_GENERIC_CMD_REQ",
	"CI_SIM_PRIM_GENERIC_CMD_CNF",
	/* Indication of card type", status and PIN state */
	"CI_SIM_PRIM_CARD_IND",
	"CI_SIM_PRIM_IS_EMERGENCY_NUMBER_REQ",
	"CI_SIM_PRIM_IS_EMERGENCY_NUMBER_CNF",
	"CI_SIM_PRIM_SIM_OWNED_IND",
	"CI_SIM_PRIM_SIM_CHANGED_IND",
	"CI_SIM_PRIM_DEVICE_STATUS_REQ",
	"CI_SIM_PRIM_DEVICE_STATUS_CNF",
/**< VADIM SimLock */
	"CI_SIM_PRIM_READ_MEP_CODES_REQ",
	"CI_SIM_PRIM_READ_MEP_CODES_CNF",
	"CI_SIM_PRIM_UDP_LOCK_REQ",
	"CI_SIM_PRIM_UDP_LOCK_CNF",
	"CI_SIM_PRIM_UDP_CHANGE_PASSWORD_REQ",
	"CI_SIM_PRIM_UDP_CHANGE_PASSWORD_CNF",
	"CI_SIM_PRIM_UDP_ASL_REQ",
	"CI_SIM_PRIM_UDP_ASL_CNF",

/**< Michal Bukai - Virtual SIM support - START */
	"CI_SIM_PRIM_SET_VSIM_REQ",
	"CI_SIM_PRIM_SET_VSIM_CNF",
	"CI_SIM_PRIM_GET_VSIM_REQ",
	"CI_SIM_PRIM_GET_VSIM_CNF",
/**< Michal Bukai - Virtual SIM support - START */
/*Michal Bukai - OTA support for AT&T - START*/
	"CI_SIM_PRIM_CHECK_MMI_STATE_IND",
	"CI_SIM_PRIM_CHECK_MMI_STATE_RSP",
/*Michal Bukai - OTA support for AT&T - END*/
	/*Michal Bukai - BT SAP support - START */
	"CI_SIM_PRIM_BTSAP_CONNECT_REQ",
	"CI_SIM_PRIM_BTSAP_CONNECT_CNF",
	"CI_SIM_PRIM_BTSAP_DISCONNECT_REQ",
	"CI_SIM_PRIM_BTSAP_DISCONNECT_CNF",
	"CI_SIM_PRIM_BTSAP_TRANSFER_APDU_REQ",
	"CI_SIM_PRIM_BTSAP_TRANSFER_APDU_CNF",
	"CI_SIM_PRIM_BTSAP_TRANSFER_ATR_REQ",
	"CI_SIM_PRIM_BTSAP_TRANSFER_ATR_CNF",
	"CI_SIM_PRIM_BTSAP_SIM_CONTROL_REQ",
	"CI_SIM_PRIM_BTSAP_SIM_CONTROL_CNF",
	"CI_SIM_PRIM_BTSAP_STATUS_IND",
	"CI_SIM_PRIM_BTSAP_STATUS_REQ",
	"CI_SIM_PRIM_BTSAP_STATUS_CNF",
	"CI_SIM_PRIM_BTSAP_SET_TRANSPORT_PROTOCOL_REQ",
	"CI_SIM_PRIM_BTSAP_SET_TRANSPORT_PROTOCOL_CNF",
	/*Michal Bukai - BT SAP support - END */
	/*Michal Bukai - Add IMSI to MEP code group - START */
	"CI_SIM_PRIM_MEP_ADD_IMSI_REQ",
	"CI_SIM_PRIM_MEP_ADD_IMSI_CNF",
	/*Michal Bukai - SIM Logic CH - NFC\ISIM support - START */
	"CI_SIM_PRIM_OPEN_LOGICAL_CHANNEL_REQ",
	"CI_SIM_PRIM_OPEN_LOGICAL_CHANNEL_CNF",
	"CI_SIM_PRIM_CLOSE_LOGICAL_CHANNEL_REQ",
	"CI_SIM_PRIM_CLOSE_LOGICAL_CHANNEL_CNF",
	/*Michal Bukai - SIM Logic CH - NFC\ISIM support - END */
	/*Michal Bukai - additional SIMAT primitives - START */
	"CI_SIM_PRIM_SIMAT_CC_STATUS_IND",
	"CI_SIM_PRIM_SIMAT_SEND_CALL_SETUP_RSP_IND",
	"CI_SIM_PRIM_SIMAT_SEND_SS_USSD_RSP_IND",
	"CI_SIM_PRIM_SIMAT_SM_CONTROL_STATUS_IND",
	"CI_SIM_PRIM_SIMAT_SEND_SM_RSP_IND",
	/*Michal Bukai - additional SIMAT primitives - END */
	/*Michal Bukai - RSAP support - START */
	"CI_SIM_PRIM_RSAP_CONN_REQ_IND",
	"CI_SIM_PRIM_RSAP_CONN_REQ_RSP",
	"CI_SIM_PRIM_RSAP_STAT_REQ",
	"CI_SIM_PRIM_RSAP_STAT_CNF",
	"CI_SIM_PRIM_RSAP_DISCONN_REQ_IND",
	"CI_SIM_PRIM_RSAP_DISCONN_REQ_RSP",
	"CI_SIM_PRIM_RSAP_GET_ATR_IND",
	"CI_SIM_PRIM_RSAP_GET_ATR_RSP",
	"CI_SIM_PRIM_RSAP_GET_STATUS_REQ_IND",
	"CI_SIM_PRIM_RSAP_SET_TRAN_P_REQ_IND",
	"CI_SIM_PRIM_RSAP_SET_TRAN_P_REQ_RSP",
	"CI_SIM_PRIM_RSAP_SIM_CONTROL_REQ_IND",
	"CI_SIM_PRIM_RSAP_SIM_CONTROL_REQ_RSP",
	"CI_SIM_PRIM_RSAP_SIM_SELECT_REQ",
	"CI_SIM_PRIM_RSAP_SIM_SELECT_CNF",
	"CI_SIM_PRIM_RSAP_STATUS_IND",
	"CI_SIM_PRIM_RSAP_TRANSFER_APDU_IND",
	"CI_SIM_PRIM_RSAP_TRANSFER_APDU_RSP",
	/*Michal Bukai - RSAP support - END */
	"CI_SIM_PRIM_DEVICE_RSP",	/*Michal Bukai - USIM Clock stop level  support */
/*ICC ID feature */
	"CI_SIM_PRIM_ICCID_IND",
	"CI_SIM_PRIM_GET_ICCID_REQ",
	"CI_SIM_PRIM_GET_ICCID_CNF",
/*ICC ID feature */
	"CI_SIM_PRIM_EAP_AUTHENTICATION_REQ",
	"CI_SIM_PRIM_EAP_AUTHENTICATION_CNF",
	"CI_SIM_EAP_RETRIEVE_PARAMETERS_REQ",
	"CI_SIM_EAP_RETRIEVE_PARAMETERS_CNF",
	"CI_SIM_PRIM_GET_NUM_UICC_APPLICATIONS_REQ",
	"CI_SIM_PRIM_GET_NUM_UICC_APPLICATIONS_CNF",
	"CI_SIM_PRIM_GET_UICC_APPLICATIONS_INFO_REQ",
	"CI_SIM_PRIM_GET_UICC_APPLICATIONS_INFO_CNF",
/* ADD NEW COMMON PRIMITIVES HERE, BEFORE 'CI_SIM_PRIM_LAST_COMMON_PRIM' */
	"CI_SIM_PRIM_LAST_COMMON_PRIM"
	    /* END OF COMMON PRIMITIVES LIST */
};

static char *sSsMsgName[] = {
	"",
	"CI_SS_PRIM_GET_CLIP_STATUS_REQ",
	"CI_SS_PRIM_GET_CLIP_STATUS_CNF",
	"CI_SS_PRIM_SET_CLIP_OPTION_REQ",
	"CI_SS_PRIM_SET_CLIP_OPTION_CNF",
	"CI_SS_PRIM_GET_CLIR_STATUS_REQ",
	"CI_SS_PRIM_GET_CLIR_STATUS_CNF",
	"CI_SS_PRIM_SET_CLIR_OPTION_REQ",
	"CI_SS_PRIM_SET_CLIR_OPTION_CNF",
	"CI_SS_PRIM_GET_COLP_STATUS_REQ",
	"CI_SS_PRIM_GET_COLP_STATUS_CNF",
	"CI_SS_PRIM_SET_COLP_OPTION_REQ",
	"CI_SS_PRIM_SET_COLP_OPTION_CNF",
	"CI_SS_PRIM_GET_CUG_CONFIG_REQ",
	"CI_SS_PRIM_GET_CUG_CONFIG_CNF",
	"CI_SS_PRIM_SET_CUG_CONFIG_REQ",
	"CI_SS_PRIM_SET_CUG_CONFIG_CNF",
	"CI_SS_PRIM_GET_CNAP_STATUS_REQ",	/*Michal Bukai - CNAP support */
	"CI_SS_PRIM_GET_CNAP_STATUS_CNF",	/*Michal Bukai - CNAP support */
	"CI_SS_PRIM_SET_CNAP_OPTION_REQ",	/*Michal Bukai - CNAP support */
	"CI_SS_PRIM_SET_CNAP_OPTION_CNF",	/*Michal Bukai - CNAP support */
	"CI_SS_PRIM_REGISTER_CF_INFO_REQ",
	"CI_SS_PRIM_REGISTER_CF_INFO_CNF",
	"CI_SS_PRIM_ERASE_CF_INFO_REQ",
	"CI_SS_PRIM_ERASE_CF_INFO_CNF",
	"CI_SS_PRIM_GET_CF_REASONS_REQ",
	"CI_SS_PRIM_GET_CF_REASONS_CNF",
	"CI_SS_PRIM_SET_CF_ACTIVATION_REQ",
	"CI_SS_PRIM_SET_CF_ACTIVATION_CNF",
	"CI_SS_PRIM_INTERROGATE_CF_INFO_REQ",
	"CI_SS_PRIM_INTERROGATE_CF_INFO_CNF",
	"CI_SS_PRIM_SET_CW_ACTIVATION_REQ",
	"CI_SS_PRIM_SET_CW_ACTIVATION_CNF",
	"CI_SS_PRIM_GET_CW_OPTION_REQ",
	"CI_SS_PRIM_GET_CW_OPTION_CNF",
	"CI_SS_PRIM_SET_CW_OPTION_REQ",
	"CI_SS_PRIM_SET_CW_OPTION_CNF",
	"CI_SS_PRIM_GET_CW_ACTIVE_STATUS_REQ",
	"CI_SS_PRIM_GET_CW_ACTIVE_STATUS_CNF",
	"CI_SS_PRIM_GET_USSD_ENABLE_REQ",
	"CI_SS_PRIM_GET_USSD_ENABLE_CNF",
	"CI_SS_PRIM_SET_USSD_ENABLE_REQ",
	"CI_SS_PRIM_SET_USSD_ENABLE_CNF",
	"CI_SS_PRIM_RECEIVED_USSD_INFO_IND",
	"CI_SS_PRIM_RECEIVED_USSD_INFO_RSP",
	"CI_SS_PRIM_START_USSD_SESSION_REQ",
	"CI_SS_PRIM_START_USSD_SESSION_CNF",
	"CI_SS_PRIM_ABORT_USSD_SESSION_REQ",
	"CI_SS_PRIM_ABORT_USSD_SESSION_CNF",
	"CI_SS_PRIM_GET_CCM_OPTION_REQ",
	"CI_SS_PRIM_GET_CCM_OPTION_CNF",
	"CI_SS_PRIM_SET_CCM_OPTION_REQ",
	"CI_SS_PRIM_SET_CCM_OPTION_CNF",
	"CI_SS_PRIM_GET_AOC_WARNING_ENABLE_REQ",
	"CI_SS_PRIM_GET_AOC_WARNING_ENABLE_CNF",
	"CI_SS_PRIM_SET_AOC_WARNING_ENABLE_REQ",
	"CI_SS_PRIM_SET_AOC_WARNING_ENABLE_CNF",
	"CI_SS_PRIM_GET_SS_NOTIFY_OPTIONS_REQ",
	"CI_SS_PRIM_GET_SS_NOTIFY_OPTIONS_CNF",
	"CI_SS_PRIM_SET_SS_NOTIFY_OPTIONS_REQ",
	"CI_SS_PRIM_SET_SS_NOTIFY_OPTIONS_CNF",
	"CI_SS_PRIM_GET_LOCALCB_LOCKS_REQ",
	"CI_SS_PRIM_GET_LOCALCB_LOCKS_CNF",
	"CI_SS_PRIM_GET_LOCALCB_LOCK_ACTIVE_REQ",
	"CI_SS_PRIM_GET_LOCALCB_LOCK_ACTIVE_CNF",
	"CI_SS_PRIM_SET_LOCALCB_LOCK_ACTIVE_REQ",
	"CI_SS_PRIM_SET_LOCALCB_LOCK_ACTIVE_CNF",
	"CI_SS_PRIM_SET_LOCALCB_NOTIFY_OPTION_REQ",
	"CI_SS_PRIM_SET_LOCALCB_NOTIFY_OPTION_CNF",
	"CI_SS_PRIM_CHANGE_CB_PASSWORD_REQ",
	"CI_SS_PRIM_CHANGE_CB_PASSWORD_CNF",
	"CI_SS_PRIM_GET_CB_STATUS_REQ",
	"CI_SS_PRIM_GET_CB_STATUS_CNF",
	"CI_SS_PRIM_SET_CB_ACTIVATE_REQ",
	"CI_SS_PRIM_SET_CB_ACTIVATE_CNF",
	"CI_SS_PRIM_GET_CB_TYPES_REQ",
	"CI_SS_PRIM_GET_CB_TYPES_CNF",
	"CI_SS_PRIM_GET_BASIC_SVC_CLASSES_REQ",
	"CI_SS_PRIM_GET_BASIC_SVC_CLASSES_CNF",
	"CI_SS_PRIM_GET_ACTIVE_CW_CLASSES_REQ",
	"CI_SS_PRIM_GET_ACTIVE_CW_CLASSES_CNF",
	"CI_SS_PRIM_GET_CB_MAP_STATUS_REQ",
	"CI_SS_PRIM_GET_CB_MAP_STATUS_CNF",
#if defined(CCI_POSITION_LOCATION)
	"CI_SS_PRIM_PRIVACY_CTRL_REG_REQ",
	"CI_SS_PRIM_PRIVACY_CTRL_REG_CNF",
	"CI_SS_PRIM_LOCATION_IND",
	"CI_SS_PRIM_LOCATION_VERIFY_RSP",
	"CI_SS_PRIM_GET_LOCATION_REQ",
	"CI_SS_PRIM_GET_LOCATION_CNF",
	"CI_SS_PRIM_GET_LCS_NWSTATE_REQ",
	"CI_SS_PRIM_GET_LCS_NWSTATE_CNF",
	"CI_SS_PRIM_LCS_NWSTATE_CFG_IND_REQ",
	"CI_SS_PRIM_LCS_NWSTATE_CFG_IND_CNF",
	"CI_SS_PRIM_LCS_NWSTATE_IND",
#endif /* CCI_POSITION_LOCATION */
	"CI_SS_PRIM_SERVICE_REQUEST_COMPLETE_IND",
	"CI_SS_PRIM_GET_COLR_STATUS_REQ",
	"CI_SS_PRIM_GET_COLR_STATUS_CNF",
	"CI_SS_PRIM_GET_CDIP_STATUS_REQ",	/*Michal Bukai - CDIP support */
	"CI_SS_PRIM_GET_CDIP_STATUS_CNF",	/*Michal Bukai - CDIP support */
	"CI_SS_PRIM_SET_CDIP_OPTION_REQ",	/*Michal Bukai - CDIP support */
	"CI_SS_PRIM_SET_CDIP_OPTION_CNF",	/*Michal Bukai - CDIP support */
	/*Michal Bukai - IMS support - Start */
	"CI_SS_PRIM_SET_UUS1_REQ",
	"CI_SS_PRIM_SET_UUS1_CNF",
	/*Michal Bukai - IMS support - End */
	"CI_SS_PRIM_LAST_COMMON_PRIM"
	    /* ADD NEW COMMON PRIMITIVES HERE, BEFORE 'CI_SS_PRIM_LAST_COMMON_PRIM' */
	    /* END OF COMMON PRIMITIVES LIST */
};

static char *sMmMsgName[] = {
	"",
	"CI_MM_PRIM_GET_NUM_SUBSCRIBER_NUMBERS_REQ",
	"CI_MM_PRIM_GET_NUM_SUBSCRIBER_NUMBERS_CNF",
	"CI_MM_PRIM_GET_SUBSCRIBER_INFO_REQ",
	"CI_MM_PRIM_GET_SUBSCRIBER_INFO_CNF",
	"CI_MM_PRIM_GET_SUPPORTED_REGRESULT_OPTIONS_REQ",
	"CI_MM_PRIM_GET_SUPPORTED_REGRESULT_OPTIONS_CNF",
	"CI_MM_PRIM_GET_REGRESULT_OPTION_REQ",
	"CI_MM_PRIM_GET_REGRESULT_OPTION_CNF",
	"CI_MM_PRIM_SET_REGRESULT_OPTION_REQ",
	"CI_MM_PRIM_SET_REGRESULT_OPTION_CNF",
	"CI_MM_PRIM_REGRESULT_IND",
	"CI_MM_PRIM_GET_REGRESULT_INFO_REQ",
	"CI_MM_PRIM_GET_REGRESULT_INFO_CNF",
	"CI_MM_PRIM_GET_SUPPORTED_ID_FORMATS_REQ",
	"CI_MM_PRIM_GET_SUPPORTED_ID_FORMATS_CNF",
	"CI_MM_PRIM_GET_ID_FORMAT_REQ",
	"CI_MM_PRIM_GET_ID_FORMAT_CNF",
	"CI_MM_PRIM_SET_ID_FORMAT_REQ",
	"CI_MM_PRIM_SET_ID_FORMAT_CNF",
	"CI_MM_PRIM_GET_NUM_NETWORK_OPERATORS_REQ",
	"CI_MM_PRIM_GET_NUM_NETWORK_OPERATORS_CNF",
	"CI_MM_PRIM_GET_NETWORK_OPERATOR_INFO_REQ",
	"CI_MM_PRIM_GET_NETWORK_OPERATOR_INFO_CNF",
	"CI_MM_PRIM_GET_NUM_PREFERRED_OPERATORS_REQ",
	"CI_MM_PRIM_GET_NUM_PREFERRED_OPERATORS_CNF",
	"CI_MM_PRIM_GET_PREFERRED_OPERATOR_INFO_REQ",
	"CI_MM_PRIM_GET_PREFERRED_OPERATOR_INFO_CNF",
	"CI_MM_PRIM_ADD_PREFERRED_OPERATOR_REQ",
	"CI_MM_PRIM_ADD_PREFERRED_OPERATOR_CNF",
	"CI_MM_PRIM_DELETE_PREFERRED_OPERATOR_REQ",
	"CI_MM_PRIM_DELETE_PREFERRED_OPERATOR_CNF",
	"CI_MM_PRIM_GET_CURRENT_OPERATOR_INFO_REQ",
	"CI_MM_PRIM_GET_CURRENT_OPERATOR_INFO_CNF",
	"CI_MM_PRIM_AUTO_REGISTER_REQ",
	"CI_MM_PRIM_AUTO_REGISTER_CNF",
	"CI_MM_PRIM_MANUAL_REGISTER_REQ",
	"CI_MM_PRIM_MANUAL_REGISTER_CNF",
	"CI_MM_PRIM_DEREGISTER_REQ",
	"CI_MM_PRIM_DEREGISTER_CNF",
	"CI_MM_PRIM_GET_SIGQUALITY_IND_CONFIG_REQ",
	"CI_MM_PRIM_GET_SIGQUALITY_IND_CONFIG_CNF",
	"CI_MM_PRIM_SET_SIGQUALITY_IND_CONFIG_REQ",
	"CI_MM_PRIM_SET_SIGQUALITY_IND_CONFIG_CNF",
	"CI_MM_PRIM_SIGQUALITY_INFO_IND",
	"CI_MM_PRIM_ENABLE_NETWORK_MODE_IND_REQ",
	"CI_MM_PRIM_ENABLE_NETWORK_MODE_IND_CNF",
	"CI_MM_PRIM_NETWORK_MODE_IND",
	"CI_MM_PRIM_GET_NITZ_INFO_REQ",
	"CI_MM_PRIM_GET_NITZ_INFO_CNF",
	"CI_MM_PRIM_NITZ_INFO_IND",
	"CI_MM_PRIM_CIPHERING_STATUS_IND",
	"CI_MM_PRIM_AIR_INTERFACE_REJECT_CAUSE_IND",	/*Tal Porat/Michal Bukai - Network Selection */
	/* Michal Bukai - Selection of preferred PLMN list +CPLS - START */
	"CI_MM_PRIM_SELECT_PREFERRED_PLMN_LIST_REQ",
	"CI_MM_PRIM_SELECT_PREFERRED_PLMN_LIST_CNF",
	"CI_MM_PRIM_GET_PREFERRED_PLMN_LIST_REQ",
	"CI_MM_PRIM_GET_PREFERRED_PLMN_LIST_CNF",
	/* Michal Bukai - Selection of preferred PLMN list +CPLS - END */
	"CI_MM_PRIM_BANDIND_IND",
	"CI_MM_PRIM_SET_BANDIND_REQ",
	"CI_MM_PRIM_SET_BANDIND_CNF",
	"CI_MM_PRIM_GET_BANDIND_REQ",
	"CI_MM_PRIM_GET_BANDIND_CNF",
	"CI_MM_PRIM_SERVICE_RESTRICTIONS_IND",
	/*Michal Bukai - cancel PLMN search (Samsung)- Start */
	"CI_MM_PRIM_CANCEL_MANUAL_PLMN_SEARCH_REQ",
	"CI_MM_PRIM_CANCEL_MANUAL_PLMN_SEARCH_CNF",
	/*Michal Bukai - cancel PLMN search (Samsung)- End */
	/*Michal Bukai - dynamic release - Start */
	"CI_MM_PRIM_ENABLE_DYNAMIC_RELEASE_REQ",
	"CI_MM_PRIM_ENABLE_DYNAMIC_RELEASE_CNF",
	"CI_MM_PRIM_GET_DYNAMIC_RELEASE_STAT_REQ",
	"CI_MM_PRIM_GET_DYNAMIC_RELEASE_STAT_CNF",
	"CI_MM_PRIM_GET_DYNAMIC_RELEASE_CAPS_REQ",
	"CI_MM_PRIM_GET_DYNAMIC_RELEASE_CAPS_CNF",
	/*Michal Bukai - dynamic release - End */
	"CI_MM_PRIM_SEARCH_REQ",
	"CI_MM_PRIM_SEARCH_CNF",
	/*Michal Bukai - Cell Lock - Start */
	"CI_MM_PRIM_CELL_LOCK_REQ",
	"CI_MM_PRIM_CELL_LOCK_CNF",
	/*Michal Bukai - Cell Lock - End */
	/*Michal Buaki - HOME ZONE support */
	"CI_MM_PRIM_HOMEZONE_IND",
	/*Z.S. DSAC support */
	"CI_MM_PRIM_DSAC_STATUS_IND",
	"CI_MM_SET_SRVCC_SUPPORT_REQ",
	"CI_MM_SET_SRVCC_SUPPORT_CNF",
	"CI_MM_GET_SRVCC_SUPPORT_REQ",
	"CI_MM_GET_SRVCC_SUPPORT_CNF",
	"CI_MM_PRIM_GET_IMS_NW_REPORT_MODE_REQ",
	"CI_MM_PRIM_GET_IMS_NW_REPORT_MODE_CNF",
	"CI_MM_IMSVOPS_IND",
	"CI_MM_SRVCC_HANDOVER_IND",
	"CI_MM_SET_EMERGENCY_NUMBER_REPORT_MODE_REQ",
	"CI_MM_SET_EMERGENCY_NUMBER_REPORT_MODE_CNF",
	"CI_MM_GET_EMERGENCY_NUMBER_REPORT_REQ",
	"CI_MM_GET_EMERGENCY_NUMBER_REPORT_CNF",
	"CI_MM_EMERGENCY_NUMBER_REPORT_IND",
	"CI_MM_SET_NW_EMERGENCY_BEARER_SERVICES_REQ",
	"CI_MM_SET_NW_EMERGENCY_BEARER_SERVICES_CNF",
	"CI_MM_GET_NW_EMERGENCY_BEARER_SERVICES_REQ",
	"CI_MM_GET_NW_EMERGENCY_BEARER_SERVICES_CNF",
	"CI_MM_NW_EMERGENCY_BEARER_SERVICES_IU_IND",
	"CI_MM_NW_EMERGENCY_BEARER_SERVICES_S1_IND",
	"CI_MM_GET_SSAC_STATUS_REQ",
	"CI_MM_GET_SSAC_STATUS_CNF",
	"CI_MM_PRIM_GET_SIGQUALITY_INFO_REQ",
	"CI_MM_PRIM_GET_SIGQUALITY_INFO_CNF",
	"CI_MM_PRIM_CM_REJECT_CAUSE_IND",
	"CI_MM_PRIM_NETWORK_MODE_REQ",
	"CI_MM_PRIM_NETWORK_MODE_CNF",
	"CI_MM_PRIM_SET_POWER_UP_PLMN_MODE_REQ",
	"CI_MM_PRIM_SET_POWER_UP_PLMN_MODE_CNF",
	"CI_MM_PRIM_GET_POWER_UP_PLMN_MODE_REQ",
	"CI_MM_PRIM_GET_POWER_UP_PLMN_MODE_CNF",
	"CI_MM_PRIM_CSG_AUTO_SEARCH_REQ",
	"CI_MM_PRIM_CSG_AUTO_SEARCH_CNF",
	"CI_MM_PRIM_CSG_LIST_SEARCH_REQ",
	"CI_MM_PRIM_CSG_LIST_SEARCH_CNF",
	"CI_MM_PRIM_CSG_SELECT_REQ",
	"CI_MM_PRIM_CSG_SELECT_CNF",
	"CI_MM_PRIM_CSG_SEARCH_STOP_REQ",
	"CI_MM_PRIM_CSG_SEARCH_STOP_CNF",
	"CI_MM_PRIM_TRIGGER_USER_RESELECTION_REQ",
	"CI_MM_PRIM_TRIGGER_USER_RESELECTION_CNF",
	"CI_MM_PRIM_SET_IMS_REGISTRATION_STATUS_REQ",
	"CI_MM_PRIM_SET_IMS_REGISTRATION_STATUS_CNF",
	"CI_MM_PRIM_GET_DISPLAY_OPERATOR_NAME_REQ",
	"CI_MM_PRIM_GET_DISPLAY_OPERATOR_NAME_CNF",
	"CI_MM_PRIM_SET_GPRS_EGPRS_MULTISLOT_CLASS_REQ",
	"CI_MM_PRIM_SET_GPRS_EGPRS_MULTISLOT_CLASS_CNF",
	"CI_MM_PRIM_GET_GPRS_EGPRS_MULTISLOT_CLASS_REQ",
	"CI_MM_PRIM_GET_GPRS_EGPRS_MULTISLOT_CLASS_CNF",
	"CI_MM_PRIM_LAST_COMMON_PRIM"
	    /* ADD NEW COMMON PRIMITIVES HERE, BEFORE 'CI_MM_PRIM_LAST_COMMON_PRIM' */
	    /* END OF COMMON PRIMITIVES LIST */
};

static char *sDevMsgName[] = {
	"",
	"CI_DEV_PRIM_STATUS_IND",
	"CI_DEV_PRIM_GET_MANU_ID_REQ",
	"CI_DEV_PRIM_GET_MANU_ID_CNF",
	"CI_DEV_PRIM_GET_MODEL_ID_REQ",
	"CI_DEV_PRIM_GET_MODEL_ID_CNF",
	"CI_DEV_PRIM_GET_REVISION_ID_REQ",
	"CI_DEV_PRIM_GET_REVISION_ID_CNF",
	"CI_DEV_PRIM_GET_SERIALNUM_ID_REQ",
	"CI_DEV_PRIM_GET_SERIALNUM_ID_CNF",
	"CI_DEV_PRIM_SET_FUNC_REQ",
	"CI_DEV_PRIM_SET_FUNC_CNF",
	"CI_DEV_PRIM_GET_FUNC_REQ",
	"CI_DEV_PRIM_GET_FUNC_CNF",
	"CI_DEV_PRIM_GET_FUNC_CAP_REQ",
	"CI_DEV_PRIM_GET_FUNC_CAP_CNF",
	"CI_DEV_PRIM_SET_GSM_POWER_CLASS_REQ",
	"CI_DEV_PRIM_SET_GSM_POWER_CLASS_CNF",
	"CI_DEV_PRIM_GET_GSM_POWER_CLASS_REQ",
	"CI_DEV_PRIM_GET_GSM_POWER_CLASS_CNF",
	"CI_DEV_PRIM_GET_GSM_POWER_CLASS_CAP_REQ",
	"CI_DEV_PRIM_GET_GSM_POWER_CLASS_CAP_CNF",
	"CI_DEV_PRIM_PM_POWER_DOWN_REQ",
	"CI_DEV_PRIM_PM_POWER_DOWN_CNF",
	"CI_DEV_PRIM_SET_ENGMODE_REPORT_OPTION_REQ",
	"CI_DEV_PRIM_SET_ENGMODE_REPORT_OPTION_CNF",
	"CI_DEV_PRIM_GET_ENGMODE_INFO_REQ",
	"CI_DEV_PRIM_GET_ENGMODE_INFO_CNF",
	"CI_DEV_PRIM_ENGMODE_INFO_IND",
	"CI_DEV_PRIM_GSM_ENGMODE_INFO_IND",
	"CI_DEV_PRIM_UMTS_ENGMODE_SVCCELL_INFO_IND",
	"CI_DEV_PRIM_UMTS_ENGMODE_INTRAFREQ_INFO_IND",
	"CI_DEV_PRIM_UMTS_ENGMODE_INTERFREQ_INFO_IND",
	"CI_DEV_PRIM_UMTS_ENGMODE_INTERRAT_INFO_IND",
	"CI_DEV_PRIM_DO_SELF_TEST_REQ",
	"CI_DEV_PRIM_DO_SELF_TEST_CNF",
	"CI_DEV_PRIM_DO_SELF_TEST_IND",
	"CI_DEV_PRIM_SET_RFS_REQ",
	"CI_DEV_PRIM_SET_RFS_CNF",
	"CI_DEV_PRIM_GET_RFS_REQ",
	"CI_DEV_PRIM_GET_RFS_CNF",
	"CI_DEV_PRIM_UMTS_ENGMODE_ACTIVE_SET_INFO_IND",
	"CI_DEV_PRIM_ACTIVE_PDP_CONTEXT_ENGMODE_IND",
	"CI_DEV_PRIM_NETWORK_MONITOR_INFO_IND",
	"CI_DEV_PRIM_LP_NWUL_MSG_REQ",
	"CI_DEV_PRIM_LP_NWUL_MSG_CNF",
	"CI_DEV_PRIM_LP_NWDL_MSG_IND",
	"CI_DEV_PRIM_LP_RRC_STATE_IND",
	"CI_DEV_PRIM_LP_MEAS_TERMINATE_IND",
	"CI_DEV_PRIM_LP_RESET_STORED_UE_POS_IND",
	"CI_DEV_PRIM_COMM_ASSERT_REQ",
	"CI_DEV_PRIM_SET_BAND_MODE_REQ",
	"CI_DEV_PRIM_SET_BAND_MODE_CNF",
	"CI_DEV_PRIM_GET_BAND_MODE_REQ",
	"CI_DEV_PRIM_GET_BAND_MODE_CNF",
	"CI_DEV_PRIM_GET_SUPPORTED_BAND_MODE_REQ",
	"CI_DEV_PRIM_GET_SUPPORTED_BAND_MODE_CNF",
	"CI_DEV_PRIM_SET_SV_REQ",
	"CI_DEV_PRIM_SET_SV_CNF",
	"CI_DEV_PRIM_GET_SV_REQ",
	"CI_DEV_PRIM_GET_SV_CNF",
	"CI_DEV_PRIM_SET_SECURITY_PARAMS_REQ",
	"CI_DEV_PRIM_SET_SECURITY_PARAMS_CNF",
	"CI_DEV_PRIM_GET_SECURITY_PARAMS_REQ",
	"CI_DEV_PRIM_GET_SECURITY_PARAMS_CNF",
	"CI_DEV_PRIM_RESET_REQUEST_IND",
	"CI_DEV_PRIM_SET_USER_TEST_REPORT_OPTION_REQ",
	"CI_DEV_PRIM_SET_USER_TEST_REPORT_OPTION_CNF",
	"CI_DEV_PRIM_USER_TEST_VALUABLE_EVENT_REPORT_IND",
	"CI_DEV_PRIM_SET_PARK_MODE_REQ",
	"CI_DEV_PRIM_SET_PARK_MODE_CNF",
	"CI_DEV_PRIM_GET_PARK_MODE_REQ",
	"CI_DEV_PRIM_GET_PARK_MODE_CNF",
	"CI_DEV_PRIM_LP_ECID_MEAS_REQ",
	"CI_DEV_PRIM_LP_ECID_MEAS_CNF",
	"CI_DEV_PRIM_SET_LP_UE_AREA_INFO_IND_REQ",
	"CI_DEV_PRIM_SET_LP_UE_AREA_INFO_IND_CNF",
	"CI_DEV_PRIM_LP_UE_AREA_INFO_IND",
	"CI_DEV_PRIM_SET_IMS_MEDIA_REQ",
	"CI_DEV_PRIM_SET_IMS_MEDIA_CNF",
	"CI_DEV_PRIM_IMS_MEDIA_IND",
	"CI_DEV_PRIM_COMMON_ENGMODE_INFO_IND",
	"CI_DEV_PRIM_SET_COM_CONFIG_REQ",
	"CI_DEV_PRIM_SET_COM_CONFIG_CNF",
	"CI_DEV_PRIM_GET_COM_CONFIG_REQ",
	"CI_DEV_PRIM_GET_COM_CONFIG_CNF" "CI_DEV_PRIM_LAST_COMMON_PRIM"
};

static char *sErrPrim[] = {
	"CI_ERR_PRIM_HASNOSUPPORT_CNF",
	"CI_ERR_PRIM_HASINVALIDPARAS_CNF",
	"CI_ERR_PRIM_ISINVALIDREQUEST_CNF",
	"CI_ERR_PRIM_SIMNOTREADY_CNF",
	"CI_ERR_PRIM_ACCESSDENIED_CNF",
	"CI_ERR_PRIM_INTERLINKDOWN_IND",
	"CI_ERR_PRIM_INTERLINKDOWN_RSP",
	"CI_ERR_PRIM_INTERLINKUP_IND",
	/* This should always be the last enum entry */
	"CI_ERR_PRIM_NEXTAVAIL"
};

#ifdef CI_DATA_DUMP

#error "Why did you compile this?"

void ciDumpMsgInfo(unsigned char srvGrpId, unsigned short primId, unsigned char *data, unsigned int dataSize)
{
	unsigned char *pCurr = data;
	unsigned int i, reqHandle, refId;

	if (srvGrpId == 0 || srvGrpId >= CI_SG_ID_NEXTAVAIL) {
		return;
	}

	printf("\n********CI DUMP MSG BEGIN**********\n");
	printf("ServiceGroup: %s(%d)\n", sSrvGroupName[srvGrpId], srvGrpId);

	if (primId >= CI_ERR_PRIM_NEXTAVAIL) {
		printf("Invalid Primitive: 0x%x\n", primId);
		printf("********CI DUMP MSG END**********\n");
		return;
	} else if (primId >= CI_ERR_PRIM_HASNOSUPPORT_CNF && primId < CI_ERR_PRIM_NEXTAVAIL) {
		printf("Primitive: %s(0x%x)\n", sErrPrim[primId - CI_ERR_PRIM_HASNOSUPPORT_CNF], primId);
	} else {
		switch (srvGrpId) {
		case CI_SG_ID_CC:
			printf("Primitive: %s(%d)\n", sCsMsgName[primId], primId);
			break;
		case CI_SG_ID_SS:
			printf("Primitive: %s(%d)\n", sSsMsgName[primId], primId);
			break;
		case CI_SG_ID_SIM:
			printf("Primitive: %s(%d)\n", sSimMsgName[primId], primId);
			break;
		case CI_SG_ID_MSG:
			printf("Primitive: %s(%d)\n", sSmsMsgName[primId], primId);
			break;
		case CI_SG_ID_PS:
			printf("Primitive: %s(%d)\n", sPsMsgName[primId], primId);
			break;
		case CI_SG_ID_PB:
			printf("Primitive: %s(%d)\n", sPbMsgName[primId], primId);
			break;
		case CI_SG_ID_MM:
			printf("Primitive: %s(%d)\n", sMmMsgName[primId], primId);
			break;
		default:
			printf("Primitive: (%d)\n", primId);
		}
	}
	memcpy(&reqHandle, pCurr, 4);
	printf("RequestHandle: 0x%x\n", reqHandle);
	pCurr += 4;
	memcpy(&refId, pCurr, 4);
	printf("RefID: 0x%x\n", refId);
	pCurr += 4;
	if (dataSize > 8) {
		printf("Data: ");
		for (i = 0; i < dataSize - 8; i++)
			printf("%02x", *pCurr++);
		printf("\n");
	}

	printf("********CI DUMP MSG END  **********\n");

}
#endif /* CI_DATA_DUMP */

void ciDumpMsgInfoToBuffer(unsigned char srvGrpId, unsigned short primId, unsigned char *data, unsigned int dataSize,
			   char *buffer)
{

	char *ptr = buffer;

	if (srvGrpId == 0 || srvGrpId >= CI_SG_ID_NEXTAVAIL) {
		sprintf(buffer, "Invalid ServiceGroupId %d ", srvGrpId);
		return;
	}

	if (primId >= CI_ERR_PRIM_NEXTAVAIL) {
		sprintf(buffer, "Invalid Primitive: 0x%x ", primId);
		return;
	} else if (primId >= CI_ERR_PRIM_HASNOSUPPORT_CNF && primId < CI_ERR_PRIM_NEXTAVAIL) {
		ptr += sprintf(buffer, "Primitive: %s(0x%x) ", sErrPrim[primId - CI_ERR_PRIM_HASNOSUPPORT_CNF], primId);
	} else {
		switch (srvGrpId) {
		case CI_SG_ID_CC:
			if (primId < sizeof(sCsMsgName) / sizeof(sCsMsgName[0]))
				ptr += sprintf(buffer, "Primitive: %s(%d) ", sCsMsgName[primId], primId);
			break;
		case CI_SG_ID_SS:
			if (primId < sizeof(sSsMsgName) / sizeof(sSsMsgName[0]))
				ptr += sprintf(buffer, "Primitive: %s(%d) ", sSsMsgName[primId], primId);
			break;
		case CI_SG_ID_SIM:
			if (primId < sizeof(sSimMsgName) / sizeof(sSimMsgName[0]))
				ptr += sprintf(buffer, "Primitive: %s(%d) ", sSimMsgName[primId], primId);
			break;
		case CI_SG_ID_MSG:
			if (primId < sizeof(sSmsMsgName) / sizeof(sSmsMsgName[0]))
				ptr += sprintf(buffer, "Primitive: %s(%d) ", sSmsMsgName[primId], primId);
			break;
		case CI_SG_ID_PS:
			if (primId < sizeof(sPsMsgName) / sizeof(sPsMsgName[0]))
				ptr += sprintf(buffer, "Primitive: %s(%d) ", sPsMsgName[primId], primId);
			break;
		case CI_SG_ID_PB:
			if (primId < sizeof(sPbMsgName) / sizeof(sPbMsgName[0]))
				ptr += sprintf(buffer, "Primitive: %s(%d) ", sPbMsgName[primId], primId);
			break;
		case CI_SG_ID_MM:
			if (primId < sizeof(sMmMsgName) / sizeof(sMmMsgName[0]))
				ptr += sprintf(buffer, "Primitive: %s(%d) ", sMmMsgName[primId], primId);
			break;
		case CI_SG_ID_DEV:
			if (primId < sizeof(sDevMsgName) / sizeof(sDevMsgName[0]))
				ptr += sprintf(buffer, "Primitive: %s(%d) ", sDevMsgName[primId], primId);
			break;

		default:
			ptr += sprintf(buffer, "Primitive: (%d) ", primId);
		}
	}

	if (ptr == buffer) {
		ptr += sprintf(buffer, " undescripted Primitive: (%d) ", primId);
	}
	if ((dataSize != 0) && (data != NULL)) {
		unsigned int i;
		for (i = 0; i < dataSize; i++) {
			ptr += sprintf(ptr, "%02X ", (unsigned char)data[i]);
		}

	}

}

/* Ido: comment for MTIL CI trace */
/*#endif / *CI_DATA_DUMP* / */

#endif /* ENV_LINUX */
