/*#include <assert.h> */
#include <linux/spinlock.h>
#include <linux/slab.h>
/*#include <assert.h> */
#include "linux_types.h"
#include "allocator.h"

#define MAX_NAME_LENGTH (16+2)
#define     BUF_HDR_GUARD                   2
#define     SIZEOF_CACHE_LINE               32
#define     SIZEOF_MIN_BUF                  (SIZEOF_CACHE_LINE * 1)	/*  Must be SIZEOF_CACHE_LINE * OddNumber */
#define     LARGEST_BUF_WITH_SPECIAL_LIST   (SIZEOF_MIN_BUF * 64)	/*  Must be SIZEOF_MIN_BUF * n */
#define     NUMBER_OF_FREE_LISTS            (1 + (LARGEST_BUF_WITH_SPECIAL_LIST / SIZEOF_MIN_BUF))
#define     POOL_ID                         0x48287997	/*  This is used for the ICAT EXPORTED FUNCTIONs */
#define     GUARD_PATTERN                   0xDEADBEEF
#define     MIN_BUF_MASK                    (SIZEOF_MIN_BUF-1)
#define     CACHE_LINE_MASK                 (SIZEOF_CACHE_LINE - 1)

typedef struct OSA_CS_INTERNAL_DATA {
	unsigned long flags;
	spinlock_t protect_lock;
} OSA_CS_INTERNAL_DATA;

typedef struct MemBufTag {
	unsigned int Guard[BUF_HDR_GUARD];
	unsigned int ReqSize;	/*  This is used only for the DATA GUARD CHECK otherwise it can be added to the Header Guard. */
	unsigned int UserParam;	/*  Free for the user to use. */
	OsaRefT poolRef;
	struct MemBufTag *pPrevBuf;	/*  The buf that is physicaly befor this buf - not prev free. */
	unsigned int AllocCount;
	unsigned int BufSize;
	struct MemBufTag *pNextFreeBuf;	/*  Next buf on free list. This is also the first address of the allocated buffer. */
} OsaMem_BufHdr;

typedef struct FreeListTag {
	struct FreeListTag *pNextList;
	OsaMem_BufHdr *pFirstBuf;
} OsaMem_FreeList;

typedef struct MemBlkHdrTag {
	unsigned int FirstAddress;
	unsigned int LastAddress;
	struct MemBlkHdrTag *pNextMemBlk;
} OsaMem_MemBlkHdr;

typedef struct PoolHeaderTag {
	unsigned int PoolID;	/*  Uswd for the ICAT EXPORTED FUNCTIONs */
	OsaRefT CriticalSectionHandle;
	OsaMem_FreeList FreeList[NUMBER_OF_FREE_LISTS];
	OsaMem_MemBlkHdr *pFirstMemBlk;
	struct PoolHeaderTag *pNextPool;
	char poolName[MAX_NAME_LENGTH];
} OsaMem_PoolHeader;

typedef enum {
	eNO_VALIDITY_CHK,
	eMIN_VALIDITY_CHK,
	eEXT_VALIDITY_CHK
} OsaMem_ValiditiyChkE;

static OsaRefT OsaMem_DefaultPoolRef;
static OsaMem_PoolHeader *OsaMem_FirstPool;
static OsaRefT OsaMem_CriticalSectionHandle;
static OsaMem_ValiditiyChkE OsaMem_ValidityCheck = eNO_VALIDITY_CHK;

