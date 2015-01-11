/*
 * err.c
 *	A BSD-ism for emitting error and warning messages
 */
#include <stdio.h>
#include <err.h>
#include <stdarg.h>
#include <errno.h>

extern const char *_progname;

static void
err_common(int eval, int code, const char *fmt, va_list ap, int do_exit)
{
	fprintf(stderr, "%s ", _progname);
	if (fmt) {
		vfprintf(stderr, fmt, ap);
	}
	fprintf(stderr, ": %s\n", strerror());
	if (do_exit) {
		exit(eval);
	}
}

void
err(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	err_common(eval, 0, fmt, ap, 1);
}

void
errc(int eval, int code, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	err_common(eval, code, fmt, ap, 1);
}

void
errx(int eval, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	err_common(eval, 0, fmt, ap, 1);
}

void
warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	err_common(0, 0, fmt, ap, 0);
}

void
warnc(int code, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	err_common(0, code, fmt, ap, 0);
}

void
warnx(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	err_common(0, 0, fmt, ap, 0);
}
