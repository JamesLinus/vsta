CC= gcc
LD= ld
INCS= -I../../../include
CFLAGS= -DDEBUG $(INCS) -O
LDFLAGS= -L../../lib
OBJS= main.o open.o rw.o stat.o
LIBS= -lusr -lc

all: pipe
.c.o:
	$(CC) $(CFLAGS) -c $*.c

pipe: $(OBJS)
	$(LD) $(LDFLAGS) -o pipe ../../lib/crt0.o $(OBJS) $(LIBS)

clean:
	rm -f *.o

clobber: clean
	rm -f pipe

install: all
	strip pipe
	cp pipe ../../../boot
