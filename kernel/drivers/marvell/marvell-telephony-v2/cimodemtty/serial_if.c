/*
 * serial_if.c - interface between PPP and USB gadget "serial port"/TTY
 *
 * This software is distributed under the terms of the GNU General
 * Public License ("GPL") as published by the Free Software Foundation,
 * either version 2 of that License or (at your option) any later version.
 */

#include <linux/ioctl.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include "serial_if.h"
#include "common_datastub.h"

/* Defines - must be the same as in pxa910_f_modem.c */
#define TIOENABLE       _IOW('T', 206, int)	/* enable data */
#define TIODISABLE      _IOW('T', 207, int)	/* disable data */
#define TIOPPPON        _IOW('T', 208, int)
#define TIOPPPOFF       _IOW('T', 209, int)
#define TIOPPPSETIP		_IOW('T', 210, int)
#define TIOPPPONCSD     _IOW('T', 211, int)

/*******************************************\
  Static Functions
\*******************************************/
static void serial_init_data_service(void);
static void serial_send_from_tty(char *packet, unsigned int size);
static void serial_rx(unsigned char cid, char *packet, int size);
static int serial_ioctl(void *tty, unsigned int cmd, unsigned long arg);

static struct serial_data_channel_interface serial_ppp;

void serial_register_data_service(dataCbFuncs DataCbFuncs)
{
	serial_ppp.cbFuncs = DataCbFuncs;
	serial_init_data_service();
}

void serial_unregister_data_service(void)
{
	dataCbFuncs DataCbFuncs = { NULL, NULL, NULL, NULL, NULL, NULL };

	serial_ppp.cbFuncs = DataCbFuncs;
}

static int serial_data_tx_callback
	(const unsigned char *buf, int count, unsigned char cid)
{
	return gs_marvell_modem_send(buf, count, cid);
}

int serial_set_modem_control_mode(void)
{
	char deact_pdp[20];
	int buf_len = 0;
	serial_ppp.modem_state = ACM_CONTROL_MODE;

	if (serial_ppp.modem_curr_cid != 0xFF) {
		/* simulate at command to deactivate
		   pdp context as if received from PC */
		snprintf(deact_pdp, 20, "AT+CGACT=0,%d\x0d",
			serial_ppp.modem_curr_cid + 1);

		buf_len = strlen(deact_pdp);

		serial_send_from_tty(deact_pdp, buf_len);
	}
	return 1;
}

static void serial_send_from_tty(char *packet, unsigned int size)
{
	gs_marvell_modem_send_to_atcmdsrv(packet, size);
}

int serial_ppp_connect(unsigned char cid)
{
	char connect_str[30] = "AT+CGDATA=\"NULL\",%d\x0d";
	if (cid != 0xFF) {
		snprintf(connect_str, 30, connect_str, cid + 1);
		serial_send_from_tty(connect_str, strlen(connect_str));
	}
	return 1;
}

int serial_authenticate(unsigned char cid,
	PppAuthenticationParamsS *auth_params)
{
	char auth_str_fmt[] = "AT*AUTHReq=%d,%d,%s,%s\x0d";
	char auth_str[256];

	if (auth_params->auth_type == PPP_PAP) {
		if ((cid != 0xFF) &&
			(*auth_params->PapAuthenticationParams->Username
			!= '\0')) {
			snprintf(auth_str, 256, auth_str_fmt, cid + 1,
				1,
				auth_params->PapAuthenticationParams->
				Username,
				auth_params->PapAuthenticationParams->
				Password);
			serial_send_from_tty(auth_str,
				strlen(auth_str));
		}
	} else if (auth_params->auth_type == PPP_CHAP) {
		/* Currently not supported */
	}
	return 1;
}

static void serial_init_data_service(void)
{
	PppInitParamsS InitParam = {
		.ServiceType = SVC_TYPE_PDP_PPP_MODEM,
		.RxCallbackFunc = serial_data_tx_callback,
		.TerminateCallbackFunc = serial_set_modem_control_mode,
		.AuthenticateCallbackFunc = serial_authenticate,
		.ConnectCallbackFunc = serial_ppp_connect,
	};

	if (serial_ppp.cbFuncs.init) {
		serial_ppp.cbFuncs.init(&InitParam);
		serial_ppp.modem_state = ACM_CONTROL_MODE;
		serial_ppp.initialized = ACM_INITIALIZED;
		serial_ppp.modem_curr_cid = 0xff;
	}

	gs_marvell_modem_rx_psd_callback = serial_rx;
	gs_marvell_modem_ioctl_notify_callback = serial_ioctl;
}

