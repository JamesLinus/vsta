LIBM_MACHOBS=infin.o

infin.o: libm/mach/infin.c
	$(CC) -c libm/mach/infin.c
