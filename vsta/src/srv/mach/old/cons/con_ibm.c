/*
 * cons_ibm.c : Console driver for VSTA.
 * 
 * This is actually a modified version of the NEC console driver I wrote for
 * Linux and BSD386, and then ported to VSTA.
 * 
 * This should be largely vt52/vt102 compatible, though there are bound to be
 * some differences. In particular, tabbing, and wrapping are handled a bit
 * differently in this driver.
 * 
 * At first I was thinking of doing a 100% vt100 compatible driver, including
 * the installable character sets, but I think I'll do a window system, and
 * do it there instead.
 */

#include <cons/con_ibm.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#define  inline			/* erase this for inlining */

uint            con_max_rows = 25;	/* size of the screen */
uint            con_max_cols = 80;

static uchar    con_cursor_x     = 0;	/* cursor positioning */
static uchar    con_cursor_y     = 0;
static uchar    con_current_attr = 0x07;/* this is plain white */

static uchar    con_scroll_start = 0;	/* scrolling area */
static uchar    con_scroll_end   = 0;

static uint     con_params[CONS_MAX_PARAM]; /* escape sequence parameters */
static uint     con_param_count = 0;

static uint     con_state = STATE_NORMAL;
static uint     con_mode  = CONS_MODE_NORMAL;

static ushort	con_beep_port = 0x61;	/* for turning the speaker on/off */
static ushort	con_reg_port = 0;	/* controller port */
static ushort	con_val_port = 0;	/* data port */

static ushort  *con_tvram    = NULL;	/* actual vram pointer */
static uchar    con_tab_size = CONS_TAB_SIZE;

extern          inportb();
extern          outportb();

/*
 * con_memcpyw() 
 *     Copy a block of memory in either 16 or 32 bit blocks.
 */
static inline void *
con_memcpyw(void *dest, void *src, unsigned count)
{
	ushort         *d  = dest;
	ushort         *s  = src;
	ulong          *dl = dest;
	ulong          *sl = src;

	if (count % 2) {
		while (count--) {
			*d++ = *s++;
		}
		return (d);
	} else {
		count /= 2;
		while (count--) {
			*dl++ = *sl++;
		}
		return (dl);
	}
}

/*
 * con_memrcpyw() 
 *     Copy a block of memory in either 16 or 32 bit blocks in reverse.
 */
static inline void *
con_memrcpyw(void *dest, void *src, unsigned count)
{
	ushort         *d  = dest;
	ushort         *s  = src;
	ulong          *dl = dest;
	ulong          *sl = src;

	if (count % 2) {
		while (count--) {
			*(d + count) = *(s + count);
		}
		return (d);
	} else {
		count /= 2;
		while (count--) {
			*(dl + count) = *(sl + count);
		}
		return (dl);
	}
}

/*
 * con_memsetw() 
 *     Set a block of memory to a value. 
 *
 * Either 16 or 32 bit quantities are set at a time.
 */
static inline void *
con_memsetw(void *start, ushort c, uint count)
{
	ushort         *s  = start;
	ulong          *sl = start;
	ulong           cl = (c << (sizeof(ushort) * 8)) + c;

	if (count % 2) {
		while (count--) {
			*s++ = c;
		}
		return (s);
	} else {
		count /= 2;
		while (count--) {
			*sl++ = cl;
		}
		return (s);
	}
}

/*
 * con_beep() 
 *     Turn on the speaker, then wait for a bit, and turn it off.
 */
static inline void
con_beep()
{
	outportb(con_beep_port, inportb(con_beep_port) | 3);
	__msleep(100);
	outportb(con_beep_port, inportb(con_beep_port) & 0xFC);	
}

/*
 * con_set_cursor_pos()
 *     Set the real cursor position on the screen.
 */
static inline void
con_set_cursor_pos(uchar x, uchar y)
{
	uint con_pos = (y * con_max_cols) + x;

	outportb(con_reg_port, 0xE);
	outportb(con_val_port, (con_pos >> 8) & 0xFF);
	outportb(con_reg_port, 0xF);
	outportb(con_val_port, con_pos & 0xFF);
}

