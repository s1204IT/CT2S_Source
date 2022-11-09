/*-------------------------------------------------------------
(C) Copyright 2006, 2007 Marvell DSPC Ltd. All Rights Reserved.
---------------------------------------------------------------*/
/***************************************************************************
*               MODULE IMPLEMENTATION FILE
****************************************************************************
*
* Filename: apcp_mmap.h
* This file provides memory map definitions for CP area addresses.
* THIS FILE CANNOT USE ANY TYPES DEFINED IN global_types.h OR INCLUDE
* ADDITIONAL FILES.
* REASON: any kernel module should be able to #include this without
* the need for extra include paths etc.
*
*/
#ifndef APCP_MMAP_H
#define APCP_MMAP_H

#if !defined(ADDR_CONVERT)	/* May be defined in EE_Postmortem.h or loadTable.h */
#undef ADDR_CONVERT
#endif

#define TAVOR_ADDR_SELECT_MASK            0xFF000000
#define PHY_OFFSET(addr) ((addr)&~TAVOR_ADDR_SELECT_MASK)

/* Get the CP area address from kernel: this should be the top 16MB
of the DDR bank#0 LOWEST alias, i.e. 0x80000000+actual bank size-16MB */
extern unsigned cp_area_addr(void);	/* arch/arm/mach-pxa/pxa930.c */
/* USE OF THIS IN USER SPACE IS PROHIBITED,
 will result in unresolved external error during link */

#define ADDR_CONVERT(ADDR)  ((unsigned char *)(((unsigned int)(ADDR) & ~TAVOR_ADDR_SELECT_MASK) | (cp_area_addr())))

#define CONVERT_BERARNY_PH_TO_ARB_PH(PVIRTADDR) (((PVIRTADDR) && ((unsigned int)(PVIRTADDR) >= 0x41A00000)) ? \
												 (unsigned int)(PVIRTADDR) + 0x11000000  :  (unsigned int)(PVIRTADDR))

#define ADDR_CONVERT_MSA_TO_AP(ADDR)    ((unsigned int)(ADDR) & 0x4FFFFFFF)
#define ADDR_CONVERT_AP_TO_MSA(ADDR)    ((unsigned int)(ADDR) | 0xD0000000)

/* Hard-coded addresses of COMM image: RO, RW, SHMEM and SPECIAL AP-to-CP SHMEM segments
   may be fetched from COM-LoadTable
   but these MACROs are used for every address translation and
   Variables causes penalty of run-time performance
*/
/* ESHEL (26M/12M) or ADIR  APPS/COMM_0x40 memory map */
#define COMM_BASE_ADDR   0x40000000
/* #define COMM_RW_ADDRESS  0x40200000*/
#define COMM_RW_ADDRESS  0x4018A000
#define COMM_RO_ADDRESS  0x41300000
#define COMM_RW_MAX_SIZE 0x01300000
/* #define SPECIAL_COM_POOL_SIZE     ((19-2) * 1024 * 1024) //Whole 0x40200...0x413 */
#define SPECIAL_COM_POOL_SIZE     (COMM_RO_ADDRESS - COMM_RW_ADDRESS)	/* Whole 0x40200...0x413 */
#define SPECIAL_APP_POOL_SIZE     (64 * 1024)	/* SHMEM memory (APPS to COMM) = 64k */
#define APP_POOL_START_ADDR       (0x40300000+0x20000+0x50000)	/* DSP+nonCache+RF */
#define LOAD_TABLE_FIX_ADDR       0x40300000

/* "Low-Level control" AP-CP shared pool.
 * On CP side this pool exists for all projects as Non-Cacheable (NC).
 * Data-structure in that pool depends upon project requirements.
 * One of "users" is Frequency-Table
 */
#define COMM_CTRL_POOL             0x40300200
#define COMM_CTRL_POOL_SIZE        (32*1024)

#endif /*APCP_MMAP_H */
