/*
 * Cam.h - Common Access Method definitions.
 */
#ifndef	__CAM_H__
#define	__CAM_H__

#include <sys/types.h>
#ifdef	__VSTA__
#include "camvsta.h"
#endif
#include "scsi.h"

/*
 * General allocation length defines for the CCB structures.
 */

#define	CAM_IOCDBLEN		12		/* Space for the CDB bytes */
#define	CAM_VUHBA		16		/* Vendor Unique HBA length */
#define	CAM_SIM_ID		16		/* SIM ID ASCII string len. */
#define	CAM_HBA_ID		16		/* HBA ID ASCII string len. */
#define	CAM_SIM_PRIV		50		/* SIM private data area len. */

/*
 * The CAM CCB header.
 */
struct	cam_ccb_header {
	struct	cam_ccb_header *myaddr;		/* address of this CCB */
	uint16	ccb_length;			/* length of the entire CCB */
	uint8	fcn_code;			/* XPT function code */
	uint8	cam_status;			/* CAM status */
	uint8	reserved;
	uint8	path_id;			/* path ID */
	uint8	target;				/* target ID */
	uint8	lun;				/* LUN */
	uint32	cam_flags;			/* OSD CAM flags */
};

/*
 * CAM Get Device Type CCB.
 */
struct	cam_getdev_ccb {
	struct	cam_ccb_header header;		/* CAM CCB header */
	void	*inquiry_data;			/* Inquiry Data Pointer */
	uint8	dev_type;			/* Peripheral Device Type */
};

/*
 * Path Inquiry CCB.
 */
struct	cam_pathinq_ccb {
	struct	cam_ccb_header header;		/* CAM CCB header */
	uint8	version;			/* Version Number */
	uint8	scsi_cap;			/* SCSI Capabilities */
	uint8	target;				/* Target Mode Support */
	uint8	misc;				/* Miscellaneous */
	uint16	engine_cnt;			/* Engine Count */
	uint8	vendor_unique[CAM_VUHBA];	/* Vendor Unique */
	uint32	private_data_size;		/* Size of Private Data Area */
	uint32	async_cap;			/* Async. Event Capabilities */
	uint8	path_id_max;			/* Highest Path ID Assigned */
	uint8	scsi_dev_id;			/* Initiator SCSI Device ID */
	uint8	reserved2;
	uint8	reserved3;
	uint8	sim_vendor_id[CAM_SIM_ID];	/* SIM Vendor ID */
	uint8	hba_vendor_id[CAM_HBA_ID];	/* HBA Vendor ID */
	uint32	osd_usage;			/* OSD Usage */
};

/*
 * Release SIM Queue CCB.
 */
struct	cam_relsim_ccb {
	struct	cam_ccb_header header;		/* CAM CCB header */
};

/*
 * Set Async Callback CCB.
 */
struct	cam_setasync_ccb {
	struct	cam_ccb_header header;		/* CAM CCB header */
	uint32	async_cap;			/* Async. Event Capabilities */
	void	*async_cb_ptr;			/* Async. Callback Pointer */
	void	*pdrv_buf_ptr;			/* PDRV Buffer Pointer */
	uint8	pdrv_buf_size;			/* PDRV Buffer Size */
};

/*
 * Set Device Type CCB.
 */
struct	cam_setdev_ccb {
	struct	cam_ccb_header header;		/* CAM CCB header */
	uint8	device_type;			/* Peripheral Device Type */
};

/*
 * Abort XPT Request CCB.
 */
struct	cam_abort_ccb {
	struct	cam_ccb_header header;		/* CAM CCB header */
	union	cam_ccb *ccb;			/* target CCB */
};

/*
 * Reset SCSI Bus CCB.
 */
struct	cam_resetbus_ccb {
	struct	cam_ccb_header header;		/* CAM CCB header */
};

/*
 * Reset SCSI Device CCB.
 */
struct	cam_resetdev_ccb {
	struct	cam_ccb_header header;		/* CAM CCB header */
};

/*
 * Terminate I/O Process Request CCB.
 */
struct	cam_termio_ccb {
	struct	cam_ccb_header header;		/* CAM CCB header */
	union	cam_ccb *ccb;			/* target CCB */
};

/*
 * Scatter/gather list element.
 */
