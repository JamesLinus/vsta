/*
 * scsicmds.c - server SCSI command utilities.
 */
#include <stdio.h>
#include <std.h>
#include "cam.h"

extern	struct cam_params cam_params;

/*
 * cam_inquire()
 *	Send an INQUIRY to the input device.
 */
long	cam_inquire(devid, inq_data, cam_status, scsi_status)
CAM_DEV	devid;
struct	scsi_inq_data *inq_data;
char	unsigned *cam_status, *scsi_status;
{
	CCB	*ccb;
	long	status = CAM_SUCCESS;

	if((ccb = xpt_ccb_alloc()) == NULL)
		status = CAM_ENOMEM;
	else {
		cam_fmt_inquiry_ccb(devid, inq_data, ccb);
		if((status = xpt_action(ccb)) == CAM_SUCCESS) {
			cam_ccb_wait(ccb);
			*cam_status = ccb->header.cam_status;
			*scsi_status = ccb->scsiio.scsi_status;
		}
		xpt_ccb_free(ccb);
	}

	return(status);
}

/*
 * cam_read_capacity()
 *	Send a READ CAPACITY command to the input device.
 */
long	cam_read_capacity(devid, rdcap_data, cam_status, scsi_status)
CAM_DEV	devid;
struct	scsi_rdcap_data *rdcap_data;
char	unsigned *cam_status, *scsi_status;
{
	CCB	*ccb;
	long	status = CAM_SUCCESS;

	if((ccb = xpt_ccb_alloc()) == NULL)
		status = CAM_ENOMEM;
	else {
		cam_fmt_rdcap_ccb(devid, rdcap_data, ccb);
		if((status = xpt_action(ccb)) == CAM_SUCCESS) {
			cam_ccb_wait(ccb);
			*cam_status = ccb->header.cam_status;
			*scsi_status = ccb->scsiio.scsi_status;
		}
		xpt_ccb_free(ccb);
	}

	return(status);
}

/*
 * cam_tur()
 *	Send Test Unit Ready command to the input device.
 */
long	cam_tur(devid, cam_status, scsi_status)
CAM_DEV	devid;
char	unsigned *cam_status, *scsi_status;
{
	CCB	*ccb;
	long	status = CAM_SUCCESS;

	if((ccb = xpt_ccb_alloc()) == NULL)
		status = CAM_ENOMEM;
	else {
		cam_fmt_tur_ccb(devid, ccb);
		if((status = xpt_action(ccb)) == CAM_SUCCESS) {
			cam_ccb_wait(ccb);
			*cam_status = ccb->header.cam_status;
			*scsi_status = ccb->scsiio.scsi_status;
		}
		xpt_ccb_free(ccb);
	}

	return(status);
}

/*
 * cam_prevent()
 *	Send PREVENT/ALLOW command to the input device.
 */
long	cam_prevent(devid, prevent, cam_status, scsi_status)
CAM_DEV	devid;
int	prevent;
char	unsigned *cam_status, *scsi_status;
{
	CCB	*ccb;
	long	status = CAM_SUCCESS;

	if((ccb = xpt_ccb_alloc()) == NULL)
		status = CAM_ENOMEM;
	else {
		cam_fmt_prevent_ccb(devid, prevent, ccb);
		if((status = xpt_action(ccb)) == CAM_SUCCESS) {
			cam_ccb_wait(ccb);
			*cam_status = ccb->header.cam_status;
			*scsi_status = ccb->scsiio.scsi_status;
		}
		xpt_ccb_free(ccb);
	}

	return(status);
}

/*
 * cam_rewind()
 *	Send a rewind command to the input device.
 */
long	cam_rewind(devid, cam_status, scsi_status)
CAM_DEV	devid;
char	unsigned *cam_status, *scsi_status;
{
	CCB	*ccb;
	long	status = CAM_SUCCESS;

	if((ccb = xpt_ccb_alloc()) == NULL)
		status = CAM_ENOMEM;
	else {
		cam_fmt_rewind_ccb(devid, ccb);
		if((status = xpt_action(ccb)) == CAM_SUCCESS) {
			cam_ccb_wait(ccb);
			*cam_status = ccb->header.cam_status;
			*scsi_status = ccb->scsiio.scsi_status;
		}
		xpt_ccb_free(ccb);
	}

	return(status);
}

/*
 * cam_wfm()
 *	Send a Write Filemark command to the input device.
 */
