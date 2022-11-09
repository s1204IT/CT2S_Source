/*------------------------------------------------------------
(C) Copyright [2006-2012] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.

 *(C) Copyright 2007 Marvell International Ltd.
 * All Rights Reserved
 */

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
 *  FILENAME: data_channel.c
 *  PURPOSE:  data channel handling functions for ACI
 *****************************************************************************/

#include "linux_types.h"

#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/socket.h>
#include <linux/netlink.h>
#include <net/sock.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/in.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include <linux/tcp.h>
#include <linux/jiffies.h>
#include <linux/icmpv6.h>
#include <net/ip.h>
#include <net/ip6_checksum.h>
#include "osa_kernel.h"
#include "ci_data_client.h"
#include "ci_data_client_shmem.h"
#include "ci_data_client_handler.h"

/************************************************************************
 * Local Variables
 ***********************************************************************/
#define TFT_MSG(...)        do {if (gCiDataTftDebug) pr_err(__VA_ARGS__); } while (0)

static int gCiDataTftSec2PrimMap[CI_DATA_TOTAL_CIDS] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int gCiDataTftCidToUseOnNoTftMatch[CI_DATA_TOTAL_CIDS] = { -1, -1, -1, -1, -1, -1, -1, -1 };

static unsigned char gCiDataTftDebug = FALSE;

static TCP_SESSION tcp_sessions_pool[MAX_TCP_SESSIONS_POOL];
static struct list_head tcp_sessions_hash[MAX_TCP_SESSIONS_HASH];
static LIST_HEAD(sessions_free_list);
static TS_DB ts_db;

static IP_FRAGS_TFT_DB gTftFragsDBIPv4[CI_DATA_TOTAL_CIDS];
static IP_FRAGS_TFT_DB gTftFragsDBIPv6[CI_DATA_TOTAL_CIDS];
static int gFragsAge;
/***********************************************************************************\
* Functions
************************************************************************************/

/***********************************************************************************\
*   Name:        CiDataTftDeleteAllFilters
*
*   Description: Delete all filters of specific primary cid
*
*   Params:      prim_cid - primary cid
*
*   Return:
\***********************************************************************************/
static void CiDataTftDeleteAllFilters(unsigned int prim_cid)
{
	CiData_TFTFilterNodeS *temp_filter;

	CiDataLockSendLock();

	while (gCiDataClient.cid[prim_cid].pTftFilterList != NULL) {
		temp_filter = gCiDataClient.cid[prim_cid].pTftFilterList;
		gCiDataClient.cid[prim_cid].pTftFilterList = gCiDataClient.cid[prim_cid].pTftFilterList->pNext;

		kfree(temp_filter);
	}

	CiDataUnlockSendLock();
}

/***********************************************************************************\
*   Name:        CiDataTftGetPrimaryBySecondary
*
*   Description: Gets the primary cid of particular secondary
*
*   Params:      sec_cid - secondary cid
*
*   Return:      the primary cid
\***********************************************************************************/
int CiDataTftGetPrimaryBySecondary(unsigned int sec_cid)
{
	/* unsigned can never be lesss than 0 */
	if (sec_cid >= CI_DATA_TOTAL_CIDS)
		return -1;

	if (gCiDataTftSec2PrimMap[sec_cid] != -1) {	/* this is secondary - return the primary */
		return gCiDataTftSec2PrimMap[sec_cid];
	} else {		/* this is primary - return it back */
		return sec_cid;
	}
}

/***********************************************************************************\
*   Name:        CiDataTftDeleteCidFilters
*
*   Description: Delete all filters of particular secondary cid
*
*   Params:      cid - any cid
*
*   Return:
\***********************************************************************************/
static void CiDataTftDeleteCidFilters(unsigned int cid)
{
	CiData_TFTFilterNodeS *pfilter, *temp_filter, *prev_filter;
	unsigned int prim_cid = CiDataTftGetPrimaryBySecondary(cid);

	if (prim_cid == -1) {
		prim_cid = cid;
	}

	CiDataLockSendLock();

	prev_filter = pfilter = gCiDataClient.cid[prim_cid].pTftFilterList;

	while (pfilter != NULL) {
		if (pfilter->filter.cid == cid) {
			temp_filter = pfilter;
			if (prev_filter != pfilter) {	/* not the first filter */
				prev_filter->pNext = pfilter->pNext;
				pfilter = pfilter->pNext;
			} else {	/*first filter */
				gCiDataClient.cid[prim_cid].pTftFilterList = prev_filter = pfilter = pfilter->pNext;
			}
			kfree(temp_filter);
		} else {
			prev_filter = pfilter;
			pfilter = pfilter->pNext;
		}
	}

	CiDataUnlockSendLock();
}

/***********************************************************************************\
*   Name:        CiDataTftSecondary2PrimaryCleanByPrimary
*
*   Description: Delete all filters of particular secondary cid
*
*   Params:      prim_cid - primary cid
*
*   Return:      0 - success
*               -1 - fail
\***********************************************************************************/
static int CiDataTftSecondary2PrimaryCleanByPrimary(unsigned int prim_cid)
{
	int i;

	if (prim_cid > CI_DATA_TOTAL_CIDS)	/*invalid primary cid */
		return -1;

	for (i = 0; i < CI_DATA_TOTAL_CIDS; i++) {
		if (gCiDataTftSec2PrimMap[i] == prim_cid)
			gCiDataTftSec2PrimMap[i] = -1;
	}
	return 0;
}

/***********************************************************************************\
*   Name:        CiDataTftSecondary2PrimarySet
*
*   Description: Set or delete secondary PDP
*
*   Params:      sec_cid - secondary cid (to delete or add)
*                prim_cid - primary cid
*                del - TRUE for delete, FALSE for add
*
*   Return:      0 - success
*               -1 - fail
\***********************************************************************************/
static int CiDataTftSecondary2PrimarySet(unsigned int sec_cid, unsigned int prim_cid, unsigned char del)
{
	CI_DATA_DEBUG(3, "CiDataTftSecondary2PrimarySet: sec_cid=%d, prim_cid=%d, del=%d", sec_cid, prim_cid, del);

	if (sec_cid >= CI_DATA_TOTAL_CIDS)	/*invalid secondary cid */
		return -1;

	if (!del) {		/* Information is updated */
		CiData_CidInfoS cidInfo;

		gCiDataTftSec2PrimMap[sec_cid] = prim_cid;

		cidInfo.cid = sec_cid;
		cidInfo.connectionType = 0;
		cidInfo.serviceType = gCiDataClient.cid[prim_cid].serviceType;	/*using primary service type */
		gCiDataClient.cid[sec_cid].downlink_data_receive_cb = gCiDataClient.cid[prim_cid].downlink_data_receive_cb;
		CiDataClientCidEnable(&cidInfo, FALSE);
	} else {		/* information is deleted */
		CiDataTftDeleteCidFilters(sec_cid);
		gCiDataTftSec2PrimMap[sec_cid] = -1;
		CiDataClientCidDisable(sec_cid);
	}

	return 0;
}

