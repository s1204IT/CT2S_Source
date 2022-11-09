/*
 * (C) Copyright 2006-2011 Marvell International Ltd.
 * All Rights Reserved
 */

/* ===========================================================================
File        :	shmem_lnx_kmod_api.h
Description :	Definition of OSA Software Layer data types specific to the
				linux kernel module(s) programing.

Notes       :
=========================================================================== */
#ifndef _SHMEM_LINUX_KERNEL_MODULE_API_H_
#define _SHMEM_LINUX_KERNEL_MODULE_API_H_

typedef void (*setPPPLoopback) (unsigned short port, int DLMult);

void registerSetPPPLoppbackCB(setPPPLoopback f);
void set_ccinet_wakelock_ptr(long *ptr);


#endif /* _SHMEM_LINUX_KERNEL_MODULE_API_H_ */
