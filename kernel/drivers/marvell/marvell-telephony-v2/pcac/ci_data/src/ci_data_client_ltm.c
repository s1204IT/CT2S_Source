/*------------------------------------------------------------
(C) Copyright [2006-2012] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
    Title:       CI Data Client - LTM
    Filename:    ci_data_client_ltm.c
    Description: Contains management of CI Data Client LTM
    Author:      Yossi Gabay
************************************************************************/
#include	<linux/version.h>
#if !defined (ESHEL)		/*!defined (TEL_CFG_FLAVOR_ITM) */
#include "itm.h"

void CiDataClientLtmRegister(void)
{
}

void CiDataClientLtmUnregister(void)
{
}

void CiDataClientLtmUplinkKickRndis(void)
{
}

unsigned int CiDataClientLtmSetIpAddress(char *str)
{
	str = str;

	return 0;
}

void CiDataClientLtmDownlinkFreePacket(struct fp_packet_details *packet_detail)
{
	(void)packet_detail;
}

int CiDataClientLtmDownlinkDataSend(void **packetsList, struct fp_net_device *dst_dev)
{
	(void)packetsList;
	(void)dst_dev;
	return NETDEV_TX_BUSY;
}
#else
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <asm/cacheflush.h>
#include <linux/netdevice.h>

#include "osa_kernel.h"
#include "ci_data_client.h"

#include "ci_data_client_ltm.h"
#include "ci_data_client_handler.h"
#include "ci_data_client_mem.h"

/*FIXME: Do not enable until the bug is solved (we get error in RNDIS tx_complete,
seems that packets are overriden) */
/*#define KICK_DL_TASK_ON_FREE */

/************************************************************************
 * Internal debug log
 ***********************************************************************/
#include "dbgLog.h"
DBGLOG_EXTERN_VAR(cidata);
#define     CIDATA_DEBUG_LOG2(a, b, c)     DBGLOG_ADD(cidata, a, b, c)

/************************************************************************
 * Internal Prototypes
 ***********************************************************************/

/************************************************************************
 * Internal Globals
 ***********************************************************************/

#define CI_DATA_LTM_PACKET_HEADER_SIZE          64
static struct fp_net_device *p_fp_rndis;	/*ITM support - downlink fast path RNDIS handler (for HOST mode only) */

/***********************************************************************************\
* Functions
************************************************************************************/

/***********************************************************************************\
*   Name:        CiDataClientLtmSetIpAddress
*
*   Description: Set LTM IP address from command lineSend single uplink packet to COMM using 'direct' (not from IPstack).
*
*   Params:      str - string command, contain the IP in ... format
*
*   Return:      string length
\***********************************************************************************/
unsigned int CiDataClientLtmSetIpAddress(char *str)
{
	CI_DATA_DEBUG(1, "CiDataClientLtmSetIpAddress: IP str=%s (debug print)", str);
	CI_DATA_DEBUG(0, "*** not yet supported ***");

	return 0;
}

/***********************************************************************************\
*   Name:        CiDataClientLtmDownlinkFreePacket
*
*   Description: Free downlink packet by incrementing the free index
*                free is done in order packets received per CID
*
*   Params:      cid - connection ID of the buffer to be freed
*
*   Return:
\***********************************************************************************/
void CiDataClientLtmDownlinkFreePacket(struct fp_packet_details *packet_detail)
{
	unsigned int cid;
#ifdef KICK_DL_TASK_ON_FREE
	OSA_STATUS osaStatus;
	unsigned int cid_idx;
	CiData_DownlinkControlAreaDatabaseS *downlink = &gCiDataClient.pSharedArea->downlink;
#endif

	BUG_ON(!packet_detail ||
	       !packet_detail->fp_src_dev || !packet_detail->fp_src_dev->private_data || !packet_detail->buffer.start);

	cid = ((CIData_CidDatabaseS *) packet_detail->fp_src_dev->private_data)->cid;

	CI_DATA_DEBUG(3, "CiDataClientLtmDownlinkFreePacket: free LTM packet, cid=%d", cid);

	fpdev_put(packet_detail->fp_src_dev);

	CiDataClientDownlinkFreePacket(cid, (CiData_DownlinkDescriptorS *) packet_detail);

#ifdef KICK_DL_TASK_ON_FREE
	CI_DATA_DESCRIPTORS_CACHE_INVALIDATE(&downlink->nextWriteIdx, sizeof(downlink->nextWriteIdx));

	for (cid_idx = 0; cid_idx < CI_DATA_TOTAL_CIDS; cid_idx++) {
		if (downlink->nextWriteIdx[cid_idx] != downlink->nextReadIdx[cid_idx]) {
			/*TODO A bug is hiding here, we get error in RNDIS tx_complete, seems that packets are overriden - SOLVE ASAP! */
			/*kick downlink task */
			osaStatus =
			    FLAG_SET(&gCiDataClient.downlinkWaitFlagRef, CI_DATA_DOWNLINK_PACKET_PENDING_FLAG_MASK,
				     OSA_FLAG_OR);
			CI_DATA_ASSERT(osaStatus == OS_SUCCESS);
			break;
		}
	}
#endif

}

