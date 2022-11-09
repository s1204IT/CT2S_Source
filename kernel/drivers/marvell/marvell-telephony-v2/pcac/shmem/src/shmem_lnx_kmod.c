/*
 * (C) Copyright 2006-2011 Marvell International Ltd.
 * All Rights Reserved
 */

/* ===========================================================================
File        : shmem_lnx_kmod.c
Description : Linux 2.6 operating system specific code for the OS Kernel
				Abstraction Layer API.
Notes       :

(C) Copyright 2008 Marvell International Ltd. All Rights Reserved.

=========================================================================== */
#include <linux/module.h>

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/aio.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>

#include "osa_kernel.h"
#include "allocator.h"
#include "shmem_lnx_kmod.h"
#include "com_mem_mapping_kernel.h"
#include "acipc_data.h"
#include "shmem_kernel_to_user_ipc.h"
#include "apcp_mmap.h"
#include "dbgLog.h"
#include "ac_rpc.h"
/*#include "assert.h" */

typedef void (*setPPPLoopback) (unsigned short port, int DLMult);
static setPPPLoopback pSetLoopBack;

void registerSetPPPLoppbackCB(setPPPLoopback f)
{
	pSetLoopBack = f;
}

EXPORT_SYMBOL(registerSetPPPLoppbackCB);

#define shmem_LINUXKERNEL_DEBUG

#ifdef	shmem_LINUXKERNEL_DEBUG
#define	shmem_DRV_MSG			printk
#else
#define	shmem_DRV_MSG(fmt, arg...)	do {} while (0)
#endif

#define	shmem_LINUX_KERNEL_MEM_ALIGN	4

typedef struct {
	ACIPCD_ActionE TxAction;
} SeciceIdToTxAction;


static int param_qos_mode;

