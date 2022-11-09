#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License version 2 as
#  published by the Free Software Foundation.

# (C) Copyright 2012 Marvell International Ltd.
# All Rights Reserved
#
include $(srctree)/drivers/marvell/marvell-telephony-v2/common.mk

pcac := $(PXA_SRC_PCAC)
ci_dir := $(PXA_SRC_CI)

TARGET := ci_data_drv
all: modules

ifeq ($(KERNELRELEASE),)
include $(MAKERULES)
extra_mod_symbols := $(PXA_ROOT_DIR)/errorhandler/build/Module.symvers \
          $(pcac)/shmem/acipc_kernel/Module.symvers

modules:
	$(ML_HIDE)$(shell_create_links)
	$(MAKE) -C $(PXA_SRC_PVK_LNX_DIR) ARCH=arm CROSS_COMPILE="$(MODULES_CROSS_COMPILE)" M=$(shell pwd) \
		KBUILD_EXTRA_SYMBOLS='$(extra_mod_symbols)' $(ML_KHIDE) modules || \
		(rm $(shel_del_links) && exit 1)
	$(ML_HIDE)rm $(shel_del_links)

install:
	$(call copy_elf,$(TARGET).ko,tel)

clean:
	$(call temp_files_clean, $(shel_del_links))
else

ext_includes := \
	-I$(PXA_ROOT_DIR)/include\
	-I$(pcac)/inc\
	-I$(pcac)/shmem/inc\
	-I$(pcac)/pca_components/inc\
	-I$(ci_dir)/inc\
	-I$(pcac)/ci_stub/src\
	-I$(pcac)/ci_data/inc\
	-I$(pcac)/dbgLog/\

objects_local = \
	./src/ci_data_client.o \
	./src/ci_data_client_api.o \
	./src/ci_data_client_ltm.o \
	./src/ci_data_client_shmem.o \
	./src/ci_data_client_handler.o \
	./src/ci_data_client_tester.o \
	./src/ci_data_kmod.o \
	./src/ci_data_client_loopback.o\
	./src/ci_data_client_mem.o \
	../dbgLog/dbgLogKernel.o\
	../dbgLog/dbgLogLnxKernel.o

ifneq ($(IS_ESHEL_PLATFORM),"Yes")
objects_local += \
	./src/itm.o
endif


$(TARGET)-objs := $(objects_local)

EXTRA_CFLAGS += \
	-I$(PXA_ROOT_DIR)/include \
	-I$(pcac)/shmem/acipc_kernel/inc\
	-I$(PXA_ROOT_DIR)/errorhandler/inc/\
	$(ext_includes)

EXTRA_CFLAGS += \
	-DACI_LNX_KERNEL \
	-DENV_LINUX \
	-DOSA_LINUX \
	-DOSA_USE_ASSERT \
	-DFLAVOR_APP

#	-DFEATURE_SHMEM\
#	-DDEBUG_BUILD\
#	-DENV_LINUX \
#	-DDATA_STUB_IN_KERNEL \
#	-DACI_USE_SHMEM

# Enable this use NEW watermark handling HighLow Callback mechanism instead of function return value
EXTRA_CFLAGS += -DACIPC_ENABLE_NEW_CALLBACK_MECHANISM

# Enable this to use cached descriptors
#EXTRA_CFLAGS += -DCI_DATA_FEATURE_DESCRIPTORS_CACHED

#EXTRA_CFLAGS += -Werror

obj-m := $(TARGET).o
CC = $(CROSS_COMPILE)gcc
endif






