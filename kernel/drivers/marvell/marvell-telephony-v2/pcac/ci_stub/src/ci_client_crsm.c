/*------------------------------------------------------------
(C) Copyright [2009] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/*=========================================================================

  File Name:	ci_client_crsm.c

  Description:  This is the implementation of CI transfer Interface functions

  Revision: 	Phillip Cho, 0.1

Marvell CONFIDENTIAL
Copyright 2009 Marvell Corporation All Rights Reserved.
The source code contained or described herein and all documents related to the source code ("Material") are owned
by Intel Corporation or its suppliers or licensors. Title to the Material remains with Intel Corporation or
its suppliers and licensors. The Material contains trade secrets and proprietary and confidential information of
Intel or its suppliers and licensors. The Material is protected by worldwide copyright and trade secret laws and
treaty provisions. No part of the Material may be used, copied, reproduced, modified, published, uploaded, posted,
transmitted, distributed, or disclosed in any way without Intel's prior express written permission.

No license under any patent, copyright, trade secret or other intellectual property right is granted to or
conferred upon you by disclosure or delivery of the Materials, either expressly, by implication, inducement,
estoppel or otherwise. Any license under such intellectual property rights must be express and approved by
Intel in writing.

Unless otherwise agreed by Intel in writing, you may not remove or alter this notice or any other notice embedded
in Materials by Intel or Intel's suppliers or licensors in any way.
  ======================================================================== */
#include <linux/string.h>

#include "ci_api_types.h"
#include "ci_api.h"
#include "ci_stub.h"
#include "osa.h"
#include "ci_mem.h"
#include "ci_trace.h"
#include "ci_sim.h"
#include "ci_msg.h"
#include "ci_pb.h"

#include "ci_dat.h"
#include "ci_opt_dat_types.h"

#include "ci_trace.h"
#include "ci_err.h"

#include "acipc_data.h"

#include "com_mem_mapping_user.h"
#include "common_datastub.h"
#include <stdio.h>
#include <stdlib.h>

		/*Michal Bukai */
unsigned char gPBMaxAlphaIdLength = 14;
#define EXT2_READ_OPER		(CI_SIM_PRIM_LAST_COMMON_PRIM + 1)
#define EXT2_UPDATE_OPER	(CI_SIM_PRIM_LAST_COMMON_PRIM + 2)

CrsmCciAction *head;
extern CiServiceHandle ciATciSvgHandle[];

/*Michal bukai - more Vritual SIM handler functions - END*/
#define SET_SIM_CMD_LEN_AND_TERM(__CnfPtr, __Len)\
			(__CnfPtr)->cnf.len = (__Len);\
			(__CnfPtr)->cnf.data[(__Len)-2] = 144;\
			(__CnfPtr)->cnf.data[(__Len)-1] = 0

/*Debug trace preperation */
#define PRIM_LOG_DEBUG_LEVEL 4
extern void MTIL_LOGCAT(int level, const char *fmt, ...);
#define CRSM_TRACE(level, fmt, args...)  										MTIL_LOGCAT(PRIM_LOG_DEBUG_LEVEL, fmt, ##args)

/*Michal Bukai - global parameters to enable FDN PB with long numbers operaton (involve both Extension2 and FDN SIM file) */
typedef enum PB_RECORD_STATUS {
	PB_RECORD_EMPTY = 0,
	PB_RECORD_TABLE,
	PB_RECORD_UPDATE,
} _ciPbRecordStatus;

typedef unsigned char ciPbRecordStatus;
typedef unsigned short ciPbRecordIndex;

typedef struct ciFDNEntryWithExt_struct {
	ciPbRecordStatus status;	/*record update status */
	ciPbRecordIndex FDNIndex;   /* FDN PB entry index */
	ciPbRecordIndex ExtIndexReq;	/* EXT2 index requested by App */
} ciFDNEntryWithExt;

typedef struct ciEXT2Entry_struct {
	ciPbRecordStatus status;	/*record update status */
	ciPbRecordIndex ExtIndexReq;	/* EXT2 index requested by App */
	unsigned char pRecord[13];  /* EXT2 data */
} ciEXT2Entry;

static ciEXT2Entry gEXT2Table[100];
static ciFDNEntryWithExt gFDNWithExtTable[100];

/* fix CRSM assert - start */
/* remove static */
int CiStubUsesDynMem;
/* fix CRSM assert - end */

void SetCiStubMemMode(int mode)
{
	CiStubUsesDynMem = mode;
}

char gPin2[CI_MAX_PASSWORD_LENGTH];

void SetPin2(char *pin2)
{
	memcpy(gPin2, pin2, sizeof(gPin2));
}

/*Michal bukai - more Vritual SIM handler functions - START*/
static unsigned char ReadSemiOctetData(unsigned char octetPosition, unsigned char *numberList,
					   unsigned char maxDigits, unsigned char *basePtr, unsigned char checkForPad)
{
	unsigned char i = 0;
	unsigned char octetPos = octetPosition;
	unsigned char loSemiOctet = TRUE;
	unsigned char moreDigitsToRead = TRUE;

	while (moreDigitsToRead) {
		if (loSemiOctet) {
			/* process a semi octet in the low nybble position */
			numberList[i] = basePtr[octetPos] & 0x0F;
			loSemiOctet = FALSE;
		} else {
			/* process a semi octet in the high nybble position */
			numberList[i] = (basePtr[octetPos] & 0xF0) >> 4;
			loSemiOctet = TRUE;
			octetPos++;
		}

		if (numberList[i] < 0x0a) {
			numberList[i] += 0x30;
		} else {
			if (numberList[i] == 0x0a) {	/*bcd value 0xa will be translate to '*' charecter */
				numberList[i] = 0x2a;
			} else if (numberList[i] == 0x0b) { /*bcd value 0xb will be translate to '#' charecter */
				numberList[i] = 0x23;
			} else if (numberList[i] == 0x0c) { /*bcd value 0xc will be translate to 'p' charecter - DTMF */
				numberList[i] = 0x70;
			} else if (numberList[i] == 0x0d) { /*bcd value 0xd will be translate to '?' charecter - Wild */
				numberList[i] = 0x3f;
			} else if (numberList[i] == 0x0F) {
				if (i + 1 == maxDigits) {
					numberList[i] = 0x00;
					return i;
				} else
					return 0;
			} else
				return 0;

		}

		if (((checkForPad == TRUE) && (numberList[i] == 0xF)) || (i + 1 == maxDigits)) {
			moreDigitsToRead = FALSE;
		} else {
			i++;
		}
	}

	/* return actual number of digits read */
	return i + 1;
}

static unsigned char ReadAddressInfo(CiAddressInfo *addr, unsigned char *basePtr)
{
	unsigned char octetPos = 0;
	unsigned char ieLength;
	unsigned char octetsRead;
	unsigned char maxDigits;

	ieLength = basePtr[octetPos];
	octetPos++;

	addr->AddrType.NumType = (unsigned char)((basePtr[octetPos] >> 4) & 0x07);
	addr->AddrType.NumPlan = (unsigned char)(basePtr[octetPos] & 0x0f);
	octetPos++;

	maxDigits = (ieLength - 1) * 2;

	addr->Length = ReadSemiOctetData(octetPos, (unsigned char *)&addr->Digits[0], maxDigits, basePtr, TRUE);
	if (addr->Length == 0) {
		return 0;
	}
	octetsRead = (ieLength + 1);

	return octetsRead;
}

static unsigned char ReadMoreAddressInfo(CiAddressInfo *addr, unsigned char *basePtr)
{
	unsigned char octetPos = 0, len = 0;
	unsigned char ieLength;
	unsigned char octetsRead;
	unsigned char maxDigits = 22;

	ieLength = basePtr[octetPos];
	if (ieLength < 12) {
		maxDigits = ieLength * 2;
	}
	if (addr->Length > CI_MAX_ADDRESS_LENGTH - maxDigits) {
		maxDigits = CI_MAX_ADDRESS_LENGTH - addr->Length;
	}
	octetPos++;

	len = ReadSemiOctetData(octetPos, (unsigned char *)&addr->Digits[addr->Length], maxDigits, basePtr, TRUE);
	addr->Length += len;
	octetsRead = ieLength;
	return octetsRead;
}

static void string2BCD(unsigned char *pp, unsigned char *tempBuf, unsigned char length)
{
	int i;
	unsigned char lo_nibble, hi_nibble;
	for (i = 0; i < length; i += 2) {
		lo_nibble = tempBuf[i + 0];
		hi_nibble = tempBuf[i + 1];
		if ((i + 1) == length) {
			hi_nibble = 0x0f;
			lo_nibble = tempBuf[i + 0];
		}
		if ((lo_nibble >= 0x30) && (lo_nibble <= 0x39))
			lo_nibble -= 0x30;
		else if (lo_nibble == 0x2a) /* charecter '*' will be translate to bcd value 0xa  */
			lo_nibble = 0x0a;
		else if (lo_nibble == 0x23) /* charecter '#' will be translate to bcd value 0xb  */
			lo_nibble = 0x0b;
		else if (lo_nibble == 0x70) /* charecter 'p' (DTMF) will be translate to bcd value 0xc  */
			lo_nibble = 0x0c;
		else if (lo_nibble == 0x3f) /* charecter '?' (Wild) will be translate to bcd value 0xd  */
			lo_nibble = 0x0d;
		else
			lo_nibble = 0x0f;

		if ((hi_nibble >= 0x30) && (hi_nibble <= 0x39))
			hi_nibble -= 0x30;
		else if (hi_nibble == 0x2a) /* charecter '*' will be translate to bcd value 0xa  */
			hi_nibble = 0x0a;
		else if (hi_nibble == 0x23) /* charecter '#' will be translate to bcd value 0xb  */
			hi_nibble = 0x0b;
		else if (hi_nibble == 0x70) /* charecter 'p' (DTMF) will be translate to bcd value 0xc  */
			hi_nibble = 0x0c;
		else if (hi_nibble == 0x3f) /* charecter '?' (Wild) will be translate to bcd value 0xd  */
			hi_nibble = 0x0d;
		else
			hi_nibble = 0x0f;
		pp[i / 2] = (lo_nibble) | (hi_nibble << 4);
	}
}

