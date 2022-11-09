#
# Makefile for mmp specific clk
#

obj-y += clk-apbc.o clk-apmu.o clk-frac.o

obj-$(CONFIG_CPU_PXA168) += clk-pxa168.o
obj-$(CONFIG_CPU_PXA910) += clk-pxa910.o
obj-$(CONFIG_CPU_MMP2) += clk-mmp2.o
obj-$(CONFIG_CPU_PXA988) += clk-pxa988.o clk-core.o clk-pll.o clk-peri.o clk-disp.o clk-common.o clk-pxa1U88.o \
clk-audio-map.o simple_dip.o
obj-y += clk-pxa1U88.o clk-core.o clk-pll.o clk-peri.o clk-disp.o clk-common.o \
clk-audio-map.o simple_dip.o
obj-y += clk-pxa1928.o clk-pll-pxa1928.o clk-disp.o clk-peri.o clk-core-pxa1928.o \
clk-audio-pxa1928.o clk-audio-map.o
obj-$(CONFIG_CPU_PXA1986) += clk-disp.o adir-clk.o adir-clk-src.o adir-clk-disp.o adir-clk-peri.o adir-clk-audio.o

ifeq ($(CONFIG_PXA_DVFS), y)
obj-y += dvfs.o clk-dvfs-dummy.o
obj-$(CONFIG_CPU_PXA988) += dvfs-dvc.o dvfs-pxa1088.o dvfs-pxa1L88.o dvfs-pxa1u88.o hwdvc-stat.o
obj-y += dvfs-pxa1928.o hwdvc-stat.o
endif

obj-$(CONFIG_DEBUG_FS) += clk-dcstat.o
