/*
 *  include/linux/itm.h
 *
 * ITM - Interface Traffic Manager - Header file
 *
 * Copyright (C) 2010 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ITM__
#define __ITM__

#include <linux/netdevice.h>
#include <linux/list.h>

struct fp_packet_details {
	union {
		u32 offset;
		void *start;
	} buffer;
	u16 buffer_size;
	u16 packet_offset;
	u16 packet_length;
	u8 cid;
	u8 reserve;
	u32 *p_buf_to_free;
	struct fp_net_device *fp_src_dev;
	const struct fp_net_device *fp_dst_dev;
	u8 isFree;
	u8 dummy[7];
} __attribute__ ((packed));

enum fpdev_state {
	FPDEV_ALIVE,
	FPDEV_DYING,
};

struct fp_net_device {
	struct list_head list;	/* private - do not touch this member! */
	struct net_device *dev;
	int (*tx_cb) (struct fp_packet_details *p_details);
	int (*tx_mul_cb) (struct fp_packet_details *p_details[]);
	void (*free_cb) (struct fp_packet_details *p_details);
	void (*flush_cb) (struct net_device *net);
	void (*die_cb) (struct fp_net_device *fp_dev);
	bool mul_support;
	void *private_data;
	struct net_device_stats stats;
	/* Conut the number of the device
	   A device can be free only when
	   reference conunt will be zero */
	atomic_t refcnt;
	enum fpdev_state state;
};

/**
 * fastpath allocator
 * Used by fastpath interfaces to remove UL copy.
 * Example - WiFi/RNDIS --> CCINET:
 * WiFi/RNDIS allocates memory for new packets in CCINET
 * directly, thus eliminating the need for memcpy
 * by the CCINET
 */
struct itm_allocator {
	void *(*malloc) (size_t size, gfp_t flags);
	int (*free) (const void *ptr);
};

typedef struct fp_net_device *(*classifier_f) (struct fp_net_device *dev, u8 *data_ptr, u32 data_size, bool mangle);

/* ITM API declarations */
extern classifier_f itm_register(struct fp_net_device *fp_dev);
extern void itm_unregister(struct fp_net_device *fp_dev);
extern void itm_register_classifier(classifier_f classifier, char *owner);
extern void itm_register_flushdb(void (*cb) (void));
extern void itm_unregister_classifier(char *owner);
extern struct fp_net_device *itm_query_interface(struct net_device *dev);
extern void itm_register_allocator(struct itm_allocator *new, char *owner);
extern void itm_unregister_allocator(char *owner);
extern struct itm_allocator *itm_get_allocator(void);
extern void fpdev_hold(struct fp_net_device *fp_dev);
extern void fpdev_put(struct fp_net_device *fp_dev);
extern void itm_flush_all_pending_packets(void);

#endif /* __ITM__ */
