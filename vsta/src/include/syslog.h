#ifndef _SYSLOG_H
#define _SYSLOG_H
/*
 * syslog.h
 *	Definitions for system logging facility
 *
 * Be warned; VSTa ignores most of this
 */

/*
 * Main entry for reporting stuff
 */
extern void syslog(int, const char *, ...);

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

#endif /* _SYSLOG_H */
