OUT=ash
OBJS=builtins.o cd.o dirent.o echo.o error.o eval.o exec.o expand.o \
	input.o jobs.o mail.o main.o memalloc.o miscbltin.o \
	mystring.o nodes.o options.o parser.o redir.o show.o \
	syntax.o trap.o output.o var.o init.o mount.o
COPTS=-DSHELL

include ../../makefile.all

ash: $(OBJS)
	rm -f ash
	$(LD) $(LDFLAGS) -o ash $(CRT0) $(OBJS) -lusr -lc

echo.o: bltin/echo.c
	$(CC) $(CFLAGS) -c bltin/echo.c

install: ash
	strip ash
	cp ash $(ROOT)/bin/sh
