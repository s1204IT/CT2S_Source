/*------------------------------------------------------------
(C) Copyright [2006-2011] Marvell International Ltd.
All Rights Reserved
------------------------------------------------------------*/

/************************************************************************
	Title:  	 Common CI Data headerfile
	Filename:    ci_data.h
	Description: Contains defines/structs/prototypes for the R7/LTE data path
	Author: 	 Yossi Gabay
************************************************************************/

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/moduleparam.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/poll.h>

#include "osa_kernel.h"
#include "ci_data_ioctl.h"
#include "ci_data_common.h"
#include "ci_data_client.h"
#include "ci_data_client_tester.h"
#include "ci_data_client_api.h"
#include "ci_data_client_ltm.h"
#include "ci_data_client_handler.h"
#include "ci_data_client_loopback.h"
#include "ci_data_client_mem.h"

/*#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/aio.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
*/

/*#include "dbgLog.h" */

#include "ci_data_client.h"
#include "ci_data_client_tester.h"

/************************************************************************
 * External Interfaces
 ***********************************************************************/

extern void CiDataUplinkDataFree(void);
extern void CiDataTftToggleDebugMode(void);

extern CiData_UplinkPoolMemS gCiDataUplinkPoolMem;

/************************************************************************
 * Internal Interfaces
 ***********************************************************************/
static int ci_data_drv_probe(struct platform_device *pdev);
static int ci_data_drv_open(struct inode *inode, struct file *filp);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
static long ci_data_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
#else
static int ci_data_drv_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg);
#endif /*LINUX_VERSION */
static ssize_t ci_data_drv_read(struct file *filp, char *buf, size_t count, loff_t *f_pos);
static ssize_t ci_data_drv_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos);
static int ci_data_drv_release(struct inode *inode, struct file *filp);
static int ci_data_drv_remove(struct platform_device *pdev);

static struct file_operations ci_data_drv_fops = {
	.open = ci_data_drv_open,
	.read = ci_data_drv_read,
	.write = ci_data_drv_write,
	.release = ci_data_drv_release,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
	.unlocked_ioctl = ci_data_drv_ioctl,
#else
	.ioctl = ci_data_drv_ioctl,
#endif
	.owner = THIS_MODULE
};

static struct miscdevice ci_data_drv_miscdev = {
	MISC_DYNAMIC_MINOR,
	"ci_data_drv",
	&ci_data_drv_fops,
};

static struct platform_device ci_data_drv_device = {
	.name = "ci_data_drv_main",
	.id = 0,
};

static struct platform_driver ci_data_drv_driver = {
	.driver = {
		   .name = "ci_data_drv_main",
		   .owner = THIS_MODULE,
		   },
	.probe = ci_data_drv_probe,
	.remove = ci_data_drv_remove
};

CiData_PdpsStat gsPdpStat;

/************************************************************************
 * External Variables
 ***********************************************************************/
extern CiData_UplinkPoolMemS gCiDataUplinkPoolMem;
/*DBGLOG_EXTERN_VAR(ci_data); */

/************************************************************************
 * Internal Variables
 ***********************************************************************/

typedef struct {
	struct platform_device *pPlatformDevice;
	unsigned int openCounter;
} CIData_DriverParamsS;

static CIData_DriverParamsS gCiDataDriverParams;

/************************************************************************
 * Functions
 ***********************************************************************/

