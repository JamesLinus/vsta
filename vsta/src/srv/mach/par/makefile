CC=gcc
VSTADIR=../../..
CFLAGS=-O -I$(VSTADIR)/include -nostdinc
OBJS=main.o par_port.o rw.o stat.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: par lpcp

par: $(OBJS)
	$(CC) -nostdlib @objs -o par \
		-L$(VSTADIR)/lib -lusr -L$(VSTADIR)/libc -lc

lpcp: lpcp.o
	$(CC) -nostdlib $(VSTADIR)/libc/crt0.o lpcp.o \
		-o lpcp -L$(VSTADIR)/libc -lc

clean:
	del *.o
	del par
	del lpcp
	del objs
