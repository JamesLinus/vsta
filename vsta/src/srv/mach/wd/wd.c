/*
 * wd.c
 *	Wester Digital hard disk handling
 *
 * This level of code assumes a single transfer is active at a time.
 * It will handle a contiguous series of sector transfers.
 */
#include <sys/types.h>
#include <sys/fs.h>
#include <wd/wd.h>
#include <sys/assert.h>

static int wd_readp(), wd_cmd();
static void wd_start();
extern void iodone();

uint first_unit;		/* Lowerst unit # configured */

/*
 * The parameters we read on each disk, and a flag to ask if we've
 * gotten them yet.
 */
static struct wdparameters xparm[NWD];
struct wdparms parm[NWD];
int configed[NWD];

/*
 * Miscellaneous counters
 */
ulong wd_strayintr = 0, wd_softerr = 0;

/*
 * Current transfer
 */
static void *busy = 0;	/* Opaque handle from our caller */
static uint cur_unit;	/* Unit active on */
static ulong cur_sec;	/* Sector being transferred now */
static uint cur_xfer;	/*  ...# sectors in this operation */
static uint cur_secs;	/* Sectors left (including cur_sec) */
static char *cur_vaddr;	/* VA to stuff bytes into */
static int cur_op;	/* Current op (FS_READ/FS_WRITE) */

/*
 * swab()
 *	Swap bytes in a range
 */
#define SWAB(field) swab(&(field), sizeof(field))
static void
swab(void *ptr, uint cnt)
{
	char *p, c;
	int x;

	p = ptr;
	for (x = 0; x < cnt; x += 2, p += 2) {
		c = p[0]; p[0] = p[1]; p[1] = c;
	}
}

/*
 * load_sect()
 *	Load a sector down to the controller during an FS_WRITE
 */
static void
load_sect(void)
{
	repoutsw(WD_PORT+WD_DATA, cur_vaddr,
		SECSZ/sizeof(ushort));
	cur_vaddr += SECSZ;
	cur_sec += 1;
	cur_secs -= 1;
	cur_xfer -= 1;
}

/*
 * wd_init()
 *	Initialize our disk controller
 *
 * Initialize controller.
 *
 * For each disk unit, see if it's present by trying to read its
 * parameters.
 */
void
wd_init(void)
{
	int x;
	int found_first;

	/*
	 * Send reset to controller, wait, drop reset bit.
	 */
	outportb(WD_PORT+WD_CTLR, CTLR_RESET|CTLR_IDIS);
	__msleep(100);
	outportb(WD_PORT+WD_CTLR, CTLR_IDIS);
	__msleep(100);

	/*
	 * Ask him if he's OK
	 */
	if (wd_cmd(WDC_DIAG) < 0) {
		printf("WD controller fails diagnostic\n");
		exit(1);
	}
	inportb(WD_PORT+WD_ERROR);

	/*
	 * Allow interrupts now
	 */
	outportb(WD_PORT+WD_CTLR, CTLR_4BIT);

	/*
	 * Scan units
	 */
	found_first = 0;
	for (x = 0; x < NWD; ++x) {
		if (wd_readp(x) < 0) {
			configed[x] = 0;
		} else {
			uint s = parm[x].w_size * SECSZ;
			const uint m = 1024*1024;

			printf("wd%d: %d.%dM\n", x,
				s / m, (s % m) / (m/10));
			configed[x] = 1;
			if (!found_first) {
				found_first = 1;
				first_unit = x;
			}
		}
	}
	if (!found_first) {
		printf("wd: no units found, exiting.\n");
		exit(1);
	}
}

/*
 * wd_io()
 *	Entry into machinery for doing I/O to a disk
 *
 * Validity of count and alignment have already been checked.
 * Interpretation of partitioning, if any, has also already been
 * done by our caller.  This routine and below looks at the disk
 * as a pure, 0-based array of SECSZ blocks.
 *
 * Returns 0 on successfully initiated I/O, 1 on error.
 */
wd_io(int op, void *handle, uint unit, ulong secnum, void *va, uint secs)
{
	ASSERT_DEBUG(unit < NWD, "wd_io: bad unit");
	ASSERT_DEBUG(busy == 0, "wd_io: busy");
	ASSERT_DEBUG(handle != 0, "wd_io: null handle");
	ASSERT_DEBUG(secs > 0, "wd_io: 0 len");

	/*
	 * If not configured, error out
	 */
	if (!configed[unit]) {
		return(1);
	}
	ASSERT_DEBUG(secnum < parm[unit].w_size, "wd_io: high sector");

	/*
	 * Record transfer parameters
	 */
	busy = handle;
	cur_unit = unit;
	cur_sec = secnum;
	cur_secs = secs;
	cur_vaddr = va;
	cur_op = op;

	/*
	 * Kick off first transfer
	 */
	wd_start();
	return(0);
}

/*
 * wd_start()
 *	Given I/O described by global parameters, initiate next sector I/O
 *
 * The size of the I/O will be the lesser of the amount left on the track
 * and the amount requested.  cur_xfer will be set to this count, in units
 * of sectors.
 */
