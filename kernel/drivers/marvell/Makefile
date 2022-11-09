#
# Makefile for the drivers of Marvell devices.
#
ifeq ($(CONFIG_64BIT), y)
        obj-y                           += security/mmp-tzdd/src/
else
ifdef CONFIG_TZ_HYPERVISOR
        obj-y                           += security/mmp-tzdd/src/
else
        obj-y                           += security/mmp-geu/
endif
endif
#obj-$(CONFIG_MRVL_WL_SD8XXX) += sd8887/ sd8777/ sd8787/ sd8897/
obj-$(CONFIG_MRVL_WL_SD8XXX) += sd8777/ sd8787/ sd8887/
#obj-$(CONFIG_MRVL_WL_SD8897) += sd8897/
obj-$(CONFIG_MRVL_MMP_MODEM)	+= marvell-telephony/
obj-$(CONFIG_MRVL_MMP_MODEM_V2) += marvell-telephony-v2/
obj-$(CONFIG_MRVL_SKMALLOC) += kmalloc/
obj-$(CONFIG_MRVL_SW_JTAG) += sw_jtag/
obj-$(CONFIG_MRVL_PANIC_FLUSH) += emmd/
obj-y += debug/
obj-$(CONFIG_MRVL_TDM) += tdm-daemon/
obj-y += rsv_emmc/
