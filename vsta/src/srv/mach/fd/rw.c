/*
 * rw.c
 *	Reads and writes to the floppy device
 *
 * I/O with DMA is a little crazier than your average server.  The
 * main loop has purposely arranged for us to receive the buffer
 * in terms of segments.  If the caller has been suitably careful,
 * we can then get a physical handle on the memory one sector at a
 * time and do true raw I/O.  If the memory crosses a 64K boundary
 * or isn't aligned, we use a bounce buffer.
 *
 * This driver does not support 360K drives, nor 360K floppies in
 * a 1.2M drive.
 */
#include <mach/io.h>
#include <sys/fs.h>
#include <sys/assert.h>
#include <sys/param.h>
#include <sys/syscall.h>
#include <syslog.h>
#include <std.h>
#include <stdio.h>
#include "fd.h"

static void unit_spinup(), unit_recal(), unit_seek(), unit_spindown(),
	unit_io(), unit_iodone(), failed(), state_machine(),
	unit_reset(), unit_failed(), unit_settle();

extern port_t fdport;			/* Our server port */
extern char fd_sysmsg[];		/* Syslog message prefix */
struct floppy floppies[NFD];		/* Per-floppy state */
static void *bounceva, *bouncepa;	/* Bounce buffer */
static int errors = 0;			/* Global error count */
static int map_handle;			/* Handle for DMA mem mapping */

/*
 * Floppies are such crocks that the only reasonable way to
 * handle them is to encode all their handling into a state
 * machine.  Even then, it isn't very pretty.
 *
 * The following state table draws a distinction between spinning
 * up a floppy for a new user, and spinning up a floppy while the
 * device has remained open.  The hope is that if it's the same
 * user then it's the same floppy, and we might be able to save
 * ourselves some work.  For new floppies, we always start with
 * a recalibration.
 *
 * The called routines can override the next-state assignment.
 * This is used by, for instance, unit_iodone() to cause
 * a recal after I/O error.
 *
 * Because we have to run a state machine for each drive, some
 * of the states below are not actually used.  In paticular,
 * the F_READY state entries are fake because we could well be
 * running the other floppy, and thus he's taking the events,
 * not us.  But the state entries still represent our strategy.
 */
struct state states[] = {
	{F_CLOSED,	FEV_WORK,	unit_spinup,	F_SPINUP1},
	{F_SPINUP1,	FEV_TIME,	unit_reset,	F_RESET},
	{F_RESET,	FEV_INTR,	unit_recal,	F_RECAL},
	{F_RESET,	FEV_TIME,	unit_failed,	F_CLOSED},
	{F_RECAL,	FEV_INTR,	unit_seek,	F_SEEK},
	{F_RECAL,	FEV_TIME,	unit_spindown,	F_CLOSED},
	{F_SEEK,	FEV_INTR,	unit_settle,	F_SETTLE},
	{F_SEEK,	FEV_TIME,	unit_reset,	F_RESET},
	{F_SETTLE,	FEV_TIME,	unit_io,	F_IO},
	{F_IO,		FEV_INTR,	unit_iodone,	F_READY},
	{F_IO,		FEV_TIME,	unit_reset,	F_RESET},
	{F_READY,	FEV_WORK,	unit_seek,	F_SEEK},
	{F_READY,	FEV_TIME,	0,		F_OFF},
	{F_READY,	FEV_CLOSED,	0,		F_CLOSED},
	{F_SPINUP2,	FEV_TIME,	unit_seek,	F_SEEK},
	{F_OFF,		FEV_WORK,	unit_spinup,	F_SPINUP2},
	{F_OFF,		FEV_CLOSED,	0,		F_CLOSED},
	{0}
};

/*
 * Different formats of floppies
 */
struct fdparms fdparms[] = {
	{15, 0x1B, 80, 2400},	/* 1.2 meg */
 	{18, 0x1B, 80, 2880}	/* 1.44 meg */
};

/*
 * Busy/waiter flags
 */
struct floppy *busy = 0;	/* Which unit is running currently */
struct llist waiters;		/* Who's waiting */

/*
 * cur_tran()
 *	Return file struct for current user
 *
 * Returns 0 if there isn't a current user
 */
static struct file *
cur_tran()
{
	if (&waiters == waiters.l_forw) {
		return(0);
	}
	return(waiters.l_forw->l_data);
}

/*
 * fdc_in()
 *	Read a byte from the FDC
 */
