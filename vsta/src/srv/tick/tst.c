#include <select.h>
#include <std.h>
#include <time.h>

int
main(int argc, char **argv)
{
	int x, fd;
	fd_set rfds;
	time_t t;

	fd = open("//fs/tick", 0);
	if (fd < 0) {
		perror("//fs/tick");
		exit(1);
	}
	for (;;) {
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		if (select(fd+1, &rfds, 0, 0, 0) < 0) {
			perror("select");
			exit(1);
		}
		x = read(fd, &t, sizeof(t));
		printf("%d 0x%x\n", x, t);
	}
}