/***********************************************************************************\
*   Name:        CiDataClientLtmDownlinkDataSend
*
*   Description: Send multiple downlink packet RNDIS Add-on
*                assumption: last item on list points to NULL
*
*   Params:      packetList - points to an array of pointers to CI downlink descriptors
*                of type CiData_DownlinkDescriptorS (*)[]. This type must match the one
*                defined in RNDIS Add-on:
*                struct rndis_packet_details *pPacketDetails[]
*
*   Return:      0 - Success (SEND_SUCCESS)
*                1 - USB Busy (SEND_BUSY)
*                2 - USB Cable Disconnected (SEND_DISCONNECTED)
\***********************************************************************************/
int CiDataClientLtmDownlinkDataSend(void **packetsList, struct fp_net_device *dst_dev)
{
	netdev_tx_t ret;
	static unsigned int cnt;

	CIDATA_DEBUG_LOG2(MDB_CiDataClientLtmDownlinkDataSend, DBG_LOG_FUNC_START, 0);
	if (unlikely(!dst_dev->tx_mul_cb)) {
		pr_err("[%s:%d] dst fp dev %s does not have a mult callback!", __func__, __LINE__, dst_dev->dev->name);
		BUG();
	}

	ret = dst_dev->tx_mul_cb((struct fp_packet_details **)packetsList);

	CIDATA_DEBUG_LOG2(MDB_CiDataClientLtmDownlinkDataSend, 1, (unsigned int)ret);

	CI_DATA_DEBUG(3, "CiDataClientLtmDownlinkDataSend: ret = %d", (unsigned int)ret);

	if (ret != NETDEV_TX_OK) {
		CI_DATA_DEBUG(2, "CiDataClientLtmDownlinkDataSend: ERROR");
		if (ret != NETDEV_TX_BUSY) {
			CI_DATA_ASSERT_MSG_P(1, FALSE, "CiDataClientLtmDownlinkDataSend: ERROR RNDIS returned ret=%d",
					     ret);
		}
		/*YG: need fix here count by CID */
		if ((++cnt & 0x1FFFF) == 0) {
			CI_DATA_DEBUG(1, "CiDataClientLtmDownlinkDataSend: suspended cnt=%d packets", cnt);
		}
	}
	CIDATA_DEBUG_LOG2(MDB_CiDataClientLtmDownlinkDataSend, DBG_LOG_FUNC_END, 0);

	return ret;
}

