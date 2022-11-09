ccflags-y += -Idrivers/marvell/marvell-telephony-v2/ppp
ccflags-y += -Idrivers/marvell/marvell-telephony-v2/cimodemtty

gs_modem-objs := \
	../ppp/chap.o \
	../ppp/ipcp.o \
	../ppp/lcp.o \
	../ppp/pap.o \
	../ppp/ppp.o \
	../ppp/ppp_log.o \
	../ppp/ppp_platform.o \
	serial_if.o
obj-m  += gs_modem.o


#$([-f gs_modem.ko] && shell mv -f gs_modem.ko ppp.ko)
