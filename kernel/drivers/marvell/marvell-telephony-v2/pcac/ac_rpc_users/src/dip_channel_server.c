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
 *  a commitment by Marvell DSPC Ltd. Marvell DSPC Ltd. assumes no
 *  responsibility or liability for any errors or inaccuracies that may appear
 *  in this document or any software that may be provided in association with
 *  this document.
 *  Except as permitted by such license, no part of this document may be
 *  reproduced, stored in a retrieval system, or transmitted in any form or by
 *  any means without the express written consent of Marvell DSPC Ltd.
 *
 *******************************************************************************
 *
  * Title: Dip channels server handler
 *
 * Filename: dip_channel_server.c
 *
 * Author:   Ian Levine
 *
 * Remarks: -
 *
 * Created: 5/09/2011
*
 * Notes:
 ******************************************************************************/

#include    "dip_channel_server.h"
#include	"ac_rpc.h"
#include <linux/kernel.h>

static unsigned short m_LastAction;

#if (defined ACI_LNX_KERNEL)
extern int modify_lcd_pclk(unsigned long int pclk_val, unsigned long int hlw_elw_blw);
/*extern int pxa95xfb_pclk_modification(unsigned int pclk);*/
static int DipChanExecAction(unsigned short action, unsigned short reset)
{
	int ret = 0;
	printk(KERN_ERR "%s: action = %d, reset = %d\n", __func__, action, reset);
	switch (action) {
	case 0:
		/*normal state no action required */
		break;
	case 1:		/* modify LCD clock frequency */
		/*SERG: if (reset)
		   ret = modify_lcd_pclk(0x0F, 0x2F3E09l);
		   else
		   ret = modify_lcd_pclk(0x0D, 0x4F6809l); */
		break;
	default:
		printk(KERN_ERR "%s: acation not valid\n", __func__);
	}
	return ret;
}

static __attribute__((unused)) void DipChanCurrentAction(unsigned int Len, void *pData)
{
	unsigned short NewAction = *(unsigned short *)pData;

	if (m_LastAction == NewAction) {
		printk(KERN_ERR "%s: New Action %d is the same as Current Action", __func__, NewAction);
	} else {
		printk(KERN_ERR "%s: Change Action %d -> %d\n", __func__, m_LastAction, NewAction);
		DipChanExecAction(m_LastAction, 1);	/* reset last action */
		DipChanExecAction(NewAction, 0);	/* execute new action */
		m_LastAction = NewAction;
	}
}
#endif

void DipChanPhase1Init(void)
{
	m_LastAction = 0;
}
