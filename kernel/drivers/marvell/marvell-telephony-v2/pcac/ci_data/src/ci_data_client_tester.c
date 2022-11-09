/*------------------------------------------------------------
(C) Copyright [2006-2011] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
	Title:  	 CI Data Client
	Filename:    ci_data_client.c
	Description: Contains implementation for CI Data server for R7/LTE data path
	Author: 	 Yossi Gabay
************************************************************************/

#include "osa_kernel.h"

#include "ci_data_client_loopback.h"
#include "ci_data_client_shmem.h"
#include "ci_data_client_api.h"

/************************************************************************
 * Internal debug log
 ***********************************************************************/
#include "dbgLog.h"
DBGLOG_EXTERN_VAR(cidata);

/*#include <stdio.h> */

/*#include <stdlib.h>
#include <cstdlib.h>

#include <string.h>*/
/*#include <stdio.h> */

#define CI_DATA_TESTER_MAX_BUFFER   	2000

/*Local Macros */

/*Globals */

typedef struct {
	unsigned char enabled;
	unsigned int numOfCachedCopies;
	unsigned int numOfNonCachedCopies;
} CIDataTester_LoopBackS;

CIDataTester_LoopBackS gCiDataTesterLoopBack;

unsigned char gCiDataTesterLoopBackEnabled = FALSE;

/***********************************************************************************\
* Functions
************************************************************************************/

void CiDataTesterUplinkDataSendMulti(unsigned int listLen, unsigned int cidList[], unsigned int cidAmount[],
					 unsigned int size)
{
	static char *buf;
	int i, j;
	unsigned int result;

	if (!buf) {
		gfp_t flag = /*(in_atomic()) ? GFP_ATOMIC : */GFP_KERNEL;
		buf = kmalloc(CI_DATA_TESTER_MAX_BUFFER, flag);
		CI_DATA_ASSERT_MSG(0, (buf), "Out of memory");
	}

	for (i = 0; i < sizeof(buf); i++) {
		buf[i] = 'a' + i % 26;
	}

	CI_DATA_DEBUG(0, "CiDataTesterUplinkDataSendMulti - sending list length of %d ", listLen);

	for (i = 0; i < listLen; i++) {
		buf[0] = '0' + cidList[i];

		for (j = 0; j < cidAmount[i]; j++) {
			buf[1] = '0' + j % 10;
			result = ci_data_uplink_data_send(cidList[i], (void *)buf, size);
			/*check for errors */
			if (result != 0) {
				CI_DATA_DEBUG(0, "CiDataTesterUplinkDataSend - packet %d, of cid=%d failed", i,
						  cidList[i]);
				CI_DATA_DEBUG(0, "CiDataTesterUplinkDataSend result=%d", result);
				break;
			}
		}
	}
}

void CiDataTesterUplinkDataSend(unsigned int num, unsigned int size)
{
	static char *buf;
	int i;
	unsigned int result;
	unsigned int thrown = 0;

	if (!buf) {
		gfp_t flag = /*(in_atomic())? GFP_ATOMIC : */GFP_KERNEL;
		buf = kmalloc(CI_DATA_TESTER_MAX_BUFFER, flag);
		CI_DATA_ASSERT_MSG(0, (buf), "Out of memory");
	}

	for (i = 0; i < sizeof(buf); i++) {
		buf[i] = 'a' + i % 26;
	}

	CI_DATA_DEBUG(0, "CiDataTesterUplinkDataSend - sending %d packets, size=%d", num, size);

	for (i = 0; i < num; i++) {
		buf[0] = '0' + i % 10;
#if 0
		result = ci_data_uplink_data_send(0, (void *)buf, size);
		/*check for errors */
		if (result != 0) {
			/*CI_DATA_DEBUG(0, "CiDataTesterUplinkDataSend - packet %d failed (result=%d) !!!", i, result); */
			/*CI_DATA_DEBUG(0, "T"); */
			thrown++;
			/*break; */
		}
#else
		do {
			result = ci_data_uplink_data_send(0, (void *)buf, size);
			thrown++;   /*retry counter */
			/*msleep(5); */
		} while (result != 0);
		/*check for errors */
#endif

	}
	CI_DATA_DEBUG(0, "CiDataTesterUplinkDataSend: requesested %d packets, thrown %d", num, thrown);

}

