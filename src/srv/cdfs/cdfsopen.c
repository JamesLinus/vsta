/*
 * cdfsopen.c - open file processing.
 */
#include <stdio.h>
#include <sys/fs.h>
#include "cdfs.h"

/*
 * cdfs_open - open the next component of a pathname.
 */
long	cdfs_open(struct cdfs_file *file, char *name, int flags)
{
	long	status;
	static	char *myname = "cdfs_open";

	CDFS_DEBUG_FCN_ENTRY(myname);
/*
 * Current node must be a directory.
 */
	if(!(*file->node.flags & ISO_DIRECTORY)) {
		status = CDFS_ENOTDIR;
/*
 * Check for write access attempt.
 */
	} else if(flags & (ACC_WRITE | ACC_CREATE)) {
		status = CDFS_ROFS;
/*
 * Lookup the new path component.
 */
	} else {
		status = cdfs_lookup_name(file, name, TRUE);
/*
 * XXX Check for read access.
		if(status == CDFS_SUCCESS) {
		}
 */

	}

	CDFS_DEBUG_FCN_EXIT(myname, status);

	return(status);
}

