LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES += $(LOCAL_PATH)/src  $(LOCAL_PATH)/src/libmpg123

LOCAL_SRC_FILES := \
    src/libmpg123/compat.c           \
    src/libmpg123/dct64.c            \
    src/libmpg123/equalizer.c        \
    src/libmpg123/feature.c          \
    src/libmpg123/format.c           \
    src/libmpg123/frame.c            \
    src/libmpg123/icy.c              \
    src/libmpg123/icy2utf8.c         \
    src/libmpg123/id3.c              \
    src/libmpg123/index.c            \
    src/libmpg123/layer1.c           \
    src/libmpg123/layer2.c           \
    src/libmpg123/layer3.c           \
    src/libmpg123/libmpg123.c        \
    src/libmpg123/ntom.c             \
    src/libmpg123/optimize.c         \
    src/libmpg123/parse.c            \
    src/libmpg123/readers.c          \
    src/libmpg123/stringbuf.c        \
    src/libmpg123/synth.c            \
    src/libmpg123/tabinit.c          \


OPT_TYPE :=neon
#neon, arm 


ifeq ($(OPT_TYPE),neon)

LOCAL_CFLAGS += -DOPT_NEON -DREAL_IS_FLOAT

ASM_FILES := \
    src/libmpg123/dct64_neon.S \
    src/libmpg123/dct64_neon_float.S \
    src/libmpg123/synth_neon.S \
    src/libmpg123/synth_neon_accurate.S \
    src/libmpg123/synth_stereo_neon.S \
    src/libmpg123/synth_stereo_neon_float.S \
    src/libmpg123/synth_stereo_neon_accurate.S \

LOCAL_SRC_FILES += $(ASM_FILES)

endif 

ifeq ($(OPT_TYPE),arm)

LOCAL_CFLAGS += -DOPT_ARM -DREAL_IS_FIXED

ASM_FILES := \
    src/libmpg123/synth_arm_accurate.S \
    src/libmpg123/synth_arm.S

LOCAL_SRC_FILES += $(ASM_FILES)

endif

ifeq ($(OPT_TYPE),generic)

LOCAL_CFLAGS += -DOPT_GENERIC -DREAL_IS_FIXED

endif    
LOCAL_ARM_MODE := arm

LOCAL_MODULE := libmpg123_mrvl
LOCAL_32_BIT_ONLY := true
LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
