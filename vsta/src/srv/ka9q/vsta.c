/*
	FILE: vsta.c

	Written by Mikel Matthews, N9DVG
	SYS5 stuff added by Jere Sandidge, K4FUM
	VSTa port by Andy Valencia, WB6RRU
*/
#include <sys/types.h>
#include <termios.h>
#include <stdio.h>
#include <signal.h>
#include <stat.h>
#include <time.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <std.h>
#include <alloc.h>
#include <fcntl.h>
#include <syslog.h>
#include "global.h"
#include "config.h"
#include "cmdparse.h"
#include "iface.h"
#include "unix.h"
#include "vsta.h"

extern int detached;
extern struct session *consess;

#define	MAXCMD	1024

/*
 * Our private hack mutex package
 */
void
init_lock(lock_t *p)
{
	*p = 0;
}
void
p_lock(volatile lock_t *p)
{
	while (*p) {
		__msleep(20);
	}
	*p = 1;
}
void
v_lock(lock_t *p)
{
	*p = 0;
}

/*
 * Tabulation of daemons running
 */
#define MAXDAEMON (8)
static struct daemon {
	pid_t d_tid;	/* Thread ID for this daemon */
	voidfun d_fn;	/* Function to run */
} daemons[MAXDAEMON];

static pid_t curtid;	/* Current thread holding ka9q_lock */
lock_t ka9q_lock;	/* Mutex among threads */
static uint nbg;	/* # of background threads from yield() */
static int
	time_idx = -1,	/* Timer task thread index in daemons[] */
	cons_idx = -1;	/*  ...console watcher */

/*
 * Typeahead for console, a circular list
 */
static uchar kbchars[32];
static uint nkbchar, kbhd, kbtl;

extern int32 next_tick(void);
extern char *startup, *config, *userfile, *Dfile, *hosts, *mailspool;
extern char *mailqdir, *mailqueue, *routeqdir, *alias, *netexe;
#ifdef	_FINGER
extern char *fingerpath;
#endif
#ifdef	XOBBS
extern char *bbsexe;
#endif

/*
 * kick()
 *	Kick the target out of any sleep they might be in
 */
static void
kick(uint idx)
{
	pid_t tid = daemons[idx].d_tid;

	if (gettid() != tid) {
		(void)notify(0, tid, "recalc");
	}
}

/*
 * vsta_daemon()
 *	Start a daemon for the given function
 */
uint
vsta_daemon(voidfun fn)
{
	uint x;

	for (x = 0; x < MAXDAEMON; ++x) {
		if (daemons[x].d_fn == 0) {
			daemons[x].d_fn = fn;
			daemons[x].d_tid = tfork(fn, 0);
			return(x);
		}
	}
	printf("Too many daemons\n");
	_exit(1);
}

/*
 * vsta_daemon_done()
 *	Clear a daemon entry
 */
void
vsta_daemon_done(uint idx)
{
	daemons[idx].d_fn = 0;
	daemons[idx].d_tid = 0;
}

/*
 * fileinit()
 *	Initialize names of all the files
 */
