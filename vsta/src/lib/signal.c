/*
 * Posix (and Standard C) signal handling functions.
 *
 * Signalling support includes generating SIGCHLD when necessary. Futhermore
 * partial suport for POSIX job control is present (actually only the ability
 * to send the signals when _POSIX_JOB_CONTROL is defined, and making sending
 * then illegal when it is not defined). 
 * 
 * For a correct behaviour, the signal state should be (re)stored when
 * exec-ing a program, in the same way that the fdl is stored.
 * This can be done by using __signal_size, __signal_save and __signal_restore.
 *
 * Missing features:
 *	- sending signals to arbitrary process groupts [kill(-pgid, signo)].
 *	- Per thread signal handling functions.
 *	- SIGCHLD will be send to the thread that enabled sending it (by
 *	  setting the action to something other than the default), not to
 *	  other threads.
 * BUG:
 *   When SIGCHLD is enabled and the process does a fork(), the sigchld thread
 *   will notice the exit() of the child, but it cannot send the signal to the
 *   main thread (ENOENT).
 */
#include <sys/fs.h>
#include <sys/wait.h>
#include <sys/sched.h>		/* For ephemeral threads */
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <event.h>
#include <std.h>

/* Options for signals */
#define NO_BLOCK  (0x01)
#define NO_IGNORE (0x02)
#define NO_CHANGE (0x04)
#define DO_WAKEUP (0x08)	/* Wakeup process when stopped, TBD */
#define NOT_SUP   (0x10)	/* Unsupported (for now) */

/*
 * Posix job control is not supported, thus the signals dealing with it are 
 * also no supported. 
 */
#define JC_NOT_SUPPORTED NOT_SUP

/*
 * sigset_t manipulation functions
 */
#if _NSIG > 32
# error "The number of signals > 32, need to rewrite sigset_t code"
#endif

#define BIT_MASK(n) (1<<(n))
#define ADD_BIT(val, n) ((val) |= BIT_MASK(n))
#define DEL_BIT(val, n) ((val) &= ~(BIT_MASK(n)))

/* All POSIX (1988) signal bits set */
#define POSIX_SET (\
                   BIT_MASK(SIGABRT) |\
		   BIT_MASK(SIGALRM) |\
		   BIT_MASK(SIGFPE)  |\
		   BIT_MASK(SIGHUP)  |\
		   BIT_MASK(SIGILL)  |\
		   BIT_MASK(SIGINT)  |\
		   BIT_MASK(SIGKILL) |\
		   BIT_MASK(SIGPIPE) |\
		   BIT_MASK(SIGQUIT) |\
		   BIT_MASK(SIGSEGV) |\
		   BIT_MASK(SIGTERM) |\
		   BIT_MASK(SIGUSR1) |\
		   BIT_MASK(SIGUSR2) |\
		   BIT_MASK(SIGCHLD))

/* Add these if we ever do POSIX job control:

		   BIT_MASK(SIGCONT) |\
		   BIT_MASK(SIGSTOP) |\
		   BIT_MASK(SIGTSTP) |\
		   BIT_MASK(SIGTTIN) |\
		   BIT_MASK(SIGTTOU)
 */


/*
 * When this variable is set, the process can continue when it 
 * was waiting using sigsuspend()
 */
static char sig_wakeup = 0;

/* thread id of the thread that enabled sending SIGCHLD */
static pid_t main_thread;

/* Signal mask, and pending signals */
static sigset_t signal_mask;
static sigset_t pending_signals;

static pid_t alarm_tosig;	/* Thread to be signalled on timer expire */
static pid_t alarm_thread = -1;	/*  ...alarm thread, if any */

/*
 * Should we send a SIGCHLD signal for stopped children? 
 *     This variable is currently never used (we set it in sigaction, but
 *     never test its value), as we don't support POSIX job control.
 *     (therefore we'll never have stopped children). 
 */
static int send_sigchild_on_stop = 0;

/* Default signal handlers */
static void sigexit(int),	/* exit normally */
	sigcore(int),		/* extit abnormally */
	sigstop(int),		/* Stop current thread, for job control */
	sigignore(int);		/* Ignore signal */

/* Forward reference */
static int kill2(pid_t pid, pid_t tid, int sig);

