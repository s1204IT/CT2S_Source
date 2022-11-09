#
# Makefile for Marvell's PXA168 processors line
#

obj-y				+= common.o devices.o

# SoC support
obj-$(CONFIG_CPU_PXA168)	+= pxa168.o
obj-$(CONFIG_CPU_PXA910)	+= pxa910.o
obj-$(CONFIG_CPU_MMP2)		+= mmp2.o sram.o
obj-$(CONFIG_CPU_PXA988)        += platsmp.o reset.o pxa988_lowpower.o mmp_cpuidle.o v7_pm_setup.o power_domain_gc.o power_domain_isp.o restart.o power_domain_vpu.o
obj-$(CONFIG_CPU_PXA1986)       += platsmp_pxa1986.o headsmp_pxa1986.o reset_pxa1986.o power_domain_gc_adir.o
obj-$(CONFIG_CPU_PXA1928)          += platsmp_pxa1928.o pxa1928_lowpower.o pxa1928_ddrtable.o v7_pm_setup.o headsmp.o reset_pxa1928.o power_domain_pxa1928_gc.o mmp_cpuidle.o

ifeq ($(CONFIG_PXA_DVFS),y)
obj-$(CONFIG_CPU_PXA1928)		+= dvfs-pxa1928.o
endif

obj-$(CONFIG_TZ_HYPERVISOR)     += tzlc/

ifeq ($(CONFIG_SUSPEND),y)
obj-$(CONFIG_CPU_PXA988)        += pm.o pm-pxa1L88.o
obj-$(CONFIG_CPU_PXA1928)		+= pm.o pm-pxa1928.o
endif

ifeq ($(CONFIG_COMMON_CLK), )
obj-y				+= clock.o
obj-$(CONFIG_CPU_PXA168)	+= clock-pxa168.o
obj-$(CONFIG_CPU_PXA910)	+= clock-pxa910.o
obj-$(CONFIG_CPU_MMP2)		+= clock-mmp2.o
endif
ifeq ($(CONFIG_PM),y)
obj-$(CONFIG_CPU_PXA910)	+= pm-pxa910.o
obj-$(CONFIG_CPU_MMP2)		+= pm-mmp2.o
endif

# coresight support
obj-$(CONFIG_CORESIGHT_SUPPORT) += coresight-v7.o

# board support
obj-$(CONFIG_MACH_ASPENITE)	+= aspenite.o
obj-$(CONFIG_MACH_ZYLONITE2)	+= aspenite.o
obj-$(CONFIG_MACH_AVENGERS_LITE)+= avengers_lite.o
obj-$(CONFIG_MACH_TAVOREVB)	+= tavorevb.o
obj-$(CONFIG_MACH_TTC_DKB)	+= ttc_dkb.o
obj-$(CONFIG_MACH_BROWNSTONE)	+= brownstone.o
obj-$(CONFIG_MACH_FLINT)	+= flint.o
obj-$(CONFIG_MACH_MARVELL_JASPER) += jasper.o
obj-$(CONFIG_MACH_MMP_DT)	+= mmp-dt.o
obj-$(CONFIG_MACH_MMP2_DT)	+= mmp2-dt.o
obj-$(CONFIG_MACH_MMPX_DT)	+= mmpx-dt.o
obj-$(CONFIG_MACH_PXA1928_DT)	+= pxa1928-dt.o
obj-$(CONFIG_MACH_ADIR_DT)	+= adir-dt.o
obj-$(CONFIG_MACH_TETON_BGA)	+= teton_bga.o
obj-$(CONFIG_MACH_GPLUGD)	+= gplugd.o
obj-$(CONFIG_GPU_RESERVE_MEM)	+= gpu_mem.o

# soc camera support
obj-$(CONFIG_MACH_SOC_CAMERA_DKB)	+= soc_camera_dkb.o
obj-$(CONFIG_MACH_SOC_CAMERA_FF)	+= soc_camera_ff.o
