#ifndef _SIGNAL_H
#define _SIGNAL_H
/*
 * signal.h
 *	A hokey little mapping from VSTa events into numbered signals
 */
#include <sys/types.h>

/*
 * Default and ignore signal "handlers"
 */
typedef voidfun sig_t;
#define SIG_DFL ((voidfun)(-1))
#define SIG_IGN ((voidfun)(-2))

/*
 * Some of these are not yet used in VSTa, but are included for completeness
 */
#define SIGHUP 1	/* Hangup */
#define SIGINT 2	/* Keyboard interrupt */
#define SIGQUIT 3	/* Keyboard abort */
#define SIGILL 4	/* Illegal instruction */
#define SIGTRAP 5
#define SIGABRT 6
#define SIGIOT SIGABRT
#define SIGUNUSED 7
#define SIGFPE 8	/* Floating point exception */
#define SIGKILL 9	/* Unmaskable kill */
#define SIGUSR1 10
#define SIGSEGV 11	/* Segmentation violation */
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15	/* Software termination */
#define SIGSTKFLT 16
#define SIGCHLD 17
#define SIGCLD SIGCHLD
/* #define SIGCONT 18	These make us appear to support job control */
/* #define SIGSTOP 19 */
/* #define SIGTSTP 20 */
/* #define SIGTTIN 21 */
/* #define SIGTTOU 22 */
#define SIGIO 23
#define SIGPOLL SIGIO
#define SIGURG SIGIO
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGLOST 29
#define SIGPWR 30
#define SIGBUS 31

#define _NSIG 32	/* Max # emulated signals */

extern voidfun signal(int, voidfun);
extern int kill(pid_t, int);

#endif /* _SIGNAL_H */