module_param(param_qos_mode, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(param_qos_mode, "controls QOS feature");

DBGLOG_EXTERN_VAR(shmem);

#if 0
#error "if you want to enable auto-print-from-Oops - read the comment below"

/* 1) enable the ANOTHER "if 0" below
 * 2) you need to put the section below into the file "/version/android/kernel/kernel/arch/arm/mach-pxa/ramdump.c:
 * 3) comment the "#error"...

static void (*ACIPCDPrintDebugLogCB)(void);

void ramdump_register_acipc_cb_func(void (*acipcPrintDbgLogCB)(void))
{
    ACIPCDPrintDebugLogCB = acipcPrintDbgLogCB;
}
EXPORT_SYMBOL(ramdump_register_acipc_cb_func);

 * EDIT THE FOLLWONG FUNCTION (add as noted below) IN THE ABOVE FILE:
static int ramdump_panic(struct notifier_block *this, unsigned long event, void *ptr)
{
    printk(KERN_ERR "ACIPCD LOG PRINT %x\n", (void *)ACIPCDPrintDebugLogCB);
    ACIPCDPrintDebugLogCB();

    printk(KERN_ERR "RAMDUMP STARTED\n");
    ramdump_fill_rdc();
    ramdump_save_static_context(&ramdump_data);
    ramdump_save_isram();
    ramdump_flush_caches();
    printk(KERN_ERR "RAMDUMP DONE\n");
    return NOTIFY_DONE;
}
*/

/*those functions used for debug purposes - by printing SHMEM database when error occurred */
extern void ramdump_register_acipc_cb_func(void (*acipcPrintLogCB(void)));
extern void seh_register_acipc_cb_func(void (*ACIPCDPrintLogCB) (void));
extern void osa_register_acipc_cb_func(void (*acipcPrintLogCB(void)));
/*extern void eeh_api_register_acipc_cb_func(void (*acipcPrintLogCB(void))); */

void ACIPCDPrintAllShmemInfo(void)
{
	ACIPCDPrintDatabase();
	ACIPCDPrintShmemDebugLog();
	shmem_print_log_callback();
}

#endif /*if 0 */

/************************************************************************
 * Internal Interfaces
 ***********************************************************************/

/************************************************************************
 * Internal Variables
 ***********************************************************************/
struct platform_device *g_shmem_patform_linux_dev;
static int g_shmem_drv_open_count;
static struct semaphore g_shmem_sem;
static struct semaphore g_shmem_sem_mem_alloc;

static int shmem_drv_open(struct inode *inode, struct file *filp);
static ssize_t shmem_drv_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static ssize_t shmem_drv_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
static int shmem_drv_release(struct inode *inode, struct file *filp);
static int shmem_drv_probe(struct platform_device *pdev);
static int shmem_drv_remove(struct platform_device *pdev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
static long shmem_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#else
static int shmem_drv_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
#endif
static int shmem_mmap(struct file *file, struct vm_area_struct *vma);
static void shmem_vma_open(struct vm_area_struct *vma);
static void shmem_vma_close(struct vm_area_struct *vma);

/* add wakelock - start */
static long *wakelockptr;
/* add wakelock - end */

static struct file_operations shmem_fops = {
	.open = shmem_drv_open,
	.read = shmem_drv_read,
	.write = shmem_drv_write,
	.release = shmem_drv_release,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
	.unlocked_ioctl = shmem_drv_ioctl,
#else
	.ioctl = shmem_drv_ioctl,
#endif
	.mmap = shmem_mmap,
	.owner = THIS_MODULE
};

static struct miscdevice shmem_miscdev = {
	MISC_DYNAMIC_MINOR,
	"acipcddrv",
	&shmem_fops,
};

static struct vm_operations_struct shmem_vm_ops = {
	.open = shmem_vma_open,
	.close = shmem_vma_close
};

static struct platform_device shmem_dev_device = {
	.name = "acipcd-shmem",
	.id = 0,
};

static struct platform_driver shmem_dev_driver = {
	.driver = {
		   .name = "acipcd-shmem",
		   .owner = THIS_MODULE,
		   },
	.probe = shmem_drv_probe,
	.remove = shmem_drv_remove
};

extern void UpdatePendingDataBuffersPtr(unsigned int *ptr);

/***********************************************************************
 *
 * Name:        shmem_drv_ioctl
 *
 * Description:
 *
 * Parameters:
 * struct inode	*	inode	   - pointer to ...
 * struct file 	*	file	   - pointer to ...
 * unsigned int 	cmd		   - command
 * unsigned long 	arg
 *
 * Returns:
 *
 * Notes:
 *
 ***********************************************************************/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
static long shmem_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
#else
static int shmem_drv_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
#endif
{

/*

	User2Kerenl calls
	-------------------------
	Register
	Rx Done ind
	Send req

	shmemMemAlloc - call to OSA kerenel + convert to Task address space ptr
	shmemMemFree  - convert to kerenl pool ptr + call to OSA pool free
	-------------------------

	Kerenel2User callbacks
	-------------------------
	RxIndication
	TxComplete
	StatusCallback
	-------------------------
*/

	SHMEMApiParams ioctl_arg;

	/*printk("YG: shmem_drv_ioctl %x\n", arg); */
	if (copy_from_user(&ioctl_arg, (void *)arg, sizeof(SHMEMApiParams)))
		return -1;

	switch (cmd) {

	case SHMEM_IOCTL_POOL_GEOMETRY:
		{
			SHMEMPoolGeometry geo;

			geo.g_CommRWaddr = g_CommRWaddr;
			geo.uiCommRWAreaSizeInBytes = g_CommRWAreaSizeInBytes;
			geo.uiCommROArea = g_CommROArea;
			geo.uiCommROAreaSizeInBytes = g_CommROAreaSizeInBytes;
			geo.uiCommPoolPhyBegin = g_CommPoolPhyBegin;
			geo.uiAppPoolPhyBegin = g_AppPoolPhyBegin;
			geo.uiCommRWArea = g_CommRWArea;

			if (copy_to_user(((SHMEMApiParams *) arg)->params, &geo, sizeof(geo)))
				return -1;
		}
		break;
	case SHMEM_IOCTL_REGISTER:
		{
			SHMEMRegiter event;
			ACIPCD_CallBackFuncS cbKernel;

			if (copy_from_user(&event, (void *)ioctl_arg.params, sizeof(event)))
				return -1;

			/* replace cb (from user level) with default cb (net-link) */
			/* ACI_CI_DATA have a special case : kernel to kernel call */
			if ((event.ServiceId == ACIPCD_CI_DATA_REQ_CNF) || (event.ServiceId == ACIPCD_CI_DATA_IND_RSP)
			    || (event.ServiceId == ACIPCD_CI_DATA_CONTROL_OLD)) {
				memcpy(&cbKernel, &event.cbFunc, sizeof(ACIPCD_CallBackFuncS));
			} else {
				if (getKernelCBFromUser(event.ServiceId, &cbKernel, event.pid)) {
					printk(KERN_ERR "SHMEM registration aborted for ServiceId=%x pid=%d\n",
					       event.ServiceId, event.pid);
					break;
				}
			}

			/*secice_id_to_tx_action[event.ServiceId].TxAction = event.config.TxAction; */
			pr_debug("[BS] SHMEM_IOCTL_REGISTER, call ACIPCDRegister, ServiceId = %d\n", event.ServiceId);
			/*down(&g_shmem_sem); */
			ioctl_arg.status = ACIPCDRegister(event.ServiceId, &cbKernel, &event.config);
			pr_debug("[BS] SHMEM_IOCTL_REGISTER, call ACIPCDRegister, ServiceId = %d - Done !!!\n",
				 event.ServiceId);
			/*up(&g_shmem_sem); */
		}
		break;
	case SHMEM_IOCTL_RX_DONE_RSP:
		{
			SHMEMRxDoneResp event;

			if (copy_from_user(&event, (void *)ioctl_arg.params, sizeof(event))) {
				ASSERT(FALSE);
				return -1;
			}
			/*down(&g_shmem_sem); */
			ioctl_arg.status = ACIPCDRxDoneRspACIPC(event.ServiceId, event.pData, event.dontMakeACIPCirq);
			/*up(&g_shmem_sem); */
		}
		break;
	case SHMEM_IOCTL_SEND_REQ:
		{
			SHMEMSendReq event;
			unsigned char *pKerenleBuffer = NULL;

			if (copy_from_user(&event, (void *)ioctl_arg.params, sizeof(event)))
				return -1;

			if (event.dontMakeACIPCirq > 1) {
				/*not unsigned char */
				ACIPCEventSetExt(ACSIPC, (ACIPC_EventsE) event.dontMakeACIPCirq);
				break;
			}

			pKerenleBuffer = event.pData;

			/*down(&g_shmem_sem); */
			ioctl_arg.status =
			    ACIPCDTxReqACIPC(event.ServiceId, pKerenleBuffer, event.Length, event.dontMakeACIPCirq);
			/*up(&g_shmem_sem); */
		}
		break;

	case SHMEM_IOCTL_ALLOC_EVENT:
		{
			SHMEMMalloc event;

			if (copy_from_user(&event, (void *)ioctl_arg.params, sizeof(event)))
				return -1;

			/*
			   1. alloc from kerenl OSA Pool
			   2. translate from kerenel2user address space
			   2.2. find associaded App pool (with pid).
			   2.3. convert OSA Pool address to this user address space.
			   3. pass to user allocated ptr.
			 */

			event.uiOffset = (unsigned int)OsaMemAlloc((void *)g_PoolXRef, event.uiSize);

			if (copy_to_user(((SHMEMApiParams *) arg)->params, &event, sizeof(event)))
				return -1;
		}
		break;
	case SHMEM_IOCTL_FREE_EVENT:
		{
			SHMEMFree event;

			if (copy_from_user(&event, (void *)ioctl_arg.params, sizeof(event)))
				return -1;

			OsaMemFree((void *)event.uiOffset);
		}
		break;

	case SHMEM_IOCTL_KERENL_DATA_CB_FN:
		{
			SHMEMDataCb event;

			if (copy_from_user(&event, (void *)ioctl_arg.params, sizeof(event)))
				return -1;

			/* get ci data kernel callbacks */
			GetCallbacksKernelFn(&event.ciDataCbFunctions, event.callbackType);
			/*printk("\n\nYG DBG: SHMEM_IOCTL_KERENL_DATA_CB_FN type=%d, linkcb=%x\n\n", event.callbackType, event.ciDataCbFunctions.LinkStatusIndCB); */
			if (copy_to_user(((SHMEMApiParams *) arg)->params, &event, sizeof(event)))
				return -1;
		}
		break;

	case SHMEM_IOCTL_SET_PENDING_BUFFERS_PTR:
		{
			UpdatePendingDataBuffersPtr(ioctl_arg.params);
		}
		break;

	case SHMEM_IOCTL_LINK_DOWN:
		{
			/* 	Reset interface
				DOWN has been notified in SHMEM_IOCTL_WDT_EVENT
				status = request for real/full reset execute or notify/partial */
			ACIPCDComIfReset(ioctl_arg.status);
		}
		break;

	case SHMEM_IOCTL_LINK_UP:
		{
			ACIPCDComRecoveryIndicate();
		}
		break;

	case SHMEM_IOCTL_GET_LOG_SIZE:
		{
			unsigned int size;

			size = DBGLOG_GET_SIZE(shmem);
			pr_debug("[BS] SHMEM_IOCTL_GET_LOG_SIZE, copy to use, size = %d\n", size);
			if (copy_to_user(((SHMEMApiParams *) arg)->params, &size, sizeof(unsigned int))) {
				pr_err("[BS] SHMEM_IOCTL_GET_LOG_SIZE, copy to use failed\n");
				return -EFAULT;
			}
			pr_debug("[BS] SHMEM_IOCTL_GET_LOG_SIZE, copy to user successful\n");
		}
		break;

	case SHMEM_IOCTL_GET_LOG:
		{
			/*unsigned char  isEnabled; */
			unsigned long ret;

			DBGLOG_ENABLE(shmem, FALSE);

			ret =
			    copy_to_user(((SHMEMApiParams *) arg)->params, DBGLOG_GET_INFO(shmem),
					 DBGLOG_GET_SIZE(shmem));

			DBGLOG_ENABLE(shmem, TRUE);

			if (ret) {
				pr_err("[BS] SHMEM_IOCTL_GET_LOG, copy to use failed\n");
				return -EFAULT;
			}
			pr_debug("[BS] SHMEM_IOCTL_GET_LOG, copy to user successful\n");

			shmem_save_log_callback();

			/*YG: call to print debug data - temp only */
			ACIPCDPrintDatabase();

		}
		break;

	case SHMEM_IOCTL_WDT_EVENT:
		{
			/*Premature SHMEM clients notification about LINK_DOWN */
			/*The later SHMEM_IOCTL_LINK_DOWN executes reset */
			pr_debug("[BS] ioctl SHMEM_IOCTL_WDT_EVENT\n");
			DBGLOG_ADD(shmem, MDB_WdtEvent, 0, 0);
/*BS - Save SHMEM log on AP assert - start */
			DBGLOG_ENABLE(shmem, FALSE);
/*BS - Save SHMEM log on AP assert - end */
			ACIPCDComIfDownIndicate(ioctl_arg.status);
		}
		break;

	case GEN_RPC_TX_REQ:
		{
			RPC_ParamsS rpcParams;

			RegisterWithShmem();

			if (copy_from_user(&rpcParams, (void *)ioctl_arg.params, sizeof(rpcParams)))
				return -1;

			pr_debug("[BS] GEN_RPC_TX_REQ: calling RPCTxReq function %s\n", rpcParams.FuncName);

			/*down(&g_shmem_sem); */
			rpcParams.rc = RPCTxReq(rpcParams.FuncName,
						rpcParams.InLen, rpcParams.InPtr, rpcParams.OutLen, rpcParams.OutPtr);

			if (copy_to_user(((SHMEMApiParams *) arg)->params, &rpcParams, sizeof(rpcParams))) {
				pr_err("GEN_RPC_TX_REQ, copy to user failed\n");
				return -EFAULT;
			}
		}
		break;

	default:
		break;

	}

	/* return status */
	if (copy_to_user((void *)arg, &ioctl_arg, sizeof(ioctl_arg.status)))
		return -1;

	return 0;
}

/***********************************************************************
 *
 * Name:        shmem_drv_open()
 *
 * Description:
 *
 * Parameters:
 *
 * Returns:
 *
 * Notes:
 *
 ***********************************************************************/
static int shmem_drv_open(struct inode *inode, struct file *filp)
{
	shmem_DRV_MSG("shmem_drv_open: open_count=%d\n", g_shmem_drv_open_count);

	/* if shmem driver is opened already. Just increase the count */
	if (g_shmem_drv_open_count) {
		g_shmem_drv_open_count++;
		return 0;
	}

	/* Ferst time open actions here! */

	/* Increase count before exit */
	g_shmem_drv_open_count++;

	return 0;
}

/***********************************************************************
 *
 * Name:        shmem_drv_read()
 *
 * Description:
 *
 * Parameters:
 *
 * Returns:
 *
 * Notes:
 *
 ***********************************************************************/
static ssize_t shmem_drv_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	printStatistic();

	return 0;
}

void shmem_drv_print_help(void)
{
	printk("SHMEM Driver Available Options:\n");
	printk("---------------------------------------------------------------------------------\n\n");
	printk("-d             : Dump shmem kernel internal [D]atabase to terminal\n");
	printk("-l             : Dump shmem kernel debug [L]og to terminal\n");
	printk("-k             : Dump shmem all [K]ernel debug & info to terminal\n");
	printk("-u             : Dump shmem [U]ser-only info to terminal\n");
	printk("-s             : Dump all [S]hmem-only debug info above (kernel+user in order)!!!\n");
	printk("-c             : Dump [C]I data-channel-kernel debug to terminal\n");
	printk("-a             : Dump [A]ll debug info above (in order)!!!\n");
	printk("-v             : Enable IP packets checksum [V]erification!!!\n");
	printk("-f             : [F]orce ASSERT on COMM\n");
	printk("-p             : Display [P]ower database\n");
	printk("-o             : Print p[O]wer users debug database\n");
	printk("-w  <wakelock_timeout>    : set and enable [W]akelock timeout in milisecconds!!!\n");
}

/***********************************************************************
 *
 * Name:        shmem_drv_write()
 *
 * Description:
 *
 * Parameters:
 *
 * Returns:
 *
 * Notes:
 *
 ***********************************************************************/
static ssize_t shmem_drv_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	size_t index = 0;

	printk(buf);

	while (index < count) {
		if (buf[index++] == '-') {
			switch (buf[index]) {
			case 'H':
			case 'h':
				shmem_drv_print_help();
				break;

			case 'D':
			case 'd':
				ACIPCDPrintDatabase();
				index++;
				break;

			case 'L':
			case 'l':
				ACIPCDPrintShmemDebugLog();
				index++;
				break;

			case 'K':
			case 'k':
				ACIPCDPrintDatabase();
				ACIPCDPrintShmemDebugLog();
				index++;
				break;

			case 'U':
			case 'u':
				shmem_print_log_callback();
				index++;
				break;

			case 'S':
			case 's':
				ACIPCDPrintDatabase();
				ACIPCDPrintShmemDebugLog();
				shmem_print_log_callback();
				index++;
				break;

			case 'C':
			case 'c':
				DataChannelKernelPrintInfo();
				index++;
				break;

			case 'A':
			case 'a':
				DataChannelKernelPrintInfo();
				ACIPCDPrintDatabase();
				ACIPCDPrintShmemDebugLog();
				shmem_print_log_callback();
				index++;
				break;

			case 'W':
			case 'w':
				if (wakelockptr != NULL) {
					int bytes_read = 0;
					index++;
					bytes_read = sscanf(&buf[index], "%ld", wakelockptr);
					index += bytes_read;
					printk("Setting ccinet wakelock timeout to %ld\n", *wakelockptr);
				} else {
					printk("Setting ccinet wakelock timeout FAILED ptr not set\n");
				}
				break;

			case 'P':
			case 'p':
				ACIPCDPrintPowerDatabase();
				break;

			case 'O':
			case 'o':
				ACIPCDPrintPowerDebugDatabase();
				break;

			case 'F':
			case 'f':
				{
					RPCForceAssertOnComm();
					break;
				}
			case 'j':
			case 'J':
				{
					int bytes_read = 0;
					unsigned int port = 0;
					unsigned int dl_mult = 0;

					index++;
					bytes_read = sscanf(&buf[index], "%d %d", &port, &dl_mult);
					index += bytes_read;
					if (bytes_read) {
						if (pSetLoopBack)
							pSetLoopBack(port, dl_mult);
					}
					break;
				}
			default:
				printk("Unsupported switch -%c\n", buf[index]);
				shmem_drv_print_help();
				break;
			}

		}
	}
	printk("Done.\n");

	return index;
}