#define     SIZEOF_HDR_OF_ALLOC_BUF         (offsetof(OsaMem_BufHdr, pNextFreeBuf))
#define     BUF_DHR_ADRS(PmEM)              ((OsaMem_BufHdr *)((unsigned int)(PmEM) - SIZEOF_HDR_OF_ALLOC_BUF))
#define     FREE_LIST_INDEX(sIZE)			((sIZE <= LARGEST_BUF_WITH_SPECIAL_LIST) ? ((sIZE - 1) / SIZEOF_MIN_BUF) : (NUMBER_OF_FREE_LISTS - 1))
#define     ALLOC_BUF_ADRS(PbUFhDR)         ((void *)((unsigned int)(PbUFhDR) + SIZEOF_HDR_OF_ALLOC_BUF))
#define     LAST_FREE_LIST(PpOOLhEADER)     (&PpOOLhEADER->FreeList[NUMBER_OF_FREE_LISTS - 1])
#define     SET_GUARD_IN_BUF_HDR(PbUFhDR)	((PbUFhDR)->Guard[0] = (PbUFhDR)->Guard[1] = GUARD_PATTERN)
#define     BUF_SIZE(rEQsIZE)               ((rEQsIZE + MIN_BUF_MASK) & ~MIN_BUF_MASK)
#define     NEXT_BUF_ADRS(PbUFhDR)          ((OsaMem_BufHdr *)((unsigned int)ALLOC_BUF_ADRS(PbUFhDR) + PbUFhDR->BufSize))
#define     VALID_PTR(PmEM)             	(OsaMem_ValidityCheck ? OsaMemValidPtr((unsigned int)(PmEM), FALSE) : TRUE)
#define     POOL_HEADER_SIZE                (sizeof(OsaMem_PoolHeader))

/***********************************************************************
 *
 * Name:        OsaMemAddToFreeList()
 *
 * Description: Add a free buffer to an OSA pool.
 *
 * Parameters:
 *  OsaMem_BufHdr   *pFreeBuf   - starting address of the memory to add to free l\
ist
*
*
* Notes:   This function should be called inside a critical section.
*
***********************************************************************/
static void OsaMemAddToFreeList(OsaMem_BufHdr *pFreeBuf)
{
	OsaMem_PoolHeader *pPoolHeader = (OsaMem_PoolHeader *) pFreeBuf->poolRef;
	unsigned int FreeListIndex = FREE_LIST_INDEX(pFreeBuf->BufSize);
	OsaMem_FreeList *pFreeList = &pPoolHeader->FreeList[FreeListIndex];
	OsaMem_BufHdr *pFree = pFreeList->pFirstBuf, *pPrev;

/*
 * Case when first on list
 */
	if (!pFree || pFreeBuf->BufSize <= pFree->BufSize) {
		pFreeList->pFirstBuf = pFreeBuf;
		pFreeBuf->pNextFreeBuf = pFree;

/*
 * If list was empty - set pointers of the smaller lists
 */
		if (!pFree && pFreeList != LAST_FREE_LIST(pPoolHeader))
			while (FreeListIndex--)
				if (pPoolHeader->FreeList[FreeListIndex].pNextList <= pFreeList)
					break;
				else
					pPoolHeader->FreeList[FreeListIndex].pNextList = pFreeList;

	}
/*
 * Not first - add it to list according to it's size
 */
	else {
		do {
			pPrev = pFree;
			pFree = pFree->pNextFreeBuf;
		} while (pFree && pFree->BufSize < pFreeBuf->BufSize);

		pPrev->pNextFreeBuf = pFreeBuf;
		pFreeBuf->pNextFreeBuf = pFree;
	}
}

/***********************************************************************
 *
 * Name:        OsaMemValidPtr()
 *
 * Description: Checks if the allocated memory is a valid memory buffer.
 *
 * Parameters:
 *  unsigned int      FirstAddress    - starting address of memory.
 *
 * Returns:
 *  TRUE  - Valid Memory
 *  FALSE - Invalid Memory
 *
 * Notes:
 *
 ***********************************************************************/
