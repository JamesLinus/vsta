include ../makefile.all

clean:

clobber: clean

install:
	mkdir -p $(ROOT)/include/sys
	mkdir -p $(ROOT)/include/mach
	cp *.h $(ROOT)/include
	cp sys/*.h $(ROOT)/include/sys
	cp mach/*.h $(ROOT)/include/mach
