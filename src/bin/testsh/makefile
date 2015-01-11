# Add -DSTAND for a more standalone-oriented flavor of shell
COPTS=-DSTAND
OBJS=testsh.o dump.o run.o
OUT=testsh

include ../../makefile.all

# LIBS= -lc for non-standalone
LIBS=-lusr -lc_s

# Use CRT0 for -lc (not standalone), otherwise CRT0SRV
testsh: $(OBJS)
	$(LD) $(LDFLAGS) -o testsh $(CRT0SRV) $(OBJS) $(LIBS)

# Copy to ../../../bin if not STAND, but we don't seem to use that much
install: all
	strip testsh
	cp testsh $(ROOT)/boot
