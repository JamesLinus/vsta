/*
 * init.c
 *	Do initial program fire-up
 *
 * This includes mounting default filesystems and then working
 * out way through the init table, running stuff as needed.
 *
 * We go out of our way to read all input in units of lines; our
 * C library "get line" routines understand both VSTa (\n) and
 * DOS (\r\n) end-of-line conventions, and map to just '\n' for us.
 */
#include <sys/types.h>
#include <sys/ports.h>
#include <sys/wait.h>
#include <sys/fs.h>
#include <stdio.h>
#include <std.h>
#include <namer/namer.h>
#include <ctype.h>

#define INITTAB "/vsta/etc/inittab"	/* Table of stuff to run */
#define FSTAB "/vsta/etc/fstab"		/* Filesystems at boot */

/*
 * State associated with each entry in the inittab
 */
struct inittab {
	uint i_flags;		/* Flags for this entry */
	uint i_when;		/* When to fire it */
	ulong i_pid;		/* PID active for this slot */
	char **i_args;		/* Command+args for this slot */
};

/*
 * Bits for i_flags
 */
#define I_RAN 1		/* Entry has been run */
#define I_RUNNING 2	/* Entry is running now */

/*
 * Values for i_when
 */
#define I_BG 1		/* Run once in background */
#define I_FG 2		/* Run once in foreground */
#define I_AGAIN 3	/* Run in background, restart on exit */

/*
 * Our init tab entries and count
 */
static struct inittab *inittab = 0;
static int ninit = 0;

/*
 * read_inittab()
 *	Parse inittab and store in-core
 */
static void
read_inittab(void)
{
	FILE *fp;
	char *q, *p, buf[80], **argv;
	struct inittab *i = NULL;
	int argc;

	/*
	 * Open inittab.  Bail to standalone shell if missing.
	 */
	if ((fp = fopen(INITTAB, "r")) == NULL) {
		perror(INITTAB);
		execl("/vsta/bin/testsh", "testsh", (char *)0);
		exit(1);
	}

	/*
	 * Read lines and parse
	 */
	while (fgets(buf, sizeof(buf)-1, fp)) {

		/*
		 * Burst line, get another inittab entry
		 */
		buf[strlen(buf)-1] = '\0';
		p = strchr(buf, ':');
		if (p == 0) {
			printf("init: malformed line: %s\n", buf);
			continue;
		}
		*p++ = '\0';
		if (*p == '\0') {
			printf("init: missing command on line: %s\n", buf);
			continue;
		}
		ninit += 1;
		inittab = realloc(inittab, sizeof(struct inittab) * ninit);
		if (inittab == 0) {
			printf("init: out of memory reading inittab\n");
			exit(1);
		}
		i = &inittab[ninit-1];
		bzero(i, sizeof(struct inittab));

		/*
		 * Parse "when"
		 */
		if (!strcmp(buf, "fg")) {
			i->i_when = I_FG;
		} else if (!strcmp(buf, "bg")) {
			i->i_when = I_BG;
		} else if (!strcmp(buf, "again")) {
			i->i_when = I_AGAIN;
		} else {
			printf("init: '%s' uknown, assuming 'bg'\n", buf);
			i->i_when = I_BG;
		}

		/*
		 * Generate args
		 */
		argv = 0;
		argc = 0;
		while (p) {
			/*
			 * Find next word
			 */
			q = strchr(p+1, ' ');
			if (q) {
				*q++ = '\0';
				while (isspace(*q)) {
					++q;
				}
			}
			argc += 1;

			/*
			 * Reallocate argv, allocate a string to hold
			 * the argument.
			 */
			argv = realloc(argv, sizeof(char *) * (argc+1));
			if (argv == 0) {
				printf("init: out of mem for argv\n");
				exit(1);
			}
			argv[argc-1] = strdup(p);
			if (argv[argc-1] == 0) {
				printf("init: out of memory for arg string\n");
				exit(1);
			}

			/*
			 * Advance to next word
			 */
			p = q;
		}

		/*
		 * Last slot is NULL.  We've been making room for it
		 * by always realloc()'ing one too many slots.
		 */
		argv[argc] = NULL;

		/*
		 * Add it to the inittab entry
		 */
		i->i_args = argv;
	}
	fclose(fp);
}

/*
 * launch()
 *	Fire up an entry
 */
static void
launch(struct inittab *i)
{
	long pid;

retry:
	/*
	 * Try and launch off a new process
	 */
	pid = fork();
	if (pid == -1) {
		printf("init: fork failed\n");
		sleep(5);
		goto retry;
	}

	/*
	 * Child--execv() off
	 */
	if (pid == 0) {
		execv(i->i_args[0], i->i_args);
		perror(i->i_args[0]);
		_exit(1);
	}

	/*
	 * Parent--mark entry and return
	 */
	i->i_pid = pid;
	i->i_flags = I_RAN|I_RUNNING;
}

/*
 * do_wait()
 *	Wait for a child to die, update our state info
 */
static void
do_wait(void)
{
	struct exitst w;
	int x;

	/*
	 * Wait for child.  If none, pause a sec, then fall out
	 * so our caller can perhaps do something new.
	 */
	if (waits(&w) < 0) {
		sleep(1);
		return;
	}

	/*
	 * Got a dead child.  Riffle through our table and
	 * update whatever entry started him.
	 */
	for (x = 0; x < ninit; ++x) {
		if (inittab[x].i_pid == w.e_pid) {
			inittab[x].i_flags &= ~I_RUNNING;
			break;
		}
	}
}

/*
 * run()
 *	Run an inittab entry
 *
 * This routine handles all the fiddling of inittab entry flags
 * and such.  It also implements the semantics of fg, bg, and again.
 */
static void
run(struct inittab *i)
{
	switch (i->i_when) {
	case I_FG:
	case I_BG:
		/*
		 * One-shot; skip the entry once it's run
		 */
		if (i->i_flags & I_RAN) {
			return;
		}
		launch(i);

		/*
		 * Wait for its death if it's foreground
		 */
		if (i->i_when == I_FG) {
			while (i->i_flags & I_RUNNING) {
				do_wait();
			}
		}
		return;

	case I_AGAIN:
		if (i->i_flags & I_RUNNING) {
			return;
		}
		launch(i);
		return;

	default:
		printf("init: bad i_when\n");
		abort();
	}
}

main(void)
{
	port_t p;
	port_name pn;

	/*
	 * Fix our name
	 */
	set_cmd("init");

	/*
	 * A moment (2.5 sec) to let servers establish their ports
	 */
	__msleep(2500);

	/*
	 * Connect to console display and keyboard
	 */
	p = msg_connect(PORT_KBD, ACC_READ);
	(void)__fd_alloc(p);
	p = msg_connect(PORT_CONS, ACC_WRITE);
	(void)__fd_alloc(p);
	(void)__fd_alloc(p);

	/*
	 * Root filesystem
	 */
	for (;;) {
		pn = namer_find("fs/root");
		if (pn < 0) {
			printf("init: can't find root, sleeping\n");
			sleep(5);
		} else {
			break;
		}
	}
	p = msg_connect(pn, ACC_READ);
	if (p < 0) {
		printf("init: can't connect to root\n");
		exit(1);
	}
	mountport("/vsta", p);

	/*
	 * Read in inittab
	 */
	read_inittab();

	/*
	 * Forever run entries from table
	 */
	for (;;) {
		int x;

		for (x = 0; x < ninit; ++x) {
			run(&inittab[x]);
		}
		do_wait();
	}
}
