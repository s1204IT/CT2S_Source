/*
 *(C) Copyright 2010 Marvell International Ltd.
 * All Rights Reserved
 */
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/wait.h>
#include <linux/tty.h>
#include <linux/tty_driver.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

#include <linux/tty_ldisc.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/module.h>
#include <linux/errno.h>

#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/mman.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <asm/pgtable.h>
/*#include <asm/cachectl.h>*/
#include <linux/delay.h>
#include "hwacc.h"

/*#define HWMAP_DEBUG*/

#define DRIVER_VERSION "v1.0"
#define DRIVER_AUTHOR "Marvell"
#define DRIVER_DESC "Driver for HW access via physical address"

/* Module information */
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

static void hwmap_vma_open(struct vm_area_struct *vma)
{
#if defined(HWMAP_DEBUG)
	pr_err("HWMAP OPEN 0x%lx -> 0x%lx (0x%lx)\n",
		   vma->vm_start,
		   vma->vm_pgoff << PAGE_SHIFT,
		   vma->vm_page_prot);
#endif
}

static void hwmap_vma_close(struct vm_area_struct *vma)
{
#if defined(HWMAP_DEBUG)
	pr_err("HWMAP CLOSE 0x%lx -> 0x%lx\n",
		   vma->vm_start,
		   vma->vm_pgoff << PAGE_SHIFT);
#endif
}

/* These are mostly for debug: do nothing useful otherwise*/
static struct vm_operations_struct vm_ops = {
	.open = hwmap_vma_open,
	.close = hwmap_vma_close
};

/* device memory map method */
/*
 vma->vm_end, vma->vm_start : specify the user space process
 address range assigned when mmap has been called;
 vma->vm_pgoff - is the physical address supplied by user to
 mmap in the last argument (off)
 However, mmap restricts the offset, so we pass this shifted
 12 bits right.
 */
int hwmap_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long pa = vma->vm_pgoff;

	/* we do not want to have this area swapped out, lock it */
	vma->vm_flags |= (VM_DONTEXPAND | VM_DONTDUMP | VM_IO);
	/* see linux/drivers/char/mem.c*/
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	/* TBD: without this vm_page_prot=0x53 and write seem to not
	 * reach the destination:
	 * - no fault on write
	 * - read immediately (same user process) return the new value
	 * - read after a second by another user process instance return
	 *   original value
	 * Why PROT_WRITE specified by mmap caller does not take effect?
	 * MAP_PRIVATE used by app results in copy-on-write behaviour,
	 * which is irrelevant for this application
	 * vma->vm_page_prot|=L_PTE_WRITE;
	 */

	if (io_remap_pfn_range(vma, vma->vm_start, pa,	/* physical page index */
			       size, vma->vm_page_prot)) {
		pr_err("remap page range failed\n");
		return -ENXIO;
	}
	vma->vm_ops = &vm_ops;
	hwmap_vma_open(vma);
	return 0;
}

/*////////////////////////////////////////////////////////////////*/
unsigned long co_proc_rw;

static void generate_co_processor_instruction(unsigned long *op_code, struct cp_operands *cp_operands)
{
	unsigned int tmp_instruction = 0;
	tmp_instruction |= CO_PRO_OPERAND_OPC1_SET(cp_operands->opc1);
	tmp_instruction |= CO_PRO_OPERAND_CRN_SET(cp_operands->crn);
	tmp_instruction |= CO_PRO_OPERAND_CRM_SET(cp_operands->crm);

	tmp_instruction |= CO_PRO_OPERAND_RT_SET(8);	/* We use only r8 */

	tmp_instruction |= CO_PRO_OPERAND_OPC2_SET(cp_operands->opc2);
	tmp_instruction |= CO_PRO_OPERAND_COPROC_SET(cp_operands->coproc);
	*op_code = tmp_instruction;
}