static int
fdc_in(void)
{
	int i, j;

	for (i = 50000; i > 0; --i) {
		j = (inportb(FD_STATUS) & (F_MASTER|F_DIR));
		if (j == (F_MASTER|F_DIR)) {
			break;
		}
		if (j == F_MASTER) {
			syslog(LOG_ERR, "%s fdc_in failed", fd_sysmsg);
			return(-1);
		}
	}
	if (i < 1) {
		syslog(LOG_ERR, "%s fdc_in failed2", fd_sysmsg);
		return(-1);
	}
	return(inportb(FD_DATA));
}

/*
 * fdc_out()
 *	Write a byte to the FDC
 */
static int
fdc_out(uchar c)
{
	int i, j;

	for (i = 50000; i > 0; --i) {
		j = (inportb(FD_STATUS) & (F_MASTER|F_DIR));
		if (j == F_MASTER) {
			break;
		}
	}
	if (i < 1) {
		syslog(LOG_ERR, "%s fdc_out failed", fd_sysmsg);
		return(-1);
	}
	outportb(FD_DATA, c);
	return(0);
}

/*
 * unit()
 *	Given unit #, return pointer to data structure
 */
struct floppy *
unit(int u)
{
	ASSERT_DEBUG((u >= 0) && (u < NFD), "fd unit: bad unit");
	return (&floppies[u]);
}

/*
 * calc_cyl()
 *	Given current block #, generate cyl/head values
 */
static void
calc_cyl(struct file *f, struct fdparms *fp)
{
	int head;

	f->f_cyl = f->f_blkno / (2*fp->f_sectors);
	head = f->f_blkno % (2*fp->f_sectors);
	head /= fp->f_sectors;
	f->f_head = head;
}

/*
 * queue_io()
 *	Record parameters of I/O, queue to unit
 */
static int
queue_io(struct floppy *fl, struct msg *m, struct file *f)
{
	struct fdparms *fp = &fdparms[fl->f_density];

	ASSERT_DEBUG(f->f_list == 0, "fd queue_io: busy");

	/*
	 * If they didn't provide a buffer, generate one for
	 * ourselves.
	 */
	if (m->m_nseg == 0) {
		f->f_buf = malloc(m->m_arg);
		if (f->f_buf == 0) {
			msg_err(m->m_sender, ENOMEM);
			return(1);
		}
		f->f_local = 1;
	} else {
		f->f_buf = m->m_buf;
		f->f_local = 0;
	}
	f->f_count = m->m_arg;
	f->f_unit = fl->f_unit;
	f->f_blkno = f->f_pos/SECSZ;
	calc_cyl(f, fp);
	f->f_dir = m->m_op;
	f->f_off = 0;
	if ((f->f_list = ll_insert(&waiters, f)) == 0) {
		if (f->f_local) {
			free(f->f_buf);
		}
		msg_err(m->m_sender, ENOMEM);
		return(1);
	}
	return(0);
}

/*
 * childproc()
 *	Code to sleep, then send a message to the parent
 */
static int child_msecs;
static void
childproc(void)
{
	static port_t selfport = 0;
	struct time t;
	struct msg m;
	extern port_name fdname;

	/*
	 * Child waits, then sends
	 */
	if (selfport == 0) {
		selfport = msg_connect(fdname, ACC_WRITE);
		if (selfport < 0) {
			selfport = 0;
			exit(1);
		}
	}

	/*
	 * Wait the interval
	 */
	time_get(&t);
	t.t_usec += (child_msecs*1000);
	while (t.t_usec > 1000000) {
		t.t_sec += 1;
		t.t_usec -= 1000000;
	}
	time_sleep(&t);

	/*
	 * Send an M_TIME message
	 */
	m.m_op = M_TIME;
	m.m_nseg = m.m_arg = m.m_arg1 = 0;
	(void)msg_send(selfport, &m);
	_exit(0);
}

/*
 * timeout()
 *	Ask for M_TIME message in requested # of milliseconds
 */
static void
timeout(int msecs)
{
	static long child;

	/*
	 * If 0, or a child, cancel child
	 */
	if (child) {
		notify(0, child, EKILL);
		child = 0;
	}
	if (msecs == 0) {
		return;
	}
	child_msecs = msecs;

	/*
	 * We launch a child thread to send our timeout message
	 */
	child = tfork(childproc);
	if (child == -1) {
		return;
	}

	/*
	 * Parent will hear on his main port
	 */
	return;
}

