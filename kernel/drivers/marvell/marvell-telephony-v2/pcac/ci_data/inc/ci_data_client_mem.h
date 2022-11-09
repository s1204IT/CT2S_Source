/*------------------------------------------------------------
(C) Copyright [2006-2011] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
    Title:       CI Data Client Mem headerfile
    Filename:    ci_data_client_mem.h
    Description: Memory manager to use uplink data shared memory
    Author:      Yossi Gabay
************************************************************************/

#if !defined(_CI_DATA_CLIENT_MEM_H_)
#define      _CI_DATA_CLIENT_MEM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "linux_types.h"
#include "osa_kernel.h"
#include "ci_data_client.h"
#include "ci_data_client_ltm.h"

/************************************************************************
 * Types
 ***********************************************************************/
	typedef struct {
		unsigned int poolBaseAddress;
		unsigned int poolSize;
		unsigned int firstFree;

		unsigned int currentNumAllocs;
		unsigned int maxNumAllocs;
		unsigned int numAllocWasNull;
		unsigned int numAllocWasNullCount;
		unsigned int flowOffTimeStampBefore;
		unsigned int flowOffTimeStampAfter;
		unsigned int flowOffCount;
		unsigned int flowOnCount;
		unsigned int flowOffMaxTime;
		unsigned int flowOffSumTime;

		spinlock_t spinlock;
		unsigned long flags;
	} CiData_UplinkPoolMemS;

/************************************************************************
 * Callback Prototypes
 ***********************************************************************/

/************************************************************************
 * Internal API's
 ***********************************************************************/

/************************************************************************
 * API's
 ***********************************************************************/
	unsigned char CiDataUplinkMemCreatePool(void *baseAddress, unsigned int size);
	void *CiDataUplinkMemDataAlloc(unsigned int size, gfp_t flags);
	int CiDataUplinkMemDataFree(const void *pMem);

	void CiDataUplinkPoolMemDebugPrint(void);

#ifdef __cplusplus
}
#endif				/*__cplusplus*/
#endif				/* _CI_DATA_CLIENT_MEM_H_ */
