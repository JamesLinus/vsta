#include <stdio.h>
#include <fcntl.h>
#include <termios.h>

int
main(int argc, char **argv)
{
	char buf[80];
	int x, y, mfd, sfd;
	struct termios tc;

	if (argc < 2) {
		fprintf(stderr, "Usage is: %s <index>\n");
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
	 * Set both sides to raw
	 */
	tcgetattr(mfd, &tc);
	tc.c_lflag &= ~(ICANON | ECHO | ISIG);
	tc.c_cc[VMIN] = 1;
	tc.c_cc[VTIME] = 0;
	tcsetattr(mfd, TCSANOW, &tc);
	tcsetattr(sfd, TCSANOW, &tc);

	for (;;) {
		printf("Cmd: ");
		if (gets(buf) == NULL) {
			break;
		}
		switch (buf[0]) {
		case 'r':
			x = read((buf[1] == 's') ?
				sfd : mfd, buf, sizeof(buf));
			printf("Got %d bytes: '", x);
			if (x > 0) {
				for (y = 0; y < x; ++y) {
					putchar(buf[y]);
				}
			}
			printf("'\n");
			break;

		case 'w':
			x = write((buf[1] == 's') ?
				sfd : mfd, buf+1, strlen(buf+1));
			printf("Wrote %d bytes\n", x);
			break;

		case 'q':
			break;
		}
	}
	return(0);
}