fileinit(char *argv0)
{
	int el;
	char *ep, *cp;
	char tmp[MAXCMD];

	/* Get the name of the currently executing program */
	if ((cp = malloc((unsigned)(strlen(argv0) + 1))) == NULL) {
		perror("malloc");
	} else {
		sprintf(cp, "%s", argv0);
		netexe = cp;
	}

#ifdef	XOBBS
	/* Get the path to the W2XO BBS executable. */
	if ((ep = getenv("XOBBS")) == NULLCHAR) {
		bbsexe = "xobbs";
	} else {
		if ((cp = malloc((unsigned)(strlen(ep) + 2))) == NULL)
			perror("malloc");
		else {
			sprintf(cp, "%s", ep);
			bbsexe = cp;
		}
	}
#endif
	/* Try to get home directory name */
	if ((ep = getenv("NETHOME")) == NULLCHAR) {
		if ((ep = getenv("HOME")) == NULLCHAR) {
			ep = ".";
		}
	}
	el = strlen(ep);
	/* Replace each of the file name strings with the complete path */
	if (*startup != '/') {
		if ((cp = malloc((unsigned)(el + strlen(startup) + 2))) == NULL)
			perror("malloc");
		else {
			sprintf(cp, "%s/%s", ep, startup);
			startup = cp;
		}
	}

	if (*config != '/') {
		if ((cp = malloc((unsigned)(el + strlen(config) + 2))) == NULL)
			perror("malloc");
		else {
			sprintf(cp, "%s/%s", ep, config);
			config = cp;
		}
	}

	if (*userfile != '/') {
		if ((cp = malloc((unsigned)(el + strlen(userfile) + 2))) == NULL)
			perror("malloc");
		else {
			sprintf(cp, "%s/%s", ep, userfile);
			userfile = cp;
		}
	}

	if (*Dfile != '/') {
		if ((cp = malloc((unsigned)(el + strlen(Dfile) + 2))) == NULL)
			perror("malloc");
		else {
			sprintf(cp, "%s/%s", ep, Dfile);
			Dfile = cp;
		}
	}

	if (*hosts != '/') {
		if ((cp = malloc((unsigned)(el + strlen(hosts) + 2))) == NULL)
			perror("malloc");
		else {
			sprintf(cp, "%s/%s", ep, hosts);
			hosts = cp;
		}
	}

	if (*alias != '/') {
		if ((cp = malloc((unsigned)(el + strlen(alias) + 2))) == NULL)
			perror("malloc");
		else {
			sprintf(cp, "%s/%s", ep, alias);
			alias = cp;
		}
	}

#ifdef		_FINGER
	if (*fingerpath != '/') {
		if ((cp = malloc((unsigned)(el + strlen(fingerpath) + 2))) == NULL)
			perror("malloc");
		else {
			sprintf(cp, "%s/%s", ep, fingerpath);
			fingerpath = cp;
		}
	}
#endif

	/* Try to get home directory name */
	if ((ep = getenv("NETSPOOL")) == NULLCHAR)
		ep = "/usr/spool";
	el = strlen(ep);

	if (*mailspool != '/') {
		if ((cp = malloc((unsigned)(el + strlen(mailspool) + 2))) == NULL)
			perror("malloc");
		else {
			sprintf(cp, "%s/%s", ep, mailspool);
			mailspool = cp;
		}
	}

	if (*mailqdir != '/') {
		if ((cp = malloc((unsigned)(el + strlen(mailqdir) + 2))) == NULL)
			perror("malloc");
		else {
			sprintf(cp, "%s/%s", ep, mailqdir);
			mailqdir = cp;
		}
	}

	if (*mailqueue != '/') {
		if ((cp = malloc((unsigned)(el + strlen(mailqueue) + 2))) == NULL)
			perror("malloc");
		else {
			sprintf(cp, "%s/%s", ep, mailqueue);
			mailqueue = cp;
		}
	}

	if (*routeqdir != '/') {
		if ((cp = malloc((unsigned)(el + strlen(routeqdir) + 2))) == NULL)
			perror("malloc");
		else {
			sprintf(cp, "%s/%s", ep, routeqdir);
			routeqdir = cp;
		}
	}
}

/*
 * gun_all()
 *	Shoot down all threads, except our own
 */
static void
gun_all(void)
{
	uint x;
	pid_t pid, mytid = gettid();

	for (x = 0; x < MAXDAEMON; ++x) {
		if (pid = daemons[x].d_tid) {
			if (mytid != pid) {
				(void)notify(0, pid, "kill");
			}
		}
	}
}

/*
 * sysreset()
 *	Routine for remote reset
 */
sysreset()
{
	uint x;

	/*
	 * Gun down all of our threads
	 */
	gun_all();

	/*
	 * Close all files except stdin/out/err
	 */
	for (x = 3; x < getdtablesize(); ++x) {
		close(x);
	}

	/*
	 * Now restart our image
	 */
	execlp(netexe, netexe, 0);
	execlp("net", "net", 0);
	printf("reset failed: exiting\n");
	exit(1);
}

/*
 * do_mainloop()
 *	Call mainloop(), handle yield()'ed threads on return
 */
void
do_mainloop(void)
{
	pid_t mytid = gettid();
	extern void mainloop(void);

	/*
	 * Record our thread as holding the lock, and run the loop
	 */
	curtid = mytid;
	mainloop();

	/*
	 * We yield()'ed under mainloop() somewhere, and thus are
	 * no longer a needed thread.  Exit.
	 */
	if (curtid != mytid) {
		nbg -= 1;
		v_lock(&ka9q_lock);
		_exit(0);
	}
	curtid = 0;
}

/*
 * timechange()
 *	Handler "kicked" when a new time event is registered
 *
 * We don't have to do anything; interrupting our system call is
 * sufficient.
 */