typedef struct cam_sg_elem {
	void	*sg_address;			/* Scatter/Gather address */
	uint32	sg_length;			/* byte count */
} CAM_SG_ELEM;

/*
 * CAM Control Block to Request I/O CCB.
 */
struct	cam_scsiio_ccb {
	struct	cam_ccb_header header;		/* CAM CCB header */
	void	*pdrv_ptr;			/* Peripheral Driver Pointer */
	union	cam_ccb *next_ccb;		/* Next CCB Pointer */
	void	*reqmap;			/* original request struct. */
	void	(*completion)();		/* Callback on Completion */
	struct	cam_sg_elem *sg_list;		/* SG List/Data Buffer Ptr. */
	uint32	xfer_len;			/* Data Transfer Length */
	void	*sense_info;			/* Sense Info. Buffer Pointer */
	uint8	sense_info_len;			/* Sense Info. Buffer Length */
	uint8	cdb_len;			/* CDB Length */
	uint16	sg_count;			/* # Scatter/Gather Entries */
	uint32	vu_field;
	uint8	scsi_status;			/* SCSI status */
	uint32	reserved3[3];
	uint32	resid;				/* Residual Length */
	uint8	cdb[CAM_IOCDBLEN];		/* CDB */
	uint32	timeout;			/* Timeout Value */
	void	*msg_buf;			/* Message Buffer Pointer */
	uint16	msg_buf_len;			/* Message Buffer Length */
	uint16	vu_flags;			/* VU Flags */
	uint8	tag_queue_action;		/* Tag Queue Action */
	uint8	reserved4[3];
/*
 * Private data...
 */
	struct	scsi_reqsns_data snsdata;	/* AUTOSENSE data */
/*
 * XPT/SIM data.
 */
	struct	cam_simq {
		struct	q_header head;		/* SIM I/O queue */
		union	cam_ccb *ccb;		/* top of CCB */
	} simq;
/*
 * Support for multiple transactions per request.
 */
	struct	cam_sg_elem *sg_base;		/* SG list base addr. */
	uint16	sg_next;			/* next SG element */
	uint16	sg_max;				/* # SG elements */
	uint32	sg_resid;			/* residual w/in SG element */
	struct	cam_sg_elem sg_element;		/* working SG area */
	uint32	lbaddr;				/* logical block address */
};

union	cam_ccb {
	struct	cam_ccb_header header;		/* CAM CCB header */
	struct	cam_getdev_ccb getdev;		/* CAM Get Device Type */
	struct	cam_pathinq_ccb pathinq;	/* Path Inquiry */
	struct	cam_relsim_ccb relsim;		/* Release SIM Queue */
	struct	cam_setasync_ccb setasync;	/* Set Async Callback */
	struct	cam_setdev_ccb setdev;		/* Set Device Type */
	struct	cam_abort_ccb abort;		/* Abort XPT Request */
	struct	cam_resetbus_ccb resetbus;	/* Reset SCSI Bus */
	struct	cam_resetdev_ccb resetdev;	/* Reset SCSI Device */
	struct	cam_termio_ccb termio;		/* Term. I/O Process Request */
	struct	cam_scsiio_ccb scsiio;		/* Request SCSI I/O */
};

typedef	union cam_ccb CCB;

/*
 * CAM Flags.
 */
#define CAM_DIR_RESV		0x00000000	/* Data dir. (00: reserved) */
#define CAM_DIR_IN		0x00000040	/* Data dir. (01: DATA IN) */
#define CAM_DIR_OUT		0x00000080	/* Data dir. (10: DATA OUT) */
#define CAM_DIR_NONE		0x000000C0	/* Data dir. (11: no data) */
#define CAM_DIS_AUTOSENSE	0x00000020	/* Disable auto sence feature */
#define CAM_SG_VALID		0x00000010	/* Scatter/gather list valid */
#define CAM_DIS_CALLBACK	0x00000008	/* Disable callback feature */
#define CAM_CDB_LINKED		0x00000004	/* CCB contains a linked CDB */
#define CAM_QUEUE_ENABLE	0x00000002	/* SIM queue actions enabled */
#define CAM_CDB_POINTER		0x00000001	/* CDB field contains a ptr. */

