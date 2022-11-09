/*
 * videobuf2-dma-sg.c - dma scatter/gather memory allocator for videobuf2
 *
 * Copyright (C) 2010 Samsung Electronics
 *
 * Author: Andrzej Pietrasiewicz <andrzej.p@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#include <linux/dma-buf.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <media/videobuf2-core.h>
#include <media/videobuf2-memops.h>
#include <media/videobuf2-dma-sg.h>

static int debug;
module_param(debug, int, 0644);

#define dprintk(level, fmt, arg...)					\
	do {								\
		if (debug >= level)					\
			printk(KERN_DEBUG "vb2-dma-sg: " fmt, ## arg);	\
	} while (0)

struct vb2_dma_sg_conf {
	struct device		*dev;
};

struct vb2_dma_sg_buf {
	void				*vaddr;
	struct page			**pages;
	int				write;
	int				offset;
	struct vb2_dma_sg_desc		sg_desc;
	atomic_t			refcount;
	struct vb2_vmarea_handler	handler;
	/* DMABUF related */
	struct dma_buf_attachment       *db_attach;
};

static void vb2_dma_sg_put(void *buf_priv);

static void *vb2_dma_sg_alloc(void *alloc_ctx, unsigned long size, gfp_t gfp_flags)
{
	struct vb2_dma_sg_buf *buf;
	int i;

	buf = kzalloc(sizeof *buf, GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->vaddr = NULL;
	buf->write = 0;
	buf->offset = 0;
	buf->sg_desc.size = size;
	/* size is already page aligned */
	buf->sg_desc.num_pages = size >> PAGE_SHIFT;

	buf->sg_desc.sglist = vzalloc(buf->sg_desc.num_pages *
				      sizeof(*buf->sg_desc.sglist));
	if (!buf->sg_desc.sglist)
		goto fail_sglist_alloc;
	sg_init_table(buf->sg_desc.sglist, buf->sg_desc.num_pages);

	buf->pages = kzalloc(buf->sg_desc.num_pages * sizeof(struct page *),
			     GFP_KERNEL);
	if (!buf->pages)
		goto fail_pages_array_alloc;

	for (i = 0; i < buf->sg_desc.num_pages; ++i) {
		buf->pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO |
					   __GFP_NOWARN | gfp_flags);
		if (NULL == buf->pages[i])
			goto fail_pages_alloc;
		sg_set_page(&buf->sg_desc.sglist[i],
			    buf->pages[i], PAGE_SIZE, 0);
	}

	buf->handler.refcount = &buf->refcount;
	buf->handler.put = vb2_dma_sg_put;
	buf->handler.arg = buf;

	atomic_inc(&buf->refcount);

	dprintk(1, "%s: Allocated buffer of %d pages\n",
		__func__, buf->sg_desc.num_pages);
	return buf;

fail_pages_alloc:
	while (--i >= 0)
		__free_page(buf->pages[i]);
	kfree(buf->pages);

fail_pages_array_alloc:
	vfree(buf->sg_desc.sglist);

fail_sglist_alloc:
	kfree(buf);
	return NULL;
}

static void vb2_dma_sg_put(void *buf_priv)
{
	struct vb2_dma_sg_buf *buf = buf_priv;
	int i = buf->sg_desc.num_pages;

	if (atomic_dec_and_test(&buf->refcount)) {
		dprintk(1, "%s: Freeing buffer of %d pages\n", __func__,
			buf->sg_desc.num_pages);
		if (buf->vaddr)
			vm_unmap_ram(buf->vaddr, buf->sg_desc.num_pages);
		vfree(buf->sg_desc.sglist);
		while (--i >= 0)
			__free_page(buf->pages[i]);
		kfree(buf->pages);
		kfree(buf);
	}
}

