/*
 * cdfsmount.c - read in the CDROM FS primary descriptor.
 */
#include <stdio.h>
#include "string.h"
#include "cdfs.h"

void	cdfs_ckrripfs(struct cdfs *cdfs);

/*
 * cdfs_mount
 *	Look for and read in a partition header.
 */
long	cdfs_mount(struct cdfs *cdfs)
{
	struct	iso_primary_descriptor *vdp;
	struct	hs_primary_descriptor *hdp;
	void	*bp;
	int	sect, first = 16, last = 100;
	long	status = CDFS_ENOENT;
	extern	int blocksize;
	static	char *myname = "cdfs_mount";

	CDFS_DEBUG_FCN_ENTRY(myname);
/*
 * Start w/ a block size of 2048.
 */
	blocksize = 2048;
/*
 * Scan through the first blocks on the disk looking for a partition
 * header.
 */
	status = CDFS_ENOENT;
	cdfs->flags = 0;
	for(sect = first; (sect <= last) && (status == CDFS_ENOENT); sect++) {
		if((bp = cdfs_getblk(cdfs, sect, 1, (void *)&vdp)) == NULL) {
			status = CDFS_EIO;
			CDFS_DEBUG(CDFS_DBG_MSG, myname, "getblk error", 0);
			break;
		}
		hdp = (struct hs_primary_descriptor *)vdp;

		if(*vdp->type == (char)ISO_VD_PRIMARY) {
/*
 * ISO 9660 primary partition.
 */
			cdfs->root_dir = *(struct iso_directory_record *)
			                   vdp->root_directory_record;
			cdfs->lbsize = isonum_723(vdp->logical_block_size);
			status = CDFS_SUCCESS;
		} else if(*vdp->type == (char)ISO_VD_END) {
/*
 * ISO 9660 end marker - don't look any further.
 */
			status = CDFS_EINVAL;
			CDFS_DEBUG(CDFS_DBG_MSG, myname,
			           "primary not found", 0);
		} else if(strncmp(hdp->id, HS_STANDARD_ID,
		                  sizeof(hdp->id)) == 0) {
/*
 * High Sierra file system.
 */
			cdfs->flags |= CDFS_HIGH_SIERRA;
			cdfs->root_dir = *(struct iso_directory_record *)
			                   hdp->root_directory_record;
			*cdfs->root_dir.flags =
			          CDFS_HS_DIR_FLAGS(hdp->root_directory_record);
			cdfs->lbsize = isonum_723(hdp->logical_block_size);
			status = CDFS_SUCCESS;
		}
		cdfs_relblk(cdfs, bp);
	}
/*
 * Check for SUSP/RRIP capability.
 */
	cdfs_ckrripfs(cdfs);

	CDFS_DEBUG_FCN_EXIT(myname, status);

	return(status);
}
		
/*
 * cdfs_unmount
 *	Free resources.
 */
void	cdfs_unmount(struct cdfs *cdfs)
{
	static	char *myname = "cdfs_unmount";

	CDFS_DEBUG_FCN_ENTRY(myname);
	bcache_inval();
	cdfs->flags = 0;
	CDFS_DEBUG_FCN_EXIT(myname, 0);
}

/*
 * cdfs_ckrripfs - determine if the current filesystem SUSP/RRIP capable.
 */
void	cdfs_ckrripfs(struct cdfs *cdfs)
{
	struct	iso_directory_record *dp;
	struct	cdfs_file file;
	long	length;
	char	rootent0[ISO_MAX_LEN_DR];

	if(cdfs->flags & CDFS_HIGH_SIERRA)
		return;
/*
 * Get the first root directory entry.
 */
	cdfs_init_file(cdfs, 0, &file);
	dp = (struct iso_directory_record *)rootent0;
	length = sizeof(rootent0);
	if(cdfs_read(&file, (char *)dp, &length) == CDFS_SUCCESS) {
/*
 * Does the entry contain the SUSP signature?
 */
		if(length > sizeof(struct iso_directory_record))
			if(cdfs_cksusp(dp, &cdfs->susp_lenskp))
				cdfs->flags |= CDFS_RRIP;
	}
}