#define CAM_DIS_DISCONNECT	0x00008000	/* Disable disconnect */
#define CAM_INITIATE_SYNC	0x00004000	/* Attempt Sync data xfer */
#define CAM_DIS_SYNC		0x00002000	/* Disable sync, go to async */
#define CAM_SIM_QHEAD		0x00001000	/* Put CCB @ the head of SIMQ */
#define CAM_SIM_QFREEZE		0x00000800	/* Freeze SIMQ */

#define CAM_CDB_PHYS		0x00400000	/* CDB pointer is physical */
#define CAM_DATA_PHYS		0x00200000	/* SG/Buffer data is physical */
#define CAM_SNS_BUF_PHYS	0x00100000	/* Autosense pointer physical */
#define CAM_MSG_BUF_PHYS	0x00080000	/* Message buffer is physical */
#define CAM_NXT_CCB_PHYS	0x00040000	/* Next CCB is physical */
#define CAM_CALLBCK_PHYS	0x00020000	/* Callback func is physical */

#define CAM_DATAB_VALID		0x80000000	/* Data buffer valid */
#define CAM_STATUS_VALID	0x40000000	/* Status buffer valid */
#define CAM_MSGB_VALID		0x20000000	/* Message buffer valid */
#define CAM_TGT_PHASE_MODE	0x08000000	/* SIM will run in phase mode */
#define CAM_TGT_CCB_AVAIL	0x04000000	/* Target CCB available */
#define CAM_DIS_AUTODISC	0x02000000	/* Disable autodisconnect */
#define CAM_DIS_AUTOSRP		0x01000000	/* Disable Auto sv/rst ptrs */

/*
 * CAM Status
 */
#define CAM_REQ_INPROG         	0x00	/* CCB request is in progress */
#define CAM_REQ_CMP            	0x01	/* CCB request completed w/out error */
#define CAM_REQ_ABORTED        	0x02	/* CCB request aborted by the host */
#define CAM_UA_ABORT           	0x03	/* Unable to Abort CCB request */
#define CAM_REQ_CMP_ERR        	0x04	/* CCB request completed with an err */
#define CAM_BUSY               	0x05	/* CAM subsystem is busy */
#define CAM_REQ_INVALID        	0x06	/* CCB request is invalid */
#define CAM_PATH_INVALID       	0x07	/* Path ID supplied is invalid */
#define CAM_DEV_NOT_THERE      	0x08	/* SCSI device not installed/there */
#define CAM_UA_TERMIO          	0x09	/* Unable to Terminate I/O CCB req */
#define CAM_SEL_TIMEOUT        	0x0A	/* Target selection timeout */
#define CAM_CMD_TIMEOUT        	0x0B	/* Command timeout */
#define CAM_MSG_REJECT_REC     	0x0D	/* Message reject received */
#define CAM_SCSI_BUS_RESET     	0x0E	/* SCSI bus reset sent/received */
#define CAM_UNCOR_PARITY       	0x0F	/* Uncorrectable parity error occured */
#define CAM_AUTOSENSE_FAIL     	0x10	/* Autosense: Request sense cmd fail */
#define CAM_NO_HBA             	0x11	/* No HBA detected Error */
#define CAM_DATA_OVUN_RUN      	0x12	/* Data overrun/underrun error */
#define CAM_UNEXP_BUSFREE      	0x13	/* Unexpected BUS free */
#define CAM_TARG_SEQ_FAIL      	0x14	/* Target bus phase sequence failure */
#define CAM_CCB_LEN_ERR        	0x15	/* CCB length supplied is inadaquate */
#define CAM_PROVIDE_FAIL       	0x16	/* Unable to provide requ. capability */
#define CAM_BDR_SENT           	0x17	/* A SCSI BDR msg was sent to target */
#define CAM_REQ_TERMIO         	0x18	/* CCB request terminated by the host */

#define CAM_LUN_INVALID        	0x38	/* LUN supplied is invalid */
#define CAM_TID_INVALID        	0x39	/* Target ID supplied is invalid */
#define CAM_FUNC_NOTAVAIL      	0x3A	/* The requ. func is not available */
#define CAM_NO_NEXUS           	0x3B	/* Nexus is not established */
#define CAM_IID_INVALID        	0x3C	/* The initiator ID is invalid */
#define CAM_CDB_RECVD          	0x3E	/* The SCSI CDB has been received */
#define CAM_SCSI_BUSY          	0x3F	/* SCSI bus busy */