static void
wd_start(void)
{
	uint cyl, sect, trk, lsect;
	struct wdparms *w = &parm[cur_unit];

#ifdef DEBUG
	ASSERT((inportb(WD_PORT+WD_STATUS) & WDS_BUSY) == 0,
		"wd_start: busy");
#endif
	/*
	 * Given disk geometry, calculate parameters for next I/O
	 */
	cyl =  cur_sec / w->w_secpercyl;
	sect = cur_sec % w->w_secpercyl;
	lsect = w->w_secpercyl - sect;
	trk = sect / w->w_secpertrk;
	sect = (sect % w->w_secpertrk) + 1;

	/*
	 * Transfer size--either the rest, or the remainder of this track
	 */
	if (cur_secs > lsect) {
		cur_xfer = lsect;
	} else {
		cur_xfer = cur_secs;
	}

	/*
	 * Program I/O
	 */
	outportb(WD_PORT+WD_SCNT, cur_xfer);
	outportb(WD_PORT+WD_SNUM, sect);
	outportb(WD_PORT+WD_CYL0, cyl & 0xFF);
	outportb(WD_PORT+WD_CYL1, (cyl >> 8) & 0xFF);
	outportb(WD_PORT+WD_SDH,
		WDSDH_EXT|WDSDH_512 | trk | (cur_unit << 4));
	outportb(WD_PORT+WD_CMD,
		(cur_op == FS_READ) ? WDC_READ : WDC_WRITE);

	/*
	 * Feed data immediately for write
	 */
	if (cur_op == FS_WRITE) {
		while ((inportb(WD_PORT+WD_STATUS) & WDS_DRQ) == 0) {
			;
		}
		load_sect();
	}
}

/*
 * wd_isr()
 *	Called on interrupt to the WD IRQ
 */
void
wd_isr(void)
{
	uint stat;
	int done = 0;

	/*
	 * Get status.  If this interrupt was meaningless, just
	 * log it and return.
	 */
	stat = inportb(WD_PORT+WD_STATUS);
	if ((stat & WDS_BUSY) || !busy) {
		wd_strayintr += 1;
		return;
	}

	/*
	 * Error--abort current activity, log error
	 */
	if (stat & WDS_ERROR) {
		void *v;

		printf("wd: hard error unit %d sector %d error=0x%x\n",
			cur_unit, cur_sec, inportb(WD_PORT+WD_ERROR));
		v = busy;
		busy = 0;
		iodone(v, -1);
		return;
	}

	/*
	 * Quietly tally soft errors
	 */
	if (stat & WDS_ECC) {
		wd_softerr += 1;
	}

	/*
	 * Read in or write out next sector
	 */
	if (cur_op == FS_READ) {
		/*
		 * Sector is ready; read it in and advance counters
		 */
		repinsw(WD_PORT+WD_DATA, cur_vaddr, SECSZ/sizeof(ushort));
		cur_vaddr += SECSZ;
		cur_sec += 1;
		cur_secs -= 1;
		cur_xfer -= 1;
	} else {
		/*
		 * Writes are in two phases; first we are interrupted
		 * to provide data, then we're interrupted when the
		 * data is written.
		 */
		if (stat & WDS_DRQ) {
			load_sect();
		} else {
			done = 1;
		}
	}

	/*
	 * Done with current transfer--complete I/O, or start next part
	 */
	if (((cur_op == FS_READ) && (cur_xfer == 0)) || done) {
		if (cur_secs > 0) {
			/*
			 * This will calculate next step, and fire
			 * the I/O.
			 */
			wd_start();
		} else {
			void *v = busy;

			/*
			 * All done.  Let upper level know.
			 */
			busy = 0;
			iodone(v, 0);
		}
	}
}

/*
 * wd_cmd()
 *	Send a command and wait for controller acceptance
 *
 * Returns -1 on error, otherwise value of status byte.
 */
static
wd_cmd(int cmd)
{
	uint count;
	int stat;

	/*
	 * Wait for controller to be ready
	 */
	count = 100000;
	while (inportb(WD_PORT + WD_STATUS) & WDS_BUSY) {
		if (--count == 0) {
			return(-1);
		}
	}

	/*
	 * Send command, wait for controller to finish
	 */
	outportb(WD_PORT + WD_CMD, cmd);
	count = 100000;
	for (;;) {
		stat = inportb(WD_PORT + WD_STATUS);
		if ((stat & WDS_BUSY) == 0) {
			return(stat);
		}
		if (--count == 0) {
			return(-1);
		}
	}
}

/*
 * readp_data()
 *	After sending a READP command, wait for data and place in buffer
 */
static
readp_data(void)
{
	uint count;
	int stat;

	/*
	 * Wait for data or error
	 */
	for (;;) {
		stat = inportb(WD_PORT + WD_STATUS);
		if (stat & WDS_ERROR) {
			return(-1);
		}
		if (stat & WDS_DRQ) {
			return(stat);
		}
		if (--count == 0) {
			return(-1);
		}
	}
}

/*
 * wd_readp()
 *	Issue READP to drive, get its geometry and such
 */
static
wd_readp(int unit)
{
	char buf[SECSZ];
	struct wdparms *w;
	struct wdparameters *xw;

	/*
	 * Send READP and see if he'll answer
	 */
	outportb(WD_PORT+WD_SDH, WDSDH_EXT|WDSDH_512 | (unit << 4));
	if (wd_cmd(WDC_READP) < 0) {
		return(-1);
	}

	/*
	 * Read in the parameters
	 */
	if (readp_data() < 0) {
		return(-1);
	}
	repinsw(WD_PORT+WD_DATA, buf, sizeof(buf)/sizeof(ushort));
	xw = &xparm[unit];
	bcopy(buf, xw, sizeof(*xw));

	/*
	 * Fix big-endian lossage
	 */
	SWAB(xw->w_model);

	/*
	 * Massage into a convenient format
	 */
	w = &parm[unit];
	w->w_cyls = xw->w_fixedcyl + xw->w_removcyl;
	w->w_tracks = xw->w_heads;
	w->w_secpertrk = xw->w_sectors;
	w->w_secpercyl = xw->w_heads * xw->w_sectors;
	w->w_size = w->w_secpercyl * w->w_cyls;
}
