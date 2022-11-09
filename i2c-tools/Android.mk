LOCAL_PATH := $(my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := i2cdetect
LOCAL_MODULE_TAGS := debug
LOCAL_SRC_FILES := tools/i2cdetect.c tools/i2cbusses.c tools/i2cbusses.h
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := i2cdump
LOCAL_MODULE_TAGS := debug
LOCAL_SRC_FILES := tools/i2cdump.c tools/i2cbusses.c tools/i2cbusses.h tools/util.c tools/util.h
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := i2cset
LOCAL_MODULE_TAGS := debug
LOCAL_SRC_FILES := tools/i2cset.c tools/i2cbusses.c tools/i2cbusses.h tools/util.c tools/util.h
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
include $(BUILD_EXECUTABLE)

include $(CLEAR_VARS)
LOCAL_MODULE := i2cget
LOCAL_MODULE_TAGS := debug
LOCAL_SRC_FILES := tools/i2cget.c tools/i2cbusses.c tools/i2cubsses.h tools/util.c tools/util.h
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
include $(BUILD_EXECUTABLE)