/*
 * Table with information about known signals. Not all of these are standard
 * (Std. C or POSIX) signals.
 *
 * For signals marked with a '?' commment  I don't know what they mean, let alone 
 * what their default action should be.
 */
static struct signal {
	int sig_number;
	char *sig_event;
	voidfun sig_default;
	voidfun sig_current;
	sigset_t sig_current_mask;
	uint sig_flags;
} signals[] = {
	{0,   "unsupp",   sigcore,    0, 0, NO_BLOCK | NO_CHANGE | NOT_SUP},
	{SIGHUP,    "hup",    sigexit,    0, 0, 0},
	{SIGINT,    EINTR,    sigexit,    0, 0, 0},
	{SIGQUIT,   "quit",   sigcore,    0, 0, 0},
	{SIGILL,    "instr",  sigcore,    0, 0, 0},
	{SIGTRAP,   "trap",   sigcore,    0, 0, 0},
	{SIGABRT,   "abort",  sigcore,    0, 0, 0},
	{7,   "unsupp",   sigcore,    0, 0, NO_BLOCK | NO_CHANGE | NOT_SUP},
	{SIGFPE,    EMATH,    sigcore,    0, 0, 0},
	{SIGKILL,   EKILL,    sigexit,    0, 0,
		NO_BLOCK | NO_CHANGE | NO_IGNORE},
	/* Note: EKILL is recognized by the kernel, and will kill the thread */
	{SIGUSR1,   "usr1",   sigexit,    0, 0, 0},
	{SIGSEGV,   EFAULT,   sigcore,    0, 0, 0},
	{SIGUSR2,   "usr2",   sigexit,    0, 0, 0},
	{SIGPIPE,   "pipe",   sigexit,    0, 0, 0},
	{SIGALRM,   "alarm",  sigexit,    0, 0, 0},
	{SIGTERM,   "term",   sigexit,    0, 0, 0},
	{SIGSTKFLT, "stkflt", sigignore,  0, 0, 0}, /* ? */
	{SIGCHLD,   "child",  sigignore,  0, 0, 0},
#ifdef SIGCONT
	{SIGCONT,   "cont",   sigignore,  0, 0, DO_WAKEUP | JC_NOT_SUPPORTED},
	{SIGSTOP,   "stop",   sigstop,    0, 0,
		NO_BLOCK | NO_IGNORE | JC_NOT_SUPPORTED},
	{SIGTSTP,   "tstop",  sigstop,    0, 0, JC_NOT_SUPPORTED},
	{SIGTTIN,   "ttin",   sigstop,    0, 0, JC_NOT_SUPPORTED},
	{SIGTTOU,   "ttou",   sigstop,    0, 0, JC_NOT_SUPPORTED},
#else
	{18,   "unsupp",   sigcore,    0, 0, NO_BLOCK | NO_CHANGE | NOT_SUP},
	{19,   "unsupp",   sigcore,    0, 0, NO_BLOCK | NO_CHANGE | NOT_SUP},
	{20,   "unsupp",   sigcore,    0, 0, NO_BLOCK | NO_CHANGE | NOT_SUP},
	{21,   "unsupp",   sigcore,    0, 0, NO_BLOCK | NO_CHANGE | NOT_SUP},
	{22,   "unsupp",   sigcore,    0, 0, NO_BLOCK | NO_CHANGE | NOT_SUP},
#endif
	{SIGIO,     "IO",     sigignore,  0, 0, 0},  /* ? */
	{SIGXCPU,   "xcpu",   sigcore,    0, 0, 0},
	{SIGXFSZ,   "xfsz",   sigcore,    0, 0, 0},
	{SIGVTALRM, "vtalrm", sigexit,    0, 0, 0},
	{SIGPROF,   "prof",   sigexit,    0, 0, 0},
	{SIGWINCH,   "winch", sigignore,  0, 0, 0},
	{SIGLOST,   "lost",   sigignore,  0, 0, 0}, /* ? */
	{SIGPWR,    "pwr",    sigignore,  0, 0, 0},
	{SIGBUS,    "bus",    sigcore,    0, 0, 0},
	{0,         0,        0,          0, 0, 0}
};

/*
 * notify_thread()
 *	Send an event to the indicated thread ID (of the current process)
 */
static int
notify_thread(pid_t thread_id, const char *event)
{
	return(notify(0, thread_id, event));
}

/*
 * sigset_isempty()
 *	Tell if signal set is empty
 */
