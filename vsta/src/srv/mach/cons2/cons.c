/*
 * cons.c
 *	Routines for serving a console display device
 */
#include <sys/types.h>
#include "cons.h"
#include <sys/mman.h>
#include <std.h>
#include <ctype.h>
#include <mach/io.h>
#include <sys/assert.h>
#include <time.h>

/*
 * Parameters for screen, filled in by init_screen()
 */
static char *phystop, *top, *physbottom, *bottom, *cur, *lastl;
char *hw_screen;
static int idx, dat, display;
static ushort beep_port = 0x61;
static struct screen *active_screen;

/*
 * Per-screen state for VT-100 scroll regions
 */
struct scroll {
	uint sc_nextreg;	/* Next available scroll region # */
	uint sc_regs[ROWS];	/* Region # per line of display */
};

/*
 * Forward declarations
 */
static void jump_cur(char *), blankline(void *p);

/*
 * LINE()
 *	Given screen position pointer, return pointer to start of its line
 */
#define LINE(pos) ((char *)(pos) - (((char *)(pos)-phystop) % (LINESZ)))

/*
 * load_screen()
 *	Switch to new screen image as stored in "s"
 */
void
load_screen(struct screen *s)
{
	bcopy(s->s_img, hw_screen, SCREENMEM);
	s->s_curimg = hw_screen;
	set_screen(s);
	cursor();
}

/*
 * save_screen()
 *	Dump screen image to memory in "s"
 */
void
save_screen(struct screen *s)
{
	bcopy(hw_screen, s->s_img, SCREENMEM);
}

/*
 * save_screen_pos()
 *	Save just cursor position
 */
void
save_screen_pos(struct screen *s)
{
	s->s_pos = cur-phystop;
}

/*
 * get_region()
 *	Given scroll regions and current position, return top/last
 *
 * Note bottom pointer would be derived from last pointer by adding
 * LINESZ.
 */
static void
get_region(struct scroll *sc, char **topp, char **lastlp)
{
	int x, reg, line = (cur - phystop) / LINESZ;
	char *t, *b;

	/*
	 * Calculate top and bottom lines of this scroll region.
	 */
	t = b = LINE(cur);
	reg = sc->sc_regs[line];
	for (x = line-1; x >= 0; --x) {
		if (sc->sc_regs[x] != reg) {
			break;
		}
		t -= LINESZ;
	}
	for (x = line+1; x < ROWS; ++x) {
		if (sc->sc_regs[x] != reg) {
			break;
		}
		b += LINESZ;
	}

	/*
	 * Return values
	 */
	*topp = t;
	*lastlp = b;
}

/*
 * set_screen()
 *	Cause the emulator to start using the named memory as the display
 */
void
set_screen(struct screen *s)
{
	/*
	 * Remember current display screen
	 */
	active_screen = s;

	/*
	 * Configure physical bounds of memory
	 */
	phystop = s->s_curimg;
	physbottom = phystop + SCREENMEM;
	top = bottom = lastl = NULL;
	jump_cur(phystop + s->s_pos);
}

/*
 * cursor()
 *	Take current data position, update hardware cursor to match
 */
void
cursor(void)
{
	ulong pos = (cur-phystop) >> 1;

	outportb(idx, 0xE);
	outportb(dat, (pos >> 8) & 0xFF);
	outportb(idx, 0xF);
	outportb(dat, pos & 0xFF);
}

/*
 * clear_screen()
 *	Apply unenhanced blanks to all of a screen
 */
void
clear_screen(char *p)
{
	ulong bl, *u, *bot;

	bl = BLANK;
	bot = (ulong *)(p+SCREENMEM);
	for (u = (ulong *)p; u < bot; ++u) {
		*u = bl;
	}
}

/*
 * cls()
 *	Clear screen, home cursor
 */
static void
cls(void)
{
	clear_screen(phystop);
	jump_cur(phystop);
}

/*
 * init_screen()
 *	Set up mapping of PC screen
 *
 * We accept either VID_MGA (mono) or VID_CGA (colour) for the type field
 */
