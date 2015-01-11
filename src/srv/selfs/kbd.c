/*
 * kbd.c
 *	Test typical select() usage against a keyboard
 */
#include <stdio.h>
#include <stddef.h>
#include <select.h>
#include <time.h>
#include <termios.h>

int
main(int argc, char **argv)
{
	struct termios tio, tin;
	struct timeval tv;
	fd_set rd;
	char c;
	int x;

	/*
	 * The usual single char mode
	 */
	tcgetattr(0, &tio);
	tin = tio;
	tin.c_lflag &= ~(ECHO | ISIG | ICANON);
	tin.c_cc[VMIN] = 1;
	tin.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &tin);

	/*
	 * Now get typing or timeouts
	 */
	for (;;) {
		FD_ZERO(&rd);
		FD_SET(0, &rd);
		tv.tv_sec = 5;
		tv.tv_usec = 0;
		x = select(1, &rd, NULL, NULL, &tv);
		if (x < 0) {
			perror("select");
			break;
		}
		if (x == 0) {
			printf("timeout\n");
			continue;
		}
		x = read(0, &c, sizeof(c));
		if (x < 0) {
			perror("read");
			break;
		}
		if (x != 1) {
			printf("read returned %d?!?\n", x);
		}
		if (c == '\n') {
			break;
		}
		(void)printf("got: '%c'\n", c);
	}
	tcsetattr(0, TCSANOW, &tio);
	return(0);
}
