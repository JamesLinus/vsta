/*
 * ptysh.c
 *	Run a shell under a PTY under ptysh's control
 */
#include <stdio.h>
#include <fcntl.h>
#include <termios.h>
#include <select.h>
#include <signal.h>
#include <std.h>

static void
die(void)
{
	exit(0);
}
static void
ttmode(int fd)
{
	struct termios tc;

	/*
	 * Get individual chars from our TTY
	 */
	tcgetattr(fd, &tc);
	tc.c_lflag &= ~(ECHO | ISIG | ICANON);
	tc.c_cc[VMIN] = 1;
	tc.c_cc[VTIME] = 0;
	tcsetattr(fd, TCSANOW, &tc);
}

int
main(int argc, char **argv)
{
	char c, buf[80];
	int x, mfd, sfd;
	fd_set fds;
	pid_t pid;

	if (argc < 2) {
		fprintf(stderr, "Usage is: %s <index>\n", argv[0]);
		exit(1);
	}

	/*
	 * Open master and slave PTY sides
	 */
	sprintf(buf, "//fs/pty:pty%s", argv[1]);
	mfd = open(buf, O_RDWR);
	if (mfd < 0) {
		perror(buf);
		exit(1);
	}
	sprintf(buf, "//fs/pty:tty%s", argv[1]);
	sfd = open(buf, O_RDWR);
	if (sfd < 0) {
		perror(buf);
		exit(1);
	}

	/*
	 * Master will deal with raw data
	 */
	ttmode(mfd);

	/*
	 * Launch a child
	 */
	pid = fork();
	if (pid == 0) {
		/*
		 * Make the slave file descriptor our
		 * stdin/out/err
		 */
		close(mfd); close(2); close(1); close(0);
		dup2(sfd, 0); dup2(sfd, 1); dup2(sfd, 2);
		close(sfd);

		/*
		 * Launch our shell
		 */
		execl("/bin/sh", "sh", NULL);
		_exit(1);
	}

	/*
	 * Exit on SIGCLD
	 */
	signal(SIGCLD, die);

	/*
	 * Drop child file descriptor on master side
	 */
	close(sfd);

	/*
	 * Wait for data on either our TTY or the PTY
	 */
	ttmode(0);
	for (;;) {
		/*
		 * Wait for data
		 */
		FD_ZERO(&fds);
		FD_SET(mfd, &fds);
		FD_SET(0, &fds);
		if (select(mfd+1, &fds, NULL, NULL, NULL) < 0) {
			perror("select");
			kill(pid, SIGKILL);
			break;
		}

		/*
		 * Typing stuff down to the PTY
		 */
		if (FD_ISSET(0, &fds)) {
			x = read(0, &c, sizeof(c));
			if (x > 0) {
				(void)write(mfd, &c, sizeof(c));
			}
		}

		/*
		 * Stuff written to the PTY... display on stdout
		 */
		if (FD_ISSET(mfd, &fds)) {
			x = read(mfd, buf, sizeof(buf));
			if (x > 0) {
				(void)write(1, buf, x);
			}
		}
	}
	return(0);
}