#define CAM_SIM_QFRZN          	0x40	/* The SIM queue is frozen w/this err */
#define CAM_AUTOSNS_VALID      	0x80	/* Autosense data valid for target */

#define	CAM_CCB_STATUS(_ccb)	\
	((_ccb)->header.cam_status & ~(CAM_SIM_QFRZN | CAM_AUTOSNS_VALID))

/*
 * Defines for the XPT function codes.
 */

/* Common function commands, 0x00 - 0x0F */
#define	XPT_NOOP		0x00		/* Execute Nothing */
#define	XPT_SCSI_IO		0x01		/* Execute requested SCSI IO */
#define	XPT_GDEV_TYPE		0x02		/* Get device type info. */
#define	XPT_PATH_INQ		0x03		/* Path Inquiry */
#define	XPT_REL_SIMQ		0x04		/* Release frozen SIM queue */
#define	XPT_SASYNC_CB		0x05		/* Set Async callback param's */
#define	XPT_SDEV_TYPE		0x06		/* Set device type info. */

/* XPT SCSI control functions, 0x10 - 0x1F */
#define	XPT_ABORT		0x10		/* Abort the selected CCB */
#define	XPT_RESET_BUS		0x11		/* Reset the SCSI bus */
#define	XPT_RESET_DEV		0x12		/* Reset SCSI devices (BDR) */
#define	XPT_TERM_IO		0x13		/* Terminate I/O process */

/* Target mode commands, 0x30 - 0x3F */
#define	XPT_EN_LUN		0x30		/* Enable LUN (Target mode) */
#define	XPT_TARGET_IO		0x31		/* Execute target IO request */

#define	XPT_FUNC		0x7F		/* TEMPLATE */
#define	XPT_VUNIQUE		0x80		/* Vendor unique commands */

/*
 * Defines for the SIM/HBA queue actions.  These value are used in the
 * SCSI I/O CCB, for the queue action field.
 */
#define	CAM_SIMPLE_QTAG		0x20		/* Tag for a simple queue */
#define CAM_HEAD_QTAG		0x21		/* Tag for head of queue */
#define CAM_ORDERED_QTAG	0x22		/* Tag for ordered queue */

/*
 * Defines for the timeout field in the SCSI I/O CCB.
 */
#define	CAM_TIME_DEFAULT	0x00000000	/* Use SIM default value */
#define	CAM_TIME_INFINITY	0xFFFFFFFF	/* Infinite timout for I/O */

/*
 * Defines for the Path Inquiry CCB fields.
 */

#define	CAM_VERSION		0x22		/* Current version value */

#define	CAM_PI_MDP_ABLE		0x80		/* Supports MDP message */
#define	CAM_PI_WIDE_32		0x40		/* Supports 32 bit wide SCSI */
#define	CAM_PI_WIDE_16		0x20		/* Supports 16 bit wide SCSI */
#define	CAM_PI_SDTR_ABLE	0x10		/* Supports SDTR message */
#define	CAM_PI_LINKED_CDB	0x08		/* Supports linked CDBs */
#define	CAM_PI_TAG_ABLE		0x02		/* Supports tag queue message */
#define	CAM_PI_SOFT_RST		0x01		/* Supports soft reset */

#define	CAM_PIT_PROCESSOR	0x80		/* Target mode processor mode */
#define	CAM_PIT_PHASE		0x40		/* Target mode phase cog. mode*/

#define	CAM_PIM_SCANHILO	0x80		/* Bus scans from ID 7 to 0 */
#define	CAM_PIM_NOREMOVE	0x40		/* Removable devices not
						   included in scan */

/*
 * Defines for Asynchronous Callback CCB fields.
 * Note that bits 31-24 are Vendor Unique and  23-8 are reserved.
 */
#define	CAM_AC_FOUND_DEVICES	0x80		/* New devies found in rescan */
#define	CAM_AC_SIM_DEREGISTER	0x40 		/* A SIM has de-registered */
#define	CAM_AC_SIM_REGISTER	0x20 		/* A SIM has registered */
#define	CAM_AC_SENT_BDR		0x10 		/* A BDR message was sent */
#define	CAM_AC_SCSI_AEN		0x08 		/* A SCSI AEN received */
#define	CAM_AC_UNSOL_RESEL	0x02 		/* A unsolicited reselection */
#define	CAM_AC_BUS_RESET	0x01 		/* A SCSI bus RESET occured */

