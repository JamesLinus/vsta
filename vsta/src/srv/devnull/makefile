CC= gcc
LD= ld
INCS= -I../include -I..
CFLAGS= -DDEBUG $(INCS) -O
LDFLAGS= -L../libc -L../lib
OBJS= main.o
LIBS= -lusr -lc

all: devnull
.c.o:
	$(CC) $(CFLAGS) -c $*.c

devnull: $(OBJS)
	$(LD) $(LDFLAGS) -o devnull ../libc/crt0.o $(OBJS) $(LIBS)

clean:
	del *.o
	del devnull
