/*
 * scsicd.c - functions for formatting CD-ROM CCB's and CDB's.
 */
#include <stdio.h>
#include <std.h>
#include "cam.h"

/*
 * cam_fmt_read_toc_cdb()
 *	Format a READ TOC CDB.
 */
void	cam_fmt_read_toc_cdb(lun, start, length, control, cdb)
long	lun, start, length, control;
struct	scsi_read_toc_cdb *cdb;
{
	bzero((char *)cdb, sizeof(struct scsi_read_toc_cdb));
	cdb->opcode = SCSI_READ_TOC;
	cdb->lun = lun;
	cdb->start_track = start;
	cdb->length1 = (length >> 8) & 0xff;
	cdb->length0 = length & 0xff;
	cdb->control = control;
}

/*
 * cam_fmt_pause_resume_cdb()
 *	Format a READ PAUSE RESUME CDB.
 */
void	cam_fmt_pause_resume_cdb(lun, resume, control, cdb)
long	lun, resume, control;
struct	scsi_pause_resume_cdb *cdb;
{
	bzero((char *)cdb, sizeof(struct scsi_pause_resume_cdb));
	cdb->opcode = SCSI_PAUSE_RESUME;
	cdb->lun = lun;
	cdb->resume = resume;
	cdb->control = control;
}

/*
 * cam_fmt_play_audio12_cdb()
 *	Format a PLAY AUDIO 12-byte CDB.
 */
void	cam_fmt_play_audio12_cdb(lun, reladr, lbaddr, length, control, cdb)
long	lun, reladr, lbaddr, length, control;
struct	scsi_play_audio12_cdb *cdb;
{
	bzero((char *)cdb, sizeof(struct scsi_play_audio12_cdb));
	cdb->opcode = SCSI_PLAY_AUDIO12;
	cdb->lun = lun;
	cdb->reladr = reladr;
	cdb->lbaddr3 = (lbaddr >> 24) & 0xff;
	cdb->lbaddr2 = (lbaddr >> 16) & 0xff;
	cdb->lbaddr1 = (lbaddr >> 8) & 0xff;
	cdb->lbaddr0 = lbaddr & 0xff;
	cdb->length3 = (length >> 24) & 0xff;
	cdb->length2 = (length >> 16) & 0xff;
	cdb->length1 = (length >> 8) & 0xff;
	cdb->length0 = length & 0xff;
	cdb->control = control;
}

/*
 * cam_fmt_play_audio_tkindx_cdb()
 *	Format a PLAY AUDIO TRACK INDEX CDB.
 */
void	cam_fmt_play_audio_tkindx_cdb(lun, start_track, start_index,
	                              end_track, end_index, control, cdb)
long	lun, start_track, start_index;
long	end_track, end_index, control;
struct	scsi_play_audio_tkindx_cdb *cdb;
{
	bzero((char *)cdb, sizeof(struct scsi_play_audio_tkindx_cdb));
	cdb->opcode = SCSI_PLAY_AUDIO_TKINDX;
	cdb->lun = lun;
	cdb->start_track = start_track;
	cdb->start_index = start_index;
	cdb->end_track = end_track;
	cdb->end_index = end_index;
	cdb->control = control;
}



/*
 * cam_fmt_read_toc_ccb
 *	Format a READ TOC CCB.
 */
void	cam_fmt_read_toc_ccb(devid, read_toc_data, start, alloc_length, ccb)
CAM_DEV	devid;
struct	scsi_read_toc_data *read_toc_data;
int	start, alloc_length;
CCB	*ccb;
{
	cam_init_scsiio_ccb(devid, CAM_DIR_IN, ccb);
	ccb->scsiio.sg_list = (void *)read_toc_data;
	ccb->scsiio.xfer_len = ccb->scsiio.resid = alloc_length;
	ccb->scsiio.cdb_len = sizeof(struct scsi_read_toc_cdb);
	cam_fmt_read_toc_cdb(ccb->header.lun, start, alloc_length, 0,
	                     (struct scsi_read_toc_cdb *)ccb->scsiio.cdb);
}

/*
 * cam_fmt_pause_resume_ccb
 *	Format a PAUSE RESUME CCB.
 */
void	cam_fmt_pause_resume_ccb(devid, resume, ccb)
CAM_DEV	devid;
int	resume;
CCB	*ccb;
{
	cam_init_scsiio_ccb(devid, 0, ccb);
	ccb->scsiio.sg_list = NULL;
	ccb->scsiio.xfer_len = ccb->scsiio.resid = 0;
	ccb->scsiio.cdb_len = sizeof(struct scsi_pause_resume_cdb);
	cam_fmt_pause_resume_cdb(ccb->header.lun, resume, 0,
	                 (struct scsi_pause_resume_cdb *)ccb->scsiio.cdb);
}

/*
 * cam_fmt_play_audio12_ccb
 *	Format a PLAY AUDIO 12-byte CCB.
 */
void	cam_fmt_play_audio12_ccb(devid, reladr, lbaddr, length, ccb)
CAM_DEV	devid;
int	reladr;
long	lbaddr, length;
CCB	*ccb;
{
	cam_init_scsiio_ccb(devid, 0, ccb);
	ccb->scsiio.sg_list = NULL;
	ccb->scsiio.xfer_len = ccb->scsiio.resid = 0;
	ccb->scsiio.cdb_len = sizeof(struct scsi_play_audio12_cdb);
	cam_fmt_play_audio12_cdb(ccb->header.lun, reladr, lbaddr, length, 0,
	                 (struct scsi_play_audio12_cdb *)ccb->scsiio.cdb);
}

/*
 * cam_fmt_play_audio_tkindx_ccb
 *	Format a PLAY AUDIO TRACK INDEX CCB.
 */
void	cam_fmt_play_audio_tkindx_ccb(devid, start_track, start_index,
	                              end_track, end_index, ccb)
CAM_DEV	devid;
long	start_track, start_index, end_track, end_index;
CCB	*ccb;
{
	cam_init_scsiio_ccb(devid, 0, ccb);
	ccb->scsiio.sg_list = NULL;
	ccb->scsiio.xfer_len = ccb->scsiio.resid = 0;
	ccb->scsiio.cdb_len = sizeof(struct scsi_play_audio_tkindx_cdb);
	cam_fmt_play_audio_tkindx_cdb(ccb->header.lun,
	                 start_track, start_index, end_track, end_index, 0,
	                 (struct scsi_play_audio_tkindx_cdb *)ccb->scsiio.cdb);
}
