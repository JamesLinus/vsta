/*
 * syslog.c
 *	A (very) poor man's syslog handler
 */
#include <sys/fs.h>
#include <syslog.h>
#include <stdio.h>

/*
 * levelmsg()
 *	Convert numeric level to string
 */
static char *
levelmsg(int level)
{
	switch (level) {
	case LOG_EMERG: return("emergency");
	case LOG_ALERT: return("alert");
	case LOG_CRIT: return("critical");
	case LOG_ERR: return("error");
	case LOG_WARNING: return("warning");
	case LOG_NOTICE: return("notice");
	case LOG_INFO: return("info");
	case LOG_DEBUG: return("debug");
	default: return("unknown");
	}
}

/*
 * syslog()
 *	Report error conditions
 *
 * We just dump them to the console
 */
void
syslog(int level, const char *msg, ...)
{
	port_t p;
	int fd;
	char buf[256];
	va_args ap;

	p = path_open("CONS:0", ACC_WRITE);
	if (p < 0) {
		return;
	}
	fd = __fd_alloc(p);
	va_start(ap, msg);
	sprintf(buf, "syslog: %s: ", levelmsg(level));
	sprintf(buf + strlen(buf), msg, ap);
	if (buf[strlen(buf)-1] != '\n') {
		strcat(buf, "\n");
	}
	write(fd, buf, strlen(buf));
	close(fd);
}
