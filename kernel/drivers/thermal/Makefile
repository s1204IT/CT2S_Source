#
# Makefile for sensor chip drivers.
#

obj-$(CONFIG_THERMAL)		+= thermal_sys.o
thermal_sys-y			+= thermal_core.o

# governors
thermal_sys-$(CONFIG_THERMAL_GOV_FAIR_SHARE)	+= fair_share.o
thermal_sys-$(CONFIG_THERMAL_GOV_STEP_WISE)	+= step_wise.o
thermal_sys-$(CONFIG_THERMAL_GOV_USER_SPACE)	+= user_space.o
thermal_sys-$(CONFIG_THERMAL_GOV_BI_DIRECTION)	+= bi_direction.o

# cpufreq cooling
thermal_sys-$(CONFIG_CPU_THERMAL)	+= cpu_cooling.o
# mrvl cooling devices
thermal_sys-$(CONFIG_COOLING_DEV_MRVL)	+= cooling_dev_mrvl.o

# platform thermal drivers
obj-$(CONFIG_SPEAR_THERMAL)	+= spear_thermal.o
obj-$(CONFIG_RCAR_THERMAL)	+= rcar_thermal.o
obj-$(CONFIG_KIRKWOOD_THERMAL)  += kirkwood_thermal.o
obj-$(CONFIG_EXYNOS_THERMAL)	+= exynos_thermal.o
obj-$(CONFIG_PXA_THERMAL)	+= pxa_thermal.o
obj-$(CONFIG_PXA1088_THERMAL)	+= pxa1088_thermal.o
obj-$(CONFIG_HELAN2_THERMAL)	+= helan2_thermal.o
obj-$(CONFIG_DOVE_THERMAL)  	+= dove_thermal.o
obj-$(CONFIG_DB8500_THERMAL)	+= db8500_thermal.o
obj-$(CONFIG_ARMADA_THERMAL)	+= armada_thermal.o
obj-$(CONFIG_DB8500_CPUFREQ_COOLING)	+= db8500_cpufreq_cooling.o
obj-$(CONFIG_INTEL_POWERCLAMP)	+= intel_powerclamp.o
obj-$(CONFIG_PXA1928_THERMAL)		+= pxa1928_thermal.o
obj-$(CONFIG_PXA1986_THERMAL)		+= pxa1986_thermal.o
obj-$(CONFIG_GPADC_THERMAL)	+= gpadc_thermal.o
