/*
 * cdfsopen.c - open file processing.
 */
#include <stdio.h>
#include <sys/fs.h>
#include "cdfs.h"

static	struct iso_extended_attributes cdfs_default_attrs;

/*
 * cdfs_open - open the next component of a pathname.
 */
long	cdfs_open(struct cdfs_file *file, char *name, int flags)
{
	struct	iso_directory_record newnode;
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
		status = cdfs_lookup_name(file, name, &newnode);
		if(status == CDFS_SUCCESS) {
/*
 * XXX Check for read access.
 */

/*
 * Update the open file structure.
 */
			file->node = newnode;
/*
 * Get extended attributes, if possible.
 */
			file->attrs = cdfs_default_attrs;
			if(isonum_711(newnode.ext_attr_length) > 0)
				(void)cdfs_read_attrs(file, &file->attrs);
		}
	}

	CDFS_DEBUG_FCN_EXIT(myname, status);

	return(status);
}