void
init_screen(int type)
{
	char *p;
	struct screen *s;

	/*
	 * Establish the video adaptor parameters
	 */
	if (type == VID_MGA) {
		idx = MGAIDX;
		dat = MGADAT;
		display = MGATOP;
	} else {
		idx = CGAIDX;
		dat = CGADAT;
		display = CGATOP;
	}

	/*
	 * Open physical device, make it the current display
	 * destination.
	 */
	p = mmap((void *)display, SCREENMEM, 
		 PROT_READ|PROT_WRITE, MAP_PHYS, 0, 0L);
	if (!p) {
		exit(1);
	}
	hw_screen = p;
	s = SCREEN(0);
	s->s_curimg = hw_screen;

	/*
	 * Move to screen 0, and clear it
	 */
	set_screen(s);
	cls();

	/*
	 * Move the cursor down some.  This leaves room for the
	 * kernel debugger to lurch to life due to a boot task
	 * dying, without scrolling the syslog output off the
	 * screen.
	 */
	write_string("\n\n\n", 3);

	/*
	 * Update cursor, and we're ready to go!
	 */
	cursor();
}

/*
 * scrollup()
 *	Scroll the screen up a line, blank last line
 */
static void
scrollup()
{
	bcopy(top + LINESZ, top, lastl - top);
	blankline(lastl);
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

	for (x = 0; x < (LINESZ/sizeof(long)); ++x) {
		*q++ = BLANK;
	}
}

/*
 * scroll_region()
 *	Configure terminal scroll regions
 */
static void
scroll_region(int x, int y)
{
	struct screen *s = active_screen;
	struct scroll *sc = s->s_scroll;

	if (!x || ((x == 1) && (y == ROWS))) {
		/*
		 * If ESC-[r, or encompasses whole screen, deconfigure
		 * any scroll region.
		 */
		free(sc);
		s->s_scroll = NULL;
	} else {
		uint reg;

		/*
		 * If first scroll region, allocate the scroll region
		 * data structure.
		 */
		if (sc == NULL) {
			sc = s->s_scroll = calloc(1, sizeof(struct scroll));
		}

		/*
		 * Get next scroll region index number
		 */
		sc->sc_nextreg += 1;
		reg = sc->sc_nextreg;

		/*
		 * Bound range of lines affected
		 */
		if (x < 1) {
			x = 1;
		} else if (x > ROWS) {
			x = ROWS;
		}
		if (y < 1) {
			y = 1;
		} else if (y > ROWS) {
			y = ROWS;
		}

		/*
		 * Walk range, updating scroll region value
		 */
		x -= 1;
		y -= 1;
		while (x <= y) {
			sc->sc_regs[x] = reg;
			x += 1;
		}
	}

	/*
	 * Now recalculate top/bottom/lastl
	 */
	top = bottom = lastl = NULL;
	jump_cur(cur);
}

/*
 * backspace()
 *	Back up a character position
 */
static void
backspace(struct screen *s)
{
	if (cur > top) {
		if (s->s_onlast) {
			s->s_onlast = 0;
		} else {
			cur -= CELLSZ;
		}
	}
}

/*
 * Bound "col" to 1..COLS, and "row" to 1..ROWS.  Note 1-based!
 */
#define BOUND(col, row) \
		if (col < 1) { col = 1; \
		} else if (col > COLS) { col = COLS; } \
		if (row < 1) { row = 1; \
		} else if (row > ROWS) { row = ROWS; }

/*
 * sequence()
 *	Called when we've decoded args and it's time to act
 *
 * This handles ESC-[ [<num>] [;<num>]] <operator>
 */
