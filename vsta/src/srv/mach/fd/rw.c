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
#include <sys/fs.h>
#include <lib/llist.h>
#include <fd/fd.h>
#include <sys/assert.h>
#include <sys/param.h>

static void unit_spinup(), unit_recal(), unit_seek(), unit_spindown(),
	unit_io(), unit_iodone(), failed(), state_machine(),
	unit_reset(), unit_failed();

extern port_t fdport;			/* Our server port */
struct floppy floppies[NFD];		/* Per-floppy state */
static void *bounceva, *bouncepa;	/* Bounce buffer */
static seg_t bouncehandle;
static int errors = 0;			/* Global error count */

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
	{F_SEEK,	FEV_INTR,	unit_io,	F_IO},
	{F_SEEK,	FEV_TIME,	unit_reset,	F_RESET},
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
	if (&waiters == waiters.l_forw)
		return(0);
	return(waiters.l_forw->l_data);
}

/*
 * fdc_in()
 *	Read a byte from the FDC
 */
static
fdc_in(void)
{
	int i, j;

	for (i = 50000; i > 0; --i) {
		j = (inportb(FD_STATUS) & (F_MASTER|F_DIR));
		if (j == (F_MASTER|F_DIR))
			break;
		if (j == F_MASTER)
			return(-1);
	}
	if (i < 1)
		return(-1);
	return(inportb(FD_DATA));
}

/*
 * fdc_out()
 *	Write a byte to the FDC
 */
static
fdc_out(uchar c)
{
	int i, j;

	for (i = 50000; i > 0; --i) {
		j = (inportb(FD_STATUS) & (F_MASTER|F_DIR));
		if (j == F_MASTER)
			break;
	}
	if (i < 1)
		return(-1);
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
	ASSERT_DEBUG((u >= 0) && (u < NFD), "fd: bad unit");
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
static
queue_io(struct floppy *fl, struct msg *m, struct file *f)
{
	struct fdparms *fp = &fdparms[fl->f_density];

	ASSERT_DEBUG(f->f_list == 0, "fd queue_io: busy");

	f->f_unit = fl->f_unit;
	f->f_blkno = f->f_pos/SECSZ;
	calc_cyl(f, fp);
	f->f_count = m->m_arg;
	f->f_off = 0;
	f->f_dir = m->m_op;
	f->f_nseg = m->m_nseg;
	bcopy(m->m_seg, f->f_seg, f->f_nseg*sizeof(seg_t));
	if ((f->f_list = ll_insert(&waiters, f)) == 0) {
		msg_err(m->m_sender, ENOMEM);
		return(1);
	}
	return(0);
}

/*
 * timeout()
 *	Ask for M_TIME message in requested # of seconds
 */
static void
timeout(int secs)
{
	struct time t;

	if (secs) {
		(void)time(&t.t_sec);
		t.t_sec += secs;
		t.t_usec = 0L;
		alarm_set(fdport, &t);
	} else {
		alarm_set(fdport, 0);
	}
}

/*
 * motor_mask()
 *	Return mask of bits for motor register
 */
static
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
	ASSERT_DEBUG(busy, "fd recal: not busy");
	busy->f_cyl = -1;
	fdc_out(FDC_RECAL);
	fdc_out(busy->f_unit);
	timeout(4);
}

/*
 * unit_seek()
 *	Move floppy arm to requested cylinder
 */
static void
unit_seek(int old, int new)
{
	struct file *f;

	ASSERT_DEBUG(cur_tran(), "fd seek: no work");
	ASSERT_DEBUG(busy, "fd seek: not busy");

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
	timeout(5);
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
	ASSERT_DEBUG(f, "fd failed(): not busy");
	msg_err(f->f_sender, EIO);
	ll_delete(f->f_list);
	f->f_list = 0;

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
	int dir;

	/*
	 * Try for straight map-down of user's memory
	 */
	if (seg_physmap(f->f_seg, f->f_nseg, f->f_off, SECSZ, &pa)) {
		/*
		 * Nope.  Use bounce buffer
		 */
		pa = (ulong)bouncepa;
	}
	dir = (f->f_dir == FS_READ) ? DMA_READ : DMA_WRITE;
	outportb(DMA_STAT1, dir);
	outportb(DMA_STAT0, dir);
	outportb(DMA_ADDR, pa & 0xFF);
	outportb(DMA_ADDR, (pa >> 8) & 0xFF);
	outportb(DMA_HIADDR, (pa >> 16) & 0xFF);
	outportb(DMA_CNT, SECSZ & 0xFF);
	outportb(DMA_CNT, (SECSZ >> 8) & 0xFF);
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

	/*
	 * Dequeue an operation.  If the DMA can't be set up,
	 * error the operation and return.
	 */
	ASSERT_DEBUG(f, "fd io: not busy");
	ASSERT_DEBUG(busy, "fd io: not busy");
	setup_dma(f);
	setup_fdc(unit(busy->f_unit), f);
	timeout(2);
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
	m.m_buflen = m.m_nseg = m.m_arg1 = 0;
	m.m_arg = f->f_count;
	msg_reply(f->f_sender, &m);

	/*
	 * Leave a timer behind; if nothing comes up, this will
	 * cause the floppies to spin down.
	 */
	if (!busy) {
		timeout(3);
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
	if (!s->s_state)
		return;

	/*
	 * Call event function
	 */
	busy->f_state = s->s_next;
	if (s->s_fun)
		(*(s->s_fun))(s->s_state, s->s_next);
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
	} else if (f->f_unit == ROOTDIR) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Check size
	 */
	if ((m->m_arg & (SECSZ-1)) ||
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
			printf("Drive %d 1.2M\n", x);
			fl->f_state = F_CLOSED;
			fl->f_density = 0;
			continue;
		}
		if ((types & TYMASK) == FD144) {
			printf("Drive %d 1.44M\n", x);
			fl->f_state = F_CLOSED;
			fl->f_density = 1;
			continue;
		}
		printf("Unknown floppy type, drive %d\n", x);
		fl->f_state = F_NXIO;
	}

	/*
	 * Get a bounce buffer for misaligned I/O
	 */
	if (seg_getpage(&bounceva, &bouncepa, 1, &bouncehandle) < 0) {
		printf("Can't get bounce buffer\n");
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
	outportb(FD_MOTOR, motor_mask());
	outportb(FD_MOTOR, motor_mask()|FD_INTR);
	timeout(2);
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
	printf("fd%d won't reset--deconfiguring\n", busy->f_unit);
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
	ASSERT_DEBUG(busy != fl, "fd_close: closed but busy");
	fl->f_state = F_CLOSED;
	fl->f_spinning = 0;
	motors();
}
