/*
 * dpart.c
 *	Code for handling IBM PC fdisk-style disk partitioning
 *
 * Andy Valencia: Original code for the wd server.
 * Mike Larson: Added support for DOS extended partition types 
 * 	in the scsi server.
 * Dave Hudson: Created "libdpart.a".  Added support for Linux, HPFS and
 *	VSTa boot filesystems.  Modified the extended partition support to
 *	handle non-DOS partitions.
 */
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/perm.h>
#include <sys/assert.h>
#include <mach/dpart.h>

/*
 * alloc_part_slot()
 *	Allocate space to the specified partition table entry
 *
 * We check to see if space has already been allocated to the partition
 * entry, and if it has we simply return a pointer to that space.  If the
 * space hasn't been allocated we allocate it and return the normal
 * result from the malloc.
 *
 * Whatever else we do if space has somehow been allocated, we zero the
 * space down
 */
static struct part *
alloc_part_slot(struct part **p)
{
	if (!*p) {
		*p = (struct part *)malloc(sizeof(struct part));
	}
	if (*p) {
		bzero(*p, sizeof(struct part));
	}
	
	return *p;
}

/*
 * dpart_init_whole()
 *	Initialise the drive information on a given disk
 *
 * We're basically writing the details of the full drive away into part of
 * the disk partition list as this makes for more simple handling elsewhere
 *
 * We expect the unit number, root device name and the size of the drive
 * in sectors.  We also want a pointer to the partition list
 *
 * We return 1 for sucessful registrations and 0 for failures
 */
int
dpart_init_whole(char *name, uint unit, uint sec_size,
		 struct part *partition[])
{
	struct part *p;

	if ((p = alloc_part_slot(&partition[WHOLE_DISK])) == NULL) {
		return 0;
	}
	p->p_val = 1;
	p->p_off = 0;
	p->p_len = sec_size;
	sprintf(p->p_name, "%s%d", name, unit);
}

/*
 * dpart_init()
 *	Initialize partition information on a given disk
 *
 * We want to get a list of all of the partitions on a specified disk, using
 * symbolic names that are meaningful (relate to the type of data stored
 * therein).
 *
 * dpart_init() expects to be passed the root device name (eg wd) and the
 * drive unit number (eg 0).  It expects a pointer to a sector buffer
 * containing partition table information, and a pointer to the sector number
 * that this buffer corresponds to.  Finally It expects a pointer to an array
 * of partition details, and a pointer to the number of the next slot to be
 * filled in this array (this and the sector number will be updated so that
 * any extended data can be filled in).
 *
 * The routine expects that the partition array will initially be full of NULL
 * pointers that can be dynamically allocated, and will assume that any non
 * NULL pointer values are in fact pointing to space that is already allocated
 * and can be reused.
 *
 * On exit we return 1 for success and 0 for failure (eg allocation errors).
 * Note that overflowing the number of partition entries for a drive does not
 * count as an error.
 */
