/*------------------------------------------------------------
(C) Copyright [2006-2012] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
	Title:  	 CI Data Client Mem
	Filename:    ci_data_client_mem.c
	Description: Contains implementation for Memory manager to use uplink data shared memory
	Author: 	 Yossi Gabay
************************************************************************/

#include <linux/spinlock.h>
/*#include <mach/timestamp.h> */

#include "ci_data_client_mem.h"

/*enable this for debug struct of pool mem */
#define CI_DATA_ENABLE_POOL_MEM_DEBUG

/* Last 4 Bytes are used to mark the buffer as free/used */
#define CI_DATA_MEM_MARK_SIZE   			(4)
#define CI_DATA_MEM_USED_BUF_SIGN   		(0xA5A5A5A5)	/*(0xDEADBEEF) */
#define CI_DATA_MEM_FREE_BUF_SIGN   		(0x89898989)	/*(0xDEADBABE) */

#define CI_DATA_PACKET_LENGTH   			(2044)  /*PKT_LENGTH - Use the size taken from rndis_add-on.h */
#define CI_DATA_MEM_BUF_SIZE				(CI_DATA_PACKET_LENGTH + CI_DATA_MEM_MARK_SIZE)

#define ALLOC_SIGN(PmEM)			(*(unsigned long *)(((unsigned char *)(PmEM) + CI_DATA_MEM_BUF_SIZE - CI_DATA_MEM_MARK_SIZE)))
#define NEXT_FREE(PmEM) 			(*(unsigned long *)(((unsigned char *)(PmEM))))

/*#define VALID_POINTER(PmEM,sIZE)    ( ((unsigned long)(PmEM) >= gCiDataUplinkPoolMem.poolBaseAddress)  &&  \
									  ((unsigned long)(PmEM)+(sIZE) <= gCiDataUplinkPoolMem.poolBaseAddress+gCiDataUplinkPoolMem.poolSize)  &&  \
									  ((((unsigned long)(PmEM) - gCiDataUplinkPoolMem.poolBaseAddress) % CI_DATA_MEM_BUF_SIZE) == 0) )
*/
/************************************************************************
 * Internal Prototypes
 ***********************************************************************/

#if defined(CI_DATA_ENABLE_POOL_MEM_DEBUG)

#define CI_DATA_UPLINK_POOL_MEM_DEBUG_ARRAY_SIZE	100
typedef struct {
	unsigned int currentIndex;
	unsigned int arrayList[CI_DATA_UPLINK_POOL_MEM_DEBUG_ARRAY_SIZE];
	unsigned char isFree[CI_DATA_UPLINK_POOL_MEM_DEBUG_ARRAY_SIZE];
} CiData_UplinkPoolMemDebugS;

static CiData_UplinkPoolMemDebugS gCiDataUplinkPoolMemDebug;

#endif /*CI_DATA_ENABLE_POOL_MEM_DEBUG */

/************************************************************************
 * Internal Globals
 ***********************************************************************/

CiData_UplinkPoolMemS gCiDataUplinkPoolMem;

/***********************************************************************************\
* Internal Functions
************************************************************************************/
static inline void CiDataUplinkMemCriticalSectionEnter(void)
{
	spin_lock_irqsave(&gCiDataUplinkPoolMem.spinlock, gCiDataUplinkPoolMem.flags);
}

static inline void CiDataUplinkMemCriticalSectionExit(void)
{
	spin_unlock_irqrestore(&gCiDataUplinkPoolMem.spinlock, gCiDataUplinkPoolMem.flags);
}

/***********************************************************************************\
* A P I's
\***********************************************************************************/

