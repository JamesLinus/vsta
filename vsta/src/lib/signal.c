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
#define MAP(s, evname) if (!strcmp(evname, e)) {return(s);};
	MAP(SIGINT, EINTR);
	MAP(SIGILL, EILL);
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
	case SIGILL: return(EILL);
	case SIGFPE: return(EMATH);
	case SIGKILL: return(EKILL);
	case SIGSEGV: return(EFAULT);
	case SIGTERM: return("term");
	default:
		return("badsig");
	}
}

/*
 * __handler()
 *	Handle VSTa event deliver, map onto signal() semantics
 */
static void
__handler(char *evstr)
{
	int s;
	voidfun vec;

	/*
	 * Convert to numeric
	 */
	s = __strtosig(evstr);

	/*
	 * Handle as appropriate
	 */
	switch (s) {
	case SIGINT:
	case SIGILL:
	case SIGFPE:
	case SIGSEGV:
		/*
		 * For an active handler, dispatch.  Otherwise fall
		 * into the unhandled case, where we terminate.
		 */
		vec = sigs[s];
		if (vec && (vec != SIG_DFL)) {
			/*
			 * SIG_IGN; ignore entirely
			 */
			if (vec == SIG_IGN) {
				return;
			}

			/*
			 * Invoke signal handler, and done
			 */
			(*vec)(s);
			return;
		}

		/* VVV fall into VVV */

	case SIGKILL:
	default:
		/*
		 * For unknown and fatal sigs, disable handler and
		 * terminate on the event
		 */
		notify_handler(0);
		notify(0, 0, evstr);
		break;
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

	if (s >= _NSIG) {
		return((voidfun)-1);
	}
	if (!init) {
		notify_handler(__handler);
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
