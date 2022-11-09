LOCAL_PATH := $(call my-dir)

MPG123_TOP := $(LOCAL_PATH)

include $(CLEAR_VARS)

include $(MPG123_TOP)/mpg123-1.14.4/Android.mk
include $(MPG123_TOP)/google-omx-mpg123-mrvl/Android.mk