/*
 * motor_mask()
 *	Return mask of bits for motor register
 */
static int
motor_mask(void)
{
	uchar motmask;
	int x;

	/*
	 * One byte holds the bits for ALL the motors.  So we have
	 * to OR together the current motor states across all drives.
	 */
	for (x = 0, motmask = 0; x < NFD; ++x) {
		if (floppies[x].f_spinning)
			motmask |= (FD_MOTMASK << x);
	}
	return(motmask);
}

/*
 * motors_off()
 *	Shut off all motors
 */
static void
motors_off(void)
{
	int x;
	struct floppy *fl;

	for (x = 0; x < NFD; ++x) {
		fl = &floppies[x];
		if (fl->f_spinning) {
			fl->f_spinning = 0;
			fl->f_state = F_OFF;
		}
	}
	outportb(FD_MOTOR, FD_INTR);
}

/*
 * motors()
 *	Spin up motor on appropriate drives
 */
static void
motors(void)
{
	uchar motmask;

	motmask = motor_mask() | FD_INTR;
	if (busy)
		motmask |= (busy->f_unit & FD_UNITMASK);
	outportb(FD_MOTOR, motmask);
}

/*
 * unit_spinup()
 *	Spin up motor on given unit
 */
static void
unit_spinup(int old, int new)
{
	busy->f_spinning = 1;
	motors();

	/*
	 * Get a time message in a while
	 */
	timeout(MOT_TIME);
}

/*
 * unit_spindown()
 *	Spin down a unit after recalibration failure
 *
 * Current operation is aborted
 */
static void
unit_spindown(int old, int new)
{
	busy->f_spinning = 0;
	busy->f_state = F_CLOSED;
	motors();
	failed();
}

/*
 * unit_recal()
 *	Recalibrate a newly-opened unit
 */
static void
unit_recal(int old, int new)
{
	uint x;

	ASSERT_DEBUG(busy, "fd recal: not busy");

	/*
	 * Sense status
	 */
	fdc_out(FDC_SENSE);
	fdc_out(0 | busy->f_unit);	/* Always head 0 after reset */
	x = fdc_in();

	/*
	 * Specify some parameters now that reset
	 */
	fdc_out(FDC_SPECIFY);
	fdc_out(FD_SPEC1);
	fdc_out(FD_SPEC2);
	busy->f_cyl = -1;
	outportb(FD_CTL, XFER_500K);

	/*
	 * Start recalibrate
	 */
	fdc_out(FDC_RECAL);
	fdc_out(busy->f_unit);
	timeout(4000);
}

/*
 * unit_seek()
 *	Move floppy arm to requested cylinder
 */
static void
unit_seek(int old, int new)
{
	struct file *f = cur_tran();
	uint x;

	ASSERT_DEBUG(f, "fd seek: no work");
	ASSERT_DEBUG(busy, "fd seek: not busy");

	/*
	 * Sense result
	 */
	fdc_out(FDC_SENSE);
	fdc_out((f->f_head << 2) | busy->f_unit);
	x = fdc_in();

	/*
	 * If the floppy consistently errors, we will keep re-entering
	 * this state.  It thus makes a good place to put a cap on the
	 * number of errors.
	 */
	if (errors > FD_MAXERR) {
		errors = 0;
		busy->f_spinning = 0;
		busy->f_state = F_CLOSED;
		motors();
		failed();
		return;
	}

	/*
	 * If we're already there, advance to I/O immediately
	 */
	f = cur_tran();
	if (busy->f_cyl == f->f_cyl) {
		busy->f_state = F_IO;
		unit_io(new, F_IO);
		return;
	}

	/*
	 * Send a seek command
	 */
	fdc_out(FDC_SEEK);
	fdc_out((f->f_head << 2) | busy->f_unit);
	fdc_out(f->f_cyl);
	busy->f_cyl = f->f_cyl;

	/*
	 * Arrange for time notification
	 */
	timeout(5000);
}

/*
 * run_queue()
 *	If there's stuff in queue, fire state machine
 *
 * If there's nothing, clears "busy"
 */
static void
run_queue()
{
	struct file *f;

	busy = 0;
	if ((f = cur_tran()) == 0)
		return;
	busy = unit(f->f_unit);
	state_machine(FEV_WORK);
}

/*
 * failed()
 *	Send back an I/O error to the current operation
 */