/***********************************************************************************\
*   Name:        CiDataTftInsertFilter
*
*   Description: Add TFT filter to cid list
*
*   Params:      pTftCommand - the tft command parameters + list of filters
*                numFilterToAdd - index of filter from above list to add
*
*   Return:      0 - success
*               -1 - fail
\***********************************************************************************/
static int CiDataTftInsertFilter(CiData_TFTCommandS *pTftCommand, unsigned int numFilterToAdd)
{
	CiData_TFTFilterNodeS *pfilter, *prev_filter /* , *temp_filter */ ;
	CiData_TFTFilterNodeS *pNewFilter;

	/*allocate room and fill tft info */
	pNewFilter = kmalloc(sizeof(CiData_TFTFilterNodeS), GFP_KERNEL);
	if (pNewFilter == NULL) {
		CI_DATA_DEBUG(1, "CiDataTftInsertFilter: error allocating memory");
		return -1;
	}
	pNewFilter->pNext = NULL;
	pNewFilter->matches = 0;
	pNewFilter->no_matches = 0;
	memcpy(&pNewFilter->filter, &pTftCommand->filter[numFilterToAdd], sizeof(CiData_TFTFilterS));

	CI_DATA_DEBUG(1,
		      "\nCiDataTftInsertFilter: numFilterToAdd=%d, ports - src min=%d, src max=%d, dst min=%d, dst max=%d ipsec%x(%d) ",
		      numFilterToAdd, pNewFilter->filter.source_port_range_min,
		      pNewFilter->filter.source_port_range_max, pNewFilter->filter.destination_port_range_min,
		      pNewFilter->filter.destination_port_range_max, (int)pNewFilter->filter.ipsec_secure_param_index,
		      (int)pNewFilter->filter.ipsec_secure_param_index_valid);

	CiDataLockSendLock();

	/*find the place to add it */
	if (gCiDataClient.cid[pTftCommand->prim_cid].pTftFilterList == NULL) {	/*first filter to add */
		gCiDataClient.cid[pTftCommand->prim_cid].pTftFilterList = pNewFilter;
	} else {		/*concatenate according evaluate precedence order */
		for (prev_filter = pfilter = gCiDataClient.cid[pTftCommand->prim_cid].pTftFilterList; pfilter != NULL;
		     pfilter = pfilter->pNext) {
			if (pNewFilter->filter.evaluate_precedence_index < pfilter->filter.evaluate_precedence_index) {
				pNewFilter->pNext = pfilter;

				if (prev_filter == pfilter) {	/*need to change the head */
					gCiDataClient.cid[pTftCommand->prim_cid].pTftFilterList = pNewFilter;
				} else {	/*add in current place */
					prev_filter->pNext = pNewFilter;
				}

				break;	/*added - exit the loop */
			} else if (pNewFilter->filter.evaluate_precedence_index == pfilter->filter.evaluate_precedence_index) {	/* sanity check - if same index then error */
				CiDataUnlockSendLock();
				CI_DATA_DEBUG(1, "CiDataTftInsertFilter: ERROR same index (index=%d)",
					      pNewFilter->filter.evaluate_precedence_index);
				kfree(pNewFilter);
				return -1;
			}

			prev_filter = pfilter;
		}

		/*check if need to add as last one */
		if (pfilter == NULL) {	/* add as the last filter */
			prev_filter->pNext = pNewFilter;
		}
	}

	CiDataUnlockSendLock();

	return 0;
}

/***********************************************************************************\
* A P I's
\***********************************************************************************/

/***********************************************************************************\
*   Name:        CiDataTftToggleDebugMode
*
*   Description: Toggle DEBUG TFT mode
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataTftToggleDebugMode(void)
{
	gCiDataTftDebug = !(gCiDataTftDebug);
	printk(KERN_ERR "CiDataTft: new DEBUG state %d", gCiDataTftDebug);
}

/***********************************************************************************\
*   Name:        CiDataTftResetCid
*
*   Description: Reset all CID secondary and TFT's
*
*   Params:      prim_cid - primary cid
*
*   Return:
\***********************************************************************************/
void CiDataTftResetCid(unsigned int prim_cid)
{
	CI_DATA_DEBUG(3, "CiDataTftCidReset: prim_cid=%d", prim_cid);

	/*delete associated primary/secondary */
	CiDataTftSecondary2PrimaryCleanByPrimary(prim_cid);

	/*delete all related filters */
	CiDataTftDeleteAllFilters(prim_cid);

	/* clean ip frags db */
	memset(&gTftFragsDBIPv4[prim_cid], 0, sizeof(gTftFragsDBIPv4[prim_cid]));
	memset(&gTftFragsDBIPv6[prim_cid], 0, sizeof(gTftFragsDBIPv6[prim_cid]));
}

/***********************************************************************************\
*   Name:        CiDataTftHandleCommand
*
*   Description: Handle new TFT command - add secondary, del secondary, add TFT
*
*   Params:      pTftCommand - the tft command parameteters
*
*   Return:
\***********************************************************************************/
int CiDataTftHandleCommand(CiData_TFTCommandS *pTftCommand)
{
	int i;

	CI_DATA_DEBUG(3, "CiDataTftHandleCommand: TFT command (num_of_filters=%d, prim_cid=%d, sec_cid=%d, del=%d)",
		      pTftCommand->num_of_filters, pTftCommand->prim_cid, pTftCommand->sec_cid, pTftCommand->del);

																										/*YG: need to define 0xFFFFFFFF as invalid_cid */
	if (pTftCommand->prim_cid >= CI_DATA_TOTAL_CIDS || ((pTftCommand->def_cid >= CI_DATA_TOTAL_CIDS) && (pTftCommand->def_cid != 0xFFFFFFFF)) || ((pTftCommand->sec_cid >= CI_DATA_TOTAL_CIDS) && (pTftCommand->sec_cid != 0xFFFFFFFF))) {
		CI_DATA_DEBUG(1, "CiDataTftHandleCommand: Error in parameters (prim_cid=%d, sec_cid=%d, del=%d def=%d)",
			      pTftCommand->prim_cid, pTftCommand->sec_cid, pTftCommand->def_cid, pTftCommand->del);
		return -1;
	}

	gCiDataTftCidToUseOnNoTftMatch[pTftCommand->prim_cid] = (int)pTftCommand->def_cid;

	if (pTftCommand->del == CI_DATA_TFT_DEL_SEC_ASSOCIATION) {	/* delete cid - here handle only secondary */

		/*delete association of primary - this will also remove the filters */
		if (CiDataTftSecondary2PrimarySet(pTftCommand->sec_cid, 0xFFFFFFFF, TRUE) == -1) {
			CI_DATA_DEBUG(1, "CiDataTftHandleCommand (del): invalid secondary cid (sec_cid=%d)",
				      pTftCommand->sec_cid);
			return -1;
		}

		return 0;
	}

	if (pTftCommand->del == CI_DATA_TFT_DEL_REPLACE_CID_FILTERS) {
		/*clear sec filters from primary */
		if (pTftCommand->sec_cid != 0xFFFFFFFF)
			CiDataTftDeleteCidFilters(pTftCommand->sec_cid);
		/*clear primary filters */
		else
			CiDataTftDeleteCidFilters(pTftCommand->prim_cid);

		/* now if filter are provided add them back */
	} else if (pTftCommand->sec_cid != 0xFFFFFFFF) {
		/* not delete and not replace */
		/* set secondary pdp command ---> need to update gCiDataTftSec2PrimMap */
		if (CiDataTftSecondary2PrimarySet(pTftCommand->sec_cid, pTftCommand->prim_cid, FALSE) == -1) {
			CI_DATA_DEBUG(1, "CiDataTftHandleCommand (set), invalid secondary cid (sec_cid=%d)",
				      pTftCommand->sec_cid);
			return -1;
		}
	}
	/* add the filter parameters if exist, filters are added according evaluation precedence order */
	for (i = 0; i < pTftCommand->num_of_filters; i++) {
		/* insert  new filter */
		if (CiDataTftInsertFilter(pTftCommand, i) == -1) {
			CI_DATA_DEBUG(1, "CiDataTftHandleCommand: no memory for new filters");
			return -1;
		}
	}

	return 0;
}

