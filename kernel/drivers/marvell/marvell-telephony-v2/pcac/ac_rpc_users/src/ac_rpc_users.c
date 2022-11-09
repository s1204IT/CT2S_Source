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
 * Title: Generic RPC Users Register and general functions (over Shared Memory)
 *
 * Filename: ac_rpc_users.c
 *
 * Author:   Yossi Gabay
 *
 * Remarks:  This file should be very simple with basic functions only !!!
 *
 * Created: 31/10/2010
 *
 * Notes:
 ******************************************************************************/

#include	"ac_rpc.h"
#include    "ac_rpc_users.h"
/*#include    "osa.h" */

#if defined(FLAVOR_COM)

#include    "csw_mem.h"
#include 	"RdGenData.h"
#include    "diag.h"

/*******************************************************************************
* Keys (public/MRD etc.) transfer over Generic RPC
******************************************************************************/
unsigned long rpcMrdPublicKeyGet(unsigned short inBufLen, void *pInBuf,
				 unsigned short outBufLen, void *pOutBuf)
{
	unsigned long *pMrdPublicKey = NULL;
	unsigned long publicKeySize;

	RDGenGetPublicKey(&pMrdPublicKey, &publicKeySize);

	if ((pMrdPublicKey == NULL) || (publicKeySize == 0))
		return 0;

	/*check that we have enough space to copy */
	if (outBufLen < publicKeySize)
		return 0;

	memcpy(pOutBuf, pMrdPublicKey, publicKeySize);

	return 1;
}

/*COMM side - the actual ASSERT */
unsigned long RPCForceAssertOnComm(unsigned short inBufLen, void *pInBuf,
				   unsigned short outBufLen, void *pOutBuf)
{
	volatile int MANUAL_FORCE_CP_ASSERT_BY_APPS;

	/*ASSERT IS FORCED ON COMM - from APPS RPC !!!!!!!!! */
	MANUAL_FORCE_CP_ASSERT_BY_APPS = 0;
	ASSERT(MANUAL_FORCE_CP_ASSERT_BY_APPS);

	return 1;		/*just to prevent compilation issue */
}

/*******************************************************************************
* function for registering users with no dedicated "home" - COMM SIZE
******************************************************************************/
void RPCUsersRegister(void)
{
	RPC_FUNC_REGISTER(rpcMrdPublicKeyGet);
	RPC_FUNC_REGISTER(RPCForceAssertOnComm);
}
#endif /*FLAVOR_COM */

#if defined(FLAVOR_APP)
#include "com_mem_mapping_kernel.h"

/*APPS side - calling the ASSERT on COMM */
void RPCForceAssertOnComm(void)
{
	char dummy[1];
	unsigned long rc;

	/*if not yet registered - then register it */
	RegisterWithShmem();

	/*calling the ASSERT function in the COMM */
	/*boaz.sommer.mrvl - use GPC for force COMM assert - start */
	rc = RPCTxReq("RPCForceAssertOnComm", 1, dummy, 0, dummy);
	/*boaz.sommer.mrvl - use GPC for force COMM assert - end */
}

#if defined (SIM_DRIVER_SUPPORTED)
extern unsigned long rpc_sim_commutate(unsigned short in_len, void *in_buf,
				       unsigned short out_len, void *out_buf);
#endif

extern void pxa9xx_platform_rfic_reset(unsigned short in_len, void *in_buf,
				       unsigned short out_len, void *out_buf);

#if defined (CONFIG_PXA_DBG_MACRO_RPC)
extern int pxa1xxx_dbg_macro_set_ddr_params(unsigned int address,
					    unsigned int size);

static void pxa_tracer_mem_allocated_event(unsigned int Len, void *pData)
{
	unsigned int address = *(unsigned int *)pData;
	unsigned int size = *(unsigned int *)(pData + 4);

	/* Convert address from COMM to APPS */
	address = (unsigned int)ADDR_CONVERT(address);

	printk(KERN_INFO "%s: address,size 0x%x,0x%x\n", __func__, address,
	       size);

	pxa1xxx_dbg_macro_set_ddr_params(address, size);
}
#endif

/*******************************************************************************
* function for registering users with no dedicated "home" - APPS SIDE
******************************************************************************/
void RPCUsersRegister(void)
{
#if defined (SIM_DRIVER_SUPPORTED)
	RPC_FUNC_REGISTER(rpc_sim_commutate);
#endif
	/*SERG:
	   RPC_FUNC_REGISTER(pxa9xx_platform_rfic_reset);
	 */
#if defined (CONFIG_PXA_DBG_MACRO_RPC)
	RPC_FUNC_REGISTER(pxa_tracer_mem_allocated_event);
#endif
}
#endif /*FLAVOR_APP */
