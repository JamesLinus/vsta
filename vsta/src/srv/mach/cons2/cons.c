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
	for (u = (ulong *)top; u < (ulong *)bottom; ++u) {
		*u = bl;
	}
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
	for (u = (ulong *)lastl; u < (ulong *)bottom; ++u) {
		*u = bl;
	}
}

/*
 * blankline()
 *	Blank line starting at "p"
 */
static void
blankline(void *p)
{
	ulong *q = p;
	uint x;

	for (x = 0; x < (COLS*CELLSZ/sizeof(long)); ++x) {
		*q++ = BLANK;
	}
}

/*
 * sequence()
 *	Called when we've decoded args and it's time to act
 */
static void
sequence(int x, int y, char c)
{
	char *p;

	switch (c) {
	case 'L':		/* Insert line */
		while (x-- > 0) {
			p = cur - ((cur-top) % (COLS*CELLSZ));
			bcopy(p, p+(COLS*CELLSZ), lastl-p);
			blankline(p);
		}
		return;

	case 'M':		/* Delete line */
		while (x-- > 0) {
			p = cur - ((cur-top) % (COLS*CELLSZ));
			bcopy(p+(COLS*CELLSZ), p, lastl-p);
			blankline(lastl);
		}
		return;

	case '@':		/* Insert character */
		while (x-- > 0) {
			y = cur-top;
			y = COLS*CELLSZ - (y % (COLS*CELLSZ));
			p = cur+y;
			bcopy(cur, cur+1, (p-cur)-1);
			*(ushort *)cur = BLANKW;
		}
		return;

	case 'P':		/* Delete character */
		while (x-- > 0) {
			y = cur-top;
			y = COLS*CELLSZ - (y % (COLS*CELLSZ));
			p = cur+y;
			bcopy(cur+1, cur, (p-cur)-1);
			p -= CELLSZ;
			*(ushort *)p = BLANKW;
		}
		return;

	case 'J':		/* Clear screen/eos */
		if (x == 1) {
			p = cur;
			while (p < bottom) {
				*(ushort *)p = BLANKW;
				p += CELLSZ;
			}
		} else {
			cls();
		}
		return;

	case 'H':		/* Position */
		cur = top + (x-1)*(COLS*CELLSZ) + (y-1)*CELLSZ;
		if (cur < top) {
			cur = top;
		} else if (cur >= bottom) {
			cur = lastl;
		}
		return;

	case 'K':		/* Clear to end of line */
		y = cur-top;
		y = COLS*CELLSZ - (y % (COLS*CELLSZ));
		p = cur+y;
		do {
			p -= CELLSZ;
			*(ushort *)p = BLANKW;
		} while (p > cur);
		return;

	default:
		/* Ignore */
		return;
	}
}

/*
 * do_multichar()
 *	Handle further characters in a multi-character sequence
 */
static
do_multichar(int state, char c)
{
	static int x, y;

	switch (state) {
	case 1:		/* Escape has arrived */
		switch (c) {
		case 'P':	/* Cursor down a line */
			return(0);
		case 'K':	/* Cursor left */
			cur -= CELLSZ;
			if (cur < top) {
				cur = top;
			}
			return(0);
		case 'H':	/* Cursor up */
			cur -= (COLS*CELLSZ);
			if (cur < top) {
				cur += (COLS*CELLSZ);
			}
			return(0);
		case 'M':	/* Cursor right */
			cur += CELLSZ;
			if (cur >= bottom) {
				cur -= CELLSZ;
			}
			return(0);
		case 'G':	/* Cursor home */
			cur = top;
			return(0);
		case '[':	/* Extended sequence */
			return(2);
		default:
			return(0);
		}

	case 2:		/* Seen Esc-[ */
		if (isdigit(c)) {
			x = c - '0';
			return(3);
		}
		sequence(1, 1, c);

	case 3:		/* Seen Esc-[<digit> */
		if (isdigit(c)) {
			x = x*10 + (c - '0');
			return(3);
		}
		if (c == ';') {
			y = 0;
			return(4);
		}
		sequence(x, 1, c);
		return(0);

	case 4:		/* Seen Esc-[<digits>; */
		if (isdigit(c)) {
			y = y*10 + (c - '0');
			return(4);
		}

		/*
		 * This wraps the sequence
		 */
		sequence(x, y, c);
		return(0);
	default:
#ifdef DEBUG
		abort();
#else
		return(0);
#endif
	}
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
	static int state = 0;

	while (cnt--) {
		c = (*s++) & 0x7F;

		/*
		 * If we are inside a multi-character sequence,
		 * continue
		 */
		if (state > 0) {
			state = do_multichar(state, c);
			continue;
		}

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
		 * \b--back up a space
		 */
		if (c == '\b') {
			if (cur > top) {
				cur -= CELLSZ;
			}
			continue;
		}

		/*
		 * \t--tab to next stop
		 */
		if (c == '\t') {
			/*
			 * Get current position
			 */
			x = cur-top;
			x %= (COLS*CELLSZ);

			/*
			 * Calculate steps to next tab stop
			 */
			x = (TABS*CELLSZ) - (x % (TABS*CELLSZ));

			/*
			 * Advance that many.  If we run off the end
			 * of the display, scroll and start at column
			 * zero.
			 */
			cur += x;
			if (cur >= bottom) {
				scrollup();
				cur = lastl;
			}
			continue;
		}

		/*
		 * Escape starts a multi-character sequence
		 */
		if (c == '\33') {
			state = 1;
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
