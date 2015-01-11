/*
 * mktemp.c
 *	Create temp file from template
 *
 * This doesn't co-exist well with read-only strings in C, but
 * needed for compatibility.
 */
#include <std.h>

/*
 * mktemp()
 *	Take template, write PID into trailing X's
 */
char *
mktemp(char *t)
{
	char *p;
	unsigned long pid;

	/*
	 * Get PID, but make sure it's an unsigned value
	 */
	pid = getpid();

	/*
	 * Point to tail of string
	 */
	p = t+strlen(t);

	/*
	 * Fill X's with PID
	 */
	while ((*--p == 'X') && (p >= t)) {
		*p = '0' + (pid % 10);
		pid /= 10;
	}
	return(t);
}
