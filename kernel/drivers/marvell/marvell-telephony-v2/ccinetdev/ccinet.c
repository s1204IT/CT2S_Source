/*
 * (C) Copyright 2006-2011 Marvell International Ltd.
 * All Rights Reserved
 */
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>

#include <asm/system.h>
#include <linux/uaccess.h>
#include <linux/io.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <net/sock.h>
#include <net/checksum.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/percpu.h>
#include <linux/cdev.h>
#include <common_datastub.h>
/* add wakelock - start */
#include <linux/wakelock.h>
/* add wakelock - end */
#include <linux/platform_device.h>
#include <linux/ipv6.h>
#include "ci_data_common.h"
#include "shmem_lnx_kmod_api.h"
#include "ci_data_client_api.h"

static void ccinet_dev_release(struct device *dev);
static int ccinet_probe(struct platform_device *dev);
static int ccinet_remove(struct platform_device *dev);
static int ccichar_open(struct inode *inode, struct file *filp);
static long ccichar_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg);

#define CINET_BUF_SIZE 2048	/* 1600//1518 */
#define CCINET_DEFAULT_MTU_SIZE 1500
#define CI_MAX_IP_V6_SIZE_INTS 4
struct ccinet_priv {
	unsigned char cid;
	int status;		/* indicate status of the interrupt */
	spinlock_t lock;	/* spinlock use to protect critical session */
	struct net_device_stats net_stats;	/* status of the network device */
	unsigned long tx_wklock_time;
	UINT32 ipv6_addr[CI_MAX_IP_V6_SIZE_INTS];
	BOOL is_ip6;
	BOOL b_handle_ra;
};

static struct platform_device ccinet_device = {
	.name = "marvell-ccinet",
	.id = 0,
	.dev = {
		.release = ccinet_dev_release,
		},
};

static struct platform_driver ccinet_driver = {
	.probe = ccinet_probe,
	.remove = ccinet_remove,
	.driver = {
		   .name = "marvell-ccinet",
		   }
};

static const struct file_operations ccinet_fops = {
	.open = ccichar_open,
	.unlocked_ioctl = ccichar_ioctl,
	.owner = THIS_MODULE
};

struct ccichar_dev {
	struct semaphore sem;	/* mutual exclusion semaphore */
	struct cdev cdev;	/* Char device structure */
};
struct CINETDEVLIST {
	struct net_device *ccinet;
	struct CINETDEVLIST *next;
};
static const char *const ccinet_name = "ccinet";
static struct CINETDEVLIST *netdrvlist;

static unsigned char ETH_HDR_ETHERTYPE_IPV4[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x08, 0x00
};

static unsigned char ETH_HDR_ETHERTYPE_IPV6[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x86, 0xDD
};

#define ETH_HEADER_SIZE sizeof(ETH_HDR_ETHERTYPE_IPV4)

#define RNDIS_RESERVE   (64)

#if	0
/*#define DPRINT(fmt,args...)	printf(fmt,##args)*/
#define DBGMSG(fmt, args...)	pr_err("CCINET: "fmt, ##args)
#define	ENTER()					pr_err("CCINET: ENTER %s\n", __func__)
#define	FUNC_EXIT()				pr_err("CCINET: EXIT %s\n", __func__)
#else
#define DPRINT(fmt, args...)	do { } while (0)
#define DBGMSG(fmt, args...)	do { } while (0)
#define	ENTER()					do { } while (0)
#define	FUNC_EXIT()				do { } while (0)
#endif

/* add wakelock - start */
static struct wake_lock ccinet_wakelock;
static long ccinet_wakelock_timeout;
static unsigned long rx_wakelock_time;
#define DEFAULT_NET_WAKELOCK_TO 5000
/* add wakelock - end */

/*//////////////////////////////////////////////////////////////
 / Interface to CCI Stub
 /////////////////////////////////////////////////////////////*/
static int connect_network(void)
{
	return 0;
}

static int disconnect_network(void)
{
	return 0;
}

/*//////////////////////////////////////////////////////////////
 / Network Operations
 /////////////////////////////////////////////////////////////*/
