#ifndef _SIGNAL_H
#define _SIGNAL_H
/*
 * signal.h
 *	A hokey little mapping from VSTa events into numbered signals
 */

/*
 * Default and ignore signal "handlers"
 */
typedef void (*__voidfun)();
#define SIG_DFL ((voidfun)(-1))
#define SIG_IGN ((voidfun)(-2))

#define SIGHUP 1	/* Hangup */
#define SIGINT 2	/* Keyboard interrupt */
#define SIGQUIT 3	/* Keyboard abort */
#define SIGILL 4	/* Illegal instruction */
#define SIGFPE 8	/* Floating point exception */
#define SIGKILL 9	/* Unmaskable kill */
#define SIGSEGV 11	/* Segmentation violation */
#define SIGTERM 15	/* Software termination */

#define _NSIG 16	/* Max # emulated signals */

extern __voidfun signal(int, __voidfun);

#endif /* _SIGNAL_H */