static int
sigset_isempty(sigset_t *set)
{
	return ((set == 0) || (*set == 0));
}

/*
 * sigset_remove()
 *	Remove the indicated signals
 */
static int
sigset_remove(sigset_t *set, sigset_t *change_set)
{
	if ((set == 0) || (change_set == 0)) {
		return(__seterr(EFAULT));
	}
	*set &= ~(*change_set);
	return(0);
}

/*
 * sigset_common()
 *	AND of existing and new
 */
static int
sigset_common(sigset_t *set, sigset_t *change_set)
{
	if ((set == 0) || (change_set == 0)) {
		return(__seterr(EFAULT));
	}
	*set &= *change_set;
	return(0);
}

/*
 * sigset_add()
 *	OR in signals
 */
static int
sigset_add(sigset_t *set, sigset_t *change_set)
{
	if ((set == 0) || (change_set == 0)) {
		return(__seterr(EFAULT));
	}
	*set |= *change_set;
	return(0);
}

/*
 * sigemptyset()
 *	Clear all signals from set
 */
int
sigemptyset(sigset_t *set)
{
	if (set == 0) {
		return(__seterr(EFAULT));
	}
	*set = 0;
	return(1);
}

/*
 * sigfillset()
 *	Set all signals in set
 */
int
sigfillset(sigset_t *set)
{
	if (set == 0) {
		return(__seterr(EFAULT));
	}
	*set = POSIX_SET;
	return(1);
}

/*
 * sigaddset()
 *	Add a signal to the set
 */
int
sigaddset(sigset_t *set, int signo)
{
	if ((signo < 0) || (signo >= _NSIG)) {
		return(__seterr(EINVAL));
	}
	if (set == 0) {
		return(__seterr(EFAULT));
	}
	ADD_BIT(*set, signo);
	return(1);
}
	
/*
 * sigdelset()
 *	Remove signal from set
 */
int
sigdelset(sigset_t *set, int signo)
{
	/* Remove signo from the set */
	if ((signo < 0) || (signo >= _NSIG)) {
		return(__seterr(EINVAL));
	}
	if (set == 0) {
		return(__seterr(EFAULT));
	}
	DEL_BIT(*set, signo);
	return(1);
}

/*
 * sigismember()
 *	Tell if signal is in set
 */
int
sigismember(sigset_t *set, int signo)
{
	if ((signo < 0) || (signo >= _NSIG)) {
		return(__seterr(EINVAL));
	}
	if (set == 0) {
		return(__seterr(EFAULT));
	}
	return((*set) & BIT_MASK(signo));
}

/*
 * VSTa doesn't use signals but events. An event is denoted by a string,
 * instead of a number as in (POSIX) signals. 
 * The kernel support for events is simple: A process can register a single
 * handler function, and it is possible to send events to processes/threads.
 */

/* Default signal handlers */

/*
 * sigignore()
 *	Ignore signal
 */
static void
sigignore(int signo)
{
	/* Ignore signal */ ;
}

/*
 * sigstop()
 *	No job control, so "shouldn't happen"
 */
static void
sigstop(int signo)
{
	raise(SIGKILL);
}

/*
 * sigcore()
 *	Dump core
 */
static void
sigcore(int signo)
{
	/*
	 * VSTa doesn't do core dumping, so we just kill ourselves
	 */
	raise(SIGKILL);
}

/*
 * sigexit()
 *	Termination of process
 */
static void
sigexit(int signo)
{
	raise(SIGKILL);
}

/*
 * __strtosig()
 *	Map event string onto signal number
 */
int
__strtosig(const char *event)
{
	int i;

	for (i = 0; signals[i].sig_event != 0; ++i) {
		if (strcmp(signals[i].sig_event, event) == 0) {
			return(i);
		}
	}

	/* Default: */
	return(SIGINT); 
}

/*
 * signal_to_event()
 *	Map signal number onto event string
 */
static const char *
signal_to_event(int signo)
{
	if ((signo < 0) || (signo >= _NSIG)) {
		return("instr");
	}
	return(signals[signo].sig_event);
}

/*
 * signal_event_handler()
 *	Event handler function
 *
 * This function is used to catch 
 * the interesting events (i.e. those that map onto signals)
 * and than dispatch to the signal handling function.
 */