static int ccinet_open(struct net_device *netdev)
{
	ENTER();

	/* need Assign the hardware MAC address of the device driver ? */
	connect_network();
/*      netif_carrier_on(netdev); */
	netif_start_queue(netdev);
	return 0;
}

static int ccinet_stop(struct net_device *netdev)
{
	ENTER();
	netif_stop_queue(netdev);
/*      netif_carrier_off(netdev); */
	disconnect_network();
	return 0;
}

static void add_to_drv_list(struct CINETDEVLIST *newdrvnode)
{
	struct CINETDEVLIST *p_curr_node;
	if (netdrvlist == NULL) {
		netdrvlist = newdrvnode;
	} else {
		p_curr_node = netdrvlist;
		while (p_curr_node->next != NULL)
			p_curr_node = p_curr_node->next;

		p_curr_node->next = newdrvnode;

	}
	return;
}

BOOL CheckForIcmpv6RouterAdvertisment(unsigned char *addr, unsigned int *buf,
				      unsigned int length, unsigned int cid)
{
	struct ipv6hdr *iph = (struct ipv6hdr *)buf;
	struct icmp6hdr *icmph = (struct icmp6hdr *)(iph + 1);
	unsigned char *rtopts = (unsigned char *)(icmph + 1);
	unsigned char *end = (unsigned char *)(((unsigned char *)buf) + length);
	unsigned int lifetime;
	UINT8 prfix_len;

	if (length < sizeof(struct ipv6hdr) + sizeof(struct icmp6hdr) + 32)
		return FALSE;

	if (icmph->icmp6_code != 0 || icmph->icmp6_type != 0x86
	    || iph->nexthdr != 0x3a || iph->version != 6) {
		return FALSE;
	}

	/* skip to icmp options */
	rtopts += 8;

	while (rtopts < end - 31) {
		/* looking for prefix options // 32 bytes long */
		if (*rtopts != 3) {
			pr_err("RA opt %d skiped\n", (int)(*rtopts));
			rtopts += (*(rtopts + 1)) * 8;
		} else {
			rtopts++;	/* skip to len */
			pr_err("RA len 4 == %d\n", (int)(*rtopts));
			rtopts++;	/* skip to prefix len */
			pr_err("RA prefix len= %d\n", (int)(*rtopts));
			prfix_len = (UINT32) ((*rtopts) / 8);
			if (prfix_len > 16) {
				pr_err("RA prefix len %d > 16\n",
				       (int)(prfix_len));
				return FALSE;
			}
			rtopts++;	/* skip to flags */
			rtopts++;	/* skip to valid life time */
			memcpy(&lifetime, rtopts, 4);
			lifetime = __cpu_to_be32(lifetime);
			/* TBD: setup renew timer */
			rtopts += 4;	/* skip to preferred life time */
			rtopts += 4;	/* skip to reserved */
			rtopts += 4;	/* skip to prefix */
			memcpy(addr, rtopts, prfix_len);

			pr_err("%s: cid=%d, v6 new v6addr prefix=",
			       __func__, cid);
			pr_err("%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
			       *addr, *(addr + 1),
			       *(addr + 2), *(addr + 3), *(addr + 4),
			       *(addr + 5), *(addr + 6), *(addr + 7));
			return TRUE;
		}
	}

	return FALSE;

}

static int search_list_by_cid(unsigned char cid, struct CINETDEVLIST **pp_buf)
{
	struct CINETDEVLIST *p_curr_buf;
	struct ccinet_priv *devobj;

	if (netdrvlist == NULL)
		return -1;

	p_curr_buf = netdrvlist;
	devobj = netdev_priv(p_curr_buf->ccinet);
	while (devobj->cid != cid) {
		p_curr_buf = p_curr_buf->next;
		if (p_curr_buf == NULL)
			break;

		devobj = netdev_priv(p_curr_buf->ccinet);
	}

	if (p_curr_buf != NULL) {
		*pp_buf = p_curr_buf;
		return 0;
	}

	return -1;
}

