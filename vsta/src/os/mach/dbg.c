/*
 * dbg.c
 *	A simple/simplistic debug interface
 *
 * Hard-wired to talk out either console or display.  If KDB isn't
 * configured, only the parts needed to display kernel printf()'s
 * are compiled in.
 */
#include <sys/misc.h>
#include "../mach/locore.h"

extern void cons_putc(int);
static int col = 0;		/* When to wrap */

#ifdef KDB
extern int cons_getc(void);
static char buf[80];		/* Typing buffer */
static int prlines = 0;		/* # lines since last paused */
#endif

/*
 * more()
 *	Sorta.  Pause if we've scrolled too much text
 */
static void
more(void)
{
#ifdef KDB
	if (++prlines < 23)
		return;
	(void)cons_getc();
	prlines = 0;
#endif
}

/*
 * putchar()
 *	Write a character to the debugger port
 *
 * Brain damage as my serial terminal doesn't wrap columns.
 */
void
putchar(int c)
{
	if (c == '\n') {
		col = 0;
		cons_putc('\r');
		cons_putc('\n');
		more();
	} else {
		if (++col >= 78) {
			cons_putc('\r'); cons_putc('\n');
			more();
			col = 1;
		}
		cons_putc(c);
	}
}

#ifdef KDB
/*
 * getchar()
 *	Get a character from the debugger port
 */
getchar(void)
{
	char c;

	prlines = 0;
	c = cons_getc() & 0x7F;
	if (c == '\r')
		c = '\n';
	return(c);
}

/*
 * gets()
 *	A spin-oriented "get line" routine
 */
void
gets(char *p)
{
	char c;
	char *start = p;

	putchar('>');
	for (;;) {
		c = getchar();
		if (c == '\b') {
			if (p > start) {
				printf("\b \b");
				p -= 1;
			}
		} else if (c == '') {
			p = start;
			printf("\\\n");
		} else {
			putchar(c);
			if (c == '\n')
				break;
			*p++ = c;
		}
	}
	*p = '\0';
}

/*
 * dbg_enter()
 *	Basic interface for debugging
 */
void
dbg_enter(void)
{
	spl_t was_on;
	extern void dbg_main(void);

	was_on = geti();
	cli();
	printf("[Kernel debugger @ spl %x]\n", was_on);
	dbg_main();
	if (was_on == SPL0) {
		sti();
	}
}
#endif /* KDB */