#if defined (TS_DEBUG)

void dumpbuf(void *p, int len)
{
	unsigned char *cp = (unsigned char *)p;
	int i;
	printk("\nBUF len=%d:\n", len);
	for (i = 0; i < len; i++) {
		printk("%02X ", *cp++);
		if (i && (i % 16) == 0)
			printk("\n");
	}
}

int ListCount(struct list_head *lh)
{
	int i = 0;
	struct list_head *tmp;
	list_for_each(tmp, lh) {
		i++;
	}
	return i;
};
#endif
void dumpTsDb(void)
{

	printk(KERN_ERR "num cur sessions %d pkts %d hashcoll %d drops=%d nfree=%d nfreecup=%d ncup=%d\n",
	       ts_db.curr_tcp_sessions,
	       ts_db.pkts_processed,
	       ts_db.max_hash_colls,
	       ts_db.pkt_drops, ts_db.no_free_sesssions, ts_db.no_free_sesssions_after_cleanup, ts_db.num_cleanups);
}

static inline unsigned int TSHashFunc(unsigned char cid, unsigned char is_v4, unsigned short *src_port,
				      unsigned short *dest_port, unsigned int *dest_ip_v46)
{
	unsigned short folded_addr;

	if (is_v4)
		folded_addr = (*(unsigned short *)dest_ip_v46) ^ (*(((unsigned short *)dest_ip_v46) + 1));
	else {
		int i;
		folded_addr = 0;
		for (i = 0; i < 4; i++)
			folded_addr ^=
			    (*(((unsigned short *)dest_ip_v46) + i * 2)) ^
			    (*(((unsigned short *)dest_ip_v46) + (i * 2) + 1));
	}

	return (unsigned int)((((*src_port) ^ (*dest_port) ^ folded_addr) + cid) & (MAX_TCP_SESSIONS_HASH - 1));
}

static inline void TSResetEntryInfo(pTCP_SESSION pS)
{
	pS->small_pkts_cnt = 0;

	pS->is_hp = FALSE;
	pS->last_activity_time = jiffies;
	pS->status = 0;
	pS->ack_drops_cnt = 0;

/*  No need to reset these */
/*      pS->prev_tcp_ack; */
/*      pS->prev_tcp_seq=0;; */
	pS->prev_flags_was_ack = 0;
	pS->prev_buff_ptr = NULL;
#ifdef TS_STATS
	pS->p_in = 0;
	pS->p_dropped = 0;
#endif
}

static inline int TSIsActiveEntry(pTCP_SESSION pS, int secs)
{
	if (time_after(jiffies, (pS->last_activity_time + HZ * secs))) {
		return 0;
	}

	return 1;
}

static inline void TSSetEntryInfo(pTCP_SESSION pS, unsigned char cid, unsigned char is_v4, unsigned short *src_port,
				  unsigned short *dest_port, unsigned int *dest_ip_v46)
{
	int addr_len;

	TSResetEntryInfo(pS);

	pS->cid = cid;
	pS->is_v4 = is_v4;
	pS->src_port = *src_port;
	pS->dest_port = *dest_port;
	addr_len = is_v4 ? 4 : 16;

	memcpy(&pS->dest_ip_v46, dest_ip_v46, addr_len);
}

static inline int TSMatchEntryInfo(pTCP_SESSION pS, unsigned char cid, unsigned char is_v4, unsigned short *src_port,
				   unsigned short *dest_port, unsigned int *dest_ip_v46)
{
	int addr_len = is_v4 ? 4 : 16;
	return (pS->cid == cid && pS->is_v4 == is_v4 && pS->src_port == *src_port &&
		pS->dest_port == *dest_port && !memcmp(&pS->dest_ip_v46, dest_ip_v46, addr_len));
}

static inline void TSHashCleaup(void)
{
	unsigned int i;

	for (i = 0; (i < MAX_TCP_SESSIONS_POOL) && (ts_db.curr_tcp_sessions > 0); i++) {
		if (tcp_sessions_pool[i].status &&
		    !TSIsActiveEntry(&tcp_sessions_pool[i], TCP_SESSION_SHORT_TIMEOUT_SECS)) {
			tcp_sessions_pool[i].status = 0;
			ts_db.curr_tcp_sessions--;
			list_move(&tcp_sessions_pool[i].tcp_link, &sessions_free_list);
#ifdef TS_DEBUG
			printk(KERN_ERR "cleanup session pkts=%d dropped=%d hp=%d sp=%d dp=%d - free list count=%d\n",
			       tcp_sessions_pool[i].p_in,
			       tcp_sessions_pool[i].p_dropped,
			       tcp_sessions_pool[i].is_hp,
			       __cpu_to_be16(tcp_sessions_pool[i].src_port),
			       __cpu_to_be16(tcp_sessions_pool[i].dest_port), ListCount(&sessions_free_list));
#endif
		}
	}
}

static inline pTCP_SESSION TSHashFindOrAdd(unsigned char cid, unsigned char is_v4, unsigned short *src_port,
					   unsigned short *dest_port, unsigned int *dest_ip_v46)
{
	pTCP_SESSION pS = NULL;
	struct list_head *plh;
	int hash_colls = 0;

	unsigned int hash_index = TSHashFunc(cid, is_v4, src_port, dest_port, dest_ip_v46);

	list_for_each(plh, &tcp_sessions_hash[hash_index]) {
		pS = list_entry(plh, TCP_SESSION, tcp_link);
		if (TSMatchEntryInfo(pS, cid, is_v4, src_port, dest_port, dest_ip_v46)) {
			if (!TSIsActiveEntry(pS, TCP_SESSION_LONG_TIMEOUT_SECS)) {
				TSResetEntryInfo(pS);
			}
			return pS;
		}
		hash_colls++;
		if (hash_colls > ts_db.max_hash_colls) {
			ts_db.max_hash_colls = hash_colls;
		}
	}

	if (list_empty(&sessions_free_list)) {
		if (time_after(jiffies, (ts_db.last_cleanup_jffs + HZ))) {
			ts_db.last_cleanup_jffs = jiffies;
			ts_db.num_cleanups++;
			TSHashCleaup();
			if (list_empty(&sessions_free_list)) {
				ts_db.no_free_sesssions_after_cleanup++;
				return NULL;
			}
		} else {
			ts_db.no_free_sesssions++;
			return NULL;
		}
	}
	/* list is not empty */
	plh = sessions_free_list.next;
	list_move(plh, &tcp_sessions_hash[hash_index]);
#ifdef TS_DEBUG
	printk(KERN_ERR "added at index %d hash list count=%d free count=%d\n", hash_index,
	       ListCount(&tcp_sessions_hash[hash_index]), ListCount(&sessions_free_list));
#endif
	pS = list_entry(plh, TCP_SESSION, tcp_link);
	TSSetEntryInfo(pS, cid, is_v4, src_port, dest_port, dest_ip_v46);
	ts_db.curr_tcp_sessions++;
	pS->status = 1;
	return pS;
}

