/*
 * getpass.c
 *	Turn off echo, get a password
 */
#include <termios.h>

/*
 * getpass()
 *	Get password
 */
char *
getpass(char *prompt)
{
	struct termios old, new;
	char c;
	static char buf[64];
	int len = 0;

	tcgetattr(0, &old);
	bcopy(&old, &new, sizeof(old));
	new.c_lflag &= ~(ECHO | ICANON);
	new.c_cc[VMIN] = 1;
	new.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &new);

	/*
	 * Print out prompt
	 */
	write(1, prompt, strlen(prompt));

	/*
	 * Read string
	 */
	for (;;) {
		/*
		 * Get next char
		 */
		if (read(0, &c, sizeof(c)) != 1) {
			break;
		}

		/*
		 * Data chars
		 */
		if (c >= ' ') {
			if (len < (sizeof(buf)-1)) {
				buf[len++] = c;
			}
			continue;
		}

		/*
		 * Backspace?
		 */
		if (c == old.c_cc[VERASE]) {
			if (len > 0) {
				(void)write(1, "\b \b", 3);
				len -= 1;
			}
			continue;
		}

		/*
		 * Kill?
		 */
		if (c == old.c_cc[VKILL]) {
			while (len > 0) {
				(void)write(1, "\b \b", 3);
				len -= 1;
			}
			continue;
		}

		/*
		 * Ignore, with beep
		 */
		(void)write(1, "\7", 1);
	}

	/*
	 * Null-terminate, restore TTY, and hand it back.
	 */
	buf[len] = '\0';
	tcsetattr(0, TCSANOW, &old);
	return(buf);
}