static void
failed(void)
{
	struct file *f = cur_tran();

	/*
	 * Detach requestor and return error
	 */
	ASSERT_DEBUG(f, "fd failed: not busy");
	ll_delete(f->f_list);
	f->f_list = 0;
	msg_err(f->f_sender, EIO);

	/*
	 * Perhaps kick off some more work
	 */
	run_queue();
}

/*
 * setup_fdc()
 *	DMA is ready, kick the FDC
 */
static void
setup_fdc(struct floppy *fl, struct file *f)
{
	struct fdparms *fp = &fdparms[fl->f_density];

	fdc_out((f->f_dir == FS_READ) ? FDC_READ : FDC_WRITE);
	fdc_out((f->f_head << 2) | fl->f_unit);
	fdc_out(f->f_cyl);
	fdc_out(f->f_head);
	fdc_out((f->f_blkno % fp->f_sectors)+1);
	fdc_out(2);	/* Sector size--always 2*datalen */
	fdc_out(fp->f_sectors);
	fdc_out(fp->f_gap);
	fdc_out(0xFF);	/* Data length--256 bytes */
}

/*
 * setup_dma()
 *	Get a physical handle on the next chunk of user memory
 *
 * Configures DMA to the user's memory if possible.  If not, uses
 * a "bounce buffer" and copyout()'s after I/O.
 *
 * TBD: can we do more than one sector at a time?  I'll play with this
 * in my copious free time....
 */
static void
setup_dma(struct file *f)
{
	ulong pa;

	/*
	 * Try for straight map-down of memory.  Use bounce buffer
	 * if we can't get the memory directly.
	 */
	map_handle = page_wire(f->f_buf + f->f_off, (void **)&pa);
	if (map_handle < 0) {
		pa = (ulong)bouncepa;
	}
	outportb(DMA_STAT0, (f->f_dir == FS_READ) ? DMA_READ : DMA_WRITE);
	outportb(DMA_STAT1, 0);
	outportb(DMA_ADDR, pa & 0xFF);
	outportb(DMA_ADDR, (pa >> 8) & 0xFF);
	outportb(DMA_HIADDR, (pa >> 16) & 0xFF);
	outportb(DMA_CNT, (SECSZ-1) & 0xFF);
	outportb(DMA_CNT, ((SECSZ-1) >> 8) & 0xFF);
	outportb(DMA_INIT, 2);
}

/*
 * unit_io()
 *	Fire up an I/O on a ready/spinning unit
 */
static void
unit_io(int old, int new)
{
	struct file *f = cur_tran();

	ASSERT_DEBUG(f, "fd io: not busy");
	ASSERT_DEBUG(busy, "fd io: not busy");

	/*
	 * Sense state after seek
	 */
	if (old == F_SETTLE) {
		uint x;

		fdc_out(FDC_SENSE);
		fdc_out((f->f_head << 2) | busy->f_unit);
		x = fdc_in();
	}

	/*
	 * Dequeue an operation.  If the DMA can't be set up,
	 * error the operation and return.
	 */
	setup_dma(f);
	setup_fdc(unit(busy->f_unit), f);
	timeout(2000);
}

/*
 * unit_iodone()
 *	Our I/O has completed
 */
static void
unit_iodone(int old, int new)
{
	uchar results[7];
	int x;
	struct file *f = cur_tran();
	struct msg m;

	ASSERT_DEBUG(busy, "fd iodone: not busy");
	ASSERT_DEBUG(f, "fd iodone: no request");

	/*
	 * Shut off timeout
	 */
	timeout(0);

	if (map_handle >= 0) {
		/*
		 * Release memory lock-down, if DMA was to user's memory
		 */
		(void)page_release(map_handle);
	} else {
		/*
		 * Need to bounce out to buffer
		 */
		bcopy(bounceva, f->f_buf+f->f_off, SECSZ);
	}

	/*
	 * Read status
	 */
	for (x = 0; x < sizeof(results); ++x) {
		results[x] = fdc_in();
	}

	/*
	 * Error code?
	 */
	if (results[0] & 0xF8) {
		errors += 1;
		busy->f_state = F_RECAL;
		unit_recal(F_SPINUP1, F_RECAL);
		return;
	}

	/*
	 * Clear error count after successful transfer
	 */
	errors = 0;

	/*
	 * Advance I/O counters.  If we have more to do in this
	 * transfer, keep our state as F_IO and start next part
	 * of transfer.
	 */
	f->f_pos += SECSZ;
	f->f_off += SECSZ;
	f->f_blkno += 1;
	f->f_count -= SECSZ;
	if (f->f_count > 0) {
		calc_cyl(f, &fdparms[busy->f_density]);
		busy->f_state = F_IO;
		unit_io(F_READY, F_IO);
		return;
	}

	/*
	 * All done.  Dequeue operation, generate a completion.  Kick
	 * the queue so any pending operation can now take over.
	 */
	ll_delete(f->f_list);
	f->f_list = 0;
	run_queue();

	/*
	 * Return results.  If we used a local buffer, send it back.
	 * Otherwise we DMA'ed into his own memory, so no segment
	 * is returned.
	 */
	if (f->f_local) {
		m.m_buf = f->f_buf;
		m.m_buflen = f->f_off;
		m.m_nseg = 1;
	} else {
		m.m_nseg = 0;
	}
	m.m_arg = f->f_off;
	m.m_arg1 = 0;
	msg_reply(f->f_sender, &m);

	/*
	 * He has it, so free back to our pool
	 */
	if (f->f_local) {
		free(f->f_buf);
	}

	/*
	 * Leave a timer behind; if nothing comes up, this will
	 * cause the floppies to spin down.
	 */
	if (!busy) {
		timeout(3000);
	}
}