static void
timechange(char *event)
{
	/*
	 * Do nothing on recalc event, die horribly on anything else
	 */
	if (strcmp(event, "recalc")) {
		printf("Event: %s\n", event);
		_exit(1);
	}
}

/*
 * recalc_timers()
 *	Kick the timer thread so he'll look at the timer queue again
 */
void
recalc_timers(void)
{
	kick(time_idx);
}

/*
 * timewatcher()
 *	Run things when next time event fires
 */
static void
timewatcher(void)
{
	int32 target;
	int x;
	struct time tm, tm2;

	/*
	 * Set up a handler for our "wakeup call", register our TID
	 */
	notify_handler(timechange);

	/*
	 * Endless server loop
	 */
	target = 0;
	for (;;) {
		/*
		 * Interlock, tally accumulated ticks,
		 * run the scheduler, then find out when
		 * we'd next like to wake up.
		 */
		p_lock(&ka9q_lock);
		if (target > 0) {
			tick(target);
		}

		/*
		 * Run the scheduler until things look idle.
		 * Idleness is defined as having an empty loopback,
		 * as well as no expired timed events.
		 */
		do {
			extern struct mbuf *loopq;

			do {
				do_mainloop();
			} while (loopq);
			target = next_tick();
		} while (target == 0);

		v_lock(&ka9q_lock);

		/*
		 * Sleep no less than 1 msec, nor more than 1 sec
		 */
		x = target*1000 / MSPTICK;
		if (x < 1) {
			x = 1;
		} else if (x > 1000) {
			x = 1000;
		}

		/*
		 * Sleep.  If interrupted, figure out how many ticks
		 * have passed.
		 */
		time_get(&tm);
		if (__msleep(x) < 0) {
			time_get(&tm2);
			x = (tm2.t_sec - tm.t_sec) * 1000;
			x = x + ((tm2.t_usec - tm.t_usec) / 1000);
			target = (x * MSPTICK) / 1000;
			if (target < 1) {
				target = 1;
			}
		}
	}
}

/*
 * conswatcher()
 *	Wait for typing on the control keyboard
 */
static void
conswatcher(void)
{
	struct termios cons;

	/*
	 * Go to single-character mode
	 */
	tcgetattr(0, &cons);
	cons.c_lflag &= ~(ICANON|ECHO);
	cons.c_cc[VMIN] = 1;
	cons.c_cc[VTIME] = 0;
	tcsetattr(0, TCSANOW, &cons);

	/*
	 * Read chars, make them available to kbread()
	 */
	for (;;) {
		char c;

		/*
		 * When we detach, this daemon is no longer
		 * needed, as telnet input traffic can drive
		 * everything quite nicely
		 */
		if (detached) {
			vsta_daemon_done(cons_idx);
			_exit(0);
		}
		if (read(0, &c, sizeof(c)) == sizeof(c)) {
			p_lock(&ka9q_lock);
			if (nkbchar < sizeof(kbchars)) {
				kbchars[kbhd++] = c;
				if (kbhd >= sizeof(kbchars)) {
					kbhd = 0;
				}
				nkbchar += 1;
			}
			do_mainloop();
			v_lock(&ka9q_lock);
		} else {
			sleep(1);
		}
	}
}

/*
 * detach_kbio()
 *	Called when KA9Q detaches from console
 *
 * For us, we need to kick conswatcher out of his read.
 */
void
detach_kbio(void)
{
	kick(cons_idx);
}

/*
 * eihalt()
 *	"Enable interrupts and halt"
 *
 * What this really does is wait for something to happen.  For VSTa,
 * this routine sets up threads on each source of data the first time
 * through.  Thereafter, when a thread finds data it takes the mutex
 * and runs mainloop() as if it was "the" KA9Q process.
 */
eihalt()
{
	/*
	 * Initialize syslog access
	 */
	(void)openlog("net", LOG_PID, LOG_DAEMON);

	/*
	 * Initialize the mutex
	 */
	init_lock(&ka9q_lock);

	/*
	 * Launch the initial threads.  The original thread dies here.
	 */
	time_idx = vsta_daemon(timewatcher);
	cons_idx = vsta_daemon(conswatcher);
	_exit(0);
}

/*
 * kbread()
 *	Return next character from keyboard FIFO, or -1
 */
