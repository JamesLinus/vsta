/*
 * Scsi.h - SCSI definitions.
 */
#ifndef	__SCSI_H__
#define	__SCSI_H__

#include <sys/types.h>
#ifdef	__VSTA__
#include "scsivsta.h"
#endif

#ifdef	__TURBOC__
typedef	unsigned int sz_bits;
#else
typedef	uint8 sz_bits;
#endif

/*
 * CDB descriptions.
 */
struct	scsi_cdb6 {
	uint8	opcode;				/* Operation code */
	sz_bits			: 5,
		lun		: 3;		/* Logical Unit Number */
	uint8	reserved[2];
	uint8	length;				/* Transfer length */
	uint8	control;
};

struct	scsi_cdb10 {
	uint8	opcode;				/* Operation code */
	sz_bits	reserved1	: 5,
		lun		: 3;		/* Logical Unit Number */
	uint8	lbaddr3;			/* logical block address */
	uint8	lbaddr2;
	uint8	lbaddr1;
	uint8	lbaddr0;
	uint8	reserved2;
	uint8	length1;			/* transfer length */
	uint8	length0;
	uint8	control;
};

/*
 * MODE SELECT 6-byte CDB.
 */
struct	scsi_mode_select_cdb6 {
	uint8	opcode;				/* Operation code */
	sz_bits	sp		: 1,		/* Save page */
		reserved1	: 3,
		pf		: 1,		/* Page format */
		lun		: 3;		/* Logical Unit Number */
	uint8	reserved2;
	uint8	reserved3;
	uint8	prmlst_len;			/* Parameter list length */
	uint8	control;
};

/*
 * MODE SENSE 6-byte CDB.
 */
struct	scsi_mode_sense_cdb6 {
	uint8	opcode;				/* Operation code */
	sz_bits	reserved1	: 3,
		dbd		: 1,		/* Disable block descriptors */
		reserved2	: 1,
		lun		: 3;		/* Logical Unit Number */
	sz_bits	pg_code	: 6,			/* Page code */
		pc		: 2;		/* Page control */
	uint8	reserved3;
	uint8	alloc_len;			/* Allocation length */
	uint8	control;
};

/*
 * SCSI_DIRECT 6-byte CDB.
 */
struct	scsi_drw_cdb6 {
	uint8	opcode;				/* Operation code */
	sz_bits	lbaddr2		: 5,		/* Logical block addr. hi */
		lun		: 3;		/* Logical Unit Number */
	uint8	lbaddr1;			/* Logical block addr. mid */
	uint8	lbaddr0;			/* Logical block addr. lo */
	uint8	length;				/* Transfer length */
	uint8	control;
};

/*
 * SCSI_SEQUENTIAL 6-byte CDB.
 */
struct	scsi_srw_cdb6 {
	uint8	opcode;				/* Operation code */
	sz_bits	fixed		: 1,		/* fixed length blocks? */
		sili		: 1,		/* Suppress Incorrect Length? */
		reserved	: 3,
		lun		: 3;		/* Logical Unit Number */
	uint8	length2;			/* transfer length */
	uint8	length1;
	uint8	length0;
	uint8	control;
};

/*
 * WRITE FILE MARKS CDB.
 */
struct	scsi_wfm_cdb {
	uint8	opcode;				/* Operation code */
	sz_bits	immed		: 1,		/* return immediately? */
		wsmk		: 1,		/* Write Setmark? */
		reserved	: 3,
		lun		: 3;		/* Logical Unit Number */
	uint8	length2;			/* transfer length */
	uint8	length1;
	uint8	length0;
	uint8	control;
};

/*
 * SPACE CDB.
 */
struct	scsi_space_cdb {
	uint8	opcode;				/* Operation code */
	sz_bits	code		: 3,		/* Space code */
		reserved	: 2,
		lun		: 3;		/* Logical Unit Number */
	uint8	count2;				/* transfer length */
	uint8	count1;
	uint8	count0;
	uint8	control;
};

/*
 * Space codes.
 */
#define	SCSI_SPC_BLOCKS		0		/* Blocks */
#define	SCSI_SPC_FILEMARKS	1		/* Filemarks */
#define	SCSI_SPC_SEQ_FILEMARKS	2		/* Sequential filemarks */
#define	SCSI_SPC_EOD		3		/* End-of-data */
#define	SCSI_SPC_SETMARKS	4		/* Setmarks */
#define	SCSI_SPC_SEQ_SETMARKS	5		/* Sequential setmarks */

