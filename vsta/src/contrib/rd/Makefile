# use these options to exclude zlib
COPTS=
LIBS=-lusr -lc

# use these options to use zlib
#COPTS=-DHAVE_ZLIB
#LIBS=-lusr -lz -lc

OBJS=main.o stat.o open.o rw.o load.o
OUT=rd

ROOT=/vsta
SRC=/vsta
CC=gcc
LD=ld
CFLAGS=-nostdinc -I$(SRC)/include -O $(COPTS)
LDFLAGS=-L$(SRC)/lib
CRT0=$(SRC)/lib/crt0.o
CRT0SRV=$(SRC)/lib/crt0srv.o

rd: $(OBJS)
	$(LD) $(LDFLAGS) -o rd $(CRT0) $(OBJS) $(LIBS)

all: $(OUT)

clean:
	rm -f $(OBJS)

clobber: clean
	rm -f $(OUT)



