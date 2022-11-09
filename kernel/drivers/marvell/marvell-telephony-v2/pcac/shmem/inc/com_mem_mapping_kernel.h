/*--------------------------------------------------------------------------------------------------------------------
(C) Copyright 2006, 2007 Marvell DSPC Ltd. All Rights Reserved.
-------------------------------------------------------------------------------------------------------------------*/

/*--------------------------------------------------------------------------------------------------------------------
INTEL CONFIDENTIAL
Copyright 2006 Intel Corporation All Rights Reserved.
The source code contained or described herein and all documents related to the source code (“Material”) are owned
by Intel Corporation or its suppliers or licensors. Title to the Material remains with Intel Corporation or
its suppliers and licensors. The Material contains trade secrets and proprietary and confidential information of
Intel or its suppliers and licensors. The Material is protected by worldwide copyright and trade secret laws and
treaty provisions. No part of the Material may be used, copied, reproduced, modified, published, uploaded, posted,
transmitted, distributed, or disclosed in any way without Intel’s prior express written permission.

No license under any patent, copyright, trade secret or other intellectual property right is granted to or
conferred upon you by disclosure or delivery of the Materials, either expressly, by implication, inducement,
estoppel or otherwise. Any license under such intellectual property rights must be express and approved by
Intel in writing.
-------------------------------------------------------------------------------------------------------------------*/

/***************************************************************************
*               MODULE IMPLEMENTATION FILE
****************************************************************************
*
* Filename: com_mem_mapping_kernel.h
*
*
******************************************************************************/

#ifndef _COMM_MEM_MAPPING_KERNEL_H_
#define _COMM_MEM_MAPPING_KERNEL_H_

#include "linux_types.h"
#include "loadTable.h"
#include "apcp_mmap.h"

#define SHMEM_DEFAULT_POOL_NAME		"ShmemPool"

extern unsigned int MapComSHMEMRegions(void);
extern void *MapComPhysicalToVirtualKern(void *pAddr, unsigned int iSizeToMap);

/*
	maps COM physical address into Linux kernel addres space
*/
extern void *mapPhyusicalToVirtualKern(void *PhysicalAddr, unsigned int iSizeToMap);
extern unsigned int ConvertPhysicalToVirtualAddr(unsigned int PhysicalAddr);
extern unsigned int ConvertVirtualToPhysicalAddr(unsigned int PhysicalAddr);

#define ACIPCD_MAP_COMADDR_TO_APPADDR(pHYaddr, size)    MapComPhysicalToVirtualKern((void *)(pHYaddr), (size))

/* returns (unsigned int) physical */
#define MAP_VIRTUAL_TO_PHYSICAL_ADDRESS(pVirtaddr)	ConvertVirtualToPhysicalAddr((unsigned int)(pVirtaddr))

#endif /* _COMM_MEM_MAPPING_KERNEL_H_ */