void CiDataTrafficShapingInit(int do_ts, int do_ack_drop)
{
	int i;

	memset(tcp_sessions_pool, 0, sizeof(tcp_sessions_pool));

	memset(&ts_db, 0, sizeof(ts_db));

	ts_db.do_ts = do_ts;
	ts_db.do_ack_drop = do_ack_drop;
	ts_db.ack_drops_thresh = 5;
	ts_db.small_pkts_limit = 160;
	ts_db.low_pri_thresh = -2;
	ts_db.high_pri_thresh = 8;
	ts_db.last_cleanup_jffs = jiffies;

	INIT_LIST_HEAD(&sessions_free_list);
	for (i = 0; i < MAX_TCP_SESSIONS_POOL; i++) {
		INIT_LIST_HEAD(&tcp_sessions_pool[i].tcp_link);
		list_add(&tcp_sessions_pool[i].tcp_link, &sessions_free_list);
	}

	for (i = 0; i < MAX_TCP_SESSIONS_HASH; i++) {
		INIT_LIST_HEAD(&tcp_sessions_hash[i]);
	}

	memset(gTftFragsDBIPv4, 0, sizeof(gTftFragsDBIPv4));
	memset(gTftFragsDBIPv6, 0, sizeof(gTftFragsDBIPv6));
}

static inline unsigned char IPv4FragNotFirst(struct iphdr *pipv4)
{
	/* if the fragment offset is not 0 this is a non first fragment */
	return (unsigned char)((pipv4->frag_off & (__cpu_to_be16(IP_OFFSET))) != 0);
}

static inline unsigned char IPv4FragLast(struct iphdr *pipv4)
{
	/* assuming we already know this is a fragment */
	/* if the more frags is 0 - then this is the last fragment */
	return (unsigned char)((pipv4->frag_off & (__cpu_to_be16(IP_MF))) == 0);
}

static inline unsigned char IPv4FragFirst(struct iphdr *pipv4)
{
	/* if the frag offset is 0 but the more frags is not 0 its a first fragment */
	return (unsigned char)((IPv4FragNotFirst(pipv4) == FALSE) && (IPv4FragLast(pipv4) == FALSE));
}

static inline unsigned char IPv6FragNotFirst(struct frag_hdr *fh)
{
	/* if the fragment offset is not 0 this is a non first fragment */
	return (unsigned char)((fh->frag_off & (__cpu_to_be16(~0x7))) != 0);
}

static inline unsigned char IPv6FragLast(struct frag_hdr *fh)
{
	/* assuming we already know this is a fragment */
	/* if the more frags is 0 - then this is the last fragment */
	return (unsigned char)((fh->frag_off & (__cpu_to_be16(IP6_MF))) == 0);
}

static inline unsigned char IPv6FragFirst(struct frag_hdr *fh)
{
	/* if the frag offset is 0 but the more frags is not 0 its a first fragment */
	return (unsigned char)((IPv6FragNotFirst(fh) == FALSE) && (IPv6FragLast(fh) == FALSE));
}

static inline unsigned char CiDataTftCheckPorts(struct tcphdr *ptcp, CiData_TFTFilterNodeS *pfilter)
{
	if (ptcp != NULL) {
		/* this is either TCP or UDP / For both ipv4 and v6 */
		if (__cpu_to_be16(ptcp->source) > pfilter->filter.source_port_range_max ||
		    __cpu_to_be16(ptcp->source) < pfilter->filter.source_port_range_min) {

			TFT_MSG("TCP/UDP src port p=%d t=%d..%d -> NO Match",
				__cpu_to_be16(ptcp->source),
				pfilter->filter.source_port_range_min, pfilter->filter.source_port_range_max);

			pfilter->no_matches++;
			return FALSE;
		}

		if (__cpu_to_be16(ptcp->dest) > pfilter->filter.destination_port_range_max ||
		    __cpu_to_be16(ptcp->dest) < pfilter->filter.destination_port_range_min) {

			TFT_MSG("TCP/UDP dest port p=%d t=%d..%d -> NO Match",
				__cpu_to_be16(ptcp->dest),
				pfilter->filter.destination_port_range_min, pfilter->filter.destination_port_range_max);
			pfilter->no_matches++;
			return FALSE;
		}
		TFT_MSG("TCP/UDP ports   -> Match\n");
	} else if (pfilter->filter.source_port_range_max != 0xFFFF ||
		   pfilter->filter.destination_port_range_max != 0xFFFF ||
		   pfilter->filter.source_port_range_min != 0 || pfilter->filter.destination_port_range_min != 0) {

		TFT_MSG("Not TCP/UDP and ports are valid - > NO Match\n");
		pfilter->no_matches++;
		return FALSE;
	} else {
		TFT_MSG("Not TCP/UDP and ports are invalid -> Match\n");
	}
	return TRUE;
}

