#include <stdio.h>
#include <fcntl.h>
#include <signal.h>

struct ethheader {
	unsigned char dest[6];
	unsigned char source[6];
	unsigned short type;
};

void usage (char *me)
{
	printf ("Usage: %s [-r] [-s]\n\t-r recives packets of ethernet\n"
		"\t-s sends a packet to ethernet\n", me);
	exit (0);
}

void dump_ethaddr (char *ralf, unsigned char *addr)
{

	printf ("%s%X:%X:%X:%X:%X:%X\n", ralf, addr[0], addr[1],
			addr[2], addr[3], addr[4], addr[5]);
}

void ether_recv (int ethfd)
{
	unsigned char buf[1500];
	struct ethheader *ethh;
	int i, len;

	while (1) {
		len = read (ethfd, buf, sizeof (buf));
		ethh = (struct ethheader *)buf;
		dump_ethaddr ("Dest eth: ", ethh->dest);
		dump_ethaddr ("Source eth: ", ethh->source);
		printf ("Type: 0x%4.4X\n", ethh->type);
		printf ("Data: ");

		i = 0;
		while (i < len - 14) {
			if (isprint (buf[14 + i])) {
				printf ("%c ", buf[14 + i]);
			} else {
				printf ("0x%X ", buf[14 + i]);
			}
			i++;
		}
		printf ("\n\n");
	}
}

void ether_send (int ethfd)
{
	char buf[200];
	struct ethheader *ethh;
	int i;


	memset (buf, 0, 200);
	ethh = (struct ethheader *)buf;

	memset (ethh->dest, 0xFF, 6);
	memset (ethh->source, 0x10, 6);
	ethh->type = 0x0800;

	i = 0;
	while (i < 100) {
		buf[14 + i] = 0x61 + i % 0x19;	
		i++;
	}

	write (ethfd, buf, 200);
}

int main (int argc, char **argv)
{
	int fd;


	if (argc < 2) {
		usage (argv[0]);
	}

	fd = open ("//net/el3:0", O_RDWR);

	if (!strcmp (argv[1], "-r")) {
		ether_recv (fd);
	}
	else if (!strcmp (argv[1], "-s")) {
		ether_send (fd);
	}
	else {
		usage (argv[0]);
	}

	close (fd);

}
