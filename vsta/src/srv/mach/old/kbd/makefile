CC= gcc
LD= ld
INCS= -I../../../include
CFLAGS= -DKBD -DDEBUG $(INCS) -O
LDFLAGS= -L../../../libc
OBJS= main.o rw.o isr.o stat.o
LIBS= -lusr -lc

.c.o:
	$(CC) $(CFLAGS) -c $*.c

kbd: $(OBJS)
	$(LD) $(LDFLAGS) -e start -o kbd ../../../libc/crt0srv.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f kbd

install: kbd
	strip kbd
	cp kbd ../../../boot
