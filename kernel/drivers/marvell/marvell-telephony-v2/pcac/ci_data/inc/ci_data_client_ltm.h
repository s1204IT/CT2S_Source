/*-----------------------------------------------------------
(C) Copyright [2006-2012] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
    Title:       CI Data Client - LTM header file
    Filename:    ci_data_client_ltm.h
    Description: Contains defines/structs/prototypes for LTM support
    Author:      Yossi Gabay
************************************************************************/

#if !defined(_CI_DATA_CLIENT_LTM_H_)
#define      _CI_DATA_CLIENT_LTM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include	<linux/version.h>
#if defined (ESHEL)		/*defined (TEL_CFG_FLAVOR_ITM) */
#include <linux/rndis_adds_on.h>
#include <linux/itm.h>
#endif
#include "ci_data_client.h"

/************************************************************************
 * Defines
 ***********************************************************************/

/************************************************************************
 * Types
 ***********************************************************************/

/************************************************************************
 * Function Prototypes
 ***********************************************************************/

	void CiDataClientLtmRegister(void);
	void CiDataClientLtmUnregister(void);
	unsigned int CiDataClientLtmSetIpAddress(char *str);
	netdev_tx_t CiDataClientLtmDownlinkDataSend(void **packetsList, struct fp_net_device *dst_dev);
	void CiDataClientLtmDownlinkFreePacket(struct fp_packet_details *packet_detail);

#ifdef __cplusplus
}
#endif				/*__cplusplus */
#endif				/* _CI_DATA_CLIENT_LTM_H_ */
