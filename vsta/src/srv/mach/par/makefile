CC=gcc
VSTADIR=../../../vsta
CFLAGS=-O -I$(VSTADIR)/include -nostdinc
OBJS=main.o par_port.o rw.o stat.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: par lpcp

par: $(OBJS) objs
	$(CC) -nostdlib @objs -o par \
		-L$(VSTADIR)/lib -lusr -L$(VSTADIR)/libc -lc

objs: makefile
	echo $(VSTADIR)/libc/crt0.o > objs
	echo $(OBJS) >> objs

lpcp: lpcp.o
	$(CC) -nostdlib $(VSTADIR)/libc/crt0.o lpcp.o \
		-o lpcp -L$(VSTADIR)/libc -lc

main.o: main.c par.h par_port.h
par_port.o: par_port.c par_port.h
rw.o: rw.c par.h par_port.h
stat.o: stat.c par.h par_port.h
lpcp.o: lpcp.c

clean:
	del *.o
	del par
	del lpcp
	del objs
