#ifdef DEBUG
/*
 * crash.c
 *	Main routine for crash program
 */
#include <sys/types.h>
#include <mach/setjmp.h>

extern void dump_phys(), dump_virt(), dump_procs(), dump_pset(),
	dump_instr(), trace(), trapframe(), dump_vas(), dump_port();
static void quit(), calc(), set();

jmp_buf dbg_errjmp;		/* For aborting on error */

/*
 * Table of commands
 */
struct {
	char *c_name;	/* Name of command */
	voidfun c_fn;	/* Function to process the command */
} cmdtab[] = {
	"=", calc,
	"btrace", trace,
	"calc", calc,
	"di", dump_instr,
	"dp", dump_phys,
	"dv", dump_virt,
	"port", dump_port,
	"proc", dump_procs,
	"pset", dump_pset,
	"quit", quit,
	"set", set,
	"tframe", trapframe,
	"trace", trace,
	"vas", dump_vas,
	0, 0
};

/*
 * strncmp()
 *	Compare with limited count
 *
 * Returns 1 on mismatch, 0 on equal
 */
static
strncmp(char *p1, char *p2, int n)
{
	int x;

	for (x = 0; x < n; ++x) {
		if (p1[x] != p2[x]) {
			return(1);
		}
		if (p1[x] == '\0') {
			return(0);
		}
	}
	return(0);
}

/*
 * strchr()
 *	Return first position w. this char
 *
 * Not needed by kernel proper, so we implement here
 */
char *
strchr(char *p, char c)
{
	while (c != *p) {
		if (*p++ == '\0') {
			return(0);
		}
	}
	return(p);
}

/*
 * quit()
 *	Bye bye
 */
static void
quit()
{
	longjmp(dbg_errjmp, 2);
}

/*
 * calc()
 *	Print out value in multiple formats
 */
static void
calc(str)
	char *str;
{
	int x;
	extern int get_num();

	x = get_num(str);
	printf("%s 0x%x %d\n",
		symloc(x), x, x);
}

/*
 * do_cmd()
 *	Given command string, look up and invoke handler
 */
static void
do_cmd(str)
	char *str;
{
	int x, len, matches = 0, match;
	char *p;

	p = strchr(str, ' ');
	if (p)
		len = p-str;
	else
		len = strlen(str);

	for (x = 0; cmdtab[x].c_name; ++x) {
		if (!strncmp(cmdtab[x].c_name, str, len)) {
			++matches;
			match = x;
		}
	}
	if (matches == 0) {
		printf("No such command\n");
		return;
	}
	if (matches > 1) {
		printf("Ambiguous\n");
		return;
	}
	(*cmdtab[match].c_fn)(p ? p+1 : p);
}

dbg_main(void)
{
	char cmd[128], lastcmd[16];

 	/*
 	 * Top-level error catcher
 	 */
	switch (setjmp(dbg_errjmp)) {
	case 0:
		break;
	case 1:
 		printf("Reset to command mode\n");
		break;
	case 2:
		printf("[Returning from debugger]\n");
		return;
	}

 	/*
 	 * Command loop
 	 */
 	for (;;) {
		printf(">");
		if (gets(cmd) == 0) {
			quit();
		}

		/*
		 * Keep last command, insert it if they just hit return
		 */
		if (!cmd[0] && lastcmd[0]) {
			strcpy(cmd, lastcmd);
		} else {
			char *p, *q, c;

			p = cmd;	/* Copy up to first space */
			q = lastcmd;
			while (c = *p++) {
				if (c == ' ')
					break;
				*q++ = c;
			}
			*q = '\0';
		}
		do_cmd(cmd);
	}
	return(0);
}

/*
 * yyerror()
 *	Report syntax error and reset
 */
yyerror()
{
	printf("Syntax error in expression\n");
	longjmp(dbg_errjmp, 1);
}

/*
 * set()
 *	Set a symbol to a value
 */
static void
set(s)
	char *s;
{
	off_t o;
	char *n = s;
	extern void setsym();

	s = strchr(s, ' ');
	if (!s) {
		printf("Usage: set <name> <value>\n");
		longjmp(dbg_errjmp, 1);
	}
	*s = '\0'; ++s;
	setsym(n, get_num(s));
}
#endif
