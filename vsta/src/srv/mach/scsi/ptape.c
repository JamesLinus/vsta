/*
 * ptape.c - peripheral tape driver.
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
#include "mtio.h"

/*
 * State flags.
 */
#define	PTAPE_WPROT		1		/* write protected */
#define	PTAPE_FM_PENDING	2		/* FM seen but not processed */
#define	PTAPE_BOM		4		/* Beginning Of Medium */

#define	PTAPE_MOVE_FROM_BOM(_flags)	((_flags) &= ~PTAPE_BOM)
#define	PTAPE_MOVE_TO_BOM(_flags)	((_flags) |= PTAPE_BOM)

/*
 * Parameters, modes, etc.
 */
extern	struct cam_params cam_params;
struct	cam_ptape_mode ptape_default_mode = {
		TRUE,				/* mdset */
		1,				/* neotfm */
		0,				/* density */
		0,				/* blklen */
		0,				/* device specific */
};

/*
 * Function prototypes.
 */
#ifdef	__STDC__
static	void ptape_complete(register CCB *ccb);
static	long ptape_open(struct cam_file *file, char *name);
static	long ptape_close(struct cam_file *file);
static	long ptape_rwio(struct cam_request *request);
static	long ptape_ioctl(struct cam_file *file, long cmdval, void *cmdargs);
static	long ptape_mtioctop(struct cam_file *file, struct mtop *mtop);
static	long ptape_get_blkprms(struct cam_file *file);
static	long ptape_set_blkprms(struct cam_file *file, int devspc, int density,
	                       uint32 nblocks, uint32 blklen);
#endif

/*
 * Peripheral disk driver operations.
 */
struct	cam_pdev_ops ptape_ops = {
	ptape_open, ptape_close, ptape_rwio, ptape_ioctl
};

/*
 * ptape_open()
 *	Complete open processing.
 */
static	long ptape_open(struct cam_file *file, char *name)
{
	union	cam_pdevice *pdev = file->pdev;
	char	unsigned cam_status, scsi_status;
	long	rtn_status;
	int	mdix, count;

#ifdef	TAPE_DEBUG
cam_info(0, "ptape_open: file = 0x%x, mode = 0x%x", file, file->mode);
#endif
/*
 * Is the drive ready?
 */
	count = 0;
	do {
		rtn_status = cam_tur(file->devid, &cam_status, &scsi_status);
		if((rtn_status == CAM_SUCCESS) && (cam_status == CAM_REQ_CMP) &&
		   (scsi_status == SCSI_GOOD))
			break;
		count++;
		cam_sleep(1);
	} while(count < cam_params.max_tape_ready_time);

	if(count >= cam_params.max_tape_ready_time)
		return(CAM_EBUSY);

	mdix = CAM_FLAGS(file->devid);
	if(!pdev->tape.modes[mdix].mdset) {
		pdev->tape.modes[mdix] = ptape_default_mode;
/*
 * Determine if the driver supports variable length or fixed length
 * blocks. For fixed length blocks, get the block length.
 */
		if((rtn_status = ptape_get_blkprms(file)) != CAM_SUCCESS)
			return(rtn_status);
	}
/*
 * Set the block length in the device header.
 */
	pdev->header.blklen = pdev->tape.modes[CAM_FLAGS(file->devid)].blklen;
/*
 * Fill in the generic peripheral driver completion function.
 */
	file->completion = ptape_complete;

	return(CAM_SUCCESS);
}

/*
 * ptape_close
 *	Tape peripheral driver close function.
 */
