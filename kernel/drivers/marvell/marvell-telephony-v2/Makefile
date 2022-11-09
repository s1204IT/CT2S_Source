subdir-ccflags-y += -Idrivers/marvell/marvell-telephony-v2/include
subdir-ccflags-y += -DEXPORT_SYMTAB

TEL_COMMONCFLAGS := -DLINUX \
		-DENV_LINUX \
		-D__linux__ \
		-D_POSIX_C_SOURCE \
		-DOSA_LINUX \
		-DOSA_MSG_QUEUE \
		-DOSA_QUEUE_NAMES \
		-D_FDI_USE_OSA_ \
		-DUSE_OSA_SEMA \
		-DCI_STUB_CLIENT_INCLUDE \
		-DCCI_POSITION_LOCATION
subdir-ccflags-y += $(TEL_COMMONCFLAGS)
SHMEM_FLAGS = \
	-DDEBUG_BUILD \
	-DACI_KERNEL_DEBUG

#Enable this to use the "SHMEM Remove-Copy optimization"
#NOTE: this must be align with the SAC+PS+CI Server on COMM, and CI Client+data_channel_kernel_shmem (part of acipcd kernel) on APPS
SHMEM_FLAGS += -DCCI_OPT_SAC_USE_SHMEM_DIRECT

# Enable this use NEW watermark handling HighLow Callback mechanism instead of function return value
SHMEM_FLAGS += -DACIPC_ENABLE_NEW_CALLBACK_MECHANISM

subdir-ccflags-y += $(SHMEM_FLAGS)
obj-m += ccinetdev/

obj-m += cimodemtty/

obj-m += citty/

obj-m += errorhandler/

obj-m += hwmap/

obj-m += pcac/