long	cam_wfm(devid, nfm, cam_status, scsi_status)
CAM_DEV	devid;
int	nfm;
char	unsigned *cam_status, *scsi_status;
{
	CCB	*ccb;
	long	status = CAM_SUCCESS;

	if((ccb = xpt_ccb_alloc()) == NULL)
		status = CAM_ENOMEM;
	else {
		cam_fmt_wfm_ccb(devid, nfm, ccb);
		if((status = xpt_action(ccb)) == CAM_SUCCESS) {
			cam_ccb_wait(ccb);
			*cam_status = ccb->header.cam_status;
			*scsi_status = ccb->scsiio.scsi_status;
		}
		xpt_ccb_free(ccb);
	}

	return(status);
}

/*
 * cam_cntuio()
 *	Start the next transfer for a large request.
 */
long	cam_cntuio(ccb)
register CCB *ccb;
{
	struct	cam_sg_elem *sg_list;
	union	cam_pdevice *pdev;
	long	count, blklen;
	uint32	xfer_len;
	uint16	next;
/*
 * Multiple transfers are only for direct read and write operations.
 */
	switch(ccb->scsiio.cdb[0]) {
	case SCSI_READ6:
	case SCSI_READ10:
	case SCSI_WRITE6:
	case SCSI_WRITE10:
		break;
	default:
		return(CAM_ENXIO);
	}
/*
 * Get the peripheral device pointer and the block size.
 */
	if((pdev = (union cam_pdevice *)ccb->scsiio.pdrv_ptr) != NULL) {
		switch(pdev->header.class) {
		case CAMPC_DISK:
			blklen = pdev->disk.blklen;
			break;
		default:
			blklen = pdev->generic.blklen;
		}
	} else {
		blklen = CAM_BLKSIZ;
	}
/*
 * Update counters.
 */
	count = ccb->scsiio.xfer_len - ccb->scsiio.resid;
	ccb->scsiio.bresid -= count;
	ccb->scsiio.bcount += count;
	ccb->scsiio.lbaddr += (count / blklen);
	if(ccb->header.cam_flags & CAM_SG_VALID)
		(char *)ccb->scsiio.sg_list->sg_address += count;
	else
		(char *)ccb->scsiio.sg_list += count;
/*
 * If the count wasn't short, check for more data to transfer.
 * Don't do multiple transfers for sequential devices.
 */
	if((count == ccb->scsiio.xfer_len) &&
	   (pdev->header.type != SCSI_SEQUENTIAL)) {
/*
 * Still more scatter/gather elements to transfer?
 */
	    if((ccb->scsiio.bresid == 0) &&
	       (ccb->header.cam_flags & CAM_SG_VALID)) {
		next = ++(ccb->scsiio.sg_next);
		if(next < ccb->scsiio.sg_max) {
			sg_list = ccb->scsiio.sg_list;
			*sg_list = ccb->scsiio.sg_base[next];
			ccb->scsiio.bresid = sg_list->sg_length;
		}
	    }
/*
 * Still more data to transfer for the current buffer?
 */
	    if(ccb->scsiio.bresid > 0) {
/*
 * Start the next I/O in the chain.
 */
		xfer_len = ccb->scsiio.bresid;
		if(xfer_len > cam_params.maxio)
			xfer_len = cam_params.maxio;
		ccb->scsiio.xfer_len = xfer_len;
		if(ccb->header.cam_flags & CAM_SG_VALID) {
#ifdef	__old__
			(char *)ccb->scsiio.sg_list->sg_address += count;
#endif
			ccb->scsiio.sg_list->sg_length = xfer_len;
		} else {
#ifdef	__old__
			(char *)ccb->scsiio.sg_list += count;
#endif
			ccb->scsiio.sg_count = xfer_len;
		}
		cam_fmt_drw_cdb6(ccb->scsiio.cdb[0], ccb->header.lun,
		             ccb->scsiio.lbaddr, xfer_len / blklen,
		             0, (struct scsi_drw_cdb6 *)ccb->scsiio.cdb);
		return(CAM_SUCCESS);
	    }
	}
/*
 * The count was "short". Fix up the 'sg_list' and 'sg_count' CCB
 * fields and return a status indicating that the I/O is finished.
 */
	ccb->scsiio.sg_list = ccb->scsiio.sg_base;
	ccb->scsiio.sg_count = ccb->scsiio.sg_max;

	return(CAM_ENMFILE);
}

/*
 * cam_start_rwio()
 *	Start a SCSI read or write operation.
 *
 *	This function will allocate and format a read/write I/O CCB,
 *	then pass off to the SIM layer to start the I/O. If
 *	neccessary, the request is broken up into several smaller
 *	requests.
 */
