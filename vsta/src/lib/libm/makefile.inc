CC=gcc
CFLAGS=
OBJS=acosh.o cabs.o expm1.o jn.o support.o asincos.o cbrt.o \
	floor.o lgamma.o tan.o asinh.o cosh.o fmod.o log.o \
	tanh.o atan.o erf.o gamma.o log10.o pow.o atan2.o \
	exp.o j0.o log1p.o sincos.o atanh.o exp__e.o j1.o log__l.o \
	sinh.o

libm.a: $(OBJS)
	rm -f libm.a
	ar rsv libm.a $(OBJS)