/*
 * con_gotoxy()
 *     Set the virtual, and physical cursor position. 
 *
 * The position must be within the current scrolling area.
 */
static inline void
con_gotoxy(uchar x, uchar y)
{
	if (x >= con_max_cols) {
		x = con_max_cols-1;
	}
	if (y < con_scroll_start) {
		y = con_scroll_start;
	}
	if (y >= con_scroll_end) {
		y = con_scroll_end-1;
	}
	con_cursor_x = x;
	con_cursor_y = y;

	con_set_cursor_pos(x, y);
}

/*
 * con_cursor_on() 
 *     Turn the cursor on by moving it back on-screen.
 */
static inline void
con_cursor_on(void)
{
	con_set_cursor_pos(con_cursor_x, con_cursor_y);
}

/*
 * con_cursor_off() 
 *     Turn the cursor off by moving it off-screen
 */
static inline void
con_cursor_off(void)
{
	con_set_cursor_pos(con_max_rows, con_max_cols + 1);
}

/*
 * con_scroll_screen_up() 
 *     Scroll the screen up a number of lines
 */
static inline void
con_scroll_screen_up(uchar lines)
{
	ushort          active_lines = con_scroll_end - con_scroll_start;
	uchar           start        = con_scroll_start;
	ushort         *ram_text     = con_tvram + (start * con_max_cols);
	ushort          fill         = (con_current_attr << 8) | ' ';

	/* Is it correct to set the attribute to the current attributes? */
	if (lines >= active_lines) {
		con_memsetw(ram_text, fill, active_lines * con_max_cols);
	} else {
		con_memcpyw(ram_text, ram_text + (lines * con_max_cols),
			    (active_lines - lines) * con_max_cols);
		con_memsetw(ram_text + ((active_lines-lines) * con_max_cols), 
                            fill, lines * con_max_cols);
	}
}

/*
 * con_scroll_screen_down() 
 *     Scroll the screen down a number of lines
 */
static inline void
con_scroll_screen_down(unsigned char lines)
{
	ushort          active_lines = con_scroll_end - con_scroll_start;
	uchar           start        = con_scroll_start;
	ushort         *ram_text     = con_tvram + (start * con_max_cols);
	ushort          fill         = (con_current_attr << 8) | ' ';

	/* Is it correct to set the attribute to the current attributes? */
	if (lines >= active_lines) {
		con_memsetw(ram_text, fill, active_lines * con_max_cols);
	} else {
		con_memrcpyw(ram_text + (lines * con_max_cols), ram_text,
			     (active_lines - lines) * con_max_cols);
		con_memsetw(ram_text, fill, lines * con_max_cols);
	}
}

/*
 * con_move() 
 *     Move the cursor in a specified direction, a specified amount.
 *
 * If the cursor wanders outside the current scrolling area, and scrolling is
 * enabled, scroll the screen.
 */
static inline void
con_move(uchar direction, uchar count, uchar scrolling_allowed)
{
	uchar           top    = con_scroll_start;
	uchar           bottom = con_scroll_end - 1;
	int             cur_x  = con_cursor_x;
	int             cur_y  = con_cursor_y;

	switch (direction) {
	case CONS_MOVE_UP:
		if (cur_y - count < top) {
			if (scrolling_allowed) {
				con_scroll_screen_down(top - (cur_y - count));
			}
			cur_y = top;
		} else {
			cur_y -= count;
		}
		break;
	case CONS_MOVE_DOWN:
		if (cur_y + count > bottom) {
			if (scrolling_allowed)
				con_scroll_screen_up((cur_y + count) - bottom);
			cur_y = bottom;
		} else {
			cur_y += count;
		}
		break;
	case CONS_MOVE_LEFT:
		if (count > con_max_cols) {
			count = con_max_cols;
		}
		if (cur_x - count < 0) {
			cur_x = con_max_cols + (cur_x - count);
			if (cur_y > top) {
				cur_y -= 1;
			} else {
				if (scrolling_allowed)
					con_scroll_screen_down(1);
				cur_y = top;
			}
		} else {
			cur_x -= count;
		}
		break;
	case CONS_MOVE_RIGHT:
		if (count > con_max_cols) {
			count = con_max_cols;
		}
		if (cur_x + count > con_max_cols) {
			cur_x = con_max_cols - (cur_x + count);
			if (cur_y < bottom) {
				cur_y += 1;
			} else {
				if (scrolling_allowed)
					con_scroll_screen_up(1);
				cur_y = bottom;
			}
		} else {
			cur_x += count;
		}
		break;
	}

	con_cursor_x = cur_x;
	con_cursor_y = cur_y;
}