static void
sequence(int x, int y, char c)
{
	char *p;
	struct screen *s = active_screen;

	/*
	 * Cap/sanity
	 */
	if (x > COLS) {
		x = COLS;
	}

	/*
	 * Dispatch command
	 */
	switch (c) {

	case 'A':	/* Cursor up */
		while (x-- > 0) {
			cur -= LINESZ;
			if (cur < top) {
				cur += LINESZ;
				break;
			}
		}
		break;

	case 'B':	/* Cursor down a line */
		while (x-- > 0) {
			cur += LINESZ;
			if (cur >= bottom) {
				cur -= LINESZ;
				break;
			}
		}
		break;

	case 'C':	/* Cursor right */
		while (x-- > 0) {
			cur += CELLSZ;
			if (cur >= bottom) {
				cur -= CELLSZ;
				break;
			}
		}
		break;

	case 'D':	/* Cursor left */
		while (x-- > 0) {
			backspace(s);
		}
		break;

	case 'L':		/* Insert line */
		while (x-- > 0) {
			p = LINE(cur);
			bcopy(p, p + LINESZ, lastl-p);
			blankline(p);
		}
		break;

	case 'M':		/* Delete line */
		while (x-- > 0) {
			p = LINE(cur);
			bcopy(p + LINESZ, p, lastl-p);
			blankline(lastl);
		}
		break;

	case '@':		/* Insert character */
		s->s_onlast = 0;
		while (x-- > 0) {
			y = cur-top;
			y = LINESZ - (y % LINESZ);
			p = cur+y;
			bcopy(cur, cur+CELLSZ, (p-cur)-CELLSZ);
			*(ushort *)cur = BLANKW;
		}
		break;

	case 'P':		/* Delete character */
		while (x-- > 0) {
			y = cur-top;
			y = LINESZ - (y % LINESZ);
			p = cur+y;
			bcopy(cur+CELLSZ, cur, (p-cur)-CELLSZ);
			p -= CELLSZ;
			*(ushort *)p = BLANKW;
		}
		break;

	case 'J':		/* Clear screen/eos */
		if (x == 1) {
			p = cur;
			while (p < physbottom) {
				*(ushort *)p = BLANKW;
				p += CELLSZ;
			}
		} else {
			cls();
		}
		break;

	case 'H':		/* Position */
		/*
		 * Bound position
		 */
		s->s_onlast = 0;
		BOUND(y, x);
		jump_cur(phystop + (x-1)*LINESZ + (y-1)*CELLSZ);
		break;

	case 'K':		/* Clear to end of line */
		y = cur-top;
		y = LINESZ - (y % LINESZ);
		p = cur+y;
		do {
			p -= CELLSZ;
			*(ushort *)p = BLANKW;
		} while (p > cur);
		break;

	case 'r':		/* Set scroll region */
		scroll_region(x, y);
		break;

	case 'm':		/* Set character enhancements */
		/*
		 * Just make it reverse if any enhancement
		 * selected, otherwise normal.
		 */
		if (x) {
			active_screen->s_attr = INVERSE;
		} else {
			active_screen->s_attr = NORMAL;
		}
		break;

	default:
		/* Ignore */
		break;
	}
}

/*
 * jump_cur()
 *	Move current position to a new arbitrary point
 *
 * Sets "cur".  Recalculates top/bottom/lastl if needed.
 */
static void
jump_cur(char *n)
{
	struct scroll *sc;

	/*
	 * Within current scroll region's range (which may encompass
	 * whole screen), so just make it happen.
	 */
	cur = n;
	if ((cur >= top) && (cur < bottom)) {
		return;
	}
	ASSERT_DEBUG((cur >= phystop) && (cur < physbottom),
		"jump_cur: out of physical bounds");

	/*
	 * If scroll regions are active, set the logical bounds,
	 * otherwise set them to the physical bounds.
	 */
	sc = active_screen->s_scroll;
	if (sc) {
		get_region(sc, &top, &lastl);
		bottom = lastl + LINESZ;
	} else {
		top = phystop;
		bottom = physbottom;
		lastl = physbottom - LINESZ;
	}
}

/*
 * do_multichar()
 *	Handle further characters in a multi-character sequence
 */
