#ifndef _SCREEN_H
#define _SCREEN_H
/*
 * screen.h
 *	Common definitions for PC/AT console display
 */
#include <sys/fs.h>
#include <sys/perm.h>
#include <llist.h>
#include <mach/kbd.h>
#include <selfs.h>

/*
 * An open file
 */
struct file {
	uint f_gen;		/* Generation of access */
	uint f_flags;		/* User access bits */
	ushort f_pos;		/* For walking virtual dir */
	ushort f_screen;	/* Which virtual screen they're on */
	uint f_readcnt;		/* # bytes requested for current op */
	long f_sender;		/*  ...return addr for current op */
	struct perm		/* Our abilities */
		f_perms[PROCPERMS];
	uchar f_nperm;
	uchar f_isig;		/* Look for signal keys? */
	struct selclient
		f_selfs;	/* State for select() support */
	struct llist
		*f_sentry;	/*  ...list of select() clients per screen */
};

/*
 * Special value for f_isig boolean to indicate that it doesn't
 * control TTY mode.
 */
#define F_ANY (2)

/*
 * Per-virtual screen state
 */
struct screen {
	uint s_gen;		/* Generation of access for this screen */
	char *s_img;		/* In-RAM image of display */
	uint s_pos;		/*  ...cursor position in this image */
	char *s_curimg;		/* Current display--s_img, or the HW */
	struct llist		/* Queue of reads pending */
		s_readers;
	char			/* Typeahead */
		s_buf[KEYBD_MAXBUF];
	ushort s_hd, s_tl;	/*  ...circularly buffered */
	uint s_nbuf;		/*  ...amount buffered */
	char s_quit, s_intr;	/* Signal keys */
	uchar s_isig,		/*  ...do they generate sigs now? */
		s_xkeys;	/* Emulate func and arrow keys? */
	pid_t s_pgrp;		/* Process group to signal */
	struct file		/* Client who opened the pgrp */
		*s_pgrp_lead;
	struct llist
		s_selectors;	/* List of select() clients */
	struct scroll
		*s_scroll;	/* Scroll region(s), if not NULL */
	char s_state,		/* Escape sequence state machine */
		s_onlast,	/* Column 80 handling */
		s_attr,		/* Current display attribute */
		s_pad0;		/* Pad to 32-bit boundary */
};

/*
 * PC screen attributes
 */
#define NORMAL 0x07		/* Attribute for normal characters */
#define INVERSE 0x70		/*  ...for highlighted */
#define BLANKW (0x0720)		/* Normal attribute, blank char */
#define BLANK (0x07200720)	/*  ... two of them in a longword */

#define ROWS 25		/* Screen dimensions */
#define COLS 80
#define CELLSZ 2	/* Bytes per character cell */
#define TABS 8		/* Tab stops every 8 positions */
#define SCREENMEM (ROWS*COLS*CELLSZ)
#define NVTY 8		/* # virtual screens supported */
#define LINESZ (COLS*CELLSZ)	/* Bytes in a single line */

#define ROOTDIR NVTY	/* Special screen # for root dir */
#define SCREEN(idx) (&screens[idx])

/*
 * Top of respective adaptors
 */
#define CONS_LOW MGAIDX

#define MGATOP 0xB0000
#define MGAIDX 0x3B4
#define MGADAT 0x3B5
#define CGATOP 0xB8000
#define CGAIDX 0x3D4
#define CGADAT 0x3D5

#define CONS_HIGH CGADAT

/*
 * Types of adaptor supported
 */
#define VID_MGA 0
#define VID_CGA 1

/*
 * Shared routines
 */
extern void save_screen(struct screen *), load_screen(struct screen *),
	set_screen(struct screen *), cursor(void),
	save_screen_pos(struct screen *);
extern void select_screen(uint);
extern void write_string(char *, uint), init_screen(int);
extern void cons_stat(struct msg *, struct file *),
	cons_wstat(struct msg *, struct file *);
extern void kbd_isr(struct msg *);
extern void kbd_enqueue(struct screen *, uint);
extern void kbd_read(struct msg *, struct file *);
extern void abort_read(struct file *);
extern void do_dbg_enter(void);
extern void clear_screen(char *);
extern void update_select(struct screen *s);

/*
 * Shared data
 */
extern char *hw_screen;
extern struct prot cons_prot;
extern struct screen screens[];
extern uint accgen;
extern uint curscreen, hwscreen;

#endif /* _SCREEN_H */
