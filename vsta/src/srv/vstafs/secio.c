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
#include <vstafs/vstafs.h>
#include <unistd.h>
#include <sys/assert.h>

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
	ASSERT(lseek(blkdev, o, SEEK_SET) == o, "read_sec: seek error");
	ASSERT(read(blkdev, p, SECSZ) == SECSZ, "read_sec: I/O error");
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
	ASSERT(lseek(blkdev, o, SEEK_SET) == o, "write_sec: seek error")
	ASSERT(write(blkdev, p, SECSZ) == SECSZ, "write_sec: I/O error");
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
	ASSERT(lseek(blkdev, o, SEEK_SET) == o, "read_sec: seek error");
	ASSERT(read(blkdev, p, sz) == sz, "read_secs: I/O error");
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
	ASSERT(lseek(blkdev, o, SEEK_SET) == o, "write_sec: seek error");
	ASSERT(write(blkdev, p, sz) == sz, "write_secs: I/O error");
}
