/*
 * isr.c
 *	Handler for keyboard interrupt events
 */
#include "cons.h"
#include <sys/assert.h>
#include <mach/io.h>

static int shift = 0,	/* Count # shift keys down */
	alt = 0,	/*  ...alt keys */
	ctl = 0,	/*  ...ctl keys */
	capstoggle = 0,	/* For toggling effect of CAPS */
	numtoggle = 0,	/*  ...NUM lock */
	isE0 = 0;	/* Prefix for extended keys (FN1, etc.) */

/* Map scan codes to ASCII, one table for normal, one for shifted */
static char normal[] = {
  0,033,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
'q','w','e','r','t','y','u','i','o','p','[',']',015,0x80,
'a','s','d','f','g','h','j','k','l',';',047,0140,0x80,
0134,'z','x','c','v','b','n','m',',','.','/',0x80,
'*',0x80,' ',0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
0x80,0x80,0x80,'0',0177
};
static char shifted[] = {
  0,033,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
'Q','W','E','R','T','Y','U','I','O','P','{','}',015,0x80,
'A','S','D','F','G','H','J','K','L',':',042,'~',0x80,
'|','Z','X','C','V','B','N','M','<','>','?',0x80,
'*',0x80,' ',0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
0x80,0x80,0x80,0x80,'7','8','9',0x80,'4','5','6',0x80,
'1','2','3','0',177
};

/*
 * key_event()
 *	Process a key event
 *
 * Handles local stuff like state of shift keys.  On true data,
 * it sends it off for use in read events
 */
static void
key_event(struct screen *s, uchar c)
{
	uchar ch;

	/*
	 * Look up in right table for current state
	 */
 	if (capstoggle) {
 		ch = normal[c];
 		if ((ch >= 'a') && (ch <= 'z')) {
 			ch = shift ? normal[c] : shifted[c];
 		} else {
 			ch = shift ? shifted[c] : normal[c];
 		}
	} else {
		ch = shift ? shifted[c] : normal[c];
	}

	/*
	 * Arrow keys and stuff like that--ignore for now.
	 */
	if (ch == 0x80) {
		return;
	}

	/*
	 * Convert to control characters if CTL key down
	 */
	if (ctl) {
		ch &= 0x1F;
	}

#ifdef DEBUG
	if (ch == '\32') {
		do_dbg_enter();
		ctl = 0;	/* We presume they released it */
		return;
	}
#endif

	/*
	 * Hand off straight data now.  The keyboard always enters
	 * data for the virtual screen currently being displayed
	 * on the hardware screen.
	 */
	kbd_enqueue(s, ch);
}

/*
 * enqueue_string()
 *	Feed a string into the keyboard queue
 */
static void
enqueue_string(struct screen *s, char *p)
{
	char c;

	while ((c = *p++)) {
		kbd_enqueue(s, c);
	}
}

/*
 * cursor_key()
 *	Process cursor keys
 *
 * Enqueue VT100-compatible Escape sequences
 * Returns 1 if it *was* a cursor key, 0 otherwise.
 */
static int
cursor_key(struct screen *s, uchar c)
{
	char *cp;

	switch (c) {
	case 72:	/* up */
		cp = "\033OA";
		break;
	case 80:	/* down */
		cp = "\033OB";
		break;
	case 77:	/* right */
		cp = "\033OC";
		break;
	case 75:	/* left */
		cp = "\033OD";
		break;
	case 73:	/* pg up */
		cp = "\033[5~";
		break;
	case 81:	/* pg down */
		cp = "\033[6~";
		break;
	case 82:	/* insert */
		cp = "\033[2~";
		break;
	case 83:	/* delete */
		cp = "\033[3~";
		break;
	case 71:	/* home */
	case 76:	/* 5 on numpad */
	case 79:	/* end */
		if ((!isE0) && ((numtoggle && !shift) ||
				(!numtoggle && shift))) {
			kbd_enqueue(s, (uint)shifted[c]);
		}
		return 1;
	default:
		return 0;
	}

	if ((!isE0) && ((numtoggle && !shift) ||
			(!numtoggle && shift))) {
		kbd_enqueue(s, shifted[c]);
	} else {
		enqueue_string(s, cp);
	}
	return 1;
}
		