static void getString(unsigned char *dest, unsigned char *src, unsigned char *len)
{
	unsigned char i = 0;
	while (src[i] != EMPTY) {
		dest[i] = src[i];
		i++;
	}
	*len = i;
}

static void judgeOp(unsigned char *pData, unsigned char *i, unsigned char j)
{
	unsigned char temp;
	temp = *i;
	while (temp < j) {
		if (pData[temp] == EMPTY)
			temp++;
		else
			break;
	}
	*i = temp;
}

static int handleReturn(unsigned short ret)
{
	if (ret == 0) {
		return CIRC_SUCCESS;
	} else {
		/* When CI Request is falied */ ;
		return CIRC_FAIL;
	}
}

static CiReturnCode GetRecordIndex(unsigned char p1, unsigned char p2)
{
	switch (p2) {
	case 3:
		if (0 == p1)
			p1 = LAST_RECORD;
		if (FIRST_RECORD == p1)
			p1 = INVALID_INDEX;
		if (0 != p1 && FIRST_RECORD != p1)
			break;
	case 2:
		if (0 == p1)
			p1 = 1;
		else if (LAST_RECORD == p1)
			p1 = INVALID_INDEX;
		else
			p1 = p1 + 1;
		break;
	default:
		break;
	}
	return p1;
}

static int GetSIMEfFileId(CiSimFilePath path)
{
	int iFileId = 0;
	int temp = 0;
	unsigned char tmpByte;
	int i;

	for (i = 0; i < 2; i++) {
		if (path.len > i) {
			tmpByte = path.valStr[path.len - 1 - i];

			if (((tmpByte & 0x0f) <= 0x0f) && ((tmpByte & 0x0f) >= 0x00)) {
				temp = (tmpByte & 0x0f) + 0;
				if (i)
					temp = temp * 256;
				iFileId += temp;
			}
			if ((tmpByte >> 4) < 0x0f) {
				temp = (tmpByte >> 4) + 0;
				temp = temp * 16;
				if (i)
					temp = temp * 256;
				iFileId += temp;
			}
		}
	}

	return iFileId;
}

static void freeCciActionlist(CrsmCciAction *tem)
{
	CrsmCciAction *ptemp = head;
	if (head == tem)
		head = tem->next;
	else {
/*  	this old code must work the same as the below while, changed due to coverity check of NULL:
		while(ptemp->next!=tem&&ptemp!=NULL)
			ptemp = ptemp ->next;*/
		while (ptemp != NULL) {
			if (ptemp->next != tem)
				ptemp = ptemp->next;	/*not found - move on */
			else
				break;  /*item found */
		}

		if (ptemp != NULL)
			ptemp->next = tem->next;
		else {
			CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_ERROR,
				  "ERROR: freeCciActionlist - tem not found!!!");
		}
	}
	CI_MEM_FREE(tem->ci_request);
	CI_MEM_FREE(tem);
}

static void add2CciActionlist(CrsmCciAction *tem)
{
	CrsmCciAction *temp = head;
	tem->next = NULL;
	if (head == NULL) {
		head = tem;
	} else {
		while (temp->next != NULL) {
			temp = temp->next;
		}
		temp->next = tem;
	}
}

CiSimPrimGenericCmdReq gSimPrimGenericCmdReq;