static unsigned char OsaMemValidPtr(unsigned int FirstAddress, unsigned char PoolCheck)
{
	OsaMem_BufHdr *pBufHdr;
	OsaMem_MemBlkHdr *pMemBlk;
	OsaMem_PoolHeader *pPool;
	unsigned int LastAddress, *pGuard;

/*
 * Check Address
 */
	if (!FirstAddress) {
		/*  Address is NULL. */
		ASSERT(FALSE);
		return FALSE;
	}

	if (FirstAddress & CACHE_LINE_MASK) {
		/*  Address is not aligned. */
		ASSERT(FALSE);
		return FALSE;
	}

/*
 * Check Buf Header
 */
	pBufHdr = BUF_DHR_ADRS(FirstAddress);
	{
		if (pBufHdr->Guard[0] != GUARD_PATTERN || pBufHdr->Guard[1] != GUARD_PATTERN) {
			/*  No header guard. */
			printk("\nOsaMemValidPtr failed: %x, %d\n", FirstAddress, PoolCheck);
			ASSERT(FALSE);
			return FALSE;
		}
	}

	if (PoolCheck) {
		if ((pBufHdr->AllocCount && !pBufHdr->ReqSize) || (!pBufHdr->AllocCount && pBufHdr->ReqSize)) {
			ASSERT(FALSE);
			return FALSE;
		}
	}

	else if (!pBufHdr->AllocCount || !pBufHdr->ReqSize) {
		ASSERT(FALSE);
		return FALSE;
	}
	if (((OsaMem_PoolHeader *) (pBufHdr->poolRef))->PoolID != POOL_ID) {
		ASSERT(FALSE);
		return FALSE;
	}

	LastAddress = (unsigned int)NEXT_BUF_ADRS(pBufHdr);

	if (OsaMem_ValidityCheck == eEXT_VALIDITY_CHK) {
#if (defined FLAVOR_APP)
		/*  Avoid ASSERT on APP side when checking a COM pool. */
		if (!PoolCheck)
#endif
		{
			for (pPool = OsaMem_FirstPool; pPool; pPool = pPool->pNextPool)
				if ((OsaRefT *) pPool == pBufHdr->poolRef)
					break;
			if (!pPool) {
				/*  Illegal poolRef. */
				ASSERT(FALSE);
				return FALSE;
			}

			for (pMemBlk = pPool->pFirstMemBlk; pMemBlk; pMemBlk = pMemBlk->pNextMemBlk)
				if (FirstAddress >= pMemBlk->FirstAddress && LastAddress <= pMemBlk->LastAddress)
					break;

			if (!pMemBlk) {
				/*  Invalid buf size. */
				ASSERT(FALSE);
				return FALSE;
			}
		}
		if (pBufHdr != ((OsaMem_BufHdr *) LastAddress)->pPrevBuf) {
			/*  Invalid buf size. */
			ASSERT(FALSE);
			return FALSE;
		}

/*
 * Check memory guard
 */
		if (pBufHdr->AllocCount) {
			pGuard =
			    (unsigned int *)((FirstAddress + pBufHdr->ReqSize + sizeof(unsigned int) - 1) &
					     ~(sizeof(unsigned int) - 1));
			while (pGuard < (unsigned int *)LastAddress)
				if (*pGuard != GUARD_PATTERN) {
					/*  No memory guard. */
					ASSERT(FALSE);
					return FALSE;
				} else
					pGuard++;
		}
	}

	return TRUE;
}

/***********************************************************************
 *
 * Name:        OsaMemAddMemoryToPool()
 *
 * Description: Add memory to an OSA pool.
 *
 * Parameters:
 *  OsaRefT     poolRef     - memory pool reference
 *  unsigned char       *memBase    - starting address of memory for the pool
 *  unsigned int      memSize     - total # bytes for the memory pool
 *
 * Returns:
 *  OS_SUCCESS          - service was completed successfully
 *  OS_INVALID_REF      - invalid pool ref.
 *  OS_INVALID_SIZE     - size not large enough.
 *  OS_INVALID_MEMORY   - illegal memory block.
 *
 * Notes:
 *
 ***********************************************************************/