/*
 * READ TOC CDB.
 */
struct	scsi_read_toc_cdb {
	uint8	opcode;				/* Operation code */
	sz_bits	reserved1	: 1,
		msf		: 1,		/* MSF format */
		reserved2	: 3,
		lun		: 3;		/* Logical Unit Number */
	uint8	reserved3[4];
	uint8	start_track;			/* starting track */
	uint8	length1;			/* allocation length */
	uint8	length0;
	uint8	control;
};

/*
 * PAUSE RESUME CDB.
 */
struct	scsi_pause_resume_cdb {
	uint8	opcode;				/* Operation code */
	sz_bits	reserved1	: 5,
		lun		: 3;		/* Logical Unit Number */
	uint8	reserved2[6];
	sz_bits	resume		: 1,		/* Resume play */
		reserved3	: 7;
	uint8	control;
};

/*
 * PLAY AUDIO(12) CDB.
 */
struct	scsi_play_audio12_cdb {
	uint8	opcode;				/* Operation code */
	sz_bits	reladr		: 1,		/* Relative addressing */
		reserved1	: 4,
		lun		: 3;		/* Logical Unit Number */
	uint8	lbaddr3;			/* logical block address */
	uint8	lbaddr2;
	uint8	lbaddr1;
	uint8	lbaddr0;
	uint8	length3;			/* transfer length */
	uint8	length2;
	uint8	length1;
	uint8	length0;
	uint8	reserved2;
	uint8	control;
};

/*
 * PLAY AUDIO TRACK INDEX CDB.
 */
struct	scsi_play_audio_tkindx_cdb {
	uint8	opcode;				/* Operation code */
	sz_bits	reserved1	: 5,		/* Relative addressing */
		lun		: 3;		/* Logical Unit Number */
	uint8	reserved2[2];
	uint8	start_track;			/* Starting track */
	uint8	start_index;			/* Starting index */
	uint8	reserved3;
	uint8	end_track;			/* Ending track */
	uint8	end_index;			/* Ending index */
	uint8	control;
};

/*
 * Union of all CDB's.
 */
union	scsi_cdb {
	struct	scsi_cdb6 cdb6;
	struct	scsi_cdb10 cdb10;
	struct	scsi_drw_cdb6 drw_cdb6;
	struct	scsi_srw_cdb6 srw_cdb6;
	struct	scsi_wfm_cdb wfm_cdb;
	struct	scsi_read_toc_cdb read_toc_cdb;
	struct	scsi_play_audio12_cdb play_audio12;
	struct	scsi_play_audio_tkindx_cdb play_audio_tkindx;
};

/*
 * SCSI status.
 */
#define	SCSI_GOOD		0		/* Good */
#define	SCSI_CHECK_COND		2		/* Check condition */
#define	SCSI_COND_MET		4		/* Condition met */
#define	SCSI_BUSY		8		/* Busy */
#define	SCSI_INTRMED		0x10		/* Intermediate  */
#define	SCSI_INTRMED_COND_MET	0x14		/* Intermediate cond. met */
#define	SCSI_RSV_CONFLICT	0x18		/* Reservation conflict */
#define	SCSI_CMD_TERM		0x22		/* Command terminated */
#define	SCSI_QUEUE_FULL		0x28		/* Queue full */

/*
 * Peripheral device types.
 */
#define	SCSI_DIRECT		0		/* Direct access device */
#define	SCSI_SEQUENTIAL		1		/* Sequential access device */
#define	SCSI_PRINTER		2		/* Printer device */
#define	SCSI_PROCESSOR		3		/* Processor device */
#define	SCSI_WORM		4		/* Write-once device */
#define	SCSI_CDROM		5		/* CD-ROM device */
#define	SCSI_SCANNER		6		/* Scanner device */
#define	SCSI_OPTICAL		7		/* Optical memory device */
#define	SCSI_MEDIUM_CHANGER	8		/* Medium changer device */
#define	SCSI_COMM		9		/* Communications device */

/*
 * SCSI commands.
 */
