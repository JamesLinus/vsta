/*
 * pdisk.c - peripheral disk driver.
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

extern	struct prot cam_prot;		/* top level protection */

/*
 * Function prototypes.
 */
#ifdef	__STDC__
static	long pdisk_open(struct cam_file *file, char *name);
static	long pdisk_close(struct cam_file *file);
static	long pdisk_rwio(struct cam_request *request);
static	long pdisk_ioctl(struct cam_file *file, long cmdval, void *cmdargs);
static	void pdisk_complete(register CCB *ccb);
#endif

/*
 * Peripheral disk driver operations.
 */
struct	cam_pdev_ops pdisk_ops = {
	pdisk_open, pdisk_close, pdisk_rwio, pdisk_ioctl
};

/*
 * pdisk_open()
 *	Complete open processing by getting the proper device ID
 *	based on the file name to be opened.
 */
static	long pdisk_open(struct cam_file *file, char *name)
{
	struct	part **parts;
	union	cam_pdevice *pdev;
	int	i;
	CAM_DEV	devid;
	long	status;
	char	unsigned cam_status, scsi_status;
	static	char *myname = "pdisk_open";

	pdev = file->pdev;
	parts = (struct part **)pdev->disk.partitions;
/*
 * Does the media have a partition table?
 */
	if((pdev->header.type == SCSI_CDROM) ||
	   (pdev->header.type == SCSI_WORM)) {
		if(parts[0] == NULL)
			return(CAM_ENOENT);
/*
 * Get the media capacity.
 * Build a partition entry for the CDROM media.
 */
		pdisk_read_capacity(pdev);
		parts[0]->p_len = pdev->disk.nblocks;
		parts[0]->p_val = 1;
	} else {
/*
 * Scan the partition table names for a match.
 */
		for(i = 0; i < MAX_PARTS; i++) {
			if(parts[i] == NULL)
				continue;
			if(!parts[i]->p_val)
				continue;
			if(strcmp(parts[i]->p_name, name) == 0) {
				devid = file->devid;
				file->devid = CAM_MKDEVID(CAM_BUS(devid),
				                          CAM_TARGET(devid),
				                          CAM_LUN(devid), i);
				break;
			}
		}
		if(i >= MAX_PARTS)
			return(CAM_ENOENT);
	}
/*
 * Prevent media removal.
 */
	if(pdev->disk.removable) {
		status = cam_prevent(file->devid, TRUE, &cam_status,
		                     &scsi_status);
		if((status != CAM_SUCCESS) || (cam_status != CAM_REQ_CMP) ||
		   (scsi_status != SCSI_GOOD)) 
			cam_error(0, myname, "can't prevent media removal");
	}
/*
 * Fill in the peripheral disk driver completion function.
 */
	file->completion = pdisk_complete;

	return(CAM_SUCCESS);
}

/*
 * pdisk_close
 *	Peripheral disk driver close function.
 */
static	long pdisk_close(struct cam_file *file)
{
	long	status;
	char	unsigned cam_status, scsi_status;
	static	char *myname = "pdisk_close";
/*
 * Prevent media removal.
 */
	if(file->pdev->disk.removable) {
		status = cam_prevent(file->devid, FALSE, &cam_status,
		                     &scsi_status);
		if((status != CAM_SUCCESS) || (cam_status != CAM_REQ_CMP) ||
		   (scsi_status != SCSI_GOOD)) 
			cam_error(0, myname, "can't allow media removal");
	}

	return(CAM_SUCCESS);
}

/*
 * pdisk_read_capacity()
 *	Get the number of blocks and block size for the disk described
 *	by 'pdev'.
 */
void	pdisk_read_capacity(union cam_pdevice *pdev)
{
	int	retry = 0;
	long	rtn_status;
	char	unsigned cam_status, scsi_status;
	struct	scsi_rdcap_data rdcap_data;
	static	char *myname = "pdisk_read_capacity";

again:
	rtn_status = cam_read_capacity(pdev->header.devid, &rdcap_data,
	                               &cam_status, &scsi_status);
	if((rtn_status == CAM_SUCCESS) && (cam_status == CAM_REQ_CMP) &&
	   (scsi_status == SCSI_GOOD)) {
		cam_si32tohi32(rdcap_data.lbaddr, &pdev->disk.nblocks);
		cam_si32tohi32(rdcap_data.blklen, &pdev->header.blklen);
	} else {
		if(retry++ < 1)
			goto again;

		if(!pdev->disk.removable)
		    cam_error(0, myname, "can't read capacity, using defaults");
/*
 * Use default values.
 */
		pdev->disk.nblocks = 0;
		pdev->header.blklen = CAM_BLKSIZ;
	}
}

/*
 * pdisk_read_part_table()
 *	Read in the partition table for the input device.
 */
