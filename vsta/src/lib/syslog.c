/*
 * syslog.c
 *	A (very) poor man's syslog handler
 */
#include <sys/fs.h>
#include <syslog.h>
#include <stdio.h>
#include <std.h>

static char *id;		/* Name os process posting message */
static int logopt, logfacil;	/* Logging options & facility */

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
 * openlog()
 *	Initialize for syslog(), record identity
 */
void
openlog(char *ident, int opt, int facil)
{
	id = strdup(ident);
	logopt = opt;
	logfacil = facil;
}

/*
 * pidstr()
 *	Return string representation of PID
 *
 * Returns empty string of LOG_PID not specified
 */
static char *
pidstr(void)
{
	static ulong pid;
	static char str[16];

	if ((logopt & LOG_PID) == 0) {
		return("");
	}
	if (pid == 0) {
		pid = getpid();
		sprintf(str, "(pid %ld) ", pid);
	}
	return(str);
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
	va_list ap;

	p = path_open("CONS:0", ACC_WRITE);
	if (p < 0) {
		return;
	}
	fd = __fd_alloc(p);
	va_start(ap, msg);
	sprintf(buf, "syslog: %s %s%s: ",
		id ? id : "", pidstr(), levelmsg(level));
	vsprintf(buf + strlen(buf), msg, ap);
	if (buf[strlen(buf)-1] != '\n') {
		strcat(buf, "\n");
	}
	write(fd, buf, strlen(buf));
	close(fd);
}
