/*
 * <sys/signal.h>
 *	Map to libc world
 *
 * In VSTa, the kernel doesn't do signals... they're emulated by the
 * C library.  We deflect from here to <signal.h>.
 */
#ifndef _SYS_SIGNAL_H
#define _SYS_SIGNAL_H

#include <signal.h>

#endif /* _SYS_SIGNAL_H */
