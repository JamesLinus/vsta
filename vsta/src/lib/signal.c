/*
 * signal.c
 *	Emulation of POSIX-style signals from VSTa events
 *
 * Amusing to map, since VSTa events are strings.  More thought on
 * what to do with the events which have no POSIX signal number.
 */
#include <signal.h>
#include <sys/fs.h>

/*
 * A slot for each signal
 */
static voidfun sigs[_NSIG];

/*
 * __strtosig()
 *	Convert string into signal number
 */
__strtosig(char *e)
{
#define MAP(s, e) if (!strcmp(#s, e)) {return(s);};
	MAP(SIGINT, EINTR);
	MAP(SIGFPE, EMATH);
	MAP(SIGKILL, EKILL);
	MAP(SIGSEGV, EFAULT);
#undef MAP
	return(SIGINT);	/* Default */
}

/*
 * sigtostr()
 *	Convert number back to string
 */
static char *
sigtostr(int s)
{
	switch (s) {
	case SIGHUP: return("hup");
	case SIGINT: return(EINTR);
	case SIGQUIT: return("quit");
	case SIGILL: return("instr");
	case SIGFPE: return(EMATH);
	case SIGKILL: return(EKILL);
	case SIGSEGV: return(EFAULT);
	case SIGTERM: return("term");
	default:
		return("badsig");
	}
}

/*
 * signal()
 *	Arrange signal handling
 */
voidfun
signal(int s, voidfun v)
{
	static int init = 0;
	voidfun ov;

	if (!init) {
		/* XXX wire handler */
	}
	if (s >= _NSIG) {
		return((voidfun)-1);
	}
	ov = sigs[s];
	sigs[s] = v;
	return(ov);
}

/*
 * kill()
 *	Send an event
 */
kill(pid_t pid, int sig)
{
	return(notify(pid, 0, sigtostr(sig)));
}
