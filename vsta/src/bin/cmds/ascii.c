/*
 * ascii.c
 *	Read keystrokes, show'em in ASCII
 *
 * To exit, do three q's in a row.
 */
#include <termios.h>
#include <stdio.h>

int
main(void)
{
	int nq = 0;
	struct termios old, new;
	char c;

	/*
	 * Help
	 */
	printf("To quit, type three 'q's (the letter q) in a row\n");

	/*
	 * Set char-at-a-time mode
	 */
	tcgetattr(0, &old);
	new = old;
	new.c_lflag &= ~(ICANON | ECHO | ISIG);
	new.c_cc[VMIN] = 1;
	new.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &new);

	/*
	 * Now read and display
	 */
	for (;;) {
		/*
		 * Get next char, exit on error
		 */
		if (read(0, &c, sizeof(c)) < 0) {
			break;
		}

		/*
		 * Count quit chars
		 */
		if (c == 'q') {
			if (++nq >= 3) {
				break;
			}
		} else {
			nq = 0;
		}

		/*
		 * Show it in a variety of forms
		 */
		(void)printf("%c\t%d\t0%o\t0x%x\n", c, c, c, c);
	}

	/*
	 * Clean up and done
	 */
	tcsetattr(0, TCSANOW, &old);
	return(0);
}