/***********************************************************************************\
*   Name:   	 CiDataUplinkMemDataFree
*
*   Description: free buffer from pool
*   			 if buffer is not in pool boundry - free using kfree
*
*   Params: 	 pMem - pointer of buffer
*
*   Return: 	 1 - buffer in shared memory so it was freed
*   		 0 - buffer in kernel memory - not freed (should be freed by the upper layer)
\***********************************************************************************/
int CiDataUplinkMemDataFree(const void *pMem)
{
	/*void *blockAlignedPtr; */

	/*CI_DATA_DEBUG(4, "CiDataUplinkMemDataFree: pMem=%x", pMem); */

	/*if(!VALID_POINTER(pMem, CI_DATA_MEM_BUF_SIZE)) */
	if (((unsigned int)pMem < gCiDataUplinkPoolMem.poolBaseAddress) ||
		((unsigned int)pMem >= gCiDataUplinkPoolMem.poolBaseAddress + gCiDataUplinkPoolMem.poolSize))
		return 0;

	CI_DATA_DEBUG(4, "CiDataUplinkMemDataFree: pMem=%x", (unsigned int)pMem);

	pMem =
		(void *)((((unsigned int)pMem - gCiDataUplinkPoolMem.poolBaseAddress) & 0xFFFFF800) +
			 gCiDataUplinkPoolMem.poolBaseAddress);

	/* Check if it marked correctly */
	if (unlikely(ALLOC_SIGN(pMem) != CI_DATA_MEM_USED_BUF_SIGN)) {
		if (ALLOC_SIGN(pMem) == CI_DATA_MEM_FREE_BUF_SIGN)
			pr_err("%s line %d: pointer %p, is already freed!\n", __func__, __LINE__, pMem);
		else
			pr_err("%s line %d: pointer %p, is corrupted (0x%lx)!\n",
				   __func__, __LINE__, pMem, ALLOC_SIGN(pMem));
		BUG();
	}

	/* Free the buf */
	CiDataUplinkMemCriticalSectionEnter();

	NEXT_FREE(pMem) = gCiDataUplinkPoolMem.firstFree;
	gCiDataUplinkPoolMem.firstFree = (unsigned long)pMem;
	ALLOC_SIGN(pMem) = CI_DATA_MEM_FREE_BUF_SIGN;

	if (gCiDataUplinkPoolMem.currentNumAllocs == 0) {
		CI_DATA_ASSERT_MSG_P(0, FALSE, "CiDataUplinkMemDataFree: too many packets freed (pMem=%x)",
					 (unsigned int)pMem);
	}
	gCiDataUplinkPoolMem.currentNumAllocs--;

#if defined(CI_DATA_ENABLE_POOL_MEM_DEBUG)
	{
		int i;

		for (i = 0; i < CI_DATA_UPLINK_POOL_MEM_DEBUG_ARRAY_SIZE; i++) {
			if (gCiDataUplinkPoolMemDebug.arrayList[i] == (unsigned int)pMem) {
				gCiDataUplinkPoolMemDebug.isFree[i] = TRUE;
			}
		}
	}
#endif /*CI_DATA_ENABLE_POOL_MEM_DEBUG */

	CiDataUplinkMemCriticalSectionExit();

	CI_DATA_DEBUG(4, "CiDataUplinkMemDataFree: normal free pMem=%x (firstFree=%x)",
			  (unsigned int)pMem, gCiDataUplinkPoolMem.firstFree);

	CI_DATA_DEBUG(3, "F=%x", (unsigned int)pMem);

	return 1;
}

static inline u64 pxa_32k_ticks_to_nsec(u32 cycles)
{
	return (unsigned long long)cycles * 1000 * 5 * 5 * 5 * 5 * 5 * 5 >> 9;
}

