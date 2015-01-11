#
# Makefile - builds VSTa CDROM filesystem
#
OBJS=main.o cdfsdir.o cdfsmount.o cdfsopen.o cdfsread.o \
         cdfsrrip.o cdfstime.o cdfserror.o cdfsio.o block.o
COPTS=-DDEBUG -D__VSTA__ -Wall
OUT=cdfs

include ../../makefile.all

cdfs: $(OBJS)
	$(LD) $(LDFLAGS) -o cdfs $(CRT0) $(OBJS) -lusr -lc

install:	cdfs
		strip cdfs
		cp cdfs $(ROOT)/boot