/*
 * con_put_char() 
 *     Output a single character to the actual device. 
 *
 * We don't worry about snow etc. Wrap when needed.
 */
static inline void
con_put_char(uchar c)
{
	ushort          offset = 0;
	ushort          code   = (con_current_attr << 8) | c;

	if (con_cursor_x == con_max_cols) {
		con_cursor_x = 0;
		if (con_cursor_y == con_scroll_end - 1) {
			con_scroll_screen_up(1);
		} else {
			con_cursor_y += 1;
		}
	}
	offset = (con_cursor_y * con_max_cols) + con_cursor_x;

	*(con_tvram + offset) = (con_current_attr << 8) | code;
	con_cursor_x += 1;
}

/*
 * con_erase_to() 
 *     Erase part of the screen. 
 *
 * The parameter `where' decides where to erase to. This is relative to 
 * the current cursor position.
 */
static inline void
con_erase_to(uchar where)
{
	ushort          start  = 0;
	ushort          length = 0;
	ushort          fill   = (con_current_attr << 8) | ' ';

	switch (where) {
	case CONS_ERASE_EOS:
		start  = (con_cursor_y * con_max_cols) + con_cursor_x;
		length = (con_max_rows * con_max_cols) - start;
		break;
	case CONS_ERASE_EOL:
		start  = (con_cursor_y * con_max_cols) + con_cursor_x;
		length = con_max_cols - con_cursor_x;
		break;
	case CONS_ERASE_SOS:
		start  = 0;
		length = (con_cursor_y * con_max_cols) + con_cursor_x + 1;
		break;
	case CONS_ERASE_SOL:
		start  = con_cursor_y * con_max_cols;
		length = con_cursor_x + 1;
		break;
	case CONS_ERASE_SCREEN:
		start  = con_scroll_start * con_max_cols;
		length = (con_scroll_end - con_scroll_start) * con_max_cols;
		break;
	case CONS_ERASE_LINE:
		start  = con_cursor_y * con_max_cols;
		length = con_max_cols;
		break;
	}

	con_memsetw((con_tvram + start), fill, length);
}

/*
 * con_insert() 
 *     Insert some blank characters or lines.
 */
static inline void
con_insert(uint mode, uint num)
{
	ushort          start;
	ushort          length;
	ushort         *ram_text = con_tvram;
	ushort          fill     = (con_current_attr << 8) | ' ';

	if (mode == CONS_LINE_MODE) {
		num *= con_max_cols;
		start = (con_cursor_y + 1) * con_max_cols;
	} else {
		start = (con_cursor_y * con_max_cols) + con_cursor_x + 1;
	}

	if (start + num > (con_max_cols * con_max_rows)) {
		length = start;
		con_memsetw(ram_text, fill, length);
		con_gotoxy(0, 0);
		return;
	}
	ram_text += start;

	con_memrcpyw(ram_text + num, ram_text, num);
	con_memsetw(ram_text, fill, num);
}

/*
 * con_delete() 
 *     Delete some characters or lines.
 */
static inline void
con_delete(uint mode, ushort num)
{
	ushort          start;
	ushort          length;
	ushort         *ram_text = con_tvram;
	ushort          fill     = (con_current_attr << 8) | ' ';

	if (mode == CONS_LINE_MODE) {
		num *= con_max_cols;
		start = (con_cursor_y + 1) * con_max_cols;
	} else {
		start = (con_cursor_y * con_max_cols) + con_cursor_x + 1;
	}

	ram_text += start;

	if (start + num > (con_max_cols * con_max_rows)) {
		length = (con_max_cols * con_max_rows) - start;
		con_memsetw(ram_text, fill, length);
		return;
	}
	con_memcpyw(ram_text, ram_text + num,
		    (con_max_cols * con_max_rows) - (start + num));

	ram_text -= start;
	ram_text += (con_max_cols * con_max_rows) - num;

	con_memsetw(ram_text, fill, num);
}

