CC=gcc
LD=ld
INCS=-I../../../../include
CFLAGS=$(INCS) -g
LIBS=-L../../../lib -lusr -lc
CRT0=../../../lib/crt0.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

PROGS=mkfs fsdb fsck
all:  $(PROGS)

fsck: fsck.o
	$(LD) -o fsck $(CRT0) fsck.o $(LIBS)

mkfs: mkfs.o
	$(LD) -o mkfs $(CRT0) mkfs.o $(LIBS)

fsdb: fsdb.o
	$(LD) -o fsdb $(CRT0) fsdb.o $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f mkfs fsdb fsck

install: all
	strip $(PROGS)
	cp mkfs ../../../../bin/mkfs_vfs
	cp fsdb ../../../../bin/fsdb_vfs
	cp fsck ../../../../bin/fsck_vfs