kbread()
{
	int fd, x;
	uchar c;
	static int setup = 0;
	char buf[256];

	if (detached) {
		/*
		 * No session?
		 */
		if (!consess) {
			/*
			 * Session closed.  Close stdin/out/err, hold
			 * their place with /dev/null.
			 */
			if (setup) {
				close(0); close(1); close(2);
				fd = open("/dev/null", O_RDWR);
				if (fd >= 0) {
					dup2(fd, 0);
					dup2(fd, 1);
					dup2(fd, 2);
				}
				if (fd > 2) {
					close(fd);
				}
				setup = 0;
			}
			return(-1);
		}

		/*
		 * First-time setup
		 */
		if (!setup) {
			/*
			 * Use a FIFO memory buffer to capture I/O.
			 * For typout, it still goes to 1/2, and we
			 * pull it back out and forward over telnet.
			 *
			 * For input, we tell the telnet module to
			 * start stuff input to 0 (not 1 for display),
			 * and our kbread() pulls it back out.
			 */
			fd = fdmem((void *)0, 4096);
			if (fd < 0) {
				return(-1);
			}
			if (fd != 0) {
				dup2(fd, 0);
				close(fd);
			}
			fd = fdmem((void *)0, 4096);
			if (fd < 0) {
				return(-1);
			}
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2) {
				close(fd);
			}

			/*
			 * Have them write to FD 0 for "output".  Output
			 * from a telnet wants to actually become input
			 * to the command processor.
			 */
			tn_stdout(0);

			setup = 1;
		}

		/*
		 * If there's output, forward it to the TCP stream
		 */
		while ((x = read(1, buf, sizeof(buf))) > 0) {
			tn_send_con(buf, x);
		}

		/*
		 * If there's input, get the next byte
		 */
		x = read(0, &c, sizeof(c));

		/*
		 * Return the input now
		 */
		if (x > 0) {
			return(c);
		}
		return(-1);
	}

	/*
	 * Local console handling here.  It was received by the
	 * conswatcher daemon, and queued to kbchars[].
	 */
	if ((nbg > 0) || (nkbchar == 0)) {
		return(-1);
	}
	c = kbchars[kbtl++];
	if (kbtl >= sizeof(kbchars)) {
		kbtl = 0;
	}
	nkbchar -= 1;
	return(c);
}

/*
 * enable()/restore()
 *	No-op on VSTa port,  would be used to mask interrupts
 */
restore(char state)
{
}
char
disable()
{
}

/*ARGSUSED*/
stxrdy(dev)
int16 dev;
{
	return 1;
}

/*
 * wildcard()
 *	Wildcard filename lookup
 */
filedir(char *name, int times, char *ret_str)
{
	static char	dname[1024], fname[256];
	static DIR *dirp = NULL;
	struct dirent *dp;
	struct stat sbuf;
	char	*cp, temp[1024];

	/*
	 * Make sure that the NULL is there in case we don't find anything
	 */
	ret_str[0] = '\0';

	if (times == 0) {
		/* default a null name to *.* */
		if (name == NULL || *name == '\0')
			name = "*.*";
		/* split path into directory and filename */
		if ((cp = strrchr(name, '/')) == NULL) {
			strcpy(dname, ".");
			strcpy(fname, name);
		} else {
			strcpy(dname, name);
			dname[cp - name] = '\0';
			strcpy(fname, cp + 1);
			/* root directory */
			if (dname[0] == '\0')
				strcpy(dname, "/");
			/* trailing '/' */
			if (fname[0] == '\0')
				strcpy(fname, "*.*");
		}
		/* close directory left over from another call */
		if (dirp != NULL)
			closedir(dirp);
		/* open directory */
		if ((dirp = opendir(dname)) == NULL) {
			printf("Could not open DIR (%s)\n", dname);
			return;
		}
	} else {
		/* for people who don't check return values */
		if (dirp == NULL)
			return;
	}

	/* scan directory */
	while ((dp = readdir(dirp)) != NULL) {
		/* test for name match */
		if (wildmat(dp->d_name, fname)) {
			/* test for regular file */
			sprintf(temp, "%s/%s", dname, dp->d_name);
			if (stat(temp, &sbuf) < 0)
				continue;
			if ((sbuf.st_mode & S_IFMT) != S_IFREG)
				continue;
			strcpy(ret_str, dp->d_name);
			break;
		}
	}

	/* close directory if we hit the end */
	if (dp == NULL) {
		closedir(dirp);
		dirp = NULL;
	}
}

/*
 * doshell()
 *	Launch a shell below us
 */
