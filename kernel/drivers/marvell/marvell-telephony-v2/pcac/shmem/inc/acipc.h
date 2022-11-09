/*
 *(C) Copyright 2006 Marvell International Ltd.
 * All Rights Reserved
 */

#ifndef __ACIPC_LIB_H
#define __ACIPC_LIB_H
#include "linux_types.h"

#define ACIPC_USER_DEBUG 1
#ifdef ACIPC_USER_DEBUG
#define prdbg_us_acipc(fmt, arg...) printk(fmt, ##arg)
#else
#define prdbg_us_acipc(fmt, arg...) do { } while (0)
#endif

/* user level ioctl commands for accessing APIs */
#define ACIPC_SET_EVENT              0
#define ACIPC_GET_EVENT                1
#define ACIPC_SEND_DATA      2
#define ACIPC_READ_DATA         3
#define ACIPC_BIND_EVENT         4
#define ACIPC_UNBIND_EVENT      5
#define ACIPC_GET_BIND_EVENT_ARG	6

#define ACIPC_NUMBER_OF_EVENTS (10)

#ifndef ACI_LNX_KERNEL
typedef unsigned int u32;
#endif

/* clients callback type*/
/*ICAT EXPORTED FUNCTION_TYPEDEF*/
typedef u32(*ACIPC_RecEventCB) (u32 eventsStatus);
typedef u32 ACIPC_Data;

/* this enum define the event type*/
/*ICAT EXPORTED ENUM*/
typedef enum {
	ACIPC_DDR_RELQ_REQ = 0x00000001,
	ACIPC_DDR_RELQ_ACK = 0x00000001,
	ACIPC_DDR_260_RELQ_REQ = 0x00000002,
	ACIPC_DDR_260_RELQ_ACK = 0x00000002,
	ACIPC_MSL_SLEEP_ALLOW = 0x00000004,
	ACIPC_MSL_WAKEUP_ACK = 0x00000008,
	ACIPC_MSL_WAKEUP_REQ = 0x00000010,
	ACIPC_DATA_Q_ADRS = 0x00000020,
	ACIPC_DATA_IND = 0x00000040,
	ACIPC_CI_DATA_DOWNLINK = 0x00000080,
	ACIPC_CI_DATA_UPLINK = 0x00000080,
	ACIPC_DDR_260_READY_REQ = 0x00000100,
	ACIPC_DDR_260_READY_ACK = 0x00000100,
	ACIPC_DDR_READY_REQ = 0x00000200,
	ACIPC_DDR_READY_ACK = 0x00000200,
} ACIPC_EventsE;

typedef enum {
	ACIPC_RC_OK = 0,
	ACIPC_HISTORICAL_EVENT_OCCUR,
	ACIPC_EVENT_ALREADY_BIND,
	ACIPC_RC_FAILURE,
	ACIPC_RC_API_FAILURE,
	ACIPC_RC_WRONG_PARAM
} ACIPC_ReturnCodeE;

extern const ACIPC_EventsE _acipcPriorityTable[ACIPC_NUMBER_OF_EVENTS];

/* used by clients when binding a callback to an event*/
/*ICAT EXPORTED ENUM*/
typedef enum {
	ACIPC_CB_NORMAL = 0,	/* callback will be called only if the DDR available        */
	ACIPC_CB_ALWAYS_NO_DDR	/* callback will be called always ,even if the DDR is not available */
} ACIPC_CBModeE;

struct acipc_ioctl_arg {
	u32 arg;
	ACIPC_EventsE set_event;
	ACIPC_CBModeE cb_mode;
};

typedef enum {
	ACSIPC,
	ACMIPC,
	ACIPC_NUMBER_OF_DEVICES
} acipc_dev_type;

extern unsigned char acipc_lib_InitACIPCShmem(void);

extern ACIPC_ReturnCodeE ACIPCEventUnBindExt(acipc_dev_type type, u32 userEvent);
extern ACIPC_ReturnCodeE ACIPCEventBindExt(acipc_dev_type type, u32 userEvent, ACIPC_RecEventCB cb,
					   ACIPC_CBModeE cbMode, u32 *historical_event_status);
extern ACIPC_ReturnCodeE ACIPCEventStatusGetExt(acipc_dev_type type, u32 userEvent, u32 *status);
extern ACIPC_ReturnCodeE ACIPCDataReadExt(acipc_dev_type type, ACIPC_Data *data);
extern ACIPC_ReturnCodeE ACIPCDataSendExt(acipc_dev_type type, ACIPC_EventsE userEvent, ACIPC_Data data);
extern ACIPC_ReturnCodeE ACIPCEventSetExt(acipc_dev_type type, ACIPC_EventsE userEvent);

#endif
