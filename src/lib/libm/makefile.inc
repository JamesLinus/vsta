include libm/mach/makefile.inc

MOBJS=acosh.o cabs.o expm1.o jn.o support.o asincos.o cbrt.o \
	floor.o lgamma.o tan.o asinh.o cosh.o fmod.o log.o \
	tanh.o atan.o erf.o gamma.o log10.o pow.o atan2.o \
	exp.o j0.o log1p.o sincos.o atanh.o exp__e.o j1.o log__l.o \
	sinh.o $(LIBM_MACHOBS)

libm.a: mkshlib libm.tmp libm/libm.db
	./mkshlib -s libm/libm.db

libm.tmp: $(MOBJS) libc.a
	$(LD) -r -o libm.tmp $(MOBJS) libc.a

acosh.o: libm/acosh.c
	$(CC) $(CFLAGS) -c libm/acosh.c
cabs.o: libm/cabs.c
	$(CC) $(CFLAGS) -c libm/cabs.c
expm1.o: libm/expm1.c
	$(CC) $(CFLAGS) -c libm/expm1.c
jn.o: libm/jn.c
	$(CC) $(CFLAGS) -c libm/jn.c
support.o: libm/support.c
	$(CC) $(CFLAGS) -c libm/support.c
asincos.o: libm/asincos.c
	$(CC) $(CFLAGS) -c libm/asincos.c
cbrt.o: libm/cbrt.c
	$(CC) $(CFLAGS) -c libm/cbrt.c
floor.o: libm/floor.c
	$(CC) $(CFLAGS) -c libm/floor.c
lgamma.o: libm/lgamma.c
	$(CC) $(CFLAGS) -c libm/lgamma.c
tan.o: libm/tan.c
	$(CC) $(CFLAGS) -c libm/tan.c
asinh.o: libm/asinh.c
	$(CC) $(CFLAGS) -c libm/asinh.c
cosh.o: libm/cosh.c
	$(CC) $(CFLAGS) -c libm/cosh.c
fmod.o: libm/fmod.c
	$(CC) $(CFLAGS) -c libm/fmod.c
log.o: libm/log.c
	$(CC) $(CFLAGS) -c libm/log.c
tanh.o: libm/tanh.c
	$(CC) $(CFLAGS) -c libm/tanh.c
atan.o: libm/atan.c
	$(CC) $(CFLAGS) -c libm/atan.c
erf.o: libm/erf.c
	$(CC) $(CFLAGS) -c libm/erf.c
gamma.o: libm/gamma.c
	$(CC) $(CFLAGS) -c libm/gamma.c
log10.o: libm/log10.c
	$(CC) $(CFLAGS) -c libm/log10.c
pow.o: libm/pow.c
	$(CC) $(CFLAGS) -c libm/pow.c
atan2.o: libm/atan2.c
	$(CC) $(CFLAGS) -c libm/atan2.c
exp.o: libm/exp.c
	$(CC) $(CFLAGS) -c libm/exp.c
j0.o: libm/j0.c
	$(CC) $(CFLAGS) -c libm/j0.c
log1p.o: libm/log1p.c
	$(CC) $(CFLAGS) -c libm/log1p.c
sincos.o: libm/sincos.c
	$(CC) $(CFLAGS) -c libm/sincos.c
atanh.o: libm/atanh.c
	$(CC) $(CFLAGS) -c libm/atanh.c
exp__e.o: libm/exp__e.c
	$(CC) $(CFLAGS) -c libm/exp__e.c
j1.o: libm/j1.c
	$(CC) $(CFLAGS) -c libm/j1.c
log__l.o: libm/log__l.c
	$(CC) $(CFLAGS) -c libm/log__l.c
sinh.o: libm/sinh.c
	$(CC) $(CFLAGS) -c libm/sinh.c