static void *vb2_dma_sg_get_userptr(void *alloc_ctx, unsigned long vaddr,
				    unsigned long size, int write)
{
	struct vb2_dma_sg_buf *buf;
	unsigned long first, last;
	int num_pages_from_user, i;

	buf = kzalloc(sizeof *buf, GFP_KERNEL);
	if (!buf)
		return NULL;

	buf->vaddr = NULL;
	buf->write = write;
	buf->offset = vaddr & ~PAGE_MASK;
	buf->sg_desc.size = size;

	first = (vaddr           & PAGE_MASK) >> PAGE_SHIFT;
	last  = ((vaddr + size - 1) & PAGE_MASK) >> PAGE_SHIFT;
	buf->sg_desc.num_pages = last - first + 1;

	buf->sg_desc.sglist = vzalloc(
		buf->sg_desc.num_pages * sizeof(*buf->sg_desc.sglist));
	if (!buf->sg_desc.sglist)
		goto userptr_fail_sglist_alloc;

	sg_init_table(buf->sg_desc.sglist, buf->sg_desc.num_pages);

	buf->pages = kzalloc(buf->sg_desc.num_pages * sizeof(struct page *),
			     GFP_KERNEL);
	if (!buf->pages)
		goto userptr_fail_pages_array_alloc;

	num_pages_from_user = get_user_pages(current, current->mm,
					     vaddr & PAGE_MASK,
					     buf->sg_desc.num_pages,
					     write,
					     1, /* force */
					     buf->pages,
					     NULL);

	if (num_pages_from_user != buf->sg_desc.num_pages)
		goto userptr_fail_get_user_pages;

	sg_set_page(&buf->sg_desc.sglist[0], buf->pages[0],
		    PAGE_SIZE - buf->offset, buf->offset);
	size -= PAGE_SIZE - buf->offset;
	for (i = 1; i < buf->sg_desc.num_pages; ++i) {
		sg_set_page(&buf->sg_desc.sglist[i], buf->pages[i],
			    min_t(size_t, PAGE_SIZE, size), 0);
		size -= min_t(size_t, PAGE_SIZE, size);
	}
	return buf;

userptr_fail_get_user_pages:
	dprintk(1, "get_user_pages requested/got: %d/%d]\n",
	       num_pages_from_user, buf->sg_desc.num_pages);
	while (--num_pages_from_user >= 0)
		put_page(buf->pages[num_pages_from_user]);
	kfree(buf->pages);

userptr_fail_pages_array_alloc:
	vfree(buf->sg_desc.sglist);

userptr_fail_sglist_alloc:
	kfree(buf);
	return NULL;
}

/*
 * @put_userptr: inform the allocator that a USERPTR buffer will no longer
 *		 be used
 */
static void vb2_dma_sg_put_userptr(void *buf_priv)
{
	struct vb2_dma_sg_buf *buf = buf_priv;
	int i = buf->sg_desc.num_pages;

	dprintk(1, "%s: Releasing userspace buffer of %d pages\n",
	       __func__, buf->sg_desc.num_pages);
	if (buf->vaddr)
		vm_unmap_ram(buf->vaddr, buf->sg_desc.num_pages);
	while (--i >= 0) {
		if (buf->write)
			set_page_dirty_lock(buf->pages[i]);
		put_page(buf->pages[i]);
	}
	vfree(buf->sg_desc.sglist);
	kfree(buf->pages);
	kfree(buf);
}

static void *vb2_dma_sg_vaddr(void *buf_priv)
{
	struct vb2_dma_sg_buf *buf = buf_priv;

	BUG_ON(!buf);

	if (!buf->vaddr)
		buf->vaddr = vm_map_ram(buf->pages,
					buf->sg_desc.num_pages,
					-1,
					PAGE_KERNEL);

	/* add offset in case userptr is not page-aligned */
	return buf->vaddr + buf->offset;
}

static unsigned int vb2_dma_sg_num_users(void *buf_priv)
{
	struct vb2_dma_sg_buf *buf = buf_priv;

	return atomic_read(&buf->refcount);
}

static int vb2_dma_sg_mmap(void *buf_priv, struct vm_area_struct *vma)
{
	struct vb2_dma_sg_buf *buf = buf_priv;
	unsigned long uaddr = vma->vm_start;
	unsigned long usize = vma->vm_end - vma->vm_start;
	int i = 0;

	if (!buf) {
		printk(KERN_ERR "No memory to map\n");
		return -EINVAL;
	}

	do {
		int ret;

		ret = vm_insert_page(vma, uaddr, buf->pages[i++]);
		if (ret) {
			printk(KERN_ERR "Remapping memory, error: %d\n", ret);
			return ret;
		}

		uaddr += PAGE_SIZE;
		usize -= PAGE_SIZE;
	} while (usize > 0);


	/*
	 * Use common vm_area operations to track buffer refcount.
	 */
	vma->vm_private_data	= &buf->handler;
	vma->vm_ops		= &vb2_common_vm_ops;

	vma->vm_ops->open(vma);

	return 0;
}

static void *vb2_dma_sg_cookie(void *buf_priv)
{
	struct vb2_dma_sg_buf *buf = buf_priv;

	return &buf->sg_desc;
}

/*********************************************/
/*       callbacks for DMABUF buffers        */
/*********************************************/

