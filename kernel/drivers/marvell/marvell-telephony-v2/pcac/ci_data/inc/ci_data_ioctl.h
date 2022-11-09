/*------------------------------------------------------------
(C) Copyright [2006-2011] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
    Title:       IOCTL functionality of CI Data headerfile
    Filename:    ci_data_ioctl.h
    Description: Contains IOCTL defines/structs/prototypes for the R7/LTE data path
    Author:      Yossi Gabay
************************************************************************/

#if !defined(_CI_DATA_IOCTL_H_)
#define      _CI_DATA_IOCTL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "linux_types.h"
#include "ci_data_common.h"
#include "ci_data_client.h"
#include "ci_data_client_handler.h"
#include "ci_data_client_loopback.h"

/******************************************************************************
 * Defines
 *****************************************************************************/

#define CI_DATA_IOCTL_IOC_MAGIC		        0x8E	/*'D'*/

#define CI_DATA_IOCTL_CID_ENABLE              _IOW(CI_DATA_IOCTL_IOC_MAGIC, 1, int)
#define CI_DATA_IOCTL_CID_DISABLE             _IOW(CI_DATA_IOCTL_IOC_MAGIC, 2, int)
#define CI_DATA_IOCTL_TFT_HANDLE              _IOW(CI_DATA_IOCTL_IOC_MAGIC, 3, int)
#define CI_DATA_IOCTL_SET_CID_IPADDR          _IOW(CI_DATA_IOCTL_IOC_MAGIC, 4, int)
#define CI_DATA_IOCTL_GCF_CMD                 _IOW(CI_DATA_IOCTL_IOC_MAGIC, 5, int)
#define CI_DATA_IOCTL_GET_CID_IPADDR          _IOW(CI_DATA_IOCTL_IOC_MAGIC, 6, int)
#define CI_DATA_IOCTL_ENABLE_LOOPBACK_B       _IOW(CI_DATA_IOCTL_IOC_MAGIC, 7, int)
#define CI_DATA_IOCTL_DISABLE_LOOPBACK_B      _IOW(CI_DATA_IOCTL_IOC_MAGIC, 8, int)
#define CI_DATA_IOCTL_TEST_MODE_ACTIVATION    _IOW(CI_DATA_IOCTL_IOC_MAGIC, 9, int)
#define CI_DATA_IOCTL_TEST_MODE_TERMINATION   _IOW(CI_DATA_IOCTL_IOC_MAGIC, 10, int)
#define CI_DATA_IOCTL_CONFIG_GLOBALS          _IOW(CI_DATA_IOCTL_IOC_MAGIC, 11, int)
#define CI_DATA_IOCTL_CID_SET_PRIORITY        _IOW(CI_DATA_IOCTL_IOC_MAGIC, 12, int)
#define CI_DATA_IOCTL_GET_PDP_STATS			  _IOW(CI_DATA_IOCTL_IOC_MAGIC, 13, int)
/******************************************************************************
 * Types
 *****************************************************************************/

#ifdef __cplusplus
}
#endif				/*__cplusplus*/
#endif				/* _CI_DATA_IOCTL_H_ */
