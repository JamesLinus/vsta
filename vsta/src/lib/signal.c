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
static __voidfun sigs[_NSIG];

/*
 * strtosig()
 *	Convert string into signal number
 */
static
strtosig(char *e)
{
#define MAP(s, e) if (!strcmp(#s, e)) {return(s);};
	MAP(SIGINT, EINTR);
	MAP(SIGFPE, EMATH);
	MAP(SIGKILL, EKILL);
	MAP(SIGSEGV, EFAULT);
#undef MAP
	return(-1);
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
__voidfun
signal(int s, __voidfun v)
{
	static int init = 0;
	__voidfun ov;

	if (!init) {
		/* XXX wire handler */
	}
	if (s >= _NSIG) {
		return((__voidfun)-1);
	}
	ov = sigs[s];
	sigs[s] = v;
	return(ov);
}