static void remove_node_by_cid(unsigned char cid)
{
	struct CINETDEVLIST *p_curr_node, *p_prev_node;
	struct ccinet_priv *devobj;

	p_prev_node = p_curr_node = netdrvlist;
	while (p_curr_node != NULL) {
		devobj = netdev_priv(p_curr_node->ccinet);
		if (devobj->cid == cid) {
			if (p_curr_node == netdrvlist) {
				netdrvlist = p_curr_node->next;
				unregister_netdev(p_curr_node->ccinet);
				free_netdev(p_curr_node->ccinet);
				kfree(p_curr_node);
				p_prev_node = p_curr_node = netdrvlist;
			} else {
				p_prev_node->next = p_curr_node->next;
				unregister_netdev(p_curr_node->ccinet);
				free_netdev(p_curr_node->ccinet);
				kfree(p_curr_node);
				p_curr_node = p_prev_node->next;
			}
		} else {
			p_prev_node = p_curr_node;
			p_curr_node = p_curr_node->next;
		}
	}
}

static void remove_drv_list(void)
{
	struct CINETDEVLIST *p_curr_node;

	p_curr_node = netdrvlist;
	while (p_curr_node != NULL) {
		netdrvlist = netdrvlist->next;
		unregister_netdev(p_curr_node->ccinet);
		free_netdev(p_curr_node->ccinet);
		kfree(p_curr_node);
		p_curr_node = netdrvlist;
	}
	return;
}

static int ccinet_tx(struct sk_buff *skb, struct net_device *netdev)
{
	struct ccinet_priv *devobj = netdev_priv(netdev);
	struct ipv6hdr *iph;
	struct icmp6hdr *icmph;
	ENTER();
	netdev->trans_start = jiffies;

	devobj->net_stats.tx_packets++;
	devobj->net_stats.tx_bytes += skb->len - ETH_HEADER_SIZE;

/* add wakelock - start */
/* optimize - not take a wakelock if one was
   already taken in the last 100 msecs */
	if (unlikely
	    (time_after
	     (netdev->trans_start, devobj->tx_wklock_time + (HZ / 8)))) {
		devobj->tx_wklock_time = netdev->trans_start;
		if (ccinet_wakelock_timeout)
			wake_lock_timeout(&ccinet_wakelock,
					  (ccinet_wakelock_timeout / 1000) *
					  HZ);
		else
			wake_lock_timeout(&ccinet_wakelock,
					  (DEFAULT_NET_WAKELOCK_TO / 1000) *
					  HZ);
	}
/* add wakelock - end */
	iph = (struct ipv6hdr *)(skb->data + ETH_HEADER_SIZE);
	icmph = (struct icmp6hdr *)(iph + 1);
	if (skb->len - ETH_HEADER_SIZE >
	    sizeof(struct ipv6hdr) + sizeof(struct icmp6hdr)) {
		if (icmph->icmp6_code == 0 && icmph->icmp6_type == 0x85
		    && iph->nexthdr == 0x3a && iph->version == 6) {
			if (!(devobj->is_ip6 == TRUE)) {
				dev_kfree_skb(skb);
				return 0;
			} else {
				pr_err("CCINET DBG: %08X::%08X::%08X::%08X\n",
				       devobj->ipv6_addr[0],
				       devobj->ipv6_addr[1],
				       devobj->ipv6_addr[2],
				       devobj->ipv6_addr[3]);
				devobj->b_handle_ra = TRUE;
			}
		}
	}

	ci_data_uplink_data_send(devobj->cid, skb->data + ETH_HEADER_SIZE,
				 skb->len - ETH_HEADER_SIZE);
	dev_kfree_skb_any(skb);
	return 0;

}

static void ccinet_tx_timeout(struct net_device *netdev)
{
	struct ccinet_priv *devobj = netdev_priv(netdev);
	ENTER();
	devobj->net_stats.tx_errors++;
/*      netif_wake_queue(netdev); // Resume tx */
	return;
}

static struct net_device_stats *ccinet_get_stats(struct net_device *dev)
{
	struct ccinet_priv *devobj;

