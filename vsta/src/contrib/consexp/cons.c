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
static char *top, *bottom, *cur, *lastl;
char *hw_screen;
static int idx, dat, display;
static ushort beep_port = 0x61;
/* 
 * default palette doesn't match ANSI order 
 * until we reprogram the palette use this map
 */
int color_map[] = { 
	0,
	4,
	2,
	6,
	1,
	5,
	3,
	7
};

/*
 * load_screen()
 *	Switch to new screen image as stored in "s"
 */
void
load_screen(struct screen *s)
{
	bcopy(s->s_img, hw_screen, SCREENMEM);
	set_screen(hw_screen, s->s_pos);
	s->s_curimg = hw_screen;
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
	s->s_pos = cur-top;
}

/*
 * set_screen()
 *	Cause the emulator to start using the named memory as the display
 */
void
set_screen(char *p, uint cursor)
{
	top = p;
	bottom = p + SCREENMEM;
	lastl = p + ((ROWS-1)*COLS*CELLSZ);
	cur = p + cursor;
	ASSERT_DEBUG((cur >= top) && (cur < bottom), "set_screen: bad cursor");
}

/*
 * cursor()
 *	Take current data position, update hardware cursor to match
 */
void
cursor(void)
{
	ulong pos = (cur-top) >> 1;

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
clear_screen(char *p, ulong blank)
{
	ulong bl, *u, *bot;

	bl = blank;
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
cls(ulong blank)
{
	clear_screen(top,blank);
	cur = top;
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

	/*
	 * Move to screen 0, and clear it
	 */
	set_screen(p, 0);
	cls(BLANK);

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
	ulong *u, bl;

	bcopy(top+(COLS*CELLSZ), top, (ROWS-1)*COLS*CELLSZ);
	bl = CURSCREEN()->s_blankl; 
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
		*q++ = CURSCREEN()->s_blankl; 
	}
}

/*
 * color_sequence()
 *	put the color parsing in it's own place
 */
static void
color_sequence(int *codes, int cnt)
{
	int i;
	/* get the current screen */
	struct screen *s = CURSCREEN();

	/* codes is an array of integer commands, execute in sequence */

	/* no codes? reset */
	if(cnt == 0) {

		s->s_attrib = NORMAL;
	}

	for(i = 0; i < cnt; i++)
	{
		switch(codes[i])
		{
		case 0:
		case 27:
                        /* turn off attributes */
			s->s_attrib = NORMAL;
			break;
			
		case 1:
			/* enable bold */
			s->s_attrib |= 0x08;
			break;
			
		case 5:
			/* enable blink */
			s->s_attrib |= 0x80;
			break;
			
		case 7:
			/* enable reverse video */
			s->s_attrib = 0x70;
			break;
			
		case 21:
		case 22:
                        /*  turn off foreground attribute */
			s->s_attrib &= 0xF7;
			break;
			
		case 25:
                        /* turn off background attribute  */
			s->s_attrib &= 0x7F;
			break;
			
		default:
                        /* set foreground color */
			if ((codes[i]>=30) && (codes[i]<=37))
			{
				s->s_attrib = (s->s_attrib & 0xF8) | 
					color_map[codes[i]-30];
			}
			
			/* set background color */
			if ((codes[i]>=40) && (codes[i]<=47))
			{
				s->s_attrib = (s->s_attrib & 0x8F) | 
					(color_map[codes[i]-40] << 4);
			}
                        /* tbd: edm:  39 and 49 restore default attribs */
		}
	}

	/* for faster blanking */
	CURSCREEN()->s_blank = ((ushort)s->s_attrib) << 8 | 
		0x20;
	CURSCREEN()->s_blankl = ((ulong)s->s_blank) << 16 |
		((ulong)s->s_blank);
}

/*
 * sequence()
 *	Called when we've decoded args and it's time to act
 * codes - array of arguments
 * cnt   - number of members of codes
 * c     - escape command
 */
static void
sequence(int *codes, int code_cnt, char c)
{
	char *p;
        /* defaults for backwards compatibility */
	int x = 1;
	int y = 1;

	switch(code_cnt)
	{
	case 2:
		y = codes[1];
		/* fall through, if there's two there must be one */
	case 1:
		x = codes[0];
		/*
		 * Cap/sanity
		 */
		if (x > COLS) {
			x = COLS;
		}
		break;
	}



	/*
	 * Dispatch command
	 */
	switch (c) {
	case 'A':	/* Cursor up */
		while (x-- > 0) {
			cur -= (COLS*CELLSZ);
			if (cur < top) {
				cur += (COLS*CELLSZ);
				x = 0;
			}
		}
		return;
	case 'B':	/* Cursor down a line */
		while (x-- > 0) {
			cur += (COLS*CELLSZ);
			if (cur >= bottom) {
				cur -= (COLS*CELLSZ);
				x = 0;
			}
		}
		return;
	case 'C':	/* Cursor right */
		while (x-- > 0) {
			cur += CELLSZ;
			if (cur >= bottom) {
				cur -= CELLSZ;
				x = 0;
			}
		}
		return;
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
			bcopy(cur, cur+CELLSZ, (p-cur)-CELLSZ);
			*(ushort *)cur = CURSCREEN()->s_blank; 
		}
		return;

	case 'P':		/* Delete character */
		while (x-- > 0) {
			y = cur-top;
			y = COLS*CELLSZ - (y % (COLS*CELLSZ));
			p = cur+y;
			bcopy(cur+CELLSZ, cur, (p-cur)-CELLSZ);
			p -= CELLSZ;
			*(ushort *)p = CURSCREEN()->s_blank; 
		}
		return;

	case 'J':		/* Clear screen/eos */
		if (x == 1) {
			p = cur;
			while (p < bottom) {
				*(ushort *)p = CURSCREEN()->s_blank; 
				p += CELLSZ;
			}
		} else {
			cls(CURSCREEN()->s_blankl);
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
			*(ushort *)p = CURSCREEN()->s_blank; 
		} while (p > cur);
		return;

	case 'm':
		color_sequence(codes,code_cnt);
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
#define MAX_CODES	24

static int
do_multichar(int state, char c)
{
	static int code_idx = 0;
	static int codes[MAX_CODES];

	switch (state) {
	case 1:		/* Escape has arrived */
		switch (c) {
		case 'P':	/* Cursor down a line */
			cur += (COLS*CELLSZ);
			if (cur >= bottom) {
				cur -= (COLS*CELLSZ);
			}
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
			code_idx = 0;
			codes[code_idx] = 0;
			return(2);
		default:
			return(0);
		}

	case 2:		/* Seen Esc-[ */
		if (isdigit(c)) {
			codes[code_idx] = codes[code_idx]*10 + (c - '0');
			/* found at least one digit, move to next state */
			return(3);
		}
		else {
			sequence(NULL, 0, c);
		}
		return(0);

	case 3:		/* Seen Esc-[<digit> */
		if(c == ';') {
			/* next digit */
			code_idx++;
			if(code_idx < MAX_CODES) {
				codes[code_idx] = 0;
				return(3);
			}
			else {
				/* edm: proper way to abort? */
				/* use only fully parse codes, or abort? */
				return (0);
			}			
		}
		else
		if (isdigit(c)) {
                        /* next digit in number */
			codes[code_idx] = codes[code_idx]*10 + (c - '0');
			return(3);
		}
		else
		{
			/* increment code count */
			code_idx++;

			sequence(codes,code_idx,c);
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
write_string(char *s, uint cnt)
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
			*cur++ = CURSCREEN()->s_attrib; 
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
