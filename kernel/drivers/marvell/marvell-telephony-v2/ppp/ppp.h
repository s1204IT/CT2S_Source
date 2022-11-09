/*
 * ppp.h -- ppp.c header
 *
 * (C) Copyright [2006-2008] Marvell International Ltd.
 * All Rights Reserved
 *
 */

#ifndef _PPP_H_
#define _PPP_H_

#if defined __cplusplus
extern "C" {
#endif				/* __cplusplus */

#include "ppp_types.h"
#include "common_datastub.h"

#define PPP_QUEUE_NUM_MESSAGES	256
#define	PPP_TIMEOUT_WAIT_FOR_CONFIG_REQUEST		100	/* ms */
#define PPP_TIMEOUT_WAIT_FOR_CONFIG_ACK			5000 /* ms */

typedef int (*DataRxCallbackFunc) (const unsigned char *packet,
	unsigned int len, unsigned char cid);
extern int registerRxCallBack(SVCTYPE pdpTye,
	DataRxCallbackFunc callback);

/*
 * Externs
 */
typedef void (*setPPPLoopback) (unsigned short port, int DLMult);
extern void sendDataReqtoInternalList(unsigned char cid, char *buf, int len);
extern void registerSetPPPLoppbackCB(setPPPLoopback f);

/************************************************************************/
/* APIs                                                                 */
/************************************************************************/
	void PppInit(PppInitParamsS *PppInitParams);	/*  PppRxCallbackFunc fPtr , PppTerminateCallbackFunc fTermCbPtr); */
	void PppDeinit(void);
	void PppMessageReq(const U_CHAR *buf, U_INT length);
	void PppCreateMessageFrameIp(void);
	void PppCreateMessageFrame(void);
	void PppSendToCallback(PppControlS *pppControl, U_CHAR *buf,
			U_INT length);
	void PppUpdateIpParams(IpcpConnectionParamsS *ipParams);
	void PppSendTerminateReq(void);
	void PppTerminate(void);
	void PppReset(void);
	void PppSetCid(U_INT cid);
	void PppEnableEchoRequest(BOOL isEnabled);
	void PppStartLcpHandshake(void);
	int pppRelayMessageFromComm(const U_CHAR *buf, U_INT count,
			U_CHAR cid);
#if defined __cplusplus
}
#endif				/* __cplusplus */
#endif				/* _PPP_H_ */