/*
 * con_set_attributes() 
 *     Set the current attribute byte from an array of parameters.
 */
static inline void
con_set_attributes(uint * attr, uchar num)
{
	int             reverse = 0;
	int             loop, a;

	con_current_attr = CONS_WHITE;
	for (loop = 0; loop < num; loop++) {
	   switch (attr[loop]) {
	   case 0:	/* reset */
	      con_current_attr = CONS_WHITE;
	      reverse = 0;
	      break;
	   case 1:
	      con_current_attr |= CONS_BOLD;
	      break;
	   case 4:
	      /* can't do anything here */
	      break;
	   case 5:
	      con_current_attr |= CONS_BLINK;
	      break;
	   case 7:	/* switch colors */
	      a = con_current_attr & 0xF0;
	      con_current_attr = (con_current_attr << 4) | a;
	      reverse = 1;
	      break;
	   case 8:
              /* This should be secret */
	      con_current_attr = CONS_BLACK;	
	      break;
	   case 30:
	      con_current_attr = 
		 !reverse ? CONS_SET_FORE(con_current_attr, CONS_BLACK)
		          : CONS_SET_BACK(con_current_attr, CONS_BLACK);
	      break;
	   case 31:
	      con_current_attr = 
		 !reverse ? CONS_SET_FORE(con_current_attr, CONS_RED)
		          : CONS_SET_BACK(con_current_attr, CONS_RED);
	      break;
	   case 32:
	      con_current_attr = 
		 !reverse ? CONS_SET_FORE(con_current_attr, CONS_GREEN)
		          : CONS_SET_BACK(con_current_attr, CONS_GREEN);
	      break;
	   case 33:
	      con_current_attr = 
		 !reverse ? CONS_SET_FORE(con_current_attr, CONS_YELLOW)
		          : CONS_SET_BACK(con_current_attr, CONS_YELLOW);
	      break;
	   case 34:
	      con_current_attr = 
		 !reverse ? CONS_SET_FORE(con_current_attr, CONS_BLUE)
		          : CONS_SET_BACK(con_current_attr, CONS_BLUE);
	      break;
	   case 35:
	      con_current_attr = 
		 !reverse ? CONS_SET_FORE(con_current_attr, CONS_MAGENTA)
		          : CONS_SET_BACK(con_current_attr, CONS_MAGENTA);
	      break;
	   case 36:
	      con_current_attr = 
		 !reverse ? CONS_SET_FORE(con_current_attr, CONS_CYAN)
		          : CONS_SET_BACK(con_current_attr, CONS_CYAN);
	      break;
	   case 37:
	      con_current_attr = 
		 !reverse ? CONS_SET_FORE(con_current_attr, CONS_WHITE)
		          : CONS_SET_BACK(con_current_attr, CONS_WHITE);
	      break;
	   case 40:
	      con_current_attr = 
		 reverse ? CONS_SET_FORE(con_current_attr, CONS_BLACK)
		         : CONS_SET_BACK(con_current_attr, CONS_BLACK);
	      break;
	   case 41:
	      con_current_attr = 
		 reverse ? CONS_SET_FORE(con_current_attr, CONS_RED)
		         : CONS_SET_BACK(con_current_attr, CONS_RED);
	      break;
	   case 42:
	      con_current_attr = 
		 reverse ? CONS_SET_FORE(con_current_attr, CONS_GREEN)
		         : CONS_SET_BACK(con_current_attr, CONS_GREEN);
	      break;
	   case 43:
	      con_current_attr = 
		 reverse ? CONS_SET_FORE(con_current_attr, CONS_YELLOW)
		         : CONS_SET_BACK(con_current_attr, CONS_YELLOW);
	      break;
	   case 44:
	      con_current_attr = 
		 reverse ? CONS_SET_FORE(con_current_attr, CONS_BLUE)
		         : CONS_SET_BACK(con_current_attr, CONS_BLUE);
	      break;
	   case 45:
	      con_current_attr = 
                 reverse ? CONS_SET_FORE(con_current_attr, CONS_MAGENTA)
		         : CONS_SET_BACK(con_current_attr, CONS_MAGENTA);
	      break;
	   case 46:
	      con_current_attr = 
                 reverse ? CONS_SET_FORE(con_current_attr, CONS_CYAN)
		         : CONS_SET_BACK(con_current_attr, CONS_CYAN);
	      break;
	   case 47:
	      con_current_attr = 
                 reverse ? CONS_SET_FORE(con_current_attr, CONS_WHITE)
		         : CONS_SET_BACK(con_current_attr, CONS_WHITE);
	      break;
	   }
	}
}