static inline unsigned char CiDataDoIPv4TFT(struct iphdr *pipv4, struct tcphdr *ptcp, unsigned int *pCid, __be32 *pspi)
{
	CiData_TFTFilterNodeS *pfilter;

	if (gCiDataClient.cid[*pCid].pTftFilterList == NULL) {
		TFT_MSG("TFT No Filters for Cid %d\n", *pCid);
		return TRUE;
	}

	CiDataLockSendLock();

	if (IPv4FragNotFirst(pipv4)) {
		int i;
		pIP_FRAG_FOR_TFP pIPF = gTftFragsDBIPv4[*pCid].frags;

		for (i = 0; i < MAX_FRAGS_PER_CID; i++) {
			if (pIPF[i].status == FRAG_STATUS_USED && (unsigned int)(pipv4->id) == pIPF[i].frag_id) {
				if (IPv4FragLast(pipv4)) {
					TFT_MSG("IPv4 Last Frag of ip id %d - > clear index %d\n", pipv4->id, i);
					pIPF[i].status = FRAG_STATUS_NOT_USED;
				}
				TFT_MSG("IPv4 Frag match of ip id %d index %d -> p_cid %d  s_cid %d\n", pipv4->id, i,
					*pCid, pIPF[i].cid);
				*pCid = (unsigned int)pIPF[i].cid;
				CiDataUnlockSendLock();
				return TRUE;
			}
		}
		TFT_MSG("IPv4 Frag match of ip id %d  -> NO MATCH\n", pipv4->id);
	}

	for (pfilter = gCiDataClient.cid[*pCid].pTftFilterList; pfilter != NULL; pfilter = pfilter->pNext) {
		if (pfilter->filter.protocol_valid) {
			if (pipv4->protocol != (__u8) pfilter->filter.protocol_number) {

				pfilter->no_matches++;
				TFT_MSG("TFT Cid %d Protocol p=%d t=%d -> No Match\n", *pCid, pipv4->protocol,
					pfilter->filter.protocol_number);
				continue;
			}
			TFT_MSG("TFT Cid %d Protocol valid  %d -> Match\n", *pCid, pipv4->protocol);
		} else {
			TFT_MSG("TFT Cid %d Protocol invalid -> Match\n", *pCid);
		}

		if ((pipv4->tos & pfilter->filter.tosTC_mask) != (__u8) pfilter->filter.tos_and_mask) {

			pfilter->no_matches++;
			TFT_MSG("TFT Cid %d TOS p_tos=%d t_mask=%d t_tosandmask %d-> NO Match\n",
				*pCid, pipv4->tos, pfilter->filter.tosTC_mask, pfilter->filter.tos_and_mask);
			continue;
		} else {
			TFT_MSG("TFT Cid %d TOS -> Match\n", *pCid);
		}

		if (pfilter->filter.source_address_len > 0 &&
		    ((pfilter->filter.source_address_len != 4) ||
		     (pfilter->filter.source_address_masked[0] != (pipv4->daddr & pfilter->filter.subnet_mask[0])))) {

			TFT_MSG("TFT Cid %d ADDR p=%08x t: len=%d addrmasked =%08lx mask=%08lx  ->NO Match\n",
				*pCid,
				pipv4->daddr,
				pfilter->filter.source_address_len,
				pfilter->filter.source_address_masked[0], pfilter->filter.subnet_mask[0]);

			pfilter->no_matches++;
			continue;
		}
		TFT_MSG("TFT Cid %d ADDR -> Match\n", *pCid);

		if (!CiDataTftCheckPorts(ptcp, pfilter)) {
			continue;
		}

		if (pfilter->filter.ipsec_secure_param_index_valid) {
			if (!pspi) {
				TFT_MSG("TFT IPv4 Cid %d filter spi %08x valid but not in packet -> NO MATCH\n", *pCid,
					(int)pfilter->filter.ipsec_secure_param_index);
				pfilter->no_matches++;
				continue;
			} else {
				if (*pspi != pfilter->filter.ipsec_secure_param_index) {
					TFT_MSG("TFT IPv4 Cid %d spi %08x  filter spi %08x -> NO MATCH\n", *pCid,
						(int)(*pspi), (int)pfilter->filter.ipsec_secure_param_index);
					pfilter->no_matches++;
					continue;
				}
			}
			TFT_MSG("TFT IPv4 Cid %d SPI ->MATCH\n", *pCid);
		}
		/* filter is match - return filter cid (primary or secondary) */
		pfilter->matches++;

		TFT_MSG("TFT CID %d to CID %d -> MATCH !!!!\n", *pCid, pfilter->filter.cid);

		if (IPv4FragFirst(pipv4)) {
			pIP_FRAG_FOR_TFP pIPF = gTftFragsDBIPv4[*pCid].frags;
			int i, index = 0;
			unsigned int minAge = 0xFFFFFFFF;

			gFragsAge++;

			if (gFragsAge == 0xFFFFFFFF)
				gFragsAge++;

			for (i = 0; i < MAX_FRAGS_PER_CID; i++) {
				if (pIPF[i].status == FRAG_STATUS_NOT_USED) {
					index = i;
					break;
				}
				if (pIPF[i].age < minAge) {
					minAge = pIPF[i].age;
					index = i;
				}
			}

			if (i < MAX_FRAGS_PER_CID) {
				TFT_MSG("IPv4 First Frag to cid %d ip id %d index %d\n", pfilter->filter.cid,
					(unsigned int)(pipv4->id), index);
			} else {
				TFT_MSG("IPv4 First Frag to cid %d ip id %d reused index %d\n", pfilter->filter.cid,
					(unsigned int)(pipv4->id), index);
			}
			pIPF[index].status = FRAG_STATUS_USED;
			pIPF[index].cid = (unsigned int)(pfilter->filter.cid);
			pIPF[index].age = gFragsAge;
			pIPF[index].frag_id = (unsigned int)(pipv4->id);
		}

		*pCid = (unsigned int)(pfilter->filter.cid);

		CiDataUnlockSendLock();
		return TRUE;
	}

	CiDataUnlockSendLock();

	/* we had no match - need to look on def cid */
	if (gCiDataTftCidToUseOnNoTftMatch[*pCid] != -1) {
		*pCid = (unsigned int)gCiDataTftCidToUseOnNoTftMatch[*pCid];
		return TRUE;
	}

	return FALSE;		/*  packet should be dropped */
}

