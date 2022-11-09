/************************************************************************/
/*  (C) Copyright 2006 Marvell Semiconductor Israel Ltd.                */
/*	All Rights Reserved.												*/
/*											                            */
/*                                                                      */
/*  This file and the software in it is furnished under                 */
/*  license and may only be used or copied in accordance with the terms */
/*  of the license. The information in this file is furnished for       */
/*  informational use only, is subject to change without notice, and    */
/*  should not be construed as a commitment by Intel Corporation.       */
/*  Intel Corporation assumes no responsibility or liability for any    */
/*  errors or inaccuracies that may appear in this document or any      */
/*  software that may be provided in association with this document.    */
/*  Except as permitted by such license, no part of this document may   */
/*  be reproduced, stored in a retrieval system, or transmitted in any  */
/*  form or by any means without the express written consent of Marvell */
/*  Corporation.                                                        */
/*                                                                      */
/* Title: Application Communication IPC Driver                          */
/*                                                                      */
/* Filename: com_mem_mapping_user.h                                     */
/*                                                                      */
/* Author:   Israel Davuidenko											*/
/*                                                                      */
/* Project, Target, subsystem: Linux							        */
/*                                                                      */
/* Remarks: -                                                           */
/*																	    */
/* Created: 21/1/2009                                                   */
/*                                                                      */
/* Modified:                                                            */
/************************************************************************/
#if !(defined COM_MEM_MAPPING_USER_H_)
#define COM_MEM_MAPPING_USER_H_

extern void *SHMEMMallocReq(unsigned int iSize);
extern void SHMEMFreeReq(void *pArg);

extern void *Kerenel2UserByPid(void *pAddress, pid_t pid);
extern void *Kernel2UserForNvm(void *pAddress);
void *Kerenel2UserAppPoolByPid(void *pAddress, pid_t pid);

extern void *User2KernelByPid(void *pAddress, pid_t pid);
extern void *User2KernelByServiceId(void *pAddress, unsigned int ServiceId);

#define MAP_PHYSICAL_TO_VIRTUAL_ADDRESS_NVM(pHYaddr)    Kernel2UserForNvm((void *)(pHYaddr))
#define MAP_VIRTUAL_TO_PHYSICAL_ADDRESS(pVirtaddr)  User2KernelByPid((void *)(pVirtaddr), getpid())

#endif /* COM_MEM_MAPPING_USER_H_ */
