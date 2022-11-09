/*------------------------------------------------------------
(C) Copyright [2006-2011] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
    Title:       Common CI Data headerfile
    Filename:    ci_data.h
    Description: Contains defines/structs/prototypes for the R7/LTE data path
    Author:      Yossi Gabay
************************************************************************/

#if !defined(_CI_DATA_COMMON_H_)
#define      _CI_DATA_COMMON_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "linux_types.h"

/******************************************************************************
 * Defines
 *****************************************************************************/
#define CI_DATA_CHANNEL_ALL_LINK_UP_MASK            (CI_DATA_CHANNEL_CONTROL | CI_DATA_CHANNEL_UPLINK | CI_DATA_CHANNEL_DOWNLINK)

/*local static IP address uses to distinguish the "to me?" for each packet */
/*#define CI_DATA_LTM_LOCAL_DEST_STATIC_IP_ADDRESS     0x812AA8C0       //192.168.42.129 */
#define CI_DATA_LTM_LOCAL_DEST_STATIC_IP_ADDRESS     0x0101A8C0	/*192.168.1.1 */

#define CI_DATA_TOTAL_CIDS                                  8	/*total CID's */
#define CI_DATA_UPLINK_MAX_PENDING_PACKETS                  512	/*per CID */
#define CI_DATA_DOWNLINK_MAX_PENDING_PACKETS                512	/*per CID */
#define CI_DATA_UPLINK_NUM_QUEUES_PER_CID                   2	/*2 queues (HighP & LowP) per CID for uplink */
#define CI_DATA_UPLINK_TOTAL_QUEUES                         (CI_DATA_TOTAL_CIDS * CI_DATA_UPLINK_NUM_QUEUES_PER_CID)	/*2 queues (HighP & LowP) per CID for uplink */

#define CI_DATA_QUEUE_TO_CID_CONVERT(qUEUEnUM)              ((qUEUEnUM) >> 1)
#define CI_DATA_QUEUE_IS_HIGH_PRIORITY(qUEUEnUM)            ((qUEUEnUM) & 0x01)	/*0 = low priority queue, 1 = high priority queue */

#define CI_DATA_CID_TO_QUEUE_CONVERT(cID, qUEUEpRIORITY)    (((cID) << 1) + (qUEUEpRIORITY))	/*KW - use '+' instead of 'or' */

#define CI_DATA_CACHE_LINE_IN_BYTES                 32
#define CI_DATA_CACHE_LINE_MASK                     (CI_DATA_CACHE_LINE_IN_BYTES - 1)	/*all lower bits set to '1' */
/*#define CI_DATA_CACHE_LINE_TRUNCATION               0xFFFFFFE0 */
/*#define CI_DATA_CACHE_LINE_ALIGN_MASK             (~CI_DATA_CACHE_LINE_MASK)   //(~32) */
#define CI_DATA_CACHE_LINE_DESCRIPTOR_MASK          0x00000018
#define CI_DATA_CACHE_LINE_LAST_DESCRIPTOR_MASK     0x00000018
#define CI_DATA_CACHE_LINE_SHIFT                    3
#define CI_DATA_CACHE_LINE_ALIGN32_UP(aDDR)         ((((unsigned int)aDDR) + CI_DATA_CACHE_LINE_IN_BYTES - 1) & ~CI_DATA_CACHE_LINE_MASK)

/*queue handling macros */
#define CI_DATA_INDEX_INCREMENT(iNDEX, tOTALiNDEXES)             ((iNDEX) = ((iNDEX) < ((tOTALiNDEXES) - 1)) ? ((iNDEX)+1) : 0)

#define CI_DATA_INDEX_ADD(iNDEX, sTEPS, tOTALiNDEXES)            ((iNDEX) = (((iNDEX)+(sTEPS)) < (tOTALiNDEXES)) ? (iNDEX)+(sTEPS) : ((iNDEX)+(sTEPS)) - (tOTALiNDEXES))

#define CI_DATA_INDEX_PEDNING_CALC(wRITE, rEAD, tOTALiNDEXES)    ((wRITE) >= (rEAD) ? \
																	(wRITE) - (rEAD) : \
																	(tOTALiNDEXES) - ((rEAD) - (wRITE)))

#define CI_DATA_INDEX_FREE_CALC(wRITE, rEAD, tOTALiNDEXES)       ((wRITE) >= (rEAD) ? \
																	((tOTALiNDEXES - 1) - ((wRITE) - (rEAD))) : \
																	(((rEAD) - (wRITE) - 1)))

/* Uplink */
#define CI_DATA_UPLINK_INDEX_INCREMENT(iNDEX)                     CI_DATA_INDEX_INCREMENT(iNDEX, CI_DATA_UPLINK_MAX_PENDING_PACKETS)