/***********************************************************************
 *
 * Name:		shmem_drv_ioctl
 *
 * Description:
 *
 * Parameters:
 * struct inode *   inode      - pointer to ...
 * struct file  *   file	   - pointer to ...
 * unsigned int 	cmd 	   - command
 * unsigned long	arg
 *
 * Returns:
 *
 * Notes:
 *
 ***********************************************************************/
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36))
static long ci_data_drv_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
#else
static int ci_data_drv_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
#endif
{

	/*if(_IOC_TYPE(cmd) != CI_DATA_IOCTL_IOC_MAGIC)
	   {
	   CI_DATA_DEBUG(0, "ci_data_drv_ioctl: ci_data_ioctl magic number is wrong!");
	   return -ENOTTY;
	   } */

	if (_IOC_TYPE(cmd) != CI_DATA_IOCTL_IOC_MAGIC) {
		if (cmd != 0x5401) {
			/*just ignore the TCGETS(0x5401) case of 'echo >' */
			CI_DATA_DEBUG(1, "ci_data_drv_ioctl: ci_data magic number is wrong! (cmd=%x, cmdmasked=%x)",
					  cmd, _IOC_TYPE(cmd));
		}
		return -ENOTTY;
	}

	CI_DATA_DEBUG(3, "ci_data_drv_ioctl: IOCTL received, cmd=%x", cmd);

	switch (cmd) {
		/*case CI_DATA_IOCTL_DATA_START:
		   printk("\nci_data_drv_ioctl: CI_DATA_IOCTL_DATA_START\n");
		   CiDataClientDataStart();/ * YG: need to check who need to call this (if any), like called by InitCCIDevStub in ci_client_api_shmem.c * /
		   break; */

	case CI_DATA_IOCTL_CID_ENABLE:
		{
			CiData_CidInfoS cidInfo;

			if (copy_from_user(&cidInfo, (CiData_CidInfoS *) arg, sizeof(CiData_CidInfoS))) {
				CI_DATA_DEBUG(0, "ci_data_drv_ioctl: CI_DATA_IOCTL_CID_ENABLE - ERROR copy");
				return -EFAULT;
			}

			CI_DATA_DEBUG(4, "ci_data_drv_ioctl: CI_DATA_IOCTL_CID_ENABLE (cid=%d, svcType=%d)",
					  cidInfo.cid, cidInfo.serviceType);

			CiDataClientCidEnable(&cidInfo, TRUE);
		}
		break;

	case CI_DATA_IOCTL_CID_DISABLE:
		CI_DATA_DEBUG(4, "ci_data_drv_ioctl: CI_DATA_IOCTL_CID_DISABLE (cid=%ld)", arg);

		CiDataTftResetCid(arg);
		CiDataClientCidDisable(arg);	/*arg = cid */
		break;

	case CI_DATA_IOCTL_TFT_HANDLE:
		{
			CiData_TFTCommandS tftCommand;

			CI_DATA_DEBUG(4, "ci_data_drv_ioctl: CI_DATA_IOCTL_TFT_HANDLE");
			if (copy_from_user(&tftCommand, (CiData_TFTCommandS *) arg, sizeof(CiData_TFTCommandS))) {
				CI_DATA_DEBUG(1, "ci_data_drv_ioctl: CI_DATA_IOCTL_TFT_HANDLE - ERROR copy");
				return -EFAULT;
			}

			CiDataTftHandleCommand(&tftCommand);
		}
		break;

	case CI_DATA_IOCTL_SET_CID_IPADDR:
		{
			CiData_CidIPInfoComandS ipInfoCommand;
			CI_DATA_DEBUG(2, "ci_data_drv_ioctl: CI_DATA_IOCTL_SET_CID_IPADDR");

			if (copy_from_user
				(&ipInfoCommand, (CiData_CidIPInfoComandS *) arg, sizeof(CiData_CidIPInfoComandS))) {
				CI_DATA_DEBUG(1, "ci_data_drv_ioctl: CI_DATA_IOCTL_SET_CID_IPADDR - ERROR copy");
				return -EFAULT;
			}

			CiDataClientCidSetIpAddress(&ipInfoCommand);

		}
		break;
	case CI_DATA_IOCTL_GET_CID_IPADDR:
		{
			CiData_CidIPInfoComandS ipInfoCommand;
			CI_DATA_DEBUG(2, "ci_data_drv_ioctl: CI_DATA_IOCTL_GET_CID_IPADDR");

			if (copy_from_user
				(&ipInfoCommand, (CiData_CidIPInfoComandS *) arg, sizeof(CiData_CidIPInfoComandS))) {
				CI_DATA_DEBUG(1, "ci_data_drv_ioctl: CI_DATA_IOCTL_GET_CID_IPADDR - ERROR copy");
				return -EFAULT;
			}

			CiDataClientCidGetIpAddress(&ipInfoCommand);

			if (copy_to_user((void *)arg, (void *)&ipInfoCommand, sizeof(CiData_CidIPInfoComandS)))
				return -EFAULT;

		}
		break;

	case CI_DATA_IOCTL_GCF_CMD:
		{
			CiData_GcfCmdS gcfCmd;
			CI_DATA_DEBUG(2, "ci_data_drv_ioctl: CI_DATA_IOCTL_GCF_CMD");
			if (copy_from_user(&gcfCmd, (CiData_GcfCmdS *) arg, sizeof(CiData_GcfCmdS))) {
				CI_DATA_DEBUG(1, "ci_data_drv_ioctl: CI_DATA_IOCTL_GCF_CMD - ERROR copy");
				return -EFAULT;
			}
			ci_data_uplink_data_send_gcf(gcfCmd.cid, gcfCmd.bytesToSend);
		}
		break;
	case CI_DATA_IOCTL_ENABLE_LOOPBACK_B:
		{
			CI_DATA_DEBUG(2, "ci_data_drv_ioctl: CI_DATA_IOCTL_ENABLE_LOOPBACK_B");
			CiDataLoopBackModeBEnable((CiData_LoopBackActivationS *) arg);
			break;
		}

	case CI_DATA_IOCTL_DISABLE_LOOPBACK_B:
		{
			CI_DATA_DEBUG(2, "ci_data_drv_ioctl: CI_DATA_IOCTL_DISABLE_LOOPBACK_B");
			CiDataLoopBackModeBDisable();
			break;
		}
	case CI_DATA_IOCTL_TEST_MODE_ACTIVATION:
		{
			CI_DATA_DEBUG(2, "ci_data_drv_ioctl: CI_DATA_IOCTL_TEST_MODE_ACTIVATION");
			CiDataLoopBackTestActivation();
			break;
		}
	case CI_DATA_IOCTL_TEST_MODE_TERMINATION:
		{
			CiDataLoopBackTestTermination();
			CI_DATA_DEBUG(2, "ci_data_drv_ioctl: CI_DATA_TEST_MODE_TERMINATION");
			break;
		}
	case CI_DATA_IOCTL_CONFIG_GLOBALS:
		{
			if (copy_from_user
				(&gCiDataClient.mtil_globals_cfg, (CiData_GlobalConfigS *) arg,
				 sizeof(CiData_GlobalConfigS))) {
				CI_DATA_DEBUG(1, "ci_data_drv_ioctl: CI_DATA_IOCTL_CONFIG_GLOBALS - ERROR copy");
				return -EFAULT;
			}
			CI_DATA_DEBUG(3, "ci_data_drv_ioctl: CI_DATA_IOCTL_CONFIG_GLOBALS");
			break;
		}

	case CI_DATA_IOCTL_CID_SET_PRIORITY:
		{
			CiData_CidInfoS cidInfo;

			if (copy_from_user(&cidInfo, (CiData_CidInfoS *) arg, sizeof(CiData_CidInfoS))) {
				CI_DATA_DEBUG(0, "ci_data_drv_ioctl: CI_DATA_IOCTL_CID_SET_PRIORITY - ERROR copy");
				return -EFAULT;
			}

			CI_DATA_DEBUG(4, "ci_data_drv_ioctl: CI_DATA_IOCTL_CID_SET_PRIORITY (cid=%d, priority=%d)",
					  cidInfo.cid, cidInfo.priority);

			CiDataClientCidSetPriority(&cidInfo);
			break;
		}
	case CI_DATA_IOCTL_GET_PDP_STATS:
		{

			memset(&gsPdpStat, 0, sizeof(gsPdpStat));

			if (copy_from_user(&gsPdpStat.cid, &((CiData_PdpsStat *) arg)->cid, sizeof(gsPdpStat.cid))) {
				CI_DATA_DEBUG(0, "ci_data_drv_ioctl: CI_DATA_IOCTL_GET_PDP_STATS - ERROR copy");
				return -EFAULT;
			}
			/*copy statistics */
			CiDataClientCidGetStat(&gsPdpStat);

			if (copy_to_user((void *)arg, (void *)&gsPdpStat, sizeof(CiData_PdpsStat)))
				return -EFAULT;

			break;
		}

	default:
		CI_DATA_DEBUG(0, "ci_data_drv_ioctl: WARNING - command not supported (cmd=%x)", cmd);
		/*CI_DATA_DEBUG(0, "ci_data_drv_ioctl: WARNING - STARTING ANYWAY - NEED TO BE FIXED!!!!");
		   CiDataClientDataStart();/ *YG: need to check who need to call this (if any), like called by InitCCIDevStub in ci_client_api_shmem.c * /
		 */
		break;
	}

	return 0;
}