	devobj = netdev_priv(dev);
	ASSERT(devobj);
	return &devobj->net_stats;
}

int ccinet_rx(struct net_device *netdev, char *packet, int pktlen)
{

	struct sk_buff *skb;
	struct ccinet_priv *priv = netdev_priv(netdev);
	UINT32 addr[CI_MAX_IP_V6_SIZE_INTS];
	ENTER();
	DBGMSG("ccinet_rx:pktlen=%d\n", pktlen);

	skb =
	    dev_alloc_skb(pktlen + NET_IP_ALIGN + ETH_HEADER_SIZE +
			  RNDIS_RESERVE);

	if (!skb) {

		printk_ratelimited(KERN_NOTICE "ccinet_rx: low on mem - packet dropped\n");

		priv->net_stats.rx_dropped++;

		goto out;

	}

	if (CheckForIcmpv6RouterAdvertisment
	    ((unsigned char *)addr, (unsigned int *)packet,
	     (unsigned int)pktlen, (unsigned int)priv->cid)) {
		if (!(priv->is_ip6)) {
			dev_kfree_skb(skb);
			return 0;
		} else {/* no need to catch more RAs for this ccinet until special
				 * notice...
				 */
			if (priv->b_handle_ra == TRUE) {
				memcpy((char *)&(priv->ipv6_addr[0]), addr,
				       sizeof(addr));
				priv->b_handle_ra = FALSE;
			} else {
				dev_kfree_skb(skb);
				return 0;
			}
		}
	}

	/* Align IP header on 16-byte boundary */
	skb_reserve(skb, NET_IP_ALIGN + RNDIS_RESERVE);

#define EXTRACT_IP_VERSION(p) ((*(unsigned char *)p & 0xf0) >> 4)

	if (EXTRACT_IP_VERSION(packet) == 6) {
		memcpy(skb_put(skb, ETH_HEADER_SIZE), ETH_HDR_ETHERTYPE_IPV6,
		       ETH_HEADER_SIZE);
	} else {
		memcpy(skb_put(skb, ETH_HEADER_SIZE), ETH_HDR_ETHERTYPE_IPV4,
		       ETH_HEADER_SIZE);
	}
	memcpy(skb_put(skb, pktlen), packet, pktlen);

	/* Write metadata, and then pass to the receive level */

	skb->dev = netdev;
	/*htons(ETH_P_IP);//eth_type_trans(skb, netdev);*/
	skb->protocol = eth_type_trans(skb, netdev);
/*      skb->ip_summed = CHECKSUM_UNNECESSARY;  / * don't check it * / */
	priv->net_stats.rx_packets++;
	priv->net_stats.rx_bytes += pktlen + ETH_HEADER_SIZE;
	netif_rx_ni(skb);

/* optimize - not take a wakelock if one was already
   taken in the last 100 msecs */
/* add wakelock - start */
	if (unlikely(time_after(jiffies, rx_wakelock_time + (HZ / 8)))) {
		rx_wakelock_time = jiffies;
		if (ccinet_wakelock_timeout)
			wake_lock_timeout(&ccinet_wakelock,
					  (ccinet_wakelock_timeout / 1000) *
					  HZ);
		else
			wake_lock_timeout(&ccinet_wakelock,
					  (DEFAULT_NET_WAKELOCK_TO / 1000) *
					  HZ);
		/* add wakelock - start */
	}
	return 0;

out:
	return -1;

}

void data_rx(char *packet, int len, unsigned char cid)
{
	struct CINETDEVLIST *pdrv;
	int err;
	err = search_list_by_cid(cid, &pdrv);
	if (err == 0)
		ccinet_rx(pdrv->ccinet, packet, len);
	return;
}

/* YG: act as a bridge function -temp */
BOOL ccinet_data_rx_cb(UINT32 cid, void *packet_address, UINT32 packet_length)
{
	data_rx((char *)packet_address, (int)packet_length, (unsigned char)cid);

	return TRUE;		/* mark packet as handled */
}

