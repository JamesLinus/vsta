/*
 * rw.c
 *	Reads and writes to the floppy device
 *
 * I/O with DMA is a little crazier than your average server.  The
 * main loop has purposely arranged for us to receive the buffer
 * in terms of segments.  If the caller has been suitably careful,
 * we can then get a physical handle on the memory one sector at a
 * time and do true raw I/O.  If the memory crosses a page boundary
 * we use a bounce buffer.
 */
#include <mach/io.h>
#include <mach/dma.h>
#include <sys/fs.h>
#include <sys/assert.h>
#include <sys/param.h>
#include <sys/syscall.h>
#include <syslog.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "fd.h"


/*
 * Mask used to see if a physical memory address is DMAable
 */
#define ISA_DMA_MASK 0xff000000


static void unit_spinup();
static void unit_recal();
static void unit_seek();
static void unit_spindown();
static void unit_io();
static void unit_iodone();
static void failed();
static void state_machine();
static void unit_reset();
static void unit_failed();
static void media_chgtest();
static void media_probe();
static void run_queue();


extern int fd_baseio;		/* Our controller's base I/O address */
extern int fd_dma;		/* DMA channel allocated */
extern int fd_irq;		/* Interrupt request line allocated */
int fd_retries = FD_MAXERR;	/* Per-controller number of retries allowed */
int fd_messages = FDM_FAIL;	/* Set the per controller messaging level */
extern int fdc_type;		/* Our controller type */
extern port_t fdport;		/* Our server port */
extern port_name fdport_name;	/* And it's name */
struct floppy floppies[NFD];	/* Per-floppy state */
static void *bounceva, *bouncepa;
				/* Bounce buffer */
static int map_handle;		/* Handle for DMA mem mapping */
static int configed = 0;	/* Have we got all of the drive details? */
static struct file prbf;	/* Pseudo file used in autoprobe operations */


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
 * Names of the FDC types
 */
char fdc_names[][FDC_NAMEMAX] = {
	"unknown", "765A", "765B", "82077AA", NULL
};


/*
 * Different parameters for combinations of floppies/drives.  These are
 * determined by many and varied arcane methods - having looked at 3 lots
 * of other floppy code I gave up and worked out some of my own... now
 * where did I leave that diving rod?
 */
struct fdparms fdparms[] = {
	{368640, 40, 2, 9, 0,	/* 360k in 360k drive */
		0x2a, 0x50,
		XFER_250K, 0xdf,
		0x04, SECTOR_512},
	{368640, 40, 2, 9, 1,	/* 360k in 720k or 1.44M drive */
		0x2a, 0x50,
		XFER_250K, 0xdd,
		0x04, SECTOR_512},
	{368640, 40, 2, 9, 1,	/* 360k in 1.2M drive */
		0x23, 0x50,
		XFER_300K, 0xdf,
		0x06, SECTOR_512},
	{737280, 80, 2, 9, 0,	/* 720k in 720k or 1.44M drive */
		0x2a, 0x50,
		XFER_250K, 0xdd,
		0x04, SECTOR_512},
	{737280, 80, 2, 9, 0,	/* 720k in 1.2M drive */
		0x23, 0x50,
		XFER_300K, 0xdf,
		0x06, SECTOR_512},
	{1228800, 80, 2, 15, 0,	/* 1.2M in 1.2M drive */
		0x1b, 0x54,
		XFER_500K, 0xdf,
		0x06, SECTOR_512},
	{1228800, 80, 2, 15, 0,	/* 1.2M in 1.44M drive */
		0x1b, 0x54,
		XFER_500K, 0xdd,
		0x06, SECTOR_512},
	{1474560, 80, 2, 18, 0,	/* 1.44M in 1.44M drive */
		0x1b, 0x6c,
		XFER_500K, 0xdd,
		0x06, SECTOR_512},
	{1474560, 80, 2, 18, 0,	/* 1.44M in 2.88M drive */
		0x1b, 0x6c,
		PXFER_TSFR | XFER_500K, 0xad,
		0x06, SECTOR_512},
	{2949120, 80, 2, 36, 0,	/* 2.88M in 2.88M drive */
		0x1b, 0x54,
		PXFER_TSFR | XFER_1M, 0xad,
		0x06, SECTOR_512}
};


/*
 * List of parameter sets that each type of floppy can handle.  Note that
 * we define these in the order that they will be checked in an autoprobe
 * sequence, so we must have the highest track counts first, sorted by the
 * number of sides and then the number of sectors
 */