static OSA_STATUS OsaMemAddMemoryToPool(OsaRefT poolRef, void *memBase, unsigned int memSize, void *pForFutureUse)
{
	OsaRefT Handle;
	OsaMem_BufHdr *pBufHdr;
	OsaMem_MemBlkHdr *pMemBlk;
	OsaMem_PoolHeader *pPool;
	unsigned int FirstAdrs, LastAdrs;

	/*
	 * Check Parameters
	 */
	FirstAdrs = (unsigned int)memBase;	/*  Temp. */
	memBase = (void *)((FirstAdrs + 3) & ~3);	/*  Align 4. */
	memSize -= ((unsigned int)memBase - FirstAdrs);

	if (memSize < (sizeof(OsaMem_MemBlkHdr) + SIZEOF_CACHE_LINE + 2 * SIZEOF_HDR_OF_ALLOC_BUF + SIZEOF_MIN_BUF))
		return OS_INVALID_SIZE;

/*
 * Get poolRef
 */
	if (!poolRef) {
		poolRef = OsaMem_DefaultPoolRef;

		if (!poolRef) {
			ASSERT(FALSE);
			return OS_INVALID_REF;
		}
	}

	pPool = (OsaMem_PoolHeader *) poolRef;

/*
 * Calculate the first memory for allocation
 */
	FirstAdrs = (unsigned int)memBase + sizeof(OsaMem_MemBlkHdr);
	memSize -= sizeof(OsaMem_MemBlkHdr);
	if (FirstAdrs & CACHE_LINE_MASK) {
		unsigned int mb = FirstAdrs;

		FirstAdrs = (FirstAdrs + CACHE_LINE_MASK) & ~CACHE_LINE_MASK;
		memSize -= FirstAdrs - mb;
	}

	memSize &= ~CACHE_LINE_MASK;
	LastAdrs = FirstAdrs + memSize - 1;

/*
 * Make sure memory is not already in use
 */
	for (pMemBlk = pPool->pFirstMemBlk; pMemBlk; pMemBlk = pMemBlk->pNextMemBlk)
		if ((FirstAdrs >= pMemBlk->FirstAddress && FirstAdrs <= pMemBlk->LastAddress) ||
		    (LastAdrs >= pMemBlk->FirstAddress && LastAdrs <= pMemBlk->LastAddress)) {
			return OS_SUCCESS;	/*  This is legal because no support for MemPoolDelete . */
		}

	pMemBlk = (OsaMem_MemBlkHdr *) memBase;

	pMemBlk->FirstAddress = FirstAdrs;
	pMemBlk->LastAddress = LastAdrs;

/*
 * Set Guard
 */
	if (OsaMem_ValidityCheck == eEXT_VALIDITY_CHK) {
		unsigned int *p = (unsigned int *)(LastAdrs + 1);

		while (--p >= (unsigned int *)FirstAdrs)
			*p = GUARD_PATTERN;
	}

/*
 * Set a Dummy buffer at the end of the memory
 */
	memSize -= SIZEOF_HDR_OF_ALLOC_BUF;

	pBufHdr = (OsaMem_BufHdr *) (FirstAdrs + memSize);
	memset(pBufHdr, 0, SIZEOF_HDR_OF_ALLOC_BUF);
	SET_GUARD_IN_BUF_HDR(pBufHdr);
	pBufHdr->poolRef = poolRef;
	pBufHdr->pPrevBuf = (OsaMem_BufHdr *) FirstAdrs;
	pBufHdr->AllocCount = 1;	/*  Make sure we will not free it. */

/*
 * Set the Buf Header
 */
	pBufHdr = pBufHdr->pPrevBuf;
	memset(pBufHdr, 0, sizeof(OsaMem_BufHdr));

	SET_GUARD_IN_BUF_HDR(pBufHdr);
	pBufHdr->poolRef = poolRef;
	memSize -= SIZEOF_HDR_OF_ALLOC_BUF;
	pBufHdr->BufSize = memSize;
	/* Add Buf to Free List */
	Handle = OsaCriticalSectionEnter(pPool->CriticalSectionHandle, NULL);

	pMemBlk->pNextMemBlk = pPool->pFirstMemBlk;
	pPool->pFirstMemBlk = pMemBlk;

	OsaMemAddToFreeList(pBufHdr);

	OsaCriticalSectionExit(Handle, NULL);

	return OS_SUCCESS;
}

/***********************************************************************
 *
 * Name:        OsaCriticalSectionCreate()
 *
 * Description:
 *
 * Parameters:  OsaCriticalSectionCreateParamsT     * pParams
 *
 * Returns:     pointer to sOsaCriticalPlaceData_t  structure
 *
 * Notes:
 *
 ***********************************************************************/
OsaRefT OsaCriticalSectionCreate(OsaCriticalSectionCreateParamsT *pParams)
{
	OSA_CS_INTERNAL_DATA *pHandle = kmalloc(sizeof(*pHandle), GFP_KERNEL);
	if (pHandle == NULL)
		return NULL;

	/* Fix for SMP - must initialize spin lock */
	spin_lock_init(&(pHandle->protect_lock));

	return (OsaRefT) pHandle;
}

