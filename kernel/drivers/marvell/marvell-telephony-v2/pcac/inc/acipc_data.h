/*
 *
 * (C) Copyright 2006-2011 Marvell International Ltd.
 * All Rights Reserved
 */

/*                                                                      */
/*                                                                      */
/*  This file and the software in it is furnished under                 */
/*  license and may only be used or copied in accordance with the terms */
/*  of the license. The information in this file is furnished for       */
/*  informational use only, is subject to change without notice, and    */
/*  should not be construed as a commitment by Intel Corporation.       */
/*  Intel Corporation assumes no responsibility or liability for any    */
/*  errors or inaccuracies that may appear in this document or any      */
/*  software that may be provided in association with this document.    */
/*  Except as permitted by such license, no part of this document may   */
/*  be reproduced, stored in a retrieval system, or transmitted in any  */
/*  form or by any means without the express written consent of Marvell */
/*  Corporation.                                                        */
/*                                                                      */
/* Title: Application Communication IPC Driver                          */
/*                                                                      */
/* Filename: acipc_data.h                                               */
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
#if !(defined _ACIPC_DATA_H_)
#define _ACIPC_DATA_H_

#ifdef EXTERN
#undef EXTERN
#endif

#define EXTERN extern

/*----------- Global defines -------------------------------------------------*/

#if (defined OSA_WINCE)
#include    "ComMemMapping.h"
#elif defined(ACI_LNX_KERNEL)
#include    "com_mem_mapping_kernel.h"
#elif defined(ENV_LINUX)
#include    "com_mem_mapping_user.h"
#endif

#if defined(ENV_LINUX)

#define     ACIPCD_APPADDR_TO_COMADDR(aDDRESS)  MAP_VIRTUAL_TO_PHYSICAL_ADDRESS(aDDRESS)
#define     ACIPCD_COMADDR_TO_APPADDR(aDDR)     ConvertPhysicalToVirtualAddr((unsigned int)(aDDR))
#define     ACIPCD_NVM_COMADDR_TO_APPADDR(aDDR) MAP_PHYSICAL_TO_VIRTUAL_ADDRESS_NVM(aDDR)

#endif

/*----------------------------------------------------------------------------------------------- */
/*
 * BS - the following section shall be removed after apcp_mmap.h changes will be taken
 * to main branch
 */
#if defined (ADDR_CONVERT_MSA_TO_AP)
#undef  ADDR_CONVERT_MSA_TO_AP
#undef  ADDR_CONVERT_AP_TO_MSA
#endif

#if !defined (ADDR_CONVERT_MSA_TO_AP)
#define ADDR_CONVERT_MSA_TO_AP(aDDR)    ((((unsigned int)(aDDR) & 0xF0000000) == 0xD0000000) ? \
										((unsigned int)(aDDR) & 0x4FFFFFFF) : (unsigned int)(aDDR))

#define ADDR_CONVERT_AP_TO_MSA(aDDR)    ((((unsigned int)(aDDR) & 0xF0000000) == 0x40000000) ? \
										 ((unsigned int)(aDDR) | 0xD0000000) : (unsigned int)(aDDR))
#endif
/*----------------------------------------------------------------------------------------------- */

#define     ACIPCD_MAX_RPC      32	/*  Can't exceed 32 */

/*ICAT EXPORTED ENUM */
typedef enum {
	ACIPCD_RESERVED = 0,
	ACIPCD_INTERNAL = ACIPCD_RESERVED,

	ACIPCD_CI_DATA_UPLINK,	/*1 */
	ACIPCD_CI_DATA_DOWNLINK,	/*2 */
	ACIPCD_DIAG_DATA,	/*3 */
	ACIPCD_AUDIO_DATA,	/*4 */
	ACIPCD_CI_CTRL,		/*5 */
	ACIPCD_CI_DATA_CONTROL,	/*6 */
	ACIPCD_DIAG_CONTROL,	/*7 */
	ACIPCD_AUDIO_COTNROL,	/*8 */
	ACIPCD_NVM_RPC,		/*9 */
	ACIPCD_RTC,		/*10 */
	ACIPCD_GEN_RPC,		/*11 */
	ACIPCD_GPC,		/*12 */
	ACIPCD_USBMGR_TUNNEL,	/*13 */
	ACIPCD_CI_DATA_REQ_CNF,	/*14 - shall be removed */
	ACIPCD_CI_DATA_IND_RSP,	/*15 - shall be removed */
	ACIPCD_CI_DATA_CONTROL_OLD,	/*16 - shall be removed */
	ACIPCD_AGPS,		/*17 - shall be removed */
	ACIPCD_AUDIO_VCM_CTRL,	/*18 - for compilation only */
	ACIPCD_AUDIO_VCM_DATA,	/*19 - for compilation only */

	ACIPCD_RESERVE_1,	/*  Can add more ... */
	ACIPCD_SG_LAST_SERVICE_ID,

/* Start MSA Services here */
	ACIPCD_MSA_INTERNAL = 32,	/*32 - this should be first for MSA services */

	ACIPCD_DIAG_DATA_MSA = 33,
	ACIPCD_DIAG_CONTROL_MSA = 34,

	ACIPCD_TEST = 35,	/*35 - this should be last, it's for internal testing */
	ACIPCD_MSA_LAST_SERVICE_ID,
	ACIPCD_LAST_SERVICE_ID = ACIPCD_MSA_LAST_SERVICE_ID
} ACIPCD_ServiceIdE;

