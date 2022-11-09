/*
 * linux/drivers/video/mmp/hw/vsync.c
 * vsync driver support
 *
 * Copyright (C) 2013 Marvell Technology Group Ltd.
 * Authors:  Yu Xu <yuxu@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <video/mmp_disp.h>

static void mmp_overlay_vsync_cb(void *data)
{
	struct mmp_overlay *overlay = (struct mmp_overlay *)data;
	struct mmp_vdma_info *vdma;

	vdma = overlay->vdma;
	if (vdma && vdma->ops && vdma->ops->vsync_cb)
		vdma->ops->vsync_cb(vdma);
}

static void mmp_overlay_vsync_notify_init(struct mmp_overlay *overlay)
{
	struct mmp_vsync_notifier_node *notifier_node;

	notifier_node = &overlay->notifier_node;
	notifier_node->cb_notify = mmp_overlay_vsync_cb;
	notifier_node->cb_data = (void *)overlay;
	mmp_path_register_special_vsync_cb(overlay->path, notifier_node);
}

static int path_wait_vsync(struct mmp_path *path)
{
	struct mmp_vsync *vsync = &path->vsync;

	mmp_path_set_irq(path, 1);
	atomic_set(&vsync->ready, 0);
	wait_event_interruptible_timeout(vsync->waitqueue,
		atomic_read(&vsync->ready), HZ / 20);
	mmp_path_set_irq(path, 0);

	return 0;
}

static int path_wait_special_vsync(struct mmp_path *path)
{
	struct mmp_vsync *vsync = &path->special_vsync;

	mmp_path_set_irq(path, 1);
	atomic_set(&vsync->ready, 0);
	wait_event_interruptible_timeout(vsync->waitqueue,
		atomic_read(&vsync->ready), HZ / 20);
	mmp_path_set_irq(path, 0);

	return 0;
}

static void path_handle_irq(struct mmp_vsync *vsync)
{
	struct mmp_vsync_notifier_node *notifier_node;
	if (!atomic_read(&vsync->ready)) {
		atomic_set(&vsync->ready, 1);
		wake_up_all(&vsync->waitqueue);
	}

	list_for_each_entry(notifier_node, &vsync->notifier_list,
			node) {
		notifier_node->cb_notify(notifier_node->cb_data);
	}
}

void mmp_vsync_init(struct mmp_path *path)
{
	struct mmp_vsync *vsync = &path->vsync;
	struct mmp_vsync *special_vsync = &path->special_vsync;
	int i;

	init_waitqueue_head(&vsync->waitqueue);
	path->ops.wait_vsync = path_wait_vsync;
	vsync->handle_irq = path_handle_irq;
	INIT_LIST_HEAD(&vsync->notifier_list);

	init_waitqueue_head(&special_vsync->waitqueue);
	path->ops.wait_special_vsync = path_wait_special_vsync;
	special_vsync->handle_irq = path_handle_irq;
	INIT_LIST_HEAD(&special_vsync->notifier_list);

	for (i = 0; i < path->overlay_num; i++)
		mmp_overlay_vsync_notify_init(&path->overlays[i]);
}

void mmp_vsync_deinit(struct mmp_path *path)
{
	struct mmp_vsync *vsync = &path->vsync;
	struct mmp_vsync *special_vsync = &path->special_vsync;
	struct mmp_vsync_notifier_node *notifier_node;

	list_for_each_entry(notifier_node, &vsync->notifier_list,
			node) {
		mmp_path_unregister_vsync_cb(path,
			notifier_node);
	}

	list_for_each_entry(notifier_node, &special_vsync->notifier_list,
			node) {
		mmp_path_unregister_vsync_cb(path,
			notifier_node);
	}

	path->ops.wait_vsync = NULL;
	path->vsync.handle_irq = NULL;
	path->ops.wait_special_vsync = NULL;
	path->special_vsync.handle_irq = NULL;
}