/***********************************************************************
 *
 * Name:        OsaMem_InitPools()
 *
 * Description: Initialize this module.
 *
 * Parameters:
 *
 * Returns:
 *  OS_SUCCESS
 *  OS_FAIL
 *
 * Notes:
 *
 ***********************************************************************/
OSA_STATUS OsaMem_InitPools(void)
{
	OsaCriticalSectionCreateParamsT Params;

	if (OsaMem_CriticalSectionHandle != 0)
		return OS_SUCCESS;

	if (SIZEOF_HDR_OF_ALLOC_BUF != SIZEOF_CACHE_LINE)
		ASSERT(FALSE);	/*  BUF_HDR_GUARD Has a wrong value */

	memset((void *)&Params, 0, sizeof(Params));
	Params.name = "OsaMemPoolBase";

	OsaMem_CriticalSectionHandle = OsaCriticalSectionCreate(&Params);

	if (!OsaMem_CriticalSectionHandle) {
		ASSERT(FALSE);
		return OS_FAIL;
	}

	OsaMem_DefaultPoolRef = NULL;
	OsaMem_FirstPool = NULL;
	OsaMem_ValidityCheck = eEXT_VALIDITY_CHK;

	return OS_SUCCESS;
}

/***********************************************************************
 *
 * Name:        OsaMemDeleteFromFreeList()
 *
 * Description: Take the buffer off the free list.
 *
 * Parameters:
 *  OsaMem_BufHdr *   - pointer to the buffer.
 *
 * Notes:   This function should be called inside a critical section.
 *
 ***********************************************************************/
static void OsaMemDeleteFromFreeList(OsaMem_BufHdr *pBuf)
{
	OsaMem_PoolHeader *pPool = (OsaMem_PoolHeader *) pBuf->poolRef;
	unsigned int i = FREE_LIST_INDEX(pBuf->BufSize);
	OsaMem_FreeList *pList = &pPool->FreeList[i];
	OsaMem_BufHdr *pPrev;

/*
 * Case when first buffer on list
 */
	if (pList->pFirstBuf == pBuf) {
		pList->pFirstBuf = pBuf->pNextFreeBuf;

		/*
		 * If list became empty - point over it
		 */
		if (!pList->pFirstBuf && pList != LAST_FREE_LIST(pPool)) {
			while (i--)
				if (pPool->FreeList[i].pNextList != pList)
					break;
				else
					pPool->FreeList[i].pNextList = pList->pNextList;
		}
	}

/*
 * Case when not first buffer on list
 */
	else {
		for (pPrev = pList->pFirstBuf; pPrev && pPrev->pNextFreeBuf != pBuf; pPrev = pPrev->pNextFreeBuf) {
		}
		if (!pPrev) {
			ASSERT(FALSE);
			return;
		}
		pPrev->pNextFreeBuf = pBuf->pNextFreeBuf;
	}
}

/***********************************************************************
 *
 * Name:        OsaMemGetPoolRef()
 *
 * Description: Get the pool reference according to name or memory addresss
 *
 * Parameters:
 *  char * - Pool Name
 *  void * - A memory address.
 *
 * Notes:
 *
 ***********************************************************************/

OsaRefT OsaMemGetPoolRef(char *poolName, void *pMem, void *pForFutureUse)
{
	OsaMem_MemBlkHdr *pMemBlk;

	OsaMem_PoolHeader *pPool;

	for (pPool = OsaMem_FirstPool; pPool; pPool = pPool->pNextPool) {
		if (poolName && !strcmp(pPool->poolName, poolName))
			return (OsaRefT) pPool;

		for (pMemBlk = pPool->pFirstMemBlk; pMemBlk; pMemBlk = pMemBlk->pNextMemBlk)
			if ((unsigned int)pMem >= pMemBlk->FirstAddress && (unsigned int)pMem <= pMemBlk->LastAddress)
				return (OsaRefT) pPool;
	}

	return NULL;
}