doshell(int argc, char **argv)
{
	int	i, stat, pid, pid1;
	char	*cp, str[MAXCMD];
	voidfun savi, savc;

	str[0] = '\0';
	for (i = 1; i < argc; i++) {
		strcat(str, argv[i]);
		strcat(str, " ");
	}

	if ((cp = getenv("SHELL")) == NULL || *cp != '\0')
		cp = "/vsta/bin/sh";

	if ((pid = fork()) == 0) {
		if (argc > 1)
			(void)execl("/vsta/bin/sh", "sh", "-c", str, 0);
		else
			(void)execl(cp, cp, (char *)0, (char *)0, 0);
		perror("execl");
		exit(1);
	} else if (pid == -1) {
		perror("fork");
		stat = -1;
	} else {
		savi = signal(SIGINT, SIG_IGN);
		savc = signal(SIGCHLD, SIG_IGN);
		while ((pid1 = wait(&stat)) != pid && pid1 != -1)
			;
		signal(SIGINT, savi);
		signal(SIGCHLD, savc);
	}

	return stat;
}

/*
 * dodir()
 *	List files
 */
dodir(int argc, char **argv)
{
	int	i, stat;
	char	str[MAXCMD];

	strcpy(str, "ls -l ");
	for (i = 1; i < argc; i++) {
		strcat(str, argv[i]);
		strcat(str, " ");
	}

	stat = system(str);

	return stat;
}

/*
 * docd()
 *	Change directory, print new absolute path
 */
int
docd(int argc, char **argv)
{
	char tmp[MAXCMD];

	if (argc > 1) {
		if (chdir(argv[1]) == -1) {
			printf("Can't change directory\n");
			return 1;
		}
	}
	if (getcwd(tmp, sizeof(tmp)) != NULL)
		printf("%s\n", tmp);

	return 0;
}

/*
 * xmkdir()
 *	Create directory, set mode
 */
int
xmkdir(char *path, int mode)
{
	return(mkdir(path));
}

/*
 * mark_msec()
 *	Return a circular millisecond clock value
 *
 * Used by icmp ping for time stamps.
 */
int16
mark_msec(void)
{
	struct time tm;
	unsigned long long clk;

	/*
	 * Convert from absolute time to one which circles with
	 * each expiration of an int16's representation of milliseconds--
	 * 65.536 seconds.
	 */
	time_get(&tm);
	clk = (tm.t_sec * 1000) + (tm.t_usec / 1000);
	return((int16)clk);
}

/*
 * dir()
 *	Return FP on a listing of the given directory
 */
FILE *
dir(char *path, int full)
{
	char *buf, tmpf[32];
	FILE *fp;

	buf = alloca(strlen(path) + sizeof(tmpf));
	if (buf == 0) {
		return(0);
	}
	sprintf(tmpf, "/tmp/ftls%d", getpid());
	sprintf(buf, "ls%s %s > %s", full ? " -l" : "", path, tmpf);
	system(buf);
	fp = fopen(tmpf, "r");
	return(fp);
}

/* Called just before exiting to restore console state */
void
iostop()
{
	uint x;
	extern struct termios savecon;

	/*
	 * Stdout, unbuffered
	 */
	setbuf(stdout, NULLCHAR);

	/*
	 * Stop all interfaces
	 */
	while (ifaces != NULLIF) {
		if (ifaces->stop != NULLFP)
			(*ifaces->stop)(ifaces);
		ifaces = ifaces->next;
	}

	/*
	 * Restore TTY modes
	 */
	tcsetattr(0, TCSANOW, &savecon);

	/*
	 * Nail our threads
	 */
	gun_all();
}

/*
 * yield()
 *	Release thread of control
 *
 * This is called from the misbegotten places in the ka9q code where
 * a spinloop wants to run, waiting for action.  This routine tfork()'s
 * a new thread to take over this thread's old duties, then iteratively
 * sleep()'s and returns, allowing the spin loop to wait.  When this
 * thread returns back out of mainloop(), it is destroyed.
 */
void
yield(void)
{
	uint x;
	pid_t mytid = gettid();

	/*
	 * If this thread corresponds to one of our official ones,
	 * launch a new instance of it.
	 */
	for (x = 0; x < MAXDAEMON; ++x) {
		if (mytid == daemons[x].d_tid) {
			daemons[x].d_tid = tfork(daemons[x].d_fn, 0);

			/*
			 * Clear us as the current thread, and tally.
			 * Release lock so life can go on in the router.
			 */
			curtid = 0;
			nbg += 1;
			break;
		}
	}

	/*
	 * Pause
	 */
	v_lock(&ka9q_lock);
	__msleep(100);

	/*
	 * Take mutex back, and run again
	 */
	p_lock(&ka9q_lock);
}