/***********************************************************************************\
*   Name:   	 CiDataUplinkMemDataAlloc
*
*   Description: Allocate buffer from pool
*
*   Params: 	 size - size of buffer to allocate (but it's pre-defined size)
*
*   Return: 	 pointer to buffer, or NULL
\***********************************************************************************/
void *CiDataUplinkMemDataAlloc(unsigned int size, gfp_t flags)
{
	void *p;

	CI_DATA_DEBUG(4, "CiDataUplinkMemDataAlloc: size=%d", size);

#if 0
YG: cancel for now
		if (size > (CI_DATA_MEM_BUF_SIZE - CI_DATA_MEM_MARK_SIZE)) {
			CI_DATA_ASSERT_MSG_P(1, FALSE,
						 "CiDataUplinkMemDataAlloc: requested buffer is too big (size = %d)", size);
			/*pr_err("%s line %d: buffer requested is too big (size - %d)\n",
			   __func__, __LINE__, size);
			   BUG(); */
		}
#endif
	if (gCiDataClient.inSilentReset == TRUE) {
		/*CI_DATA_DEBUG(2, "CI MEM: Alloc - doing malloc %d", size); */
		return kmalloc(size, GFP_ATOMIC);
	}

	CiDataUplinkMemCriticalSectionEnter();

	if (!gCiDataUplinkPoolMem.firstFree) {  /*try to free before setting vertict */
		CiDataUplinkDataFree();
	}

	if (gCiDataUplinkPoolMem.firstFree) {
		ALLOC_SIGN(gCiDataUplinkPoolMem.firstFree) = CI_DATA_MEM_USED_BUF_SIGN;
		p = (void *)gCiDataUplinkPoolMem.firstFree;
		gCiDataUplinkPoolMem.firstFree = NEXT_FREE(gCiDataUplinkPoolMem.firstFree);

		gCiDataUplinkPoolMem.currentNumAllocs++;
		if (gCiDataUplinkPoolMem.currentNumAllocs > gCiDataUplinkPoolMem.maxNumAllocs) {
			gCiDataUplinkPoolMem.maxNumAllocs = gCiDataUplinkPoolMem.currentNumAllocs;
		}

		if (gCiDataUplinkPoolMem.numAllocWasNullCount >= 50) {
			unsigned int us, delta;
			gCiDataUplinkPoolMem.numAllocWasNullCount = 0;
			gCiDataUplinkPoolMem.flowOnCount++;
#if 0   			/* pxa_timestamp_read_slow_cnt doesn't exist in kernel 3.10 */
			if (pxa_timestamp_read_slow_cnt(&gCiDataUplinkPoolMem.flowOffTimeStampAfter) < 0) {
				pr_err("%s: CI timetsamp read err\n", __func__);
			}
#endif
			delta =
				gCiDataUplinkPoolMem.flowOffTimeStampAfter - gCiDataUplinkPoolMem.flowOffTimeStampBefore;
			us = (u32) (pxa_32k_ticks_to_nsec(delta)) / 1000;
			gCiDataUplinkPoolMem.flowOffMaxTime =
				(gCiDataUplinkPoolMem.flowOffMaxTime > us ? gCiDataUplinkPoolMem.flowOffMaxTime : us);
			gCiDataUplinkPoolMem.flowOffSumTime += us;
		}
	} else {
		gCiDataUplinkPoolMem.numAllocWasNull++;
		gCiDataUplinkPoolMem.numAllocWasNullCount++;
		if (gCiDataUplinkPoolMem.numAllocWasNullCount == 50) {
			gCiDataUplinkPoolMem.flowOffCount++;
#if 0   			/* pxa_timestamp_read_slow_cnt doesn't exist in kernel 3.10 */
			if (pxa_timestamp_read_slow_cnt(&gCiDataUplinkPoolMem.flowOffTimeStampBefore) < 0) {
				pr_err("%s: CI timetsamp read err\n", __func__);
			}
#endif
		}
		CiDataClientDropLowestPriorityPackets();
		p = NULL;
	}

#if defined(CI_DATA_ENABLE_POOL_MEM_DEBUG)
	gCiDataUplinkPoolMemDebug.arrayList[gCiDataUplinkPoolMemDebug.currentIndex] = (unsigned int)p;
	gCiDataUplinkPoolMemDebug.isFree[gCiDataUplinkPoolMemDebug.currentIndex] = FALSE;
	if ((++gCiDataUplinkPoolMemDebug.currentIndex) >= CI_DATA_UPLINK_POOL_MEM_DEBUG_ARRAY_SIZE) {
		gCiDataUplinkPoolMemDebug.currentIndex = 0;
	}
#endif /*CI_DATA_ENABLE_POOL_MEM_DEBUG */

	CiDataUplinkMemCriticalSectionExit();

	CI_DATA_DEBUG(4, "CiDataUplinkMemDataAlloc: ptr=%x (firstFree=%x), size=%d",
			  (unsigned int)p, gCiDataUplinkPoolMem.firstFree, size);

	CI_DATA_DEBUG(2, "A=%x", (unsigned int)p);

	return p;
}