/***********************************************************************
 *
 * Name:        OsaMemFree()
 *
 * Description: Free the Memory.
 *
 * Parameters:
 *  void * - starting address of the memory.
 *
 * Notes:
 *
 ***********************************************************************/
void OsaMemFree(void *pMem)
{
	OsaMem_BufHdr *pBufHdr = BUF_DHR_ADRS(pMem), *pNeighbour;
	OsaMem_PoolHeader *pPoolHeader = (OsaMem_PoolHeader *) pBufHdr->poolRef;
	OsaRefT Handle;
	unsigned int BytesAddedToPool;

	Handle = OsaCriticalSectionEnter(pPoolHeader->CriticalSectionHandle, NULL);

/*
 * Case when more than one user on this memory.
 */
	if (--pBufHdr->AllocCount) {
		OsaCriticalSectionExit(Handle, NULL);
		return;
	}
	pBufHdr->ReqSize = 0;

	BytesAddedToPool = pBufHdr->BufSize;

/*
 * Case when prev buffer is not free
 */
	pNeighbour = pBufHdr->pPrevBuf;

	if (!pNeighbour || pNeighbour->AllocCount) {
		pNeighbour = NEXT_BUF_ADRS(pBufHdr);
	}

/*
 * When prev buffer is free - take it off list and combine them together
 */
	else {
		OsaMemDeleteFromFreeList(pNeighbour);

		pBufHdr->BufSize += SIZEOF_HDR_OF_ALLOC_BUF;
		pNeighbour->BufSize += pBufHdr->BufSize;
		pBufHdr = pNeighbour;
		pNeighbour = NEXT_BUF_ADRS(pBufHdr);
		pNeighbour->pPrevBuf = pBufHdr;
		BytesAddedToPool += SIZEOF_HDR_OF_ALLOC_BUF;
	}
/*
 * Add the buf to free list
 */
	OsaMemAddToFreeList(pBufHdr);
	OsaCriticalSectionExit(Handle, NULL);
}

/***********************************************************************
 *
 * Name:        OsaMemGetUserParam()
 *
 * Description: Returns the user's parameter from the internal database.
 *
 * Parameters:
 *
 * Notes:
 *
 ***********************************************************************/
unsigned int OsaMemGetUserParam(void *pMem)
{
	if (!VALID_PTR(pMem))
		return 0;

	return BUF_DHR_ADRS(pMem)->UserParam;
}

/***********************************************************************
 *
 * Name:        OsaMemSetUserParam()
 *
 * Description: Enables the user to save a 32 bit parameter in the internal database.
 *
 * Parameters:
 *  void *  -   Starting address of the allocated memory.
 *  unsigned int  -   Value to save.
 *
 * Returns:
 *  TRUE  - Success
 *  FALSE - Fail
 *
 * Notes:
 *
 ***********************************************************************/
OSA_STATUS OsaMemSetUserParam(void *pMem, unsigned int UserParam)
{
	if (!VALID_PTR(pMem))
		return OS_INVALID_MEMORY;	/*  The ASSERT is handled in the function. */

	BUF_DHR_ADRS(pMem)->UserParam = UserParam;

	return OS_SUCCESS;
}

/***********************************************************************
 *
 * Name:        OsaMemAlloc()
 *
 * Description: Allocate Memory.
 *
 * Parameters:
 *  OsaRefT         poolRef     - memory pool reference
 *  unsigned int          Size        - Size of needed memory.
 *
 * Returns:
 *  void * - starting address of a the free memory.
 *
 * Notes:
 *
 ***********************************************************************/
