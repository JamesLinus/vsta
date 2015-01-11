#
# Makefile - builds VSTa SCSI/CAM server
#
COPTS=-D__SIM154X__ -DDEBUG -D__VSTA__
OUT=cam
CAM_OBJS=scsicd.o scsicmn.o scsidir.o scsiseq.o camutil.o
CAMDRV_OBJS=pdisk.o ptape.o pgen.o confdvr.o sim.o xpt.o scsicmds.o \
              camtimer.o camerror.o insque.o
CAMMDEP_OBJS=sim154x.o
CAMVSTA_OBJS=camdata.o camvsta.o vstasrv.o vstautil.o
OBJS=main.o $(CAM_OBJS) $(CAMDRV_OBJS) $(CAMMDEP_OBJS) $(CAMVSTA_OBJS)

include ../../../makefile.all

cam: $(OBJS)
	$(LD) $(LDFLAGS) -o cam $(CRT0SRV) $(OBJS) -ldpart -lusr -lc_s

install: cam
		strip cam
		cp cam $(ROOT)/boot
