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
__fprintf(FILE *fp, const char *fmt, va_list argptr)
{
	char buf[BUFSIZ], *p, c;

	__doprnt(buf, fmt, (int *)argptr);
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
fprintf(FILE *fp, const char *fmt, ...)
{
	va_list ap;
	
	va_start(ap, fmt);	
	return(__fprintf(fp, fmt, ap));
}

/*
 * printf()
 *	Output to stdout
 *
 * This one is only used when you run printf() without using stdio.h.
 */
printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	return(__fprintf(stdout, fmt, ap));
}

/*
 * sprintf()
 *	Formatted output to a buffer
 */
sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	__doprnt(buf, fmt, (int *)ap);
	return(0);
}

/*
 * vfprintf()
 *	Formatted output to a FILE
 */
vfprintf(FILE *fp, const char *fmt, va_list ap)
{
	return(__fprintf(fp, fmt, ap));
}

/*
 * vprintf()
 *	Output to stdout
 *
 * This one is only used when you run printf() without using stdio.h.
 */
vprintf(const char *fmt, va_list ap)
{
	return(__fprintf(stdout, fmt, ap));
}

/*
 * vsprintf()
 *	Formatted output to a buffer
 */
vsprintf(char *buf, const char *fmt, va_list ap)
{
	__doprnt(buf, fmt, (int *)ap);
	return(0);
}

/*
 * perror()
 *	Print out error to stderr
 */
void
perror(const char *msg)
{
	char *p;

	p = strerror();
	fprintf(stderr, "%s: %s\n", (int)msg, p);
}
