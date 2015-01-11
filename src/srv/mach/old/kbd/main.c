/*
 * main.c
 *	Main message handling
 */
#include <sys/perm.h>
#include <sys/fs.h>
#include <hash.h>
#include <stdio.h>
#include <mach/kbd.h>
#include <sys/assert.h>
#include <sys/ports.h>

extern void kbd_read(), abort_read(), *malloc(), kbd_init(), kbd_isr(),
	kbd_stat(), kbd_wstat();
extern char *strerror();

static struct hash *filehash;	/* Map session->context structure */

port_t kbdport;	/* Port we receive contacts through */
uint accgen = 0;	/* Generation counter for access */

/*
 * Protection for keyboard; starts out with access for all.  sys can
 * change the protection label.
 */
struct prot kbd_prot = {
	1,
	ACC_READ|ACC_WRITE,
	{1},
	{ACC_CHMOD}
};

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
	uperms = perm_calc(perms, nperms, &kbd_prot);
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
	f->f_gen = accgen;
	f->f_flags = uperms;

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
	*f = *fold;

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
	(void)hash_delete(filehash, m->m_sender);
	free(f);
}

/*
 * check_gen()
 *	Check access generation
 */
static
check_gen(struct msg *m, struct file *f)
{
	if (f->f_gen != accgen) {
		msg_err(m->m_sender, EIO);
		return(1);
	}
	return(0);
}

/*
 * kbd_main()
 *	Endless loop to receive and serve requests
 */
static void
kbd_main()
{
	struct msg msg;
	int x;
	struct file *f;
loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(kbdport, &msg);
	if (x < 0) {
		perror("kbd: msg_receive");
		goto loop;
	}

	/*
	 * All incoming data should fit in one buffer
	 */
	if (msg.m_nseg > 1) {
		msg_err(msg.m_sender, EINVAL);
		goto loop;
	}

	/*
	 * Categorize by basic message operation
	 */
	f = hash_lookup(filehash, msg.m_sender);
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
		 * Hunt down any active operation for this
		 * handle, and abort it.  Then answer with
		 * an abort acknowledge.
		 */
		if (f->f_count) {
			abort_read(f);
		}
		msg_reply(msg.m_sender, &msg);
		break;
	case M_ISR:		/* Interrupt */
		ASSERT_DEBUG(f == 0, "kbd: session from kernel");
		kbd_isr(&msg);
		break;
	case FS_READ:		/* Read file */
		if (check_gen(&msg, f)) {
			break;
		}
		kbd_read(&msg, f);
		break;
	case FS_STAT:		/* Get stat of file */
		if (check_gen(&msg, f)) {
			break;
		}
		kbd_stat(&msg, f);
		break;
	case FS_WSTAT:		/* Writes stats */
		if (check_gen(&msg, f)) {
			break;
		}
		kbd_wstat(&msg, f);
		break;
	default:		/* Unknown */
		msg_err(msg.m_sender, EINVAL);
		break;
	}
	goto loop;
}

/*
 * main()
 *	Startup of the keyboard server
 */
main()
{
	/*
	 * Allocate handle->file hash table.  16 is just a guess
	 * as to what we'll have to handle.
	 */
        filehash = hash_alloc(16);
	if (filehash == 0) {
		perror("file hash");
		exit(1);
        }

	/*
	 * Init our data structures
	 */
	kbd_init();

	/*
	 * Enable I/O for the needed range
	 */
	if (enable_io(KEYBD_LOW, KEYBD_HIGH) < 0) {
		perror("Keyboard I/O");
		exit(1);
	}

	/*
	 * Get a port for the keyboard
	 */
	kbdport = msg_port(PORT_KBD, 0);

	/*
	 * Tell system about our I/O vector
	 */
	if (enable_isr(kbdport, KEYBD_IRQ)) {
		perror("Keyboard IRQ");
		exit(1);
	}

	/*
	 * Start serving requests for the filesystem
	 */
	kbd_main();
}
