/*
 * cons.c
 *	Routines for serving a console display device
 */
#include <sys/types.h>
#include <cons/cons.h>
#include <sys/mman.h>

/*
 * Parameters for screen, filled in by init_screen()
 */
static char *top, *bottom, *cur, *lastl;

/*
 * cursor()
 *	Take current data position, update hardware cursor to match
 */
static void
cursor()
{
	ulong pos = (cur-top) >> 1;

	outportb(IDX, 0xE);
	outportb(DAT, (pos >> 8) & 0xFF);
	outportb(IDX, 0xF);
	outportb(DAT, pos & 0xFF);
}

/*
 * cls()
 *	Clear screen, home cursor
 */
static void
cls(void)
{
	ulong bl, *u;

	bl = BLANK;
	for (u = (ulong *)top; u < (ulong *)bottom; ++u)
		*u = bl;
	cur = top;
}

/*
 * init_screen()
 *	Set up mapping of PC screen
 */
void
init_screen(void)
{
	char *p;

	p = mmap((void *)DISPLAY, ROWS*COLS*CELLSZ,
		PROT_READ|PROT_WRITE, MAP_PHYS, 0, 0L);
	if (!p) {
		exit(1);
	}
	cur = top = p;
	bottom = p + (ROWS*COLS*CELLSZ);
	lastl = p + ((ROWS-1)*COLS*CELLSZ);
	cls(); cursor();
}

/*
 * scrollup()
 *	Scroll the screen up a line, blank last line
 */
static void
scrollup()
{
	ulong *u, bl;

	bcopy(top+(COLS*CELLSZ), top, (ROWS-1)*COLS*CELLSZ);
	bl = BLANK;
	for (u = (ulong *)lastl; u < (ulong *)bottom; ++u)
		*u = bl;
}

/*
 * write_string()
 *	Given a counted string, put the characters onto the screen
 */
void
write_string(char *s, int cnt)
{
	char c;
	int x;

	while (cnt--) {
		c = (*s++) & 0x7F;

		/*
		 * Printing characters are easy
		 */
		if ((c >= ' ') && (c < 0x7F)) {
			*cur++ = c;
			*cur++ = NORMAL;
			if (cur >= bottom) {	/* Scroll */
				scrollup();
				cur = lastl;
			}
			continue;
		}

		/*
		 * newline
		 */
		if (c == '\n') {
			/*
			 * Last line--just scroll
			 */
			if ((cur+COLS*CELLSZ) >= bottom) {
				scrollup();
				cur = lastl;
				continue;
			}

			/*
			 * Calculate address of start of next line
			 */
			x = cur-top;
			x = x + (COLS*CELLSZ - (x % (COLS*CELLSZ)));
			cur = top + x;
			continue;
		}

		/*
		 * carriage return
		 */
		if (c == '\r') {
			x = cur-top;
			x = x - (x % (COLS*CELLSZ));
			cur = top + x;
			continue;
		}

		/*
		 * \f--clear screen, home cursor.
		 * It ain't ANSI, but it works....
		 */
		if (c == '\f') {
			cls();
			continue;
		}

		/*
		 * \b--back up a space
		 */
		if (c == '\b') {
			if (cur > top) {
				cur -= CELLSZ;
			}
			continue;
		}

		/*
		 * Ignore other control characters
		 */
	}

	/*
	 * Only bother with cursor now that the whole string's
	 * on the screen.
	 */
	cursor();
}