static inline unsigned char CiDataDoIPv6TFT(struct ipv6hdr *pipv6, struct frag_hdr *fh, struct tcphdr *ptcp,
					    unsigned int *pCid, __u8 nexthdr, __be32 *pspi)
{
	CiData_TFTFilterNodeS *pfilter;

	if (gCiDataClient.cid[*pCid].pTftFilterList == NULL)
		return TRUE;

	CiDataLockSendLock();

	if (fh != NULL) {
		if (IPv6FragNotFirst(fh)) {
			int i;
			pIP_FRAG_FOR_TFP pIPF = gTftFragsDBIPv6[*pCid].frags;

			for (i = 0; i < MAX_FRAGS_PER_CID; i++) {
				if (pIPF[i].status == FRAG_STATUS_USED
				    && (unsigned int)(fh->identification) == pIPF[i].frag_id) {
					if (IPv6FragLast(fh)) {
						TFT_MSG("IPv6 Last Frag of ip id %d - > clear index %d\n",
							fh->identification, i);
						pIPF[i].status = FRAG_STATUS_NOT_USED;
					}
					TFT_MSG("IPv6 Frag match of ip id %d index %d -> p_cid %d  s_cid %d\n",
						fh->identification, i, *pCid, pIPF[i].cid);
					*pCid = (unsigned int)pIPF[i].cid;
					CiDataUnlockSendLock();
					return TRUE;
				}
			}
			TFT_MSG("IPv6 Frag match of ip id %d  -> NO MATCH\n", fh->identification);
		}
	}

	for (pfilter = gCiDataClient.cid[*pCid].pTftFilterList; pfilter != NULL; pfilter = pfilter->pNext) {
		if (pfilter->filter.protocol_valid) {
			if (nexthdr != (__u8) pfilter->filter.protocol_number) {
				pfilter->no_matches++;
				TFT_MSG("TFT IPv6 Cid %d Protocol p=%d t=%d -> No Match\n", *pCid, (int)nexthdr,
					pfilter->filter.protocol_number);
				continue;
			}
		}
		TFT_MSG("TFT IPv6 Cid %d Protocol %d -> MATCH\n", *pCid, pfilter->filter.protocol_number);

		if (pfilter->filter.tosTC_mask) {
			/* create masked v6 traffic class */
			unsigned char tos = (unsigned char)pipv6->priority;

			tos <<= 4;
			tos |= ((pipv6->flow_lbl[0] & 0xF0) >> 4);
			tos &= pfilter->filter.tosTC_mask;

			if (tos != pfilter->filter.tos_and_mask) {
				pfilter->no_matches++;
				TFT_MSG("TFT IPv6 Cid %d TOS p=%d t=%d -> No Match\n", *pCid, (int)tos,
					(int)pfilter->filter.tos_and_mask);
				continue;
			}
		}
		TFT_MSG("TFT IPv6 Cid %d TOS %d -> MATCH\n", *pCid, (int)pfilter->filter.tos_and_mask);

		if (pfilter->filter.source_address_len > 0 &&
		    ((pfilter->filter.source_address_len != 16) ||
		     ((pipv6->daddr.s6_addr32[0] & pfilter->filter.subnet_mask[0]) !=
		      pfilter->filter.source_address_masked[0]) ||
		     ((pipv6->daddr.s6_addr32[1] & pfilter->filter.subnet_mask[1]) !=
		      pfilter->filter.source_address_masked[1]) ||
		     ((pipv6->daddr.s6_addr32[2] & pfilter->filter.subnet_mask[2]) !=
		      pfilter->filter.source_address_masked[2]) ||
		     ((pipv6->daddr.s6_addr32[3] & pfilter->filter.subnet_mask[3]) !=
		      pfilter->filter.source_address_masked[3]))) {

			TFT_MSG("TFT IPv6 Cid %d addr -> NO MATCH\n", *pCid);
			pfilter->no_matches++;
			continue;
		}
		TFT_MSG("TFT IPv6 Cid %d addr -> MATCH\n", *pCid);

		if (!CiDataTftCheckPorts(ptcp, pfilter)) {
			continue;
		}

		if (pfilter->filter.flowLabel != 0xffffffff) {
			__u8 *plbl = (__u8 *) &pfilter->filter.flowLabel;

			if (((pipv6->flow_lbl[0] & 0x0F) != (plbl[1] & 0x0F)) ||
			    pipv6->flow_lbl[1] != plbl[2] || pipv6->flow_lbl[2] != plbl[3]) {

				TFT_MSG("TFT IPv6 Cid %d flow label no match pflbl=%02x%02x%02x flbl=%08x\n", *pCid,
					pipv6->flow_lbl[0] & 0x0f, pipv6->flow_lbl[1], pipv6->flow_lbl[2],
					(int)pfilter->filter.flowLabel);
				pfilter->no_matches++;
				continue;
			} else {
				TFT_MSG("TFT IPv6 Cid %d flow label ->MATCH\n", *pCid);
			}
		} else {
			TFT_MSG("TFT IPv6 Cid %d flow label - not present ->MATCH\n", *pCid);
		}

		if (pfilter->filter.ipsec_secure_param_index_valid) {
			if (!pspi) {
				TFT_MSG("TFT IPv6 Cid %d filter spi %08x valid but not in packet -> NO MATCH\n", *pCid,
					(int)pfilter->filter.ipsec_secure_param_index);
				pfilter->no_matches++;
				continue;
			} else {
				if (*pspi != pfilter->filter.ipsec_secure_param_index) {
					TFT_MSG("TFT IPv6 Cid %d spi %08x  filter spi %08x -> NO MATCH\n", *pCid,
						(int)(*pspi), (int)pfilter->filter.ipsec_secure_param_index);
					pfilter->no_matches++;
					continue;
				}
			}
			TFT_MSG("TFT IPv6 Cid %d SPI ->MATCH\n", *pCid);
		}
		/* filter is match - return filter cid (primary or secondary) */
		pfilter->matches++;

		TFT_MSG("TFT IPv6 CID %d to CID %d -> MATCH !!!!\n", *pCid, pfilter->filter.cid);

		if (fh != NULL) {
			if (IPv6FragFirst(fh)) {
				pIP_FRAG_FOR_TFP pIPF = gTftFragsDBIPv6[*pCid].frags;
				int i, index = 0;
				unsigned int minAge = 0xFFFFFFFF;

				gFragsAge++;

				if (gFragsAge == 0xFFFFFFFF)
					gFragsAge++;

				for (i = 0; i < MAX_FRAGS_PER_CID; i++) {
					if (pIPF[i].status == FRAG_STATUS_NOT_USED) {
						index = i;
						break;
					}
					if (pIPF[i].age < minAge) {
						minAge = pIPF[i].age;
						index = i;
					}
				}

				if (i < MAX_FRAGS_PER_CID) {
					TFT_MSG("IPv6 First Frag to cid %d ip id %d index %d\n", pfilter->filter.cid,
						(unsigned int)(fh->identification), index);
				} else {
					TFT_MSG("IPv6 First Frag to cid %d ip id %d reused index %d\n",
						pfilter->filter.cid, (unsigned int)(fh->identification), index);
				}
				pIPF[index].status = FRAG_STATUS_USED;
				pIPF[index].cid = (unsigned int)(pfilter->filter.cid);
				pIPF[index].age = gFragsAge;
				pIPF[index].frag_id = (unsigned int)(fh->identification);
			}
		}

		*pCid = (unsigned int)(pfilter->filter.cid);
		CiDataUnlockSendLock();
		return TRUE;

	}

	CiDataUnlockSendLock();

	/* we had no match - need to send on def cid */
	if (gCiDataTftCidToUseOnNoTftMatch[*pCid] != -1) {
		*pCid = (unsigned int)gCiDataTftCidToUseOnNoTftMatch[*pCid];
		TFT_MSG("TFTIPv6 No match PASS to default %d\n", *pCid);
		return TRUE;
	}

	TFT_MSG("TFTIPv6 No match drop\n");
	return FALSE;		/*  packet should be dropped */
}

