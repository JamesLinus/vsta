/*
 * cdfsrrip.c - CDFS Rock Ridge support.
 */
#include "cdfs.h"
#include "isofs_rr.h"

#define	SUSP_CHK0	0xbe
#define	SUSP_CHK1	0xef

/*
 * cdfs_check_susp
 *	Check the root directory entry to see if the SUSP protocol
 *	indicator field is present.
 */
int	cdfs_check_susp(struct iso_directory_record *root_dir)
{
	ISO_RRIP_OFFSET *pind;
/*
 * Get a pointer to the SUSP data.
 */
	pind = (ISO_RRIP_OFFSET *)
	            (root_dir->name + isonum_711(root_dir->name_len));
        if((isonum_711(root_dir->name_len) & 1) == 0)
		pind = (ISO_RRIP_OFFSET *)((char *)pind + 1);
/*
 * Any SUSP records?
 */
	if((char *)pind <= ((char *)root_dir + isonum_711(root_dir->length)))
		return(0);
/*
 * Check the signature for "SP" system use field. If present, look at
 * the "check" bytes - they must spell "beef".
 */
	if((pind->h.type[0] != 'S') || (pind->h.type[1] != 'P'))
		return(0);
	return(((unsigned char)pind->check[0] == SUSP_CHK0) &&
	       ((unsigned char)pind->check[1] == SUSP_CHK1));
}