#define	CAM_INQLEN		36		/* Inquiry string length */

/*
 * The Async callback information.  This structure is used to store the
 * supplied info from the Set Async Callback CCB, in the EDT table.
 */
typedef struct cam_async_info {
	uint16	cam_event_enable;		/* Event enables for Callback */
	void	(*cam_async_func)();		/* Async Callback function */
	uint32	cam_async_blen;			/* "information" buffer len. */
	uint8	*cam_async_ptr;			/* "information" address */
} CAM_ASYNC_INFO;

/*
 * The CAM_SIM_ENTRY definition is used to define the entry points for
 * the SIMs contained in the SCSI CAM subsystem.  Each SIM file will
 * contain a declaration for it's entry.  The address for this entry will
 * be stored in the cam_conftbl[] array along will all the other SIM
 * entries.
 */
typedef struct cam_sim_entry {
	long	(*sim_init)();			/* SIM init routine */
	long	(*sim_action)();		/* SIM CCB go routine */
} CAM_SIM_ENTRY;

/*
 * The CAM EDT table, this structure contains the device information for
 * all the devices, SCSI ID and LUN, for all the SCSI busses in the system.
 */
typedef struct cam_edt_entry {
	long	tlun_found;			/* target/LUN exists */
	struct	cam_async_info async_info;	/* B/T/L Async callback info. */
	uint32	owner_tag;			/* Tag for PDRV's ownership */
	char	inq_data[CAM_INQLEN];		/* inquiry data storage */
} CAM_EDT_ENTRY;

#define	CAM_MAX_TARGET		7		/* maximum target number */
#define	CAM_NTARGETS		8		/* max. # of targets/bus */
#define	CAM_MAX_LUN		7		/* maximum LUN */
#define	CAM_NLUNS		8		/* max. # of LUNs/target */

#ifdef	_XXX_
/*
 * CAM status.
 */
#define	CAM_REQ_INPROG		0x00		/* Request in progress */
#define	CAM_REQ_COMPLETE	0x01		/* Request complete w/o error */
#define	CAM_REQ_ABORTED		0x02		/* Request aborted by host */
#define	CAM_CANT_ABORT		0x03		/* Unable to Abort Request */
#define	CAM_REQ_FAILED		0x04		/* Request complete w/ error */
#define	CAM_BUSY		0x05		/* Can't accept request */
#define	CAM_REQ_INVALID		0x06		/* Invalid Request */
#define	CAM_PATH_ID_INVALID	0x07		/* Invalid Path ID  */
#define	CAM_NO_DEVICE		0x08		/* SCSI device not installed */
#define	CAM_CANT_TERMINATE	0x09		/* Can't Terminate I/O Proc. */
#define	CAM_SELECT_TIMEOUT	0x0A		/* Target Selection Timeout */
#define	CAM_COMMAND_TIMEOUT	0x0B		/* Command Timeout */
#define	CAM_MSG_REJECT		0x0D		/* Message Reject received */
#define	CAM_BUS_RESET		0x0E		/* SCSI Bus Reset Sent/Rcv'd */
#define	CAM_PARITY_ERROR	0x0F		/* Parity Error Detected */
#define	CAM_AUTOSENSE_FAILED	0x10		/* Autosense RQST_SNS failed */
#define	CAM_NO_HBA		0x11		/* HBA no longer responds */
#define	CAM_DATA_OVERRUN	0x12		/* Data OverRun error */
#define	CAM_UNEXPECTED_BUS_FREE	0x13		/* Unexpected Bus Free */
#define	CAM_PHASE_ERROR		0x14		/* Target bus phase error */
#define	CAM_SHORT_CCB		0x15		/* CCB Length Inadequate */
#define	CAM_INVALID_CAP		0x16		/* Rq'sted capability invalid */
#define	CAM_BDR_SENT		0x17		/* Bus Device Reset Sent */
#define	CAM_IOPROC_TERMINATED	0x18		/* TERMIO on this CCB */
#define	CAM_LUN_INVALID		0x38		/* Invalid LUN */
#define	CAM_TARG_ID_INVALID	0x39		/* Invalid Target ID */
#define	CAM_NOT_IMPLEMENTED	0x3A		/* Function Not Implemented */
#define	CAM_NO_NEXUS		0x3B		/* Nexus not Established */
#define	CAM_INITIATOR_INVALID	0x3C		/* Invalid Initiator ID */
#define	CAM_CDB_RECEIVED	0x3D		/* SCSI CDB Received */
#define	CAM_LUN_ENABLED		0x3E		/* LUN Already Enabled */
#define	CAM_BUS_BUSY		0x3F		/* SCSI bus Busy */