int densities[FDTYPES][DISK_DENSITIES] = {
	{DISK_360_360, -1},
	{DISK_1200_1200, DISK_720_1200, DISK_360_1200, -1},
	{DISK_720_720, DISK_360_720, -1},
	{DISK_1440_1440, DISK_1200_1440, DISK_720_720, DISK_360_720, -1},
	{DISK_2880_2880, DISK_1440_2880, DISK_1200_1440,
		DISK_720_720, DISK_360_720, -1}
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
cur_tran(void)
{
	if (busy->f_prbcount != FD_NOPROBE) {
		return(&prbf);
	}
	
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
	int t_ok = 1, j;
	struct time tim, st;

	/*
	 * Establish a timeout start time
	 */
	time_get(&st);

	do {
		j = (inportb(fd_baseio + FD_STATUS)
		     & (F_MASTER | F_DIR | F_CMDBUSY));
		if (j == (F_MASTER | F_DIR | F_CMDBUSY)) {
			break;
		}
		if (j == F_MASTER) {
			if (busy->f_messages == FDM_ALL) {
				syslog(LOG_ERR, "fdc_in failed");
			}
			return(-1);
		}
		time_get(&tim);
		t_ok = ((tim.t_sec - st.t_sec) * 1000000
			+ (tim.t_usec - st.t_usec)) < IO_TIMEOUT;
	} while(t_ok);
	
	if (!t_ok) {
		if (busy->f_messages == FDM_ALL) {
			syslog(LOG_ERR, "fdc_in failed2");
		}
		return(-1);
	}
	return(inportb(fd_baseio + FD_DATA));
}


/*
 * fdc_out()
 *	Write a byte to the FDC
 */
static int
fdc_out(uchar c)
{
	int t_ok = 1, j;
	struct time tim, st;

	/*
	 * Establish a timeout start time
	 */
	time_get(&st);

	do {
		j = (inportb(fd_baseio + FD_STATUS) & (F_MASTER | F_DIR));
		if (j == F_MASTER) {
			break;
		}
		time_get(&tim);
		t_ok = ((tim.t_sec - st.t_sec) * 1000000
			+ (tim.t_usec - st.t_usec)) < IO_TIMEOUT;
	} while(t_ok);
	
	if (!t_ok) {
		if (busy->f_messages == FDM_ALL) {
			syslog(LOG_ERR, "fdc_out failed - code %02x", j);
		}
		return(-1);
	}
	outportb(fd_baseio + FD_DATA, c);
	return(0);
}


/*
 * unit()
 *	Given unit number, return pointer to unit data structure
 */
struct floppy *
unit(int u)
{
	ASSERT_DEBUG((u >= 0) && (u < NFD), "fd unit: bad unit");
	return (&floppies[u]);
}


/*
 * calc_cyl()
 *	Given current block number, generate cyl/head values
 */
static void
calc_cyl(struct file *f, struct fdparms *fp)
{
	int head;

	f->f_cyl = (f->f_blkno / (fp->f_heads * fp->f_sectors));
	head = f->f_blkno % (fp->f_heads * fp->f_sectors);
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
	f->f_blkno = f->f_pos / SECSZ(fl->f_parms.f_secsize);
	calc_cyl(f, &fl->f_parms);
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
static void
childproc(ulong child_msecs)
{
	static port_t selfport = 0;
	struct time t;
	struct msg m;

	/*
	 * Child waits, then sends
	 */
	if (selfport == 0) {
		selfport = msg_connect(fdport_name, ACC_WRITE);
		if (selfport < 0) {
			selfport = 0;
			exit(1);
		}
	}

	/*
	 * Wait the interval
	 */
	time_get(&t);
	t.t_usec += (child_msecs * 1000);
	while (t.t_usec > 1000000) {
		t.t_sec += 1;
		t.t_usec -= 1000000;
	}
	time_sleep(&t);

	/*
	 * Send an FD_TIME message
	 */
	m.m_op = FD_TIME;
	m.m_nseg = m.m_arg = m.m_arg1 = 0;
	(void)msg_send(selfport, &m);
	_exit(0);
}


/*
 * timeout()
 *	Ask for FD_TIME message in requested number of milliseconds
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

	/*
	 * We launch a child thread to send our timeout message
	 */
	child = tfork(childproc, msecs);
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
		if (floppies[x].f_spinning) {
			motmask |= (FD_MOTMASK << x);
		}
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
	outportb(fd_baseio + FD_MOTOR, FD_INTR);
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
	if (busy) {
		motmask |= (busy->f_unit & FD_UNITMASK);
	}
	outportb(fd_baseio + FD_MOTOR, motmask);
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
	timeout(MOTOR_TIME);
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
	failed(EIO);
}


/*
 * unit_recal()
 *	Recalibrate a newly-opened unit
 *
 * Also reconfigure the FDC to handle the current drive's characteristics.
 * If we're using a decent FDC we also set up some time savings.
 */
static void
unit_recal(int old, int new)
{
	struct fdparms *fp = &busy->f_parms;

	ASSERT_DEBUG(busy, "fd recal: not busy");

	/*
	 * If we've just received an interrupt from a unit_reset(), sense
	 * the status to keep the FDC happy -- drive polling mode demands
	 * this!
	 */
	if (old == F_RESET) {
		int i;

		for (i = 0; i < 4; i++) {
			fdc_out(FDC_SENSEI);
			(void)fdc_in();
			(void)fdc_in();
		}
	}

	/*
	 * If this is just a tidy up after an abort we want to get on with
	 * something productive again
	 */
	if (busy->f_abort) {
		busy->f_abort = FALSE;
		busy->f_state = F_READY;
		busy->f_ranow = 0;
		run_queue();
		if (!busy) {
			timeout(3000);
		}
		return;
	}

	/*
	 * If we have an 82077, configure the FDC.  Note that we don't
	 * allow implied seeks on stretched media :-(
	 */
	if (fdc_type == FDC_HAVE_82077) {
		fdc_out(FDC_CONFIGURE);
		fdc_out(FD_CONF1);
		if (fp->f_stretch) {
			fdc_out(FD_CONF2_STRETCH);
		} else {
			fdc_out(FD_CONF2_NOSTRETCH);
		}
		fdc_out(FD_CONF3);
	}

	/*
	 * Specify HUT (head unload time), SRT (step rate time) and
	 * HLT (Head load time)
	 */
	fdc_out(FDC_SPECIFY);
	fdc_out(fp->f_spec1);
	fdc_out(fp->f_spec2);
	busy->f_cyl = -1;
	
	/*
	 * Establish the data transfer rate
	 */
	outportb(fd_baseio + FD_CTL, fp->f_rate & PXFER_MASK);
	if (fp->f_rate & PXFER_TSFR) {
		ASSERT_DEBUG(fdc_type == FDC_HAVE_82077,
			     "fd unit_recal: not enhanced FDC");

		/*
		 * Set up any perpendicular mode transfers separately from
		 * the main tramsfers
		 */
		fdc_out(FDC_PERPENDICULAR);
		if (fp->f_rate & PXFER_MASK == XFER_1M) {
			fdc_out(PXFER_1M);
		} else {
			fdc_out(PXFER_500K);
		}
	}

	/*
	 * Take a quick look at the drive change line - we need to be
	 * sure whether we have new media or not
	 */
	media_chgtest();

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
	uint s0, pcn;
	int cyl;

	ASSERT_DEBUG(f, "fd seek: no work");
	ASSERT_DEBUG(busy, "fd seek: not busy");

	/*
	 * If the floppy consistently errors, we will keep re-entering
	 * this state.  It thus makes a good place to put a cap on the
	 * number of errors.  Note that we have two error limits - one for
	 * normal operations and one for autoprobe operations
	 */
	if (((busy->f_errors > busy->f_retries)
	     && (busy->f_prbcount != FD_NOPROBE))
	    || ((busy->f_errors > FD_PROBEERR)
	        && (busy->f_prbcount == FD_NOPROBE))) {
		busy->f_errors = 0;
		failed(EIO);
		return;
	}

	/*
	 * Good place to check the change-line status
	 */
	media_chgtest();

	/*
	 * Sense result if we got here from a recalibrate
	 */
	if (old == F_RECAL) {
		fdc_out(FDC_SENSEI);
		s0 = fdc_in();
		pcn = fdc_in();
		if (((s0 & 0xc0) != 0) || (pcn != 0)) {
			/*
			 * This case is unfortunately all too common - some
			 * FDCs just can't cope with a recal from track 79
			 * back down to 0 in a single attempt!
			 */
			busy->f_errors++;
			busy->f_state = F_RESET;
			unit_reset(new, F_RESET);
			return;
		}
	}

	/*
	 * If we're already there or we can handle implied seeks, advance
	 * to I/O immediately
	 */
	f = cur_tran();
	cyl = f->f_cyl;
	if ((busy->f_cyl == f->f_cyl)
	    || ((fdc_type == FDC_HAVE_82077) && (!busy->f_parms.f_stretch))) {
		/*
		 * If we've detected a media change we need to clear the
		 * change-line flag down.  If we were going to do an implied
		 * seek, simply do an explicit one, otherwise seek to the
		 * "wrong" track and ensure that the error recovery method
		 * doesn't count the resultant "failure"
		 */
		if (!busy->f_chgactive) {
			busy->f_state = F_IO;
			unit_io(F_IO, F_IO);
			return;
		} 
		if (busy->f_cyl == f->f_cyl) {
			/*
			 * Set the wrong target track, but we won't count
			 * this one as an errror
			 */
			if (cyl) {
				cyl--;
			} else {
				cyl++;
			}
			busy->f_errors--;
		}
	}

	/*
	 * Send a seek command
	 */
	fdc_out(FDC_SEEK);
	fdc_out((f->f_head << 2) | busy->f_unit);
	fdc_out(cyl << busy->f_parms.f_stretch);
	busy->f_cyl = cyl;

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
run_queue(void)
{
	struct file *f;

	if ((f = cur_tran()) == 0) {
		busy = 0;
		return;
	}
	busy = unit(f->f_unit);
	state_machine(FEV_WORK);
}


/*
 * failed()
 *	Send back an I/O error to the current operation
 *
 * We also take this opportunity to shut down the drive motor and leave 
 * the floppy state as F_CLOSED
 */
static void
failed(char *errstr)
{
	struct file *f;
	
	/*
	 * If we failed during an autoprobe operation we need to make
	 * the cur_tran() details reflect the original request, not the
	 * probe request
	 */
	if (busy->f_prbcount != FD_NOPROBE) {
		busy->f_prbcount = FD_NOPROBE;
	}

	f = cur_tran();

	/*
	 * Shut down the drive motor
	 */
	busy->f_spinning = 0;
	busy->f_state = F_CLOSED;
	motors();

	/*
	 * Detach requestor and return error
	 */
	ASSERT_DEBUG(f, "fd failed: not busy");
	ll_delete(f->f_list);
	f->f_list = 0;
	msg_err(f->f_sender, errstr);

	if (f->f_local) {
		free(f->f_buf);
	}

	/*
	 * Relase any wired pages
	 */
	if (map_handle >= 0) {
		(void)page_release(map_handle);
	}

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
	struct fdparms *fp = &fl->f_parms;

	fdc_out((f->f_dir == FS_READ) ? FDC_READ : FDC_WRITE);
	fdc_out((f->f_head << 2) | fl->f_unit);
	fdc_out(f->f_cyl);
	fdc_out(f->f_head);
	if (!fl->f_ranow) {
		fdc_out((f->f_blkno % fp->f_sectors) + 1);
	} else {
		fdc_out(((fl->f_rablock + fl->f_racount) % fp->f_sectors) + 1);
	}
	fdc_out(fp->f_secsize);
	fdc_out(fp->f_sectors);
	fdc_out(fp->f_gap);
	fdc_out(0xff);		/* Data length - 0xff means ignore DTL */
}


/*
 * setup_dma()
 *	Get a physical handle on the next chunk of user memory
 *
 * Configures DMA to the user's memory if possible.  If not, uses
 * a "bounce buffer" and copyout()'s after I/O.
 */
static void
setup_dma(struct file *f)
{
	ulong pa;
	uint secsz = SECSZ(busy->f_parms.f_secsize);
	void *fbuf;
	uint foff;

	/*
	 * Sort out where the I/O's going - it could be into the
	 * read-ahead buffer
	 */
	if (!busy->f_ranow) {
		fbuf = f->f_buf + f->f_off;
	} else {
		fbuf = busy->f_rabuf + (secsz * busy->f_racount);
	}

	/*
	 * First off, we need to ensure we don't keep grabbing wired pages
	 * as they're a pretty scarce resource
	 */
	if (map_handle >= 0) {
		(void)page_release(map_handle);
		map_handle = -1;
	}

	/*
	 * Second, we need to check if the next transfer will cross a
	 * page boundary - just because two pages are contiguous in virtual
	 * space doesn't mean that they are physically.  We take the easy
	 * approach and just use the bounce buffer for this case
	 */
	if ((((uint)fbuf) & (NBPG - 1)) + secsz <= NBPG) {
		/*
		 * Try for straight map-down of memory.  Use bounce buffer
		 * if we can't get the memory directly.
		 */
		map_handle = page_wire(fbuf, (void **)&pa);
		if (map_handle > 0) {
			/*
			 * Make sure that the wired page is the DMAable
			 * area of system memory.  We'll assume for now that
			 * we're going to have to live with the usual
			 * 16 MByte ISA bus limit
			 */
			if (pa & ISA_DMA_MASK) {
				page_release(map_handle);
				map_handle = -1;
			}
		}
	}
	if (map_handle < 0) {
		/*
		 * We're going to use the bounce buffer
		 */
		pa = (ulong)bouncepa;
		
		/*
		 * Are we doing a write - if we are we need to copy the
		 * user's data into the bounce buffer
		 */
		if (f->f_dir == FS_WRITE) {
			bcopy(fbuf, bounceva, secsz);
		}
	}
	
	outportb(DMA_STAT0, (f->f_dir == FS_READ) ? DMA_READ : DMA_WRITE);
	outportb(DMA_STAT1, 0);
	outportb(DMA_ADDR, pa & 0xff);
	outportb(DMA_ADDR, (pa >> 8) & 0xff);
	outportb(DMA_HIADDR, (pa >> 16) & 0xff);
	outportb(DMA_CNT, (secsz - 1) & 0xff);
	outportb(DMA_CNT, ((secsz - 1) >> 8) & 0xff);
	outportb(DMA_INIT, 2);
}


/*
 * io_complete()
 *	Reply to our clien and handle the clean up after our I/O's complete
 */
static void
io_complete(struct file *f)
{
	struct msg m;

	/*
	 * All done. Return results.  If we used a local buffer, send it
	 * back.  Otherwise we DMA'ed into his own memory, so no segment
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
	 * Dequeue the completed operation.  Kick the queue so
	 * that any pending operation can now take over.
	 */
	ll_delete(f->f_list);
	f->f_list = 0;
	run_queue();

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
	if (old == F_SEEK) {
		uint s0, pcn;

		fdc_out(FDC_SENSEI);
		s0 = fdc_in();
		pcn = fdc_in();

		/*
		 * Another good place to check the change line status - we
		 * have completed a seek to get here, so the change-line
		 * should now be reset.
		 */
		media_chgtest();

		/*
		 * Make sure we're where we should be - recal if we're not
		 */
		if (pcn != (f->f_cyl << busy->f_parms.f_stretch)) {
			busy->f_errors++;
			busy->f_state = F_RECAL;
			unit_recal(F_IO, F_RECAL);
			return;
		}
	}

	/*
	 * Before we actually request the data off the media check to see
	 * that we haven't already cached it
	 */
	if ((!busy->f_ranow)
	    && (f->f_dir == FS_READ)
	    && (busy->f_racount > 0)
	    && (f->f_blkno >= busy->f_rablock)
	    && (f->f_blkno < busy->f_rablock + busy->f_racount)) {
		/*
		 * OK we have cached data we can work with
		 */
		uint secsz = SECSZ(busy->f_parms.f_secsize);
		int clen = (busy->f_rablock + busy->f_racount - f->f_blkno)
			   * secsz;

		if (clen > f->f_count) {
			clen = f->f_count;
		}
		bcopy(busy->f_rabuf + ((f->f_blkno - busy->f_rablock) * secsz),
		      f->f_buf + f->f_off, clen);
		f->f_off += clen;
		f->f_pos += clen;
		f->f_blkno += (clen / secsz);
		f->f_count -= clen;
		
		/*
		 * If we have now completed the I/O, terminate the cycle.
		 * If we've still got some to go it can only really mean that
		 * the data we're after is on the next track so seek
		 * to it
		 */
		if (f->f_count == 0) {
			busy->f_state = F_READY;
			io_complete(f);
			return;
		} else {
			calc_cyl(f, &busy->f_parms);
			busy->f_state = F_SEEK;
			unit_seek(F_READY, F_SEEK);
			return;
		}
	}

	/*
	 * Setup the DMA and FDC registers
	 */
	setup_dma(f);
	setup_fdc(unit(busy->f_unit), f);
	timeout(1000);
}


/*
 * unit_iodone()
 *	Our I/O has completed
 */
static void
unit_iodone(int old, int new)
{
	uchar results[7];
	uint secsz = SECSZ(busy->f_parms.f_secsize);
	int x;
	struct file *f = cur_tran();

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
	}

	/*
	 * Establish the "last used" density as being what we are now!
	 */
	if (busy->f_density != DISK_AUTOPROBE) {
		busy->f_lastuseddens = busy->f_density;
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
	if (results[0] & 0xd8) {
		if (busy->f_prbcount != FD_NOPROBE) {
			/*
			 * We're actually probing the media type - this one's
			 * failed, so let's try the next one - after a few
			 * attempts!
			 */
			busy->f_errors++;
			if (busy->f_errors < busy->f_retries) {
				busy->f_state = F_RECAL;
				unit_recal(F_IO, F_RECAL);
				return;
			}

			if (busy->f_messages == FDM_ALL) {
				syslog(LOG_INFO, "attempted probe for " \
					"%d byte media failed",
					busy->f_parms.f_size);
			}
			busy->f_errors = 0;
			busy->f_prbcount++;
			if (busy->f_posdens[busy->f_prbcount] == -1) {
				busy->f_parms.f_size = FD_PUNDEF;
				failed(EIO);
				return;
			}
			busy->f_parms
				= fdparms[busy->f_posdens[busy->f_prbcount]];
			prbf.f_blkno = (busy->f_parms.f_size
					/ SECSZ(busy->f_parms.f_secsize)) - 1;
			calc_cyl(&prbf, &busy->f_parms);
			busy->f_state = F_RESET;
			unit_reset(F_SPINUP1, F_RESET);
			return;
		}
		if (results[1] & FD_WRITEPROT) {
			if (busy->f_messages <= FDM_FAIL) {
				syslog(LOG_ERR, "unit %d: write protected!",
					busy->f_unit);
			}
			failed(EACCES);
			return;
		}
		if (busy->f_messages == FDM_ALL) {
			syslog(LOG_ERR, "I/O error - %02x %02x %02x " \
				"%02x %02X %02X %02x",
				results[0], results[1], results[2],
				results[3], results[4], results[5],
				results[6]);
		}
		busy->f_errors++;
		busy->f_state = F_RECAL;
		unit_recal(F_IO, F_RECAL);
		return;
	}

	if (busy->f_prbcount != FD_NOPROBE) {
		/*
		 * We've successfully just detected a media type!  Flag
		 * that we're not probing any more an kick off the real I/O
		 * that we were going to do before we started the autoprobe
		 */
		if (busy->f_messages == FDM_ALL) {
			syslog(LOG_INFO, "probe found %d byte media",
				busy->f_parms.f_size);
		}
		busy->f_lastuseddens = busy->f_posdens[busy->f_prbcount];
		busy->f_prbcount = FD_NOPROBE;

		/*
		 * Check that we don't overflow the media capacity
		 */
		if (f->f_count + f->f_pos > busy->f_parms.f_size) {
			failed(ENOSPC);
			return;
		}

		run_queue();
		return;
	}

	/*
	 * Clear error count after successful transfer
	 */
	busy->f_errors = 0;

	/*
	 * If we weren't DMAing then we need to copy the bounce buffer
	 * back into user land
	 */
	if ((map_handle < 0) && (f->f_dir == FS_READ)) {
		/*
		 * Need to bounce out to buffer
		 */
		if (!busy->f_ranow) {
			bcopy(bounceva, f->f_buf + f->f_off, secsz);
		} else {
			bcopy(bounceva,
			      busy->f_rabuf + (secsz * busy->f_racount),
			      secsz);
		}
	}

	/*
	 * Advance I/O counters.  If we have more to do in this
	 * transfer, keep our state as F_IO and start next part
	 * of transfer.
	 */
	if (!busy->f_ranow) {
		f->f_pos += secsz;
		f->f_off += secsz;
		f->f_blkno += 1;
		f->f_count -= secsz;
		if (f->f_count > 0) {
			calc_cyl(f, &busy->f_parms);
			busy->f_state = F_SEEK;
			unit_seek(F_READY, F_SEEK);
			return;
		} else if (f->f_dir == FS_READ) {
			busy->f_ranow = 1;
			busy->f_rablock = f->f_blkno;
			busy->f_racount = -1;
		}
	}

	if (busy->f_ranow) {
		busy->f_racount++;
		if ((busy->f_rablock + busy->f_racount)
		    % busy->f_parms.f_sectors) {
			busy->f_state = F_SEEK;
			unit_seek(F_READY, F_SEEK);
			return;
		} else {
			busy->f_ranow = 0;
		}
	}

	io_complete(f);
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
		printf("  dens = %d, unit = %d, spin = %d, " \
		       "f_cyl = %d, open = %d\n",
		       busy->f_density, busy->f_unit,
		       busy->f_spinning, busy->f_cyl, busy->f_opencnt);
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
 * of sector sizes, or we return tidings of doom and despair.
 */
void
fd_rw(struct msg *m, struct file *f)
{
	uint secsz;
	struct floppy *fl;

	/*
	 * Sanity check operations on directories
	 */
	if (m->m_op == FS_READ) {
		if (f->f_slot == ROOTDIR) {
			fd_readdir(m, f);
			return;
		}
	} else {
		/*
		 * FS_WRITE
		 */
		if ((f->f_slot == ROOTDIR) || (m->m_nseg != 1)) {
			msg_err(m->m_sender, EINVAL);
			return;
		}
	}

	/*
	 * Check size of the I/O request
	 */
	if ((m->m_arg > MAXIO) || (m->m_arg <= 0)) {
		msg_err(m->m_sender, EINVAL);
		return;
	}

	fl = unit(f->f_unit);
	ASSERT_DEBUG(fl, "fd fd_rw: bad node number");

	/*
	 * Check alignment of request (block alignment) - note we assume that
	 * in the event that we're not using a user-defined media parameter
	 * that the sector size will be 512 bytes!
	 */
	if ((f->f_slot == SPECIALNODE)
	    && (fl->f_specialdens == DISK_USERDEF)) {
		secsz = fl->f_userp.f_secsize;
	} else {
		secsz = SECSZ(SECTOR_512);
	}

	if ((m->m_arg & (secsz - 1)) || (f->f_pos & (secsz - 1))) {
		msg_err(m->m_sender, EBALIGN);
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
	 * Check permission
	 */
	if (((m->m_op == FS_READ) && !(f->f_flags & ACC_READ)) ||
			((m->m_op == FS_WRITE) && !(f->f_flags & ACC_WRITE))) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Check that we have valid media parameters
	 */
	if (fl->f_parms.f_size == FD_PUNDEF) {
		busy = fl;
		media_probe(f);
	} else {
		/*
		 * Check that we don't overflow the media capacity
		 */
		if (m->m_arg + f->f_pos > fl->f_parms.f_size) {
			msg_err(m->m_sender, ENOSPC);
			return;
		}
	}

	/*
	 * Queue I/O to unit - note that if we started an autoprobe, or
	 * there's already a device request being performed we just queue
	 * the request, and don't attempt to kick off the state machine
	 */
	if (!queue_io(fl, m, f) && !busy) {
		busy = fl;
		state_machine(FEV_WORK);
	}
}


/*
 * fd_init()
 *	Initialise the floppy server's variable space
 *
 * Establish bounce buffers for misaligned I/O's.  Build command queues.
 * Determine the drives connected and the type of controller that we have 
 */
void
fd_init(void)
{
	int x, c;
	struct floppy *fl;
	uchar types;

	/*
	 * Get a bounce buffer for misaligned I/O
	 */
	bounceva = malloc(NBPG);
	if (page_wire(bounceva, &bouncepa) < 0) {
		syslog(LOG_ERR, "can't get bounce buffer");
		exit(1);
	}
	if ((uint)bouncepa & ISA_DMA_MASK) {
		syslog(LOG_ERR, "bounce buffer out of ISA range (0x%x)",
		       bouncepa);
		exit(1);
	}

	/*
	 * Initialize waiter's queue
	 */
	ll_init(&waiters);

	/*
	 * Ensure that we start with motors turned off - reset the FDC
	 * while we're at it!
	 */
	outportb(fd_baseio + FD_MOTOR, 0);
	motors_off();

	if (fdc_type == FDC_HAVE_UNKNOWN) {
		/*
		 * Find out the controller ID
		 */
		fdc_out(FDC_VERSION);
		c = fdc_in();
		if (c == FDC_VERSION_765A) {
			fdc_type = FDC_HAVE_765A;
		} else if (c == FDC_VERSION_765B) {
			/*
			 * OK, we have at least a 765B, but we may have an
			 * 82077 - we look for some unique features to see
			 * which.  Note that we can only assume bits 0 and
			 * 1 have to respond in the TDR if it's present
			 */
			uchar s1, s2, s3;

			fdc_type = FDC_HAVE_765B;

			s1 = inportb(fd_baseio + FD_TAPE);
			outportb(fd_baseio + FD_TAPE, 0x01);
			s2 = inportb(fd_baseio + FD_TAPE);
			outportb(fd_baseio + FD_TAPE, 0x02);
			s3 = inportb(fd_baseio + FD_TAPE);
			outportb(fd_baseio + FD_TAPE, s1);
			if (((s2 & 0x03) == 0x01) && ((s3 & 0x03) == 0x02)) {
				fdc_type = FDC_HAVE_82077;
			}
		}

		if (fdc_type == FDC_HAVE_UNKNOWN) {
			/*
			 * We've not found an FDC that we recognise - flag a
			 * fail and exit the server
			 */
			syslog(LOG_ERR, "unable to find FDC - aborting!");
			exit(1);
		}
	}

	syslog(LOG_INFO, "FDC %s on IRQ %d, DMA %d, I/O base 0x%x",
		fdc_names[fdc_type], fd_irq, fd_dma, fd_baseio);

	/*
	 * Probe floppy presences
	 */
	outportb(RTCSEL, NV_FDPORT);
	types = inportb(RTCDATA);
	fl = floppies;
	for (x = 0; x < NFD; ++x, ++fl) {
		fl->f_unit = x;
		fl->f_spinning = 0;
		fl->f_opencnt = 0;
		fl->f_mediachg = 0;
		fl->f_chgactive = 0;
		fl->f_state = F_CLOSED;
		fl->f_type = (types >> (4 * (NFD - 1 - x))) & TYMASK;
		fl->f_errors = 0;
		fl->f_retries = FD_MAXERR;
		fl->f_specialdens = DISK_AUTOPROBE;
		fl->f_lastuseddens = DISK_AUTOPROBE;
		fl->f_userp.f_size = FD_PUNDEF;
		fl->f_prbcount = FD_NOPROBE;
		fl->f_messages = FDM_FAIL;
		fl->f_abort = FALSE;

		switch(fl->f_type) {
		case FDNONE:
			fl->f_state = F_NXIO;
			break;

		case FD2880:
			syslog(LOG_INFO, "unit %d: 2.88M\n", x);
			fl->f_posdens = densities[FD2880 - 1];
			break;

		case FD1440:
			syslog(LOG_INFO, "unit %d: 1.44M\n", x);
			fl->f_posdens = densities[FD1440 - 1];
			break;

		case FD720:
			syslog(LOG_INFO, "unit %d: 720k\n", x);
			fl->f_posdens = densities[FD720 - 1];
			break;

		case FD1200:
			syslog(LOG_INFO, "unit %d: 1.2M", x);
			fl->f_posdens = densities[FD1200 - 1];
			break;

		case FD360:
			syslog(LOG_INFO, "unit %d: 360k", x);
			fl->f_posdens = densities[FD360 - 1];
			break;

		default:
			syslog(LOG_INFO, "unit %d: unknown type", x);
			fl->f_state = F_NXIO;
		}
		configed += (fl->f_state != F_NXIO ? 1 : 0);

		if (fl->f_state != F_NXIO) {
			/*
			 * Allocate read-ahead buffer space
			 */
			fl->f_rabuf = (void *)malloc(RABUFSIZE);
			fl->f_rablock = -1;
			fl->f_racount = 0;
			fl->f_ranow = 0;
		}
	}
	if (!configed) {
		syslog(LOG_INFO, "no drives found - server exiting!");
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
	/*
	 * If we're not yet configured then this is just spurious and we
	 * don't want to know!
	 */
	if (configed) {
		state_machine(FEV_INTR);
	}
}


/*
 * abort_io()
 *	Cancel floppy operation
 */
void
abort_io(struct file *f)
{
	ASSERT(busy, "fd abort_io: not busy");

	if (f == cur_tran()) {
		/*
		 * We have to be pretty heavy-handed.  We reset the
		 * controller to stop DMA and seeks, and leave the
		 * floppy in a state where it'll be completely reset by
		 * the next user.  We also clear out any wired DMA pages.
		 */
		timeout(0);
		if (map_handle >= 0) {
			(void)page_release(map_handle);
		}
		busy->f_abort = TRUE;
		busy->f_state = F_RESET;
		unit_reset(F_READY, F_RESET);
	}

	ll_delete(f->f_list);
	f->f_list = 0;
	if (f->f_local) {
		free(f->f_buf);
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
		busy->f_errors++;
	}

	/*
	 * We handle resets differently, depending on the controller types
	 */
	if (fdc_type == FDC_HAVE_82077) {
		outportb(fd_baseio + FD_DRSEL, F_SWRESET);
	} else {
		outportb(fd_baseio + FD_MOTOR, motor_mask());
		__usleep(RESET_SLEEP);
		motors();
	}

	timeout(2000);
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
	syslog(LOG_ERR, "unit %d: won't reset - deconfiguring", busy->f_unit);
	failed(ENXIO);
	busy->f_state = F_NXIO;
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
	if (f->f_slot == ROOTDIR) {
		return;
	}
	
	/*
	 * Abort any active I/O
	 */
	if (f->f_list) {
		abort_io(f);
	}
	
	/*
	 * Decrement open count
	 */
	fl = unit(f->f_unit);
	fl->f_opencnt -= 1;
}


/*
 * media_chgtest()
 *	Check the status of the diskette change-line
 *
 * Look at the diskette change-line status and decide if this is new media
 * in the drive.  If it is, mark in the per-floppy structure that a change
 * is active (a seek will clear it) and increment the count of media
 * changes.  If we were active and are now clear, mark the floppy state
 * as being no-change-active
 */
static void
media_chgtest(void)
{
	if (inportb(fd_baseio + FD_DIGIN) & FD_MEDIACHG) {
		if (!busy->f_chgactive) {
			busy->f_chgactive = 1;
			busy->f_mediachg++;
			busy->f_rablock = -1;
			busy->f_racount = 0;
			if (cur_tran()->f_slot == SPECIALNODE) {
				media_probe(cur_tran());
			}
			if (busy->f_messages <= FDM_WARNING) {
				syslog(LOG_INFO, "unit %d: media changed",
					busy->f_unit);
			}
		}
	} else {
		if (busy->f_chgactive) {
			busy->f_chgactive = 0;
		}
	}
}


/*
 * media_probe()
 *	Initiate a check on the diskette media
 *
 * If we're into autoprobing the media, then we kick of an autoprobe hunt
 * sequence.  The remainder of the sequence will be handled in unit_iodone()
 */
static void
media_probe(struct file *f)
{
	/*
	 * Don't try and start a probe if we're already doing one
	 */
	if (busy->f_prbcount != FD_NOPROBE) {
		return;
	}

	/*
	 * If we're running user defined parameters then this was
	 * a bad request - flag it as such
	 */
	if (busy->f_density == DISK_USERDEF) {
		failed(EIO);
		return;
	}

	/*
	 * Ah, then our only excuse is that we should be autoprobing
	 * the media type.  We need to create a pseudo file that
	 * can then be ammended to give the impression of a client
	 * reading the last sector of the disk - we keep trying
	 * until we find a sector or run out of possibilities.  We
	 * do our I/O into the bounce buffer - nothing else can use
	 * it while we're running.
	 */
	ASSERT_DEBUG(busy->f_density == DISK_AUTOPROBE,
		     "fd fd_rw: bad media parameters");

	busy->f_prbcount = 0;
	busy->f_parms = fdparms[busy->f_posdens[busy->f_prbcount]];
	busy->f_lastuseddens = DISK_AUTOPROBE;
	prbf = *f;
	prbf.f_local = 0;
	prbf.f_count = SECSZ(busy->f_parms.f_secsize);
	prbf.f_buf = bounceva;
	prbf.f_unit = f->f_unit;
	prbf.f_slot = 0;
	prbf.f_blkno = (busy->f_parms.f_size
			/ SECSZ(busy->f_parms.f_secsize)) - 1;
	calc_cyl(&prbf, &busy->f_parms);
	prbf.f_dir = FS_READ;
	prbf.f_off = 0;

	state_machine(FEV_WORK);
}
