COPTS=-Wall -DDEBUG
OBJS=main.o el3.o rw.o dir.o stat.o netlib.o
OUT=3c509

include ../../../makefile.all

el3.o: el3.h

3c509: $(OBJS)
	rm -f $(OUT)
	$(LD) $(LDFLAGS) -o $(OUT) $(CRT0) $(OBJS) -lusr -lc

install: $(OUT)
	strip $(OUT)
	cp $(OUT) $(ROOT)/boot