void	pdisk_read_part_table(union cam_pdevice *pdev)
{
	CCB	*ccb;
	struct	part **parts;
	char	*buffer;
	uint	start;
	long	status;
	int	retry = 0, next_part;
	struct	cam_request request;
	static	char *myname = "pdisk_read_part_table";
/*
 * Allocate the partition table.
 */
	parts = cam_alloc_mem(sizeof(*parts) * MAX_PARTS, NULL, 0);
	if(parts == NULL) {
		cam_error(0, myname, "can't allocate partition table");
		return;
	}
	bzero(parts, sizeof(*parts) * MAX_PARTS);
	pdev->disk.partitions = (void *)parts;
/*
 * Removable media - since media isn't necessarily present, don't read
 * in any partition entries yet.
 */
	if(pdev->disk.removable) {
		parts[0] = (struct part *)cam_alloc_mem(sizeof(struct part),
		                                        NULL, 0);
		if(parts[0] == NULL) {
			cam_error(0, myname, "can't allocate partition entry");
		} else {
			bzero((char *)parts[0], sizeof(struct part));
			sprintf(parts[0]->p_name, "%s0", pdev->header.name);
		}
		return;
	}
/*
 * Allocate an I/O buffer for the partition table.
 */
	if((buffer = cam_alloc_mem(pdev->header.blklen, NULL, 0)) == NULL) {
		cam_error(0, myname, "can't allocate I/O buffer");
		pdev->disk.partitions = NULL;
		cam_free_mem(parts, 0);
		return;
	}
/*
 * Initialize the "whole" disk partition first.
 */
	dpart_init_whole(pdev->header.name, CAM_TARGET(pdev->header.devid),
	                 pdev->header.blklen, &cam_prot, parts);
/*
 * Now go and initialize the physical partition structures.
 */
	start = 0;
	next_part = FIRST_PART;
	do {
again:
/*
 * Read in the current sector.
 */
		request.devid = pdev->header.devid;
		request.file = NULL;
		request.pdev = pdev;
		request.offset = start * pdev->header.blklen;
		request.status = CAM_SUCCESS;
		request.cam_flags = CAM_DIR_IN;
		request.sg_list = (void *)buffer;
		request.sg_count = pdev->header.blklen;
		if((status = cam_start_rwio(&request, &ccb)) != CAM_SUCCESS) {
			cam_error(0, myname, "sector read error");
			break;
		}
/*
 * Wait for the read to complete.
 */
		cam_ccb_wait(ccb);
		if(ccb->header.cam_status != CAM_REQ_CMP) {
			cam_error(0, myname,
			          "sector read returned cam error %d",
			           ccb->header.cam_status);
			break;
		}

		if(ccb->scsiio.scsi_status != SCSI_GOOD) {
/*
 * XXX this hack should go away after autosense is working.
 */
			if((ccb->scsiio.scsi_status == SCSI_CHECK_COND) &&
			   (retry < 1)) {
				retry++;
				goto again;
			}
			cam_error(0, myname,
			          "sector read returned scsi error %d",
			           ccb->scsiio.scsi_status);
			break;
		}
/*
 * TODO: make bus and possibly lun part of "unit".
 */
		dpart_init(pdev->header.name, CAM_TARGET(request.devid),
		           buffer, &start, &cam_prot, parts, &next_part);

		xpt_ccb_free(ccb);
	} while(start != 0);

	cam_free_mem(buffer, 0);
}

/*
 * pdisk_rwio - read/write common code.
 */
static	long pdisk_rwio(struct cam_request *request)
{
	struct	cam_file *file = request->file;
	void	*sg_list = request->sg_list;
	uint16	sg_count = request->sg_count;
	ulong	blkoff, max;
	uint32	blklen, sg_length;
	uint	blkcnt;
	long	status;
	int	i;
	static	char *myname = "pdisk_rwio";

	if(file->pdev->disk.nblocks == 0)
		return(CAM_ENXIO);
/*
 * Convert the file position into a block offset.
 */
	blklen = file->pdev->header.blklen;
	if(dpart_get_offset((struct part **)file->pdev->disk.partitions,
	                    CAM_PARTITION(file->devid),
	                    file->position / blklen,
	                    &blkoff, &blkcnt) != 0) {
cam_info(0, "pdisk_rwio: past EOP, position = %d", file->position);
		return(CAM_ENOSPC);
	}
	max = blkcnt * blklen;
/*
 * Check alignment of request. Trim the transfer count, if necessary.
 */
	if(file->position & (blklen - 1))
		return(CAM_EBALIGN);
	if(!(request->cam_flags & CAM_SG_VALID)) {
		if(sg_count & (blklen - 1))
			return(CAM_EBALIGN);
		if(sg_count > max) {
cam_info(0, "pdisk_rwio: trimming !SG cnt from %d to %d", sg_count, max);
			sg_count = max;
		}
	} else {
		for(i = 0; i < sg_count; i++) {
			sg_length = ((CAM_SG_ELEM *)sg_list)[i].sg_length;
			if(sg_length & (blklen - 1))
				return(CAM_EBALIGN);
			if(sg_length > max) {
cam_info(0, "pdisk_rwio: trimming SG len from %d to %d", sg_length, max);
				((CAM_SG_ELEM *)sg_list)[i].sg_length = max;
				max = 0;
			} else
				max -= sg_length;
		}
	}

	request->offset = blkoff * file->pdev->header.blklen;
	status = cam_start_rwio(request, NULL);

	return(status);
}

/*
 * pdisk_complete
 *	Peripheral disk driver I/O completion function.
 */
static	void pdisk_complete(register CCB *ccb)
{
	cam_complete(ccb);
	xpt_ccb_free(ccb);
}

/*
 * pdisk_ioctl - ioctl processing.
 */
static	long pdisk_ioctl(struct cam_file *file, long cmdval, void *cmdargs)
{
	switch(cmdval) {
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

