/*--------------------------------------------------------------------------------------------------------------------
(C) Copyright 2006, 2007 Marvell DSPC Ltd. All Rights Reserved.
-------------------------------------------------------------------------------------------------------------------*/

/***************************************************************************
*               MODULE IMPLEMENTATION FILE
****************************************************************************
*
* Filename: com_mem_mapping_kerenl.c
*
*
*
******************************************************************************/

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/aio.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/mman.h>
#include <linux/version.h>
#include "linux_types.h"

#include <mach/hardware.h>
#include <asm/irq.h>
#include <linux/pxa9xx_acipc_v2.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include "com_mem_mapping_kernel.h"
#include "loadTable.h"

#include "allocator.h"

#define COM_MEM_MAP_DEBUG 1

#ifdef COM_MEM_MAP_DEBUG
#define pr_debug_memm_lnx(fmt, arg...) printk(fmt, ##arg)
#define ERRMSG(fmt, arg...) printk(fmt, ##arg)
#else
#define pr_debug_memm_lnx(fmt, arg...)
#define ERRMSG
#endif

#define ALIGN_MALLOC_MASK				  (16 - 1)

/* APP--COM definitions refer in the "LoadTable.h" and "apcp_mmap.h" */
/* RW address is defined statically. No LOAD-TABLE needed in this case */
/* The LoadTable has 2 copies: in RO and in RW. */
/* The RO const is "always present", but RW is initialized by the COMM running. */
/* On the startup the APPS may pass over this code BEFORE the COMM made the copy, */
/* if RW should be used the static declaration is preferable then dynamic "look for table" */
#define LOAD_TABLE_IS_NOT_USED

/* for internals usage */
/*static unsigned int g_CommRObaseAddr                = (unsigned int)NULL; */
unsigned int g_CommRWArea = (unsigned int)NULL;

/* The COM read/write start address with his size */

/* virtual address used for address translation rutine */
unsigned int g_CommRWaddr = (unsigned int)NULL;
unsigned int g_CommRWAreaSizeInBytes = (unsigned int)NULL;

/* The COM read only start address with his size */
unsigned int g_CommROArea = (unsigned int)NULL;
unsigned int g_CommROAreaSizeInBytes = (unsigned int)NULL;

unsigned int g_CommPoolPhyBegin = (unsigned int)NULL;
unsigned int g_AppPoolPhyBegin = (unsigned int)NULL;

/* pool reference (this pool shoul be openeed by name from the OSA side) */
OsaRefT g_PoolXRef;

#define PAGE_OFFS_BITS(pgsz) ((unsigned long)(pgsz)-1)
#define PAGE_MASK_BITS(pgsz) (~PAGE_OFFS_BITS(pgsz))

/* Local routine definitions */
#if defined(LOAD_TABLE_IS_NOT_USED)
#define ImageTableInit()        TRUE
#define ImageTableFree() /**/
#else
unsigned char ImageTableInit(void);
void ImageTableFree(void);
#endif
unsigned char OpenCommWRarea(void);
unsigned char OpenCommRO(void);

void UnmapCompPhysical(void *addr, size_t length);

/*****************************************************
*
*	MapComSHMEMRegions
*
****************************************************/
unsigned int MapComSHMEMRegions(void)
{
	unsigned int bRes = FALSE;

	if (ImageTableInit() == FALSE)
		goto error;

	if (OpenCommWRarea() == FALSE)
		goto error;

	if (OpenCommRO() == FALSE)
		goto error;

	bRes = TRUE;
error:
	if (bRes == FALSE)
		ImageTableFree();
	return bRes;
}

/*****************************************************
*
*	MapComPhysicalToVirtualKern
*	map physical (COM based) memory range into kernel addrss space
*
****************************************************/
void *MapComPhysicalToVirtualKern(void *pAddr, unsigned int iSizeToMap)
{
	void *pRet;

	pRet = ioremap_nocache((unsigned int)ADDR_CONVERT(pAddr), iSizeToMap);

	pr_debug_memm_lnx("MapComPhysicalToVirtualKern  pAddr=0x%x, iSizeToMap = %d, AppsSizePtr = 0x%x\n",
			  (unsigned int)pAddr, iSizeToMap, (unsigned int)pRet);

	return pRet;
}

/*****************************************************
*
*	UnmapCompPhysical
*
****************************************************/
void UnmapCompPhysical(void *pAddr, size_t length)
{
	iounmap(pAddr);
}

#if !defined(LOAD_TABLE_IS_NOT_USED)
/*****************************************************
*	ImageTableInit
****************************************************/
unsigned char ImageTableInit(void)
{
	unsigned char bRes = TRUE;
	g_CommRObaseAddr = (unsigned int)MapComPhysicalToVirtualKern((void *)COMM_RO_ADDRESS,
								     LOAD_TABLE_OFFSET + sizeof(LoadTableType));
	if ((void *)g_CommRObaseAddr == NULL) {
		ERRMSG("%s: g_CommRObaseAddr is NULL\n", __func__);
		bRes = FALSE;
	}
	return bRes;
}

/*****************************************************
*	ImageTableFree
****************************************************/
void ImageTableFree(void)
{
	if ((void *)g_CommRObaseAddr != NULL) {
		UnmapCompPhysical((void *)g_CommRObaseAddr, LOAD_TABLE_OFFSET + sizeof(LoadTableType));
		g_CommRObaseAddr = (unsigned int)NULL;
	}
	if ((void *)g_CommRWaddr != NULL) {
		UnmapCompPhysical((void *)g_CommRWaddr, g_CommRWAreaSizeInBytes);
		g_CommRWaddr = (unsigned int)NULL;
	}
	return;
}
#endif /* !LOAD_TABLE_IS_NOT_USED */

