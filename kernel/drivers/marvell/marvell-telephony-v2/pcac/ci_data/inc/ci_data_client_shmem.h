/*-----------------------------------------------------------
(C) Copyright [2006-2011] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
    Title:       Client CI Data SHMEM headerfile
    Filename:    ci_data_client_shmem.h
    Description: Handles the CI Data interface with SHMEM
    Author:      Yossi Gabay
************************************************************************/

#if !defined(_CI_DATA_CLIENT_SHMEM_H_)
#define      _CI_DATA_CLIENT_SHMEM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <linux/spinlock.h>

#include "ci_data_common.h"
#include "ci_data_client_api.h"
#include "ci_data_ioctl.h"
#include "ci_data_client_handler.h"

/************************************************************************
 * Defines
 ***********************************************************************/

#define CI_DATA_CONTROL_APPS_HIGH_WATERMARK         40
#define CI_DATA_CONTROL_APPS_LOW_WATERMARK          2

#define CI_DATA_UPLINK_APPS_HIGH_WATERMARK          20
#define CI_DATA_UPLINK_APPS_LOW_WATERMARK           2

#define CI_DATA_DOWNLINK_APPS_HIGH_WATERMARK        100
#define CI_DATA_DOWNLINK_APPS_LOW_WATERMARK         80

/*#define CI_DATA_DOWNLINK_APPS_TASK_STACK_SIZE       256 //OSA - set to minimum since Linux doesn't use it but it does checks it */
#define CI_DATA_DOWNLINK_APPS_TASK_PRIORITY         50
#define CI_DATA_DOWNLINK_APPS_TASK_NAME             "CIDATA_DL_TSK"
#define CI_DATA_DOWNLINK_APPS_MAX_PACKETS_IN_A_ROW  50	/*number of packets-in-a-row to handle on donwlink */

/************************************************************************
 * Types
 ***********************************************************************/

/************************************************************************
 * Function Prototypes (internal API)
 ***********************************************************************/

	void CiDataClientShmemRegister(void);
	void CiDataUplinkDataStartNotifySend(unsigned char);

/*

void        CiDataPathInit(void);
void        CiDataPathDownlinkTaskStart(void);

void        CiDataPathDownlinkRegister(unsigned int cid, ci_data_downlink_data_receive_cb downlink_data_receive_cb);
void        CiDataPathDownlinkUnregister(unsigned int cid);

unsigned int      CiDataPathUplinkCopyToInternalMemory(unsigned int cid, void *packet_address, unsigned int packet_length);
unsigned int      CiDataPathUplinkCopyToSharedMemory(unsigned int cid, void *packet_address, unsigned int packet_length);
*/

#ifdef __cplusplus
}
#endif				/*__cplusplus */
#endif				/* _CI_DATA_CLIENT_SHMEM_H_ */
