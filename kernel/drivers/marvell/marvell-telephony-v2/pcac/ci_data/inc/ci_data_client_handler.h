/*------------------------------------------------------------
(C) Copyright [2006-2011] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
    Title:       TFT Handling functionality of CI Data
    Filename:    ci_data_tft_handler.h
    Description: Contains TFT handling functionality for the R7/LTE data path
    Author:      Yossi Gabay
************************************************************************/

#if !defined(_CI_DATA_HANDLER_H_)
#define      _CI_DATA_HANDLER_H_

#if defined(__KERNEL__)
#include <linux/in.h>
#include <linux/ipv6.h>
/*#include <linux/net/ipv6.h> */
#include <linux/tcp.h>
/*#include <linux/jiffies.h> */
#include <linux/list.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include "linux_types.h"

#if defined(__KERNEL__)

	typedef enum _FRAG_STATUS {
		FRAG_STATUS_NOT_USED,
		FRAG_STATUS_USED,
		FRAG_STATUS_MAX = 0x7FFFFFFF
	} FRAG_STATUS;

	typedef struct _IP_FRAG_FOR_TFT {
		FRAG_STATUS status;
		unsigned int frag_id;
		unsigned int cid;
		unsigned int age;
	} IP_FRAG_FOR_TFP, *pIP_FRAG_FOR_TFP;

#define MAX_FRAGS_PER_CID		4

	typedef struct _IP_FRAGS_TFT_DB {
		IP_FRAG_FOR_TFP frags[MAX_FRAGS_PER_CID];
	} IP_FRAGS_TFT_DB;

#define TCP_FLAGS_MASK			0x3F
#define TCP_FLAGS_ACK_ONLY		0x10
#define TS_INVALID_INDEX 0xffffffff
#define TCP_SESSION_SHORT_TIMEOUT_SECS 5
#define TCP_SESSION_LONG_TIMEOUT_SECS 300
	typedef struct _TCP_SESSION {
		struct list_head tcp_link;
		unsigned short src_port;
		unsigned short dest_port;
		unsigned int dest_ip_v46[4];
		unsigned long last_activity_time;
		int small_pkts_cnt;
		int ack_drops_cnt;
		unsigned int prev_tcp_ack;
		unsigned int prev_tcp_seq;
		void *prev_buff_ptr;
		unsigned int uplink_q_number;	/* filled by LTM to allow drop of previous packet */
		unsigned int uplink_q_index;	/* filled by LTM to allow drop of previous packet */
		unsigned char is_v4;
		unsigned char cid;
		unsigned char is_hp;
		unsigned char status;
#ifdef TS_STATS
		unsigned int p_in;
		unsigned int p_dropped;
#endif
		char prev_flags_was_ack;
	} TCP_SESSION, *pTCP_SESSION;

	typedef struct _TS_DB {
		int do_ts;
		int do_ack_drop;
		int ack_drops_thresh;
		int small_pkts_limit;
		int low_pri_thresh;
		int high_pri_thresh;
		int curr_tcp_sessions;
		unsigned long last_cleanup_jffs;
		int max_hash_colls;
		int pkt_drops;
		int no_free_sesssions;
		int no_free_sesssions_after_cleanup;
		int num_cleanups;
		int pkts_processed;
	} TS_DB;
#endif /*__KERNEL__*/

/******************************************************************************
 * Defines
 *****************************************************************************/

#define CI_DATA_MAX_CID_TFT_FILTER      16

#define MAX_TCP_SESSIONS_POOL			256	/* must be power of 2 */
#define MAX_TCP_SESSIONS_HASH			512	/* must be power of 2 */

/******************************************************************************
 * Types
 *****************************************************************************/

/* TFT filter definition structure */
	typedef struct {
		unsigned long source_address_masked[4];	/* should be in big endian (nw order) already masked */
		unsigned long subnet_mask[4];
		unsigned long ipsec_secure_param_index;
		unsigned long flowLabel;	/*only for ipV6 -  - 0xfffffffff is invalid */
		unsigned short destination_port_range_min;	/* in little endian */
		unsigned short destination_port_range_max;	/* in little endian */
		unsigned short source_port_range_min;	/* in little endian */
		unsigned short source_port_range_max;	/* in little endian */
		unsigned char cid;	/* the cid this filter aasociated with - usualy secondary but might be primary */
		unsigned char evaluate_precedence_index;	/* must be unique within the same pdp set */
		unsigned char source_address_len;	/* if len is 4 -> ip v4 if len 16 ipv6, 0 -> is invalid */
		unsigned char protocol_number;	/*  next header for ip v6 */
		unsigned char protocol_valid;
		unsigned char ipsec_secure_param_index_valid;
		unsigned char tos_and_mask;
		unsigned char tosTC_mask;
	} CiData_TFTFilterS;

/* TFT Set structure is implemented as linked list. the linked list is in evaluation precedence order.
   There are MAX_CID_NUM linked lists
   There are MAX_CID_TFT_FILTER  * MAX_TFT_CID_NUM filters (lists) in one set (linked lists) */
	typedef struct _CiData_TFTFilterNodeS {
		CiData_TFTFilterS filter;
		/* debug info -> need to add to debug output */
		unsigned int matches;
		unsigned int no_matches;
		/* end debug info */
		struct _CiData_TFTFilterNodeS *pNext;
	} CiData_TFTFilterNodeS;

	typedef enum {
		CI_DATA_TFT_DEL_FLASE = 0,
		CI_DATA_TFT_DEL_SEC_ASSOCIATION = 1,	/* secondary is not active any more */
		CI_DATA_TFT_DEL_REPLACE_CID_FILTERS = 2,	/* delete or replace filters of given cid primary or secondary ro replace prim provide sec as 0xffffffff */
		CI_DATA_TFT_DELTFT_MAX_LAST = 0x7FFFFFFF	/* make sure size is 32 bits */
	} CiData_TftDelCmdE;

/* TFT ioctl Structure */
	typedef struct {
		CiData_TftDelCmdE del;
		unsigned int prim_cid;
		unsigned int sec_cid;
		unsigned int def_cid;
		unsigned int num_of_filters;
		CiData_TFTFilterS filter[CI_DATA_MAX_CID_TFT_FILTER];
	} CiData_TFTCommandS;

/************************************************************************
 * Function Prototypes (internal API)
 ***********************************************************************/

	void CiDataTftResetCid(unsigned int prim_cid);
	int CiDataTftHandleCommand(CiData_TFTCommandS *pTftCommand);

	int CiDataTftGetPrimaryBySecondary(unsigned int sec_cid);

#if defined(__KERNEL__)
	unsigned char CiDataUplinkProcessPacket(unsigned int *pCid,
						void *packet_address,
						unsigned int packet_length,
						CiData_QueuePriorityE *
						pQueuePriority,
						pTCP_SESSION *
						ppUsedTcpSession);
	void CiDataUplinkSetPacketInfo(pTCP_SESSION pS, unsigned int qNumber,
				       unsigned int index);
#endif
	void CiDataTrafficShapingInit(int do_ts, int do_ack_drop);
	void ConstructIcmpv6RouterSolicit(unsigned int *buf,
					  unsigned int *saddr);
	unsigned char CheckForIcmpv6RouterAdvertisment(unsigned int *buf,
						       unsigned int length,
						       unsigned int cid);
	void CiDataUplinkSetAckDropThresh(char treshCommand);
	void CiDataUpGetTsDbStat(int *pNoTcpSession, int *pNoHashColl);

#ifdef __cplusplus
}
#endif				/*__cplusplus */
#endif				/* _CI_DATA_IOCTL_H_ */