#define	SCSI_CHANGE_DEFN	0x40		/* Change definition */
#define	SCSI_COMPARE		0x39		/* Compare */
#define	SCSI_COPY		0x18		/* Copy */
#define	SCSI_COPY_VERIFY	0x3a		/* Copy and verify */
#define	SCSI_FORMAT_UNIT	0x04		/* Format unit */
#define	SCSI_INQUIRY		0x12		/* Inquiry */
#define	SCSI_CACHE_CTL		0x36		/* Lock/unlock cache */
#define	SCSI_LOG_SELECT		0x4c		/* Log select */
#define	SCSI_LOG_SENSE		0x4d		/* Log sense */
#define	SCSI_MODE_SELECT6	0x15		/* Mode select (6-byte) */
#define	SCSI_MODE_SELECT10	0x55		/* Mode select (10-byte) */
#define	SCSI_MODE_SENSE6	0x1a		/* Mode sense (6-byte) */
#define	SCSI_MODE_SENSE10	0x5a		/* Mode sense (10-byte) */
#define	SCSI_PRE_FETCH		0x34		/* Pre-fetch */
#define	SCSI_PREVENT		0x1e		/* Prevent/allow removal */
#define	SCSI_READ6		0x08		/* Read (6-byte) */
#define	SCSI_READ10		0x28		/* Read (10-byte) */
#define	SCSI_READ_BUFFER	0x3c		/* Read buffer */
#define	SCSI_READ_CAPACITY	0x25		/* Read capacity  */
#define	SCSI_READ_DEFECTS	0x37		/* Read defect data */
#define	SCSI_READ_LONG		0x3e		/* Read long */
#define	SCSI_REASSIGN_BLOCKS	0x07		/* Reassign blocks */
#define	SCSI_RECV_DIAG		0x1c		/* Receive diagnostic */
#define	SCSI_RELEASE		0x17		/* Release  */
#define	SCSI_REQSNS		0x03		/* Request sense */
#define	SCSI_RESERVE		0x16		/* Reserve */
#define	SCSI_REZERO_UNIT	0x01		/* Rezero unit */
#define	SCSI_SEARCH_EQUAL	0x31		/* Search data equal */
#define	SCSI_SEARCH_HIGH	0x30		/* Search data high */
#define	SCSI_SEARCH_LOW		0x32		/* Search data low */
#define	SCSI_SEEK6		0x0b		/* Seek (6-byte) */
#define	SCSI_SEEK10		0x2b		/* Seek (10-byte) */
#define	SCSI_SEND_DIAG		0x1d		/* Send diagnostic */
#define	SCSI_SET_LIMITS		0x33		/* Set limits */
#define	SCSI_STOP_START		0x1b		/* Stop/start unit */
#define	SCSI_SYNC_CACHE		0x35		/* Synchronize cache */
#define	SCSI_TUR		0x00		/* Test unit ready */
#define	SCSI_VERIFY		0x2f		/* Verify */
#define	SCSI_WRITE6		0x0a		/* Write (6-byte) */
#define	SCSI_WRITE10		0x2a		/* Write (10-byte) */
#define	SCSI_WRITE_VERIFY	0x2e		/* Write and verify */
#define	SCSI_WRITE_BUFFER	0x3b		/* Write buffer */
#define	SCSI_WRITE_LONG		0x3f		/* Write long */
#define	SCSI_WRITE_SAME		0x41		/* Write same */
#define	SCSI_REWIND		0x01		/* Rewind */
#define	SCSI_SPACE		0x11		/* Positioning functions */
#define	SCSI_WFM		0x10		/* Write File Marks */
#define	SCSI_READ_TOC		0x43		/* Read Table of Contents */
#define	SCSI_PAUSE_RESUME	0x4b		/* Pause/Resume Audio */
#define	SCSI_PLAY_AUDIO10	0x45		/* Play Audio (10-byte) */
#define	SCSI_PLAY_AUDIO12	0xa5		/* Play Audio (12-byte) */
#define	SCSI_PLAY_AUDIO_TKINDX	0x48		/* Play Audio Track Index */

/*
 * SCSI bus phases.
 */
enum	scsi_bus_phases {
	SCSI_BUS_FREE,
	SCSI_ARBITRATION_PHASE,
	SCSI_SELECT_PHASE,
	SCSI_RESELECT_PHASE,
	SCSI_COMMAND_PHASE,
	SCSI_DATA_PHASE,
	SCSI_STATUS_PHASE,
	SCSI_MESSAGE_PHASE
};

/*
 * Limits.
 */
