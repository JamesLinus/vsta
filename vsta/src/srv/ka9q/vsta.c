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
#include "global.h"
#include "config.h"
#include "cmdparse.h"
#include "iface.h"
#include "unix.h"

#define	MAXCMD	1024

/*
 * Our private hack mutex package
 */
typedef unsigned char lock_t;
static void
init_lock(lock_t *p)
{
	*p = 0;
}
static void
p_lock(volatile lock_t *p)
{
	while (*p) {
		__msleep(20);
	}
	*p = 1;
}
v_lock(lock_t *p)
{
	*p = 0;
}

/*
 * We post a thread to each source of data.  These are:
 */
static pid_t netpid,	/* Ethernet interface */
	conspid,	/* Controlling console */
	timepid;	/* Timing events */
			/* XXX TBD: serve TCP sockets, maybe UDP */
static char kbchar;	/* Current char available to kbread() */
static lock_t		/* Mutex among threads */
	ka9q_lock;

extern void mainloop(void);
extern int32 next_tick(void);
extern char *startup, *config, *userfile, *Dfile, *hosts, *mailspool;
extern char *mailqdir, *mailqueue, *routeqdir, *alias, *netexe;
extern void eth_recv_daemon(void);
#ifdef	_FINGER
extern char *fingerpath;
#endif
#ifdef	XOBBS
extern char *bbsexe;
#endif

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
 * gun()
 *	Shoot down the thread unless it's us
 */
gun(pid_t pid)
{
	pid_t mytid = gettid();

	if (mytid != pid) {
		(void)notify(0, pid, "kill");
	}
}

/*
 * sysreset()
 *	Routine for remote reset
 */
sysreset()
{
	int x;

	/*
	 * Gun down all of our threads
	 */
	gun(netpid);
	gun(conspid);
	gun(timepid);

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
 * timechange()
 *	Handler "kicked" when a new time event is registered
 *
 * We don't have to do anything; interrupting our system call is
 * sufficient.
 */
static void
timechange(void)
{
	/* No-op */
}

/*
 * recalc_timers()
 *	Kick the timer thread so he'll look at the timer queue again
 */
void
recalc_timers(void)
{
	(void)notify(0, timepid, "recalc");
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
	 * Set up a handler for our "wakeup call"
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
		mainloop();
		target = next_tick();
		v_lock(&ka9q_lock);

		/*
		 * Sleep no less than 1 msec, nor more than 1 sec
		 */
		if (target > 0) {
			/*
			 * Convert from ticks to msec
			 */
			x = target*1000 / MSPTICK;
			if (x < 1) {
				x = 1;
			} else if (x > 1000) {
				x = 1000;
			}
		} else {
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
 * netwatcher()
 *	Watch main network interface (i.e., ethernet) and pass packets
 */
static void
netwatcher(void)
{
	for (;;) {
		eth_recv_daemon();
		p_lock(&ka9q_lock);
		mainloop();
		v_lock(&ka9q_lock);
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

		if (read(0, &c, sizeof(c)) == sizeof(c)) {
			p_lock(&ka9q_lock);
			kbchar = c;
			mainloop();
			v_lock(&ka9q_lock);
		} else {
			sleep(1);
		}
	}
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
	 * Initialize the mutex
	 */
	init_lock(&ka9q_lock);

	/*
	 * Launch the three threads which always exist.  We use the
	 * main thread for the console watcher.
	 */
	netpid = tfork(netwatcher);
	timepid = tfork(timewatcher);
	conspid = gettid();
	conswatcher();
}

/*
 * kbread()
 *	Return next character from keyboard, or -1
 */
kbread()
{
	int c;

	if (kbchar) {
		c = kbchar;
		kbchar = 0;
	} else {
		c = -1;
	}
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
	gun(netpid);
	gun(conspid);
	gun(timepid);
}