/*
 * con_respond() 
 *     Send a response to a process.
 */
static inline void
con_respond(char *message)
{
	/*
	 * TODO : This is probably not needed anyway (though querying cursor
	 * position might be needed).
	 */
}

/*
 * con_putchar() 
 *     Write a character to the screen and handle all the escape sequences. 
 * 
 * Most escape sequences are recognised, but some of the more obscure ones are
 * simply ignored.
 */
static void
con_putchar(uchar c)
{
	ushort          foo, bar;
	char            buff[64];

	switch (con_state) {
	case STATE_NORMAL:
		switch (c) {
		case '\0':	/* This is a padding character    */
			break;
		case '\7':	/* CTRL-G = beep                  */
			con_beep();
			break;
		case '\b':	/* backspace character            */
			if (con_cursor_x > 0)
				con_cursor_x -= 1;
			break;
		case '\t':	/* tab character                  */
			con_putchar(' ');
			while (con_cursor_x % con_tab_size) {
				con_putchar(' ');
			}
			break;
		case '\n':	/* line feed                      */
			if (con_cursor_y == con_scroll_end - 1) {
				con_scroll_screen_up(1);
			} else {
				con_cursor_y += 1;
			}
			break;
		case '\r':	/* carriage return               */
			con_cursor_x = 0;
			break;
		case '\33':	/* escape sequence starts        */
			con_state = STATE_ESCAPE;
			break;
		default:
			con_put_char(c);	/* Filtering ??   */
			break;
		}
		break;
	case STATE_ESCAPE:
		con_state = STATE_NORMAL;
		switch (c) {
		case '(':
			con_state = STATE_ESCAPE_LPAREN;
			break;
		case ')':
			con_state = STATE_ESCAPE_RPAREN;
			break;
		case '#':
			con_state = STATE_ESCAPE_SHARP;
			break;
		case '[':	/* start compound escape sequence */
			con_state = STATE_ESCAPE_BRACKET;
			break;
		case '>':	/* numeric keypad mode            */
			break;
		case '<':	/* VT52:  ansi mode               */
			break;
		case '^':	/* VT52:  autoprint on            */
			break;
		case ' ':	/* VT52:  autoprint off           */
			break;
		case ']':	/* VT52:  print screen            */
			break;
		case '=':	/* application keyboard mode      */
			break;
		case '7':	/* save cursor and attribute      */
			con_cursor_off();	/* Correct ??     */
			break;
		case '8':	/* restore cursor and attribute   */
			con_cursor_on();	/* Correct ??     */
			break;
		case 'c':	/* reset everything               */
			break;
		case 'A':	/* VT52:  cursor up               */
			con_move(CONS_MOVE_UP, 1, FALSE);
			break;
		case 'B':	/* VT52:  cursor down             */
			con_move(CONS_MOVE_DOWN, 1, FALSE);
			break;
		case 'C':	/* VT52:  cursor right            */
			con_move(CONS_MOVE_RIGHT, 1, FALSE);
			break;
		case 'D':	/* Index. (VT52: cursor left)     */
			if (con_mode & CONS_VT52_PRIORITY) {
				con_move(CONS_MOVE_LEFT, 1, FALSE);
			} else {
				con_move(CONS_MOVE_DOWN, 1, TRUE);
			}
			break;
		case 'E':	/* CR + Index                     */
			con_cursor_x = 0;
			con_move(CONS_MOVE_DOWN, 1, TRUE);
			break;
		case 'F':	/* VT52:  graphics mode           */
			con_mode = CONS_VTGRAPH | CONS_VT52_PRIORITY;
			break;
		case 'G':	/* VT52:  normal character mode   */
			con_mode = CONS_VT52_PRIORITY;
			break;
		case 'H':	/* set horizontal tab (VT52:home) */
			if (con_mode & CONS_VT52_PRIORITY) {
				con_gotoxy(0, 0);
			}
			break;
		case 'I':	/* VT52:  reverse index           */
			con_move(CONS_MOVE_UP, 1, FALSE);
			break;
		case 'J':	/* VT52: erase to EOS             */
			con_erase_to(CONS_ERASE_EOS);
			break;
		case 'K':	/* VT52: erase to EOL             */
			con_erase_to(CONS_ERASE_EOS);
			break;
		case 'M':	/* reverse index                  */
			con_move(CONS_MOVE_UP, 1, TRUE);
			break;
		case 'N':	/* set G2 for 1 character         */
			break;
		case 'O':	/* set G3 for 1 character         */
			break;
		case 'V':	/* VT52:  print cursor line       */
			break;
		case 'W':	/* VT52:  print controller on     */
			break;
		case 'X':	/* VT52:  print controller off    */
			break;
		case 'Y':	/* VT52: position cursor          */
			break;
		case 'Z':	/* VT52/VT102 term ident request  */
			if (con_mode & CONS_VT52_PRIORITY) {
				con_respond(RESPOND_VT52_ID);
			} else {
				con_respond(RESPOND_VT102_ID);
			}
			break;
		}
		break;
	case STATE_ESCAPE_LPAREN:	/* ESC ( seen             */
		switch (c) {
		case 'A':	/* UK characters set as G0        */
			break;
		case 'B':	/* USA characters set as G0       */
			break;
		case '0':	/* Graphics as G0                 */
			break;
		case '1':	/* Alt. ROM as G0                 */
			break;
		case '2':	/* Special Alt. ROM as G0         */
			break;
		}
		con_state = STATE_NORMAL;
		break;
	case STATE_ESCAPE_RPAREN:	/* ESC ) seen             */
		switch (c) {
		case 'A':	/* UK characters set as G1        */
			break;
		case 'B':	/* USA characters set as G1       */
			break;
		case '0':	/* Graphics as G1                 */
			break;
		case '1':	/* Alt. ROM as G1                 */
			break;
		case '2':	/* Special Alt. ROM as G1         */
			break;
		}
		con_state = STATE_NORMAL;
		break;
	case STATE_ESCAPE_SHARP:	/* ESC # seen             */
		switch (c) {
		case '3':	/* top half of line attribute     */
			break;
		case '4':	/* bottom half of line attribute  */
			break;
		case '5':	/* single width & height          */
			break;
		case '6':	/* double width                   */
			break;
		case '8':	/* screen adjustment              */
			break;
		}
		con_state = STATE_NORMAL;
		break;
	case STATE_ESCAPE_BRACKET:	/* ESC [ seen             */
		for (foo = 0; foo < CONS_MAX_PARAM; foo++) {
			con_params[foo] = 0;
		}
		con_param_count = 0;
		con_state = STATE_GET_PARAMETERS;
		/*
		 * Note that is is possible (and perhaps usual) for this to
		 * fall though to the next state. This is not a bug.
		 */
		if (c == '[') {
			con_state = STATE_FUNCTION_KEY;
			break;
		}
		if (c == '?') {
			break;
		}
	case STATE_GET_PARAMETERS:	/* ESC [ seen. Get params */
		if (c == ';' && con_param_count < CONS_MAX_PARAM) {
			con_param_count += 1;
			break;
		} else {
			if (c >= '0' && c <= '9') {
				foo = con_param_count;
				con_params[foo] *= 10;
				con_params[foo] += c - '0';
				break;
			} else {
				con_param_count += 1;
				con_state = STATE_GOT_PARAMETERS;
			}
		}
	case STATE_GOT_PARAMETERS:	/* ESC [ seen. Got params */
		con_state = STATE_NORMAL;
		switch (c) {
		case '@':	/* insert N characters            */
			if (con_param_count >= 1) {
				foo = con_params[0];
				con_insert(CONS_CHAR_MODE, foo);
			}
			break;
		case 'c':	/* vt102 terminal identify request */
			con_respond(RESPOND_VT102_ID);
			break;
		case 'f':	/* move cursor                     */
			if (con_param_count >= 2) {
				if (con_params[1]) {
					con_params[1] -= 1;
				}
				if (con_params[0]) {
					con_params[0] -= 1;
				}
				con_gotoxy(con_params[1],
					   con_params[0]);
			}
			break;
		case 'g':	/* erase TABS                      */
			break;
		case 'h':	/* set flags                       */
			break;
		case 'i':	/* printer control                 */
			break;
		case 'l':	/* reset flags                     */
			break;
		case 'm':	/* set attributes                  */
			if (con_param_count >= 1) {
				con_set_attributes(con_params,
						   con_param_count);
			}
			break;
		case 'n':	/* report device status request    */
			if (con_param_count >= 1) {
				switch (con_params[0]) {
				case 5:	/* status report request   */
					con_respond(RESPOND_STATUS_OK);
					break;
				case 6:	/* cursor position request */
					sprintf(buff, "\033[%d;%dR",
						con_cursor_x, con_cursor_y);
					con_respond(buff);
					break;
				case 15:	/* status request from
						 * printer 
                                                 */
					con_respond(RESPOND_STATUS_OK);
					break;
				}
			}
			break;
		case 'q':	/* LED #1 on/off                   */
			if (con_params[0] == 0) {	/* off     */
			} else {                        /* on      */
			}
			break;
		case 'r':	/* set scrolling region            */
			if (con_param_count >= 2) {
				foo = con_params[0];
				bar = con_params[1];
				if (foo != bar) {
					con_scroll_start = min(foo, bar);
					con_scroll_end = max(foo, bar);
				}
			}
			break;
		case 'y':	/* invoke test                     */
			con_respond(RESPOND_STATUS_OK);
			break;
		case 'A':	/* move up N lines                 */
			if (con_param_count >= 1) {
				con_move(CONS_MOVE_UP, con_params[0], FALSE);
			}
			break;
		case 'B':	/* move down N lines               */
			if (con_param_count >= 1) {
				con_move(CONS_MOVE_DOWN, con_params[0], FALSE);
			}
			break;
		case 'C':	/* move right N characters         */
			if (con_param_count >= 1) {
				con_move(CONS_MOVE_RIGHT, con_params[0],FALSE);
			}
			break;
		case 'D':	/* move left M characters          */
			if (con_param_count >= 1) {
				con_move(CONS_MOVE_LEFT, con_params[0], FALSE);
			}
			break;
		case 'H':	/* move cursor                     */
			if (con_param_count >= 2) {
				if (con_params[1]) {
					con_params[1] -= 1;
				}
				if (con_params[0]) {
					con_params[0] -= 1;
				}
				con_gotoxy(con_params[1], con_params[0]);
			}
			break;
		case 'J':	/* display oriented erase          */
			if (con_param_count >= 1) {
				switch (con_params[0]) {
				case 0:
					con_erase_to(CONS_ERASE_EOS);
					break;
				case 1:
					con_erase_to(CONS_ERASE_SOS);
					break;
				case 2:
					con_erase_to(CONS_ERASE_SCREEN);
					break;
				}
			}
			break;
		case 'K':	/* line oriented erase             */
			if (con_param_count >= 1) {
				switch (con_params[0]) {
				case 0:
					con_erase_to(CONS_ERASE_EOL);
					break;
				case 1:
					con_erase_to(CONS_ERASE_SOL);
					break;
				case 2:
					con_erase_to(CONS_ERASE_LINE);
					break;
				}
			}
			break;
		case 'L':	/* insert N lines                */
			if (con_param_count >= 1) {
				con_insert(CONS_LINE_MODE, con_params[0]);
			}
			break;
		case 'M':	/* delete N lines                */
			if (con_param_count >= 1) {
				con_delete(CONS_LINE_MODE, con_params[0]);
			}
			break;
		case 'P':	/* delete N characters to right  */
			if (con_param_count >= 1) {
				con_delete(CONS_CHAR_MODE, con_params[0]);
			}
			break;
		}
		con_state = STATE_NORMAL;
		break;
	case STATE_FUNCTION_KEY:	/* ESC [ [ seen          */
		con_state = STATE_NORMAL;
		break;
	default:
		con_state = STATE_NORMAL;
	}
}

