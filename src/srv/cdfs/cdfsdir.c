/*
 * cdfsdir.c - directory operations.
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "cdfs.h"

/*
 * Function prototypes.
 */
void	cdfs_fncvt(char *isoname, char *newname, int isoname_len);
int	cdfs_fncmp(char *isoname, char *string, int isoname_len);

/*
 * cdfs_get_dirname - extract a file name from a directory entry.
 */
static	int cdfs_get_fname(struct cdfs_file *file,
	                   struct iso_directory_record *dp,
	                   char *buffer, int buflen)
{
	char	*rripname;
	int	name_len;

	name_len = isonum_711(dp->name_len);
/*
 * Try for a RRIP alternate name.
 */
	rripname = NULL;
	if(file->cdfs->flags & CDFS_RRIP)
		cdfs_get_altname(dp, &rripname, &name_len);
/*
 * Room left in the current buffer?
 */
	if((name_len + 2) > buflen)
		return(FALSE);
/*
 * Copy the current entry's name.
 */
	if(rripname != NULL)
		strncat(buffer, rripname, name_len);
	else
		cdfs_fncvt(dp->name, buffer, name_len);

	buffer[name_len] = '\0';
	strcat(buffer, "\n");

	return(TRUE);
}

/*
 * cdfs_read_dir - copy the current directory names into the input
 * buffer. Copy up to length bytes. '*length' is set to the number
 * of bytes actually copied.
 */
long	cdfs_read_dir(struct cdfs_file *file, char *buffer, long *length)
{
	void	*bp;
	struct	iso_directory_record *dp = &file->node;
	long	status, lbn, file_off, file_size = isonum_733(dp->size);
	int	dplen, buff_off, index, count, finished;
	int	unsigned file_flags;
	static	char *myname = "cdfs_read_dir";

	CDFS_DEBUG_FCN_ENTRY(myname);

  	if(*length > file_size)
 		*length = file_size;
/*
 * Skip the first 2 directory entries.
 */
	if(file->position < 2)
		file->position = 2;

	status = CDFS_SUCCESS;
	buff_off = 0;
	file_off = 0;
	index = 0;
	finished = 0;
	while((buff_off < *length) && (file_off < file_size) &&
	      (status == CDFS_SUCCESS) && !finished) {
/*
 * Get the next block from the directory.
 */
		lbn = cdfs_bmap(file, file_off);

		bp = cdfs_getblk(file->cdfs, lbn, 1, (void **)&dp);
		if(bp == NULL) {
			*length = 0;
			CDFS_DEBUG_FCN_EXIT(myname, CDFS_EIO);
			return(CDFS_EIO);
		}
/*
 * Get the maximum number of bytes to copy from the current block.
 */
		count = file_size - file_off;
		if(count > file->cdfs->lbsize)
			count = file->cdfs->lbsize;
		if(count > *length)
			count = *length;
/*
 * Get the 'name' of each directory entry in the current block.
 */
		while(count > 0) {
			dplen = isonum_711(dp->length);
/*
 * Unused byte positions after the last directory record are set to 0.
 */
			if(dplen == 0)
				break;
/*
 * The 'flags' field is in a different place for High Sierra CDROM's.
 */
			if(file->cdfs->flags & CDFS_HIGH_SIERRA)
				file_flags =
				    *((struct hs_directory_record *)dp)->flags;
			else
				file_flags = *dp->flags;
/*
 * For directories, the file's position field is a directory entry index.
 * Don't copy directory names until the file's directory entry index
 * is reached. Also, only copy the last directory name of an multi-extent
 * file and don't copy associated file names.
 */
			if((index >= file->position) && (dplen > 0)) {
			    if(((file_flags & ISO_MULT_EXTENT) == 0) &&
			       ((file_flags & ISO_ASSOC_FILE) == 0)) {

				if(!cdfs_get_fname(file, dp, buffer + buff_off,
				                   *length - buff_off)) {
					finished = TRUE;
					break;
				}

				buff_off += strlen(buffer + buff_off);
			    }
/*
 * Count all directory entries in the file's position field.
 */
			    file->position++;
			}
/*
 * Update for the next directory entry.
 */
			count -= dplen;
			dp = (struct iso_directory_record *)
			                ((char *)dp + dplen);
			index++;
		}
		cdfs_relblk(file->cdfs, bp);
		file_off += file->cdfs->lbsize;
	}

	*length = buff_off;

	CDFS_DEBUG_FCN_EXIT(myname, status);

	return(status);
}

/*
 * cdfs_get_other_attrs - try to get extended and/or POSIX file
 * attributes.
 */
static	void cdfs_get_other_attrs(struct cdfs_file *file,
	                          struct iso_directory_record *dp)
{
	long	status;

	file->flags &= ~CDFS_ALL_ATTRS;
/*
 * Try to get extended attributes.
 */
	if(isonum_711(file->node.ext_attr_length) > 0) {
		status = cdfs_get_extnd_attrs(file, &file->extnd_attrs);
		if(status == CDFS_SUCCESS)
			file->flags |= CDFS_EXTND_ATTRS;
	}
/*
 * Try to get POSIX attributes.
 */
	if(file->cdfs->flags & CDFS_RRIP) {
		status = cdfs_get_posix_attrs(dp, &file->posix_attrs);
		if(status == CDFS_SUCCESS)
			file->flags |= CDFS_POSIX_ATTRS;
	}
}

