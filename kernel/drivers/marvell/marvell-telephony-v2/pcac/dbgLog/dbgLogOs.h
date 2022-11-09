/*------------------------------------------------------------
(C) Copyright [2006-2008] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/******************************************************************************
**
**  FILENAME:       dbgLogOs.h
**
**  PURPOSE:        Header file for OS specific code
**
**  NOTES:			Windows mobile Specific Code
**
******************************************************************************/

#if !defined(_DBG_LOG_OS_H_)
#define _DBG_LOG_OS_H_

#include <linux/kernel.h>
#include <linux/slab.h>
#include "linux_types.h"

#define	DBGLOG_EXPORT(a)	EXPORT_SYMBOL(a)

/*#define SHMEM_EVENT_LOG_SIZE 1024*8*/

#if defined(__KERNEL__)
#define DBG_LOG_PRINT           printk
#define DBG_LOG_MALLOC(s)       kmalloc((s), GFP_KERNEL)
#define DBG_LOG_FREE            kfree
#else /*__KERNEL__*/
#define DBG_LOG_PRINT           printf
#define DBG_LOG_MALLOC(s)       malloc((s))
#define DBG_LOG_FREE            free
#endif /*__KERNEL__*/

void DbgLogOsInit(void *p);
unsigned int dbgLogGetTimestamp(void);

#endif /*_DBG_LOG_OS_H_*/
