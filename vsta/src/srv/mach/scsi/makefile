#
# Makefile - builds VSTa SCSI/CAM server
#

CC = gcc
LD = ld

HFILES = cam.h camvsta.h diskpart.h insque.h scsi.h scsivsta.h
CFILES = camdata.c camutil.c camvsta.c insque.c \
         main.c pdisk.c pgen.c sim.c sim154x.c vstautil.c vstasrv.c \
         scsicd.c scsicmn.c scsidir.c scsiseq.c scsicmds.c

INCS = -I../../../include
CFLAGS = -DDEBUG -D__VSTA__ $(INCS) -O
LDFLAGS = -L../../../libc
CRT0=../../../lib/crt0srv.o

CAM_OBJS = scsicd.o scsicmn.o scsidir.o scsiseq.o camutil.o
CAMDRV_OBJS = scsicmds.o camtimer.o camerror.o insque.o sim.o xpt.o
CAMMDEP_OBJS = sim154x.o
CAMVSTA_OBJS = camdata.o camvsta.o pdisk.o pgen.o vstasrv.o vstautil.o
CAM_LIB = libcam.a
CAMDRV_LIB = libcamdrv.a
CAMMDEP_LIB = libcammdep.a
CAMVSTA_LIB = libcamvsta.a

.c.o:
		$(CC) $(CFLAGS) -c $*.c

cam:	main.o $(CAMVSTA_LIB) $(CAM_LIB) $(CAMMDEP_LIB) $(CAMDRV_LIB)
	$(LD) $(LDFLAGS) -o cam $(CRT0) main.o $(CAMVSTA_LIB) \
		$(CAMMDEP_LIB) $(CAMDRV_LIB) $(CAM_LIB) -ldpart -lusr -lc_s

libcam.a:	$(CAM_OBJS)
		rm -f libcam.a
		ar rsv libcam.a $(CAM_OBJS)

libcamdrv.a:	$(CAMDRV_OBJS)
		rm -f libcamdrv.a
		ar rsv libcamdrv.a $(CAMDRV_OBJS)

libcammdep.a:	$(CAMMDEP_OBJS)
		rm -f libcammdep.a
		ar rsv libcammdep.a $(CAMMDEP_OBJS)

libcamvsta.a:	$(CAMVSTA_OBJS)
		rm -f libcamvsta.a
		ar rsv libcamvsta.a $(CAMVSTA_OBJS)

clean:
		rm -f *.o

clobber: clean
		rm -f cam *.a

install: cam
		strip cam
		cp cam ../../../boot

cam.tar:
		tar cvf cam.tar makefile README *.h *.c

camdata.o:	camdata.c cam.h camvsta.h scsi.h scsivsta.h
camutil.o:	camutil.c cam.h camvsta.h scsi.h scsivsta.h
camvsta.o:	camvsta.c cam.h camvsta.h scsi.h scsivsta.h
insque.o:	insque.c insque.h
main.o:		main.c cam.h camvsta.h scsi.h scsivsta.h
pdisk.o:	pdisk.c cam.h camvsta.h scsi.h scsivsta.h
pgen.o:		pgen.c cam.h camvsta.h scsi.h scsivsta.h
sim.o:		sim.c cam.h camvsta.h scsi.h scsivsta.h
sim154x.o:	sim154x.c cam.h camvsta.h scsi.h scsivsta.h
vstasrv.o:	vstasrv.c camvsta.h
vstautil.o:	vstautil.c
xpt.o:		xpt.c cam.h camvsta.h scsi.h scsivsta.h