int
dpart_init(char *name, uint unit, char *secbuf,
	   uint *sec_num, struct part *partition[], int *next_part)
{
	struct part *p;
	struct part_slot *s;
	int x;
	static int vsswap, vsbfs;
	static int ext, dos, hpfs, bsd, minix;
	static int lxnat, lxminix, lxswap;
	static int ext_base_sec;
	int extended = 0;

	/*
	 * Are we restarting the list here?
	 */
	if (*next_part == FIRST_PART) {
		vsswap = 0;
		vsbfs = 0;
		ext = 0;
		dos = 0;
		hpfs = 0;
		bsd = 0;
		minix = 0;
		lxnat = 0;
		lxminix = 0;
		lxswap = 0;
		ext_base_sec = 0;
	}

	/*
	 * Sanity check contents
	 */
	if ((SIG(secbuf, 0) != 0x55) || (SIG(secbuf, 1) != 0xAA)) {
		syslog(LOG_ALERT,
		       "%s%d: invalid partition signature.\n", name, unit);
		return 0;
	}

	/*
	 * Massage the partitioning
	 */
	for (x = 0; x < NPART; x++) {
		if (*next_part > LAST_PART) {
			/*
			 * If we've overflowed our slots, abort.  Make it
			 * look like it was a valid return however.
			 */
			*sec_num = 0;
			return 1;
		}

		/*
		 * Sort out the space for partition data
		 */
		if ((p = alloc_part_slot(&partition[*next_part])) == NULL) {
			return 0;
		}

		/*
		 * Point to next partition's info in the sector buffer
		 */
		s = PART(secbuf, x);

		/*
		 * Fill in off/len.  They're ignored if we don't
		 * set p_val below.
		 */
		p->p_off = s->ps_start;
		p->p_len = s->ps_len;

		/*
		 * Look up type
		 */
		switch (s->ps_type) {
		case 0:			/* Idle partition */
			break;

		case PT_EXT:		/* Extended partition */
			*sec_num = ext_base_sec + p->p_off;
			if (!ext_base_sec) {
				ext_base_sec = p->p_off;
			}
			extended = 1;
			break;

		case PT_DOS12:		/* DOS filesystem */
		case PT_DOS16:
		case PT_DOSBIG:
			sprintf(p->p_name, "%s%d_dos%d", name,
				unit, dos++);
			p->p_val = 1;
			break;

		case PT_OS2HPFS:       	/* OS/2 HPFS fs */
			sprintf(p->p_name, "%s%d_hpfs%d", name,
				unit, hpfs++);
			p->p_val = 1;
			break;

		case PT_MINIX:		/* Minix fs */
			sprintf(p->p_name, "%s%d_minix%d", name,
				unit, minix++);
			p->p_val = 1;
			break;

		case PT_LXMINIX:	/* Linux/Minix fs */
			sprintf(p->p_name, "%s%d_lxminix%d", name,
				unit, lxminix++);
			p->p_val = 1;
			break;

		case PT_LXSWAP:		/* Linux swap */
			sprintf(p->p_name, "%s%d_lxswap%d", name,
				unit, lxswap++);
			p->p_val = 1;
			break;

		case PT_LXNAT:		/* Linux native fs - eg ext2 */
			sprintf(p->p_name, "%s%d_lxnat%d", name,
				unit, lxnat++);
			p->p_val = 1;
			break;

		case PT_VSBFS:		/* VSTa boot fs */
			sprintf(p->p_name, "%s%d_bfs%d", name, unit, vsbfs++);
			p->p_val = 1;
			break;

		case PT_VSSWAP:		/* VSTa swap */
			sprintf(p->p_name, "%s%d_swap%d", name,
				unit, vsswap++);
			p->p_val = 1;
			break;

		case PT_386BSD:		/* 386BSD McKusick-type fs */
			sprintf(p->p_name, "%s%d_bsd%d", name, unit, bsd++);
			p->p_val = 1;
			break;

		default:		/* Unknown */
			/*
			 * Ignore zero-length
			 */
			if (s->ps_len == 0) {
				continue;
			}
			sprintf(p->p_name, "%s%d_p%d", name, unit, x);
			p->p_val = 1;
			break;
		}

		if (s->ps_type != 0 && s->ps_type != PT_EXT) {
			(*next_part)++;
printf("current name %s\n", p->p_name);
		}
	}
	if(!extended)
		*sec_num = 0;

	return 1;
}

/*
 * dpart_get_offset()
 *	Given f_node, return an offset or error
 *
 * There are three cases; the offset is within the partition--return 0.
 * The offset is at the first byte beyond the partition--return 1.
 * Or, the offset is outside the partition--return 2.  This allows our
 * caller to differentiate between EOF (reaching the end of the partition
 * after having read sequentially through it) and an illegal offset (say,
 * hosery with lseek().)
 *
 * This routine is written with a fair amount of trust in our caller;
 * we expect that he has screened illegal units and accesses to the
 * root directory.
 *
 * All values are in units of sectors.
 */
int
dpart_get_offset(struct part *partition[], int part_slot, ulong off,
		 ulong *offp, uint *cntp)
{
	ulong plen, poff;
	struct part *p = partition[part_slot];

	ASSERT_DEBUG((part_slot < MAX_PARTS), "dpart_get_offset: slot");
	ASSERT_DEBUG(p, "dpart_get_offset: unitialised slot");

	/*
	 * Calculate offset in sectors, get pointer to partition.
	 */
	ASSERT_DEBUG(p->p_val == 1, "get_offset: bad part");
	plen = p->p_len;
	poff = p->p_off;

	/*
	 * Outside partition
	 */
	if (off >= plen) {
		if (off == plen) {
			return 1;
		}
		return 2;
	}

	/*
	 * Return offset/length
	 */
	*offp = (off + poff);
	*cntp = (plen - off);
	return 0;
}
