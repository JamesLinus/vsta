CC= gcc
LD= ld
INCS= -I../include -I..
CFLAGS= -DDEBUG $(INCS) -O
LDFLAGS= -L../libc -L../lib
OBJS= main.o rw.o isr.o stat.o
CLIB=../libc/libc.a
LIBS= -lusr -lc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

kbd: $(OBJS)
	$(LD) $(LDFLAGS) -e start -o kbd ../libc/crt0.o $(OBJS) $(LIBS)