/*
 * function_key()
 *	Process function keys
 *
 * Returns 1 if it *was* a function key, 0 otherwise.
 */
static int
function_key(struct screen *s, uchar c)
{
	char *p;

	switch (c) {
	case 59:	/* F1 */
		p = "\033OP";
		break;
	case 60:	/* F2 */
		p = "\033OQ";
		break;
	case 61:	/* F3 */
		p = "\033OR";
		break;
	case 62:	/* F4 */
		p = "\033OS";
		break;
	case 63:	/* F5 */
		p = "\033OT";
		break;
	case 64:	/* F6 */
		p = "\033OU";
		break;
	case 65:	/* F7 */
		p = "\033OV";
		break;
	case 66:	/* F8 */
		p = "\033OW";
		break;
	case 67:	/* F9 */
		p = "\033OX";
		break;
	case 68:	/* F10 */
	case 87:	/* F11 */
	case 88:	/* F12 */
		p = 0;
		break;
	default:
		return 0;
	}
	if (p) {
		enqueue_string(s, p);
	}
	return(1);
}

/*
 * shift_key()
 *	Process shift key changes
 *
 * Returns 1 if it *was* a shift-type key, 0 otherwise.
 */
static int
shift_key(uchar c)
{
	switch (c) {
	case 0x36:		/* Shift key down */
	case 0x2a:
		shift = 1;
		break;
	case 0xb6:		/* Shift key up */
	case 0xaa:
		shift = 0;
		break;
	case 0xe0:		/* Prefix for "left side" */
 		isE0 = 1;
		break;
	case 0x1d:		/* Control key down */
		ctl = 1;
		break;
	case 0x9d:		/* Control key up */
		ctl = 0;
		break;
	case 0x38:		/* Alt key down */
		alt = 1;
		break;
	case 0xb8:		/* Alt key up */
		alt = 0;
		break;
	case 0x3a:		/* Ignore cap/num down; they might repeat */
	case 0x45:
		break;
	case 0xba:		/* Caps lock up */
#ifdef CAPS
		capstoggle = !capstoggle;
#endif
		break;
	case 0xc5:		/* Num lock up */
		numtoggle = !numtoggle;
		break;
	default:
		return(0);
	}
	return(1);
}

/*
 * kbd_isr()
 *	Called to process an interrupt event from the system keyboard
 *
 * We take the data, strobe the keyboard so it can get more, map to ASCII,
 * and send the data off to be buffered or satisfy pending reads.
 */
void
kbd_isr(struct msg *m)
{
	uchar data, strobe;
	struct screen *s = &screens[hwscreen];

	ASSERT_DEBUG(m->m_arg == KEYBD_IRQ, "kbd_isr: bad IRQ");

	/*
	 * Pull data, toggle controller so it can accept more
	 */
	data = inportb(KEYBD_DATA);
	strobe = inportb(KEYBD_CTL);
	outportb(KEYBD_CTL, strobe|KEYBD_ENABLE);
	outportb(KEYBD_CTL, strobe);

	/*
	 * Function keys--ALT-F1 and so forth switch screens
	 */
	if (alt && ((data >= F1) && (data <= F10))) {
		select_screen(data - F1);
		return;
	}

  	/*
 	 * Winnow out various special keys.  The routines will fiddle
	 * state and queue bytes as appropriate.  Plain old data is
	 * then queued here.
  	 */
 	if (!shift_key(data) &&
			!(data >= 0x80) &&
			!cursor_key(s, data) &&
			!function_key(s, data)) {
		key_event(s, data);
 	}

	/*
	 * Clear our prefix flag unless we've seen it now
	 */
 	if (data != 0xE0) {
 		isE0 = 0;
  	}
}
