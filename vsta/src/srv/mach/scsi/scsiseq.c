/*
 * scsiseq.c - functions for formatting SCSI SEQUENTIAL CCB's and CDB's.
 */
#include <stdio.h>
#include <std.h>
#include "cam.h"

/*
 * cam_fmt_srw_cdb6()
 *	Format a SCSI_SEQUENTIAL read/write 6-byte CDB.
 */
void	cam_fmt_srw_cdb6(opcode, lun, sili, fixed, length, control, cdb)
long	opcode, lun, length, control;
int	sili, fixed;
struct	scsi_srw_cdb6 *cdb;
{
	cam_fmt_cdb6(opcode, lun, 0, control, (struct scsi_cdb6 *)cdb);
	cdb->sili = sili;
	cdb->fixed = fixed;
	cdb->length2 = (length >> 16) & 0xff;
	cdb->length1 = (length >> 8) & 0xff;
	cdb->length0 = length & 0xff;
}

/*
 * cam_fmt_wfm_cdb()
 *	Format a Write Filemark CDB.
 */
void	cam_fmt_wfm_cdb(opcode, lun, wsmk, immed, length, control, cdb)
long	opcode, lun, length, control;
int	wsmk, immed;
struct	scsi_wfm_cdb *cdb;
{
	cam_fmt_cdb6(opcode, lun, 0, control, (struct scsi_cdb6 *)cdb);
	cdb->wsmk = wsmk;
	cdb->immed = immed;
	cdb->length2 = (length >> 16) & 0xff;
	cdb->length1 = (length >> 8) & 0xff;
	cdb->length0 = length & 0xff;
}

/*
 * Cam_fmt_rewind
 *	Format a REWIND CCB.
 */
void	cam_fmt_rewind_ccb(devid, ccb)
CAM_DEV	devid;
CCB	*ccb;
{
	cam_init_scsiio_ccb(devid, 0, ccb);
	ccb->scsiio.cdb_len = 6;
	cam_fmt_cdb6(SCSI_REWIND, ccb->header.lun, 0, 0,
	             (struct scsi_cdb6 *)ccb->scsiio.cdb);
}

/*
 * Cam_fmt_wfm_ccb
 *	Format a WRITE FILE MARK CCB.
 */
void	cam_fmt_wfm_ccb(devid, nfm, ccb)
CAM_DEV	devid;
int	nfm;
CCB	*ccb;
{
	cam_init_scsiio_ccb(devid, 0, ccb);
	ccb->scsiio.cdb_len = 6;
	cam_fmt_wfm_cdb(SCSI_WFM, ccb->header.lun, 0, 0, nfm, 0,
	                (struct scsi_wfm_cdb *)ccb->scsiio.cdb);
}