/*
 * ACIPCD_TxCnfActionE
 * Request SHMEM driver to free service memory on Tx Confirm
 */

/*ICAT EXPORTED ENUM */
typedef enum {
	ACIPCD_TXCNF_DO_NOTHING,
	ACIPCD_TXCNF_FREE_MEM
} ACIPCD_TxCnfActionE;

/*ICAT EXPORTED ENUM */
typedef enum {
	ACIPCD_DO_NOTHING,	/*  No action is required before send or after receive. */
	ACIPCD_HANDLE_CACHE,	/*  Tx: Clean cache before sending / Rx: Invalidate cache before receive. */
	ACIPCD_COPY,		/*  Tx: Copy to shared memory before sending / Rx: Currently not supported. */
	ACIPCD_USE_ADDR_AS_PARAM,	/*do not translate COMM addr to APPS addr and vice versa */
	ACIPCD_LAST = 0x7fffffff
} ACIPCD_ActionE;

typedef enum {
	ACIPCD_WATERMARK_LOW = 0,
	ACIPCD_WATERMARK_HIGH
} ACIPCD_WatermarkTypeE;

typedef enum {
	ACIPCD_RC_OK = 0,
	ACIPCD_RC_HIGH_WM,	/*  The message WAS added to the queue. */
	ACIPCD_RC_LINK_DOWN,	/*  The message was NOT added to the queue. */
	ACIPCD_RC_Q_FULL,	/*  The message was NOT added to the queue. */
	ACIPCD_RC_ILLEGAL_PARAM,
	ACIPCD_RC_NO_MEMORY
} ACIPCD_ReturnCodeE;

/*-----------------8/6/2009 10:33AM-----------------
 * for debug log
 * --------------------------------------------------*/
/*ICAT EXPORTED ENUM */
typedef enum {
	SHMEM_DBG_EVENT_EMPTY = 0,

	/*ACIPCD events */
	MDB_ACIPCDRegister,	/*1 */
	MDB_ACIPCDTxReq,	/*2 */
	MDB_ACIPCDRxDoneRsp,	/*3 */
	MDB_ACIPCDSend,		/*4 */
	MDB_ACIPCDRxIndEvent,	/*5 */
	MDB_ACIPCDCheckForRxInd,	/*6 */
	MDB_ACIPCDRxInd,	/*7 */
	MDB_ACIPCDHandleWatermarks,	/*8 */
	MDB_ACIPCDQueueAdrsIndEvent,	/*9 */
	MDB_ACIPCDQueueAdrsInd,	/*10 */
	MDB_ACIPCDDummyRxInd,	/*11 */
	MDB_ACIPCDDummyTxDoneCnf,	/*12 */
	MDB_ACIPCDDummyTxFailCnf,	/*13 */
	MDB_ACIPCDLinkStatus,	/*14 */
	MDB_ACIPCDRpcEnded,	/*15 */

	MDB_CI_DataRxInd_IND_RSP,	/*16 */
	MDB_CI_DataClientIndicationeCallback_IND_RSP,	/*17 */
	MDB_CI_DataSendRxDoneRsp_IND_RSP,	/*18 */
	MDB_CI_DataWatermarkInd_IND_RSP,	/*19 */
	MDB_CI_DataLinkStatusInd_IND_RSP,	/*20 */

	MDB_sendDataReqtoInternalList,	/*21 */
	MDB_addBufferToInternalList,	/*22 */
	MDB_CI_DataSendTask_REQ_CNF,	/*23 */
	MDB_CI_DataSendReqPrim_REQ_CNF,	/*24 */
	MDB_CI_DataSendPacket_REQ_CNF,	/*25 */
	MDB_CI_DataTxDoneInd_REQ_CNF,	/*26 */
	MDB_CI_DataRxInd_REQ_CNF,	/*27 */
	MDB_CI_DataSendRxDoneRsp_REQ_CNF,	/*28 */
	MDB_CI_DataWatermarkInd_REQ_CNF,	/*29 */
	MDB_CI_DataLinkStatusInd_REQ_CNF,	/*30 */
	MDB_CI_DataReqMaxLimitSet_REQ_CNF,	/*31 */

	MDB_callback_RxInd_n = 50,	/*50 */

	MDB_ACIPCDAddToAllocList,
	MDB_ACIPCDRemoveFromAllocList,
	MDB_ACIPCDCleanupSharedMemory,

	MDB_WdtEvent = 0x7FFFFFFE,

	/*Must be Last Event */
	MDB_ShmemDebugLastEntry = 0x7FFFFFFF
} SHMEM_EventTypeE;

