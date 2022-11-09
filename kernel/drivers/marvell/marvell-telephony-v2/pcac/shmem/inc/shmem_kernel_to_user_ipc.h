/*
 * (C) Copyright 2006-2011 Marvell International Ltd.
 * All Rights Reserved
 */

/* ===========================================================================
File        : shmem_kernel_to_user_ipc.h
=========================================================================== */
#ifndef _SHMEM_KERNEL_TO_USER_IPC_H_
#define _SHMEM_KERNEL_TO_USER_IPC_H_

#include "acipc.h"
#include "acipc_data.h"

/************************************************************************
 * External Interfaces
 ***********************************************************************/

/*************************************************************************
 * Constants
 *************************************************************************/
#define SHMEM_CALLBACK_PAYLOAD	64	/*idaviden must be changed to sizeof of max data structure*/
#define NETLINK_CALLBACK		17

/*************************************************************************
 * Specific Types
 *************************************************************************/

typedef enum {
/*Pay attention to Keep consistency with ACIPCD_CallBackFuncS*/
	RxInd = 0,
	LinkStatusInd,
	LinkStatusIndExt,
#if defined(ACIPC_ENABLE_NEW_CALLBACK_MECHANISM)
	WatermarkInd,
#else /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
	LowWmInd,
#endif /*ACIPC_ENABLE_NEW_CALLBACK_MECHANISM */
	TxDoneCnf,
	TxFailCnf,
	LAST_CB_TYPE
} SHMEMCallbackTypesE;

#define SHMEM_PrintUserLogCB            0x100
#define SHMEM_SaveUserLogCB				0x101

typedef struct {
	void *pData;
	unsigned int len;
} SHMEMRxIndS;

typedef struct {
	unsigned char LinkStatus;
	void *pData;
} SHMEMLinkStatusS;

typedef struct {
	void *pData;
} SHMEMConfiramtionS;

typedef struct {
	ACIPCD_WatermarkTypeE type;
} SHMEMWatermarkTypeS;

typedef union _mslCallbackContext {
	SHMEMRxIndS rxIndParms;
	SHMEMLinkStatusS linkStatusParms;
	SHMEMConfiramtionS confParms;
	SHMEMWatermarkTypeS watermarkTypeParms;
} SHMEMCallbackContext, *PSHMEMCallbackContext;

typedef struct {
	unsigned int serviceId;
	unsigned int functionId;
	SHMEMCallbackContext context;
} SHMEMCallbackFunctionContext;

extern int getKernelCBFromUser(ACIPCD_ServiceIdE ServiceId, ACIPCD_CallBackFuncS *cbKernel, pid_t pid);

#endif /* _SHMEM_KERNEL_TO_USER_IPC_H_ */
