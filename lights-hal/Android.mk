LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := lights.c

LOCAL_SHARED_LIBRARIES := \
    libutils \
    libcutils \
    libhardware\

$(info BOARD_LIGHTS_BUTTONS=$(BOARD_LIGHTS_BUTTONS))
ifeq ($(BOARD_LIGHTS_BUTTONS),buttons_via_lcd)
LOCAL_CFLAGS += -DBUTTONS_VIA_LCD
endif

# do not prelink
LOCAL_PRELINK_MODULE := false

LOCAL_MODULE := lights.mrvl

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