/***********************************************************************
 *
 * Name:        shmem_drv_release()
 *
 * Description:
 *
 * Parameters:
 *
 * Returns:
 *
 * Notes:
 *
 ***********************************************************************/
static int shmem_drv_release(struct inode *inode, struct file *filp)
{
	/* Just release the resource when access count because 0 */
	shmem_DRV_MSG("shmem_drv_release: release the resource\n");

	if (!--g_shmem_drv_open_count) {
		/*
		 *   Release resource              ...
		 *   Remove the file from the list ...
		 */
	}
	return 0;
}

/***********************************************************************
 *
 * Name:        shmem_drv_probe()
 *
 * Description:
 *
 * Parameters:
 *
 * Returns:
 *
 * Notes:
 *
 ***********************************************************************/
static int shmem_drv_probe(struct platform_device *pdev)
{
	int ret = -1;

	sema_init(&g_shmem_sem, 1);
	sema_init(&g_shmem_sem_mem_alloc, 1);

	/* map shared COM/Apps memory region */
	if (!MapComSHMEMRegions()) {
		shmem_DRV_MSG("shmem_drv_probe: unable to map Comm/Apps shared memory region\n");
		goto shmem_init_failed;
	}

	if (init_callback() < 0) {
		shmem_DRV_MSG("shmem_drv_probe: unable to init net-link socket callback\n");
		/*      goto shmem_init_failed; */

	}

	ACIPCDPhase1Init();
	ACIPCDPhase2Init();

	/* Register the char device */
	ret = misc_register(&shmem_miscdev);
	if (ret) {
		shmem_DRV_MSG("shmem_drv_probe: cannot register char device\n");
		goto shmem_init_failed;
	}

	g_shmem_patform_linux_dev = pdev;

	shmem_DRV_MSG("shmem_drv_probe: successful!\n");

	ret = 0;

shmem_init_failed:
	return ret;
}