/***********************************************************************************\
*   Name:        CiDataClientLtmClassifier
*
*   Description: HOST mode registered classifier in ITM.
*                Algorithm:
*                1. Check if UL or DL by comparing dev to p_fp_rndis
*                2. UL (dev == fp_rndis) - Keep old LTM logic
*                3. DL (dev != fp_rndis) - if HOST_IP, return p_fp_rndis
*                        else, return NULL (LOCAL PDP)
*   Params:      packet - buffer to send
*                len - length of the buffer
*
*   Return:      NULL - packet should be sent to the IP Stack by the caller
*                Otherwise, returns the destination interface's direct structure (containing the tx cb)
*   Notes:       LTM supports the following interfaces ONLY: RNDIS and CI
\***********************************************************************************/
struct fp_net_device *CiDataClientLtmClassifier(struct fp_net_device *dev,
						unsigned char *packet, unsigned int len, bool mangle)
{
	unsigned char ip_ver;
	int cid;
	unsigned int ip4_dst;
	unsigned int ip4_src;

	if (!packet || !len) {
		CI_DATA_DEBUG(1, "%s:%d: packet = %p, len =%d\n", __func__, __LINE__, packet, len);
		BUG();
	}
	/*Downlink (local - send to stack, host ip - send to rndis) */
	if (dev != p_fp_rndis) {
		CIData_CidDatabaseS *cid_db = (CIData_CidDatabaseS *) dev->private_data;
		BUG_ON(!cid_db);

		if (cid_db->serviceType == CI_DATA_SVC_TYPE_PDP_HOSTIP) {
			fpdev_hold(dev);
			fpdev_hold(p_fp_rndis);
			return p_fp_rndis;
		} else {
			return NULL;
		}
	}
	/*UPLINK (Regular LTM direct logic) */

	if (gCiDataClient.dataStatus != CI_DATA_IS_ENABLED)
		return NULL;

	/*Extract packet details: ip version, ip_src and ip_dst (used if ipv4 only) */
	ip_ver = ((struct iphdr *)packet)->version;
	ip4_src = (unsigned int)((struct iphdr *)packet)->saddr;
	ip4_dst = (unsigned int)((struct iphdr *)packet)->daddr;

	if (ip_ver != 4 && ip_ver != 6)
		return NULL;	/* not ipv4 and not ipv6 (May be used for ARP) */

	if (ip_ver == 4) {
		CI_DATA_DEBUG(5, "%s:%d: incoming ipv4 packet, src = %pI4, dst = %pI4, ltmLocalDestIpAddress = %pI4",
			      __func__, __LINE__, &ip4_src, &ip4_dst,
			      &gCiDataClient.mtil_globals_cfg.ltmLocalDestIpAddress);

		if (ip4_src == 0) {
			CI_DATA_DEBUG(3, "%s:%d: source IP = 0 !!! transer to local, ptr = %p, len = %d", __func__,
				      __LINE__, packet, len);
			return NULL;
		}
		/*check if destination address is local */
		if (gCiDataClient.mtil_globals_cfg.ltmLocalDestIpAddress == ip4_dst ||
		    ip4_dst == (gCiDataClient.mtil_globals_cfg.ltmLocalDestIpAddress | 0xFF000000)) {
			CI_DATA_DEBUG(4,
				      "%s:%d: found local packet !!! src = %pI4, dst = %pI4, ltmLocalDestIpAddress = %pI4",
				      __func__, __LINE__, &ip4_src, &ip4_dst,
				      &gCiDataClient.mtil_globals_cfg.ltmLocalDestIpAddress);
			return NULL;
		}
		/* Search for a CID which uses this packet's src IP */
		for (cid = 0; cid < CI_DATA_TOTAL_CIDS; cid++) {

			if (gCiDataClient.cid[cid].state != CI_DATA_CID_STATE_PRIMARY)
				continue;

			if (gCiDataClient.cid[cid].ipAddrs.ipv4addr == ip4_src) {
				CI_DATA_DEBUG(3, "%s:%d: IP match found!!! (cid=%d, ptr=0x%08x, len=%d)",
					      __func__, __LINE__, cid, (unsigned int)packet, len);
				fpdev_hold(dev);
				fpdev_hold(gCiDataClient.cid[cid].fp_dev);
				return gCiDataClient.cid[cid].fp_dev;
			}
		}

	} else {		/* ipv6 */

		/* Search for a CID which uses this packet's src IP */
		for (cid = 0; cid < CI_DATA_TOTAL_CIDS; cid++) {

			if (gCiDataClient.cid[cid].state != CI_DATA_CID_STATE_PRIMARY)
				continue;

			if (gCiDataClient.cid[cid].ipAddrs.ipv6addr[2] == 0
			    && gCiDataClient.cid[cid].ipAddrs.ipv6addr[3] == 0)
				continue;	/* CID is not configured for ip v6 */

			if ((gCiDataClient.cid[cid].ipV6PrefixLen != 0) &&
			    (memcmp(gCiDataClient.cid[cid].ipAddrs.ipv6addr,
				    &(((struct ipv6hdr *)packet)->saddr), gCiDataClient.cid[cid].ipV6PrefixLen) == 0)) {
				/* we have prefix - so we must allow only this prefix. */
				/* TBD: add support for non 64 bit prefix and use bit compare */
				CI_DATA_DEBUG(3, "%s:%d: IP match found!!! (cid=%d, ptr=0x%08x, len=%d)",
					      __func__, __LINE__, cid, (unsigned int)packet, len);
				fpdev_hold(dev);
				fpdev_hold(gCiDataClient.cid[cid].fp_dev);
				return gCiDataClient.cid[cid].fp_dev;
			}
		}
	}

	/*NO IP Match */
	CI_DATA_DEBUG(3, "%s:%d: NO IP match found!!! (ptr=0x%08x, len=%d)", __func__, __LINE__, (unsigned int)packet,
		      len);
	return NULL;
}

/***********************************************************************************\
*   Name:        CiDataClientLtmRegister
*
*   Description: Register LTM onto RNDIS Add-on & CI Data
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataClientLtmRegister(void)
{
	CI_DATA_DEBUG(3, "CiDataClientLtmRegister called");

	p_fp_rndis = itm_query_interface(dev_get_by_name(&init_net, "usb0"));
	BUG_ON(!p_fp_rndis);
	/* dev_get_by_name increments the reference count for the device.
	   We must call dev_put so that if the usb is disconnected the device
	   can be freed. Not a problem since the fp_dev will also be freed */
	dev_put(p_fp_rndis->dev);

	itm_register_classifier(&CiDataClientLtmClassifier, "LTM");
}

/***********************************************************************************\
*   Name:        CiDataClientLtmUnregister
*
*   Description: Unregister LTM
*
*   Params:
*
*   Return:
\***********************************************************************************/
void CiDataClientLtmUnregister(void)
{
	CI_DATA_DEBUG(3, "CiDataClientLtmUnregister called");
	itm_unregister_classifier("LTM");
}
#endif /*!defined (ESHEL) */