static	long ptape_close(struct cam_file *file)
{
	char	unsigned cam_status, scsi_status;
	int	mdix;
	long	rtn_status;
	struct	scsi_reqsns_data snsdata;
	static	char *myname = "ptape_close";

#ifdef	TAPE_DEBUG
cam_info(0, "ptape_close: mode = 0x%x", file->mode);
#endif
	mdix = CAM_FLAGS(file->devid);
/*
 * If open for write, write file marks.
 */
	if((file->mode & ACC_WRITE) && (file->position > 0)) {
		rtn_status = cam_wfm(file->devid,
		                     file->pdev->tape.modes[mdix].neotfm,
		                     &cam_status, &scsi_status);
		(void)cam_check_error(myname, "WFM error", rtn_status,
		                      cam_status, scsi_status);
	}
/*
 * Rewind the tape.
 */
	if(!CAM_NOREWIND(file->devid)) {
		rtn_status = cam_rewind(file->devid, &cam_status, &scsi_status);
		if(!cam_check_error(myname, "rewind error", rtn_status,
		                    cam_status, scsi_status))
			PTAPE_MOVE_TO_BOM(file->pdev->tape.flags);
	} else if(file->pdev->tape.flags & PTAPE_FM_PENDING) {
/*
 * A FILEMARK was seen but not processed. Backspace the tape so state
 * doesn't have to be carried across open's.
 */
		file->pdev->tape.flags &= ~PTAPE_FM_PENDING;
		rtn_status = cam_space(file->devid, SCSI_SPC_FILEMARKS, -1,
		                       &cam_status, &scsi_status, &snsdata);
		if((scsi_status != SCSI_CHECK_COND) ||
		   (snsdata.snskey != 0) || !snsdata.filemark)
			(void)cam_check_error(myname, "SPACE error", rtn_status,
			                      cam_status, scsi_status);
	}

	return(CAM_SUCCESS);
}

/*
 * ptape_rwio - read/write common code.
 */
static	long ptape_rwio(struct cam_request *request)
{
	union	cam_pdevice *pdev = request->file->pdev;
	void	*sg_list = request->sg_list;
	uint16	sg_count = request->sg_count;
	long	status, blklen;
/*
 * Must do I/O in a single read/write operation.
 */
	if((sg_count > 1) ||
	   (((CAM_SG_ELEM *)sg_list)->sg_length > cam_params.maxio))
		return(CAM_E2BIG);
/*
 * For drives that support fixed length blocks, the size of each
 * scatter/gather buffer must be a multiple of the block length.
 */
	if((blklen = pdev->header.blklen) != 0)
		if((((CAM_SG_ELEM *)sg_list)->sg_length % blklen) != 0)
			return(CAM_EBALIGN);
/*
 * Check for writes to write-protected tapes.
 * Start the I/O.
 */
	if(pdev->tape.flags & PTAPE_WPROT)
		if(request->cam_flags & CAM_DIR_OUT)
			return(CAM_EROFS);
/*
 * If a FILEMARK was read on the last operation, EOD has been reached.
 */
	if(pdev->tape.flags & PTAPE_FM_PENDING) {
		pdev->tape.flags &= ~PTAPE_FM_PENDING;
		if(request->cam_flags & CAM_DIR_IN) {
			cam_iodone(request);
			return(CAM_SUCCESS);
		}
	}

	status = cam_start_rwio(request, NULL);

	return(status);
}

/*
 * ptape_complete
 *	Tape peripheral driver I/O completion function.
 */
static	void ptape_complete(register CCB *ccb)
{
	struct	cam_request *request;
	union	cam_pdevice *pdev;
	uint32	resid;
	static	char *myname =  "ptape_complete";

	request = (struct cam_request *)ccb->scsiio.reqmap;
	pdev = request->file->pdev;
/*
 * For successful I/O, reset the BOM flag.
 */
	if((ccb->header.fcn_code == XPT_SCSI_IO) &&
	   (CAM_CCB_STATUS(ccb) == CAM_REQ_CMP)) {
		PTAPE_MOVE_FROM_BOM(pdev->tape.flags);
	}

	if(ccb->header.cam_status & CAM_AUTOSNS_VALID) {
#ifdef	__comment__
		(void)cam_check_error("ptape_complete", "I/O error",
		                      CAM_SUCCESS, ccb->header.cam_status,
		                      ccb->scsiio.scsi_status);
cam_print_sense(cam_info, 0, &ccb->scsiio.snsdata);
#endif
		if(ccb->scsiio.snsdata.snskey == 0) {
			if(ccb->scsiio.snsdata.filemark) {
				cam_si32tohi32(ccb->scsiio.snsdata.info,
				               &resid);
				if(pdev->header.blklen != 0)
					resid *= pdev->header.blklen;
/*
 * Not a fatal error, so make the SCSI status good.
 * If data was transfered and this was a read, save file mark processing
 * for later.
 */
				ccb->scsiio.scsi_status = SCSI_GOOD;
				if((request->bresid < request->bcount) &&
				   (ccb->header.cam_flags & CAM_DIR_IN))
					pdev->tape.flags |= PTAPE_FM_PENDING;
			}
		}
#ifdef	__comment__
{
	struct	cam_request *request;
	request = (struct cam_request *)ccb->scsiio.reqmap;
	cam_info(0, "bcount = %d, bresid = %d, scsiio.resid = %d",
	         request->bcount, request->bresid, ccb->scsiio.resid);
}
#endif
	}

	cam_complete(ccb);
	xpt_ccb_free(ccb);
}