/***********************************************************************
 *
 * Name:		ci_data_drv_read()
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
static ssize_t ci_data_drv_read(struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	/*YG: add statistics */
	/*CiDataPrintStatistic(); */

	return 0;
}

/***********************************************************************
 *
 * Name:		ci_data_drv_print_help()
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
void ci_data_drv_print_help(void)
{
	printk("CI-DATA Driver Available Options:\n");
	printk("-------------------------------------------\n\n");
	printk("-h  				   : Print this [H]elp\n");
	printk("-a  				   : Print [A]ll info\n");
	printk("-b  				   : Print [B]asic CID info\n");
	printk("-d  				   : Dump ci_data_drv internal [D]atabase\n");
	printk("-s  				   : Print ci_data_drv [S]tatistics\n");
	printk("-0  				   : Reset Statistics (set all to zero)\n");
	printk("-c[type] [params]      : [C]ontrol ci_data_drv functionality - depends on type\n");
	printk("	-cd 			   : control [d]ata disable/enable\n");
	printk("	-cp#			   : control multi-[p]acket limit\n");
	printk
		("-t[operation] [params] : Run [T]ests of the  ci_data_drv - [operation]=operation number, [params]=optional\n");
	printk("-l[type]			   : print Debug [L]og (type=t only supported)\n");
	printk("-f  				   : Toggle T[F]T Logging\n");
	printk("-k  				   : [K]ick RNDIS for re-alloc - for debug\n");
	printk("-u  				   : Free [U]plink packets - if possible - for debug\n");
	printk("-z[num] 			   : set ack drop threash for pkt [Z]bb\n");
	printk("-zz 				   : Enable/Disable add ignore\n");

	/*printk ("-i xxx.xxx.xxx.xxx    : Set [I]P address for LTM\n"); */
	/*printk ("-l   		  : Dump shmem kernel debug [L]og to terminal\n");
	   printk ("-k  		   : Dump shmem all [K]ernel debug & info to terminal\n");
	   printk ("-u  		   : Dump shmem [U]ser-only info to terminal\n");
	   printk ("-s  		   : Dump all [S]hmem-only debug info above (kernel+user in order)!!!\n");
	   printk ("-c  		   : Dump [C]I data-channel-kernel debug to terminal\n");
	   printk ("-a  		   : Dump [A]ll debug info above (in order)!!!\n"); */
}

