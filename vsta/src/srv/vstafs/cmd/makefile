CC=gcc
LD=ld
INCS=-I../../include -I../..
CFLAGS=$(INCS) -g
LIBS=-L../../libc -L../../lib -lusr -lc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: mkfs fsdb

mkfs: mkfs.o
	$(LD) -o mkfs ../../libc/crt0.o mkfs.o $(LIBS)

fsdb: fsdb.o
	$(LD) -o fsdb ../../libc/crt0.o fsdb.o $(LIBS)

clean:
	del *.o
	del mkfs
	del fsdb
