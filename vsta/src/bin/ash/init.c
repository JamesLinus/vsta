/*
 * This file was generated by the mkinit program.
 */

#include "shell.h"
#include "mystring.h"
#include "eval.h"
#include "input.h"
#include "error.h"
#include "options.h"
#include "redir.h"
#include <signal.h>
#include "trap.h"
#include "output.h"
#include "memalloc.h"
#include "var.h"

#define MAXPWD 256
#define main echocmd
#define ALL (E_OPEN|E_CREAT|E_EXEC)
#define EV_EXIT 01		/* exit after evaluating tree */
#define EV_TESTED 02		/* exit status is checked; ignore -e flag */
#define EV_BACKCMD 04		/* command executing within back quotes */
#define SKIPBREAK 1
#define SKIPCONT 2
#define SKIPFUNC 3
#define _POSIX_SOURCE		/* try to find NGROUPS_MAX */
#define CMDTABLESIZE 31		/* should be prime */
#define ARB 1			/* actual size determined at run time */
#define NEWARGS 5
#define MAXLOGNAME 32
#define MAXPWLINE 128
#define EOF_NLEFT -99		/* value of parsenleft when EOF pushed back */
#define MAXMBOXES 10
#define PROFILE 0
#define MINSIZE 504		/* minimum size of a block */
#define DEFINE_OPTIONS
#define EOFMARKLEN 79
#define GDB_HACK 1 /* avoid local declarations which gdb can't handle */
#define EMPTY -2		/* marks an unused slot in redirtab */
#define PIPESIZE 4096		/* amount of buffering in a pipe */
#define S_DFL 1			/* default signal handling (SIG_DFL) */
#define S_CATCH 2		/* signal is caught */
#define S_IGN 3			/* signal is ignored (SIG_IGN) */
#define S_HARD_IGN 4		/* signal is ignored permenantly */
#define OUTBUFSIZ BUFSIZ
#define BLOCK_OUT -2		/* output to a fixed block of memory */
#define MEM_OUT -3		/* output to dynamically allocated memory */
#define OUTPUT_ERR 01		/* error occurred on output */
#define TEMPSIZE 24
#define VTABSIZE 39



extern int evalskip;		/* set if we are skipping commands */
extern int loopnest;		/* current loop nesting level */

extern void deletefuncs();

struct parsefile {
	int linno;		/* current line */
	int fd;			/* file descriptor (or -1 if string) */
	int nleft;		/* number of chars left in buffer */
	char *nextc;		/* next char in buffer */
	struct parsefile *prev;	/* preceding file on stack */
	char *buf;		/* input buffer */
};

extern int parsenleft;		/* copy of parsefile->nleft */
extern struct parsefile basepf;	/* top level input file */

extern pid_t backgndpid;	/* pid of last background process */
extern int jobctl;

extern int tokpushback;		/* last token pushed back */

struct redirtab {
	struct redirtab *next;
	short renamed[10];
};

extern struct redirtab *redirlist;

#define MAXSIG (64)
extern char sigmode[MAXSIG];	/* current value of signal */

extern void shprocvar();



/*
 * Initialization code.
 */

void
init() {

      /* from input.c: */
      {
	      extern char basebuf[];

	      basepf.nextc = basepf.buf = basebuf;
      }

#ifdef XXX
      /* from var.c: */
	{
		static char *imports[] =
			{"PATH", "TERM", "HOME", "USER", "PS1", 0};
		int x;

		initvar();
		for (x = 0; imports[x]; ++x) {
			char *p, *q;
			extern char *getenv();
			extern void *malloc();

			p = getenv(imports[x]);
			if (p) {
				q = malloc(strlen(p) + strlen(imports[x]) +
					4);
				if (q) {
					sprintf(q, "%s=%s", imports[x], p);
					setvareq(q, 0);
				}
				free(p);
			}
		}
	}
#else
	initvar();
#endif /* XXX */

}



/*
 * This routine is called when an error or an interrupt occurs in an
 * interactive shell and control is returned to the main command loop.
 */

void
reset() {

      /* from eval.c: */
      {
	      evalskip = 0;
	      loopnest = 0;
	      funcnest = 0;
      }

      /* from input.c: */
      {
	      if (exception != EXSHELLPROC)
		      parsenleft = 0;            /* clear input buffer */
	      popallfiles();
      }

      /* from parser.c: */
      {
	      tokpushback = 0;
      }

      /* from redir.c: */
      {
	      while (redirlist)
		      popredir();
      }

      /* from output.c: */
      {
	      out1 = &output;
	      out2 = &errout;
	      if (memout.buf != NULL) {
		      ckfree(memout.buf);
		      memout.buf = NULL;
	      }
      }
}



/*
 * This routine is called to initialize the shell to run a shell procedure.
 */

void
initshellproc() {

      /* from eval.c: */
      {
	      exitstatus = 0;
      }

      /* from exec.c: */
      {
	      deletefuncs();
      }

      /* from input.c: */
      {
	      popallfiles();
      }

      /* from jobs.c: */
      {
	      backgndpid = -1;
#if JOBS
	      jobctl = 0;
#endif
      }

      /* from options.c: */
      {
	      char *p;

	      for (p = optval ; p < optval + sizeof optval ; p++)
		      *p = 0;
      }

      /* from redir.c: */
      {
	      clearredir();
      }

      /* from trap.c: */
      {
	      char *sm;

	      clear_traps();
	      for (sm = sigmode ; sm < sigmode + MAXSIG ; sm++) {
		      if (*sm == S_IGN)
			      *sm = S_HARD_IGN;
	      }
      }

      /* from var.c: */
      {
	      shprocvar();
      }
}
