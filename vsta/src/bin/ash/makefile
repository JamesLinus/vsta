CC=gcc
LD=ld
CRT0=../../lib/crt0.o
LIBS=-L../../lib -lc

SRCS=	builtins.c cd.c dirent.c bltin/echo.c \
	error.c eval.c exec.c expand.c \
	input.c jobs.c mail.c main.c memalloc.c miscbltin.c \
	mystring.c nodes.c options.c parser.c redir.c show.c \
	syntax.c trap.c output.c var.c init.c mount.c

OBJS=	builtins.o cd.o dirent.o echo.o error.o eval.o exec.o expand.o \
	input.o jobs.o mail.o main.o memalloc.o miscbltin.o \
	mystring.o nodes.o options.o parser.o redir.o show.o \
	syntax.o trap.o output.o var.o init.o mount.o

CFLAGS=-DSHELL -I. -I../../../include -O

ash: $(OBJS)
	$(LD) -o ash $(CRT0) $(OBJS) $(LIBS)

echo.o: bltin/echo.c
	$(CC) $(CFLAGS) -c bltin/echo.c
