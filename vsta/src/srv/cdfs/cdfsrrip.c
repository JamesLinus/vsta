/*
 * cdfsrrip.c - CDFS Rock Ridge support.
 *
 * cdfsrrip currently handles the following RRIP fields:
 *
 *	"PX"		POSIX file attributes
 *	"NM"		Alternate name
 *	"SL"		Symbolic link
 */
#include <stdio.h>
#include <string.h>
#include "cdfs.h"
#include "isofs_rr.h"

#define	SUSP_CHK0	0xbe
#define	SUSP_CHK1	0xef

/*
 * System use field extraction.
 */
#define	SUSP_SIGNATURE(_susp)	((_susp)->type)
#define	SUSP_LENGTH(_susp)	isonum_711((_susp)->length)
#define	SUSP_VERSION(_susp)	((_susp)->version)
#define	SUSP_DATA(_susp)	((unsigned char *)(&(_susp)[1]))

#define	SUSP_MKSIG(_lo, _hi)	(((_hi) << 8) | (_lo))
#define	SUSP_GETSIG(_susp)	SUSP_MKSIG((_susp)->type[0],		\
				           (_susp)->type[1])

#define	SUSP_NEXT(_susp)	((ISO_SUSP_HEADER *)			\
					((char *)(_susp) + SUSP_LENGTH(_susp)))


/*
 * cdfs_find_susp - search for the system use field corresponding to
 * the input signature.
 */
int	cdfs_find_susp(struct iso_directory_record *dp, int sig, void **result)
{
	ISO_SUSP_HEADER *susp;
	char	*dpend;
	int	found;
/*
 * Get a pointer to the SUSP data.
 */
	susp = (ISO_SUSP_HEADER *)(dp->name + isonum_711(dp->name_len));
        if((isonum_711(dp->name_len) & 1) == 0)
		susp = (ISO_SUSP_HEADER *)((char *)susp + 1);
/*
 * Search for a signature matching the input 'sig'.
 */
	found = FALSE;
	dpend = (char *)dp + isonum_711(dp->length);
	while((dpend - (char *)susp) > sizeof(ISO_SUSP_HEADER)) {
		if(SUSP_GETSIG(susp) == sig) {
			found = TRUE;
			*result = susp;
			break;
		}
		susp = SUSP_NEXT(susp);
	}

	return(found);
}

/*
 * cdfs_cksusp - determine if the input directory entry contains
 * the System Use Sharing Protocol Indicator.
 */
int	cdfs_cksusp(struct iso_directory_record *dp, int *skip)
{
	ISO_RRIP_OFFSET *susp;
/*
 * Get a pointer to the SUSP data.
 */
	susp = (ISO_RRIP_OFFSET *)(dp->name + isonum_711(dp->name_len));
        if((isonum_711(dp->name_len) & 1) == 0)
		susp = (ISO_RRIP_OFFSET *)((char *)susp + 1);
/*
 * Any SUSP records?
 */
	if((char *)susp >= ((char *)dp + isonum_711(dp->length)))
		return(FALSE);
/*
 * Check the signature for "SP" system use field. If present, look at
 * the "check" bytes - they must spell "beef".
 */
	if((susp->h.type[0] != 'S') || (susp->h.type[1] != 'P'))
		return(FALSE);

	if(((unsigned char)susp->check[0] == SUSP_CHK0) &&
	   ((unsigned char)susp->check[1] == SUSP_CHK1)) {
		if(skip != NULL)
			*skip = isonum_711(susp->skip);
		return(TRUE);
	} else
		return(FALSE);
}

/*
 * cdfs_get_altname - get a pointer to the alternate name and name
 * length.
 */
void	cdfs_get_altname(struct iso_directory_record *dp,
	                 char **name, int *name_len)
{
	ISO_RRIP_ALTNAME *susp;

	*name = NULL;
	if(cdfs_find_susp(dp, SUSP_MKSIG('N', 'M'), (void *)&susp)) {
		*name = (char *)(susp + 1);
		*name_len = isonum_711(susp->h.length) - sizeof(*susp);
	}
}

/*
 * cdfs_get_posix_attrs - get POSIX file attributes.
 */
long	cdfs_get_posix_attrs(struct iso_directory_record *dp,
	                     struct cdfs_posix_attrs *attrs)
{
	ISO_RRIP_ATTR *susp;

	if(cdfs_find_susp(dp, SUSP_MKSIG('P', 'X'), (void *)&susp)) {
		attrs->mode = isonum_733(susp->mode_l);
		attrs->nlink = isonum_733(susp->links_l);
		attrs->uid = isonum_733(susp->uid_l);
		attrs->gid = isonum_733(susp->gid_l);
		return(CDFS_SUCCESS);
	}

	return(CDFS_ENOENT);
}

/*
 * cdfs_get_symlink_name - get the symbolic link name.
 */
long	cdfs_get_symlink_name(struct iso_directory_record *dp,
	                      char *symname, int *name_len)
{
	ISO_RRIP_SLINK *susp;
	ISO_RRIP_SLINK_COMPONENT *component;
	char	*prefix;
	int	actlen;

	if(!cdfs_find_susp(dp, SUSP_MKSIG('S', 'L'), (void *)&susp))
		return(CDFS_ENOENT);
	component = (ISO_RRIP_SLINK_COMPONENT *)susp->component;
/*
 * The following flags aren't supported.
 */
	if(*component->cflag & ISO_SUSP_CFLAG_CONTINUE) {
		cdfs_error(0, "cdfs_get_symlink_name",
		           "symlink continue records not supported.");
		return(CDFS_EINVAL);
	}
	if(*component->cflag & ISO_SUSP_CFLAG_VOLROOT) {
		cdfs_error(0, "cdfs_get_symlink_name",
		           "symlink VOLROOT flag not supported.");
		return(CDFS_EINVAL);
	}
	if(*component->cflag & ISO_SUSP_CFLAG_HOST) {
		cdfs_error(0, "cdfs_get_symlink_name",
		           "symlink HOST flag not supported.");
		return(CDFS_EINVAL);
	}
/*
 * Copy initial name characters dictated by the component flags.
 * These flags are mutually exclusive.
 */
	prefix = "";
	switch(*component->cflag & ISO_SUSP_CFLAG_CONTINUE) {
	case ISO_SUSP_CFLAG_CURRENT:
		prefix = "./";
		break;
	case ISO_SUSP_CFLAG_PARENT:
		prefix = "../";
		break;
	case ISO_SUSP_CFLAG_ROOT:
		prefix = "/";
		break;
	}
	actlen = strlen(prefix);
	if(symname != NULL)
		strcat(symname, prefix);
/*
 * Copy the component name.
 */
	actlen += isonum_711(component->clen);
	if(symname != NULL) {
		strncat(symname, (char *)component->name,
		        isonum_711(component->clen));
		symname[actlen] = '\0';
	}

	*name_len = actlen;

	return(CDFS_SUCCESS);
}