static int validate_addr(struct net_device *netdev)
{
	ENTER();
	return 0;
}

/*///////////////////////////////////////////////////////////////
 / Initialization
 //////////////////////////////////////////////////////////////*/
static const struct net_device_ops cci_netdev_ops = {
	.ndo_open = ccinet_open,
	.ndo_stop = ccinet_stop,
	.ndo_start_xmit = ccinet_tx,
	.ndo_tx_timeout = ccinet_tx_timeout,
	.ndo_get_stats = ccinet_get_stats,
	.ndo_validate_addr = validate_addr
};

static void ccinet_setup(struct net_device *netdev)
{

	struct ccinet_priv *devpriv;

	ENTER();
	ether_setup(netdev);
	netdev->mtu = CCINET_DEFAULT_MTU_SIZE;
	netdev->netdev_ops = &cci_netdev_ops;
/*netdev->watchdog_timeo = 5;  //jiffies */
	netdev->flags |= IFF_NOARP;
	devpriv = netdev_priv(netdev);
	memset(devpriv, 0, sizeof(struct ccinet_priv));
	spin_lock_init(&devpriv->lock);

}

static struct miscdevice ccinet_miscdev = {
	MISC_DYNAMIC_MINOR,
	"ccichar",
	&ccinet_fops,
};

static int ccinet_probe(struct platform_device *dev)
{
	int err;

	err = misc_register(&ccinet_miscdev);
	ASSERT(err == 0);

	return 0;
}

static int __init ccinet_init(void)
{
	int ret;

	ENTER();

/* add wakelock - start */
	pr_err("**********************  ");
	pr_err("INIT CCINET WAKELOCK *********************\n");

	wake_lock_init(&ccinet_wakelock, WAKE_LOCK_SUSPEND, "ccinet_lock");
	set_ccinet_wakelock_ptr(&ccinet_wakelock_timeout);
/* add wakelock - end */

	ret = platform_device_register(&ccinet_device);
	if (!ret) {
		ret = platform_driver_register(&ccinet_driver);
		if (ret)
			platform_device_unregister(&ccinet_device);
	} else {
		pr_err("Cannot register CCINET platform device\n");
	}

	return ret;

};

static void __exit ccinet_exit(void)
{

/* add wakelock - start */
	wake_lock_destroy(&ccinet_wakelock);
/* add wakelock - end */
	remove_drv_list();
	platform_driver_unregister(&ccinet_driver);
	platform_device_unregister(&ccinet_device);
}

static void ccinet_dev_release(struct device *dev)
{
	return;
}

static int ccinet_remove(struct platform_device *dev)
{
	remove_drv_list();
	misc_deregister(&ccinet_miscdev);
	return 0;
}

static int ccichar_open(struct inode *inode, struct file *filp)
{
	struct ccichar_dev *dev;

	ENTER();
	dev = container_of(inode->i_cdev, struct ccichar_dev, cdev);

	filp->private_data = dev;

	return nonseekable_open(inode, filp);
}