/*
 * ptape_ioctl - ioctl processing.
 */
static	long ptape_ioctl(struct cam_file *file, long cmdval, void *cmdargs)
{
	switch(cmdval) {
	case MTIOCTOP:
		return(ptape_mtioctop(file, (struct mtop *)cmdargs));
	case CAM_EXEC_IOCCB:
	 	if(xpt_action((CCB *)cmdargs) != CAM_SUCCESS)
			return(CAM_EIO);
		cam_ccb_wait((CCB *)cmdargs);
		break;
	default:
		return(CAM_ENXIO);
	}
	return(CAM_SUCCESS);
}

/*
 * ptape_mtioctop - magnetic tape operations.
 */
static	long ptape_mtioctop(struct cam_file *file, struct mtop *mtop)
{
	union	cam_pdevice *pdev;
	long	rtn_status = CAM_SUCCESS, spc_count;
	int	spc_code, mdix;
	char	unsigned cam_status, scsi_status;
	struct	scsi_reqsns_data snsdata;
	static	char *myname = "ptape_mtioctop";

	pdev = file->pdev;
	mdix = CAM_FLAGS(file->devid);
	switch(mtop->mt_op) {
	case MTWEOF:
		rtn_status = cam_wfm(file->devid, pdev->tape.modes[mdix].neotfm,
		                     &cam_status, &scsi_status);
		(void)cam_check_error(myname, "WFM error", rtn_status,
		                      cam_status, scsi_status);
		break;
	case MTFSF:
	case MTBSF:
	case MTFSR:
	case MTBSR:
/*
 * SPACE operations. Determine the what and how many things to space over.
 * If a File Mark is pending and the operation is Space File, decrement
 * the space count.
 */
		spc_count = mtop->mt_count;
		if((mtop->mt_op == MTBSF) || (mtop->mt_op == MTBSR))
			spc_count *= -1;
		if((mtop->mt_op == MTFSF) || (mtop->mt_op == MTBSF)) {
			spc_code = SCSI_SPC_FILEMARKS;
			if(pdev->tape.flags & PTAPE_FM_PENDING) {
				spc_count -= 1;
				pdev->tape.flags &= ~PTAPE_FM_PENDING;
			}
		} else
			spc_code = SCSI_SPC_BLOCKS;
/*
 * Do the specified SPACE operation.
 */
		rtn_status = cam_space(file->devid, spc_code, spc_count,
		                       &cam_status, &scsi_status, &snsdata);
		if((scsi_status == SCSI_CHECK_COND)  &&
		   (snsdata.snskey == 0) && (snsdata.filemark) &&
		   ((mtop->mt_op != MTFSF) && (mtop->mt_op != MTBSF)))
			(void)cam_check_error(myname, "SPACE error", rtn_status,
			                      cam_status, scsi_status);
		break;
	case MTREW:
		rtn_status = cam_rewind(file->devid, &cam_status, &scsi_status);
		if(!cam_check_error(myname, "rewind error", rtn_status,
		                      cam_status, scsi_status))
			PTAPE_MOVE_TO_BOM(pdev->tape.flags);
		break;
	case MTOFFL:
		return(CAM_ENXIO);
	case MTNOP:
		break;
	case MTCACHE:
	case MTNOCACHE:
		return(CAM_ENXIO);
	case MTSETBSIZ:
		rtn_status = ptape_set_blkprms(file,
		                               pdev->tape.modes[mdix].devspc,
		                               pdev->tape.modes[mdix].density,
		                               0, mtop->mt_count);
		if(rtn_status == CAM_SUCCESS)
			pdev->tape.modes[mdix].blklen = mtop->mt_count;
		break;
	case MTSETDNSTY:
		rtn_status = ptape_set_blkprms(file,
		                               pdev->tape.modes[mdix].devspc,
		                               mtop->mt_count, 0,
		                               pdev->tape.modes[mdix].blklen);
		if(rtn_status == CAM_SUCCESS)
			pdev->tape.modes[mdix].density = mtop->mt_count;
		break;
	case MTSETDRVBUFFER:
		rtn_status = ptape_set_blkprms(file, mtop->mt_count << 4,
		                              pdev->tape.modes[mdix].density,
		                              0, pdev->tape.modes[mdix].blklen);
		if(rtn_status == CAM_SUCCESS)
			pdev->tape.modes[mdix].devspc = mtop->mt_count << 4;
		break;
	default:
		return(CAM_ENXIO);
	}

	return(rtn_status);
}