unsigned char CiDataTesterDownlinkDataReceivedCB(unsigned int cid, void *packet_address, unsigned int packet_length)
{
	unsigned int result;
	int i;
	static char *localBuffer_1;
	static char *localBuffer_2;
	static int totalPacketsThrown;
	static int totalPacketsReceived;

	if (!localBuffer_1) {
		gfp_t flag = /*(in_atomic())? GFP_ATOMIC : */GFP_KERNEL;
		localBuffer_1 = kmalloc(CI_DATA_TESTER_MAX_BUFFER, flag);
		localBuffer_2 = kmalloc(CI_DATA_TESTER_MAX_BUFFER, flag);
		CI_DATA_ASSERT_MSG(0, (localBuffer_1 && localBuffer_2), "Out of memory");
	}
	CI_DATA_DEBUG(1,
			  "DBG CiDataTesterDownlinkDataReceivedCB: received packet (cid=%d, packet_address=%x, packet_length=%d)",
			  cid, (unsigned int)packet_address, packet_length);

	/*if(packet_length == 0)
	   {
	   CI_DATA_DEBUG(0, "CiDataTesterDownlinkDataReceivedCB - packet_length is ZERO!!! cid=%d, packet_address=%x",
	   cid, packet_address);
	   } */

	if (gCiDataTesterLoopBack.numOfCachedCopies > 0) {
		for (i = 0; i < gCiDataTesterLoopBack.numOfCachedCopies; i++) {
			memcpy(localBuffer_2, localBuffer_1, packet_length);
		}
	}
	if (gCiDataTesterLoopBack.numOfNonCachedCopies > 0) {
		for (i = 0; i < gCiDataTesterLoopBack.numOfNonCachedCopies; i++) {
			memcpy(localBuffer_1, packet_address, packet_length);
		}
	}

	totalPacketsReceived++;

	/*if(gCiDataTesterLoopBack.enabled) */
	if (gCiDataTesterLoopBackEnabled) {
#if 0   			/*replace source & destination IP's (4bytes for IPv4 only) */
		char tempBuf[10];

		memcpy(tempBuf, (char *)packet_address + 12, 4);	/*source IP is at offset 12bytes */
		memcpy((char *)packet_address + 12, (char *)packet_address + 16, 4);	/*destination IP is at offset 16bytes */
		memcpy((char *)packet_address + 16, tempBuf, 4);

		/*set ICMP header type to 0 - Echo Reply */
		((char *)packet_address)[20] = 0;
#endif

		/*result = ci_data_uplink_data_send(cid, packet_address, packet_length); */
		result = ci_data_uplink_data_send(cid, localBuffer_1, packet_length);
		/*check for errors */
		if (result != 0) {
			if ((totalPacketsThrown++ % 50000) == 0) {
				/*CI_DATA_DEBUG(0, "R=%d, T=%d\n", totalPacketsReceived, totalPacketsThrown); */
				/*CI_DATA_DEBUG(0, "CiDataTesterDownlinkDataReceivedCB - Info: totalPacketsReceived=%d, totalPacketsThrown=%d", */
				/*  		  totalPacketsReceived, totalPacketsThrown); */
			}
#if 0
			CI_DATA_DEBUG(0,
					  "CiDataTesterDownlinkDataReceivedCB - send packet %x failed, size %d (result=%d) !!!",
					  (unsigned int)packet_address, packet_length, result);
#endif
		}
	} else {
		CI_DATA_DEBUG(2,
				  "CiDataTesterDownlinkDataReceivedCB: Received cid=%d, packet_address=%x, packet_length=%d",
				  cid, (unsigned int)packet_address, packet_length);
	}
#if 1
	/*print buffer content */
	CI_DATA_DEBUG(0, "CiDataTesterDownlinkDataReceivedCB: buffer content --->");
	for (i = 0; i < packet_length; i++) {
		CI_DATA_DEBUG(0, "%x,", ((char *)packet_address)[i]);
	}

	CI_DATA_DEBUG(0, "<--- buffer ends");
#endif

	return TRUE;
}