#define CI_DATA_UPLINK_INDEX_ADD(iNDEX, sTEPS)                    CI_DATA_INDEX_ADD(iNDEX, sTEPS, CI_DATA_UPLINK_MAX_PENDING_PACKETS)

#define CI_DATA_UPLINK_INDEX_PEDNING_CALC(wRITE, rEAD)            CI_DATA_INDEX_PEDNING_CALC(wRITE, rEAD, CI_DATA_UPLINK_MAX_PENDING_PACKETS)

#define CI_DATA_UPLINK_INDEX_FREE_CALC(wRITE, rEAD)               CI_DATA_INDEX_FREE_CALC(wRITE, rEAD, CI_DATA_UPLINK_MAX_PENDING_PACKETS)

/* Downlink */
#define CI_DATA_DOWNLINK_INDEX_INCREMENT(iNDEX)                   CI_DATA_INDEX_INCREMENT(iNDEX, CI_DATA_DOWNLINK_MAX_PENDING_PACKETS)

#define CI_DATA_DOWNLINK_INDEX_ADD(iNDEX, sTEPS)                  CI_DATA_INDEX_ADD(iNDEX, sTEPS, CI_DATA_DOWNLINK_MAX_PENDING_PACKETS)

#define CI_DATA_DOWNLINK_INDEX_PEDNING_CALC(wRITE, rEAD)          CI_DATA_INDEX_PEDNING_CALC(wRITE, rEAD, CI_DATA_DOWNLINK_MAX_PENDING_PACKETS)

#define CI_DATA_DOWNLINK_INDEX_FREE_CALC(wRITE, rEAD)             CI_DATA_INDEX_FREE_CALC(wRITE, rEAD, CI_DATA_DOWNLINK_MAX_PENDING_PACKETS)

/* Uplink + Downlink */

#define CI_DATA_BUFF_FREE_CALC(aPtr, bPtr, sIZE)                  ((aPtr) >= (bPtr) ? \
																	((sIZE) - ((aPtr) - (bPtr))) : \
																	((bPtr) - (aPtr)))

/*#define CI_DATA_LOCK                                ... */
/*#define CI_DATA_UNLOCK                              ... */
/*#define CI_DATA_LOCK                                0 */
/*#define CI_DATA_UNLOCK(pREVlOCK)                    //(pREVlOCK) */

/*Debug stuff */
/*if lEVEL=0 - assert always, if lEVEL=1 - only if.... */
/*#define CI_DATA_ASSERT(lEVEL,cOND)                        ASSERT(cOND) */

#define CI_DATA_ASSERT(cOND)                        ASSERT((cOND))

#define CI_DATA_ASSERT_MSG(lEVEL, cOND, mSG)        	{   if (!(cOND))                                                                           \
															{                                                                                   \
																CI_DATA_DEBUG(0, "ASSERT at %s [line=%d]: "mSG, __func__, __LINE__);        \
																ASSERT(FALSE);                                                                  \
															}                                                                                   \
														}

#define CI_DATA_ASSERT_MSG_P(lEVEL, cOND, mSG, pARAM)	{   if (!(cOND))                                                                           \
															{                                                                                   \
																CI_DATA_DEBUG(0, "ASSERT at %s [line=%d]: "mSG, __func__, __LINE__, pARAM);  \
																ASSERT(FALSE);                                                                  \
															}                                                                                   \
														}

