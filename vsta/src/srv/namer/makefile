CC= gcc
LD= ld
INCS= -I../../../include
CFLAGS= -DDEBUG $(INCS) -O
LDFLAGS= -L../../lib
OBJS= main.o open.o rw.o stat.o
LIBS= -lusr -lc_s

all: namer
.c.o:
	$(CC) $(CFLAGS) -c $*.c

namer: $(OBJS)
	$(LD) $(LDFLAGS) -o namer ../../lib/crt0srv.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f namer

install: all
	strip namer
	cp namer ../../../boot
