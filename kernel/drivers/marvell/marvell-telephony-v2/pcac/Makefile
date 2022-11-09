ccflags-y += -Idrivers/marvell/marvell-telephony-v2/pcac/inc \
			 -Idrivers/marvell/marvell-telephony-v2/pcac/shmem/inc \
			 -Idrivers/marvell/marvell-telephony-v2/pcac/dbgLog

ccflags-y += \
	-DOSA_LINUX\
	-DOSA_USE_ASSERT\
	-DDEBUG_BUILD\
	-DACI_LNX_KERNEL\
	-DENV_LINUX \
	-DFLAVOR_APP \
	-DDATA_STUB_IN_KERNEL \
	-DACI_USE_SHMEM \
	-DACIPC_ENABLE_NEW_CALLBACK_MECHANISM \
	-DCCI_OPT_SAC_USE_SHMEM_DIRECT

acipcdev-objs := dbgLog/dbgLogKernel.o \
	dbgLog/dbgLogLnxKernel.o \
	ac_rpc/src/ac_rpc.o \
	ac_rpc_users/src/ac_rpc_users.o \
	ac_rpc_users/src/dip_channel_server.o \
	shmem/src/acipc_data.o\
	shmem/src/com_mem_mapping_kernel.o\
	shmem/src/shmem_lnx_kmod.o\
	shmem/src/shmem_kernel_to_user_ipc.o\
	shmem/src/data_channel_kernel_shmem.o\
	shmem/src/allocator.o
obj-m	+= acipcdev.o
obj-m	+= ci_data/