void *OsaMemAlloc(OsaRefT poolRef, unsigned int Size)
{
	OsaMem_PoolHeader *pPoolHeader;
	unsigned int BufSize, BufSizeTail, FreeListIndex;
	OsaMem_FreeList *pFreeList;
	OsaMem_BufHdr *pFree, *pNewBuf;
	OsaRefT Handle;

/*
 * Check Parameters
 */

	if (!poolRef) {
		poolRef = OsaMem_DefaultPoolRef;

		if (!poolRef) {
			ASSERT(FALSE);
			return NULL;
		}
	}

	if (OsaMem_ValidityCheck == eEXT_VALIDITY_CHK)
		BufSize = BUF_SIZE(Size + 4);	/*  Make sure to have at least one guard at the end of the data. */
	else
		BufSize = BUF_SIZE(Size);

	FreeListIndex = FREE_LIST_INDEX(BufSize);

/*
 * Enter Critical Section
 */
	pPoolHeader = (OsaMem_PoolHeader *) poolRef;
	pFreeList = &pPoolHeader->FreeList[FreeListIndex];

	Handle = OsaCriticalSectionEnter(pPoolHeader->CriticalSectionHandle, NULL);
/*
 * Get a free buf
 */
	if (!pFreeList->pFirstBuf)
		pFreeList = pFreeList->pNextList;

	if (!pFreeList) {
		/*  No memory found. */
		OsaCriticalSectionExit(Handle, NULL);
		return NULL;
	}

	for (pFree = pFreeList->pFirstBuf; pFree; pFree = pFree->pNextFreeBuf)
		if (BufSize <= pFree->BufSize)
			break;

	if (!pFree) {
		/*  No memory found. */
		OsaCriticalSectionExit(Handle, NULL);
		return NULL;
	}

	OsaMemDeleteFromFreeList(pFree);

/*
 * Case when the buf is large enough to be added back to list
 */
	BufSizeTail = pFree->BufSize - BufSize;

	if (BufSizeTail >= (SIZEOF_HDR_OF_ALLOC_BUF + SIZEOF_MIN_BUF)) {
		pFree->BufSize = BufSize;
		pNewBuf = NEXT_BUF_ADRS(pFree);
		memset(pNewBuf, 0, sizeof(OsaMem_BufHdr));
		SET_GUARD_IN_BUF_HDR(pNewBuf);
		pNewBuf->poolRef = poolRef;
		pNewBuf->pPrevBuf = pFree;
		pNewBuf->BufSize = BufSizeTail - SIZEOF_HDR_OF_ALLOC_BUF;
		OsaMemAddToFreeList(pNewBuf);

		NEXT_BUF_ADRS(pNewBuf)->pPrevBuf = pNewBuf;
/*        CHECK_POOL( pPoolHeader ) ; */

		BufSizeTail = SIZEOF_HDR_OF_ALLOC_BUF;	/*  Used for the statistics. */
	} else
		BufSizeTail = 0;	/*  Used for the statistics. */
/*
 * Mark as allocated
 */
	pFree->AllocCount = 1;
	pFree->ReqSize = Size;
	if (OsaMem_ValidityCheck == eEXT_VALIDITY_CHK) {
		unsigned int *p = (unsigned int *)NEXT_BUF_ADRS(pFree),
		    *pEndOfData = (unsigned int *)((unsigned int)ALLOC_BUF_ADRS(pFree) + Size);

		while (--p >= pEndOfData)
			*p = GUARD_PATTERN;
	}

/*
 * Exit Critical Section and return
 */

	OsaCriticalSectionExit(Handle, NULL);

	return ALLOC_BUF_ADRS(pFree);
}

/***********************************************************************
 *
 * Name:        OsaCriticalSectionEnter()
 *
 * Description:
 *
 * Parameters:  OsaRefT  OsaRef   - Reference from the Create Function.
 *
 * Returns:     pointer to sOsaCriticalPlaceData_t  structure
 *
 * Notes:
 *
 ***********************************************************************/
OsaRefT OsaCriticalSectionEnter(OsaRefT OsaRef,	/*  [IN]    Reference from the Create Function. */
				void *pForFutureUse)
{
	OSA_CS_INTERNAL_DATA *pHandle = (OSA_CS_INTERNAL_DATA *) OsaRef;
	spin_lock_irqsave(&(pHandle->protect_lock), pHandle->flags);

	return (OsaRefT) pHandle;
}

/***********************************************************************
 *
 * Name:        OsaCriticalSectionExit()
 *
 * Description:
 *
 * Parameters:  OsaRefT  OsaRef   - Reference from the Create Function.
 *
 * Returns:     none
 *
 * Notes:
 *
 ***********************************************************************/