/*
 * ptape_get_blkprms - get MODE SENSE block parameters.
 */
static	long ptape_get_blkprms(struct cam_file *file)
{
	union	cam_pdevice *pdev = file->pdev;
	long	rtn_status;
	int	mdix;
	char	unsigned cam_status, scsi_status;
	struct {
		struct	scsi_mdparam_hdr6 header;
		struct	scsi_blkdesc blkdesc[1];
	} snsbuf;
	static	char *myname = "ptape_get_blkprms";

	mdix = CAM_FLAGS(file->devid);
	rtn_status = cam_mode_sense(file->devid, 0, 0, 0, sizeof(snsbuf),
	                            (unsigned char *)&snsbuf,
	                            &cam_status, &scsi_status);
	if(cam_check_error(myname, "mode sense error", rtn_status,
	                   cam_status, scsi_status))
		return(CAM_EIO);

	cam_si24tohi32(snsbuf.blkdesc[0].blklen,
	               &pdev->tape.modes[mdix].blklen);
	if(snsbuf.header.device_spec & SCSI_TAPE_WP)
		pdev->tape.flags |= PTAPE_WPROT;
	else
		pdev->tape.flags &= ~PTAPE_WPROT;

#ifdef	TAPE_DEBUG
cam_info(0, "snsbuf len = 0x%x, type = 0x%x, dev_spec = 0x%x, bdesc_len = 0x%x",
         snsbuf.header.length, snsbuf.header.medium_type,
         snsbuf.header.device_spec, snsbuf.header.blkdesc_len);
cam_info(0, "density = %d, blklen = %d", snsbuf.blkdesc[0].density,
         pdev->tape.modes[mdix].blklen);
#endif

	return(CAM_SUCCESS);
}

/*
 * ptape_set_blkprms - set MODE SENSE block parameters.
 */
static	long ptape_set_blkprms(struct cam_file *file, int devspc, int density,
	                       uint32 nblocks, uint32 blklen)
{
	char	unsigned cam_status, scsi_status;
	long	rtn_status;
	struct {
		struct	scsi_mdparam_hdr6 header;
		struct	scsi_blkdesc blkdesc;
	} selbuf;
	static	char *myname = "ptape_set_blkprms";

	bzero((char *)&selbuf, sizeof(selbuf));
	selbuf.header.device_spec = devspc;
	selbuf.header.blkdesc_len = 8;
	selbuf.blkdesc.density = density;
	cam_hi32tosi24(nblocks, selbuf.blkdesc.nblocks);
	cam_hi32tosi24(blklen, selbuf.blkdesc.blklen);
	rtn_status = cam_mode_select(file->devid, 0, 0, sizeof(selbuf),
	                             (unsigned char *)&selbuf,
	                             &cam_status, &scsi_status);
	if(cam_check_error(myname, "mode select error", rtn_status,
	                   cam_status, scsi_status))
		return(CAM_EIO);
	return(CAM_SUCCESS);
}

