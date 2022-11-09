LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        SoftMP12.cpp

LOCAL_C_INCLUDES := \
        frameworks/av/media/libstagefright/include \
        frameworks/native/include/media/openmax \
        $(LOCAL_PATH)/../mpg123-1.14.4/src/libmpg123/ \

LOCAL_SHARED_LIBRARIES := \
        libmpg123_mrvl \
        libstagefright libstagefright_omx libstagefright_foundation libutils liblog

LOCAL_MODULE := libstagefright_soft_mrvl_mpg123dec
LOCAL_32_BIT_ONLY := true
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