void OsaCriticalSectionExit(OsaRefT OsaRef,	/*  [IN]    Reference from the Enter Function. */
			    void *pForFutureUse)
{
	OSA_CS_INTERNAL_DATA *pHandle = (OSA_CS_INTERNAL_DATA *) OsaRef;
	spin_unlock_irqrestore(&(pHandle->protect_lock), pHandle->flags);
}

/***********************************************************************
 *
 * Name:        OsaMemCreatePool()
 *
 * Description: Initialize a pool that will be handled by OSA and not by the OS.
 *              This pool always will allocate a size that is a multiply of 32 (=Cache line)
 *              and aligned 32.
 *
 * Parameters:
 *  OsaRefT                     *pOsaRef    [OT]    pointer to memory pool structure to fill
 *  OsaMemCreateParamsT         *pParams    [IN]    Input Parameters (see datatype for detai\
ls).
*
* Returns:
*  OS_SUCCESS          - service was completed successfully
*  OS_INVALID_SIZE     - size not large enough.
*  OS_INVALID_MEMORY   - illegal memory block.
*
* Notes:

*
***********************************************************************/
OSA_STATUS OsaMemCreatePool(OsaRefT *pOsaRef, OsaMemCreateParamsT *pParams)
{
	OsaCriticalSectionCreateParamsT CritSecParams;
	OSA_STATUS osaStatus;
	OsaMem_PoolHeader *pPoolHeader;
	OsaRefT Handle;
	unsigned int i, poolBase, poolSize = 0;	/*  Avoid warning. */
	char Name[MAX_NAME_LENGTH], *pName;

	OsaMem_InitPools();
/*
 * Check Parameters
 */
	ASSERT(pParams);

	if (!pParams->poolBase) {
		/*  If no mem - alloc header from default pool. */
		poolBase = (unsigned int)OsaMemAlloc(NULL, POOL_HEADER_SIZE);
		ASSERT(poolBase);
	}

	else if (pParams->poolSize < POOL_HEADER_SIZE) {
		ASSERT(FALSE);
		return OS_INVALID_SIZE;
	}

	else {
		poolBase = ((unsigned int)pParams->poolBase + 3) & ~3;	/*  Align 4. */
		poolSize = pParams->poolSize - (poolBase - (unsigned int)pParams->poolBase);
	}

/*
 * Set pool's header
 */
	pPoolHeader = (OsaMem_PoolHeader *) poolBase;
	memset(pPoolHeader, 0, POOL_HEADER_SIZE);

	for (i = 0; i < NUMBER_OF_FREE_LISTS - 1; i++)
		pPoolHeader->FreeList[i].pNextList = LAST_FREE_LIST(pPoolHeader);	/* Point to special pool. */

	*pOsaRef = (OsaRefT) poolBase;
	pPoolHeader->PoolID = POOL_ID;

	if (pParams->name && *pParams->name)
		pName = pParams->name;
	else
		snprintf(pName = Name, sizeof(Name), "OsaPool_%08X", (unsigned int)poolBase);

	memset((void *)&CritSecParams, 0, sizeof(CritSecParams));
	CritSecParams.name = pName;
	CritSecParams.bSharedForIpc = pParams->bSharedForIpc;

	pPoolHeader->CriticalSectionHandle = OsaCriticalSectionCreate(&CritSecParams);
	ASSERT(pPoolHeader->CriticalSectionHandle);

/*
 * Add memory to the pool
 */
	if (pParams->poolBase) {
		osaStatus = OsaMemAddMemoryToPool(*pOsaRef, (void *)(poolBase + POOL_HEADER_SIZE),
						  poolSize - POOL_HEADER_SIZE, NULL);
		ASSERT(osaStatus == OS_SUCCESS);
	}
/*
 * Link Pools for Diag Reports
 */
	Handle = OsaCriticalSectionEnter(OsaMem_CriticalSectionHandle, NULL);

	pPoolHeader->pNextPool = OsaMem_FirstPool;
	OsaMem_FirstPool = pPoolHeader;
	strncpy(pPoolHeader->poolName, pName, MAX_NAME_LENGTH);
	pPoolHeader->poolName[MAX_NAME_LENGTH - 1] = '\0';

	OsaCriticalSectionExit(Handle, NULL);
	return OS_SUCCESS;
}
