/*
 * isr.c
 *	Handler for interrupt events
 */
#include <sys/msg.h>
#include <sys/types.h>
#include <sys/assert.h>
#include <kbd/kbd.h>

int shift = 0,		/* Count # shift keys down */
	alt = 0,	/*  ...alt keys */
	ctl = 0;	/*  ...ctl keys */
int capstoggle = 0;	/* For toggling effect of CAPS */
int numtoggle = 0;	/*  ...NUM lock */

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
'A','S','D','F','G','H','J','K','L',';',042,'~',0x80,
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
key_event(uchar c)
{
	uchar ch;
	extern void kbd_enqueue();

	/*
	 * Look up in right table for current state
	 */
	if ((c > 70) && numtoggle) {
		ch = shifted[c];
	} else {
		ch = shift ? shifted[c] : normal[c];
	}

	/*
	 * Oops.
	 */
	ASSERT_DEBUG(ch != 0x80, "key_event: shift in data");

	/*
	 * Convert to control characters if CTL key down
	 */
	if (ctl) {
		ch &= 0x1F;
	}

#ifdef DEBUG
	if (ch == '\32') {
		extern void dbg_enter();

		dbg_enter();
		return;
	}
#endif

	/*
	 * Hand off straight data now
	 */
	kbd_enqueue((uint)ch);
}

/*
 * shift_key()
 *	Process shift key changes
 *
 * Returns 1 if it *was* a shift-type key, 0 otherwise.
 */
static
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
		capstoggle = !capstoggle;
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

	ASSERT_DEBUG(m->m_arg == KEYBD_IRQ, "kbd_isr: bad IRQ");

	/*
	 * Pull data, toggle controller so it can accept more
	 */
	data = inportb(KEYBD_DATA);
	strobe = inportb(KEYBD_CTL);
	outportb(KEYBD_CTL, strobe|KEYBD_ENABLE);
	outportb(KEYBD_CTL, strobe);

	/*
	 * Try data as a shift key.  If not, filter key release
	 * events (sorry, X) and process the data.
	 */
	if (!shift_key(data)) {
		if (data <= 0x80) {
			key_event(data);
		}
	}
}
