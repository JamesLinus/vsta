/*
 * main.c
 *	Main message handling
 *
 * This handler takes both console (output) and keyboard (input).  It
 * also does the magic necessary to multiplex onto the multiple virtual
 * consoles.
 */
#include <sys/perm.h>
#include <sys/types.h>
#include <sys/fs.h>
#include <sys/ports.h>
#include <sys/syscall.h>
#include <sys/namer.h>
#include <hash.h>
#include "cons.h"
#include <stdio.h>
#include <std.h>
#include <syslog.h>

extern int valid_fname(void *, uint);

static struct hash
	*filehash;	/* Map session->context structure */

port_t consport;	/* Port we receive contacts through */
uint curscreen = 0,	/* Current screen # receiving data */
	hwscreen = 0;	/* Screen # showing on HW */

/*
 * Protection for console; starts out with access for all.  sys can
 * change the protection label.
 */
struct prot cons_prot = {
	1,
	ACC_READ|ACC_WRITE,
	{1},
	{ACC_CHMOD}
};

/*
 * Per-virtual screen state
 */
struct screen screens[NVTY];

/*
 * new_client()
 *	Create new per-connect structure
 */
static void
new_client(struct msg *m)
{
	struct file *f;
	struct perm *perms;
	int uperms, nperms, desired;

	/*
	 * See if they're OK to access
	 */
	perms = (struct perm *)m->m_buf;
	nperms = (m->m_buflen)/sizeof(struct perm);
	uperms = perm_calc(perms, nperms, &cons_prot);
	desired = m->m_arg & (ACC_WRITE|ACC_READ|ACC_CHMOD);
	if ((uperms & desired) != desired) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Get data structure
	 */
	if ((f = malloc(sizeof(struct file))) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Fill in fields.
	 */
	bzero(f, sizeof(struct file));
	f->f_flags = uperms;
	f->f_screen = ROOTDIR;
	f->f_isig = F_ANY;
	sc_init(&f->f_selfs);
	f->f_selfs.sc_nosupp = ACC_WRITE;

	/*
	 * Record perms so we can mimic their identity to send
	 * TTY signals.
	 */
	f->f_nperm = nperms;
	bcopy(perms, f->f_perms, sizeof(f->f_perms));

	/*
	 * Hash under the sender's handle
	 */
        if (hash_insert(filehash, m->m_sender, f)) {
		free(f);
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Return acceptance
	 */
	msg_accept(m->m_sender);
}

/*
 * dup_client()
 *	Duplicate current file access onto new session
 */
static void
dup_client(struct msg *m, struct file *fold)
{
	struct file *f;

	/*
	 * Get data structure
	 */
	if ((f = malloc(sizeof(struct file))) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Fill in fields.  Simply duplicate old file.
	 */
	bcopy(fold, f, sizeof(*f));
	f->f_sentry = 0;
	sc_init(&f->f_selfs);
	f->f_selfs.sc_nosupp = ACC_WRITE;

	/*
	 * Hash under the sender's handle
	 */
        if (hash_insert(filehash, m->m_arg, f)) {
		free(f);
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Return acceptance
	 */
	m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * dead_client()
 *	Someone has gone away.  Free their info.
 */
static void
dead_client(struct msg *m, struct file *f)
{
	struct screen *s = SCREEN(f->f_screen);

	/*
	 * If this is the leader of the process group, stop handing
	 * this target keyboard-generated signals.
	 */
	if (s->s_pgrp_lead == f) {
		s->s_pgrp_lead = 0;
		s->s_pgrp = 0;
	}

	/*
	 * Stop being a select() client
	 */
	if (f->f_sentry) {
		sc_done(&f->f_selfs);
		ll_delete(f->f_sentry);
	}

	/*
	 * Clean up
	 */
	(void)hash_delete(filehash, m->m_sender);
	free(f);
}

/*
 * check_gen()
 *	See if the console is still accessible to them
 */
static int
check_gen(struct msg *m, struct file *f)
{
	if (f->f_gen != screens[f->f_screen].s_gen) {
		msg_err(m->m_sender, EIO);
		return(1);
	}
	return(0);
}

/*
 * do_open()
 *	Open from root to particular device
 */
static void
do_open(struct msg *m, struct file *f)
{
	uint x;
	struct screen *s;

	/*
	 * Must be in root
	 */
	if (f->f_screen != ROOTDIR) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Get screen number
	 */
	x = ((char *)m->m_buf)[0] - '0';
	if (x >= NVTY) {
		msg_err(m->m_sender, ESRCH);
		return;
	}

	/*
	 * Allocate screen memory if it isn't there already
	 */
	s = &screens[x];
	if (s->s_img == 0) {
		s->s_img = malloc(SCREENMEM);
		if (s->s_img == 0) {
			msg_err(m->m_sender, strerror());
			return;
		}

		/*
		 * Record it as off-screen initially, and start it
		 * as blank.
		 */
		s->s_curimg = s->s_img;
		clear_screen(s->s_img);
	}

	/*
	 * Switch to this screen, and tell them they've succeeded.  Note
	 * that the screen is *not* the HW one until they switch to it.
	 */
	f->f_screen = x;
	m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * switch_screen()
 *	Input has arrived for a different screen; switch
 *
 * This routine does not flip the visual screens; it causes the
 * pointers in our emulator to point at different buffers.
 *
 * If the new screen is on the hardware, we don't point at its RAM
 * image, we ask for the hardware instead.
 */
static void
switch_screen(uint new)
{
	struct screen *s;

	/*
	 * Save the old screen position.  The data itself isn't saved,
	 * but the display position may have moved and we need to update
	 * our copy.
	 */
	save_screen_pos(&screens[curscreen]);

	/*
	 * Set to the new one, and point the display engine at its
	 * memory.
	 */
	s = &screens[curscreen = new];
	set_screen(s);
}

/*
 * select_screen()
 *	Switch hardware display to new screen #
 */
void
select_screen(uint new)
{
	struct screen *sold = &screens[hwscreen],
		*snew = &screens[new];

	/*
	 * Don't switch to a screen which has never been opened.  Don't
	 * bother switching to the current (it's a no-op anyway).  Bounce
	 * attempts to switch to an unconfigured screen.
	 */
	if ((new >= NVTY) || (snew->s_img == 0) || (hwscreen == new)) {
		return;
	}

	/*
	 * Save cursor position for curscreen
	 */
	save_screen_pos(&screens[curscreen]);

	/*
	 * Dump HW image into curscreen, and set curscreen to start
	 * doing its I/O to the RAM copy.
	 */
	save_screen(sold);
	sold->s_curimg = sold->s_img;

	/*
	 * Switch to new guy, and tell him to start displaying to HW
	 */
	curscreen = hwscreen = new;
	load_screen(snew);
}

/*
 * do_readdir()
 *	Read from pseudo-dir ROOTDIR
 */
static void
do_readdir(struct msg *m, struct file *f)
{
	static char *mydir;
	static int len;

	/*
	 * Create our "directory" just once, and hold it for further use.
	 * This assumes we never go to three digit VTY numbers.
	 */
	if (mydir == 0) {
		uint x;

		/*
		 * Get space for NVTY sequences of "%2d\n", plus '\0'
		 */
		mydir = malloc(3 * NVTY + 1);
		if (mydir == 0) {
			msg_err(m->m_sender, strerror());
			return;
		}

		/*
		 * Write them on the buffer
		 */
		mydir[0] = '\0';
		for (x = 0; x < NVTY; ++x) {
			sprintf(mydir + strlen(mydir), "%d\n", x);
		}
		len = strlen(mydir);
	}

	if (f->f_pos >= len) {
		/*
		 * If they have it all, return 0 count
		 */
		m->m_arg = m->m_nseg = 0;
	} else {
		/*
		 * Otherwise give them whatever part of "mydir"
		 * they need now.
		 */
		m->m_nseg = 1;
		m->m_buf = mydir + f->f_pos;
		m->m_buflen = len - f->f_pos;
		if (m->m_buflen > m->m_arg) {
			m->m_buflen = m->m_arg;
		}
		m->m_arg = m->m_buflen;
		f->f_pos += m->m_arg;
	}

	/*
	 * Send it back
	 */
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * refresh_isig()
 *	Whoever touches the device last gets to say what mode it's in.
 */
inline static void
refresh_isig(struct file *f)
{
	if (f && (f->f_isig != F_ANY) && (f->f_screen != ROOTDIR)) {
		SCREEN(f->f_screen)->s_isig = f->f_isig;
	}
}

/*
 * screen_main()
 *	Endless loop to receive and serve requests
 */
static void
screen_main()
{
	struct msg msg;
	char *buf2 = 0;
	int x;
	struct file *f;

loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(consport, &msg);
	if (x < 0) {
		syslog(LOG_ERR, "msg_receive");
		goto loop;
	}

	/*
	 * If we've received more than a single buffer of data, pull it in
	 * to a dynamic buffer.
	 */
	if (msg.m_nseg > 1) {
		buf2 = malloc(x);
		if (buf2 == 0) {
			msg_err(msg.m_sender, E2BIG);
			goto loop;
		}
		(void)seg_copyin(msg.m_seg, msg.m_nseg, buf2, x);
		msg.m_buf = buf2;
		msg.m_buflen = x;
		msg.m_nseg = 1;
	}

	/*
	 * Get client
	 */
	f = hash_lookup(filehash, msg.m_sender);

	/*
	 * Categorize by basic message operation
	 */
	switch (msg.m_op & MSG_MASK) {
	case M_CONNECT:		/* New client */
		new_client(&msg);
		break;
	case M_DISCONNECT:	/* Client done */
		dead_client(&msg, f);
		break;
	case M_DUP:		/* File handle dup during exec() */
		dup_client(&msg, f);
		break;
	case M_ABORT:		/* Aborted operation */
		/*
		 * If there's a pending read, abort it.  Writes are
		 * synchronous.
		 */
		if (f->f_readcnt) {
			abort_read(f);
			f->f_readcnt = 0;
		}
		msg_reply(msg.m_sender, &msg);
		break;

	case FS_READ:		/* Read "directory" or keyboard */
		if (f->f_screen == ROOTDIR) {
			do_readdir(&msg, f);
		} else {
			if (check_gen(&msg, f)) {
				break;
			}

			/*
			 * Update I/O count, flag that a complete I/O has
			 * occurred, * and thus we'll need to update
			 * select() status on the next change.
			 */
			f->f_selfs.sc_iocount += 1;
			f->f_selfs.sc_needsel = 0;

			refresh_isig(f);
			kbd_read(&msg, f);
		}
		break;

	case FS_WRITE:		/* Write file */
		/*
		 * Can't write to root
		 */
		if (f->f_screen == ROOTDIR) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}

		/*
		 * Check access generation
		 */
		if (check_gen(&msg, f)) {
			break;
		}

		/*
		 * If this is I/O to a different display than was
		 * last rendered via write_string(), tell it to switch
		 * over.
		 */
		if (curscreen != f->f_screen) {
			switch_screen(f->f_screen);
		}

		/*
		 * Write data
		 */
		refresh_isig(f);
		if (msg.m_buflen > 0) {
			/*
			 * Scribble the bytes onto the display, be it
			 * the HW or our RAM image.
			 */
			write_string(msg.m_buf, msg.m_buflen);

			/*
			 * If this screen is the one on the hardware,
			 * update the HW cursor.
			 */
			if (curscreen == hwscreen) {
				cursor();
			}
		}
		msg.m_arg = x;
		msg.m_buflen = msg.m_arg1 = msg.m_nseg = 0;
		msg_reply(msg.m_sender, &msg);
		f->f_selfs.sc_iocount += 1;
		f->f_selfs.sc_needsel = 1;
		break;

	case FS_STAT:		/* Stat of file */
		if (check_gen(&msg, f)) {
			break;
		}
		cons_stat(&msg, f);
		break;
	case FS_WSTAT:		/* Write selected stat fields */
		if (f->f_screen == ROOTDIR) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		if (check_gen(&msg, f)) {
			break;
		}
		cons_wstat(&msg, f);
		break;
	case FS_OPEN:		/* Open particular screen device */
		if (!valid_fname(msg.m_buf, msg.m_buflen)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		do_open(&msg, f);
		break;

	case M_ISR:		/* Interrupt */
		kbd_isr(&msg);
		break;

	default:		/* Unknown */
		msg_err(msg.m_sender, EINVAL);
		break;
	}

	/*
	 * Free dynamic storage if in use
	 */
	if (buf2) {
		free(buf2);
		buf2 = 0;
	}
	goto loop;
}

#ifdef DEBUG
/*
 * do_dbg_enter()
 *	Drop into kernel debugger
 *
 * Save/restore screen so on-screen kernel debugger won't mess up
 * our screen.
 */
void
do_dbg_enter(void)
{
	extern void dbg_enter(void);

	save_screen_pos(&screens[curscreen]);
	save_screen(&screens[hwscreen]);
	dbg_enter();
	load_screen(&screens[hwscreen]);
}
#endif

/*
 * main()
 *	Startup of the screen server
 */
int
main(int argc, char **argv)
{
	int vid_type = VID_CGA;
	int i;
	struct screen *s;

	/*
	 * Initialize syslog
	 */
	openlog("cons", LOG_PID, LOG_DAEMON);

	/*
	 * First let's parse any command line options
	 */
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-color")) {
			vid_type = VID_CGA;
		}
		else if (!strcmp(argv[i], "-colour")) {
			vid_type = VID_CGA;
		}
		else if (!strcmp(argv[i], "-mono")) {
			vid_type = VID_MGA;
		}
	}

	/*
	 * Allocate handle->file hash table.  16 is just a guess
	 * as to what we'll have to handle.
	 */
        filehash = hash_alloc(16);
	if (filehash == 0) {
		syslog(LOG_ERR, "file hash not allocated");
		exit(1);
        }

	/*
	 * Turn on our I/O access
	 */
	if (enable_io(CONS_LOW, CONS_HIGH) < 0) {
		syslog(LOG_ERR, "can't do display I/O operations");
		exit(1);
	}
	if (enable_io(KEYBD_LOW, KEYBD_HIGH) < 0) {
		syslog(LOG_ERR, "can't do keyboard I/O operations");
		exit(1);
	}

	/*
	 * Get a port for the console
	 */
	consport = msg_port(PORT_CONS, 0);
	(void)namer_register("tty/cons", PORT_CONS);

	/*
	 * Tell system about our I/O vector
	 */
	if (enable_isr(consport, KEYBD_IRQ)) {
		syslog(LOG_ERR, "can't get keyboard IRQ %d", KEYBD_IRQ);
		exit(1);
	}

	/*
	 * Initialize state for all screens.
	 */
	for (i = 0, s = screens; i < NVTY; ++i, ++s) {
		s->s_intr = '\3';	/* ^C */
		s->s_quit = '\34';	/* ^\ */
		s->s_isig = 1;
		s->s_xkeys = 1;
		ll_init(&s->s_readers);
		ll_init(&s->s_selectors);
	}

	/*
	 * Allocate memory for screen 0, the current screen.  Mark
	 * him as currently using the hardware.
	 */
	screens[0].s_img = malloc(SCREENMEM);
	if (screens[0].s_img == 0) {
		syslog(LOG_ERR, "can't allocated screen #0 image");
		exit(1);
	}

	/*
	 * Let screen mapping get initialized.  screens[0] will be
	 * used initially, and will be the one displayed on the
	 * hardware.
	 */
	init_screen(vid_type);

	/*
	 * Start serving requests for the filesystem
	 */
	screen_main();
	return(0);
}
