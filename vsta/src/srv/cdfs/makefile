#
# Makefile - builds VSTa CDROM filesystem
#

CC = gcc
LD = ld

HFILES = cdfs.h
CFILES = main.c cdfsdir.c cdfsmount.c cdfsopen.c cdfsread.c \
         cdfsrrip.c cdfstime.c cdfserror.c cdfsio.c block.c
OFILES = main.o cdfsdir.o cdfsmount.o cdfsopen.o cdfsread.o \
         cdfsrrip.o cdfstime.o cdfserror.o cdfsio.o block.o
HFILES = cdfs.h highsier.h iso.h isofs_rrip.h


INCS = -I../../../include
CFLAGS = -DDEBUG -D__VSTA__ $(INCS) -O -Wall
LDFLAGS = -L../../lib
CRT0=../../lib/crt0.o

cdfs:		$(OFILES)
		$(LD) $(LDFLAGS) -o cdfs $(CRT0) $(OFILES) -lusr -lc

clean:
		rm -f *.o

clobber:	clean
		rm -f cdfs

install:	cdfs
		strip cdfs
		cp cdfs /vsta/boot

cdfs.tar:
		tar cvf cdfs.tar makefile README $(HFILES) $(CFILES)

main.o:		main.c cdfs.h
cdfsdir.o:	cdfsdir.c cdfs.h
cdfsmount.o:	cdfsmount.c cdfs.h
cdfsopen.o:	cdfsopen.c cdfs.h
cdfsread.o:	cdfsread.c cdfs.h
cdfstime.o:	cdfstime.c cdfs.h
cdfsio.o:	cdfsio.c cdfs.h
block.o:	block.c