static void
signal_event_handler(const char *event)
{
	int i, sig = -1;
	sigset_t save_set = signal_mask;
	voidfun sigfun;

	/*
	 * Find signal number and index in the signal table
	 */
	for (i = 0; signals[i].sig_event != 0; ++i) {
		if (strcmp(signals[i].sig_event, event) == 0) {
			sig = i;
			break;
		}
	}

	/*
	 * Not recognized signal, ignore
	 */
	if (sig == -1) {
		return;
	}   

	/*
	 * Not supported signals are ignored (it should not be 
	 * possible to generate them anyway).
	 */
	if (signals[sig].sig_flags & NOT_SUP) {
		return;
	}

	/*
	 * Blocked signal
	 */
	if (sigismember(&signal_mask, sig)) {
		sigaddset(&pending_signals, sig);
		return;
	} 

	/*
	 * This is what we call
	 */
	sigfun = signals[sig].sig_current;

	/*
	 * Block it while we're processing it
	 */
	sigset_add(&signal_mask, &signals[sig].sig_current_mask);
	sigaddset(&signal_mask, sig);

	/*
	 * Invoke the function
	 */
	(*sigfun)(sig);

	/*
	 * Restore and continue
	 */
	signal_mask = save_set;
	sig_wakeup = 1;
}

/*
 * siginit()
 *	Initialize signal subsystem 
 */
static void
siginit(void)
{
	static int init_done = 0;
	int i;
	struct signal *s;

	/*
	 * Avoid initializing more than once
	 */
	if (init_done) {
		return;
	}
	init_done = 1;

	/*
	 * Initialize current handler, and add hander to event hash
	 */
	for (i = 0, s = signals; s->sig_event != 0; ++i, ++s) {
#ifdef DEBUG
		if (i != s->sig_number) {
			abort();
		}
#endif
		s->sig_current = s->sig_default;
		sigemptyset(&s->sig_current_mask);
		sigemptyset(&signal_mask);
		sigemptyset(&pending_signals);
		handle_event(s->sig_event, signal_event_handler);
	}
}

/*
 * sigchild_thread_func()
 *	Watch for exiting children, send SIGCHLD
 */
static void
sigchild_thread_func(int dummy)
{
	const char *sigchld;

	(void)sched_op(SCHEDOP_EPHEM, 0);
	sigchld = signal_to_event(SIGCHLD);
	for (;;) {
		wait_child();
		(void)notify_thread(main_thread, sigchld);
	}
}

/*
 * map_handler()
 *	Convert our internal handler representation into the external one
 */
static voidfun
map_handler(struct signal *s, voidfun h)
{
	if (h == s->sig_default) {
		return(SIG_DFL);
	}
	if (h == sigignore) {
		return(SIG_IGN);
	}
	return(h);
}

/*
 * sigaction()
 *	Select an action for the given signal
 */
int
sigaction(int sig, struct sigaction *act, struct sigaction *oact)
{
	static pid_t child_thread = -1;
	int i;
	struct signal *s;

	/*
	 * Find entry in the signal table
	 */
	if ((sig < 0) || (sig >= _NSIG)) {
		return(__seterr(EINVAL));
	}
	s = &signals[sig];

	/*
	 * Invalid signal number
	 */
	if (s->sig_flags & NOT_SUP) {
		return(__seterr(ENOSYS));
	}

	/*
	 * One-time init
	 */
	siginit();

	/*
	 * If oact set, fill in current action
	 */
	if (oact != 0) {
		oact->sa_handler = map_handler(s, s->sig_current);
		oact->sa_mask = s->sig_current_mask;
		oact->sa_flags = 0;
	}

	/*
	 * If not setting a new action, all done
	 */
	if (act == 0) {
		return(0);
	}

	/*
	 * Sanity check
	 */
	if (s->sig_flags & NO_CHANGE) {
		return(__seterr(EINVAL));
	} 
	if (s->sig_flags & NO_IGNORE) {
		if (act->sa_handler == SIG_IGN) {
			return(__seterr(EINVAL));
		}
	}	

	if (act->sa_handler == SIG_IGN) {
		s->sig_current = sigignore;
		if ((sig == SIGCHLD) && (child_thread != -1)) {
			notify_thread(child_thread, "kill");
			child_thread = -1;
		}
	} else if (act->sa_handler == SIG_DFL) {
		s->sig_current = s->sig_default;
		if ((sig == SIGCHLD) && (child_thread != -1)) {
			notify_thread(child_thread, "kill");
			child_thread = -1;
		}
	} else {
		s->sig_current = act->sa_handler;
		if ((sig == SIGCHLD) && (child_thread == -1)) {
			main_thread = gettid();
			child_thread = tfork(sigchild_thread_func, 0);
		}
	}

	s->sig_current_mask = act->sa_mask;

	if (sig == SIGCHLD) {
		send_sigchild_on_stop = (act->sa_mask & SA_NOCLDSTOP) == 0;
	}
	return(0);
}