/***********************************************************************
 *
 * Name:		ci_data_drv_write()
 *
 * Description: parse the "echo -x>/dev/ci_data_drv" commands
 *
 * Parameters:
 *
 * Returns:
 *
 * Notes:
 *
 ***********************************************************************/
static ssize_t ci_data_drv_write(struct file *filp, const char *buf, size_t count, loff_t *f_pos)
{
	size_t index = 0;
	unsigned int len = 0;

	while (index < count) {
		if (buf[index++] == '-') {
			switch (buf[index]) {
			case 'H':
			case 'h':
				ci_data_drv_print_help();
				break;

			case 'A':
			case 'a':
				CiDataPrintDatabase(TRUE);
				CiDataPrintStatistics(TRUE);
				index++;
				break;

			case 'B':
			case 'b':
				index++;
				CiDataPrintBasicCidInfo();
				break;

			case 'D':
			case 'd':
				CiDataPrintDatabase(FALSE);
				index++;
				break;

			case 'S':
			case 's':
				CiDataPrintStatistics(FALSE);
				index++;
				break;

			case 'C':
			case 'c':
				index++;
				CiDataHandleControlCommand(buf[index], (char *)(buf + index + 1));
				index++;
				break;

			case 'Z':
			case 'z':
				index++;
				CiDataUplinkSetAckDropThresh(buf[index]);
				index++;
				break;

			case 'T':
			case 't':
				index++;
				CiDataTesterRunCommand(buf[index], (char *)(buf + index + 1));
				index++;
				break;

			case 'I':
			case 'i':   /*set IP address for LTM */
				index++;
				len = CiDataClientLtmSetIpAddress((char *)(buf + index));
				index++;
				break;

			case 'L':
			case 'l':   /*Save debug log - next parameter can be 'b' for binary or 't' for text */
				index++;
				CiDataControlLogging(buf, &index);
				break;

			case '0':
				index++;
				printk("\nci_data_drv: resetting all statistics\n");
				memset(&gCiDataDebugStatistics, 0, sizeof(gCiDataDebugStatistics));
				gCiDataUplinkPoolMem.maxNumAllocs = 0;
				gCiDataUplinkPoolMem.numAllocWasNull = 0;
				gCiDataUplinkPoolMem.flowOffCount = 0;
				gCiDataUplinkPoolMem.flowOnCount = 0;
				gCiDataUplinkPoolMem.flowOffMaxTime = 0;
				gCiDataUplinkPoolMem.flowOffSumTime = 0;
				gCiDataUplinkPoolMem.flowOffTimeStampBefore = 0;
				gCiDataUplinkPoolMem.flowOffTimeStampAfter = 0;
				break;

			case 'F':
			case 'f':
				index++;
				CiDataTftToggleDebugMode();
				break;

			case 'U':
			case 'u':
				CiDataUplinkDataFree();
				break;

			default:
				index++;
				printk("\nci_data_drv: Unsupported command -%c\n", buf[index]);
				ci_data_drv_print_help();
				break;
			}
		}
	}

	return index;
}