#endif	/*_XXX_*/

/**
 ** SIM structures.
 **/

struct	sim_target {
	uint32	target_flags;
	struct	q_header head;			/* CCB queue */
	int	nactive;			/* number of active CCB's */
};

struct	sim_bus {
	long	path_id;			/* CAM path ID */
	enum	scsi_bus_phases phase;
	uint32	bus_flags;
	uint32	last_target;			/* last target scheduled  */
	long	(*start)();			/* SIM driver start I/O */
	struct	sim_target target_info[CAM_NTARGETS];
};

/**
 ** CAM EDT structures.
 **/

/*
 * The cam_edt[] data structure is created during the initialization process
 * to contain the necessary information of all the devices found on all the
 * HBAs during the init sequence. 
 */
struct	cam_bus_entry {
	CAM_SIM_ENTRY	*sim_entry;		/* SIM function pointers */
	struct	cam_device_entry *devices;
	int	ndevices;
};

/*
 * Per target/lun info.
 */
struct	cam_device_entry {
	uint8	target;				/* target ID */
	uint8	lun;				/* LUN */
	struct	cam_edt_entry dev_edt;		/* Device EDT info */
	union {
		struct {
			uint32	blklen;		/* logical block length */
			uint32	nblocks;	/* number of disk blocks */
		} disk;
	} device;
};

/**
 ** Peripheral driver/device definitions.
 **/

/*
 * CAM peripheral driver classes.
 */
enum	cam_pdrv_classes {
	CAMPC_GEN, CAMPC_DISK, CAMPC_TAPE, CAMPC_PRINTER,
	CAMPC_PROCESSOR, CAMPC_SCANNER, CAMPC_CHANGER, CAMPC_COMM
};

struct	cam_pdev_header {			/* peripheral device type */
	uint8	type;				/* peripheral type */
	enum	cam_pdrv_classes class;		/* peripheral driver class */
	CAM_DEV	devid;				/* device ID */
	uint32	blklen;				/* logical block length */
	char	*name;				/* device name */
	char	vendor_id[9];			/* Vendor ID */
	char	prod_id[17];			/* Product ID */
	char	prod_rev[5];			/* Product Revision */
	void	*osd;				/* OSD storage */
	union	cam_pdevice *next;		/* next link in the chain */
};

struct	cam_pdisk {				/* peripheral disk */
	struct	cam_pdev_header header;
	uint32	nblocks;			/* number of blocks */
	void	*partitions;			/* partition table */
	uchar	removable;			/* is media removable? */
};

struct	cam_ptape {				/* peripheral tape */
	struct	cam_pdev_header header;
	uint32	flags;				/* see ptape.c */
	struct	cam_ptape_mode {
		int	mdset;			/* mode set? */
		int	neotfm;			/* # filemarks for EOT */
		int	density;		/* density code */
		uint32	blklen;			/* fixed block length */
		uint16	devspc;			/* device specific */
	} modes[CAM_MAX_TAPE_MODES];
};

struct	cam_pgen {				/* generic */
	struct	cam_pdev_header header;
	uint32	nblocks;			/* number of blocks */
};

union	cam_pdevice {				/* peripheral device */
	struct	cam_pdev_header header;
	struct	cam_pdisk disk;			/* disk driver */
	struct	cam_ptape tape;			/* tape driver */
	struct	cam_pgen generic;		/* generic driver */
};

/*
 * Error flags.
 */
#define	CAM_PRINT_SYSERR	1		/* system error messages */

/**
 ** Debug.
 **/
#define	CAM_DBG_FCN_ENTRY	1		/* function entry messages */
#define	CAM_DBG_FCN_EXIT	2		/* function exit messages */
#define	CAM_DBG_MSG		4		/* generic messages */
#define	CAM_DBG_DEVCONF		8		/* device configuration */

#define	CAM_DBG_ALL		(CAM_DBG_FCN_ENTRY | CAM_DBG_FCN_EXIT |	\
				 CAM_DBG_MSG | CAM_DBG_DEVCONF)

