#
#	Makefile for KA9Q TCP/IP package for Berkeley UNIX
#
CC=gcc
CFLAGS=-DUNIX -DBSD -DSLOWCHECK -DMSPTICK=20
LIBS=-lusr

SRCS =	main.c arp.c arpcmd.c arpdump.c ax25.c ax25cmd.c \
	ax25dump.c ax25subr.c ax25user.c ax_mbx.c cmdparse.c \
	domain.c slhc.c bootp.c \
	files.c fingcli.c fingserv.c ftp.c ftpcli.c ftpserv.c icmp.c \
	icmpcmd.c icmpdump.c icmpmsg.c iface.c ip.c ipcmd.c ipdump.c \
	iproute.c kiss.c lapb.c lapbtime.c lcsum.c mbuf.c misc.c mulport.c \
	netuser.c nrcmd.c nrs.c nr3.c nrsubr.c nr4.c nr4subr.c \
	nr4user.c nr4timer.c pathname.c session.c slfp.c slip.c smisc.c \
	smtpcli.c smtpserv.c tcpcmd.c tcpdump.c \
	tcpin.c tcpout.c tcpsubr.c tcptimer.c tcpuser.c telnet.c \
	timer.c tnserv.c trace.c ttydriv.c udp.c udpcmd.c \
	udpdump.c version.c wildmat.c vsta.c vsta_io.c geneth.c enet.c \
	enetdump.c vsta_srv.c

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

net:	$(OBJS)
	$(CC) $(CFLAGS) -o net $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f net net.debug

install: net
	strip net
	cp net ../../bin/net