static int execute_co_processor_instruction(unsigned int inst, char write)
{

	unsigned long value_to_check = 0;
	unsigned long *inst_rel_add = NULL;

#ifdef CONFIG_THUMB2_KERNEL
	/* The entire kernel and modules are compiled in thumb2, including
	   inline asm, which was intended to generate ARM mode code.
	   Thumb2 instruction set allows generating thumb instructions for
	   every ARM mode instruction written 1:1, except for conditionals,
	   which require an additional "it" instruction.
	   The only problem is with the run-time generated instruction here,
	   because it's an ARM mode one.
	   Luckily for MCR/MRC the layout of A1 and T1 opcodes is identical,
	   except for condition code [31:28], which must be 4b'1110 in T1
	   The order of half-words however is opposite (1st is lower address,
	   while in ARM upper part is higher address).
	   See DDI0406B, A8.6.100 and A8.6.92. */
	inst = (inst >> 16) | (inst << 16);
#endif

	/* function arm register summery:
	   r8   : Use for the value ARM-regiseter<-->co-proc-regiseter read/write
	   r5   : Use to Store the relative instruction address
	   r7   : use for debug purposes, to see if the the value is not changed in case of successful instruction chage.
	   ARM Cache problam:
	   See:
	   http://blogs.arm.com/software-enablement/caches-and-selfmodifying-code/
	 */

	__asm__ __volatile__("	adr %%r5, 26f;\n" "	mov %0, %%r5;\n" : "=r"(inst_rel_add) : : "%r5");

	*inst_rel_add = inst;	/*Write the passing parameter 32bit instruction to the reletive address '26'*/

	/*Clean dcash and invalidate i cash for single instruction */
	__asm__ __volatile__("dsb\n"
					"mcr p15,0,%0,c7,c10,1;			@Clean dcash\n"
					"mcr p15,0,%0,c7,c5,1;			@Invalidates a single I-Cache\n"
					"dsb;isb\n" : : "r"(inst_rel_add));

	if (HWAP_IOCTL_MCR == write)
		__asm__ __volatile__("mov %%r8, %0" : : "r"(co_proc_rw) : "%r8");

	__asm__ __volatile__("		mov %%r7, #0x1;\n"
			     "26:	mov %%r7, #0x0;\n			@this command will be replace with the user instructon\n"
			     "		mov %0, %%r7;\n" : "=r"(value_to_check) : : "%r7");

	__asm__ __volatile__("		mov %0, %%r8;\n" : "=r"(co_proc_rw) : : "%r8");

	return (value_to_check == 1 ? 0 : -1);

}

#define HERE(x) \
	pr_err("here here %s %d address = 0x%lx\n", __func__, __LINE__, x);
long hwmap_ioctl(struct file *file, unsigned int ioctl_num,	/* number and param for ioctl */
		 unsigned long ioctl_param)
{
	unsigned long inst = 0;
	int err, ret = 0;
	struct cp_operands cp_operands = { 0 };

	switch (ioctl_num) {
	case HWAP_IOCTL_MRC:	/*Read Co-pro register*/
		err = copy_from_user(&cp_operands, (struct cp_operands *)ioctl_param, sizeof(struct cp_operands));
		if (!err) {
			generate_co_processor_instruction(&inst, &cp_operands);
			ret = execute_co_processor_instruction(inst | ARM_V7_MRC_MASK, HWAP_IOCTL_MRC);
			cp_operands.rt = co_proc_rw;
			err = copy_to_user((struct cp_operands *)ioctl_param, &cp_operands, sizeof(struct cp_operands));
			if (err || ret) {
				HERE(ioctl_param)
				    return -1;
			}
		} else {
			HERE(ioctl_param)
			    return -1;
		}
		break;
	case HWAP_IOCTL_MCR:	/*Write Co-pro register*/
		err = copy_from_user(&cp_operands, (struct cp_operands *)ioctl_param, sizeof(struct cp_operands));
		if (!err) {
			generate_co_processor_instruction(&inst, &cp_operands);
			co_proc_rw = cp_operands.rt;
			ret = execute_co_processor_instruction(inst | ARM_V7_MCR_MASK, HWAP_IOCTL_MCR);
			ret |= execute_co_processor_instruction(inst | ARM_V7_MRC_MASK, HWAP_IOCTL_MRC);
			cp_operands.rt = co_proc_rw;
			err = copy_to_user((struct cp_operands *)ioctl_param, &cp_operands, sizeof(struct cp_operands));
			if (err || ret) {
				HERE(ioctl_param)
				    return -1;
			}
		} else {
			HERE(ioctl_param)
			    return -1;
		}
		break;
	default:
		pr_err("\nunknown cmd\n");
	}

	return 0;
}

/*/////////////////////////////////////////////////////////////////*/

static const struct file_operations hwmap_fops = {
	.owner = THIS_MODULE,
	.mmap = hwmap_mmap,
	.unlocked_ioctl = hwmap_ioctl
};

static struct miscdevice hwmap_miscdev = {
	MISC_DYNAMIC_MINOR,
	"hwmap",
	&hwmap_fops
};

static int __init hwmap_init(void)
{
	int err;
	err = misc_register(&hwmap_miscdev);
	if (err) {
		pr_err("%s can't register device\n", __func__);
		return -ENOMEM;
	}
	return 0;
}

static void __exit hwmap_exit(void)
{
	misc_deregister(&hwmap_miscdev);
	pr_debug("%s unregistering done\n", __func__);
}

#ifdef MODULE
module_init(hwmap_init);
module_exit(hwmap_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("HW memory mapped access");
#else
subsys_initcall(hwmap_init);
#endif
