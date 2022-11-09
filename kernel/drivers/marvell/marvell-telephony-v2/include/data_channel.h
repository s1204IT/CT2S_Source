/*------------------------------------------------------------
(C) Copyright [2006-2008] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

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
 *  FILENAME:   data_channel.h
 *  PURPOSE:    data_channel API used by stub
 *
 *****************************************************************************/

#ifndef __DATA_CHANNEL_H__
#define __DATA_CHANNEL_H__

#include <linux/semaphore.h>
#include <linux/spinlock.h>
#include <linux/kfifo.h>

#include "linux_types.h"

/*********************
 * Status Definitions
 *********************/
/* Error code */
#define	PCAC_NO_ERROR							0
#define PCAC_SUCCESS                            (PCAC_NO_ERROR)
#define	PCAC_ERROR								(PCAC_NO_ERROR-1)
#define	PCAC_REQUEST_PENDING					(PCAC_NO_ERROR-2)
#define	PCAC_ERROR_OUTOFMEMORY					(PCAC_NO_ERROR-3)
#define PCAC_ERROR_LOCKFAILED					(PCAC_NO_ERROR-4)
#define PCAC_ERROR_NULL_CONTEXT					(PCAC_NO_ERROR-5)
#define PCAC_ERROR_NULL_BUFFER					(PCAC_NO_ERROR-6)
#define PCAC_ERROR_NULL_CALLBACK				(PCAC_NO_ERROR-7)
#define	PCAC_ERROR_INIT							(PCAC_NO_ERROR-8)
#define PCAC_ERROR_SVC_ADD_FAIL					(PCAC_NO_ERROR-9)
#define PCAC_ERROR_SVC_INVALID_PARAMETER		(PCAC_NO_ERROR-10)
#define PCAC_ERROR_TX_NOT_EMPTY                 (PCAC_NO_ERROR-11)
#define PCAC_ERROR_TX_FLUSH                     (PCAC_NO_ERROR-12)
#define PCAC_PACKET_HEADER                      (PCAC_NO_ERROR-13)

typedef struct {
	spinlock_t lock;
	struct semaphore sem;
	struct kfifo k_fifo;
} *MsgQRef, MsgQ;

typedef struct {
	unsigned int ceUserContext;
	unsigned int cei;
	MsgQRef msgQ;
} CIDATASTUB_CONN_CONF_PARM;

typedef struct {
	unsigned int cei;
	MsgQRef msgQ;
} CIDATASTUB_ACT_CONF_PARM;

/* TODO */
typedef struct {
	/* Local connect point information */
	unsigned int localSapi;
	unsigned int localCei;

	/* Remote connect point information */
	unsigned int remoteSapi;

	/* Node information */
	unsigned char localNode;
	unsigned char remoteNode;
} DataChannelConnectPointInfo;

/**
 * dataChanFindMsgQBySapi
 *
 * @brief Search the queue associated with given sapi in data channel message
 *        queue list.
 *
 * Global function.
 *
 * @param unsigned int   The sapi to be seached.
 *
 * @return OSAMsgQRef The found message queue reference or NULL.
 *
 */
MsgQRef dataChanFindMsgQBySapi(unsigned int sapi);

/**
 * dataChanFindMsgQByContext
 *
 * @brief Search the queue associated with given user context in data channel
 *        message queue list.
 *
 * Global function.
 *
 * @param unsigned int   The user context to be seached.
 *
 * @return OSAMsgQRef The found message queue reference or NULL.
 *
 */
MsgQRef dataChanFindMsgQByContext(unsigned int userContext);

/**
 * dataChanFindMsgQByCei
 *
 * @brief Search the queue associated with given cei in data channel message
 *        queue list.
 *
 * Global function.
 *
 * @param unsigned int   The cei to be seached.
 *
 * @return OSAMsgQRef The found message queue reference or NULL.
 *
 */
MsgQRef dataChanFindMsgQByCei(unsigned int cei);

/**
 * dataChanFindContextByCei
 *
 * @brief Search the context associated with given cei in data channel message
 *        queue list.
 *
 * Global function.
 *
 * @param unsigned int   The cei to be seached.
 *
 * @return unsigned int The context or NULL.
 *
 */
unsigned int dataChanFindContextByCei(unsigned int cei);

#endif /* __DATA_CHANNEL_H__ */
