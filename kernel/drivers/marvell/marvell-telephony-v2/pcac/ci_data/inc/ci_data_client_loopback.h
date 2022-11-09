/*------------------------------------------------------------
(C) Copyright [2006-2013] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
    Title:       CI Data Client LoopBack headerfile
    Filename:    ci_data_client_loopBack.h
    Description: Contains defines/structs/prototypes for the loopBack mode operation
    Author:      Zohar Sefti
************************************************************************/

#if !defined(_CI_DATA_CLIENT_LOOPBACK_H_)
#define      _CI_DATA_CLIENT_LOOPBACK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "linux_types.h"
#include "ci_data_client.h"
#ifdef __KERNEL__
/*#include <linux/spinlock.h> */
#include <linux/timer.h>
#endif

/************************************************************************
 * Defines
 ***********************************************************************/

/************************************************************************
 * Types
 ***********************************************************************/

	typedef struct {
		unsigned int cid;
		unsigned char enable;
		unsigned int delay;
	} CiData_LoopBackActivationS;
/************************************************************************
 * Function Prototypes
 ***********************************************************************/

/* Ci Data LoopBack type B mode handler function - echo DL packets backword to UL */
	void CiDataLoopBackModeBRegister(int cid);
	void CiDataLoopBackModeBDisable(void);
	void CIDataLoopBackModeBTimerCallBack(unsigned long val);
	void CIDataLoopBackModeBStartTimer(void);
	unsigned char CiDataLoopBackModeBCallBack(unsigned int cid, void *packet_address, unsigned int packet_length);
	void CiDataLoopBackModeBEnable(CiData_LoopBackActivationS *pLoopBackActivationParams);
	void CiDataLoopBackTestActivation(void);
	void CiDataLoopBackTestTermination(void);
/*CI Data Client (APPS) API - Uplink (data from APPS to COMM) */
	unsigned int CiDataLoopBackModeBUplinkDataSend(unsigned int cid, void *packet_address,
						       unsigned int packet_length);
	void CiDataLoopBackModeBHandleDelayedPckt(unsigned int cid);
#ifdef __cplusplus
}
#endif				/*__cplusplus */
#endif				/* _CI_DATA_CLIENT_API_H_ */