#define	CAM_DEBUG(_when, _myname, _msg, _arg)				\
		cam_debug(_when, _myname, _msg, _arg)

#define	CAM_DEBUG2(_when, _myname, _msg, _arg1, _arg2)			\
		cam_debug(_when, _myname, _msg, _arg1, _arg2)

#define	CAM_DEBUG_FCN_ENTRY(_myname)					\
		cam_debug(CAM_DBG_FCN_ENTRY, _myname, "...")

#define	CAM_DEBUG_FCN_EXIT(_myname, _status)				\
		cam_debug(CAM_DBG_FCN_EXIT, _myname,			\
		          "exitting with status %d", _status)

/**
 ** External data declarations.
 **/
extern	CAM_SIM_ENTRY **cam_conftbl;
extern	int cam_nconftbl_entries;
extern	struct cam_bus_entry *cam_edt;
extern	uint32 cam_debug_flags;

/**
 ** Standard function prototypes.
 **/
#ifdef	__STDC__
long	xpt_init(void);
CCB	*xpt_ccb_alloc(void);
void	xpt_ccb_free(CCB *ccb);
long	xpt_action(CCB *ccb);
long	xpt_bus_register(CAM_SIM_ENTRY *);
long	xpt_bus_deregister(long path_id);
long	xpt_async(long opcode, long path_id, long target_id, long lun,
	          void *buffer_ptr, long data_cnt);
void	cam_error(uint32 flags, char *myname, char *fmt, ...);
void	cam_info(uint32 flags, char *fmt, ...);
void	cam_debug(uint32 when, char *myname, char *fmt, ...);
void	cam_print_sense(void (*prfcn)(), uint32 flags,
	                struct scsi_reqsns_data *snsdata);
int	cam_check_error(char *myname, char *msg, long rtn_status,
	                int unsigned cam_status, int unsigned scsi_status);
void	cam_fmt_ccb_header(long fcn_code, CAM_DEV devid, uint32 cam_flags,
	                   CCB *ccb);
void	cam_fmt_cdb6(long opcode, long lun, long length,
	             long control, struct scsi_cdb6 *cdb);
void	cam_fmt_cdb10(long opcode, long lun, uint32 lbaddr, long length,
	              long control, struct scsi_cdb10 *cdb);
long	unsigned cam_get_sg_xfer_len(CAM_SG_ELEM *sg_list, int sg_count);
void	cam_si32tohi32(unsigned char *si, uint32 *hi);
void	cam_si24tohi32(unsigned char *si, uint32 *hi);
void	cam_hi32tosi24(uint32 hi, unsigned char *si);
long	cam_cntuio(CCB *ccb);
long	cam_start_scsiio(CAM_DEV devid, CCB *ccb,
	                 void (*completion)(), uint32 cam_flags);
long	cam_start_rwio(struct cam_request *request, CCB **pccb);
void	cam_fmt_inquiry_ccb(CAM_DEV devid, struct scsi_inq_data *inq_data,
	                    CCB *ccb);
void	cam_fmt_mode_select_ccb(CAM_DEV devid, int pf, int sp, int prmlst_len,
	                        unsigned char *selbuf, CCB *ccb);
void	cam_fmt_mode_sense_ccb(CAM_DEV devid, int dbd, int pc, int pg_code,
	                       int alloc_len, unsigned char *snsbuf, CCB *ccb);
void	cam_fmt_reqsns_ccb(CAM_DEV devid, struct scsi_reqsns_data *reqsns_data,
	                    CCB *ccb);
void	cam_fmt_rdcap_ccb(CAM_DEV devid, struct scsi_rdcap_data *rdcap_data,
	                  CCB *ccb);
void	cam_fmt_tur_ccb(CAM_DEV devid, CCB *ccb);
void	cam_fmt_prevent_ccb(CAM_DEV devid, int prevent, CCB *ccb);
void	cam_fmt_rewind_ccb(CAM_DEV devid, CCB *ccb);
void	cam_fmt_wfm_ccb(CAM_DEV devid, int nfm, CCB *ccb);
void	cam_fmt_space_ccb(CAM_DEV devid, int code, long count, CCB *ccb);
void	cam_fmt_read_toc_ccb(CAM_DEV devid,
	                     struct scsi_read_toc_data *read_toc_data,
	                     int start, int alloc_length, CCB *ccb);
