/*
 * itm.c: Interface traffic manager.
 *
 *  Copyright:  (C) Copyright 2008-2010 Marvell International Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  publishhed by the Free Software Foundation.
 */

/*-------------------------------------------------------------*/
/* This is a stub file for use when kernel doesn't support ITM */
/*-------------------------------------------------------------*/
#include "itm.h"

#define DBG(lvl, fmt, arg...) do {} while (0)	/* Do nothing */

struct fp_net_device *classifier_dummy(struct fp_net_device
				       *dev, u8 *data_ptr, u32 data_size, bool mangle)
{
	return NULL;
}

/****************************************************************************
 *				ITM API
 ****************************************************************************/
classifier_f itm_register(struct fp_net_device *fp_dev)
{
	(void)fp_dev;
	return &classifier_dummy;
}

void itm_unregister(struct fp_net_device *fp_dev)
{
	(void)fp_dev;
}

void itm_register_flushdb(void (*cb) (void))
{
	(void)cb;
}

void itm_register_classifier(classifier_f classifier, char *owner)
{
	(void)classifier;
	(void)owner;
}

void itm_unregister_classifier(char *owner)
{
	(void)owner;
}

struct fp_net_device *itm_query_interface(struct net_device *dev)
{
	(void)dev;
	return NULL;
}

void itm_register_allocator(struct itm_allocator *new, char *owner)
{
	(void)new;
	(void)owner;
}

void itm_unregister_allocator(char *owner)
{
	(void)owner;
}

struct itm_allocator *itm_get_allocator(void)
{
	return NULL;
}

void fpdev_hold(struct fp_net_device *fp_dev)
{
	(void)fp_dev;
}

void fpdev_put(struct fp_net_device *fp_dev)
{
	(void)fp_dev;
}

void itm_flush_all_pending_packets(void)
{
}
