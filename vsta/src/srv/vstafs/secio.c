/*
 * secio.c
 *	Routines for reading and writing sectors
 *
 * The problem of I/O errors is handled using a simplistic approach.  All
 * of the filesystem is designed so that on-disk structures will be
 * consistent if the filesystem is aborted at any given point in time.
 * Rather than further complicate this, we simply abort on I/O error,
 * and fall back onto this design.
 *
 * Lots of filesystems have strategies for going on in the face of I/O
 * errors.  Lots of filesystems get scary after an I/O error.  Use a
 * mirror if you really care.
 */
#include <stdio.h>
#include <unistd.h>
#include <vstafs/vstafs.h>

extern int blkdev;

/*
 * read_sec()
 *	Read a sector
 */
void
read_sec(daddr_t d, void *p)
{
	off_t o;

	o = (off_t)stob(d);
	if (lseek(blkdev, o, SEEK_SET) != o) {
		fprintf(stderr, "read_sec: seek error to off %ld\n", (long)o);
		exit(1);
	}
	if (read(blkdev, p, SECSZ) != SECSZ) {
		fprintf(stderr, "read_sec: read error off %ld buf 0x%x\n",
			(long)o, p);
		exit(1);
	}
}

/*
 * write_sec()
 *	Write a sector
 */
void
write_sec(daddr_t d, void *p)
{
	off_t o;

	o = (off_t)stob(d);
	if (lseek(blkdev, o, SEEK_SET) != o) {
		fprintf(stderr, "write_sec: seek error to off %ld\n", (long)o);
		exit(1);
	}
	if (write(blkdev, p, SECSZ) != SECSZ) {
		fprintf(stderr, "write_sec: write error off %ld buf 0x%x\n",
			(long)o, p);
		exit(1);
	}
}

/*
 * read_secs()
 *	Read sectors
 */
void
read_secs(daddr_t d, void *p, uint nsec)
{
	off_t o;
	uint sz = stob(nsec);

	o = (off_t)stob(d);
	if (lseek(blkdev, o, SEEK_SET) != o) {
		fprintf(stderr, "read_sec: seek error to off %ld\n", (long)o);
		exit(1);
	}
	if (read(blkdev, p, sz) != sz) {
		fprintf(stderr,
			"read_secs: read error off %ld buf 0x%x size %d\n",
			(long)o, p, sz);
		exit(1);
	}
}

/*
 * write_secs()
 *	Write sectors
 */
void
write_secs(daddr_t d, void *p, uint nsec)
{
	off_t o;
	uint sz = stob(nsec);

	o = (off_t)stob(d);
	if (lseek(blkdev, o, SEEK_SET) != o) {
		fprintf(stderr, "write_sec: seek error to off %ld\n", (long)o);
		exit(1);
	}
	if (write(blkdev, p, sz) != sz) {
		fprintf(stderr,
			"write_secs: write error off %ld buf 0x%x size %d\n",
			(long)o, p, sz);
		exit(1);
	}
}
