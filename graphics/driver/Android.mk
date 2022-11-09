.PHONY: build-galcore

ARCH ?= arm64
ARCH_TYPE ?= arm64

ifneq ($(ARCH),)
       ARCH := $(ARCH)
       ARCH_TYPE := $(ARCH)
       BUILD_PARAMETERS := ARCH=${ARCH} ARCH_TYPE=${ARCH_TYPE} CROSS_COMPILE=${CROSS_COMPILE} -j$(MAKE_JOBS)
endif

ifeq ($(ARCH), arm64)
       CROSS_COMPILE := aarch64-linux-android-
       BUILD_PARAMETERS := -j$(MAKE_JOBS)
endif

$(PRODUCT_OUT)/ramdisk.img: galcore.ko

include $(CLEAR_VARS)
GALCORE_SRC_PATH := $(ANDROID_BUILD_TOP)/vendor/marvell/generic/graphics/driver
LOCAL_PATH := $(GALCORE_SRC_PATH)
LOCAL_SRC_FILES := galcore.ko
LOCAL_MODULE := $(LOCAL_SRC_FILES)
LOCAL_MODULE_CLASS := EXECUTABLES
LOCAL_MODULE_PATH := $(PRODUCT_OUT)/system/lib/modules
$(LOCAL_PATH)/$(LOCAL_SRC_FILES): build-galcore
include $(BUILD_PREBUILT)

build-galcore: uImage
	cd $(GALCORE_SRC_PATH) &&\
	$(MAKE) $(BUILD_PARAMETERS)
ifeq (,$(wildcard $(PRODUCT_OUT)/system/lib/modules))
	mkdir -p $(PRODUCT_OUT)/system/lib/modules
endif
	cp $(GALCORE_SRC_PATH)/galcore.ko $(PRODUCT_OUT)/system/lib/modules


.PHONY: build-debug-galcore
build-debug-galcore: $(PRODUCT_OUT)/$(KERNEL_IMAGE)_debug
	cd $(GALCORE_SRC_PATH) &&\
	$(MAKE) $(BUILD_PARAMETERS)
ifeq (,$(wildcard $(PRODUCT_OUT)/system/lib/modules))
	mkdir -p $(PRODUCT_OUT)/system/lib/modules
endif
	cp $(GALCORE_SRC_PATH)/galcore.ko $(PRODUCT_OUT)/system/lib/modules
