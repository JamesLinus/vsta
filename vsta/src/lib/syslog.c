/*
 * syslog.c
 *	A (very) poor man's syslog handler
 */
#include <sys/fs.h>
#include <syslog.h>

/*
 * Our bane of existence, varargs, and our way to avoid it
 */
#define ARG(start, idx) (*((void **)&(start) + (idx)))

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
syslog(int level, char *msg, ...)
{
	port_t p;
	int fd;
	char buf[256];

	p = path_open("CONS:0", ACC_WRITE);
	if (p < 0) {
		return;
	}
	fd = __fd_alloc(p);
	sprintf(buf, "syslog: %s: ", levelmsg(level));
	sprintf(buf + strlen(buf), msg,
		ARG(msg, 1), ARG(msg, 2), ARG(msg, 3),
		ARG(msg, 4), ARG(msg, 5), ARG(msg, 6));
	if (buf[strlen(buf)-1] != '\n') {
		strcat(buf, "\n");
	}
	write(fd, buf, strlen(buf));
	close(fd);
}
