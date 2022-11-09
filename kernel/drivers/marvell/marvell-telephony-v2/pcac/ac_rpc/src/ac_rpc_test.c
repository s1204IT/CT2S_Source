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
 * Title: Generic RPC test
 *
 * Filename: ac_rpc_test.c
 *
 * Author:   Boaz Sommer
 *
 * Remarks: -
 *
 * Created: 15/09/2010
 *
 * Notes:
 ******************************************************************************/
#include	"acipc_data.h"
#include	"ac_rpc.h"


void RpcCall(char *string, unsigned int l)
{
	char returned_string[256] = "";
	unsigned short len;
	unsigned long rc;

	len = strlen(string) + 1;

	rc = RPC_CALL("TestRpcCallback", len, string, len, returned_string);

	if (rc < 0) {
		pr_info("Rpc Call failed");
	} else {
		pr_info("Rpc Call succeeded, returned string = %s", returned_string);
	}
}
