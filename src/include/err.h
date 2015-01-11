/*
 * err.h
 *	A BSD-ism for emitting error and warning messages
 */
#ifndef _ERR_H
#define _ERR_H

extern void err(int eval, const char *fmt, ...);
extern void errc(int eval, int code, const char *fmt, ...);
extern void errx(int eval, const char *fmt, ...);
extern void warn(const char *fmt, ...);
extern void warnc(int code, const char *fmt, ...);
extern void warnx(const char *fmt, ...);

#endif /* _ERR_H */
