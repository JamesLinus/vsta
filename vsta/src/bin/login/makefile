LIBS=-lusr -lc
OBJS=login.o runrc.o
OUT=runrc login
include ../../makefile.all

runrc: runrc.o
	$(LD) $(LDFLAGS) -o runrc $(CRT0) runrc.o $(LIBS)

login: $(OBJS)
	$(LD) $(LDFLAGS) -o login $(CRT0) login.o $(LIBS)

install: all
	strip login runrc
	cp login runrc $(ROOT)/bin
