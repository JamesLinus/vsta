OBJS=main.o par_port.o rw.o stat.o
OUT=par

include ../../../makefile.all

par: $(OBJS)
	$(LD) -o par $(CRT0) $(OBJS) $(LIBS) -lusr -lc

lpcp: lpcp.o
	$(LD) -o lpcp $(CRT0) lpcp.o $(LIBS) -lc

install: par
	strip par
	cp par $(ROOT)/boot