/*
 * state_machine()
 *	State machine for getting work done on floppy units
 *
 * This machine is invoked in response to external events like
 * timeouts and interrupts, as well as internally-generated
 * events, like aborts, retries, and completions.
 */
static void
state_machine(int event)
{
	int x;
	struct state *s;

#ifdef XXX
	printf("state_machine(%d) cur state ", event);
	if (busy) {
		printf("%d\n", busy->f_state);
		printf("  dens = %d, unit = %d, spin = %d, f_cyl = %d, open = %d\n",
		       busy->f_density, busy->f_unit, busy->f_spinning, busy->f_cyl, busy->f_opencnt);
	} else {
		printf("<none>\n");
	}
#endif

	/*
	 * !busy is a special case; we're not driving one of our
	 * floppy drives.  Instead, we're shutting off the motors
	 * due to enough idleness.
	 */
	if (!busy) {
		if (event == FEV_TIME) {
			motors_off();
		}
		return;
	}

	/*
	 * Look up state/event pair
	 */
	for (x = 0, s = states; s->s_state; ++x, ++s) {
		if ((s->s_event == event) &&
				(s->s_state == busy->f_state)) {
			break;
		}
	}

	/*
	 * Ignore events which don't have a table entry
	 */
	if (!s->s_state) {
		return;
	}

	/*
	 * Call event function
	 */
	busy->f_state = s->s_next;
	if (s->s_fun) {
		(*(s->s_fun))(s->s_state, s->s_next);
	}
}

/*
 * fd_rw()
 *	Do I/O to the floppy
 *
 * m_arg specifies how much they want.  It must be in increments
 * of sector sizes, or we EINVAL'em out of here.
 */
void
fd_rw(struct msg *m, struct file *f)
{
	struct floppy *fl;

	/*
	 * Sanity check operations on directories
	 */
	if (m->m_op == FS_READ) {
		if (f->f_unit == ROOTDIR) {
			fd_readdir(m, f);
			return;
		}
	} else {
		/* FS_WRITE */
		if ((f->f_unit == ROOTDIR) || (m->m_nseg != 1)) {
			msg_err(m->m_sender, EINVAL);
			return;
		}
	}

	/*
	 * Check size
	 */
	if ((m->m_arg & (SECSZ-1)) ||
			(m->m_arg > MAXIO) ||
			(f->f_pos & (SECSZ-1))) {
		msg_err(m->m_sender, EINVAL);
		return;
	}
	fl = unit(f->f_unit);

	/*
	 * Check permission
	 */
	if (((m->m_op == FS_READ) && !(f->f_flags & ACC_READ)) ||
			((m->m_op == FS_WRITE) && !(f->f_flags & ACC_WRITE))) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Unit present?
	 */
	if (fl->f_state == F_NXIO) {
		msg_err(m->m_sender, ENXIO);
		return;
	}

	/*
	 * Queue I/O to unit
	 */
	if (!queue_io(fl, m, f) && !busy) {
		busy = fl;
		state_machine(FEV_WORK);
	}
}

/*
 * fd_init()
 *	Set format for each NVRAM-configured floppy
 */