#ifdef XXX
/*
 * con_print() 
 *     Print a string on the screen. Map '\n' etc.
 */
static void
con_print(char *s)
{
	while (*s) {
		if (*s == '\n')
			con_putchar('\r');
		con_putchar(*s++);
	}
	con_gotoxy(con_cursor_x, con_cursor_y);
}

/*
 * con_test() 
 *     A very simple test of the console driver. 
 *
 * It gives you something to look at while the system boots....
 */
static void
con_test_driver(void)
{
	unsigned int    loop;
	char            buff[64];

	con_erase_to(CONS_ERASE_SCREEN);
	con_gotoxy(0, 0);
	con_print("                                 CONSOLE TEST\n\n\n");
	con_print(
"          BLACK   RED     GREEN   YELLOW  BLUE    MAGENTA CYAN    WHITE\n");
	con_print("UNDERLINE ");
	for (loop = 30; loop < 38; loop++) {
		sprintf(buff, "\033[%d;4m########", loop);
		con_print(buff);
	}
	con_print("\033[37m\n");
	con_print("BLINK     ");
	for (loop = 30; loop < 38; loop++) {
		sprintf(buff, "\033[%d;5m########", loop);
		con_print(buff);
	}
	con_print("\033[37m\n");
	con_print("REVERSE   ");
	for (loop = 30; loop < 38; loop++) {
		sprintf(buff, "\033[%d;7m########", loop);
		con_print(buff);
	}
	con_print("\033[37m\n");
	con_print("UN+BL+REV ");
	for (loop = 30; loop < 38; loop++) {
		sprintf(buff, "\033[%d;4;5;7m########", loop);
		con_print(buff);
	}
	con_print("\033[37m\n");
}
#endif /* XXX */

