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
#include <fcntl.h>
#include <sys/assert.h>
#include <stdio.h>
#include <std.h>

extern int blkdev;

/*
 * do_read()/do_write()/do_seek()
 *	Wrappers so we can share debugging complaints
 */
static void
do_read(void *buf, uint nbyte)
{
	int x;

	x = read(blkdev, buf, nbyte);
#ifdef DEBUG
	if (x != nbyte) {
		perror("read");
		fprintf(stderr, "read(%d, %x, %d) returns %d\n",
			blkdev, (uint)buf, nbyte, x);
	}
#endif
	ASSERT(x == nbyte, "do_read: I/O failed");
}
static void
do_write(void *buf, uint nbyte)
{
	int x;

	x = write(blkdev, buf, nbyte);
#ifdef DEBUG
	if (x != nbyte) {
		perror("write");
		fprintf(stderr, "write(%d, %x, %d) returns %d\n",
			blkdev, (uint)buf, nbyte, x);
	}
#endif
	ASSERT(x == nbyte, "do_write: I/O failed");
}
static void
do_lseek(off_t off)
{
	off_t o;

	o = lseek(blkdev, off, SEEK_SET);
#ifdef DEBUG
	if (o != off) {
		perror("lseek");
		fprintf(stderr, "lseek(%d, %ld) returns %ld\n",
			blkdev, off, o);
	}
#endif
	ASSERT(o == off, "do_lseek: seek failed");
}

/*
 * read_sec()
 *	Read a sector
 */
void
read_sec(daddr_t d, void *p)
{
	do_lseek(stob(d));
	do_read(p, SECSZ);
}

/*
 * write_sec()
 *	Write a sector
 */
void
write_sec(daddr_t d, void *p)
{
	do_lseek(stob(d));
	do_write(p, SECSZ);
}

/*
 * read_secs()
 *	Read sectors
 */
void
read_secs(daddr_t d, void *p, uint nsec)
{
	do_lseek(stob(d));
	do_read(p, stob(nsec));
}

/*
 * write_secs()
 *	Write sectors
 */
void
write_secs(daddr_t d, void *p, uint nsec)
{
	do_lseek(stob(d));
	do_write(p, stob(nsec));
}