/***********************************************************************************\
*   Name:        CiDataUplinkProcessPacket
*
*   Description: Process data packet - analyze packet as follows:
*                1. check if need to drop packet
*                2. find if this packet should be put on HIGH or LOW priority queue
*                3. evaluate secondary CID if required
*
*   Params:      *pCid - IN original cid, OUT secondary cid
*                *packet_address - packet address to analyze
*                packet_length - length of packet
*                *pQueuePriority - result of queue priority to use (HIGH/LOW)
*
*   Return:      unsigned char - send the pacekt or not - TRUE to send, FALSE to drop
\***********************************************************************************/
unsigned char CiDataUplinkProcessPacket(unsigned int *pCid, void *packet_address, unsigned int packet_length,
					CiData_QueuePriorityE *pQueuePriority, pTCP_SESSION *ppUsedTcpSession)
{
	struct iphdr *pipv4 = (struct iphdr *)packet_address;
	struct tcphdr *ptcp = NULL;
	struct ipv6hdr *pipv6 = NULL;
	int data_bytes = 0;
	unsigned char isv4 = 0;
	pTCP_SESSION pS = NULL;
	unsigned int be_ack;
	__u8 nexthdr;		/* for ipv6 tft */
	__u8 *pEnd = ((__u8 *) packet_address) + packet_length;
	struct frag_hdr *fh = NULL;
	__be32 *pspi = NULL;
	struct ip_esp_hdr *esp;

	if (ppUsedTcpSession != NULL) {
		*ppUsedTcpSession = NULL;
	}

	TFT_MSG("\n\n******* %s start **********\n", __func__);

	*pQueuePriority = CI_DATA_QUEUE_PRIORITY_LOW;

	ts_db.pkts_processed++;

	if (pipv4->version == 4) {
		if (pipv4->protocol != IPPROTO_TCP) {
			if (pipv4->protocol == IPPROTO_UDP) {
				/* ports info in same position */
				ptcp = (struct tcphdr *)((unsigned char *)packet_address + (pipv4->ihl << 2));
			} else if (pipv4->protocol == IPPROTO_ESP) {
				esp = (struct ip_esp_hdr *)((unsigned char *)packet_address + (pipv4->ihl << 2));
				pspi = &esp->spi;
			}
			return CiDataDoIPv4TFT(pipv4, ptcp, pCid, pspi);
		}

		isv4 = TRUE;
		ptcp = (struct tcphdr *)((unsigned char *)packet_address + (pipv4->ihl << 2));

	} else if (pipv4->version == 6) {
		struct ipv6_opt_hdr *opthdr;
		__u8 *pnexthdr;

		pipv6 = (struct ipv6hdr *)packet_address;

		/* skip next headers till we find the last one */
		nexthdr = pipv6->nexthdr;
		pnexthdr = (__u8 *) (pipv6 + 1);
		opthdr = (struct ipv6_opt_hdr *)pnexthdr;

		while (pnexthdr + sizeof(struct tcphdr) <= pEnd) {
			switch (nexthdr) {
			case NEXTHDR_TCP:
				isv4 = FALSE;
				ptcp = (struct tcphdr *)pnexthdr;
				goto do_packet_priority;
				break;
			case NEXTHDR_UDP:
				ptcp = (struct tcphdr *)pnexthdr;	/* for TFT the ports will still be correct */
				return CiDataDoIPv6TFT(pipv6, fh, ptcp, pCid, nexthdr, pspi);
				break;
			case NEXTHDR_DEST:
			case NEXTHDR_ROUTING:
			case NEXTHDR_HOP:
				/* here we assume A TCP header could be the next one */
				pnexthdr += ipv6_optlen(opthdr);
				opthdr = (struct ipv6_opt_hdr *)pnexthdr;
				nexthdr = opthdr->nexthdr;
				break;
			case NEXTHDR_FRAGMENT:
				fh = (struct frag_hdr *)pnexthdr;
				nexthdr = fh->nexthdr;
				pnexthdr = (__u8 *) (fh + 1);
				break;

/*
current understanding of spec is that SPI of TFT is only for ESP header

			case NEXTHDR_AUTH:
					ah=	(struct ip_auth_hdr*)pnexthdr;
					opthdr=(struct ipv6_opt_hdr *)pnexthdr;
					nexthdr=ah->nexthdr;
					pnexthdr+=ipv6_optlen(opthdr);
					pspi=&ah->spi;
					break;
*/
			case NEXTHDR_ESP:
				esp = (struct ip_esp_hdr *)pnexthdr;
				pspi = &esp->spi;
				return CiDataDoIPv6TFT(pipv6, fh, NULL, pCid, nexthdr, pspi);

			default:
/*
			/ * the below cases are assumed not to be followed by a TCP header * /
			/ * thus we stop looking for TCP header in this case * /
			case NEXTHDR_NONE:
			case NEXTHDR_IPV6:
			case NEXTHDR_ICMP:
			case NEXTHDR_MOBILITY
*/
				return CiDataDoIPv6TFT(pipv6, fh, NULL, pCid, nexthdr, pspi);
				break;
			}
		}
	} else {
		ts_db.pkt_drops++;
		return FALSE;
	}

do_packet_priority:

	if (ptcp) {
		if ((unsigned int)(ptcp + 1) > (unsigned int)pEnd) {
			ts_db.pkt_drops++;
			return FALSE;	/* DROP */
		}

		if (isv4) {
			pS = TSHashFindOrAdd((unsigned char)*pCid, isv4,
					     (unsigned short *)(&ptcp->source),
					     (unsigned short *)(&ptcp->dest), &pipv4->daddr);

			if (pS == NULL) {
				return CiDataDoIPv4TFT(pipv4, ptcp, pCid, NULL);
			}
			data_bytes = (int)(packet_length - ((pipv4->ihl << 2) + (ptcp->doff << 2)));
		} else {
			pS = TSHashFindOrAdd((unsigned char)*pCid, isv4,
					     (unsigned short *)(&ptcp->source),
					     (unsigned short *)(&ptcp->dest), (unsigned int *)&pipv6->daddr);

			if (pS == NULL) {
				return CiDataDoIPv6TFT(pipv6, fh, ptcp, pCid, nexthdr, pspi);
			}
			data_bytes = (int)(packet_length - (((int)ptcp - (int)pipv6) + (ptcp->doff << 2)));
		}
	} else {
		ASSERT(pipv6 != NULL);	/*KW */
		return CiDataDoIPv6TFT(pipv6, fh, ptcp, pCid, nexthdr, pspi);
	}

	pS->last_activity_time = jiffies;

	if (pS->prev_buff_ptr == NULL) {
		/* new or timed out session */
		pS->is_hp = (data_bytes == 0);
#ifdef TS_STATS
		printk("init session src = %d dst =%d hp=%d\n",
		       __cpu_to_be16(pS->src_port), __cpu_to_be16(pS->dest_port), pS->is_hp);
#endif
	}
#ifdef TS_STATS
	pS->p_in++;
#endif

	if (data_bytes < ts_db.small_pkts_limit) {
		if (pS->small_pkts_cnt < ts_db.high_pri_thresh) {
			pS->small_pkts_cnt++;
			if (pS->is_hp == FALSE && pS->small_pkts_cnt == ts_db.high_pri_thresh) {
				pS->is_hp = TRUE;
#ifdef TS_STATS
				printk("session src = %d dst =%d pkts =%d LP->HP\n",
				       __cpu_to_be16(pS->src_port), __cpu_to_be16(pS->dest_port), pS->p_in);
#endif
			}
		}
	} else {
		if (pS->small_pkts_cnt > ts_db.low_pri_thresh) {
			pS->small_pkts_cnt--;
			if (pS->is_hp == TRUE && pS->small_pkts_cnt == ts_db.low_pri_thresh) {
				pS->is_hp = FALSE;
#ifdef TS_STATS
				printk("session src = %d dst =%d pkts =%d HP->LP\n",
				       __cpu_to_be16(pS->src_port), __cpu_to_be16(pS->dest_port), pS->p_in);
#endif
			}
		}
	}

	be_ack = __cpu_to_be32(ptcp->ack_seq);

	if (ts_db.do_ack_drop &&
	    pS->small_pkts_cnt == ts_db.high_pri_thresh &&
	    data_bytes == 0 &&
	    pS->prev_flags_was_ack &&
	    (int)(be_ack - pS->prev_tcp_ack) > 0 &&
	    ptcp->seq == pS->prev_tcp_seq &&
	    (((*(((unsigned char *)&ptcp->window) - 1)) & TCP_FLAGS_MASK) == TCP_FLAGS_ACK_ONLY) &&
	    pS->ack_drops_cnt < ts_db.ack_drops_thresh) {

		/* mark prev packet is dropped */
		/* TBD */
		CiDataUplinkMarkDroppedPacket(pS->uplink_q_number, pS->uplink_q_index);

		pS->ack_drops_cnt++;
#ifdef TS_STATS
		pS->p_dropped++;
#endif
	} else {
		pS->ack_drops_cnt = 0;
	}

	pS->prev_buff_ptr = packet_address;
	pS->prev_tcp_ack = be_ack;
	pS->prev_tcp_seq = ptcp->seq;
	pS->prev_flags_was_ack =
	    (char)(((*(((unsigned char *)&ptcp->window) - 1)) & TCP_FLAGS_MASK) == TCP_FLAGS_ACK_ONLY);

	if (pS->is_hp) {
		*pQueuePriority = CI_DATA_QUEUE_PRIORITY_HIGH;
		if (ppUsedTcpSession != NULL) {
			*ppUsedTcpSession = pS;
		}
	}

	if (isv4)
		return CiDataDoIPv4TFT(pipv4, ptcp, pCid, NULL);

	return CiDataDoIPv6TFT(pipv6, fh, ptcp, pCid, nexthdr, pspi);

}

void CiDataUplinkSetPacketInfo(pTCP_SESSION pS, unsigned int qNumber, unsigned int index)
{
	if (pS && pS->prev_buff_ptr) {
		pS->uplink_q_index = index;
		pS->uplink_q_number = qNumber;
	}
}

void ConstructIcmpv6RouterSolicit(unsigned int *buf, unsigned int *saddr)
{
	struct ipv6hdr *iph = (struct ipv6hdr *)buf;
	struct icmp6hdr *icmph = (struct icmp6hdr *)(iph + 1);

	/*fill ip v6 header */
	memcpy(buf, "\x60\x00\x00\x00\x00\x08\x3a\xff", 8);

	/* fill ipv6 source address with UID from NW and local link prefix (0xfe80) */
	memcpy(&iph->saddr.s6_addr32[2], saddr + 2, 8);
	memcpy(&iph->saddr, "\xfe\x80\x00\x00\x00\x00\x00\x00", 8);

	/* setup ipv6 daddr to mcast adress */
	memcpy(&iph->daddr, "\xff\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02", 16);

	icmph->icmp6_type = 0x85;	/* router solicitation code */
	icmph->icmp6_code = 0x00;
	icmph->icmp6_cksum = 0x0000;

	/* fill icmph rsvd field with 0 */
	memcpy(&icmph->icmp6_dataun.un_data32[0], "\x00\x00\x00\x00", 4);

	/* construct the  checksum */
	icmph->icmp6_cksum = csum_ipv6_magic(&iph->saddr, &iph->daddr, 0x08, 0x3a, csum_partial(icmph, 8, 0));
}