/******************************************************************************
 * Types
 *****************************************************************************/

	typedef enum {
		CI_DATA_OK = 0,
		CI_DATA_OK_FREE_IS_NEEDED,
		CI_DATA_ERR_CID_INVALID,
		CI_DATA_ERR_PARAMS,
		CI_DATA_ERR_DATA_DISABLED,
		CI_DATA_ERR_CID_DISABLED,
		CI_DATA_ERR_ENOMEM,	/* no more indexes or memory */
		CI_DATA_ERR_PKT_SHOULD_DROP
	} CiData_ErrorCodesE;

	typedef enum {
		CI_DATA_CHANNEL_CONTROL = 0x01,
		CI_DATA_CHANNEL_UPLINK = 0x02,
		CI_DATA_CHANNEL_DOWNLINK = 0x04,
	} CiData_ChannelTypeE;

	typedef enum {
		CI_DATA_SVC_TYPE_CSD = 0,
		CI_DATA_SVC_TYPE_PDP_DIRECTIP = 1,
		CI_DATA_SVC_TYPE_PDP_PPP_MODEM = 2,
		CI_DATA_SVC_TYPE_PDP_BT_PPP_MODEM = 3,
		CI_DATA_SVC_TYPE_PDP_HOSTIP = 4,
		CI_DATA_SVC_TYPE_PDP_LOOPBACK_MODE_B = 5,
		/*CI_DATA_SVC_TYPE_NULL                         =       5 , // used for GCF Testing */
		CI_DATA_SVC_TYPES_MAX
	} CiData_ServiceTypeE;

	typedef enum {
		CI_DATA_QUEUE_PRIORITY_LOW = 0,
		CI_DATA_QUEUE_PRIORITY_HIGH
	} CiData_QueuePriorityE;

	typedef enum {
		CI_DATA_RESERVED_NOTIFY = 0,	/*opcode sent as length and zero is not allowed in SHMEM */
		CI_DATA_UPLINK_DATA_START_NOTIFY,	/*1 */
		CI_DATA_UPLINK_DATA_STOP_NOTIFY,	/*2 */
		CI_DATA_DOWNLINK_DATA_START_NOTIFY,	/*3 */

		/* Control Notifications */
		CI_DATA_SHARED_AREA_ADDRESS_NOTIFY = 10,	/*10 */
		CI_DATA_DOWNLINK_QUEUE_START_ADDRESS_NOTIFY,	/*11 */
		CI_DATA_DOWNLINK_QUEUE_SIZE_NOTIFY,	/*12 */

		/*0x100 is the base for the start address */
		CI_DATA_UPLINK_CID_QUEUE_START_ADDRESS_NOTIFY = 0x100,	/*0x100 */

		/*0x200 is the base for the queue size */
		CI_DATA_UPLINK_CID_QUEUE_SIZE_NOTIFY = 0x200	/*0x100 */
	} CiData_NotificationsE;

	typedef enum {
		CI_DATA_HOST = 0,
		CI_DATA_HOST_LESS
	} CiData_ModeE;

	typedef struct {
		CiData_ModeE mode;
		unsigned int ltmLocalDestIpAddress;	/*local destination IP address of target */
	} CiData_GlobalConfigS;

	typedef volatile struct {
		unsigned int packetOffset;
		unsigned int packet_length;
		unsigned int packetDropped;
		unsigned char dummy[20];	/*filler to fix issues of packet_length=0 */
	} CiData_UplinkDescriptorS;

	typedef volatile struct {
		union {
			unsigned int bufferOffset;
			void *pBufferStart;
		} buffer;
		unsigned short bufferSize;

		unsigned short packetOffset;
		unsigned short packet_length;
		unsigned char cid;
		unsigned char reserved;
		unsigned int *pBufferToFree;
		struct fp_net_device *fp_src_dev;
		struct fp_net_device *fp_dst_dev;
		unsigned char isFree;
		unsigned char dummy[7];	/*filler to fix issues of packet_length=0 */
	} __attribute__ ((packed)) CiData_DownlinkDescriptorS;

	typedef volatile struct {
		unsigned short nextWriteIdx[CI_DATA_UPLINK_TOTAL_QUEUES];
		unsigned short nextReadIdx[CI_DATA_UPLINK_TOTAL_QUEUES];
		unsigned int nextReadCnfIdx[CI_DATA_UPLINK_TOTAL_QUEUES];
		unsigned short nextFreeIdx[CI_DATA_UPLINK_TOTAL_QUEUES];

		unsigned int totalDataBytesSent[CI_DATA_UPLINK_TOTAL_QUEUES];
		unsigned int totalDataBytesFree[CI_DATA_UPLINK_TOTAL_QUEUES];

		unsigned int ciDataServerActive;
		unsigned int filler[7];
		 CiData_UplinkDescriptorS
		    cidDescriptors[CI_DATA_UPLINK_TOTAL_QUEUES]
		    [CI_DATA_UPLINK_MAX_PENDING_PACKETS];

	} CiData_UplinkControlAreaDatabaseS;

	typedef volatile struct {
		unsigned int nextWriteIdx[CI_DATA_TOTAL_CIDS];
		unsigned int nextReadIdx[CI_DATA_TOTAL_CIDS];
		unsigned int nextReadCnfIdx[CI_DATA_TOTAL_CIDS];
		unsigned int nextFreeIdx[CI_DATA_TOTAL_CIDS];

		CiData_DownlinkDescriptorS
		    cidDescriptors[CI_DATA_TOTAL_CIDS]
		    [CI_DATA_DOWNLINK_MAX_PENDING_PACKETS];

	} CiData_DownlinkControlAreaDatabaseS;

	typedef volatile struct {
		CiData_UplinkControlAreaDatabaseS uplink;
		CiData_DownlinkControlAreaDatabaseS downlink;
	} CiData_SharedAreaS;

#ifdef __cplusplus
}
#endif				/*__cplusplus */
#endif				/* _CI_DATA_H_ */
