/*
 * pgen.c - generic peripheral driver. Place holder for unsupported
 * peripheral types.
 */
#include <stdio.h>
#include <stdlib.h>
#include <hash.h>
#include <sys/msg.h>
#include <sys/fs.h>
#include <sys/perm.h>
#include <sys/mman.h>
#include <sys/ports.h>
#include <mach/dpart.h>
#include "cam.h"

extern	struct hash *cam_filehash;	/* Map session->context structure */
extern	struct prot cam_prot;		/* top level protection */
extern	union cam_pdevice *cam_pdevices;/* the peripheral device table */
extern	union cam_pdevice *cam_last_pdevice;

/*
 * Function prototypes.
 */
#ifdef	__STDC__
void	pgen_read_capacity(union cam_pdevice *pdev);
static	void pgen_complete(register CCB *ccb);
static	long pgen_open(struct cam_file *file, char *name);
static	long pgen_close(struct cam_file *file);
static	long pgen_rwio(struct cam_request *request, int cam_flags,
	               void *sg_list, uint16 sg_count);
#endif

/*
 * Peripheral disk driver operations.
 */
struct	cam_pdev_ops pgen_ops = {
	pgen_open, pgen_close, pgen_rwio
};

/*
 * pgen_open()
 *	Complete open processing.
 */
static	long pgen_open(struct cam_file *file, char *name)
{
	pgen_read_capacity(file->pdev);
/*
 * Fill in the generic peripheral driver completion function.
 */
	file->completion = pgen_complete;

	return(CAM_SUCCESS);
}

/*
 * pgen_close
 *	Generic peripheral driver close function.
 */
static	long pgen_close(struct cam_file *file)
{
	char	unsigned cam_status, scsi_status;

	if(file->pdev->header.type == SCSI_SEQUENTIAL) {
/*
 * XXX Need to do this conditionally based on read/write mode.
 */
		(void)cam_wfm(file->devid, 2, &cam_status, &scsi_status);
		(void)cam_rewind(file->devid, &cam_status, &scsi_status);
	}
	return(CAM_SUCCESS);
}

/*
 * pgen_read_capacity()
 *	Get the number of blocks and block size for the disk described
 *	by 'pdev'.
 */
void	pgen_read_capacity(union cam_pdevice *pdev)
{
	long	rtn_status;
	char	unsigned cam_status, scsi_status;
	struct	scsi_rdcap_data rdcap_data;
	static	char *myname = "pgen_read_capacity";

	rtn_status = cam_read_capacity(pdev->header.devid, &rdcap_data,
	                               &cam_status, &scsi_status);
	if((rtn_status == CAM_SUCCESS) && (cam_status == CAM_REQ_CMP) &&
	   (scsi_status == SCSI_GOOD)) {
		cam_sitohi32(rdcap_data.lbaddr, &pdev->generic.nblocks);
		cam_sitohi32(rdcap_data.blklen, &pdev->generic.blklen);
	} else {
/*
 * Use default values.
 */
		pdev->generic.nblocks = 0;
		pdev->generic.blklen = CAM_BLKSIZ;
	}
}

/*
 * pgen_rwio - read/write common code.
 */
static	long pgen_rwio(struct cam_request *request, int cam_flags,
	               void *sg_list, uint16 sg_count)
{
	long	status;

	status = cam_start_rwio(request, cam_flags, sg_list, sg_count, NULL);
	return(status);
}

/*
 * pgen_complete
 *	Generic peripheral driver I/O completion function.
 */
static	void pgen_complete(register CCB *ccb)
{
	cam_complete(ccb);
	xpt_ccb_free(ccb);
}

