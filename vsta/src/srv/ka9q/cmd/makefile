CC=gcc
CFLAGS=-Wall -DDEBUG
PROGS=proxyd inetdb telnetd

all: $(PROGS)

inetdb: inetdb.o
	rm -f inetdb
	$(CC) $(CFLAGS) -o inetdb inetdb.o

telnetd: telnetd.o
	rm -f telnetd
	$(CC) $(CFLAGS) -o telnetd telnetd.o -lusr

proxyd: proxyd.o
	rm -f proxyd
	$(CC) $(CFLAGS) -o proxyd proxyd.o -lusr

clean:
	rm -f *.o

clobber: clean
	rm -f $(PROGS)