/* add wakelock - start */
void set_ccinet_wakelock_ptr(long *ptr)
{
	wakelockptr = ptr;
}

EXPORT_SYMBOL(set_ccinet_wakelock_ptr);
/* add wakelock - end */

/***********************************************************************
 *
 * Name:        shmem_drv_remove()
 *
 * Description:
 *
 * Parameters:
 *
 * Returns:
 *
 * Notes:
 *
 ***********************************************************************/
static int shmem_drv_remove(struct platform_device *pdev)
{
	misc_deregister(&shmem_miscdev);
	return 0;
}

/***********************************************************************
 *
 * Name:        shmem_drv_init
 *
 * Description:
 *
 * Parameters:
 *
 * Returns:
 *
 * Notes:
 *
 ***********************************************************************/
static int __init shmem_drv_init(void)
{
	int ret;

	ret = platform_device_register(&shmem_dev_device);
	if (ret)
		printk("Cannot register SHMEM platform device \n");

	shmem_DRV_MSG("\n shmem_drv_init device_register ret:%d\n", ret);

	ret = platform_driver_register(&shmem_dev_driver);
	if (ret)
		printk("Cannot register SHMEM platform driver\n");

	shmem_DRV_MSG("\n shmem_drv_init finish ret:%d\n", ret);

	return ret;
}

