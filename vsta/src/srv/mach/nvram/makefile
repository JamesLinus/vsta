CC	= gcc
LD	= ld
RM	= rm -f
INCS	= -I../../../../include
CFLAGS	= -Wall $(INCS)
LDFLAGS	= -L../../../lib
OBJS	= main.o rw.o dir.o stat.o
LIBS	= -lusr -lc
CRT0	= ../../../lib/crt0.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

all: nvram

nvram: $(OBJS)
	$(LD) $(LDFLAGS) -o nvram $(CRT0) $(OBJS) $(LIBS)

clean:
	$(RM) *.o

clobber: clean
	$(RM) nvram

install: nvram
	strip nvram
	cp nvram ../../../../boot
