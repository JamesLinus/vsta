CC= gcc
LD= ld
CFLAGS= -g -I../../include -I../..
LDFLAGS= -L../../libc -L../../lib
LIBS= -lusr -lc
OBJS= login.o

.c.o:
	$(CC) $(CFLAGS) -c $*.c

login: $(OBJS)
	$(LD) $(LDFLAGS) -e start -o login ../../libc/crt0.o $(OBJS) $(LIBS)
