/*
 * dbgio.c
 *	I/O port access
 */
#ifdef KDB
#include "../mach/locore.h"

extern char *strchr();

/*
 * dbg_inport()
 *	Input a byte from a port
 */
void
dbg_inport(char *p)
{
	int x, y;

	x = get_num(p);
	y = inportb(x);
	printf("0x%x -> 0x%x\n", x, y);
}

/*
 * dbg_outport()
 *	Output a byte to a port
 */
void
dbg_outport(char *p)
{
	char *val;
	int x, y;

	val = strchr(p, ' ');
	if (!val) {
		printf("Usage: outport <port> <value>\n");
		return;
	}
	*val++ = '\0';
	x = get_num(p);
	y = get_num(val);
	outportb(x, y);
	printf("0x%x <- 0x%x\n", x, y);
}

#endif /* KDB */