/*
 * con_write_string()
 *     Write a string of a certain length to the display.
 */
void
con_write_string(char *s, int cnt)
{
	while (cnt--) {
		if (*s == '\n')
			con_putchar('\r');
		con_putchar(*s++);
	}
	con_gotoxy(con_cursor_x, con_cursor_y);
}

/*
 * con_initialise() 
 *     Initialise the display driver.
 *
 * Parse the arguments specifying the size and type of
 * display, and then set up the beep port, the card data and command ports,
 * the screen size, and the pointer to TVRAM.
 */
void
con_initialise(int argc, char **argv)
{
	int             loop;
	ulong           addr;

	addr = 0xb8000;
	con_reg_port = 0x3d4;
	con_val_port = 0x3d5;
	for (loop = 1; loop < argc; loop++) {
		if (!strcmp(argv[loop], "-color")) {
			addr = 0xb8000;
			con_reg_port = 0x3d4;
			con_val_port = 0x3d5;
		} else if (!strcmp(argv[loop], "-mono")) {
			addr = 0xb0000;
			con_reg_port = 0x3b4;
			con_val_port = 0x3b5;
		} else if (!strcmp(argv[loop], "-width")) {
			if (loop < argc - 1) {
				con_max_cols = atoi(argv[loop + 1]);
			}
		} else if (!strcmp(argv[loop], "-height")) {
			if (loop < argc - 1) {
				con_max_rows = atoi(argv[loop + 1]);
			}
		};
	}
	con_scroll_end = con_max_rows;

	if (enable_io(con_reg_port, con_val_port) < 0) {
		fprintf(stderr, "VTCONS: Unable to allocate I/O ports.\n");
		exit(1);
	}
	if (enable_io(con_beep_port, con_beep_port) < 0) {
		fprintf(stderr, "VTCONS: Unable to allocate beep port.\n");
		exit(1);
	}
	con_tvram = mmap((void *) addr, 
                         (con_max_rows * con_max_cols) * sizeof(ushort),
			 PROT_READ | PROT_WRITE, MAP_PHYS, 0, 0L);
#ifdef XXX
	con_test_driver();
#else
	con_erase_to(CONS_ERASE_SCREEN);
	con_gotoxy(0, 0);
#endif
}
