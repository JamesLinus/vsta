COPTS=-Wall
OBJS=fsck.o
OUT=fsck mkfs fsdb
EXTRA_CLEAN=mkfs.o fsdb.o

include ../../../makefile.all

fsck: $(OBJS)
	rm -f fsck
	$(LD) $(LDFLAGS) -o fsck $(CRT0) $(OBJS) -lc

mkfs: mkfs.o
	rm -f mkfs
	$(LD) $(LDFLAGS) -o mkfs $(CRT0) mkfs.o -lc

fsdb: fsdb.o
	rm -f fsdb
	$(LD) $(LDFLAGS) -o fsdb $(CRT0) fsdb.o -lusr -lc

install: $(OUT)
	strip $(OUT)
	cp fsck $(ROOT)/bin/fsck_vfs
	cp mkfs $(ROOT)/bin/mkfs_vfs
	cp fsdb $(ROOT)/bin/fsdb_vfs
