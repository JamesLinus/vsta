/*
 * wd.c
 *	Western Digital hard disk handling
 *
 * This level of code assumes a single transfer is active at a time.
 * It will handle a contiguous series of sector transfers.
 */
#include <sys/types.h>
#include <sys/fs.h>
#include <sys/assert.h>
#include <sys/syscall.h>
#include <mach/nvram.h>
#include <mach/io.h>
#include <syslog.h>
#include <std.h>
#include <stdio.h>
#include <time.h>
#include "wd.h"

static int wd_cmd(int);
static void wd_start(), wd_readp(int), wd_cmos(int),
	wd_parseparms(int, char *);

uint first_unit;		/* Lowest unit # configured */

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
 * swab2()
 *	Swap bytes in a range
 */
#define SWAB(field) swab2(&(field), sizeof(field))
static void
swab2(void *ptr, uint cnt)
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
	repoutsw(wd_baseio + WD_DATA, cur_vaddr,
		SECSZ/sizeof(ushort));
	cur_vaddr += SECSZ;
	cur_sec += 1;
	cur_secs -= 1;
	cur_xfer -= 1;
}

/*
 * usage()
 *	Tell how to use
 */
static void
usage(void)
{
	fprintf(stderr, "Usage: wd [ch[1-2] | userdef] <drive-configs>" \
			" [opts=args] ...\n\n");
	fprintf(stderr, "       drive-configs: d[number]:readp\n" \
			"                      d[number]:cmos\n" \
			"                      d[number]:[cyls]:" \
			"[tracks]:[sectors]\n\n");
	fprintf(stderr, "       options:       iobase=<I/O-base-address>\n" \
			"                      irq=<IRQ-number>\n" \
			"                      namer=<namer-entry>\n\n");
	exit(1);
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
wd_init(int argc, char **argv)
{
	int i = 1;
	int x;
	int found_first = 0;
	int cfg_params[NWD] = {0, 0};

	/*
	 * Start checking the usage
	 */
	if (argc < 2) {
		usage();
	}
	if (!strncmp(argv[1], "ch", 2)) {
		int p;

		/*
		 * We're using a standard disk definition - find the lookup
		 * table reference details
		 */		
		p = argv[1][2] - '1';
		if (p < 0 || p >= NWD) {
			usage();
		}
		if (p == 0) {
			wd_baseio = WD1_PORT;
			wd_irq = WD1_IRQ;
			strcpy(wd_namer_name, "disk/wd1");
		} else {
			wd_baseio = WD2_PORT;
			wd_irq = WD2_IRQ;
			strcpy(wd_namer_name, "disk/wd2");
		}
		i++;
	} else if (!strcmp(argv[1], "userdef")) {
		/*
		 * Set initial conditions for a user-defined setup.  Basically
		 * make sure that there are no defaults
		 */
		wd_baseio = 0;
		wd_irq = 0;
		i++;
	} else {
		/*
		 * Establish the default
		 */
		wd_baseio = WD1_PORT;
		wd_irq = WD1_IRQ;
		strcpy(wd_namer_name, "disk/wd");
	}

	/*
	 * Start processing the option parameters
	 */
	while (i < argc) {
		if (argv[i][0] == 'd') {
			/*
			 * Select drive parameters
			 */
			int n;
			
			n = argv[i][1] - '0';
			if (n < 0 || n >= NWD) {
				fprintf(stderr, "wd: non supported drive " \
					"number '%c' - aborting\n",
					argv[i][1]);	
				exit(1);
			}
			cfg_params[n] = i;
		} else if (!strncmp(argv[i], "irq=", 4)) {
			/*
			 * Select a new IRQ line
			 */
			wd_irq = atoi(&argv[i][4]);
		} else if (!strncmp(argv[i], "baseio=", 7)) {
			/*
			 * Select a new base I/O port address
			 */
			wd_baseio = atoi(&argv[i][7]);
		} else if (!strncmp(argv[i], "namer=", 6)) {
			/*
			 * Select a new namer entry
			 */
			if ((strlen(&argv[i][6]) == 0)
			    || (strlen(&argv[i][6]) >= NAMESZ)) {
				fprintf(stderr, "wd: invalid name '%s' " \
					"- aborting\n", &argv[i][6]);
				exit(1);
			}
			strcpy(wd_namer_name, &argv[i][6]);
		} else {
			fprintf(stderr,
				"wd: unknown option '%s' - aborting\n",
				argv[i]);
			exit(1);
		}
		i++;
	}

	/*
	 * Check that after all of the messing about we have a valid set
	 * of parameters - report failures if we don't
	 */
	if (wd_baseio == 0) {
		fprintf(stderr,
			"wd: no I/O base address specified - aborting\n");
		exit(1);
	}
	if (wd_irq == 0) {
		fprintf(stderr,
			"wd: no IRQ line specified - aborting\n");
		exit(1);
	}
	if (wd_namer_name[0] == '\0') {
		fprintf(stderr,
			"wd: no namer entry specified - aborting\n");
		exit(1);
	}

	/*
	 * Enable I/O for the needed range
	 */
	if (enable_io(wd_baseio, wd_baseio + WD_CTLR) < 0) {
		syslog(LOG_ERR, "I/O permissions not granted");
		exit(1);
	}

	/*
	 * Send reset to controller, wait, drop reset bit.
	 */
	outportb(wd_baseio + WD_CTLR, CTLR_RESET|CTLR_IDIS);
	__msleep(100);
	outportb(wd_baseio + WD_CTLR, CTLR_IDIS);
	__msleep(100);

	/*
	 * Ask him if he's OK
	 */
	if (wd_cmd(WDC_DIAG) < 0) {
		syslog(LOG_ERR, "controller failed self diagnostic");
		exit(1);
	}
	inportb(wd_baseio + WD_ERROR);

	/*
	 * Allow interrupts now
	 */
	outportb(wd_baseio + WD_CTLR, CTLR_4BIT);

	/*
	 * Scan the unit whose parameters were specified
	 */
	for (i = 0; i < NWD; i++) {
		disks[i].d_configed = 0;
		x = cfg_params[i];
		if (!x) {
			continue;
		}

		/*
		 * Verify colon and argument
		 */
		if ((argv[x][2] != ':') || !argv[x][3]) {
			fprintf(stderr,
				"wd: bad argument '%s' - aborting\n",
				argv[cfg_params[i]]);
			exit(1);
		}

		/*
		 * Now get drive parameters in the requested way
		 */
		if (!strcmp(argv[x]+3, "readp")) {
			wd_readp(i);
		} else if (!strcmp(argv[x]+3, "cmos")) {
			wd_cmos(i);
		} else {
			wd_parseparms(i, argv[x]+3);
		}

		if (disks[i].d_configed) {
			struct wdparms *w = &disks[i].d_parm;
			uint s = w->w_size * SECSZ;
			const uint m = 1024*1024;

			syslog(LOG_INFO,
				"unit %d: %d.%dM - " \
				"%d heads, %d cylinders, %d sectors",
				i, s / m, (s % m) / (m / 10),
				w->w_tracks, w->w_cyls, w->w_secpertrk);
			found_first = 1;
			if (i < first_unit) {
				first_unit = i;
			}
		}
	}
	if (!found_first) {
		syslog(LOG_ERR, "no units found, exiting");
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
int
wd_io(int op, void *handle, uint unit, ulong secnum, void *va, uint secs)
{
	ASSERT_DEBUG(unit < NWD, "wd_io: bad unit");
	ASSERT_DEBUG(busy == 0, "wd_io: busy");
	ASSERT_DEBUG(handle != 0, "wd_io: null handle");
	ASSERT_DEBUG(secs > 0, "wd_io: 0 len");

	/*
	 * If not configured, error out
	 */
	if (!disks[unit].d_configed) {
		return(1);
	}
	ASSERT_DEBUG(secnum < disks[unit].d_parm.w_size, "wd_io: high sector");

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
	struct wdparms *w = &disks[cur_unit].d_parm;
	uint base = wd_baseio;

	/*
	 * For laptops with power management, our inportb() here can
	 * cause the disk to wake up; the controller can then flag
	 * a busy disk until the spinup is complete.  Give it 5 seconds.
	 */
	cyl = 0;
	while (inportb(base + WD_STATUS) & WDS_BUSY) {
		if (++cyl > 50) {
			ASSERT(0, "wd_start: busy");
		}
		__msleep(100);
	}

	/*
	 * Given disk geometry, calculate parameters for next I/O
	 */
	cyl =  cur_sec / w->w_secpercyl;
	sect = cur_sec % w->w_secpercyl;
#ifdef AUTO_HEAD
	lsect = w->w_secpercyl - sect;
#endif
	trk = sect / w->w_secpertrk;
	sect = (sect % w->w_secpertrk) + 1;
#ifndef AUTO_HEAD
	/* Cap at end of this track--heads won't switch automatically */
	lsect = w->w_secpertrk + 1 - sect;
#endif

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
	outportb(base + WD_SCNT, cur_xfer);
	outportb(base + WD_SNUM, sect);
	outportb(base + WD_CYL0, cyl & 0xFF);
	outportb(base + WD_CYL1, (cyl >> 8) & 0xFF);
	outportb(base + WD_SDH,
		WDSDH_EXT|WDSDH_512 | trk | (cur_unit << 4));
	outportb(base + WD_CMD,
		(cur_op == FS_READ) ? WDC_READ : WDC_WRITE);

	/*
	 * Feed data immediately for write
	 */
	if (cur_op == FS_WRITE) {
		while ((inportb(base + WD_STATUS) & WDS_DRQ) == 0) {
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
	stat = inportb(wd_baseio + WD_STATUS);
	if ((stat & WDS_BUSY) || !busy) {
		wd_strayintr += 1;
		return;
	}

	/*
	 * Error--abort current activity, log error
	 */
	if (stat & WDS_ERROR) {
		void *v;

		syslog(LOG_ERR, "hard error unit %d sector %d error=0x%x",
		       cur_unit, cur_sec, inportb(wd_baseio + WD_ERROR));
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
		repinsw(wd_baseio + WD_DATA, cur_vaddr, SECSZ/sizeof(ushort));
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
static int
wd_cmd(int cmd)
{
	uint count;
	int stat;
	const uint timeout = 10000000;

	/*
	 * Wait for controller to be ready
	 */
	count = timeout;
	while (inportb(wd_baseio + WD_STATUS) & WDS_BUSY) {
		if (--count == 0) {
			return(-1);
		}
	}

	/*
	 * Send command, wait for controller to finish
	 */
	outportb(wd_baseio + WD_CMD, cmd);
	count = timeout;
	for (;;) {
		stat = inportb(wd_baseio + WD_STATUS);
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
static int
readp_data(void)
{
	uint count;

	/*
	 * Wait for data or error
	 */
	for (count = 200000; count > 0; --count) {
		uint stat;

		stat = inportb(wd_baseio + WD_STATUS);
		if (stat & WDS_ERROR) {
			return(-1);
		}
		if (stat & WDS_DRQ) {
			return(stat);
		}
	}
	return(-1);
}

/*
 * wd_readp()
 *	Issue READP to drive, get its geometry and such
 *
 * On success, sets disks[unit].d_configed to 1.
 */
static void
wd_readp(int unit)
{
	char buf[SECSZ];
	struct wdparms *w;
	struct wdparameters xw;

	/*
	 * Send READP and see if he'll answer
	 */
	outportb(wd_baseio + WD_SDH, WDSDH_EXT|WDSDH_512 | (unit << 4));
	if (wd_cmd(WDC_READP) < 0) {
		return;
	}

	/*
	 * Read in the parameters
	 */
	if (readp_data() < 0) {
		return;
	}
	repinsw(wd_baseio + WD_DATA, buf, sizeof(buf)/sizeof(ushort));
	bcopy(buf, &xw, sizeof(xw));

	/*
	 * Give the controller the geometry. I'm not really sure why my
	 * drive needs the little delay, but it did... (pat)
	 */
	__msleep(100);
	outportb(wd_baseio + WD_SDH,
		WDSDH_EXT|WDSDH_512 | (unit << 4) | (xw.w_heads - 1));
	outportb(wd_baseio + WD_SCNT, xw.w_sectors);
	if (wd_cmd(WDC_SPECIFY) < 0) {
		return;
	}

	/*
	 * Fix big-endian lossage
	 */
	SWAB(xw.w_model);

	/*
	 * Massage into a convenient format
	 */
	w = &disks[unit].d_parm;
	w->w_cyls = xw.w_fixedcyl + xw.w_removcyl;
	w->w_tracks = xw.w_heads;
	w->w_secpertrk = xw.w_sectors;
	w->w_secpercyl = xw.w_heads * xw.w_sectors;
	w->w_size = w->w_secpercyl * w->w_cyls;
	disks[unit].d_configed = 1;
}

/*
 * wd_parseparms()
 *	Parse user-specified drive parameters
 */
static void
wd_parseparms(int unit, char *parms)
{
	struct wdparms *w = &disks[unit].d_parm;
	char *p;

	w->w_cyls = atoi(parms);
	p = strchr(parms, ':');
	if (p) {
		w->w_tracks = atoi(++p);
		p = strchr(p, ':');
	}
	if (p) {
		w->w_secpertrk = atoi(++p);
	}
	if (!p) {
		syslog(LOG_ERR, "unit %d: bad parameters: %s",
		       unit, parms);
		return;
	}
	w->w_secpercyl = w->w_tracks * w->w_secpertrk;
	w->w_size = w->w_secpercyl * w->w_cyls;
	disks[unit].d_configed = 1;
}

/*
 * cmos_read()
 *	Read a byte through the NVRAM interface
 */
static uint
cmos_read(unsigned char address)
{
	outportb(RTCSEL, address);
	return(inportb(RTCDATA));
}

/*
 * wd_cmos()
 *	Get disk parameters from NVRAM BIOS storage
 */
static void
wd_cmos(int unit)
{
	const uint
		nv_cfg = 0x12,		/* Config'ed at all */
		nv_lo8 = 0x1B,		/* Low, high count of secs */
		nv_hi8 = 0x1C,
		nv_heads = 0x1D,	/* # heads */
		nv_sec = 0x23;		/* # sec/track */
	uint off =			/* I/O off for unit */
		((unit == 0) ? 0 : 9);
	struct wdparms *w;

	/*
	 * NVRAM only handles two
	 */
	if (unit > 1) {
		syslog(LOG_ERR, "unit %d: no CMOS information", unit);
		return;
	}

	/*
	 * Read config register to see if present
	 */
	if ((cmos_read(nv_cfg) & ((unit == 0) ? 0xF0 : 0x0F)) == 0) {
		return;
	}

	/*
	 * It appears present, so read in the parameters
	 */
	w = &disks[unit].d_parm;
	w->w_cyls = cmos_read(nv_hi8 + off) << 8 | cmos_read(nv_lo8 + off);
	w->w_tracks = cmos_read(nv_heads + off);
	w->w_secpertrk = cmos_read(nv_sec + off);

	/*
	 * Some NVRAM setups will have the drive marked present,
	 * but with zeroes for all parameters.  Pretend like it
	 * isn't there at all.
	 */
	if (!w->w_cyls || !w->w_tracks || !w->w_secpertrk) {
		return;
	}

	/*
	 * Otherwise calculate the reset & mark it present
	 */
	w->w_secpercyl = w->w_tracks * w->w_secpertrk;
	w->w_size = w->w_secpercyl * w->w_cyls;
	disks[unit].d_configed = 1;
}