/*****************************************************
*
*	OpenCommWRarea
*
*	Thes rutine opens whole Comm (Arbell) address space
*	for Apps Rx(SHMEM) transactions to Comm side
*
****************************************************/
unsigned char OpenCommWRarea(void)
{
#if !defined(LOAD_TABLE_IS_NOT_USED)
	LoadTableType *p;
	char signature[] = LOAD_TABLE_SIGNATURE_STR;
	unsigned char signatureFound = FALSE;

	/* Find loadTable */
	p = (LoadTableType *) (g_CommRObaseAddr + LOAD_TABLE_OFFSET);
	signatureFound = (memcmp(p->Signature, signature, sizeof(signature)) == 0);
	if (!signatureFound) {
		ERRMSG("%s: no Arbell image signature was found\n", __func__);
		return FALSE;
	}
	/* Take COM RW area begin/end descriptions from the loadTable */
	/* and make WIN-MAP for them */
	g_CommRWArea = (unsigned int)(p->ramRWbegin);
	g_CommPoolPhyBegin = (unsigned int)(p->ramRWbegin);
#else
	g_CommRWArea = (unsigned int)COMM_RW_ADDRESS;
	g_CommPoolPhyBegin = (unsigned int)COMM_RW_ADDRESS;
#endif /*LOAD_TABLE_IS_NOT_USED */

	g_CommRWAreaSizeInBytes = SPECIAL_COM_POOL_SIZE;
	g_CommRWaddr = (unsigned int)MapComPhysicalToVirtualKern((void *)g_CommPoolPhyBegin, g_CommRWAreaSizeInBytes);

	if ((void *)g_CommRWaddr == NULL) {
		ERRMSG("%s: unable to map Arbell RW region\n", __func__);
		return FALSE;
	}
	return TRUE;
}

/*****************************************************
*
*	OpenCommRO
*
*	This rutine opens APPS (Boerne) shared memory pool
*	in COMM (Arbell) RO memmory region
*
****************************************************/
unsigned char OpenCommRO(void)
{
	unsigned int alignGap;
	OsaMemCreateParamsT memParams;

	g_CommROAreaSizeInBytes = SPECIAL_APP_POOL_SIZE;
	g_AppPoolPhyBegin = (unsigned int)(APP_POOL_START_ADDR);

	g_CommROArea =
	    (unsigned int)MapComPhysicalToVirtualKern((void *)(APP_POOL_START_ADDR), g_CommROAreaSizeInBytes);
	if (g_CommROArea == (unsigned int)NULL) {
		ERRMSG("%s: unable to init Berny (Comm) side pool memory\n", __func__);
		return FALSE;
	}

	alignGap = (((unsigned int)g_CommROArea + ALIGN_MALLOC_MASK) & ~ALIGN_MALLOC_MASK) - (unsigned int)g_CommROArea;

	/*link mapped memory to OSA pool strucrture */
	memParams.poolBase = (void *)(((unsigned int)g_CommROArea + ALIGN_MALLOC_MASK) & ~ALIGN_MALLOC_MASK);
	memParams.poolSize = (SPECIAL_APP_POOL_SIZE - alignGap);
	memParams.name = SHMEM_DEFAULT_POOL_NAME;
	memParams.bSharedForIpc = TRUE;

	/* call to OSA */
	OsaMemCreatePool(&g_PoolXRef, &memParams);

	return TRUE;
}

/*****************************************************
*
*	ConvertPhysicalToVirtualAddr
*
*   This routine converts COM physical address to APPS
*   virtual mapped address
*
****************************************************/
unsigned int ConvertPhysicalToVirtualAddr(unsigned int PhysicalAddr)
{
	unsigned int ret_Addr = 0;

	/* APPS pool */
	if ((PhysicalAddr >= APP_POOL_START_ADDR)
	    && (PhysicalAddr < (APP_POOL_START_ADDR + SPECIAL_APP_POOL_SIZE)))
		ret_Addr = g_CommROArea + (PhysicalAddr - APP_POOL_START_ADDR);
	else			/* COM RW area */
		ret_Addr = g_CommRWaddr + (PhysicalAddr - g_CommRWArea);

/*      printk("ret_Addr 0x%x\n", ret_Addr); */
	return ret_Addr;
}

EXPORT_SYMBOL(ConvertPhysicalToVirtualAddr);

/*****************************************************
*
*	ConvertVirtualToPhysicalAddr
*
*
*   This routine converts APPS virtual mapped address
* 	to COM physical address
*
****************************************************/
unsigned int ConvertVirtualToPhysicalAddr(unsigned int VirtualAddr)
{
	unsigned int ret_Addr = VirtualAddr;

/*      printk("VirtualAddr =0x%x\n", VirtualAddr); */

	/* Apps pool */
	if (VirtualAddr >= g_CommROArea && VirtualAddr <= g_CommROArea + g_CommROAreaSizeInBytes) {
		ret_Addr = CONVERT_BERARNY_PH_TO_ARB_PH(APP_POOL_START_ADDR + (VirtualAddr - g_CommROArea));
/*              printk("1 ConvertVirtualToPhysicalAddr offset %d, APP_POOL_START_ADDR=0x%x, VirtualAddr=0x%x ret_Addr = 0x%x\n", VirtualAddr - g_CommROArea, APP_POOL_START_ADDR, VirtualAddr, ret_Addr); */
	}
	/* com size address (NVM style messages) */
	else if (VirtualAddr) {
		ret_Addr = g_CommRWArea + (VirtualAddr - g_CommRWaddr);
/*              printk("2 ConvertVirtualToPhysicalAddr VirtualAddr=0x%x ret_Addr = 0x%x\n", VirtualAddr, ret_Addr); */
	}
	return ret_Addr;
}