/*
 * cdfs_lookup_name - look up 'name' in the current directory node.
 * If successful, return the new directory node in 'newnode'.
 */
long	cdfs_lookup_name(struct cdfs_file *file, char *name, int update)
{
	void	*bp;
	struct	iso_directory_record *dp;
	char	*rripname;
	long	file_size = isonum_733(file->node.size);
	long	status;
        int	lbn, file_off, count, name_len;
	static	char *myname = "cdfs_lookup_name";

	CDFS_DEBUG_FCN_ENTRY(myname);

	name_len = strlen(name);

	status = CDFS_ENOENT;
	file_off = 0;
	while((file_off < file_size) && (status == CDFS_ENOENT)) {
/*
 * Get the next block from the directory.
 */
		lbn = cdfs_bmap(file, file_off);

		bp = cdfs_getblk(file->cdfs, lbn, 1, (void **)&dp);
		if(bp == NULL) {
			CDFS_DEBUG_FCN_EXIT(myname, CDFS_EIO);
			return(CDFS_EIO);
		}
/*
 * Scan the current directory block for 'name'.
 */
		count = file_size - file_off;
		if(count > file->cdfs->lbsize)
			count = file->cdfs->lbsize;
		while(count > 0) {
/*
 * Does the current directory entry match?
 */
			rripname = NULL;
			if(file->cdfs->flags & CDFS_RRIP)
				cdfs_get_altname(dp, &rripname, &name_len);

			if(((rripname != NULL) &&
			     (strncmp(rripname, name, name_len) == 0)) ||
			   (cdfs_fncmp(dp->name, name, name_len) == 0)) {
				status = CDFS_SUCCESS;
				break;
			}
/*
 * Update for the next directory entry.
 */
			count -= isonum_711(dp->length);
			dp = (struct iso_directory_record *)
				((char *)dp + isonum_711(dp->length));
/*
 * Unused byte positions after the last directory record are set to 0.
 */
			if(isonum_711(dp->length) == 0)
				break;
		}
/*
 * If an entry was found, update the input file structure.
 */
		if(status == CDFS_SUCCESS) {
			if(update) {
				file->node = *dp;
				cdfs_get_other_attrs(file, dp);
			}

			if(file->cdfs->flags & CDFS_HIGH_SIERRA)
				*file->node.flags = CDFS_HS_DIR_FLAGS(dp);
		}

		cdfs_relblk(file->cdfs, bp);
		file_off += file->cdfs->lbsize;
	}

	CDFS_DEBUG_FCN_EXIT(myname, status);

	return(status);
}

/*
 * cdfs_get_extnd_attrs - read the input file's extended attribute data.
 */
long	cdfs_get_extnd_attrs(struct cdfs_file *file,
	                     struct iso_extended_attributes *attrs)
{
	void	*bp, *data;
	long	status = CDFS_SUCCESS;
	int	lbn, length;
	static	char *myname = "cdfs_read_attrs";

	CDFS_DEBUG_FCN_ENTRY(myname);

	if((length = isonum_711(file->node.ext_attr_length)) == 0)
		status = CDFS_ENOENT;
	else {
		if(length > 1)
			cdfs_error(0, myname,
			           "multi-block extents not supported");
/*
 * Get the next block from the directory.
 */
		lbn = cdfs_bmap(file, -(length * file->cdfs->lbsize));

		if((bp = cdfs_getblk(file->cdfs, lbn, 1, &data)) == NULL) {
			status = CDFS_EIO;
		} else {
			bcopy(data, attrs, sizeof(*attrs));
		}
		cdfs_relblk(file->cdfs, bp);
	}

	CDFS_DEBUG_FCN_EXIT(myname, status);

	return(status);
}

/*
 * cdfs_fncvt
 *	Convert an ISO 9660 style name to something compatible with
 *	the target operating system. '\0' terminate the result.
 */
void	cdfs_fncvt(char *isoname, char *newname, int isoname_len)
{
	int	len;
	char	*src, *dst;

	src = isoname;
	dst = newname;
	for(len = 0; len < isoname_len; len++, src++, dst++) {
		if((*src == '\0') || (*src == ';'))
			break;
		else if(isupper(*src))
			*dst = tolower(*src);
		else
			*dst = *src;
	}

	*dst = '\0';
	return;
}

/*
 * cdfs_fncmp
 *	Compare an ISO 9660 style filename with a '\0' terminated string.
 *	Alphabetic characters in the ISO 9660 filename are coverted
 *	to lower case before they are compared and a ';' in the ISO
 *	name is treated like a '\0'.
 */ 
int	cdfs_fncmp(char *isoname, char *string, int isoname_len)
{
	int	len;
	char	*s1, *s2;
	char	unsigned c1;

	s1 = isoname;
	s2 = string;
	for(len = 0; len < isoname_len; len++, s1++, s2++) {
		if(*s1 == ';')
			c1 = '\0';
		else if(isupper(*s1))
			c1 = tolower(*s1);
		else
			c1 = *s1;

		if(c1 != (unsigned char)*s2)
			return(c1 - (unsigned char)*s2);

		if(*s2 == '\0')
			break;
	}

	if((len == isoname_len) && (*s2 != '\0'))
		return(-1);
	else
		return(0);
}


