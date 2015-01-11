/*
 * cdfsread.c - file read operations.
 */
#include <stdio.h>
#include <string.h>
#include "cdfs.h"

/*
 * cdfs_read - read from the current directory or file.
 */
long	cdfs_read(struct cdfs_file *file, char *buffer, long *length)
{
	void	*bp, *data;
	int	resid, count, boff, step, blk;
	long	status, filesize = isonum_733(file->node.size);
	static	char *myname = "cdfs_read";

	CDFS_DEBUG_FCN_ENTRY(myname);
/*
 * Is the current file position at EOF?
 */
	if(file->position >= filesize) {
		*length = 0;
		CDFS_DEBUG_FCN_EXIT(myname, CDFS_SUCCESS);
		return(CDFS_SUCCESS);
	}
/*
 * Calculate the number of bytes to get.
 */
	if((resid = *length) > (filesize - file->position))
		resid = filesize - file->position;

	count = 0;
	status = CDFS_SUCCESS;
	while(resid > 0) {
/*
 * Calculate how much to take out of current block.
 */
		boff = file->position & (file->cdfs->lbsize - 1);
		step = file->cdfs->lbsize - boff;
		if(step > resid)
			step = resid;
/*
 * Get the next block from the file.
 */
		blk = cdfs_bmap(file, file->position);
		if((bp = cdfs_getblk(file->cdfs, blk, 1, &data)) == NULL) {
			status = CDFS_EIO;
			break;
		}
		bcopy((char *)data + boff, buffer + count, step);

		file->position += step;
		cdfs_relblk(file->cdfs, bp);

		count += step;
		resid -= step;
	}

	*length = count;

	CDFS_DEBUG_FCN_EXIT(myname, status);

	return(status);
}

/*
 * cdfs_bmap - convert the input file offset into a disk block address.
 * Assumes that the disk block size and the "logical block size" are
 * the same.
 */
long	cdfs_bmap(struct cdfs_file *file, long offset)
{
	return(isonum_733(file->node.extent) +
	       isonum_711(file->node.ext_attr_length) +
	       (offset / file->cdfs->lbsize));
}

