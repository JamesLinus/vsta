CC= gcc
LD= ld
INCS= -I../../../include
CFLAGS= -DDEBUG $(INCS) -O
LDFLAGS= -L../../../libc -L../../../lib
OBJS= main.o rw.o wd.o fdisk.o dir.o stat.o
LIBS= -lusr -lc

all: wd
.c.o:
	$(CC) $(CFLAGS) -c $*.c

wd: $(OBJS)
	$(LD) $(LDFLAGS) -o wd ../../../libc/crt0srv.o $(OBJS) $(LIBS)

clean:
	del *.o
	del wd
