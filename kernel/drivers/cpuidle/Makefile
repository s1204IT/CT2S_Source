#
# Makefile for cpuidle.
#

obj-y += cpuidle.o driver.o governor.o sysfs.o governors/
obj-$(CONFIG_ARCH_NEEDS_CPU_IDLE_COUPLED) += coupled.o

obj-$(CONFIG_CPU_IDLE_CALXEDA)	+= cpuidle-calxeda.o
obj-$(CONFIG_ARCH_KIRKWOOD)	+= cpuidle-kirkwood.o
obj-$(CONFIG_ARM64_CPUIDLE)	+= cpuidle-arm64.o
obj-$(CONFIG_CPU_IDLE_MMP_V7)	+= cpuidle-mmp-armv7.o
obj-$(CONFIG_CPU_IDLE_MMP_V8)	+= cpuidle-mmp-armv8.o
