#ifndef _SYSLOG_H
#define _SYSLOG_H
/*
 * syslog.h
 *	Definitions for system logging facility
 *
 * Be warned; VSTa ignores most of this
 */
#include <stdarg.h>

/*
 * Main entry for reporting stuff
 */
extern void syslog(int, const char *, ...),
	vsyslog(int, const char *, va_list),
	openlog(char *, int, int);

/*
 * Level values
 */
#define LOG_EMERG (1)		/* Panic */
#define LOG_ALERT (2)		/* Watch out */
#define LOG_CRIT (3)		/* Critical errors */
#define LOG_ERR (4)		/* Oops, but oh well */
#define LOG_WARNING (5)		/* You *were* warned */
#define LOG_NOTICE (6)		/* FYI */
#define LOG_INFO (7)		/* FYI too */
#define LOG_DEBUG (8)		/* Hacker's friend */

/*
 * Logopt bits
 */
#define LOG_PID (1)
#define LOG_CONS (2)
#define LOG_NDELAY (4)
#define LOG_NOWAIT LOG_NDELAY

/*
 * Facility
 */
#define LOG_KERN (1)
#define LOG_USER (2)
#define LOG_MAIL (3)
#define LOG_DAEMON (4)
#define LOG_AUTH (5)
#define LOG_LPR (6)
#define LOG_NEWS (7)
#define LOG_LOCAL (8)

#endif /* _SYSLOG_H */
