CC=gcc
CFLAGS=-DDEBUG
PROGS=inetdb telnetd

all: $(PROGS)

inetdb: inetdb.o
	$(CC) $(CFLAGS) -o inetdb inetdb.o

telnetd: telnetd.o
	$(CC) $(CFLAGS) -o telnetd telnetd.o -lusr
