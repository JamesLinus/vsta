/*
 * linux.h
 *	Definition of compatibility routines for Linux
 */
#ifndef _LINUX_H
#define _LINUX_H
#include <sys/syscall.h>

/*
 * iopl()
 *	Set I/O privilege level
 *
 * We map this into access to all I/O ports
 */
#define iopl(x) enable_io(0, 0xFFFF)

/*
 * Stub values for TTY modes not supported
 */
#define IUCLC (0)
#define ONOCR (0)
#define VSUSP (0)

#endif /* _LINUX_H */
