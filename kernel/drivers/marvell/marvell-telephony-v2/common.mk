PXA_SRC_DIR := $(srctree)/drivers/marvell/marvell-telephony-v2

PXA_ROOT_DIR := $(PXA_SRC_DIR)

FLAVOR_DIAG_MSA_MANAGE := Yes

#Project source
PXA_SRC_PVK_LNX_DIR := $(abspath $(ANDROID_BUILD_TOP)/kernel/)
PXA_SRC_PCAC := $(PXA_SRC_DIR)/pcac

TEL_COMMONCFLAGS := -DLINUX \
		-DENV_LINUX \
		-D__linux__ \
		-DBIONIC \
		-D_POSIX_C_SOURCE \
		-DOSA_LINUX \
		-DOSA_USE_ASSERT \
		-DOSA_MSG_QUEUE \
		-DOSA_QUEUE_NAMES \
		-D_FDI_USE_OSA_ \
		-DUSE_OSA_SEMA \
		-DCI_STUB_CLIENT_INCLUDE \
		-DTAVOR_AUDIO \
		-DCCI_POSITION_LOCATION \
		-DFLAVOR_EEH_NODIAG

SHMEM_FLAGS = \
	-DDEBUG_BUILD \
	-DACI_KERNEL_DEBUG

#Enable this to use the "SHMEM Remove-Copy optimization"
#NOTE: this must be align with the SAC+PS+CI Server on COMM, and CI Client+data_channel_kernel_shmem (part of acipcd kernel) on APPS
SHMEM_FLAGS += -DCCI_OPT_SAC_USE_SHMEM_DIRECT

# Enable this use NEW watermark handling HighLow Callback mechanism instead of function return value
SHMEM_FLAGS += -DACIPC_ENABLE_NEW_CALLBACK_MECHANISM

TEL_COMMONCFLAGS += $(SHMEM_FLAGS)