void
fd_init(void)
{
	int x;
	struct floppy *fl;
	uchar types;

	/*
	 * Initialize waiter's queue
	 */
	ll_init(&waiters);

	/*
	 * Ensure that we start with motors turned off
	 */
	outportb(FD_MOTOR, 0);

	/*
	 * Probe floppy presences
	 */
	outportb(RTCSEL, NV_FDPORT);
	types = inportb(RTCDATA);
	fl = floppies;
	for (x = 0; x < NFD; ++x, ++fl, types <<= 4) {
		fl->f_unit = x;
		fl->f_spinning = 0;
		fl->f_opencnt = 0;
		if ((types & TYMASK) == FDNONE) {
			fl->f_state = F_NXIO;
			continue;
		}
		if ((types & TYMASK) == FD12) {
			syslog(LOG_INFO, "%s fd%d: 1.2M", fd_sysmsg, x);
			fl->f_state = F_CLOSED;
			fl->f_density = 0;
			continue;
		}
		if ((types & TYMASK) == FD144) {
			syslog(LOG_INFO, "%s fd%d: 1.44M\n", fd_sysmsg, x);
			fl->f_state = F_CLOSED;
			fl->f_density = 1;
			continue;
		}
		syslog(LOG_INFO, "%s fd%d: unknown type", fd_sysmsg, x);
		fl->f_state = F_NXIO;
	}

	/*
	 * Get a bounce buffer for misaligned I/O
	 */
	bounceva = malloc(NBPG);
	if (page_wire(bounceva, &bouncepa) < 0) {
		syslog(LOG_ERR, "%s can't get bounce buffer", fd_sysmsg);
		exit(1);
	}
}

/*
 * fd_isr()
 *	We have received a floppy interrupt.  Drive the state machine.
 */
void
fd_isr(void)
{
	state_machine(FEV_INTR);
}

/*
 * abort_io()
 *	Cancel floppy operation
 */
void
abort_io(struct file *f)
{
	ASSERT(busy, "fd abort_io: not busy");
	if (f != cur_tran()) {
		/*
		 * If it's not the current operation, this is easy
		 */
		ll_delete(f->f_list);
		f->f_list = 0;
		msg_err(f->f_sender, EIO);
	} else {
		/*
		 * Otherwise we have to be pretty heavy-handed.  We reset
		 * the controller to stop DMA and seeks, and leave the
		 * floppy in a state where it'll be completely reset by
		 * the next user.
		 */
		timeout(0);
		ll_delete(f->f_list);
		f->f_list = 0;
		msg_err(f->f_sender, EIO);
		outportb(FD_MOTOR, 0);
		motors_off();
		busy->f_state = F_CLOSED;
		run_queue();
	}
}

/*
 * fd_time()
 *	A time event has happened
 */
void
fd_time(void)
{
	state_machine(FEV_TIME);
}

/*
 * unit_reset()
 *	Send reset command
 */
static void
unit_reset(int old, int new)
{
	if (old == F_IO) {
		/*
		 * We're reseting after a failed I/O operation
		 */
		errors++;
	}

	outportb(FD_MOTOR, motor_mask());
	outportb(FD_MOTOR, motor_mask()|FD_INTR);
	timeout(2000);
}

/*
 * unit_settle()
 *	Pause to let the heads to settle
 */
static void
unit_settle(int old, int new)
{
	timeout(HEAD_SETTLE);
}

/*
 * unit_failed()
 *	Unit won't even reset--mark as non-existent
 */
static void
unit_failed(int old, int new)
{
	if (!busy)
		return;
	syslog(LOG_ERR, "%s fd%d: won't reset--deconfiguring",
		fd_sysmsg, busy->f_unit);
	busy->f_state = F_NXIO;
	busy->f_spinning = 0;
	motors();
	failed();
}

/*
 * fd_close()
 *	Process a close on a device
 */
void
fd_close(struct msg *m, struct file *f)
{
	struct floppy *fl;

	/*
	 * If it's just at the directory level, no problem
	 */
	if (f->f_unit == ROOTDIR) {
		return;
	}

	/*
	 * Abort any active I/O
	 */
	if (f->f_list) {
		abort_io(f);
	}

	/*
	 * Decrement open count.  On last close, shut off motor to make
	 * it easier to swap floppies.
	 */
	fl = unit(f->f_unit);
	if ((fl->f_opencnt -= 1) > 0)
		return;
	ASSERT_DEBUG(busy != fl, "fd fd_close: closed but busy");
	fl->f_state = F_CLOSED;
	fl->f_spinning = 0;
	motors();
}