static long ccichar_ioctl(struct file *filp, unsigned int cmd,
			  unsigned long arg)
{
	unsigned char cid;
	struct CINETDEVLIST *pdrv;
	struct ccinet_priv *devobj;
	int err;
	char intfname[20];
	ccinet_ip_param ip_param;
	if (_IOC_TYPE(cmd) != CCINET_IOC_MAGIC) {
		DBGMSG("ccichar_ioctl: cci magic number is wrong!\n");
		return -ENOTTY;
	}

	switch (cmd) {
	case CCINET_IP_ENABLE:
		cid = (unsigned char)arg;
		DBGMSG("ccichar_ioctl: CCINET_IP_ENABLE,cid=%d\n", cid);
		err = search_list_by_cid(cid, &pdrv);
		if (err == 0) {
			pr_err("ccichar_ioctl: cid already in drv list.\n");
			return 0;
		}
		/* registering on Data channel for downlink */
		ci_data_downlink_register(cid,
					  (ci_data_downlink_data_receive_cb)
					  ccinet_data_rx_cb);
		pdrv = kmalloc(sizeof(struct CINETDEVLIST), GFP_KERNEL);
		/* pdrv->ccinet = alloc_netdev(sizeof(struct ccinet_priv),
		   "ccinet%d",ccinet_setup); */
		snprintf(intfname, sizeof(intfname) - 1, "ccinet%d", cid);
		intfname[sizeof(intfname) - 1] = '\0';	/*just in case... */
		DBGMSG("ccichar_ioctl: intfname=%s\n", intfname);
		pdrv->ccinet =
		    alloc_netdev(sizeof(struct ccinet_priv), intfname,
				 ccinet_setup);
		ASSERT(pdrv->ccinet);
		err = register_netdev(pdrv->ccinet);
		ASSERT(err == 0);
		devobj = netdev_priv(pdrv->ccinet);
		devobj->cid = cid;
		pdrv->next = NULL;
		add_to_drv_list(pdrv);
		break;
	case CCINET_IP_DISABLE:
		pr_err("\nCCINET DBG: CCINET_IP_DISABLE\n");
		cid = (unsigned char)arg;
		remove_node_by_cid(cid);

		/* Un-registering on Data channel for downlink */
		ci_data_downlink_unregister(cid);
		break;

	case CCINET_ALL_IP_DISABLE:
		pr_err("\nCCINET DBG: CCINET_ALL_IP_DISABLE\n");
		remove_drv_list();
		break;

	case CCINET_IPV6_CONFIG:
		pr_err("CCINET DBG: CCINET_IPV6_CONFIG\n");
		if (copy_from_user
		    (&ip_param, (ccinet_ip_param *) arg,
		     sizeof(ccinet_ip_param))) {
			DBGMSG(1,
			       "ccichar_ioctl: CCINET_IP_ENABLE - ERROR copy");
			return -EFAULT;
		}
		err = search_list_by_cid(ip_param.cid, &pdrv);
		if (err != 0) {
			pr_err("ccichar_ioctl:  cid already in drv list.\n");
			return 0;
		}
		devobj = netdev_priv(pdrv->ccinet);
		devobj->is_ip6 = TRUE;
		devobj->cid = ip_param.cid;
		memcpy((char *)&(devobj->ipv6_addr[0]), ip_param.ipv6_addr,
		       sizeof(ip_param.ipv6_addr));
		pr_err("CCINET DBG: CCINET_IPV6_CONFIG: cid %d, is_ip6 = %d\n",
		       (int)ip_param.cid, (int)devobj->is_ip6);
		break;

	case CI_DATA_IOCTL_GET_V6_IPADDR:
		pr_err("CCINET DBG: CI_DATA_IOCTL_GET_V6_IPADDR\n");

		if (copy_from_user
		    (&ip_param, (ccinet_ip_param *) arg,
		     sizeof(ccinet_ip_param))) {
			DBGMSG(1,
			       "ccichar_ioctl: CCINET_IP_ENABLE - ERROR copy");
			return -EFAULT;
		}

		err = search_list_by_cid(ip_param.cid, &pdrv);
		if (err != 0) {
			pr_err("ccichar_ioctl:  cid already in drv list.\n");
			return 0;
		}
		devobj = netdev_priv(pdrv->ccinet);
		memcpy(ip_param.ipv6_addr, devobj->ipv6_addr,
		       sizeof(ip_param.ipv6_addr));

		if (copy_to_user
		    ((void *)arg, (void *)&ip_param, sizeof(ccinet_ip_param))) {
			pr_err("CI_DATA_IOCTL_GET_V6_IPADDR copy-to-user FAILED!!!:");
			pr_err("%08X::%08X::%08X::%08X\n", devobj->ipv6_addr[0],
			       devobj->ipv6_addr[1], devobj->ipv6_addr[2],
			       devobj->ipv6_addr[3]);
			return -EFAULT;
		}
		break;
	}
	return 0;
}

module_init(ccinet_init);
module_exit(ccinet_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marvell");
MODULE_DESCRIPTION("Marvell CI Network Driver");