void CiDataTesterRegister(void)
{
	int cid;

	/*init tester */
	memset(&gCiDataTesterLoopBack, 0, sizeof(gCiDataTesterLoopBack));

	/*register tester */
	CI_DATA_DEBUG(0, "\nCI Data Client Tester - Registering Tester CID's...");
	for (cid = 0; cid < CI_DATA_TOTAL_CIDS; cid++) {
		ci_data_downlink_register(cid, CiDataTesterDownlinkDataReceivedCB);
	}
}

void CiDataControlLogging(const char *buf, size_t *index)
{
	char mode;

	switch (buf[*index]) {
	case 'b':
	case 't':
		mode = buf[*index];
		(*index)++;
		if (mode == 'b') {
			DBGLOG_PRINT_BINARY(cidata);
		} else {
			DBGLOG_PRINT(cidata);
		}
		break;

	default:
		CI_DATA_DEBUG(0, "CiDataControlLogging: operation not supported (%c)", buf[*index]);
		(*index)++;
		break;
	}

}

void CiDataTesterRunCommand(char oper, char *params)
{
	OSA_STATUS osaStatus;

	CI_DATA_DEBUG(0, "CiDataTesterRunCommand DEBUG: oper=%c, params=%s", oper, params);
	switch (oper) {
	case '1':   	/*Uplink: single big packet (1000 bytes), CID=0 */
		CiDataTesterUplinkDataSend(1, 1500);
		break;

	case '2':   	/*Uplink: number of big packets (1000 bytes), CID=0 */
		{
			/*int num = atoi(params); */
			char *endp;
			int num = simple_strtoul(params, &endp, 10);
			CiDataTesterUplinkDataSend(num, 1500);
		}
		break;

	case '3':   	/*Uplink: number of small packets (50 bytes), CID=0 */
		{
			char *endp;
			int num = simple_strtoul(params, &endp, 10);

			CiDataTesterUplinkDataSend(num, 50);
		}
		break;

	case '4':   	/*Uplink: multi list - few packets per CID, medium size (100 bytes) */
		{
			char *endp;
			unsigned int listLen = simple_strtoul(params, &endp, 10);
			unsigned int cidList[8];
			unsigned int cidAmount[8];
			int i;

			/*printk("\n First format =%s\n", endp); */

			for (i = 0; i < listLen; i++) {
				/*params = strchr(params, ' ');//move to next item */
				params = endp;
				/*printk("format1 =%s\n", params); */
				cidList[i] = simple_strtoul(params + 1, &endp, 10);

				/*params = strchr(params, ' ');//move to next item */
				params = endp;
				/*printk("format2 =%s\n", params); */
				cidAmount[i] = simple_strtoul(params + 1, &endp, 10);
			}
			CiDataTesterUplinkDataSendMulti(listLen, cidList, cidAmount, 100);
		}
		break;

	case '5':   	/*Uplink: multi list - few packets per CID, bigger size (500 bytes) */
		{
			char *endp;
			unsigned int listLen = simple_strtoul(params, &endp, 10);
			unsigned int cidList[8];
			unsigned int cidAmount[8];
			int i;

			/*printk("\n First format =%s\n", endp); */

			for (i = 0; i < listLen; i++) {
				/*params = strchr(params, ' ');//move to next item */
				params = endp;
				/*printk("format1 =%s\n", params); */
				cidList[i] = simple_strtoul(params + 1, &endp, 10);

				/*params = strchr(params, ' ');//move to next item */
				params = endp;
				/*printk("format2 =%s\n", params); */
				cidAmount[i] = simple_strtoul(params + 1, &endp, 10);
			}
			CiDataTesterUplinkDataSendMulti(listLen, cidList, cidAmount, 500);
		}
		break;

	case '9':   	/*downlink: force-activating the downlinkTask */
		CI_DATA_DEBUG(0, "Force activating of the downlink task");
		osaStatus =
			FLAG_SET(&gCiDataClient.downlinkWaitFlagRef, CI_DATA_DOWNLINK_PACKET_PENDING_FLAG_MASK,
				 OSA_FLAG_OR);
		CI_DATA_ASSERT(osaStatus == OS_SUCCESS);
		break;

	case 'l':
	case 'L':   	/*loopBack enable/disable */
		if (*params == '1') {
			char *endp;
			int num = simple_strtoul(params + 1, &endp, 10);

			CI_DATA_DEBUG(0, "Data loopBack - setting numOfCachedCopies=%d", num);
			gCiDataTesterLoopBack.numOfCachedCopies = num;
		} else if (*params == '2') {
			char *endp;
			int num = simple_strtoul(params + 1, &endp, 10);

			CI_DATA_DEBUG(0, "Data loopBack - setting numOfNonCachedCopies=%d", num);
			gCiDataTesterLoopBack.numOfNonCachedCopies = num;
		} else if (*params == '3') {
			CiData_LoopBackActivationS pLoopBackActivationParams;
			pLoopBackActivationParams.cid = 0;
			pLoopBackActivationParams.delay = 10;
			pLoopBackActivationParams.enable = TRUE;
			CiDataLoopBackModeBEnable(&pLoopBackActivationParams);
		} else {
			if (gCiDataTesterLoopBackEnabled) {
				CI_DATA_DEBUG(0, "Data loopBack is turned OFF");
				gCiDataTesterLoopBackEnabled = FALSE;
			} else {
				CI_DATA_DEBUG(0, "Data loopBack is turned ON !!!");
				gCiDataTesterLoopBackEnabled = TRUE;
			}
		}

		break;

	case 'e':
	case 'E':   	/*enable CID */
		if ((*params == 'a') || (*params == 'A')) { /*enable all CID's */
			int cid;
			CiData_CidInfoS cidInfo;
			CI_DATA_DEBUG(0, "CiDataTester Enable all CID's!!!");
			for (cid = 0; cid < CI_DATA_TOTAL_CIDS; cid++) {
				cidInfo.cid = cid;
				cidInfo.connectionType = CI_DATA_SVC_TYPE_PDP_DIRECTIP;
				cidInfo.serviceType = 1;

				CiDataClientCidEnable(&cidInfo, TRUE);
			}
		} else {
			char *endp;
			unsigned int cid = simple_strtoul(params + 1, &endp, 10);
			CiData_CidInfoS cidInfo;

			cidInfo.cid = cid;
			cidInfo.connectionType = CI_DATA_SVC_TYPE_PDP_DIRECTIP;
			cidInfo.serviceType = 1;

			CI_DATA_DEBUG(0, "CiDataTester Enable CID=%d", cid);
			CiDataClientCidEnable(&cidInfo, TRUE);
		}
		break;

	case 'd':
	case 'D':   	/*disable CID */
		if ((*params == 'a') || (*params == 'A')) { /*disable all CID's */
			int cid;
			CI_DATA_DEBUG(0, "CiDataTester Disable all CID's!!!");
			for (cid = 0; cid < CI_DATA_TOTAL_CIDS; cid++) {
				CiDataClientCidDisable(cid);
			}
		} else {
			char *endp;
			unsigned int cid = simple_strtoul(params + 1, &endp, 10);

			CI_DATA_DEBUG(0, "CiDataTester Disable CID=%d", cid);
			CiDataClientCidDisable(cid);
		}
		break;

	case 'p':
	case 'P':   	/*CID priority */
		{
			char *endp;
			unsigned int cid = simple_strtoul(params + 1, &endp, 10);
			unsigned int priority = simple_strtoul(endp + 1, &endp, 10);
			CiData_CidInfoS cidInfo;

			if (cid < CI_DATA_TOTAL_CIDS) {
				cidInfo.cid = cid;
				cidInfo.priority = priority;
				CI_DATA_DEBUG(0, "CiDataTester Set CID Priority, CID=%d, Priority=%d", cid, priority);

				CiDataClientCidSetPriority(&cidInfo);
			} else {
				CiDataClientPrintActiveCids();
			}
		}
		break;

	case 'q':
	case 'Q':   	/*CID priority */
		{
			gCiDataClient.uplinkQosDisabled = (gCiDataClient.uplinkQosDisabled == TRUE) ? FALSE : TRUE;
			CI_DATA_DEBUG(0, "CiDataTester Uplink QOS is %s",
					  (gCiDataClient.uplinkQosDisabled == TRUE) ? "disabled" : "enabled");
		}
		break;

	default:		/*BOTH: client-tester registeration */
		CiDataTesterRegister();
		break;
	}

}
