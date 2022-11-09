LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    mpg123Test.c \

#LOCAL_SRC_FILES += mpg123_to_wav.c

LOCAL_MODULE_TAGS := debug

LOCAL_MODULE := MPG123Test
LOCAL_32_BIT_ONLY := true
LOCAL_ARM_MODE := arm

LOCAL_SHARED_LIBRARIES := \
    libstagefright \
    libmpg123_mrvl \
    libdl

LOCAL_C_INCLUDES := \
vendor/marvell/generic/mpg123-mrvl/mpg123-1.14.4/src/libmpg123 \

LOCAL_MODULE_PATH :=$(LOCAL_PATH)

include $(BUILD_EXECUTABLE)