static int
do_multichar(int state, char c)
{
	static int x, y;
	struct screen *s = active_screen;

	switch (state) {
	case 1:		/* Escape has arrived */
		switch (c) {

		case 'P':	/* Cursor down a line */
			cur += LINESZ;
			if (cur >= bottom) {
				cur -= LINESZ;
			}
			return(0);

		case 'K':	/* Cursor left */
			backspace(s);
			return(0);

		case 'H':	/* Cursor up */
			cur -= LINESZ;
			if (cur < top) {
				cur += LINESZ;
			}
			return(0);

		case 'D':	/* Scroll forward */
			bcopy(top + LINESZ, top, lastl-top);
			blankline(lastl);
			return(0);

		case 'M':	/* Scroll reverse */
			bcopy(top, top + LINESZ, lastl-top);
			blankline(top);
			return(0);

		case 'G':	/* Cursor home */
			s->s_onlast = 0;
			jump_cur(phystop);
			return(0);

		case '[':	/* Extended sequence */
			return(2);

		case '(':	/* Extended char set */
			return(5);

		default:
			return(0);
		}

	case 2:		/* Seen Esc-[ */
		/*
		 * WTF is this question mark?  Anyway, ignoring it
		 * seems to be the Right Thing for now.
		 */
		if (c == '?') {
			return(3);
		}
		if (isdigit(c)) {
			x = c - '0';
			return(3);
		}
		if ((c == 'r') || (c == 'm')) {
			sequence(0, 0, c);
		} else {
			sequence(1, 1, c);
		}
		return(0);

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

	case 5:		/* Seen Esc-([<digits>] */
		/* We just ignore the extended character set for now */
		if (isdigit(c)) {
			return(5);
		}
		return(0);

	default:
		return(0);
	}
}

/*
 * write_string()
 *	Given a counted string, put the characters onto the screen
 */
void
write_string(char *p, uint cnt)
{
	int x;
	struct screen *s = active_screen;
	char c, attr = s->s_attr;

	while (cnt--) {
		c = (*p++) & 0x7F;

		/*
		 * If we are inside a multi-character sequence,
		 * continue
		 */
		if (s->s_state > 0) {
			s->s_state = do_multichar(s->s_state, c);
			attr = s->s_attr;	/* May have changed */
			continue;
		}

		/*
		 * Printing characters are easy
		 */
		if ((c >= ' ') && (c < 0x7F)) {
			/*
			 * If we put to last position on line last time,
			 * advance position for new output.
			 */
			if (s->s_onlast) {
				cur += CELLSZ;
				if (cur >= bottom) {	/* Scroll */
					scrollup();
					cur = lastl;
				}
				s->s_onlast = 0;
			}

			/*
			 * Put char on screen
			 */
			*cur++ = c;
			*cur++ = attr;

			/*
			 * Delay motion to new line until next
			 * output char.
			 */
			if (((cur - top) % LINESZ) == 0) {
				cur -= CELLSZ;
				s->s_onlast = 1;
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
			if ((cur+LINESZ) >= bottom) {
				scrollup();
				if (s->s_onlcr) {
					s->s_onlast = 0;
					cur = lastl;
				}
				continue;
			}

			/*
			 * Move to next line.  If ONLCR is active,
			 * the implicit \r moves to the start of it.
			 */
			cur = cur + LINESZ;
			if (s->s_onlcr) {
				cur = LINE(cur);
				s->s_onlast = 0;
			}
			continue;
		}

		/*
		 * carriage return
		 */
		if (c == '\r') {
			s->s_onlast = 0;
			cur = LINE(cur);
			continue;
		}

		/*
		 * \b--back up a space
		 */
		if (c == '\b') {
			backspace(s);
			continue;
		}

		/*
		 * \t--tab to next stop
		 */
		if (c == '\t') {
			/*
			 * Get current position
			 */
			s->s_onlast = 0;
			x = cur-top;
			x %= LINESZ;

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
			s->s_state = 1;
			continue;
		}

		/*
		 * Ring bell
		 */
		if (c == '\7') {
			outportb(beep_port, inportb(beep_port) |  3);
			__msleep(100);
			outportb(beep_port, inportb(beep_port) & ~3);
		}

		/*
		 * Ignore other control characters
		 */
	}
}
