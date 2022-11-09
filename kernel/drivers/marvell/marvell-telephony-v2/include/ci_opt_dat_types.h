/*

 *(C) Copyright 2006, 2007 Marvell DSPC Ltd. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * All Rights Reserved
 *
 * FILENAME:   ci_opt_dat_types.h
 * PURPOSE:    Data types file for the CI STUB Data Plane
 */

#if !defined(_CI_OPT_DAT_TYPES_H_)
#define _CI_OPT_DAT_TYPES_H_

#define CI_DAT_TYPES_VER_MAJOR 1
#define CI_DAT_TYPES_VER_MINOR 0

#include "common_datastub.h"

#define CI_DAT_DL_HEADER_SIZE   4	/* MSL_UMD_PDU_HEADER_SIZE */

/*Note: ci_cfg.h contain same definition, if want to modify please modify both*/
/* CI data plane: maximum number of rx buffers to register */
#define CI_DAT_MAX_BUFS 10

typedef enum _CiDatPduType_ {
	CI_DATA_PDU = 1,
	CI_CTRL_PDU
} _CiDatPduType;
typedef unsigned char CiDatPduType;

/* TBD: CI client/server common internal structures
 * for DATA service, move to some where*/
typedef union _CiGroupHandle {
	unsigned int svcHandle;	/* Ci Service Handle */
	unsigned int sgHandle;
} CiGroupHandle;

typedef union _CiCallHandle {
	unsigned int reqHandle;
	unsigned int indHandle;
} CiCallHandle;

typedef struct CiAciInfo_struct {
	unsigned char dlHeader[CI_DAT_DL_HEADER_SIZE];	/* 4 bytes aligned */
	CiDatPduType type;
	unsigned char param1;	/* param1,2,3: pdu type depended parameters */
	unsigned char param2;
	unsigned char param3;
} CiAciInfo;

typedef struct CiStubInfo_struct {
	CiAciInfo info;		/* data, buf management PDUs */
	CiGroupHandle gHandle;	/* CiService, or CiSgOpaque Handle */
	CiCallHandle cHandle;	/* CiRequest or CiIndicate Handle */
	unsigned int pad[4];	/* Add pad to ensure that the size of CiStubInfo
				 * is 32 bytes aligned, which is required by
				 * Comm memory cache line */
} CiStubInfo;
/* Note: sizeof (CiAciPduInfo) has to be equal
 * to CI_DAT_ACI_HEADER_SIZE in ci_dat.h */

/* Begin of ACI optimization for data channel */

#define CI_DAT_ACI_HEADER_SIZE 32
/* DL Header(4) + Stub PDU Info(4) + service handle(4)
   + req/ind handle(4) + pad(16)  */

typedef struct CiDatPduHeader_struct {
	unsigned int connId;	/* link id: call id for CS connection;
				 *context id for PS connection */
	CiDatConnType connType;	/* connection type */
	CiDatType datType;	/* data type */
	CiBoolean isLast;	/* is this the last segmentation of the packet */
	unsigned char seqNo;	/* sequence number for each CI Dat Pdu */
	unsigned int datLen;	/* length of payload data to follow */
	unsigned char *pPayload;	/* point to the payload buffer for separated
					 * header buffer and payload buffer,
					 * NULL for contiguous pdu buffer */
} CiDatPduHeader;		/* This structure must be 32 bits aligned */

typedef struct CiDatPduInfo_struct {
	/* ACI header space: ACI will fill it */
	unsigned char aciHeader[CI_DAT_ACI_HEADER_SIZE];
	/* PDU Header description */
	CiDatPduHeader ciHeader;
	/* After this point start the payload data */
} CiDatPduInfo;			/* This structure must be 32 bits aligned */

typedef unsigned int CiDatBufSize;

/* Array of buffer pointers - configurable */
typedef unsigned char *CiDatBufArray[CI_DAT_MAX_BUFS];

typedef struct CiDatBufInfo_struct {
	/* number of available buffers */
	unsigned int numBufs;
	/* pointer to the array of data buffer pointers */
	CiDatBufArray *pBufArray;
	/* CiDatBufArray   bufArray; */
} CiDatBufInfo;

/* End of ACI optimization for data channel */
#endif /* _CI_DAT_TYPES_H_ */

/*                      end of ci_opt_dat_types.h
  --------------------------------------------------------------------------- */
