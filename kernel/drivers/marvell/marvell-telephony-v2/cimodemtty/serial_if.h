#ifndef _SERIAL_INTERFACE_H_
#define	_SERIAL_INTERFACE_H_

#include "ppp_types.h"

/*******************************************\
  externs from tty
\*******************************************/

typedef void (*marvell_modem_rx_callback)
	(unsigned char cid, char *packet, int len);

typedef int (*marvell_modem_ioctl_notify_callback)
	(void *tty, unsigned int cmd, unsigned long arg);


extern int gs_marvell_modem_send(const unsigned char *buf,
	int count, unsigned char cid);
extern int gs_marvell_modem_send_to_atcmdsrv(char *buf, int count);

extern marvell_modem_rx_callback gs_marvell_modem_rx_psd_callback;
extern marvell_modem_ioctl_notify_callback
	gs_marvell_modem_ioctl_notify_callback;

typedef int (*rxCallbackFunc) (const unsigned char *packet,
	int len, unsigned char cid);
typedef void (*terminateCallbackFunc) (void);
typedef void (*authenticateCallbackFunc) (unsigned char cid,
	PppAuthenticationParamsS *auth_params);
typedef void (*connectCallbackFunc) (unsigned char cid);

typedef struct _initParams {
	unsigned int ServiceType;
	rxCallbackFunc RxCallbackFunc;
	terminateCallbackFunc TerminateCallbackFunc;
	authenticateCallbackFunc AuthenticateCallbackFunc;
	connectCallbackFunc ConnectCallbackFunc;
} initParams;

typedef struct _IpConnectionParams {
	unsigned int IpAddress;
	unsigned int PrimaryDns;
	unsigned int SecondaryDns;
} ipConnectionParams;

/* PppInit prototype */
typedef void (*initFunc) (PppInitParamsS *);
/* PppDeinit prototype */
typedef void (*deInitFunc) (void);
/* PppReset prototype */
typedef void (*resetFunc) (void);
/* PppSetCid prototype */
typedef void (*setCidFunc) (unsigned int);
/* PppMessageReq prototype */
typedef void (*messageReqFunc) (const U_CHAR *, U_INT);
/* PppUpdateIpParams prototype */
typedef void (*updateIpParamsFunc) (IpcpConnectionParamsS *);

typedef struct _dataCBFuncs {
	initFunc init;
	deInitFunc deInit;
	resetFunc reset;
	setCidFunc setCid;
	messageReqFunc messageReq;
	updateIpParamsFunc updateParameters;
} dataCbFuncs;

struct serial_data_channel_interface {
	dataCbFuncs cbFuncs;
#define ACM_INITIALIZED		1
#define ACM_NON_INITIALIZED	0
	unsigned int initialized;
#define ACM_DATA_MODE		1
#define ACM_CONTROL_MODE	0
	unsigned int modem_state;
	unsigned char modem_curr_cid;
};

void serial_register_data_service(dataCbFuncs dataCbFuncs);
void serial_unregister_data_service(void);

#endif
