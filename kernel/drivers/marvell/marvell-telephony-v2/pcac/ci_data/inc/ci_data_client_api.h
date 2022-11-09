/*------------------------------------------------------------
(C) Copyright [2006-2011] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
    Title:       CI Data Client API headerfile
    Filename:    ci_data_client_api.h
    Description: Contains defines/structs/prototypes for the R7/LTE data path
    Author:      Yossi Gabay
************************************************************************/

#if !defined(_CI_DATA_CLIENT_API_H_)
#define      _CI_DATA_CLIENT_API_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "linux_types.h"

#define CI_DATA_DOWNLINK_PACKET_PENDING_FLAG_MASK   0x00000001
#define CI_DATA_DOWNLINK_SILENT_RESET_FLAG_MASK     0x00000002
#define CI_DATA_DOWNLINK_APPS_TASK_FLAG_MASK        (CI_DATA_DOWNLINK_PACKET_PENDING_FLAG_MASK | CI_DATA_DOWNLINK_SILENT_RESET_FLAG_MASK)

#define CI_DATA_SILENT_RESET_FLAG_MASK              0x00000001
#define CI_DATA_SILENT_RESET_WAIT_TIMEOUT           1000

/************************************************************************
 * Callback Prototypes
 ***********************************************************************/

/*APPS Downlink callback prototype */
	typedef unsigned char (*ci_data_downlink_data_receive_cb) (unsigned int
								   cid,
								   void
								   *packet_address,
								   unsigned int
								   packet_length);
	typedef unsigned char (*CiDataLtmDownlinkDataReceiveCB_Type) (void
								      **packetsList);

/************************************************************************
 * Internal API's
 ***********************************************************************/

/************************************************************************
 * API's
 ***********************************************************************/

/*YG: void        CiDataClientInit(void); */

/*CI Data Client (APPS) API - Uplink (data from APPS to COMM) */
	CiData_ErrorCodesE ci_data_uplink_data_send(unsigned int cid,
						    void *packet_address,
						    unsigned int packet_length);
	unsigned int ci_data_uplink_data_send_gcf(unsigned int cid,
						  unsigned int packet_length);

/*CI Data Client (APPS) API - Downlink (data from COMM to APPS) */
	void ci_data_downlink_register(unsigned int cid,
				       ci_data_downlink_data_receive_cb
				       downlink_data_receive_cb);
	void CiDataLtmDownlinkRegister(unsigned int cid,
				       CiDataLtmDownlinkDataReceiveCB_Type
				       ltmDownlinkDataReceiveCB);

/*CI Data Client (APPS) API - Downlink (data from COMM to APPS) */
	void ci_data_downlink_unregister(unsigned int cid);

/*CI Data Client (APPS) API - Downlink - confirm packet ready to be freed in COMM */
	void CiDataClientDownlinkFreePacket(unsigned int cid,
					    CiData_DownlinkDescriptorS * desc);

/* Ci Data Send Lock to be used for TFT as well */
	void CiDataLockSendLock(void);

/* Ci Data Send UnLock to be used for TFT as well */
	void CiDataUnlockSendLock(void);

#ifdef __cplusplus
}
#endif				/*__cplusplus */
#endif				/* _CI_DATA_CLIENT_API_H_ */
