/*
 * (C) Copyright 2006-2011 Marvell International Ltd.
 * All Rights Reserved
 */

/* ===========================================================================
File        : 	shmem_lnx_kmod.h
Description : 	Definition of OSA Software Layer data types specific to the
				linux kernel module(s) programing.

Notes       :
=========================================================================== */
#ifndef _SHMEM_LINUX_KERNEL_MODULE_H_
#define _SHMEM_LINUX_KERNEL_MODULE_H_

#include "shmem_lnx_kmod_api.h"
#include "acipc.h"
#include "acipc_data.h"


/************************************************************************
 * External Interfaces
 ***********************************************************************/

/*************************************************************************
 * Constants
 *************************************************************************/

typedef struct {
	unsigned int g_CommRWaddr;
	unsigned int uiCommRWAreaSizeInBytes;
	unsigned int uiCommROArea;
	unsigned int uiCommROAreaSizeInBytes;

	unsigned int uiCommPoolPhyBegin;
	unsigned int uiAppPoolPhyBegin;
	/* COM physical start Address used for
	   Aduio dedicated SHMEM buffer */
	unsigned int uiCommRWArea;
} SHMEMPoolGeometry;

/* user level ioctl commands for accessing APIs */
/* from User2Kerenl calls */

#define SHMEM_IOC_MAGIC					'S'
#define SHMEM_IOCTL_POOL_GEOMETRY			_IOW(SHMEM_IOC_MAGIC, 1, int)
#define SHMEM_IOCTL_REGISTER				_IOW(SHMEM_IOC_MAGIC, 2, int)
#define SHMEM_IOCTL_RX_DONE_RSP				_IOW(SHMEM_IOC_MAGIC, 3, int)
#define SHMEM_IOCTL_SEND_REQ				_IOW(SHMEM_IOC_MAGIC, 4, int)
#define SHMEM_IOCTL_ALLOC_EVENT				_IOW(SHMEM_IOC_MAGIC, 5, int)
#define SHMEM_IOCTL_FREE_EVENT				_IOW(SHMEM_IOC_MAGIC, 6, int)
#define SHMEM_IOCTL_KERENL_DATA_CB_FN		_IOW(SHMEM_IOC_MAGIC, 7, int)
#define SHMEM_IOCTL_DEBUG_GET_THREAD_ID		_IOW(SHMEM_IOC_MAGIC, 8, int)
#define SHMEM_IOCTL_LINK_DOWN				_IOW(SHMEM_IOC_MAGIC, 9, int)
#define SHMEM_IOCTL_LINK_UP					_IOW(SHMEM_IOC_MAGIC, 10, int)
#define SHMEM_IOCTL_DEBUG_GET_DBGLOG_SIZE	_IOW(SHMEM_IOC_MAGIC, 11, int)
#define SHMEM_IOCTL_DEBUG_GET_DBGLOG_INFO	_IOW(SHMEM_IOC_MAGIC, 12, int)
#define SHMEM_IOCTL_GET_LOG_SIZE			_IOW(SHMEM_IOC_MAGIC, 13, int)
#define SHMEM_IOCTL_GET_LOG					_IOW(SHMEM_IOC_MAGIC, 14, int)
#define SHMEM_IOCTL_WDT_EVENT				_IOW(SHMEM_IOC_MAGIC, 15, int)

#define GEN_RPC_TX_REQ						_IOW(SHMEM_IOC_MAGIC, 16, int)

#define SHMEM_IOCTL_SET_PENDING_BUFFERS_PTR _IOW(SHMEM_IOC_MAGIC, 20, int)

/*************************************************************************
 * Specific Types
 *************************************************************************/
typedef struct {
	unsigned int status;
	void *params;
} SHMEMApiParams;

typedef struct {
	ACIPCD_ServiceIdE ServiceId;
	ACIPCD_CallBackFuncS cbFunc;
	ACIPCD_ConfigS config;
	unsigned int pid;
} SHMEMRegiter;

typedef struct {
	ACIPCD_ServiceIdE ServiceId;
	void *pData;
	unsigned char dontMakeACIPCirq;
} SHMEMRxDoneResp;

typedef struct {
	ACIPCD_ServiceIdE ServiceId;
	void *pData;
	short Length;
	unsigned int pid;
	unsigned int /*unsigned char */ dontMakeACIPCirq;
	acipc_dev_type acipc_type;
} SHMEMSendReq;

typedef struct {
	unsigned int uiSize;
	unsigned int uiOffset;
} SHMEMMalloc;

typedef struct {
	unsigned int uiOffset;
} SHMEMFree;

typedef struct {
	ACIPCD_CallBackFuncS ciDataCbFunctions;
	unsigned int callbackType;	/*type=0 for REQ_ONF, type=1 for IND_RSP*/
} SHMEMDataCb;


/************************************************************************
 * External Interfaces
 ***********************************************************************/
extern void GetCallbacksKernelFn(ACIPCD_CallBackFuncS *pCallBackFunc, unsigned int callbackType);
extern void DataChannelKernelPrintInfo(void);
extern void ACIPCDPrintDatabase(void);
extern void ACIPCDPrintShmemDebugLog(void);
extern void shmem_print_log_callback(void);
extern void shmem_save_log_callback(void);
extern void RPCForceAssertOnComm(void);
extern void ACIPCDPrintPowerDatabase(void);
extern void ACIPCDPrintPowerDebugDatabase(void);
extern int 	init_callback(void);
extern void printStatistic(void);


/************************************************************************
 * External Variables
 ***********************************************************************/

/* COM Virtul (kerenel mapped) pool start address */
extern unsigned int g_CommRWaddr;

/* COM pool size */
extern unsigned int g_CommRWAreaSizeInBytes;

/* COM physical start addr */
extern unsigned int g_CommRWArea;

/* App pool start address */
extern unsigned int g_CommROArea;
/* App pool size */
extern unsigned int g_CommROAreaSizeInBytes;

extern unsigned int g_CommPoolPhyBegin;
extern unsigned int g_AppPoolPhyBegin;
extern OsaRefT g_PoolXRef;


#endif /* _SHMEM_LINUX_KERNEL_MODULE_H_ */