long	cam_start_rwio(request, cam_flags, sg_list, sg_count, pccb)
struct	cam_request *request;
uint32	cam_flags;
void	*sg_list;
long	unsigned sg_count;
CCB	**pccb;
{
	register CCB *ccb;
	union	cam_pdevice *pdev;
	long	lbaddr, nblocks, blklen, status = CAM_SUCCESS;
	char	*myname = "cam_start_rwio", scsi_opcode;

	CAM_DEBUG_FCN_ENTRY(myname);

	switch(cam_flags & (CAM_DIR_IN | CAM_DIR_OUT)) {
	case CAM_DIR_IN:
		scsi_opcode = SCSI_READ6;
		break;
	case CAM_DIR_OUT:
		scsi_opcode = SCSI_WRITE6;
		break;
	default:
		return(CAM_EINVAL);
	}

	if((ccb = xpt_ccb_alloc()) == NULL)
		status = CAM_ENOMEM;
	else {
		cam_fmt_ccb_header(XPT_SCSI_IO, request->devid, cam_flags, ccb);
/*
 * Get the peripheral device pointer and the block size.
 */
		if((pdev = request->pdev) != NULL) {
			switch(pdev->header.class) {
			case CAMPC_DISK:
				blklen = pdev->disk.blklen;
				break;
			default:
				blklen = pdev->generic.blklen;
			}
		} else {
			blklen = CAM_BLKSIZ;
		}
/*
 * Initialize the multiple-transfer-per-request fields.
 */
		ccb->scsiio.sg_base = sg_list;
		if(cam_flags & CAM_SG_VALID) {
			ccb->scsiio.sg_max = sg_count;
			ccb->scsiio.sg_next = 0;
			ccb->scsiio.sg_element = ((CAM_SG_ELEM *)sg_list)[0];
			ccb->scsiio.bresid = ccb->scsiio.sg_element.sg_length;
		} else {
			ccb->scsiio.bresid = sg_count;
			ccb->scsiio.sg_max = ccb->scsiio.sg_next = 0;
		}
/*
 * Get the transfer length.
 */
		ccb->scsiio.xfer_len = ccb->scsiio.bresid;
		if(ccb->scsiio.xfer_len > cam_params.maxio)
			ccb->scsiio.xfer_len = cam_params.maxio;
/*
 * Initialize the 'sg_list' and 'sg_count' fields.
 * Note that only one scatter/gather element is transferred at a time.
 */
		if(cam_flags & CAM_SG_VALID) {
			ccb->scsiio.sg_list = &ccb->scsiio.sg_element;
			ccb->scsiio.sg_list->sg_length = ccb->scsiio.xfer_len;
			ccb->scsiio.sg_count = 1;
		} else {
			ccb->scsiio.sg_list = sg_list;
			ccb->scsiio.sg_count = ccb->scsiio.xfer_len;
		}
/*
 * Format the CDB and remaining CCB fields.
 */
		lbaddr = request->offset / blklen;
		nblocks = ccb->scsiio.xfer_len / blklen;
		if(pdev->header.type == SCSI_SEQUENTIAL) {
			cam_fmt_srw_cdb6(scsi_opcode, CAM_LUN(request->devid),
			              0, 0, ccb->scsiio.xfer_len, 0,
			              (struct scsi_srw_cdb6 *)ccb->scsiio.cdb);
			ccb->scsiio.cdb_len = 6;
		} else if((pdev->header.type == SCSI_OPTICAL) ||
		          (lbaddr > SCSI_MAX_CDB6_LBADDR) ||
		          (nblocks > SCSI_MAX_CDB6_BLOCKS)) {
			if(scsi_opcode == SCSI_READ6)
				scsi_opcode = SCSI_READ10;
			else
				scsi_opcode = SCSI_WRITE10;
			cam_fmt_drw_cdb10(scsi_opcode, CAM_LUN(request->devid),
			           lbaddr, nblocks, 0,
			           (struct scsi_drw_cdb10 *)ccb->scsiio.cdb);
			ccb->scsiio.cdb_len = 10;
		} else {
			cam_fmt_drw_cdb6(scsi_opcode, CAM_LUN(request->devid),
			           lbaddr, nblocks, 0,
			           (struct scsi_drw_cdb6 *)ccb->scsiio.cdb);
			ccb->scsiio.cdb_len = 6;
		}
		ccb->scsiio.reqmap = (void *)request;
		if(request->file != NULL)
			ccb->scsiio.completion = request->file->completion;
		else
			ccb->scsiio.completion = NULL;
		ccb->scsiio.bcount = 0;
		ccb->scsiio.lbaddr = lbaddr;
		ccb->scsiio.pdrv_ptr = (void *)pdev;
/*
 * Start the I/O.
 */
		if((status = xpt_action(ccb)) != CAM_SUCCESS)
			xpt_ccb_free(ccb);
		else {
			if(pccb != NULL)
				*pccb = ccb;
		}
	}

	CAM_DEBUG_FCN_EXIT(myname, status);
	return(status);
}
