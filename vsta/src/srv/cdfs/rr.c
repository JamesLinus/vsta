
#define	SUSP_CHK0	0xbe
#define	SUSP_CHK1	0xef

struct	susp_proto_ind {
	...
	char	unsigned check[2];		/* check bytes = 0xbeef */
	char	unsigned len_skp;		/* skip length */
};


/*
 * cdfs_check_susp
 *	Check the root directory entry to see if the SUSP protocol
 *	indicator field is present.
 */
int	cdfs_check_susp(struct iso_directory_record *root_dir)
{
	struct	susp_proto_ind *pind;
/*
 * Get a pointer to the SUSP data.
 */
	pind = (struct susp_proto_ind *)
	            (root_dir->name + isonum_711(root_dir->name_len);
        if(isonum_711(root_dir->name_len) & 1) == 0)
                pind = (struct susp_proto_ind *)((char *)pind + 1);
/*
 * Any SUSP records?
 */
	if((char *)pind <= ((char *)root_dir + isonum_711(root_dir->length)))
		return(FALSE);
/*
 * Check the signature for "SP" system use field. If present, look at
 * the "check" bytes - they must spell "beef".
 */
	...

	return((pind->ckeck[0] == SUSP_CHK0) && (pind->check[1] == SUSP_CHK1));
}

