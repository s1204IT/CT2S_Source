/*--------------------------------------------------------------------------------------------------------------------
(C) Copyright 2012, 2013 Marvell DSPC Ltd. All Rights Reserved.
-------------------------------------------------------------------------------------------------------------------*/

/***************************************************************************
*               MODULE IMPLEMENTATION FILE
****************************************************************************
*
* Filename: loadTable.h
*
* The OBM is responsible to copy image from flash to the DDR.
* It doesn't know about real image size and always copy the maximum 7MB.
* The problems:
*  - long time for copying (about 2 sec),
*  - all ERR/spy/debug buffers are overwriten.
*
* SOLUTION:
* Put the Image BEGIN and END address onto predefined area - offset_0x1C0 size 0x40
* Add the the text-signature to recognize are these addresses present in image or not.
* The signature is following the BEGIN/END and is next ":BEGIN:END:LOAD_TABLE_SIGNATURE"
* OBM should check signature in flash and if it is present MAY use the size=(END-BEGIN).
* If signature is invalid the default 7MB is used.
* The IMAGE_END region added into scatter file
*
******************************************************************************/

/*=======================================================================*/
/*        NOTE: This file may be used by OBM or WIN-CE                   */
/*=======================================================================*/
#ifndef _LOAD_TABLE_H_
#define _LOAD_TABLE_H_

#include "apcp_mmap.h"

#define INVALID_ADDRESS           0xBAD0DEAD

#define LOAD_TABLE_OFFSET       0
#define LOAD_TABLE_OLD_OFFSET   0x01C0
#define LT_APP2COM_DATA_LEN     48
#define LT_RELOC_LEN          0x20

typedef struct {
	unsigned int b2init;	/* branch to init routine */
	unsigned int init;	/* image init routine */
	unsigned int addrLoadTableRWcopy;	/* This CONST table is copied into RW area pointed by this */
	unsigned int ee_postmortem;	/* EE_PostmortemDesc addr (should be on offset 0xC */
	unsigned int numOfLife;	/* Increment for every restart (life) */

	unsigned int diag_p_diagIntIfQPtrData;	/*Pointer to DIAG-Pointer*/
	unsigned int diag_p_diagIntIfReportsList;	/*Pointer to DIAG-Pointer*/
	unsigned int spare_CCCC;	/*spare currently used with "CCCC" pattern*/

	unsigned int initState;	/*address of BSP_InitStateE COMM state*/
	unsigned int mipsRamBuffer;	/*address of BSP_InitStateE COMM state*/

	unsigned int apps2commDataLen;	/* one-direction Apps2com channel for generic command/data*/
	unsigned char apps2commDataBuf[LT_APP2COM_DATA_LEN];	/* Currently is not used*/

	/* AP image is relocateable,
	   i.e. can be loaded into an address different
	   from the address it has been linked to. */

	/*Handled by relocChunk code*/
	unsigned int relocChunk[LT_RELOC_LEN / 4];

	unsigned int loadDataBegin;
	unsigned int loadDataEnd;
	unsigned int execDataBegin;

	unsigned int extraDdrBegin;
	unsigned int extraDdrEnd;

	unsigned int ddrOverlapReuseEnabled;

	unsigned int roCodeInitBegin;
	unsigned int roCodeInitDdrEnd;

	unsigned int RFplugROcodeBegin;
	unsigned int RFplugROcodeEnd;
	unsigned int RFplugRWdataBegin;
	unsigned int RFplugRWdataEnd;
	unsigned int dspAreaBegin;
	unsigned int dspAreaEnd;
	/*---- later DSP free memory {*/
	unsigned int dspAreaTracerBegin;
	unsigned int dspAreaTracerSize;
	unsigned int dspAreaHeapBegin;
	unsigned int dspAreaHeapSize;
	unsigned int dspLoadTableAddr;
	/*---- later DSP free memory }*/

	unsigned int rwNonValuableBegin;
	unsigned int rwNonValuableSize;

	unsigned char filler[LOAD_TABLE_OLD_OFFSET - (4 * 24) - LT_APP2COM_DATA_LEN - LT_RELOC_LEN - (4 * 11)];

	unsigned int SHMEM_APPS2COM_AREA_Begin;	/* APPS writes only, COMM reads only*/
	unsigned int SHMEM_APPS2COM_AREA_End;	/*      SPECIAL_POOL*/
	char versionLT[4];	/*LT_VERSION*/
/*LOAD_TABLE_OLD_OFFSET=0x01C0*/
	unsigned int imageBegin;	/* image addresses are in HARBELL address space 0xD0??.????*/
	unsigned int imageEnd;	/* for BOERNE use conversion to 0xBF??.????*/
	char Signature[16];
	unsigned int sharedFreeBegin;
	unsigned int sharedFreeEnd;
	unsigned int ramRWbegin;
	unsigned int ramRWend;
	unsigned int imageId;	/* image ID can be APPS,COMM or RF-Plugin*/
	unsigned int ReliableDataBegin;
	unsigned int ReliableDataEnd;

	char OBM_VerString[8];	/*All "OBM" here are temp solution*/
	unsigned int OBM_Data32bits;
} LoadTableType;		/*total size 512bytes */

#define LT_VERSION                {'L', 'T', '0', '2'}
#define LOAD_TABLE_SIGNATURE_STR  "LOAD_TABLE_SIGN"	/*15 + zeroEnd */

extern unsigned int getCommImageBaseAddr(void);
extern void getAppComShareMemAddr(unsigned int *begin, unsigned int *end);

unsigned int ConvertPhysicalAddrToVirtualAddr(unsigned int PhysicalAddr);
int commImageTableInit(void);
void commImageTableFree(void);

#define MAP_PHYSICAL_TO_VIRTUAL_ADDRESS(pHYaddr) ConvertPhysicalAddrToVirtualAddr((unsigned int)(pHYaddr))

typedef enum {
	RESET_BASIC_NONE = 0,
	RESET_BASIC_1 = 0x7CAFE001,	/*while(1)*/
	RESET_BASIC_2 = 0x7CAFE002,	/*LogStream*/
	RESET_BASIC_3 = 0x7CAFE003	/*for future use*/
} enumResetType;

#ifndef _LOAD_TABLE_C_
extern LoadTableType loadTable;	/* do NOT use "const" in EXTERN prototype */
#endif

extern LoadTableType *pLoadTable;

/**************************************************************************
* CP DDR dump utilities making "[0za]"
*   - ROW dump into file
*   - "0"  add ZERO-PADs to file to obtine stripped file
*   - "z"  zip from CP DDR into file
*   - "a"  append to file
* NOTES:
*  These have STATIC variables inside and therefore are NOT reenterant
*  Basically called fron User-Interface BUT have minimum of syntax check
*  If some processes are working on the same file the result is unpredictable
*/

/* eehDumpCpMemory(... "0za", int step) where "int step" is:
 *  step=-1  open, write, close done in one same step
 *  step=1   open with SW-checking forced, write, NO-close
 *  step=0   NO-open, write, close
 */
int eehDumpCpMemory(const char *outfile, unsigned offs, unsigned size, char *opt, int step);
int eehDumpCpMemoryUtil(const char *outfile, int numParams, char *argv[], char *opt);
void eehCpZip_Full(const char *outfile);
void eehCpZip_StripMsaNonvaluable(const char *outfile);
void eehCpZip_StripNonvaluable(const char *outfile);

#endif /*_LOAD_TABLE_H_*/