/*
 * sigprocmask()
 *	Set signal mask for process
 */
int
sigprocmask(int how, sigset_t *nset, sigset_t *oset)
{
	int i;
	sigset_t set;

	/*
	 * One-time init
	 */
	siginit();

	/*
	 * Return current signal mask
	 */
	if (oset) {
		*oset = signal_mask;
	}

	if (nset) {
		set = *nset;

		/*
		 * Quietly remove all non-blockable signals.
		 * (POSIX says we should do this quietly)
		 */
		for (i = 0; signals[i].sig_event != 0; ++i) {
			if (signals[i].sig_flags & NO_BLOCK) {
				sigdelset(&set, i);
			}
		}

		switch (how) {
		case SIG_BLOCK:
			sigset_add(&signal_mask, &set);
			break;
		case SIG_UNBLOCK:
			sigset_remove(&signal_mask, &set);
			break;
		case SIG_SETMASK:
			signal_mask = set;
			break;
		default:
			return(__seterr(EINVAL));
		}
	}

	/*
	 * Deliver all pending, but now unblocked, signals
	 */
	for (i = 0; i < _NSIG; ++i) {
		sigset_t save_set = signal_mask;
		voidfun sigfun = signals[i].sig_current;

		if (!sigismember(&pending_signals, i) ||
				sigismember(&signal_mask, i)) {
			continue;
		}

		sigdelset(&pending_signals, i);
		sigset_add(&signal_mask, &signals[i].sig_current_mask);
		sigaddset(&signal_mask, i);
		sigfun(i);
		signal_mask = save_set;
	}
	return(1);
}

/*
 * sigpending()
 *	Check if there are signals in the 'set' pending
 */
int
sigpending(sigset_t *set)
{
	sigset_t tmp_set;

	if (set == 0) {
		return(__seterr(EFAULT));
	}
	tmp_set = pending_signals;
	sigset_common(&tmp_set, set);
	return(sigset_isempty(&tmp_set));
}

/*
 * pause()
 *	Wait for any, not blocked signal, Standard C
 */
int
pause(void)
{
	return(sigsuspend(&signal_mask));
}

/*
 * sigsuspend()
 *	Suspend waiting for a signal to happen
 *
 * Includes atomic adjustment of signal mask
 */
int
sigsuspend(sigset_t *set)
{
	sigset_t old_set;

	if (set == 0) {
		return(__seterr(EFAULT));
	}

	old_set = signal_mask;
	sigset_remove(&old_set, set);
	if (sigpending(&old_set)) {
		int i;

		for (i = 0; i < _NSIG; i++) {
			voidfun sigfun;
			sigset_t save_set;

			if (!sigismember(&pending_signals, i) ||
					!sigismember(&old_set, i)) {
				continue;
			}

			sigfun = signals[i].sig_current;
			save_set = signal_mask;

			sigdelset(&pending_signals, i);
			sigset_add(&signal_mask, &signals[i].sig_current_mask);
			sigaddset(&signal_mask, i);
			sigfun(i);
			signal_mask = save_set;
		}
		return(1);
	}

	/*
	 * Loop until a signal is delivered (not just
	 * until an event is sent,
	 * the event may not be a signal and it may
	 * be a blocked signal).
	 * TBD: there's a race condition here.
	 */
	sigprocmask(SIG_SETMASK, set, &old_set);
	sig_wakeup = 0;
	while (!sig_wakeup) {
		sleep(200000);
	}

	/*
	 * restore signal mask
	 */
	sigprocmask(SIG_SETMASK, &old_set, 0);
	return(0);
}

/*
 * kill()
 *	Send signal to a process
 */
