CC=gcc
LD=ld
INCS=-I../../../include
CFLAGS=$(INCS) -g
LIBS=-L../../../libc -L../../../lib -lusr -lc
CRT0=../../../libc/crt0.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: mkfs fsdb fsck

fsck: fsck.o
	$(LD) -o fsck $(CRT0) fsck.o $(LIBS)

mkfs: mkfs.o
	$(LD) -o mkfs $(CRT0) mkfs.o $(LIBS)

fsdb: fsdb.o
	$(LD) -o fsdb $(CRT0) fsdb.o $(LIBS)

clean:
	del *.o
	del mkfs
	del fsdb
	del fsck