void	cam_fmt_pause_resume_ccb(CAM_DEV devid, int resume, CCB *ccb);
void	cam_fmt_play_audio_ccb12(CAM_DEV devid, int reladr, long lbaddr,
	                         long length, CCB *ccb);
void	cam_fmt_play_audio_tkindx_ccb(CAM_DEV devid, long start_track,
	                              long start_index, long end_track,
	                              long end_index, CCB *ccb);
long	cam_inquire(CAM_DEV devid, struct scsi_inq_data *inq_data,
                    char unsigned *cam_status, char unsigned *scsi_status);
long	cam_reqsns(CAM_DEV devid, struct scsi_reqsns_data *reqsns_data, 
                   char unsigned *cam_status, char unsigned *scsi_status);
long	cam_mode_select(CAM_DEV devid, int pf, int sp, int prmlst_len,
	                unsigned char *selbuf,
	                unsigned char *cam_status, unsigned char *scsi_status);
long	cam_mode_sense(CAM_DEV devid, int dbd, int pc, int pg_code,
	               int alloc_len, unsigned char *snsbuf,
	               unsigned char *cam_status, unsigned char *scsi_status);
long	cam_read_capacity(CAM_DEV devid, struct scsi_rdcap_data *rdcap_data,
	                  char unsigned *cam_status,
	                  char unsigned *scsi_status);
long	cam_tur(CAM_DEV devid, char unsigned *cam_status,
	        char unsigned *scsi_status);
long	cam_prevent(CAM_DEV devid, int prevent, char unsigned *cam_status,
	            char unsigned *scsi_status);
long	cam_rewind(CAM_DEV devid, char unsigned *cam_status,
	           char unsigned *scsi_status);
long	cam_wfm(CAM_DEV devid, int nfm, char unsigned *cam_status,
	        char unsigned *scsi_status);
long	cam_space(CAM_DEV devid, int code, long count,
	          char unsigned *cam_status, char unsigned *scsi_status,
	          struct scsi_reqsns_data *snsdata);
void	cam_iodone(struct cam_request *request);
void	cam_complete(CCB *ccb);
long	cam_ccb_wait(CCB *ccb);
bool	cam_get_msg(struct msg *msg);
long	cam_mk_sg_list(void *proto, int count,
	               CAM_SG_ELEM **sg_list, uint16 *sg_count);
uint32	cam_get_sgbcount(CAM_SG_ELEM *sg_list, uint16 sg_count);
void	sim_complete(struct sim_bus *bus_info, CCB *ccb);
long	sim_action(CCB *ccb, struct sim_bus *bus_info);
CCB	*sim_get_active_ccb(struct sim_bus *bus_info, int target, int tag);
void	*cam_alloc_mem(size_t size, void *ptr, unsigned int align);
void	cam_free_mem(void *ptr, unsigned int align);
long	cam_enable_io(int low, int high);
long	cam_enable_isr(int intr, long arg, void (*handler)());
long	cam_page_wire(void *va, void **pa, int *handle, int base16M);
long	cam_page_release(int handle);
void	cam_sleep(int sec);
void	cam_msleep(int ms);
void	cam_timer_thread(void), cam_proc_timer(void);
#else
long	xpt_init();
CCB	*xpt_ccb_alloc();
void	xpt_ccb_free();
long	xpt_action(), xpt_bus_register(), xpt_bus_deregister(), xpt_async();
void	cam_error(), cam_debug();
void	cam_fmt_ccb_header(), cam_fmt_cdb6();
void	cam_fmt_inquiry_ccb(), cam_fmt_rdcap(), cam_fmt_tur_ccb();
void	cam_fmt_rewind_ccb(), cam_fmt_wfm_ccb();
void	cam_fmt_read_toc_ccb();
long	cam_start_scsiio(), cam_inquire(), cam_tur();
long	cam_rewind(), cam_wfm();
void	cam_ccb_wait();
long	cam_mk_sg_list();
long	sim_action();
long	cam_alloc_mem();
void	cam_free_mem();
long	cam_enable_io(), cam_enable_isr(), cam_page_wire(), cam_page_release();
void	cam_msleep();
void	cam_timer_thread(), cam_proc_timer();
#endif

#endif		/*__CAM_H__*/