/*

ip hdr

60|00|00|00|00|40|3a|ff|
fe|80|00|00|00|00|00|00|80|47|78|b9|9f|d5|7a|8b|
ff|02|00|00|00|00|00|00|00|00|00|00|00|00|00|01|

icmpv6 routing advertisment

86|00|20|27|40|00|00|5a|00|00|00|00|00|00|00|00|01|01|00|22|68|20|18|c7|05|01|00|00|00|00|05|dc|03|04|40|c0|00|00|0b|b8|00|00|0b|b8|00|00|00|00|20|01|00|00|00|00|00|00|00|00|00|00|00|00|00|00|
*/

unsigned char CheckForIcmpv6RouterAdvertisment(unsigned int *buf, unsigned int length, unsigned int cid)
{
	struct ipv6hdr *iph = (struct ipv6hdr *)buf;
	struct icmp6hdr *icmph = (struct icmp6hdr *)(iph + 1);
	unsigned char *rtopts = (unsigned char *)(icmph + 1);
	unsigned char *end = (unsigned char *)(((unsigned char *)buf) + length);
	unsigned int lifetime;
	unsigned int orig_cid = cid;

	if (length < sizeof(struct ipv6hdr) + sizeof(struct icmp6hdr) + 32) {
		return FALSE;
	}

	if (icmph->icmp6_code != 0 || icmph->icmp6_type != 0x86 || iph->nexthdr != 0x3a || iph->version != 6) {
		return FALSE;
	}

	cid = CiDataTftGetPrimaryBySecondary(cid);
	if (cid == -1) {
		CI_DATA_DEBUG(1, "Invalid CID %d orig=%d\n", cid, orig_cid);
		return FALSE;
	}

	if (cid >= CI_DATA_TOTAL_CIDS) {
		CI_DATA_DEBUG(1, "Invalid CID2 %d orig=%d\n", cid, orig_cid);
		return FALSE;
	}

	if ((gCiDataClient.cid[cid].ipAddrs.ipv6addr[0] | gCiDataClient.cid[cid].ipAddrs.ipv6addr[1] |
	     gCiDataClient.cid[cid].ipAddrs.ipv6addr[2] | gCiDataClient.cid[cid].ipAddrs.ipv6addr[3]) == 0) {
		CI_DATA_DEBUG(1, "CID %d is not v6\n", cid);
		return FALSE;
	}

	if (gCiDataClient.cid[cid].ipAddrs.ipv6addr[0] != 0 || gCiDataClient.cid[cid].ipAddrs.ipv6addr[1] != 0) {
		CI_DATA_DEBUG(1, "CID %d is already has a prefix\n", cid);
		return FALSE;
	}

	CI_DATA_DEBUG(1, "RA override daddr %08x %08x %08x %08x",
		      (int)iph->daddr.s6_addr32[0], (int)iph->daddr.s6_addr32[1],
		      (int)iph->daddr.s6_addr32[2], (int)iph->daddr.s6_addr32[3]);

	memcpy((void *)&iph->daddr, "\xff\x02\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", 16);

	/* skip to icmp options */
	rtopts += 8;

	icmph->icmp6_cksum = 0;
	icmph->icmp6_cksum =
	    csum_ipv6_magic(&iph->saddr, &iph->daddr, length - sizeof(*iph), 0x3a,
			    csum_partial(icmph, length - sizeof(*iph), 0));

	while (rtopts < end - 31) {
		/* looking for prefix options // 32 bytes long */
		if (*rtopts != 3) {
			printk("RA opt %d skiped\n", (int)(*rtopts));
			rtopts += (*(rtopts + 1)) * 8;
		} else {
			rtopts++;	/* skip to len */
			CI_DATA_DEBUG(1, "RA len 4 == %d\n", (int)(*rtopts));
			rtopts++;	/* skip to prefix len */
			CI_DATA_DEBUG(1, "RA prefix len= %d\n", (int)(*rtopts));
			gCiDataClient.cid[cid].ipV6PrefixLen = (unsigned int)((*rtopts) / 8);	/* TBD fix for bit prefix */
			rtopts++;	/* skip to flags */
			rtopts++;	/* skip to valid life time */
			memcpy(&lifetime, rtopts, 4);
			lifetime = __cpu_to_be32(lifetime);
			/*TBD: setup renew timer */
			rtopts += 4;	/* skip to preferred life time */
			rtopts += 4;	/* skip to reserved */
			rtopts += 4;	/* skip to prefix */

			if (gCiDataClient.cid[cid].ipV6PrefixLen > 16) {
				CI_DATA_DEBUG(1, "RA invalid prefix len = %d\n", gCiDataClient.cid[cid].ipV6PrefixLen);
				return FALSE;
			}

			CiDataLockSendLock();
			memcpy(gCiDataClient.cid[cid].ipAddrs.ipv6addr, rtopts, gCiDataClient.cid[cid].ipV6PrefixLen);
			CiDataUnlockSendLock();

			CI_DATA_DEBUG(1, "RA lifetime %08x %d\n", lifetime, (lifetime / 4) * 3 + 1);
			/*if (lifetime < 0x7FFFFFFFU){ */

			/*CiDataIPv6RouterSolSetupTimer((unsigned long)cid,(lifetime/4)*3+1); */
			/*} */

			CI_DATA_DEBUG(0, "%s: cid=%d, v6 prefix length %d new v6addr=%08X:%08X:%08X:%08X",
				      __func__, cid, gCiDataClient.cid[cid].ipV6PrefixLen,
				      gCiDataClient.cid[cid].ipAddrs.ipv6addr[0],
				      gCiDataClient.cid[cid].ipAddrs.ipv6addr[1],
				      gCiDataClient.cid[cid].ipAddrs.ipv6addr[2],
				      gCiDataClient.cid[cid].ipAddrs.ipv6addr[3]);

			return TRUE;
		}
	}
	return FALSE;
}

void CiDataUplinkSetAckDropThresh(char threshCommand)
{
	if (threshCommand == 'z') {
		gCiDataClient.uplinkIgnorePacketsDisabled = (gCiDataClient.uplinkIgnorePacketsDisabled) ? FALSE : TRUE;
		printk(KERN_ERR "Current uplink-ack-ignore-packets = %s\n",
		       (gCiDataClient.uplinkIgnorePacketsDisabled) ? "ENABLE" : "DISABLE");
	} else {
		ts_db.ack_drops_thresh = threshCommand - '0';
		printk(KERN_ERR "New value for uplink-ack-ignore-packets = %d\n", ts_db.ack_drops_thresh);
	}
}

void CiDataUpGetTsDbStat(int *pNoTcpSession, int *pNoHashColl)
{
	if (pNoTcpSession != NULL) {
		*pNoTcpSession = ts_db.curr_tcp_sessions;
	}
	if (pNoHashColl != NULL) {
		*pNoHashColl = ts_db.max_hash_colls;
	}
}