static int serial_ioctl(void *tty, unsigned int cmd, unsigned long arg)
{
	DIRECTIPCONFIG ipParams;
	IpcpConnectionParamsS dataIpParams;

	/* Unused Params */
	(void)tty;
	(void)cmd;

	if (serial_ppp.initialized != ACM_INITIALIZED) {
		pr_err("serial_ppp is not initialized\n");
		return -EIO;
	}

	switch (cmd) {
	case TIOENABLE:
		serial_ppp.modem_curr_cid = (unsigned char)arg;
		serial_ppp.cbFuncs.setCid(serial_ppp.modem_curr_cid);
		pr_info("TIOENABLE: cid=%ld\n", arg);
		return 0;
	case TIODISABLE:
		serial_ppp.modem_curr_cid = (unsigned char)0xff;
		serial_ppp.cbFuncs.setCid(serial_ppp.modem_curr_cid);
		serial_ppp.modem_state = ACM_CONTROL_MODE;
		pr_info("TIODISABLE: cid=%ld\n", arg);
		return 0;
	case TIOPPPON:
		pr_info("TIOPPON: cid=%ld\n", arg);

		/* LOCK(); */
		/* fix cable disconnect /connect issue. */
		serial_ppp.cbFuncs.reset();
		serial_ppp.modem_curr_cid = (unsigned char)arg;
		serial_ppp.cbFuncs.setCid(serial_ppp.modem_curr_cid);
		serial_ppp.modem_state = ACM_DATA_MODE;

		gs_marvell_modem_send("\r\nCONNECT\r\n",
			sizeof("\r\nCONNECT\r\n") - 1,
				serial_ppp.modem_curr_cid);

		return 0;
	case TIOPPPOFF:
		pr_info("TIOPPPOFF: cid=%ld\n", arg);
		serial_ppp.modem_curr_cid = 0xff;
		serial_ppp.cbFuncs.setCid(serial_ppp.modem_curr_cid);
		serial_ppp.modem_state = ACM_CONTROL_MODE;
		serial_ppp.cbFuncs.reset();
		return 0;
	case TIOPPPSETIP:
		if (copy_from_user(&ipParams, (DIRECTIPCONFIG *)arg,
				sizeof(DIRECTIPCONFIG))) {
			return -EFAULT;
		}

		pr_info("TIOPPPSETIP: got IP %08x",
			ipParams.ipv4.inIPAddress);
		dataIpParams.IpAddress = ipParams.ipv4.inIPAddress;
		dataIpParams.PrimaryDns = ipParams.ipv4.inPrimaryDNS;
		dataIpParams.SecondaryDns = ipParams.ipv4.inSecondaryDNS;
		serial_ppp.cbFuncs.updateParameters(&dataIpParams);

		return 0;

		/* case TIOCMGET: */
	case TIOPPPONCSD:
		return 0;
	}

	/* could not handle ioctl */
	return -ENOIOCTLCMD;
}

/*
 * RX tasklet takes data out of the RX queue and hands it up to the TTY
 * layer until it refuses to take any more data (or is throttled back).
 * Then it issues reads for any further data.
 *
 * If the RX queue becomes full enough that no usb_request is queued,
 * the OUT endpoint may begin NAKing as soon as its FIFO fills up.
 * So QUEUE_SIZE packets plus however many the FIFO holds (usually two)
 * can be buffered before the TTY layer's buffers (currently 64 KB).
 */
static void serial_rx(unsigned char cid, char *packet, int size)
{
	(void)cid;

	/* when
	   serial_ppp.modem_state == ACM_CONTROL_MODE then data
	   passed via TTY to MTIL otherwise passed to PPP */
	if ((serial_ppp.modem_state == ACM_DATA_MODE) &&
		(serial_ppp.cbFuncs.messageReq) && (size > 0)) {
		serial_ppp.cbFuncs.messageReq(packet, size);
	}
}
