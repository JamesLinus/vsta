/*
 * scsicmn.c - functions for formatting common CCB's and CDB's
 */
#include <stdio.h>
#include <std.h>
#include "cam.h"

/*
 * cam_fmt_cdb6()
 *	Format a 6-byte CDB.
 */
void	cam_fmt_cdb6(opcode, lun, length, control, cdb)
long	opcode, lun, length, control;
struct	scsi_cdb6 *cdb;
{
	bzero((char *)cdb, 6);
	cdb->opcode = opcode;
	cdb->lun = lun;
	cdb->length = length;
	cdb->control = control;
}

/*
 * cam_fmt_cdb10()
 *	Format a 10-byte CDB.
 */
void	cam_fmt_cdb10(opcode, lun, lbaddr, length, control, cdb)
long	opcode, lun, length, control;
uint32	lbaddr;
struct	scsi_cdb10 *cdb;
{
	bzero((char *)cdb, 10);
	cdb->opcode = opcode;
	cdb->lun = lun;
	cdb->lbaddr3 = (lbaddr >> 24) & 0xff;
	cdb->lbaddr2 = (lbaddr >> 16) & 0xff;
	cdb->lbaddr1 = (lbaddr >> 8) & 0xff;
	cdb->lbaddr0 = lbaddr & 0xff;
	cdb->length1 = (length >> 8) & 0xff;
	cdb->length0 = length & 0xff;
	cdb->control = control;
}

/*
 * cam_fmt_ccb_header()
 *	Format a CAM CCB header.
 */
void	cam_fmt_ccb_header(fcn_code, devid, cam_flags, ccb)
long	fcn_code;
CAM_DEV	devid;
uint32	cam_flags;
CCB	*ccb;
{
	int	length;

	ccb->header.myaddr = (void *)ccb;
	ccb->header.fcn_code = fcn_code;
	ccb->header.cam_status = CAM_REQ_INPROG;
	ccb->header.path_id = CAM_PATH_ID(devid);
	ccb->header.target = CAM_TARGET(devid);
	ccb->header.lun = CAM_LUN(devid);
	ccb->header.cam_flags = cam_flags;

	switch(fcn_code) {
	case XPT_NOOP:
		length = sizeof(struct cam_ccb_header);
		break;
	case XPT_SCSI_IO:
		length = sizeof(struct cam_scsiio_ccb);
		break;
	case XPT_GDEV_TYPE:
		length = sizeof(struct cam_getdev_ccb);
		break;
	case XPT_PATH_INQ:
		length = sizeof(struct cam_pathinq_ccb);
		break;
	case XPT_REL_SIMQ:
		length = sizeof(struct cam_relsim_ccb);
		break;
	case XPT_SASYNC_CB:
		length = sizeof(struct cam_setasync_ccb);
		break;
	default:
		length = sizeof(struct cam_ccb_header);
		break;
	}
	ccb->header.ccb_length = length;
}

/*
 * cam_init_scsiio_ccb()
 *	Initialize a SCSIIO CCB.
 */
void	cam_init_scsiio_ccb(devid, cam_flags, ccb)
CAM_DEV	devid;
uint32	cam_flags;
CCB	*ccb;
{
	bzero((char *)ccb, sizeof(struct cam_scsiio_ccb));
	cam_fmt_ccb_header(XPT_SCSI_IO, devid, cam_flags, ccb);
}

/*
 * cam_fmt_inquiry_ccb
 *	Format an INQUIRY CCB.
 */
void	cam_fmt_inquiry_ccb(devid, inq_data, ccb)
CAM_DEV	devid;
struct	scsi_inq_data *inq_data;
CCB	*ccb;
{
	cam_init_scsiio_ccb(devid, CAM_DIR_IN, ccb);
	ccb->scsiio.sg_list = (void *)inq_data;
	ccb->scsiio.xfer_len = sizeof(*inq_data);
	ccb->scsiio.cdb_len = 6;
	ccb->scsiio.resid = sizeof(*inq_data);
	cam_fmt_cdb6(SCSI_INQUIRY, ccb->header.lun, sizeof(*inq_data), 0,
	             (struct scsi_cdb6 *)ccb->scsiio.cdb);
}

/*
 * cam_fmt_reqsns_ccb
 *	Format a REQUEST SENSE CCB.
 */
void	cam_fmt_reqsns_ccb(devid, reqsns_data, ccb)
CAM_DEV	devid;
struct	scsi_reqsns_data *reqsns_data;
CCB	*ccb;
{
	cam_init_scsiio_ccb(devid, CAM_DIR_IN, ccb);
	ccb->scsiio.sg_list = (void *)reqsns_data;
	ccb->scsiio.xfer_len = sizeof(*reqsns_data);
	ccb->scsiio.cdb_len = 6;
	ccb->scsiio.resid = sizeof(*reqsns_data);
	cam_fmt_cdb6(SCSI_REQSNS, ccb->header.lun, sizeof(*reqsns_data), 0,
	             (struct scsi_cdb6 *)ccb->scsiio.cdb);
}

/*
 * Cam_fmt_rdcap_ccb
 *	Format a READ CAPACITY CCB.
 */
void	cam_fmt_rdcap_ccb(devid, rdcap_data, ccb)
CAM_DEV	devid;
struct	scsi_rdcap_data *rdcap_data;
CCB	*ccb;
{
	cam_init_scsiio_ccb(devid, CAM_DIR_IN, ccb);
	ccb->scsiio.sg_list = (void *)rdcap_data;
	ccb->scsiio.xfer_len = sizeof(*rdcap_data);
	ccb->scsiio.cdb_len = 10;
	ccb->scsiio.sg_count = sizeof(*rdcap_data);
	ccb->scsiio.resid = sizeof(*rdcap_data);
	cam_fmt_cdb10(SCSI_READ_CAPACITY, ccb->header.lun, 0, 0, 0,
	              (struct scsi_cdb10 *)ccb->scsiio.cdb);
}

/*
 * Cam_fmt_tur_ccb
 *	Format a TEST UNIT READY CCB.
 */
void	cam_fmt_tur_ccb(devid, ccb)
CAM_DEV	devid;
CCB	*ccb;
{
	cam_init_scsiio_ccb(devid, 0, ccb);
	ccb->scsiio.cdb_len = 6;
	cam_fmt_cdb6(SCSI_TUR, ccb->header.lun, 0, 0,
	             (struct scsi_cdb6 *)ccb->scsiio.cdb);
}
