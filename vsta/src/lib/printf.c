/*
 * printf.c
 *	printf/sprintf implementations
 *
 * Use the underlying __doprnt() routine for their dirty work
 */
#include <stdio.h>
#include <std.h>
#include <sys/param.h>

extern void __doprnt();

/*
 * __fprintf()
 *	Formatted output to a FILE, with args in array form
 */
static
__fprintf(FILE *fp, char *fmt, int *argptr)
{
	char buf[BUFSIZ], *p, c;

	__doprnt(buf, fmt, argptr);
	p = buf;
	while (c = *p++) {
		putc(c, fp);
	}
	return(0);
}

/*
 * fprintf()
 *	Formatted output to a FILE
 */
fprintf(FILE *fp, char *fmt, int arg0, ...)
{
	return(__fprintf(fp, fmt, &arg0));
}

/*
 * printf()
 *	Output to stdout
 *
 * This one is only used when you run printf() without using stdio.h.
 */
printf(char *fmt, int arg0, ...)
{
	return(__fprintf(stdout, fmt, &arg0));
}

/*
 * sprintf()
 *	Formatted output to a buffer
 */
sprintf(char *buf, char *fmt, int arg0, ...)
{
	__doprnt(buf, fmt, &arg0);
	return(0);
}

/*
 * perror()
 *	Print out error to stderr
 */
void
perror(char *msg)
{
	char *p;

	p = strerror();
	fprintf(stderr, "%s: %s\n", (int)msg, p);
}