CiReturnCode ciRequest_Transfer(CiServiceHandle svchandle,
				CiPrimitiveID primId, CiRequestHandle crsm_reqHandle, void *paras)
{
	int cmd = 0;
	CiReturnCode ret = CIRC_FAIL;
	int rc = -1;
	int iFileId = 0;
	unsigned char judge = 1;
	unsigned short iIndex = 0;
	unsigned short iPBIndex = 0;
	unsigned char deleteOp = FALSE;
	unsigned char updateOp = FALSE;

	CiSimPrimGenericCmdReq *GenSimCmd_crsm = &gSimPrimGenericCmdReq;

	memcpy(GenSimCmd_crsm, paras, sizeof(CiSimPrimGenericCmdReq));

	CI_MEM_FREE(paras);

	paras = GenSimCmd_crsm;

	iFileId = GetSIMEfFileId(GenSimCmd_crsm->path);
	cmd = GenSimCmd_crsm->instruction;
	void *temp;

	if (EF_SMS == iFileId || EF_FDN == iFileId || EF_EXT2 == iFileId) {
		/*Michal Bukai - Extension2 support - update FDN PB with long numbers */
		if (iFileId == EF_EXT2) {
			if (READ_RECORD == cmd) {
				CrsmCciAction *pnode;
				pnode = (CrsmCciAction *) CI_MEM_ALLOC(sizeof(CrsmCciAction));
				if (pnode != NULL) {
					pnode->cci_confirm = EXT2_READ_OPER;
					pnode->crsm_reshandle = crsm_reqHandle;
					pnode->svshandle = svchandle;
					pnode->ci_request = NULL;
					pnode->GenSimCmd_crsm = GenSimCmd_crsm;
					add2CciActionlist(pnode);
				} else
					return CIRC_FAIL;
			}
			if (UPDATE_RECORD == cmd) {
				CrsmCciAction *pnode;
				pnode = (CrsmCciAction *) CI_MEM_ALLOC(sizeof(CrsmCciAction));
				if (pnode != NULL) {
					unsigned char iLen = 13;
					unsigned char bExtExist = FALSE;
					iPBIndex = GetRecordIndex(GenSimCmd_crsm->param1, GenSimCmd_crsm->param2);
					if (GenSimCmd_crsm->length < 14)
						iLen = GenSimCmd_crsm->length;

					/*just update EXT2 table - record will be update when FDN will be update (via CI_PB) - if not update via FDN, this record will not be updatein EXT2 file */
					/*Record exists - Update record data */
					for (iIndex = 0; iIndex < 100; iIndex++) {
						if (gEXT2Table[iIndex].ExtIndexReq == iPBIndex) {
							memset(gEXT2Table[iIndex].pRecord, EMPTY, 13);

							memcpy(gEXT2Table[iIndex].pRecord, GenSimCmd_crsm->data, iLen);
							gEXT2Table[iIndex].status = PB_RECORD_TABLE;
							bExtExist = TRUE;
							iIndex = 100;
						}
					}

					/*Record doesn't exist - enter new */
					if (!bExtExist) {
						for (iIndex = 0; iIndex < 100; iIndex++) {
							if (gEXT2Table[iIndex].status == PB_RECORD_EMPTY) {
								gEXT2Table[iIndex].ExtIndexReq = iPBIndex;
								gEXT2Table[iIndex].status = PB_RECORD_TABLE;
								memset(gEXT2Table[iIndex].pRecord, EMPTY, 13);

								memcpy(gEXT2Table[iIndex].pRecord, GenSimCmd_crsm->data,
									   iLen);
								iIndex = 100;
							}
						}
					}
					/*send fake request */

					GenSimCmd_crsm->instruction = CI_SIM_READ_RECORD;
					GenSimCmd_crsm->length = 0;
					GenSimCmd_crsm->responseExpected = TRUE;
					GenSimCmd_crsm->responseLength = 0x0d;

					pnode->cci_confirm = EXT2_UPDATE_OPER;
					pnode->crsm_reshandle = crsm_reqHandle;
					pnode->svshandle = svchandle;
					pnode->ci_request = NULL;
					add2CciActionlist(pnode);

				} else
					return CIRC_FAIL;
			}
			if (GET_RESPONSE == cmd) {
				GenSimCmd_crsm->instruction = CI_SIM_SELECT;
				GenSimCmd_crsm->param1 = 0;
				GenSimCmd_crsm->param2 = 0;
				GenSimCmd_crsm->length = 2;
				GenSimCmd_crsm->data[0] = (iFileId >> 8) & 0xFF;
				GenSimCmd_crsm->data[1] = (iFileId) & 0xFF;
				GenSimCmd_crsm->responseExpected = TRUE;
				GenSimCmd_crsm->responseLength = 255;
				if (GenSimCmd_crsm->path.len > 1) { /* remove the file id the last 2 bytes */
					GenSimCmd_crsm->path.valStr[GenSimCmd_crsm->path.len - 1] = '\0';
					GenSimCmd_crsm->path.valStr[GenSimCmd_crsm->path.len - 2] = '\0';
					GenSimCmd_crsm->path.len -= 2;
				}
			}
			return ciRequest_1(svchandle, primId, crsm_reqHandle, GenSimCmd_crsm);
		}
		if (iFileId == EF_SMS) {
			if (READ_RECORD == cmd) {
				/*Michal Bukai - in case of Read SMS operetion, CRSM command will not Mapped to CI_MSG (CQ00056743) */

				return ciRequest_1(svchandle, primId, crsm_reqHandle, paras);
			}

			if (UPDATE_RECORD == cmd) {
				judgeOp(GenSimCmd_crsm->data, &judge, GenSimCmd_crsm->length);

				if (judge == GenSimCmd_crsm->length) {
					CrsmCciAction *pnode;
					pnode = (CrsmCciAction *) CI_MEM_ALLOC(sizeof(CrsmCciAction));
					if (pnode != NULL) {
						pnode->cci_confirm = CI_MSG_PRIM_DELETE_MESSAGE_CNF;
						pnode->crsm_reshandle = crsm_reqHandle;
						pnode->svshandle = svchandle;
						pnode->ci_request =
							(void *)CI_MEM_ALLOC(sizeof(CiMsgPrimSelectStorageReq));
						pnode->GenSimCmd_crsm = GenSimCmd_crsm;
						if (pnode->ci_request != NULL) {
							((CiMsgPrimSelectStorageReq *) (pnode->ci_request))->storage =
								CI_MSG_STORAGE_SM;
							((CiMsgPrimSelectStorageReq *) (pnode->ci_request))->type =
								CI_MSG_STRGOPERTYPE_READ_DELETE;
							add2CciActionlist(pnode);
						} else {
							CI_MEM_FREE(pnode);
							return CIRC_FAIL;
						}
					} else
						return CIRC_FAIL;

					temp = pnode->ci_request;
					pnode->ci_request = NULL;

					ret = ciRequest_1(ciATciSvgHandle[CI_SG_ID_MSG], CI_MSG_PRIM_SELECT_STORAGE_REQ,
							  crsm_reqHandle, temp);

					if (ret || !CiStubUsesDynMem)
						CI_MEM_FREE(temp);

					rc = handleReturn(ret);
					return rc;

				}

				if (judge < GenSimCmd_crsm->length) {
					CrsmCciAction *pnode;
					pnode = (CrsmCciAction *) CI_MEM_ALLOC(sizeof(CrsmCciAction));
					if (pnode != NULL) {
						pnode->cci_confirm = CI_MSG_PRIM_WRITE_MSG_WITH_INDEX_CNF;
						pnode->crsm_reshandle = crsm_reqHandle;
						pnode->svshandle = svchandle;
						pnode->ci_request =
							(void *)CI_MEM_ALLOC(sizeof(CiMsgPrimSelectStorageReq));
						pnode->GenSimCmd_crsm = GenSimCmd_crsm;
						if (pnode->ci_request != NULL) {
							((CiMsgPrimSelectStorageReq *) (pnode->ci_request))->storage =
								CI_MSG_STORAGE_SM;
							((CiMsgPrimSelectStorageReq *) (pnode->ci_request))->type =
								CI_MSG_STRGOPERTYPE_WRITE_SEND;
							add2CciActionlist(pnode);
						} else {
							CI_MEM_FREE(pnode);
							return CIRC_FAIL;
						}
					} else
						return CIRC_FAIL;

					temp = pnode->ci_request;
					pnode->ci_request = NULL;

					ret = ciRequest_1(ciATciSvgHandle[CI_SG_ID_MSG], CI_MSG_PRIM_SELECT_STORAGE_REQ,
							  crsm_reqHandle, temp);

					if (ret || !CiStubUsesDynMem)
						CI_MEM_FREE(temp);

					rc = handleReturn(ret);
					return rc;
				}
			}

			/* START: Michal Bukai - Convert Response command to Select command */
			if (GET_RESPONSE == cmd) {
				GenSimCmd_crsm->instruction = CI_SIM_SELECT;
				GenSimCmd_crsm->param1 = 0;
				GenSimCmd_crsm->param2 = 0;
				GenSimCmd_crsm->length = 2;
				GenSimCmd_crsm->data[0] = (iFileId >> 8) & 0xFF;
				GenSimCmd_crsm->data[1] = (iFileId) & 0xFF;
				GenSimCmd_crsm->responseExpected = TRUE;
				GenSimCmd_crsm->responseLength = 255;
				if (GenSimCmd_crsm->path.len > 1) { /* remove the file id the last 2 bytes */
					GenSimCmd_crsm->path.valStr[GenSimCmd_crsm->path.len - 1] = '\0';
					GenSimCmd_crsm->path.valStr[GenSimCmd_crsm->path.len - 2] = '\0';
					GenSimCmd_crsm->path.len -= 2;
				}
				return ciRequest_1(svchandle, primId, crsm_reqHandle, GenSimCmd_crsm);
			}
			/* END: Michal Bukai - Convert Response command to Select command */
		}

		if (iFileId == EF_FDN) {
			if (READ_RECORD == cmd) {
				CrsmCciAction *pnode;
				pnode = (CrsmCciAction *) CI_MEM_ALLOC(sizeof(CrsmCciAction));
				if (pnode != NULL) {
					pnode->cci_confirm = CI_PB_PRIM_READ_PHONEBOOK_ENTRY_CNF;
					pnode->crsm_reshandle = crsm_reqHandle;
					pnode->svshandle = svchandle;
					pnode->ci_request = (void *)CI_MEM_ALLOC(sizeof(CiPbPrimSelectPhonebookReq));
					pnode->GenSimCmd_crsm = GenSimCmd_crsm;
					if (pnode->ci_request != NULL) {
						((CiPbPrimSelectPhonebookReq *) (pnode->ci_request))->PbId =
							CI_PHONEBOOK_FD;
						strcpy((char
							*)((CiPbPrimSelectPhonebookReq *) (pnode->
											   ci_request))->password.data,
							   gPin2);
						((CiPbPrimSelectPhonebookReq *) (pnode->ci_request))->password.len =
							(unsigned char)strlen(gPin2);
						add2CciActionlist(pnode);
					} else {
						CI_MEM_FREE(pnode);
						return CIRC_FAIL;
					}
				} else
					return CIRC_FAIL;

				temp = pnode->ci_request;
				pnode->ci_request = NULL;

				ret = ciRequest_1(ciATciSvgHandle[CI_SG_ID_PB], CI_PB_PRIM_SELECT_PHONEBOOK_REQ,
						  crsm_reqHandle, temp);

				if (ret || !CiStubUsesDynMem)
					CI_MEM_FREE(temp);

				rc = handleReturn(ret);
				return rc;
				/* handle the return value */
			}

			if (UPDATE_RECORD == cmd) {
				if (GenSimCmd_crsm->length < 14)
					return CIRC_FAIL;   /*pdu dat must contain at least 14 bytes */

				unsigned char AlphaIDLen = GenSimCmd_crsm->length - 14;
				judge = 0;

				judgeOp(GenSimCmd_crsm->data, &judge, AlphaIDLen);

				if (judge == AlphaIDLen)
					deleteOp = TRUE;
				else {
					judge = AlphaIDLen;
					judgeOp(GenSimCmd_crsm->data, &judge, GenSimCmd_crsm->length);

					if (judge == GenSimCmd_crsm->length)
						deleteOp = TRUE;
					else
						updateOp = TRUE;
				}
				if (deleteOp == TRUE) {
					CrsmCciAction *pnode;
					pnode = (CrsmCciAction *) CI_MEM_ALLOC(sizeof(CrsmCciAction));
					if (pnode != NULL) {
						pnode->cci_confirm = CI_PB_PRIM_DELETE_PHONEBOOK_ENTRY_CNF;
						pnode->crsm_reshandle = crsm_reqHandle;
						pnode->svshandle = svchandle;
						pnode->ci_request =
							(void *)CI_MEM_ALLOC(sizeof(CiPbPrimSelectPhonebookReq));
						pnode->GenSimCmd_crsm = GenSimCmd_crsm;
						if (pnode->ci_request != NULL) {
							((CiPbPrimSelectPhonebookReq *) (pnode->ci_request))->PbId =
								CI_PHONEBOOK_FD;
							strcpy((char
								*)((CiPbPrimSelectPhonebookReq *) (pnode->
												   ci_request))->password.
								   data, gPin2);
							((CiPbPrimSelectPhonebookReq *) (pnode->ci_request))->
								password.len = (unsigned char)strlen(gPin2);

							add2CciActionlist(pnode);
						} else {
							CI_MEM_FREE(pnode);
							return CIRC_FAIL;
						}
					} else
						return CIRC_FAIL;

					temp = pnode->ci_request;
					pnode->ci_request = NULL;

					ret = ciRequest_1(ciATciSvgHandle[CI_SG_ID_PB], CI_PB_PRIM_SELECT_PHONEBOOK_REQ,
							  crsm_reqHandle, temp);

					if (ret || !CiStubUsesDynMem)
						CI_MEM_FREE(temp);

					rc = handleReturn(ret);
					return rc;
				}
				if (updateOp == TRUE) {
					CrsmCciAction *pnode;
					pnode = (CrsmCciAction *) CI_MEM_ALLOC(sizeof(CrsmCciAction));
					if (pnode != NULL) {
						pnode->cci_confirm = CI_PB_PRIM_REPLACE_PHONEBOOK_ENTRY_CNF;
						pnode->crsm_reshandle = crsm_reqHandle;
						pnode->svshandle = svchandle;
						pnode->ci_request =
							(void *)CI_MEM_ALLOC(sizeof(CiPbPrimSelectPhonebookReq));
						pnode->GenSimCmd_crsm = GenSimCmd_crsm;
						if (pnode->ci_request != NULL) {
							((CiPbPrimSelectPhonebookReq *) (pnode->ci_request))->PbId =
								CI_PHONEBOOK_FD;
							strcpy((char
								*)((CiPbPrimSelectPhonebookReq *) (pnode->
												   ci_request))->password.
								   data, gPin2);
							((CiPbPrimSelectPhonebookReq *) (pnode->ci_request))->
								password.len = (unsigned char)strlen(gPin2);

							add2CciActionlist(pnode);
						} else {
							CI_MEM_FREE(pnode);
							return CIRC_FAIL;
						}
					} else
						return CIRC_FAIL;

					temp = pnode->ci_request;
					pnode->ci_request = NULL;

					ret = ciRequest_1(ciATciSvgHandle[CI_SG_ID_PB], CI_PB_PRIM_SELECT_PHONEBOOK_REQ,
							  crsm_reqHandle, temp);

					if (ret || !CiStubUsesDynMem)
						CI_MEM_FREE(temp);

					rc = handleReturn(ret);
					return rc;
				}
			}
			/* START: Michal Bukai - Convert Response command to Select command */
			if (GET_RESPONSE == cmd) {
				GenSimCmd_crsm->instruction = CI_SIM_SELECT;
				GenSimCmd_crsm->param1 = 0;
				GenSimCmd_crsm->param2 = 0;
				GenSimCmd_crsm->length = 2;
				GenSimCmd_crsm->data[0] = (iFileId >> 8) & 0xFF;
				GenSimCmd_crsm->data[1] = (iFileId) & 0xFF;

				GenSimCmd_crsm->responseExpected = TRUE;
				GenSimCmd_crsm->responseLength = 255;
				if (GenSimCmd_crsm->path.len > 1) { /* remove the file id the last 2 bytes */
					GenSimCmd_crsm->path.valStr[GenSimCmd_crsm->path.len - 1] = '\0';
					GenSimCmd_crsm->path.valStr[GenSimCmd_crsm->path.len - 2] = '\0';
					GenSimCmd_crsm->path.len -= 2;
				}
				return ciRequest_1(svchandle, primId, crsm_reqHandle, GenSimCmd_crsm);
			}
			/* END: Michal Bukai - Convert Response command to Select command */
		} else {
			CCI_TRACE(CI_CLIENT_STUB, CI_CLIENT, __LINE__, CCI_TRACE_ERROR,
				  " *****it is not support yet!****");
		}
	} else {
		/* START: Michal Bukai - Convert Response command to Select command for all other file IDs */
		if (GET_RESPONSE == cmd) {
			GenSimCmd_crsm->instruction = CI_SIM_SELECT;
			GenSimCmd_crsm->param1 = 0;
			GenSimCmd_crsm->param2 = 0;
			GenSimCmd_crsm->length = 2;
			GenSimCmd_crsm->data[0] = (iFileId >> 8) & 0xFF;
			GenSimCmd_crsm->data[1] = (iFileId) & 0xFF;
			GenSimCmd_crsm->responseExpected = TRUE;
			GenSimCmd_crsm->responseLength = 255;
			if (GenSimCmd_crsm->path.len > 1) { /* remove the file id the last 2 bytes */
				GenSimCmd_crsm->path.valStr[GenSimCmd_crsm->path.len - 1] = '\0';
				GenSimCmd_crsm->path.valStr[GenSimCmd_crsm->path.len - 2] = '\0';
				GenSimCmd_crsm->path.len -= 2;
			}
			return ciRequest_1(svchandle, primId, crsm_reqHandle, GenSimCmd_crsm);
		}
		/* END: Michal Bukai - Convert Response command to Select command */
		else {
			return ciRequest_1(svchandle, primId, crsm_reqHandle, paras);
		}
	}
	return CIRC_FAIL;

}

