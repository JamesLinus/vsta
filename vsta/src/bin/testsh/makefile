# Add -DSTAND for a more standalone-oriented flavor of shell
COPTS=-DSTAND
OBJS=testsh.o dump.o run.o
OUT=testsh

include ../../makefile.all

# LIBS= -lc
LIBS=-lusr -lc_s

testsh: $(OBJS)
	$(LD) $(LDFLAGS) -o testsh $(CRT0) $(OBJS) $(LIBS)

# Copy to ../../../bin if not STAND, but we don't seem to use that much
install: all
	strip testsh
	cp testsh $(ROOT)/boot