static int vb2_dma_sg_map_dmabuf(void *mem_priv)
{
	struct vb2_dma_sg_buf *buf = mem_priv;

	if (WARN_ON(!buf->db_attach)) {
		pr_err("trying to pin a non attached buffer\n");
		return -EINVAL;
	}

	if (WARN_ON(buf->sg_desc.sgt)) {
		pr_err("dmabuf buffer is already pinned\n");
		return 0;
	}

	/* get the associated scatterlist for this buffer */
	buf->sg_desc.sgt = dma_buf_map_attachment(buf->db_attach,
				buf->write ? DMA_FROM_DEVICE : DMA_TO_DEVICE);
	if (IS_ERR_OR_NULL(buf->sg_desc.sgt)) {
		pr_err("Error getting dmabuf scatterlist\n");
		return -EINVAL;
	}

	buf->sg_desc.sglist = buf->sg_desc.sgt->sgl;
	buf->sg_desc.num_pages = PAGE_ALIGN(buf->sg_desc.size) / PAGE_SIZE;

	return 0;
}

static void vb2_dma_sg_unmap_dmabuf(void *mem_priv)
{
	struct vb2_dma_sg_buf *buf = mem_priv;

	if (WARN_ON(!buf->db_attach)) {
		pr_err("trying to unpin a not attached buffer\n");
		return;
	}

	if (WARN_ON(!buf->sg_desc.sgt)) {
		pr_err("dmabuf buffer is already unpinned\n");
		return;
	}

	dma_buf_unmap_attachment(buf->db_attach, buf->sg_desc.sgt,
				buf->write ? DMA_FROM_DEVICE : DMA_TO_DEVICE);

	buf->sg_desc.size = 0;
	buf->sg_desc.num_pages = 0;
	buf->sg_desc.sglist = NULL;
	buf->sg_desc.sgt = NULL;
}

static void vb2_dma_sg_detach_dmabuf(void *mem_priv)
{
	struct vb2_dma_sg_buf *buf = mem_priv;

	/* if vb2 works correctly you should never detach mapped buffer */
	if (WARN_ON(buf->sg_desc.sgt))
		vb2_dma_sg_unmap_dmabuf(buf);

	/* detach this attachment */
	dma_buf_detach(buf->db_attach->dmabuf, buf->db_attach);
	kfree(buf);
}

static void *vb2_dma_sg_attach_dmabuf(void *alloc_ctx, struct dma_buf *dbuf,
	unsigned long size, int write)
{
	struct vb2_dma_sg_conf *conf = alloc_ctx;
	struct vb2_dma_sg_buf *buf;
	struct dma_buf_attachment *dba;

	if (dbuf->size < size)
		return ERR_PTR(-EFAULT);

	buf = kzalloc(sizeof(*buf), GFP_KERNEL);
	if (!buf)
		return ERR_PTR(-ENOMEM);

	/* create attachment for the dmabuf with the user device */
	dba = dma_buf_attach(dbuf, conf->dev);
	if (IS_ERR(dba)) {
		pr_err("failed to attach dmabuf\n");
		kfree(buf);
		return dba;
	}

	buf->write = write;
	buf->sg_desc.size = size;
	buf->db_attach = dba;

	return buf;
}

const struct vb2_mem_ops vb2_dma_sg_memops = {
	.alloc		= vb2_dma_sg_alloc,
	.put		= vb2_dma_sg_put,
	.get_userptr	= vb2_dma_sg_get_userptr,
	.put_userptr	= vb2_dma_sg_put_userptr,
	.vaddr		= vb2_dma_sg_vaddr,
	.mmap		= vb2_dma_sg_mmap,
	.num_users	= vb2_dma_sg_num_users,
	.cookie		= vb2_dma_sg_cookie,
	.map_dmabuf	= vb2_dma_sg_map_dmabuf,
	.unmap_dmabuf	= vb2_dma_sg_unmap_dmabuf,
	.attach_dmabuf	= vb2_dma_sg_attach_dmabuf,
	.detach_dmabuf	= vb2_dma_sg_detach_dmabuf,
};
EXPORT_SYMBOL_GPL(vb2_dma_sg_memops);

void *vb2_dma_sg_init_ctx(struct device *dev)
{
	struct vb2_dma_sg_conf *conf;

	conf = devm_kzalloc(dev, sizeof(*conf), GFP_KERNEL);
	if (!conf)
		return ERR_PTR(-ENOMEM);

	conf->dev = dev;

	return conf;
}
EXPORT_SYMBOL_GPL(vb2_dma_sg_init_ctx);

void vb2_dma_sg_cleanup_ctx(void *alloc_ctx)
{
	struct vb2_dma_sg_conf *conf = alloc_ctx;
	struct device *dev = conf->dev;
	devm_kfree(dev, alloc_ctx);
}
EXPORT_SYMBOL_GPL(vb2_dma_sg_cleanup_ctx);

MODULE_DESCRIPTION("dma scatter/gather memory handling routines for videobuf2");
MODULE_AUTHOR("Andrzej Pietrasiewicz");
MODULE_LICENSE("GPL");