/* fix CRSM assert - start */
/* we must use a static buffer here so ci client (MTSD) will not attempt to send free buffer on this local buffer */
CiSimPrimGenericCmdCnf gSimPrimGenericCmdCnf;

void clientCiConfirmCallback_transfer(CiConfirmArgs *pArg, CrsmCciAction *tem)
{
	CiReturnCode ret = CIRC_FAIL;
	int rc = -1;
	CiConfirmArgs cnf_args;
	CiSimPrimGenericCmdCnf *genericCmdCnf = NULL;
	unsigned char sendCiReq = FALSE;	/*Michal Bukai - Memory and other clean up in case CI reqest was not sen due to wrong data or memory allocation problems */
	void *temp;

	if (CiStubUsesDynMem)
		genericCmdCnf = &gSimPrimGenericCmdCnf;
	else
		genericCmdCnf = (CiSimPrimGenericCmdCnf *) CI_MEM_ALLOC(sizeof(CiSimPrimGenericCmdCnf));
/* fix CRSM assert - end */

	if (genericCmdCnf != NULL) {
		memset(genericCmdCnf, 0, sizeof(CiSimPrimGenericCmdCnf));
		switch (pArg->primId) {
		case (unsigned short)CI_MSG_PRIM_SELECT_STORAGE_CNF:
			{
				CiMsgPrimSelectStorageCnf *selectStorageCnf;
				selectStorageCnf = (CiMsgPrimSelectStorageCnf *) (pArg->paras);
				sendCiReq = FALSE;
				if (selectStorageCnf->rc == CIRC_MSG_SUCCESS) {
					if (tem == NULL) {
						if (!CiStubUsesDynMem)
							CI_MEM_FREE(genericCmdCnf);
						break;
					}

					if (tem->cci_confirm == CI_MSG_PRIM_DELETE_MESSAGE_CNF) {
						CI_MEM_FREE(tem->ci_request);
						tem->ci_request =
							(void *)CI_MEM_ALLOC(sizeof(CiMsgPrimDeleteMessageReq));
						if (tem->ci_request != NULL) {
							memset(tem->ci_request, 0, sizeof(CiMsgPrimDeleteMessageReq));
							((CiMsgPrimDeleteMessageReq *) (tem->ci_request))->index =
								GetRecordIndex(tem->GenSimCmd_crsm->param1,
									   tem->GenSimCmd_crsm->param2);
							if ((unsigned short)INVALID_INDEX !=
								((CiMsgPrimDeleteMessageReq *) (tem->ci_request))->index) {
								temp = tem->ci_request;
								tem->ci_request = NULL;

								ret =
									ciRequest_1(ciATciSvgHandle[CI_SG_ID_MSG],
										CI_MSG_PRIM_DELETE_MESSAGE_REQ,
										tem->crsm_reshandle, temp);

								if (ret || !CiStubUsesDynMem)
									CI_MEM_FREE(temp);

								rc = handleReturn(ret);
								sendCiReq = TRUE;
							}
						}
					}
					/*Michal Bukai - Map and validate CRSM data to status CS address and data fields of WriteMsgWithIndex struct */
					if (tem->cci_confirm == CI_MSG_PRIM_WRITE_MSG_WITH_INDEX_CNF) {
						unsigned char pduIndex = 0;
						/*unsigned short dataLen = 0; */
						CI_MEM_FREE(tem->ci_request);
						tem->ci_request =
							(void *)CI_MEM_ALLOC(sizeof(CiMsgPrimWriteMsgWithIndexReq));
						if (tem->ci_request != NULL) {
							memset(tem->ci_request, 0,
								   sizeof(CiMsgPrimWriteMsgWithIndexReq));
							((CiMsgPrimWriteMsgWithIndexReq *) (tem->ci_request))->index =
								GetRecordIndex(tem->GenSimCmd_crsm->param1,
									   tem->GenSimCmd_crsm->param2);
							if ((unsigned short)INVALID_INDEX !=
								((CiMsgPrimWriteMsgWithIndexReq *) (tem->
												ci_request))->index) {
								/*Status Byte */
								((CiMsgPrimWriteMsgWithIndexReq *) (tem->
													ci_request))->status
									= tem->GenSimCmd_crsm->data[pduIndex];
								pduIndex++;
								/*Service Center Address */
								if (tem->GenSimCmd_crsm->data[pduIndex] == 0x00) {  /*Service Center Address = NULL */
									((CiMsgPrimWriteMsgWithIndexReq
									  *) (tem->ci_request))->optSca.Present = FALSE;
									pduIndex += 1;
									sendCiReq = TRUE;
								} else if ((tem->GenSimCmd_crsm->data[pduIndex] == 0x01) && (tem->GenSimCmd_crsm->data[pduIndex + 1] == 0x80)) {	/*Service Center Address = NULL */
									((CiMsgPrimWriteMsgWithIndexReq
									  *) (tem->ci_request))->optSca.Present = FALSE;
									pduIndex += 2;
									sendCiReq = TRUE;
								} else {
									/*check if service center address length is valid - if not set service center address to null */
									if ((tem->GenSimCmd_crsm->data[pduIndex] > 1)
										&& (tem->GenSimCmd_crsm->data[pduIndex] <
										12)) {
										((CiMsgPrimWriteMsgWithIndexReq
										  *) (tem->ci_request))->
							  optSca.Present = TRUE;
										pduIndex +=
											ReadAddressInfo((CiAddressInfo *) &
													(((CiMsgPrimWriteMsgWithIndexReq *) (tem->ci_request))->optSca.AddressInfo), (unsigned char *)&(tem->GenSimCmd_crsm->data[1]));
										if (pduIndex == 1)
											sendCiReq = FALSE;
										else
											sendCiReq = TRUE;
									} else {
										sendCiReq = FALSE;
									}
								}

								if (sendCiReq) {
									unsigned short dataLen;

									dataLen =
										tem->GenSimCmd_crsm->length - pduIndex;
									if (dataLen > CI_MAX_CI_MSG_PDU_SIZE) {
										dataLen = CI_MAX_CI_MSG_PDU_SIZE;
									}

									((CiMsgPrimWriteMsgWithIndexReq
									  *) (tem->ci_request))->pdu.len = dataLen;

									if (pduIndex >
										(CI_SIM_MAX_APDU_DATA_SIZE -
										 CI_MAX_CI_MSG_PDU_SIZE))
										pduIndex =
											CI_SIM_MAX_APDU_DATA_SIZE -
											CI_MAX_CI_MSG_PDU_SIZE;

									memcpy(((CiMsgPrimWriteMsgWithIndexReq
										 *) (tem->ci_request))->pdu.data,
										   &(tem->GenSimCmd_crsm->data[pduIndex]),
										   dataLen);
								}

								if (sendCiReq) {
									temp = tem->ci_request;
									tem->ci_request = NULL;

									ret =
										ciRequest_1(ciATciSvgHandle[CI_SG_ID_MSG],
											CI_MSG_PRIM_WRITE_MSG_WITH_INDEX_REQ,
											tem->crsm_reshandle, temp);

									if (ret || !CiStubUsesDynMem)
										CI_MEM_FREE(temp);

									rc = handleReturn(ret);
								}
							}
						}
					}
					/*Michal Bukai - Memory and other clean up in case CI reqest was not sen due to wrong data or memory allocation problems */
					if (!sendCiReq) {
						genericCmdCnf->rc = CIRC_SIM_FAILURE;
						if (tem != NULL) {
							cnf_args.id = CI_SG_ID_SIM;
							cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
							cnf_args.paras = genericCmdCnf;
							cnf_args.opSgCnfHandle = tem->svshandle;
							cnf_args.reqHandle = tem->crsm_reshandle;
							freeCciActionlist(tem);
							clientCiConfirmCallback(&cnf_args);
						} else if (!CiStubUsesDynMem)
							CI_MEM_FREE(genericCmdCnf);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				} else {
					if (selectStorageCnf->rc == CIRC_MSG_SIM_NOT_READY)
						genericCmdCnf->rc = CIRC_SIM_SIM_NOT_READY;
					else
						genericCmdCnf->rc = CIRC_SIM_MEM_PROBLEM;
					if (tem != NULL) {
						cnf_args.id = CI_SG_ID_SIM;
						cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
						cnf_args.paras = genericCmdCnf;
						cnf_args.opSgCnfHandle = tem->svshandle;
						cnf_args.reqHandle = tem->crsm_reshandle;
						freeCciActionlist(tem);
						clientCiConfirmCallback(&cnf_args);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				}
				break;
			}
		case (unsigned short)CI_PB_PRIM_SELECT_PHONEBOOK_CNF:
			{
				CiPbPrimSelectPhonebookCnf *selectPBCnf;
				selectPBCnf = (CiPbPrimSelectPhonebookCnf *) (pArg->paras);
				sendCiReq = FALSE;
				if (selectPBCnf->Result == CIRC_PB_SUCCESS) {
					if (tem == NULL) {
						if (!CiStubUsesDynMem)
							CI_MEM_FREE(genericCmdCnf);
						break;
					}

					if (tem->cci_confirm == CI_PB_PRIM_READ_PHONEBOOK_ENTRY_CNF) {
						CI_MEM_FREE(tem->ci_request);
						tem->ci_request =
							(void *)CI_MEM_ALLOC(sizeof(CiPbPrimGetPhonebookInfoReq));
						if (tem->ci_request != NULL) {
							memset(tem->ci_request, 0, sizeof(CiPbPrimGetPhonebookInfoReq));
							((CiPbPrimGetPhonebookInfoReq *) (tem->ci_request))->PbId =
								CI_PHONEBOOK_FD;

							temp = tem->ci_request;
							tem->ci_request = NULL;

							ret =
								ciRequest_1(ciATciSvgHandle[CI_SG_ID_PB],
									CI_PB_PRIM_GET_PHONEBOOK_INFO_REQ,
									tem->crsm_reshandle, temp);

							if (ret || !CiStubUsesDynMem)
								CI_MEM_FREE(temp);

							rc = handleReturn(ret);
							sendCiReq = TRUE;

						}
					}
					if (tem->cci_confirm == CI_PB_PRIM_DELETE_PHONEBOOK_ENTRY_CNF) {
						CI_MEM_FREE(tem->ci_request);
						tem->ci_request =
							(void *)CI_MEM_ALLOC(sizeof(CiPbPrimDeletePhonebookEntryReq));
						if (tem->ci_request != NULL) {
							memset(tem->ci_request, 0,
								   sizeof(CiPbPrimDeletePhonebookEntryReq));
							((CiPbPrimDeletePhonebookEntryReq *) (tem->ci_request))->Index =
								GetRecordIndex(tem->GenSimCmd_crsm->param1,
									   tem->GenSimCmd_crsm->param2);
							if ((unsigned short)INVALID_INDEX !=
								((CiPbPrimDeletePhonebookEntryReq *) (tem->
												  ci_request))->Index) {
								temp = tem->ci_request;
								tem->ci_request = NULL;

								ret =
									ciRequest_1(ciATciSvgHandle[CI_SG_ID_PB],
										CI_PB_PRIM_DELETE_PHONEBOOK_ENTRY_REQ,
										tem->crsm_reshandle, temp);

								if (ret || !CiStubUsesDynMem)
									CI_MEM_FREE(temp);

								rc = handleReturn(ret);
								sendCiReq = TRUE;
							}
						}
					}
					if (tem->cci_confirm == CI_PB_PRIM_REPLACE_PHONEBOOK_ENTRY_CNF) {
						CI_MEM_FREE(tem->ci_request);
						tem->ci_request =
							(void *)CI_MEM_ALLOC(sizeof(CiPbPrimReplacePhonebookEntryReq));
						unsigned char AlphaIDLen = tem->GenSimCmd_crsm->length - 14;	/*we checked taht the pdu data contain at least 14 bytes, befor the SELECT command */

						if (tem->ci_request != NULL) {
							memset(tem->ci_request, 0,
								   sizeof(CiPbPrimReplacePhonebookEntryReq));
							getString(((CiPbPrimReplacePhonebookEntryReq
									*) (tem->ci_request))->entry.Name.Name,
								  tem->GenSimCmd_crsm->data,
								  &(((CiPbPrimReplacePhonebookEntryReq
									  *) (tem->ci_request))->entry.Name.Length));

							((CiPbPrimReplacePhonebookEntryReq *) (tem->
												   ci_request))->Index =
								GetRecordIndex(tem->GenSimCmd_crsm->param1,
									   tem->GenSimCmd_crsm->param2);

							if ((tem->GenSimCmd_crsm->data[AlphaIDLen] > 1)
								&& (tem->GenSimCmd_crsm->data[AlphaIDLen] < 12)) {
								if (ReadAddressInfo
									((CiAddressInfo *) &
									 (((CiPbPrimReplacePhonebookEntryReq
									*) (tem->ci_request))->entry.Number),
									 (unsigned char *)&(tem->
											GenSimCmd_crsm->data
											[AlphaIDLen])) == 0)
									sendCiReq = FALSE;
								else {
									unsigned short iIndex = 0, iExtIndex = 0;
									unsigned char bFDNExist = FALSE;
									iExtIndex =
										tem->GenSimCmd_crsm->
										data[tem->GenSimCmd_crsm->length - 1];

									/*Update FDN table in case this is record with extention */
									if (iExtIndex != 0xFF) {
										for (iIndex = 0; iIndex < 100; iIndex++) {
											if (gFDNWithExtTable
												[iIndex].FDNIndex ==
												((CiPbPrimReplacePhonebookEntryReq *) (tem->ci_request))->Index) {
												gFDNWithExtTable
													[iIndex].ExtIndexReq
													= iExtIndex;
												bFDNExist = TRUE;
												iIndex = 100;
											}
										}

										if (!bFDNExist) {
											for (iIndex = 0; iIndex < 100;
												 iIndex++) {
												if (gFDNWithExtTable
													[iIndex].status ==
													PB_RECORD_EMPTY) {
													gFDNWithExtTable
														[iIndex].status
														=
														PB_RECORD_UPDATE;
													gFDNWithExtTable
														[iIndex].FDNIndex
														=
														((CiPbPrimReplacePhonebookEntryReq *) (tem->ci_request))->Index;
													gFDNWithExtTable
														[iIndex].ExtIndexReq
														= iExtIndex;
													iIndex = 100;
												}
											}
										}
										/*Add extention informaition to the address */
										for (iIndex = 0; iIndex < 100; iIndex++) {
											if (gEXT2Table
												[iIndex].ExtIndexReq ==
												iExtIndex
												&& gEXT2Table[iIndex].status
												> PB_RECORD_EMPTY
												&&
												gEXT2Table[iIndex].pRecord
												[0] == 2) {
												gEXT2Table
													[iIndex].status =
													PB_RECORD_UPDATE;
												ReadMoreAddressInfo((CiAddressInfo *) &(((CiPbPrimReplacePhonebookEntryReq *) (tem->ci_request))->entry.Number), (unsigned char *)&(gEXT2Table[iIndex].pRecord[1]));
												iExtIndex =
													gEXT2Table
													[iIndex].pRecord
													[12];
												while (iExtIndex != 0XFF
													   &&
													   ((CiPbPrimReplacePhonebookEntryReq *) (tem->ci_request))->entry.Number.Length < CI_MAX_ADDRESS_LENGTH) {
													for (iIndex = 0;
														 iIndex <
														 100;
														 iIndex++) {
														if (gEXT2Table[iIndex].ExtIndexReq == iExtIndex && gEXT2Table[iIndex].status > PB_RECORD_EMPTY && gEXT2Table[iIndex].pRecord[0] == 2) {
															gEXT2Table
																[iIndex].status
																=
																PB_RECORD_UPDATE;
															ReadMoreAddressInfo
																((CiAddressInfo *) &(((CiPbPrimReplacePhonebookEntryReq *) (tem->ci_request))->entry.Number), (unsigned char *)&(gEXT2Table[iIndex].pRecord[1]));
															iExtIndex
																=
																gEXT2Table
																[iIndex].pRecord
																[12];
															iIndex
																=
																100;
														}
													}
												}
												iIndex = 100;
											}
										}
									}
									sendCiReq = TRUE;
								}
							} else {
								sendCiReq = FALSE;
							}

							if (sendCiReq == TRUE
								&& (unsigned short)INVALID_INDEX !=
								((CiPbPrimReplacePhonebookEntryReq *) (tem->
												   ci_request))->Index) {
								temp = tem->ci_request;
								tem->ci_request = NULL;

								ret =
									ciRequest_1(ciATciSvgHandle[CI_SG_ID_PB],
										CI_PB_PRIM_REPLACE_PHONEBOOK_ENTRY_REQ,
										tem->crsm_reshandle, temp);

								if (ret || !CiStubUsesDynMem)
									CI_MEM_FREE(temp);

								rc = handleReturn(ret);
							}
						}
					}
					/*Michal Bukai - Memory and other clean up in case CI reqest was not sen due to wrong data or memory allocation problems */
					if (!sendCiReq) {
						genericCmdCnf->rc = CIRC_SIM_FAILURE;
						if (tem != NULL) {
							cnf_args.id = CI_SG_ID_SIM;
							cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
							cnf_args.paras = genericCmdCnf;
							cnf_args.opSgCnfHandle = tem->svshandle;
							cnf_args.reqHandle = tem->crsm_reshandle;
							freeCciActionlist(tem);
							clientCiConfirmCallback(&cnf_args);
						} else if (!CiStubUsesDynMem)
							CI_MEM_FREE(genericCmdCnf);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				} else {
					if (selectPBCnf->Result == CIRC_PB_SIM_NOT_READY)
						genericCmdCnf->rc = CIRC_SIM_SIM_NOT_READY;
					else
						genericCmdCnf->rc = CIRC_SIM_FAILURE;

					if (tem != NULL) {
						cnf_args.id = CI_SG_ID_SIM;
						cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
						cnf_args.paras = genericCmdCnf;
						cnf_args.opSgCnfHandle = tem->svshandle;
						cnf_args.reqHandle = tem->crsm_reshandle;
						freeCciActionlist(tem);
						clientCiConfirmCallback(&cnf_args);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				}
				break;
			}

			/*Michal Bukai - in case of Read SMS operetion, CRSM command will not Mapped to CI_MSG (CQ00056743) */
		case (unsigned short)CI_MSG_PRIM_DELETE_MESSAGE_CNF:
			{
				CiMsgPrimDeleteMessageCnf *deleteMsgCnf;
				deleteMsgCnf = (CiMsgPrimDeleteMessageCnf *) (pArg->paras);
				if (deleteMsgCnf->rc == CIRC_MSG_SUCCESS) {

					genericCmdCnf->rc = CIRC_SIM_OK;
					SET_SIM_CMD_LEN_AND_TERM(genericCmdCnf, 2); /*SW1+SW2 */

					if (tem != NULL) {
						cnf_args.id = CI_SG_ID_SIM;
						cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
						cnf_args.paras = genericCmdCnf;
						cnf_args.opSgCnfHandle = tem->svshandle;
						cnf_args.reqHandle = tem->crsm_reshandle;
						freeCciActionlist(tem);
						clientCiConfirmCallback(&cnf_args);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				} else {
					if (deleteMsgCnf->rc == CIRC_MSG_SIM_NOT_READY)
						genericCmdCnf->rc = CIRC_SIM_SIM_NOT_READY;
					else
						genericCmdCnf->rc = CIRC_SIM_FAILURE;
					if (tem != NULL) {
						cnf_args.id = CI_SG_ID_SIM;
						cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
						cnf_args.paras = genericCmdCnf;
						cnf_args.opSgCnfHandle = tem->svshandle;
						cnf_args.reqHandle = tem->crsm_reshandle;
						freeCciActionlist(tem);
						clientCiConfirmCallback(&cnf_args);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				}
				break;
			}

		case (unsigned short)CI_MSG_PRIM_WRITE_MSG_WITH_INDEX_CNF:
			{
				CiMsgPrimWriteMsgWithIndexCnf *writeMsgWithIndexCnf;
				writeMsgWithIndexCnf = (CiMsgPrimWriteMsgWithIndexCnf *) (pArg->paras);
				if (writeMsgWithIndexCnf->rc == CIRC_MSG_SUCCESS) {
					genericCmdCnf->rc = CIRC_SIM_OK;

					SET_SIM_CMD_LEN_AND_TERM(genericCmdCnf, 2); /*SW1+SW2 */

					if (tem != NULL) {
						cnf_args.id = CI_SG_ID_SIM;
						cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
						cnf_args.paras = genericCmdCnf;
						cnf_args.opSgCnfHandle = tem->svshandle;
						cnf_args.reqHandle = tem->crsm_reshandle;
						freeCciActionlist(tem);
						clientCiConfirmCallback(&cnf_args);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				} else {
					if (writeMsgWithIndexCnf->rc == CIRC_MSG_SIM_NOT_READY)
						genericCmdCnf->rc = CIRC_SIM_SIM_NOT_READY;
					else
						genericCmdCnf->rc = CIRC_SIM_FAILURE;
					if (tem != NULL) {
						cnf_args.id = CI_SG_ID_SIM;
						cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
						cnf_args.paras = genericCmdCnf;
						cnf_args.opSgCnfHandle = tem->svshandle;
						cnf_args.reqHandle = tem->crsm_reshandle;
						freeCciActionlist(tem);
						clientCiConfirmCallback(&cnf_args);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				}
				break;
			}
		case (unsigned short)CI_PB_PRIM_READ_PHONEBOOK_ENTRY_CNF:
			{
				CiPbPrimReadPhonebookEntryCnf *readEntryCnf;
				readEntryCnf = (CiPbPrimReadPhonebookEntryCnf *) (pArg->paras);
				switch (readEntryCnf->Result) {
				case CIRC_PB_SUCCESS:
					{
						if (tem != NULL) {
							unsigned char fdnNumLen;
							unsigned char bFDNRecordExistInTable = FALSE;
							unsigned short iIndex, iPBIndex;
							memset(genericCmdCnf->cnf.data, EMPTY,
								   gPBMaxAlphaIdLength + 14);
							fdnNumLen = readEntryCnf->entry.Number.Length;
							if (fdnNumLen > 20) {
								for (iIndex = 0; iIndex < 100; iIndex++) {
									iPBIndex =
										GetRecordIndex(tem->GenSimCmd_crsm->param1,
											   tem->GenSimCmd_crsm->param2);
									if (gFDNWithExtTable[iIndex].FDNIndex ==
										iPBIndex
										&& gFDNWithExtTable[iIndex].status >
										PB_RECORD_EMPTY) {
										genericCmdCnf->
											cnf.data[gPBMaxAlphaIdLength + 13] =
											(unsigned char)
											gFDNWithExtTable
											[iIndex].ExtIndexReq;
										bFDNRecordExistInTable = TRUE;
										iIndex = 100;
									}
								}

								if (bFDNRecordExistInTable) {
									fdnNumLen = 20;
								} else {
									ret =
										ciRequest_1(tem->svshandle,
											CI_SIM_PRIM_GENERIC_CMD_REQ,
											tem->crsm_reshandle,
											tem->GenSimCmd_crsm);
									rc = handleReturn(ret);
								}
							}

							if (fdnNumLen <= 20) {
								string2BCD(&
									   (genericCmdCnf->
										cnf.data[gPBMaxAlphaIdLength + 2]),
									   readEntryCnf->entry.Number.Digits,
									   fdnNumLen);

								if (fdnNumLen % 2 == 0)
									fdnNumLen = fdnNumLen / 2 + 1;
								else
									fdnNumLen = fdnNumLen / 2 + 2;
								genericCmdCnf->cnf.data[gPBMaxAlphaIdLength] =
									fdnNumLen;

								if (readEntryCnf->entry.Number.AddrType.NumType == 0x01)
									genericCmdCnf->cnf.data[gPBMaxAlphaIdLength +
												1] = 0x91;
								else
									genericCmdCnf->cnf.data[gPBMaxAlphaIdLength +
												1] = 0x81;

								memcpy(genericCmdCnf->cnf.data,
									   readEntryCnf->entry.Name.Name,
									   readEntryCnf->entry.Name.Length);

								genericCmdCnf->rc = CIRC_SIM_OK;
								SET_SIM_CMD_LEN_AND_TERM(genericCmdCnf, gPBMaxAlphaIdLength + 16);  /*SW1+SW2 */

								cnf_args.id = CI_SG_ID_SIM;
								cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
								cnf_args.paras = genericCmdCnf;
								cnf_args.opSgCnfHandle = tem->svshandle;
								cnf_args.reqHandle = tem->crsm_reshandle;
								freeCciActionlist(tem);
								clientCiConfirmCallback(&cnf_args);
							} else if (!CiStubUsesDynMem)
								CI_MEM_FREE(genericCmdCnf);
						} else if (!CiStubUsesDynMem)
							CI_MEM_FREE(genericCmdCnf);
						break;
					}
				case CIRC_PB_DATA_UNAVAILABLE:
					{
						if (tem != NULL) {
							genericCmdCnf->rc = CIRC_SIM_OK;

							SET_SIM_CMD_LEN_AND_TERM(genericCmdCnf, gPBMaxAlphaIdLength + 16);  /*SW1+SW2 */

							memset(genericCmdCnf->cnf.data, EMPTY,
								   genericCmdCnf->cnf.len - 2);

							cnf_args.id = CI_SG_ID_SIM;
							cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
							cnf_args.paras = genericCmdCnf;
							cnf_args.opSgCnfHandle = tem->svshandle;
							cnf_args.reqHandle = tem->crsm_reshandle;
							freeCciActionlist(tem);
							clientCiConfirmCallback(&cnf_args);
						} else if (!CiStubUsesDynMem)
							CI_MEM_FREE(genericCmdCnf);
						break;
					}
				default:
					{
						if (readEntryCnf->Result == CIRC_PB_SIM_NOT_READY)
							genericCmdCnf->rc = CIRC_SIM_SIM_NOT_READY;
						else
							genericCmdCnf->rc = CIRC_SIM_FAILURE;

						if (tem != NULL) {
							cnf_args.id = CI_SG_ID_SIM;
							cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
							cnf_args.paras = genericCmdCnf;
							cnf_args.opSgCnfHandle = tem->svshandle;
							cnf_args.reqHandle = tem->crsm_reshandle;
							freeCciActionlist(tem);
							clientCiConfirmCallback(&cnf_args);
						} else if (!CiStubUsesDynMem)
							CI_MEM_FREE(genericCmdCnf);
						break;
					}
				}
				break;
			}
		case (unsigned short)CI_PB_PRIM_DELETE_PHONEBOOK_ENTRY_CNF:
			{
				CiPbPrimDeletePhonebookEntryCnf *deleteEntryCnf;
				deleteEntryCnf = (CiPbPrimDeletePhonebookEntryCnf *) (pArg->paras);
				if (deleteEntryCnf->Result == CIRC_PB_SUCCESS) {
					if (tem != NULL) {
						unsigned short iIndex, iPBIndex;
						iPBIndex =
							GetRecordIndex(tem->GenSimCmd_crsm->param1,
								   tem->GenSimCmd_crsm->param2);
						/*update FDN and EXT2 table if necessary: */
						for (iIndex = 0; iIndex < 100; iIndex++) {
							if (gFDNWithExtTable[iIndex].FDNIndex == iPBIndex) {
								iPBIndex = gFDNWithExtTable[iIndex].ExtIndexReq;
								gFDNWithExtTable[iIndex].status = PB_RECORD_EMPTY;
								gFDNWithExtTable[iIndex].FDNIndex = 0;
								iPBIndex = gFDNWithExtTable[iIndex].ExtIndexReq;
								gFDNWithExtTable[iIndex].ExtIndexReq = 0;

								for (iIndex = 0;
									 iIndex < 100 && iPBIndex > 0 && iPBIndex < 0xff;
									 iIndex++) {
									if (gEXT2Table[iIndex].ExtIndexReq == iPBIndex) {
										iPBIndex =
											gEXT2Table[iIndex].pRecord[12];
										memset(gEXT2Table[iIndex].pRecord,
											   EMPTY, 13);
										iIndex = 200;
										while (iPBIndex > 0 && iPBIndex < 0xff
											   && iIndex == 200) {
											for (iIndex = 0; iIndex < 100;
												 iIndex++) {
												if (gEXT2Table
													[iIndex].ExtIndexReq
													== iPBIndex) {
													iPBIndex =
														gEXT2Table
														[iIndex].pRecord
														[12];
													memset
														(gEXT2Table
														 [iIndex].pRecord,
														 EMPTY, 13);
													iIndex = 200;
												}
											}
										}
									}
								}
								iIndex = 100;
							}
						}
						genericCmdCnf->rc = CIRC_SIM_OK;
						SET_SIM_CMD_LEN_AND_TERM(genericCmdCnf, 2); /*SW1+SW2 */

						cnf_args.id = CI_SG_ID_SIM;
						cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
						cnf_args.paras = genericCmdCnf;
						cnf_args.opSgCnfHandle = tem->svshandle;
						cnf_args.reqHandle = tem->crsm_reshandle;
						freeCciActionlist(tem);
						clientCiConfirmCallback(&cnf_args);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				}
				/* Michal Bukai - added in order to support PB failur in case PIN2 required */
				else if (deleteEntryCnf->Result == CIRC_PB_FDN_OPER_NOT_ALLOWED) {
					if (tem != NULL) {
						CI_MEM_FREE(tem->ci_request);
						tem->ci_request =
							(void *)CI_MEM_ALLOC(sizeof(CiSimPrimDeviceStatusReq));
						if (tem->ci_request != NULL) {
							memset(tem->ci_request, 0, sizeof(CiSimPrimDeviceStatusReq));
							temp = tem->ci_request;
							tem->ci_request = NULL;

							ret =
								ciRequest_1(ciATciSvgHandle[CI_SG_ID_SIM],
									CI_SIM_PRIM_DEVICE_STATUS_REQ,
									tem->crsm_reshandle, temp);
							if (ret || !CiStubUsesDynMem)
								CI_MEM_FREE(temp);

							rc = handleReturn(ret);

							if (!CiStubUsesDynMem)
								CI_MEM_FREE(genericCmdCnf);
						} else {
							genericCmdCnf->rc = CIRC_SIM_FAILURE;
							if (tem != NULL) {
								cnf_args.id = CI_SG_ID_SIM;
								cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
								cnf_args.paras = genericCmdCnf;
								cnf_args.opSgCnfHandle = tem->svshandle;
								cnf_args.reqHandle = tem->crsm_reshandle;
								freeCciActionlist(tem);
								clientCiConfirmCallback(&cnf_args);
							}
						}
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				} else {
					if (deleteEntryCnf->Result == CIRC_PB_SIM_NOT_READY)
						genericCmdCnf->rc = CIRC_SIM_SIM_NOT_READY;
					else
						genericCmdCnf->rc = CIRC_SIM_FAILURE;

					if (tem != NULL) {
						cnf_args.id = CI_SG_ID_SIM;
						cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
						cnf_args.paras = genericCmdCnf;
						cnf_args.opSgCnfHandle = tem->svshandle;
						cnf_args.reqHandle = tem->crsm_reshandle;
						freeCciActionlist(tem);
						clientCiConfirmCallback(&cnf_args);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				}
				break;
			}
		case (unsigned short)CI_PB_PRIM_REPLACE_PHONEBOOK_ENTRY_CNF:
			{
				CiPbPrimReplacePhonebookEntryCnf *replaceEntryCnf;
				replaceEntryCnf = (CiPbPrimReplacePhonebookEntryCnf *) (pArg->paras);

				if (replaceEntryCnf->Result == CIRC_PB_SUCCESS) {
					if (tem != NULL) {
						genericCmdCnf->rc = CIRC_SIM_OK;

						SET_SIM_CMD_LEN_AND_TERM(genericCmdCnf, 2);

						cnf_args.id = CI_SG_ID_SIM;
						cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
						cnf_args.paras = genericCmdCnf;
						cnf_args.opSgCnfHandle = tem->svshandle;
						cnf_args.reqHandle = tem->crsm_reshandle;
						freeCciActionlist(tem);
						clientCiConfirmCallback(&cnf_args);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				}
				/* Michal Bukai - added in order to support PB failur in case PIN2 required */
				else if (replaceEntryCnf->Result == CIRC_PB_FDN_OPER_NOT_ALLOWED) {
					if (tem != NULL) {
						CI_MEM_FREE(tem->ci_request);
						tem->ci_request =
							(void *)CI_MEM_ALLOC(sizeof(CiSimPrimDeviceStatusReq));
						if (tem->ci_request != NULL) {
							memset(tem->ci_request, 0, sizeof(CiSimPrimDeviceStatusReq));
							temp = tem->ci_request;
							tem->ci_request = NULL;

							ret =
								ciRequest_1(ciATciSvgHandle[CI_SG_ID_SIM],
									CI_SIM_PRIM_DEVICE_STATUS_REQ,
									tem->crsm_reshandle, temp);
							if (ret || !CiStubUsesDynMem)
								CI_MEM_FREE(temp);

							rc = handleReturn(ret);
							if (!CiStubUsesDynMem)
								CI_MEM_FREE(genericCmdCnf);
						} else {
							genericCmdCnf->rc = CIRC_SIM_FAILURE;

							cnf_args.id = CI_SG_ID_SIM;
							cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
							cnf_args.paras = genericCmdCnf;
							cnf_args.opSgCnfHandle = tem->svshandle;
							cnf_args.reqHandle = tem->crsm_reshandle;
							freeCciActionlist(tem);
							clientCiConfirmCallback(&cnf_args);
						}
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				} else {
					if (replaceEntryCnf->Result == CIRC_PB_SIM_NOT_READY)
						genericCmdCnf->rc = CIRC_SIM_SIM_NOT_READY;
					else
						genericCmdCnf->rc = CIRC_SIM_FAILURE;

					if (tem != NULL) {
						cnf_args.id = CI_SG_ID_SIM;
						cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
						cnf_args.paras = genericCmdCnf;
						cnf_args.opSgCnfHandle = tem->svshandle;
						cnf_args.reqHandle = tem->crsm_reshandle;
						freeCciActionlist(tem);
						clientCiConfirmCallback(&cnf_args);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				}
				break;
			}
		case (unsigned short)CI_SIM_PRIM_DEVICE_STATUS_CNF:
			{
				CiSimPrimDeviceStatusCnf *deviceStatusCnf;
				deviceStatusCnf = (CiSimPrimDeviceStatusCnf *) (pArg->paras);

				if (deviceStatusCnf->rc == CIRC_SIM_OK) {
					/*Map PIN State to SIM Result Code */
					switch (deviceStatusCnf->pinState) {
					case CI_SIM_PIN_ST_CHV1_REQUIRED:
					case CI_SIM_PIN_ST_UNIVERSALPIN_REQUIRED:
						{
							genericCmdCnf->rc = CIRC_SIM_PIN_REQUIRED;
							break;
						}
					case CI_SIM_PIN_ST_CHV2_REQUIRED:
						{
							genericCmdCnf->rc = CIRC_SIM_PIN2_REQUIRED;
							break;
						}
					case CI_SIM_PIN_ST_UNBLOCK_CHV1_REQUIRED:
					case CI_SIM_PIN_ST_UNBLOCK_UNIVERSALPIN_REQUIRED:
						{
							genericCmdCnf->rc = CIRC_SIM_PUK_REQUIRED;
							break;
						}
					case CI_SIM_PIN_ST_UNBLOCK_CHV2_REQUIRED:
						{
							genericCmdCnf->rc = CIRC_SIM_PUK2_REQUIRED;
							break;
						}
					case CI_SIM_PIN_ST_READY:   /*should not happen - we get to this confirmation CI_SIM_PRIM_DEVICE_STATUS_CNF - after replace/delete PB operation fail due to CIRC_PB_FDN_OPER_NOT_ALLOWED */
						{
							genericCmdCnf->rc = CIRC_SIM_OK;
							break;
						}

					default:
						{
							genericCmdCnf->rc = CIRC_SIM_UNKNOWN;;
							break;
						}
					}

					if (tem != NULL) {
						cnf_args.id = CI_SG_ID_SIM;
						cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
						cnf_args.paras = genericCmdCnf;
						cnf_args.opSgCnfHandle = tem->svshandle;
						cnf_args.reqHandle = tem->crsm_reshandle;
						freeCciActionlist(tem);
						clientCiConfirmCallback(&cnf_args);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				} else {
					if (deviceStatusCnf->rc == CIRC_SIM_SIM_NOT_READY)
						genericCmdCnf->rc = CIRC_SIM_SIM_NOT_READY;
					else
						genericCmdCnf->rc = CIRC_SIM_FAILURE;
					if (tem != NULL) {
						cnf_args.id = CI_SG_ID_SIM;
						cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
						cnf_args.paras = genericCmdCnf;
						cnf_args.opSgCnfHandle = tem->svshandle;
						cnf_args.reqHandle = tem->crsm_reshandle;
						freeCciActionlist(tem);
						clientCiConfirmCallback(&cnf_args);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				}
				break;
			}
		case (unsigned short)CI_PB_PRIM_GET_PHONEBOOK_INFO_CNF:
			{
				CiPbPrimGetPhonebookInfoCnf *pbInfo;
				pbInfo = (CiPbPrimGetPhonebookInfoCnf *) (pArg->paras);

				if (pbInfo->Result == CIRC_PB_SUCCESS) {
					sendCiReq = FALSE;

					gPBMaxAlphaIdLength = pbInfo->info.MaxAlphaIdLength;

					if (tem == NULL) {
						if (!CiStubUsesDynMem)
							CI_MEM_FREE(genericCmdCnf);
						break;
					}

					CI_MEM_FREE(tem->ci_request);
					tem->ci_request = (void *)CI_MEM_ALLOC(sizeof(CiPbPrimReadPhonebookEntryReq));
					if (tem->ci_request != NULL) {
						memset(tem->ci_request, 0, sizeof(CiPbPrimReadPhonebookEntryReq));
						((CiPbPrimReadPhonebookEntryReq *) (tem->ci_request))->Index =
							GetRecordIndex(tem->GenSimCmd_crsm->param1,
								   tem->GenSimCmd_crsm->param2);
						if ((unsigned short)INVALID_INDEX !=
							((CiPbPrimReadPhonebookEntryReq *) (tem->ci_request))->Index) {
							temp = tem->ci_request;
							tem->ci_request = NULL;

							ret =
								ciRequest_1(ciATciSvgHandle[CI_SG_ID_PB],
									CI_PB_PRIM_READ_PHONEBOOK_ENTRY_REQ,
									tem->crsm_reshandle, temp);

							if (ret || !CiStubUsesDynMem)
								CI_MEM_FREE(temp);

							rc = handleReturn(ret);
							sendCiReq = TRUE;
						}
					}
					if (!sendCiReq) {
						genericCmdCnf->rc = CIRC_SIM_FAILURE;
						cnf_args.id = CI_SG_ID_SIM;
						cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
						cnf_args.paras = genericCmdCnf;
						cnf_args.opSgCnfHandle = tem->svshandle;
						cnf_args.reqHandle = tem->crsm_reshandle;
						freeCciActionlist(tem);
						clientCiConfirmCallback(&cnf_args);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				} else {
					if (pbInfo->Result == CIRC_PB_SIM_NOT_READY)
						genericCmdCnf->rc = CIRC_SIM_SIM_NOT_READY;
					else
						genericCmdCnf->rc = CIRC_SIM_FAILURE;
					if (tem != NULL) {
						cnf_args.id = CI_SG_ID_SIM;
						cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
						cnf_args.paras = genericCmdCnf;
						cnf_args.opSgCnfHandle = tem->svshandle;
						cnf_args.reqHandle = tem->crsm_reshandle;
						freeCciActionlist(tem);
						clientCiConfirmCallback(&cnf_args);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				}
				break;
			}
		case (unsigned short)CI_SIM_PRIM_GENERIC_CMD_CNF:
			{
				CiSimPrimGenericCmdCnf *tmpGenericCmdCnf;
				tmpGenericCmdCnf = (CiSimPrimGenericCmdCnf *) (pArg->paras);
				if (tmpGenericCmdCnf->rc == CIRC_SIM_OK) {
					unsigned short iIndex, iPBIndex;
					genericCmdCnf->rc = CIRC_SIM_OK;
					if (tem != NULL) {
						if (tem->cci_confirm == CI_PB_PRIM_READ_PHONEBOOK_ENTRY_CNF) {
							memcpy(genericCmdCnf->cnf.data, tmpGenericCmdCnf->cnf.data,
								   tmpGenericCmdCnf->cnf.len);
							genericCmdCnf->cnf.len = tmpGenericCmdCnf->cnf.len;
							cnf_args.id = CI_SG_ID_SIM;
							cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
							cnf_args.paras = genericCmdCnf;
							cnf_args.opSgCnfHandle = tem->svshandle;
							cnf_args.reqHandle = tem->crsm_reshandle;
							freeCciActionlist(tem);
							clientCiConfirmCallback(&cnf_args);
						} else if (tem->cci_confirm == EXT2_READ_OPER) {
							iPBIndex =
								GetRecordIndex(tem->GenSimCmd_crsm->param1,
									   tem->GenSimCmd_crsm->param2);
							memcpy(genericCmdCnf->cnf.data, tmpGenericCmdCnf->cnf.data,
								   tmpGenericCmdCnf->cnf.len);
							genericCmdCnf->cnf.len = tmpGenericCmdCnf->cnf.len;

							for (iIndex = 0; iIndex < 100; iIndex++) {
								if (gEXT2Table[iIndex].status > PB_RECORD_EMPTY
									&& gEXT2Table[iIndex].ExtIndexReq == iPBIndex) {
									memset(genericCmdCnf->cnf.data, EMPTY, 13);
									memcpy(genericCmdCnf->cnf.data,
										   gEXT2Table[iIndex].pRecord, 13);

									SET_SIM_CMD_LEN_AND_TERM(genericCmdCnf, 15);	/*SW1+SW2 */
									iIndex = 100;
								}
							}
							cnf_args.id = CI_SG_ID_SIM;
							cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
							cnf_args.paras = genericCmdCnf;
							cnf_args.opSgCnfHandle = tem->svshandle;
							cnf_args.reqHandle = tem->crsm_reshandle;
							freeCciActionlist(tem);
							clientCiConfirmCallback(&cnf_args);
						} else if (tem->cci_confirm == EXT2_UPDATE_OPER) {
							SET_SIM_CMD_LEN_AND_TERM(genericCmdCnf, 2); /*SW1+SW2 */
							cnf_args.id = CI_SG_ID_SIM;
							cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
							cnf_args.paras = genericCmdCnf;
							cnf_args.opSgCnfHandle = tem->svshandle;
							cnf_args.reqHandle = tem->crsm_reshandle;
							freeCciActionlist(tem);
							clientCiConfirmCallback(&cnf_args);
						} else if (!CiStubUsesDynMem)
							CI_MEM_FREE(genericCmdCnf);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				} else {
					if (tmpGenericCmdCnf->rc == CIRC_SIM_SIM_NOT_READY)
						genericCmdCnf->rc = CIRC_SIM_SIM_NOT_READY;
					else
						genericCmdCnf->rc = CIRC_SIM_FAILURE;
					if (tem != NULL) {
						cnf_args.id = CI_SG_ID_SIM;
						cnf_args.primId = CI_SIM_PRIM_GENERIC_CMD_CNF;
						cnf_args.paras = genericCmdCnf;
						cnf_args.opSgCnfHandle = tem->svshandle;
						cnf_args.reqHandle = tem->crsm_reshandle;
						freeCciActionlist(tem);
						clientCiConfirmCallback(&cnf_args);
					} else if (!CiStubUsesDynMem)
						CI_MEM_FREE(genericCmdCnf);
				}
				break;
			}
		default:
			{
				if (!CiStubUsesDynMem)
					CI_MEM_FREE(genericCmdCnf);
			}
		}
	}

}
