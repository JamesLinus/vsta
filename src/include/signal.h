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
#define SIG_ERR ((voidfun)(-3))

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

/*
 * Standard C signal functions
 */
extern voidfun signal(int, voidfun);
extern int kill(pid_t, int);
extern int raise(int);

/*
 * Function shared between waitpid() emulation and signal emulation
 */
extern void wait_child(void);

/*
 * Mask of signals, must have at least _NSIG bits (viva POSIX)
 */
typedef uint sigset_t;

/*
 * Signal handling description
 */
struct sigaction {
	voidfun sa_handler;	/* Handler function, SIG_DFL or SIG_IGN */
	sigset_t sa_mask;	/* Additional set of signals to be blocked */
	uint sa_flags;		/* Flags to affect behavior of signal */
};

/*
 * Bits in sa_flags
 */
#define SA_NOCLDSTOP 1	/* Do not generate SIGCHLD when children stop */

/*
 * Values for sigprocmask
 */
#define SIG_BLOCK 1	/* Block signals in 'set', other signals unaffected */
#define SIG_UNBLOCK 2   /* Unblock signals in 'set',  ,, */
#define SIG_SETMASK 3   /* New mask is 'set' */

/*
 * POSIX functions
 */
extern int sigemptyset(sigset_t *),
	sigfillset(sigset_t *),
	sigaddset(sigset_t *, int),
	sigdelset(sigset_t *, int),
	sigismember(sigset_t *, int),
	sigaction(int, struct sigaction *, struct sigaction *),
	sigprocmask(int, sigset_t *, sigset_t *),
	sigpending(sigset_t *),
	sigsuspend(sigset_t *);

/*
 * Other functions
 */
extern const char *strsignal(int);

/*
 * Internal infrastructure
 */
extern void __signal_save(char *);
extern char *__signal_restore(char *);
extern int __signal_size(void), __strtosig(const char *);

#endif /* _SIGNAL_H */
