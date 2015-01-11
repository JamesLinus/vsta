OUT=setime date
OBJS=setime.o time.o getdate.o date.o
include ../../makefile.all
LIB=-lc

setime: setime.o time.o getdate.o
	$(LD) $(LDFLAGS) $(CRT0) -o setime setime.o time.o getdate.o $(LIB)

date: date.o time.o
	$(LD) $(LDFLAGS) $(CRT0) -o date date.o time.o $(LIB)

install: all
	strip $(OUT)
	cp $(OUT) $(ROOT)/bin