#define	SCSI_MAX_CDB6_LBADDR	((long)((1 << 20) - 1))
#define	SCSI_MAX_CDB6_BLOCKS	255

/*
 * Inquiry data.
 */
struct	scsi_inq_data {
	sz_bits	pdev_type	: 5,		/* Peripheral device type */
		periph_qual	: 3;		/* Peripheral qualifier */
	sz_bits	type_mod	: 7,		/* Device-type modifier */
		rmb		: 1;		/* Removable device? */
	sz_bits	ansi_version	: 3,		/* ANSI-approved version */
		ecma_version	: 3,		/* ECMA version */
		iso_version	: 2;		/* ISO version */
	sz_bits	response_fmt	: 4,		/* Response data format */
		reserved1	: 2,
		trmiop		: 1,		/* Term. I/O process */
		aenc		: 1;		/* AENC capability */
	uint8	additional_len;			/* Additional length */
	uint8	reserved2[2];
	sz_bits	sft_reset	: 1,		/* Soft Reset */
		cmdque		: 1,		/* Command queuing */
		reserved3	: 1,
		linked		: 1,		/* Linked command */
		sync		: 1,		/* Sync. data transfer */
		wbus16		: 1,		/* Wide bus 16 */
		wbus32		: 1,		/* Wide bus 32 */
		reladr		: 1;		/* Relative addressing */
	uint8	vendor_id[8];			/* Vendor ID */
	uint8	prod_id[16];			/* Product ID */
	uint8	prod_rev[4];			/* Product Revision */
};

/*
 * REQUEST SENSE data for error codes 0x70 and 0x71.
 */
struct	scsi_reqsns_data {
	sz_bits	error_code	: 7,		/* Error code (0x70 or 0x71) */
		valid		: 1;		/* Peripheral qualifier */
	uint8	segnum;				/* Segment number */
	sz_bits	snskey		: 4,		/* Sense key */
		reserved	: 1,
		ili		: 1,		/* Incorrect Length Indicator */
		eom		: 1,		/* End Of Medium */
		filemark	: 1;		/* Filemark */
	uint8	info[4];			/* Information field */
	uint8	adsns_len;			/* Additional sense length */
	uint8	cmdspc_info[4];			/* Command specific info. */
	uint8	snscode;			/* Additional sense code */
	uint8	snsqual;			/* Add. sense code qualifier */
	uint8	fru_code;			/* Field replaceable unit */
	uint8	snskey_spc[3];			/* Sense key specific */
};

/*
 * MODE SELECT/SENSE 6-byte mode parameter header.
 */
struct	scsi_mdparam_hdr6 {
	uint8	length;				/* Mode data length */
	uint8	medium_type;			/* Medium type */
	uint8	device_spec;			/* Device specific parameter */
	uint8	blkdesc_len;			/* Block descriptor length */
};

/*
 * MODE SELECT/SENSE block descriptor.
 */
struct	scsi_mdparam_blkdesc {
	uint8	density;			/* density code */
	uint8	nblocks[3];			/* number of blocks */
	uint8	reserved;
	uint8	blklen[3];			/* block length */
};

/*
 * MODE SELECT/SENSE page code.
 */
#define	SCSI_VEND_SPEC_PGCODE	0
#define	SCSI_RDWR_ERECOV_PGCODE	1
#define	SCSI_CONNECT_PGCODE	2
#define	SCSI_FORMAT_DEV_PGCODE	3
#define	SCSI_RIGID_GEOM_PGCODE	4
#define	SCSI_FLEX_DISK_PGCODE	5
#define	SCSI_VRFY_ERECOV_PGCODE	7
#define	SCSI_CACHE_PGCODE	8
#define	SCSI_PERIPH_DEV_PGCODE	9
#define	SCSI_CNTL_MODE_PGCODE	0xa
#define	SCSI_MEDIUM_TYPE_PGCODE	0xb
#define	SCSI_NOTCH_PART_PGCODE	0xc
#define	SCSI_ALL_PGS_PGCODE	0x3f

/*
 * MODE SELECT/SENSE page header.
 */
struct	scsi_mdpage_header {
	uint8	page_code	: 6,		/* page code */
		reserved1	: 1,
		ps		: 1;		/* Parameters Savable */
	uint8	page_length;			/* page length */
};

/*
 * MODE SELECT/SENSE cache parameters mode page.
 */
