#
#	Makefile for KA9Q TCP/IP package for Berkeley UNIX
#
COPTS=-DUNIX -DBSD -DSLOWCHECK -DMSPTICK=20 -DASY
OUT=net
OBJS =	main.o arp.o arpcmd.o arpdump.o ax25.o ax25cmd.o \
	ax25dump.o ax25subr.o ax25user.o ax_mbx.o cmdparse.o \
	domain.o slhc.o bootp.o \
	files.o fingcli.o fingserv.o ftp.o ftpcli.o ftpserv.o icmp.o \
	icmpcmd.o icmpdump.o icmpmsg.o iface.o ip.o ipcmd.o ipdump.o \
	iproute.o kiss.o lapb.o lapbtime.o lcsum.o mbuf.o misc.o mulport.o \
	netuser.o nrcmd.o nrs.o nr3.o nrsubr.o nr4.o nr4subr.o \
	nr4user.o nr4timer.o pathname.o session.o slfp.o slip.o smisc.o \
	smtpcli.o smtpserv.o tcpcmd.o tcpdump.o \
	tcpin.o tcpout.o tcpsubr.o tcptimer.o tcpuser.o telnet.o \
	timer.o tnserv.o trace.o ttydriv.o udp.o udpcmd.o \
	udpdump.o version.o wildmat.o vsta.o vsta_io.o geneth.o enet.o \
	enetdump.o vsta_srv.o

include ../../makefile.all

net: $(OBJS)
	rm -f net
	$(LD) $(LDFLAGS) -o net $(CRT0) $(OBJS) -lusr -lc

install: net
	strip net
	cp net $(ROOT)/bin