void CiDataUplinkPoolMemDebugPrint(void)
{
#if defined(CI_DATA_ENABLE_POOL_MEM_DEBUG)
	int i;

	CI_DATA_DEBUG(0, "CiDataUplinkPoolMemDebugPrint: currentNumAllocs=%d", gCiDataUplinkPoolMem.currentNumAllocs);
	CI_DATA_DEBUG(0, "CiDataUplinkPoolMemDebugPrint: currentIndex=%d", gCiDataUplinkPoolMemDebug.currentIndex);

	for (i = 0; i < CI_DATA_UPLINK_POOL_MEM_DEBUG_ARRAY_SIZE; i++) {
		if (gCiDataUplinkPoolMemDebug.isFree[i]) {
			CI_DATA_DEBUG(0, "    index=%d, ptr=%x, isFree=%d",
					  i, gCiDataUplinkPoolMemDebug.arrayList[i], gCiDataUplinkPoolMemDebug.isFree[i]);
		}
	}
#else
	CI_DATA_DEBUG(0, "CiDataUplinkPoolMemDebugPrint: Error - mem pool debug DISABLED");
#endif
}

/***********************************************************************************\
*   Name:   	 CiDataUplinkMemCreatePool
*
*   Description: Init the uplink memory pool - PRE-DEFINED buffer size
*
*   Params: 	 *baseAddress - base address of the pool
*   			 size - size of pool
*
*   Return: 	 TRUE=success
\***********************************************************************************/
unsigned char CiDataUplinkMemCreatePool(void *baseAddress, unsigned int size)
{
	unsigned int tempBuffer;
	unsigned int tempSize;

	gCiDataUplinkPoolMem.poolBaseAddress = ((unsigned int)baseAddress + 0x1F) & ~0x1F;  /*Align 32. */
	gCiDataUplinkPoolMem.poolSize = size - (gCiDataUplinkPoolMem.poolBaseAddress - (unsigned int)baseAddress);

	CI_DATA_DEBUG(1, "CiDataUplinkMemCreatePool: baseAddress=%x, size=%d, align address=%x, new size=%d",
			  (unsigned int)baseAddress, size, gCiDataUplinkPoolMem.poolBaseAddress,
			  gCiDataUplinkPoolMem.poolSize);

	/* initilize the pool */
	memset(baseAddress, 0, size);
	gCiDataUplinkPoolMem.maxNumAllocs = 0;
	gCiDataUplinkPoolMem.currentNumAllocs = 0;
	gCiDataUplinkPoolMem.numAllocWasNull = 0;
	gCiDataUplinkPoolMem.numAllocWasNullCount = 0;
	gCiDataUplinkPoolMem.flowOffCount = 0;
	gCiDataUplinkPoolMem.flowOnCount = 0;
	gCiDataUplinkPoolMem.flowOffMaxTime = 0;
	gCiDataUplinkPoolMem.flowOffSumTime = 0;
	gCiDataUplinkPoolMem.flowOffTimeStampBefore = 0;
	gCiDataUplinkPoolMem.flowOffTimeStampAfter = 0;

	/* initialize spinlock */
	spin_lock_init(&(gCiDataUplinkPoolMem.spinlock));

	/*mark blocks as free */
	tempBuffer = gCiDataUplinkPoolMem.poolBaseAddress;
	tempSize = gCiDataUplinkPoolMem.poolSize;

	while (tempSize > CI_DATA_MEM_BUF_SIZE) {
		ALLOC_SIGN(tempBuffer) = CI_DATA_MEM_FREE_BUF_SIGN;
		NEXT_FREE(tempBuffer) = tempBuffer + CI_DATA_MEM_BUF_SIZE;

		/*printk("tempBuffer =%x, nextF=%x, size=%d", tempBuffer, NEXT_FREE(tempBuffer), tempSize); */
		tempSize -= CI_DATA_MEM_BUF_SIZE;
		tempBuffer += CI_DATA_MEM_BUF_SIZE;
	}

	NEXT_FREE(tempBuffer - CI_DATA_MEM_BUF_SIZE) = 0;   /*End of free list - going back one packet */
	/*init the first free packet */
	gCiDataUplinkPoolMem.firstFree = gCiDataUplinkPoolMem.poolBaseAddress;
	CI_DATA_DEBUG(2, "CiDataUplinkMemCreatePool - firstfree=%x, nextfree=%x",
			  gCiDataUplinkPoolMem.firstFree, (unsigned int)NEXT_FREE(gCiDataUplinkPoolMem.firstFree));

	return TRUE;
}
