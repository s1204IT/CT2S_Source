/*------------------------------------------------------------
(C) Copyright [2006-2008] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/******************************************************************************
**
**  FILENAME:       dbgLogWm.c
**
**  PURPOSE: Windows mobile specific functiions
**
******************************************************************************/

#include "dbgLog.h"

#if 0				/*defined(__KERNEL__)*/
#include <asm/hardware.h>
#define LINUX_FREE_RUNNING_32K_REG      __REG(0x40A00040)
#define	LNX_GET_TIME_STAMP	LINUX_FREE_RUNNING_32K_REG
#else
#define	LNX_GET_TIME_STAMP	0
#endif

unsigned int dbgLogGetTimestamp(void)
{
	return LNX_GET_TIME_STAMP;
}

void DbgLogOsInit(__attribute__ ((unused))
		  void *p)
{
}
