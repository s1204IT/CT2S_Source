# Makefile for MARVELL MMP ISP driver

obj-$(CONFIG_VIDEO_MMPISP_SENSOR) += mvisp_sensor.o
obj-$(CONFIG_VIDEO_MMPISP_B52SENSOR) += b52-sensor.o
obj-$(CONFIG_VIDEO_MMPISP_DW9714L) += dw9714l.o
obj-$(CONFIG_HOST_SUBDEV)	+= host_subdev.o
obj-$(CONFIG_VIDEO_MMPISP_AREA51) += socisp-pdev.o socisp-mdev.o socisp-vdev.o

# TODO: Add Camera/ISP IP driver below
obj-$(CONFIG_VIDEO_MVISP_FLASH)		+= mvisp_flash.o
obj-$(CONFIG_VIDEO_MMPISP_AREA51) += ispdma.o ispccic.o isp.o

obj-$(CONFIG_VIDEO_MMPISP_B52) += b52-reg.o b52isp-ctrl.o b52isp.o

obj-$(CONFIG_VIDEO_MMPISP_CCICV2) += ccicv2.o

# TODO: Add platform Camera/ISP driver below

obj-$(CONFIG_VIDEO_MMPISP_PLATFORM) += plat_cam.o

obj-$(CONFIG_VIDEO_MMPISP_B52) += b52socisp/