int
kill(pid_t pid, int sig)
{
	/*
	 * Process group
	 */
	if (pid == 0) {
		return(kill2(-1, 0, sig));
	}

	/*
	 * Send to process group '-pid' 
	 * Not implementable using VSTa interface.
	 */
	if (pid < 0) {
		__seterr(EINVAL);
		return(-1);
	}

	return(kill2(pid, 0, sig));
}

/*
 * kill2
 *	Send event to specific thread
 */
static int
kill2(pid_t pid, pid_t tid, int sig)
{
	struct signal *s;

	/*
	 * Find index in the table
	 */
	if ((sig < 0) || (sig >= _NSIG)) {
		__seterr(EINVAL);
		return(-1);
	}
	s = &signals[sig];

	/*
	 * Unsupported signal
	 */
	if (s->sig_flags & NOT_SUP) {
		__seterr(EINVAL);
		return(-1);
	}

	return(notify(pid, tid, s->sig_event));
}

/*
 * raise
 *	Standard C version of 'kill'
 *
 * Equavalent to kill(getpid(), signo). As the signal doesn't 
 * travel outside the current process, we can directly call
 * the event handler, without using the kernel to do it.
 * The only exception is SIGKILL, the event "kill" will be 
 * caught by the kernel.
 */
int
raise(int signo)
{
	struct signal *s;

	if ((signo < 0) || (signo >= _NSIG)) {
		__seterr(EINVAL);
		return(-1);
	}
	s = &signals[signo];

	if (s->sig_flags & NOT_SUP) {
		__seterr(EINVAL);
		return(-1);
	}

	/*
	 * SIGKILL is special, it should kill the current process
	 */
	if (signo == SIGKILL) {
		return(notify(0, 0, EKILL));
	}

	signal_event_handler(s->sig_event);
	return(1);
}

/*
 * signal
 *	Set signal handler, Standard C version of sigaction()
 */
voidfun
signal(int s, voidfun sigfun)
{
	struct sigaction signew, sigold;

	signew.sa_handler = sigfun;
	sigemptyset(&signew.sa_mask);
	signew.sa_flags   = 0;
	if (sigaction(s, &signew, &sigold) == -1) {
		return(SIG_ERR);
	} else {
		return(sigold.sa_handler);
	}
}

/*
 * alarm_thread_func
 *	Sleep 'alarm_seconds' seconds, and then send signal to 'thread'
 */
static void
alarm_thread_func(int alarm_seconds)
{
	sched_op(SCHEDOP_EPHEM, 0);
	__msleep(alarm_seconds * 1000);
	notify_thread(alarm_tosig, signal_to_event(SIGALRM));
	alarm_thread = -1;
	_exit(0);
}

/*
 * alarm
 *	Send an alarm In 'seconds' seconds
 *
 * The number of
 * seconds untill the expire of the existing timer is returned.
 * The return value will be 0 when no timer is present, when
 * 'seconds' is 0, no timer will be started.
 */
uint
alarm(unsigned int seconds)
{
	time_t now = time(0);
	uint time_remaining;
	static time_t alarm_time;	/* When was last alarm started */
	static uint alarm_seconds;	/* Time value last used */

	/*
	 * Kill alarm thread
	 */
	if (alarm_thread != -1) {
		notify_thread(alarm_thread, signal_to_event(SIGKILL));
		alarm_thread = -1;

		/*
		 * Calculate remaining time
		 */
		time_remaining = (alarm_time+alarm_seconds) - now;
	} else {
		time_remaining = 0;
	}

	if (seconds > 0) {
		alarm_time = time(0);
		alarm_tosig = gettid();
		alarm_seconds = seconds;
		alarm_thread = tfork(alarm_thread_func, seconds);
		return((alarm_thread >= 0) ? time_remaining : -1);
	} else {
		return(time_remaining);
	}
}

/*
 * __signal_size()
 *	Store/read signal info in/from memory
 *
 * Used to implement signal handling across exec()
 */
int
__signal_size(void)
{
	return(_NSIG);
}

/*
 * These encode signal actions as written to the state saved
 * between exec()'s.
 */
#define BL_IGN 'a'
#define NOTBL_IGN 'b'
#define BL_DFL 'c'
#define NOTBL_DFL 'd'
#define BL_IGN_PENDING 'e'
#define BL_DFL_PENDING 'f'

/*
 * encode_sig()
 *	Encode a signal into its representative state
 */
