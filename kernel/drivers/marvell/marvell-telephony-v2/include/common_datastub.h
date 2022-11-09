/*------------------------------------------------------------
(C) Copyright [2006-2008] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/******************************************************************************
 *
 *  COPYRIGHT (C) 2005 Intel Corporation.
 *
 *  This software as well as the software described in it is furnished under
 *  license and may only be used or copied in accordance with the terms of the
 *  license. The information in this file is furnished for informational use
 *  only, is subject to change without notice, and should not be construed as
 *  a commitment by Intel Corporation. Intel Corporation assumes no
 *  responsibility or liability for any errors or inaccuracies that may appear
 *  in this document or any software that may be provided in association with
 *  this document.
 *  Except as permitted by such license, no part of this document may be
 *  reproduced, stored in a retrieval system, or transmitted in any form or by
 *  any means without the express written consent of Intel Corporation.
 *
 *  FILENAME:   data_channel.h
 *  PURPOSE:    data_channel API used by stub
 *
 *****************************************************************************/

#ifndef __COMMON_DATASTUB_H__
#define __COMMON_DATASTUB_H__

#include <linux_types.h>

#define CCINET_MAJOR 241
#define CCINET_IOC_MAGIC CCINET_MAJOR
#define CCINET_IP_ENABLE  _IOW(CCINET_IOC_MAGIC, 1, int)
#define CCINET_IP_DISABLE  _IOW(CCINET_IOC_MAGIC, 2, int)
#define CCINET_ALL_IP_DISABLE  _IOW(CCINET_IOC_MAGIC, 3, int)
#define CCINET_IPV6_CONFIG  _IOW(CCINET_IOC_MAGIC, 4, int)
#define CI_DATA_IOCTL_GET_V6_IPADDR  _IOW(CCINET_IOC_MAGIC, 5, int)
#define CIDATATTY_TTY_MINORS		3

typedef struct directipconfig_tag {
	int dwContextId;
	int dwProtocol;
	struct {
		int inIPAddress;
		int inPrimaryDNS;
		int inSecondaryDNS;
		int inDefaultGateway;
		int inSubnetMask;
	} ipv4;
} DIRECTIPCONFIG;

typedef enum {
	SVC_TYPE_CSD = 0,
	SVC_TYPE_PDP_DIRECTIP = 1,
	SVC_TYPE_PDP_PPP_MODEM = 2,
	SVC_TYPE_PDP_BT_PPP_MODEM = 3,
	SVC_TYPE_NULL = 4,	/* used for GCF Testing */
	SVC_TYPES_MAX
} SVCTYPE;

/*
 * must be greater the 0x10
 * (becuase we use Netlink message hdr Type to carry the cid
 * and must be higher than any PS context id)
 */
#define CSD_CALL_ID_OFFSET		20

struct datahandle_obj {
	unsigned int m_cid;	/* Context ID for PS, Call Id + CSD_CALL_ID_OFFSET , for CSD */
	SVCTYPE svctype;
};

typedef struct _datahandle {
	struct datahandle_obj handle;
	struct _datahandle *next;
} DATAHANDLELIST;

typedef struct _ccinet_ip_param {
	unsigned char cid;
	unsigned int ipv6_addr[4];
} ccinet_ip_param;
#define CSD_NETLINK_PROTO	20

#endif /* __COMMON_DATASTUB_H__ */
