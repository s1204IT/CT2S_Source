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
 * Filename: ac_rpc.h
 *
 * Author:   Boaz Sommer
 *
 * Remarks: -
 *
 * Created: 15/09/2010
 *
 * Notes:
 ******************************************************************************/
#ifndef __AC_RPC_H__
#define __AC_RPC_H__
#include "linux_types.h"
#if defined (OSA_LINUX)
#define	AC_RPC_INCLUDE
#endif /*OSA_xxx */

#define MAX_FUNC_NAME_LENGTH	128	/*maximum length of function name */
#define MAX_RPC_FUNC_ENTRIES	32	/*max RPC entries */

/*
 * RPC API Macros
 */

#if !defined (AC_RPC_INCLUDE)	/* if AC_RPC_INCLUDE is not defined, RPC is disable, so define stubs */
#define     RPC_FUNC_REGISTER(fUNC)
#define     RPC_CALL(fUNC, iNlEN, pIN, oUTlEN, pOUT)	-1
#define     RPC_SEND_AND_FORGET(fUNC, iNlEN, pIN)
#else /* AC_RPC_INCLUDE is defined, macros point to actual functions */
#define     RPC_FUNC_REGISTER(fUNC)     \
				RPCFuncRegister(#fUNC, (RpcFuncType)fUNC)

#define     RPC_CALL(fUNC, iNlEN, pIN, oUTlEN, pOUT)               \
				RPCTxReq(fUNC,                                \
						(unsigned short)(iNlEN),  (void *)(pIN),     \
						(unsigned short)(oUTlEN), (void *)(pOUT))

#define     RPC_SEND_AND_FORGET(fUNC, iNlEN, pIN)               \
						RPCTxReq(fUNC,                                \
							(unsigned short)(iNlEN),  (void *)(pIN), 0, NULL)
#endif /*AC_RPC_INCLUDE */

#define		RPC_ALIGNED_LENGTH(lENGTH)	(((lENGTH)+3)&(~3))

#define		RPC_LOCK()
#define		RPC_UNLOCK()

/* Currently a stub */
#if (defined OSA_NUCLEUS) || (defined (ENV_LINUX) && !defined (__KERNEL__))
#define		RPC_ERR_PRINT(x)    printf(x)
#elif defined (__KERNEL__)
#define		RPC_ERR_PRINT(x)    printk(x)
#endif

typedef enum {
	RPC_NO_FUNCTION = -1	/*-1 used in "RPC_DataS->(unsigned long   rc) */
} RPC_RcErrorCodeE;

/*
 * RPC Structure
 */
typedef struct {
	unsigned long rc;
	unsigned short DataInIndex;	/* = (strlen(&Data[0]) + 4) & ~ 3 */
	unsigned short DataInLen;
	unsigned short DataOutIndex;	/* = (DataInIndex+DataInLen+3) & ~ 3 */
	unsigned short DataOutLen;
	unsigned char Data[1];	/*  RPC Name, DataIn, DataOut */
} RPC_DataS;

typedef struct {
	unsigned long rc;
	char *FuncName;
	unsigned short InLen;
	void *InPtr;
	unsigned short OutLen;
	void *OutPtr;
} RPC_ParamsS;

/*typedef struct {} RPC_QueueMsgS declared static in .C */

/*  Allocation size = (offsetof(RPC_DataS,Data) + strlen(RpcName) + 1 + */
/*                     DataInLen + 3 + DataOutLen) ~ 3 */

/*
 * API Prototype
 */
typedef unsigned long (*RpcFuncType) (unsigned short, void *, unsigned short, void *);

void RPCPhase1Init(void);
void RPCPhase2Init(void);
void RegisterWithShmem(void);
RpcFuncType RPCFuncRegister(char *, RpcFuncType);
unsigned long RPCTxReq(char *, unsigned short, void *, unsigned short, void *);

#endif