struct	scsi_cache_mdpage {
	struct	scsi_mdpage_header header;
	uint8	rcd		: 1,		/* Read Cache Disable */
		mf		: 1,		/* Multiplication Factor */
		wce		: 1,		/* Write Cache Enable */
		reserved2	: 5;
	uint8	wrreten_pri	: 4,		/* write retention priority */
		rdreten_pri	: 4;		/* read retention priority */
	uint8	dis_pref_xfer_len[2];		/* disable pre-fetch xfer len */
	uint8	min_pre_fetch[2];		/* minimum pre-fetch */
	uint8	max_pre_fetch[2];		/* maximum pre-fetch */
	uint8	max_ceiling[2];			/* maximum pre-fetch ceiling */
};

/*
 * MODE SELECT/SENSE rigid disk drive geometry mode page.
 */
struct	scsi_rigid_geom_mdpage {
	struct	scsi_mdpage_header header;
	uint8	ncylinders[3];			/* number of cylinders */
	uint8	nheads;				/* number of heads */
	uint8	wrprecomp_start[3];		/* write precomp. start */
	uint8	redwrcur_start[3];		/* reduced wr. current start */
	uint8	step_rate[2];			/* drive step rate */
	uint8	landing_zone[3];		/* landing zone cylinder */
	uint8	rpl		: 1,		/* Rotational Position Lock */
		reserved1	: 7;
	uint8	rotational_offset;		/* rotational offset */
	uint8	reserved2;
	uint8	rotation_rate[2];		/* medium rotation rate */
	uint8	reserved3[2];
};

/*
 * Union of MODE SELECT/SENSE mode pages.
 */
union	scsi_mdpages {
	struct	scsi_mdpage_header header;
	struct	scsi_cache_mdpage cache;
	struct	scsi_rigid_geom_mdpage rigid_geom;
};

struct	scsi_mdparam_list6 {
	struct	scsi_mdparam_hdr6 header;
	struct	scsi_mdparam_blkdesc blkdesc[1];
};

/*
 * Macros for navigating MODE SENSE/SELECT pages.
 */
#define	SCSI_GET_MDPARAM_BLKDESC6(_p)					\
		(((struct scsi_mdparam_list6)(_p))->blkdesc)
#define	SCSI_GET_MDPAGES6(_mdh6)					\
		((union scsi_mdpages *)((char *)((_mdh6) + 1) +		\
		                        (_mdh6)->blkdesc_len))
#define	SCSI_GET_NEXT_MDPAGE(_mdpg)					\
		((union scsi_mdpages *)((char *)(_mdpg) +		\
		                    sizeof(struct scsi_mdpage_header) +	\
		                    (_mdpg)->header.page_length))

/*
 * Device specific defines.
 */
#define	SCSI_TAPE_SPEED(_ds)	((_ds) & 0xf)
#define	SCSI_TAPE_BUFMODE(_ds)	(((_ds) >> 4) & 7)
#define	SCSI_TAPE_NOBUF		0		/* not buffered */
#define	SCSI_TAPE_MULTBUF	1		/* 1 or more buffered blocks */
#define	SCSI_TAPE_1BUF		2		/* 1 buffered block */
#define	SCSI_TAPE_WP		0x80		/* tape write protect */

/*
 * MODE SENSE block descriptor.
 */
struct	scsi_blkdesc {
	uint8	density;			/* Density code */
	uint8	nblocks[3];			/* Number of blocks */
	uint8	reserved;
	uint8	blklen[3];			/* Block length */
};

/*
 * Read capacity data.
 */
struct	scsi_rdcap_data {
	uint8	lbaddr[4];			/* logical block address */
	uint8	blklen[4];			/* logical block length */
};

/*
 * READ TOC data.
 */
struct	scsi_read_toc_data {
	struct	scsi_read_toc_header {
		uint8	length1;		/* TOC data length */
		uint8	length0;
		uint8	first_track;		/* first track number */
		uint8	last_track;		/* last track number */
	} header;
	struct	scsi_read_toc_desc {
		uint8	reserved1;
		sz_bits	control		: 4,	/* track attributes */
			adr		: 4;	/* ADR sub-chan Q field */
		uint8	track;			/* track number */
		uint8	reserved2;
		uint8	address3;		/* absolute CD-ROM address */
		uint8	address2;
		uint8	address1;
		uint8	address0;
	} desc[1];
};

#endif	/*__SCSI_H__*/