/* (*LinkStatusIndCBext)(UINT32-bit-parameters extention)
 * Down/Up with additional info passed over UINT32 bits
 */
#define ACIPCD_LINK_STATUS_PARAM_NO       0x00000000
#define ACIPCD_LINK_STATUS_PARAM_USED       (1<<0)
#define ACIPCD_LINK_STATUS_STAGE1_ONLY      (1<<1)
#define ACIPCD_LINK_STATUS_NOTIFY_ONLY    ACIPCD_LINK_STATUS_STAGE1_ONLY
#define ACIPCD_LINK_STATUS_FULL_EXEC_REQ    (1<<2)

/*
 * All Call Back Functions are called under a HISR
 */
typedef struct {
/*Pay attention to Keep consistency with SHMEMCallbackTypesE */
	void (*RxIndCB) (void *, unsigned int);	/*  Data indication (Pointer to the data & its length). */
	void (*LinkStatusIndCB) (unsigned char);	/*  Link status (TRUE=Up, FALSE=Down). */
	void (*LinkStatusIndCBext) (unsigned char, unsigned int); /*LinkStatus(TRUE/FALSE=Up/Down, UINT32-bit-parameters).*/
#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
	void (*WatermarkIndCB) (ACIPCD_WatermarkTypeE);	/*  Watermark (high/low) indication */
#else				/*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
	void (*LowWmIndCB) (void);	/*  Low water mark indication - can restart sending. */
#endif				/*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
	void (*TxDoneCnfCB) (void *);	/*  Tx complete (Pointer to buffer) - can free the buffer. */
	void (*TxFailCnfCB) (void *);	/*  Tx fail (Pointer to buffer) - can free the buffer. */
} ACIPCD_CallBackFuncS;

typedef struct {
	unsigned int RpcTimout;	/*  0 - Not RPC, Else timout according to OSA. */
	unsigned char HighWm;	/*  How many messages we allow other side to send befor he receives our TxComplete. */
	unsigned char LowWm;	/*  Once he stoped sending, he will restart when Send-Ack <= LowWm */
	ACIPCD_ActionE TxAction;	/*  Action to take before sending. */
	ACIPCD_ActionE RxAction;	/*  Action to take before calling the RxIndCB callback function. */
	unsigned char BlockOnRegister;	/*  TRUE - ACIPCD will block until the link on the other side is up. */
	ACIPCD_TxCnfActionE TxCnfAction;	/*  DO_NOTHING / MEM_FREE_ON_TX_CNF */
} ACIPCD_ConfigS;

/*----------- API prototype -------------------------------------------------*/
#ifdef FN
#undef FN
#endif

#if (defined OSA_WINCE)  &&  (!defined SHMEM_MODULE)
#define     ACIPCDCheckForRxInd     ACIPCDCheckForRxIndShell
#define     ACIPCDLinkStatus        ACIPCDLinkStatusShell
#define     ACIPCDPhase1Init        ACIPCDPhase1InitShell
#define     ACIPCDPhase2Init        ACIPCDPhase2InitShell
#define     ACIPCDRegister          ACIPCDRegisterShell
#define     ACIPCDTxReq             ACIPCDTxReqShell
#define     ACIPCDRxDoneRsp         ACIPCDRxDoneRspShell
#endif

EXTERN void ACIPCDPhase1Init(void);
EXTERN void ACIPCDPhase2Init(void);

EXTERN ACIPCD_ReturnCodeE ACIPCDRegister(ACIPCD_ServiceIdE, ACIPCD_CallBackFuncS*, ACIPCD_ConfigS*);

EXTERN unsigned char ACIPCDLinkStatus(ACIPCD_ServiceIdE);	/*  TRUE=Up, FALSE=Down. */
EXTERN void ACIPCDCheckForRxInd(void);	/*  This is external only if want to work in polling. */

EXTERN ACIPCD_ReturnCodeE ACIPCDTxReq(ACIPCD_ServiceIdE, void *, unsigned short);	/*  ServiceId, Data, Length. */
EXTERN ACIPCD_ReturnCodeE ACIPCDRxDoneRsp(ACIPCD_ServiceIdE, void *);	/*  ServiceId, Data */
EXTERN ACIPCD_ReturnCodeE ACIPCDTxReqACIPC(ACIPCD_ServiceIdE, void *, unsigned short, unsigned char dontMakeACIPCirq);
EXTERN ACIPCD_ReturnCodeE ACIPCDRxDoneRspACIPC(ACIPCD_ServiceIdE, void *, unsigned char dontMakeACIPCirq);

EXTERN void ACIPCDComIfDownIndicate(unsigned int realExecReq);
EXTERN void ACIPCDComIfReset(unsigned int realExecReq);
EXTERN void ACIPCDComRecoveryIndicate(void);

#ifdef __cplusplus
}
#endif

#undef  EXTERN

#endif /* _ACIPC_DATA_H_ */
