#include <fcntl.h>

#define SECSZ (512)

static char secbuf[2*SECSZ];
static char *secptr;

/*
 * fragem()
 *	Create the two files, and try to cause fragmentation
 */
static void
fragem(char *p1, char *p2)
{
	int fd1, fd2;
	int x;

	fd1 = open(p1, O_WRITE|O_CREAT, 0600);
	fd2 = open(p2, O_WRITE|O_CREAT, 0600);
	if ((fd1 < 0) || (fd2 < 0)) {
		perror(p1);
		exit(1);
	}
	for (x = 0; x < 200; ++x) {
		if (write(fd1, secptr, SECSZ) != SECSZ) {
			break;
		}
		if (write(fd2, secptr, SECSZ) != SECSZ) {
			break;
		}
	}
	printf("%d frags written\n", x);
	close(fd2);
	close(fd1);
}

main(int argc, char **argv)
{
	int x;
	char buf[200], buf2[200];

	if (argc != 2) {
		printf("Usage is: %s <path>\n", argv[0]);
		exit(1);
	}
	secptr = (char *)(((long)secbuf + (SECSZ-1)) & ~(SECSZ-1));
	bzero(secptr, SECSZ);
	for (x = 0; x <= 4; ++x) {
		sprintf(buf, "%s/%d-a", argv[1], x);
		sprintf(buf2, "%s/%d-b", argv[1], x);
		fragem(buf, buf2);
		gets(buf);
		if (buf[0] == 'q') {
			exit(0);
		}
	}
	return(0);
}