/***********************************************************************
 *
 * Name:        shmem_drv_exit()
 *
 * Description:
 *
 * Parameters:
 *
 * Returns:
 *
 * Notes:
 *
 ***********************************************************************/
static void __exit shmem_drv_exit(void)
{
	shmem_DRV_MSG("\n shmem_drv_exit \n");
	platform_driver_unregister(&shmem_dev_driver);
}

/***********************************************************************
 *
 * Name:        shmem_mmap
 *
 * Description:
 *
 * Parameters:
 *
 * Returns:
 *
 * Notes:
 *
 ***********************************************************************/
static int shmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	/* User space supplies CP addresses (0xd0xxxxxx) */
	unsigned long pa = __phys_to_pfn((unsigned long)ADDR_CONVERT(__pfn_to_phys(vma->vm_pgoff)));
	vma->vm_pgoff = pa;

	/* we do not want to have this area swapped out, lock it */
	vma->vm_flags |= (/*VM_DONTCOPY | */ VM_IO | VM_DONTEXPAND | VM_DONTDUMP);
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	if (io_remap_pfn_range(vma, vma->vm_start, pa, size, vma->vm_page_prot)) {
		return -ENXIO;
	}

	vma->vm_ops = &shmem_vm_ops;
	shmem_vma_open(vma);

	return 0;
}

/* device memory map methods */
static void shmem_vma_open(struct vm_area_struct *vma)
{
}

static void shmem_vma_close(struct vm_area_struct *vma)
{
}

/******************************************************************************/
/********************** Module Export Symbols  ********************************/
/******************************************************************************/

/******************************************************************************/
/********************** Module Export Symbols End *****************************/
/******************************************************************************/
module_init(shmem_drv_init);
module_exit(shmem_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marvell");
MODULE_DESCRIPTION("Marvell SHMEM driver");
/******************************************************************************/