static char
encode_sig(int sig)
{
	voidfun handler = signals[sig].sig_current;
	int blocked = sigismember(&signal_mask, sig);
	int pending = sigismember(&pending_signals, sig);

	/*
	 * Either the process has ignored the
	 * signal, or a handler is present.
	 * If a handler is present, this should
	 * be restored to its default after
	 * the exec, as the new process might
	 * not have the same handlers.
	 */
	if (handler == sigignore) {
		return(blocked ?
			(pending ? BL_IGN : BL_IGN_PENDING) : NOTBL_IGN);
	} else {
		return(blocked ?
			(pending ? BL_DFL : BL_DFL_PENDING) : NOTBL_DFL);
	}
}

/*
 * decode_sig()
 *	Extract signal state previously encoded in encode_sig()
 */
static void
decode_sig(int sig, char c)
{
	switch (c) {
	case BL_IGN_PENDING:
		sigaddset(&pending_signals, sig);
		/* VVV Fall through */
	case BL_IGN: 
		sigaddset(&signal_mask, sig);
		/* VVV Fall through */
	case NOTBL_IGN:
		signals[sig].sig_current = sigignore;
		break;
	case BL_DFL_PENDING:
		sigaddset(&pending_signals, sig);
		/* VVV Fall through */
	case BL_DFL:
		sigaddset(&signal_mask, sig);
		/* VVV Fall through */
	case NOTBL_DFL: 
	default: /* Set all unknown ones to SIG_DFL */
		signals[sig].sig_current = signals[sig].sig_default;
		break;
	}
}

/*
 * __signal_save()
 *	Save signal state
 */
void
__signal_save(char *p)
{
	int i;

	for (i = 0; i < _NSIG; i++) {
		p[i] = encode_sig(i);
	}
}

/*
 * __signal_restore()
 *	Restore previously saved signal state
 */
char *
__signal_restore(char *p)
{
	int i;

	siginit();

	/*
	 * Don't touch anything if we don't appear to have saved
	 * signal state.
	 */
	if ((p == 0) || (*p == 0)) {
		return(p);
	}

	/*
	 * Restore each signal
	 */
	for (i = 0; i < _NSIG; ++i) {
		decode_sig(i, p[i]);
	}

	return(p + _NSIG);
}

/*
 * strsignal()
 *	Give string name for signal
 *
 * Note that this isn't the VSTa fault string value; it's the ASCII display
 * of the C #define name.
 */
const char *
strsignal(int sig)
{
	switch (sig) {
	case SIGHUP: return("SIGHUP");
	case SIGINT: return("SIGINT");
	case SIGQUIT: return("SIGQUIT");
	case SIGILL: return("SIGILL");
	case SIGTRAP: return("SIGTRAP");
	case SIGABRT: return("SIGABRT");
	case SIGUNUSED: return("SIGUNUSED");
	case SIGFPE: return("SIGFPE");
	case SIGKILL: return("SIGKILL");
	case SIGUSR1: return("SIGUSR1");
	case SIGSEGV: return("SIGSEGV");
	case SIGUSR2: return("SIGUSR2");
	case SIGPIPE: return("SIGPIPE");
	case SIGALRM: return("SIGALRM");
	case SIGTERM: return("SIGTERM");
	case SIGSTKFLT: return("SIGSTKFLT");
	case SIGCHLD: return("SIGCHLD");
#ifdef SIGCONT
	case SIGCONT: return("SIGCONT");
	case SIGSTOP: return("SIGSTOP");
	case SIGTSTP: return("SIGTSTP");
	case SIGTTIN: return("SIGTTIN");
	case SIGTTOU: return("SIGTTOU");
#endif
	case SIGIO: return("SIGIO");
	case SIGXCPU: return("SIGXCPU");
	case SIGXFSZ: return("SIGXFSZ");
	case SIGVTALRM: return("SIGVTALRM");
	case SIGPROF: return("SIGPROF");
	case SIGWINCH: return("SIGWINCH");
	case SIGLOST: return("SIGLOST");
	case SIGPWR: return("SIGPWR");
	case SIGBUS: return("SIGBUS");
	default: return("Unknown");
	}
}

/*
 * killpg()
 *	Send signal to process group of PID
 */
int
killpg(pid_t pid, int sig)
{
	return(kill2(pid, -1, sig));
}
