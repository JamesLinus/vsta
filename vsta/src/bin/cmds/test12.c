/*
 * test for pipe() working
 */
#include <sys/types.h>

static int fds[2];

static void
reader(void)
{
	char buf[80];
	int x;
	static char msg[] = "Reader\n", msg2[] = "Reader done\n";

	write(1, msg, sizeof(msg)-1);
	while ((x = read(fds[0], buf, sizeof(buf))) > 0) {
		write(1, buf, x);
		write(1, "\n", 1);
	}
	write(1, msg2, sizeof(msg2)-1);
	_exit(0);
}

main()
{
	int x;
	pid_t pid;
	static char msg[] = "Writer\n", msg2[] = "Writer done\n";

	if (pipe(fds) < 0) {
		perror("pipe");
		exit(1);
	}
	pid = tfork(reader);
	write(1, msg, sizeof(msg)-1);
	for (x = 0; x < 5; ++x) {
		static char testpat[] = "abcdefghijklmnopqrstuvwxyz";

		write(fds[1], testpat, x*4 + 1);
	}
	close(fds[1]);
	write(1, msg2, sizeof(msg2)-1);
	exit(0);
}