/***********************************************************************
 *
 * Name:		shmem_drv_release()
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
static int ci_data_drv_release(struct inode *inode, struct file *filp)
{
	/* Just release the resource when access count because 0 */
	/*CI_DATA_DEBUG(3, "ci_data_drv_release: release the driver resource, (openCount=%d)", gCiDataDriverParams.openCounter); */

	if ((--gCiDataDriverParams.openCounter) == 0) { /*last release */
		/*YG: need to unregister from shmem and release allocations */
		/*CI_DATA_DEBUG(1, "ci_data_drv_release: last release called!!! need to release everything"); */
	}

	return 0;
}

/***********************************************************************
 *
 * Name:		shmem_drv_open()
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
static int ci_data_drv_open(struct inode *inode, struct file *filp)
{
	CI_DATA_DEBUG(5, "ci_data_drv_open: open called (openCounter=%d)", gCiDataDriverParams.openCounter);

	if (gCiDataDriverParams.openCounter == 0) { /* first open */
		/*nothing to do for now... */
	}

	/* other opens */
	gCiDataDriverParams.openCounter++;

	return 0;
}

/***********************************************************************
 *
 * Name:		ci_data_drv_probe()
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
static int ci_data_drv_probe(struct platform_device *pdev)
{
	int ret = 0;

	/* Register the char device */
	ret = misc_register(&ci_data_drv_miscdev);
	if (ret) {
		CI_DATA_DEBUG(0, "ci_data_drv_probe: cannot register char device, ret=%d\n", ret);

		return ret;
	}
	/*init globals */
	gCiDataDriverParams.pPlatformDevice = pdev;
	gCiDataDriverParams.openCounter = 0;

	CiDataClientInit();

	CI_DATA_DEBUG(2, "ci_data_drv_probe: finished successful!\n");

	return ret;
}

/***********************************************************************
 *
 * Name:		ci_data_drv_remove()
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
static int ci_data_drv_remove(struct platform_device *pdev)
{
	CI_DATA_DEBUG(0, "ci_data_drv_remove called, deregistering misc-device\n");
	misc_deregister(&ci_data_drv_miscdev);
	return 0;
}

/***********************************************************************
 *
 * Name:		ci_data_drv_exit()
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
static void __exit ci_data_drv_exit(void)
{
	CI_DATA_DEBUG(0, "ci_data_drv_exit called, deregistering driver\n");
	platform_driver_unregister(&ci_data_drv_driver);
}

/***********************************************************************
 *
 * Name:		ci_data_drv_init
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
static int __init ci_data_drv_init(void)
{
	int ret;

	ret = platform_device_register(&ci_data_drv_device);
	if (ret) {
		CI_DATA_DEBUG(0, "Cannot register CI-DATA platform device, ret=%d", ret);

		return ret;
	}

	CI_DATA_DEBUG(1, "ci_data_drv_init register device OK");

	ret = platform_driver_register(&ci_data_drv_driver);
	if (ret) {
		CI_DATA_DEBUG(0, "Cannot register CI-DATA platform driver, ret=%d", ret);
		platform_device_unregister(&ci_data_drv_device);

		return ret;
	}

	CI_DATA_DEBUG(1, "ci_data_drv_init register driver OK");

	return ret;
}

/******************************************************************************/
/********************** Module Export Symbols  ********************************/
/******************************************************************************/

module_init(ci_data_drv_init);
module_exit(ci_data_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marvell");
MODULE_DESCRIPTION("Marvell CI-DATA driver");
/******************************************************************************/
