/*
 * main.c - top level VSTa interface and peripheral driver code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <std.h>
#include <hash.h>
#include <syslog.h>
#include <sys/msg.h>
#include <sys/fs.h>
#include <sys/perm.h>
#include <mach/dpart.h>
#include <sys/syscall.h>
#include "cam.h"

struct	hash *cam_filehash;		/* Map session->context structure */
port_t	cam_port;			/* Port CAM receives contacts through */
static	port_name cam_port_name;	/*  ...its name */

/*
 * Top-level protection for WD hierarchy
 */
struct	prot cam_prot = {
	2,
	0,
	{1, 1},
	{ACC_READ, ACC_WRITE|ACC_CHMOD}
};

/*
 * Options (from the command line), limits, etc.
 */
struct	cam_params cam_params = { CAM_MAXIO };

/*
 * The peripheral device table.
 */
union	cam_pdevice *cam_pdevices = NULL, *cam_last_pdevice = NULL;

/*
 * Queue messages until they can be handled.
 */
struct	q_header cam_msgq;

/*
 * SCSI device type to SCSI device name. Note that since MO disks
 * behave in many ways like SCSI_DIRECT devices, they are also
 * named "sd".
 */
char	*cam_dev_names[] = {
	"sd",				/* SCSI_DIRECT */
	"st",				/* SCSI_SEQUENTIAL (tape) */
	"sp",				/* SCSI_PRINTER */
	"sh",				/* SCSI_PROCESSOR (host) */
	"sw",				/* SCSI_WORM */
	"sr",				/* SCSI_CDROM (read-only) */
	"ss",				/* SCSI_SCANNER */
	"sd",				/* SCSI_OPTICAL */
	"sm",				/* SCSI_MEDIUM_CHANGER */
	"sc"				/* SCSI_COMM */
};
static	int cam_ndev_names = sizeof(cam_dev_names) / sizeof(cam_dev_names[0]);

/*
 * Peripheral driver jump tables.
 */
extern	struct cam_pdev_ops pdisk_ops, pgen_ops;

/*
 * Function prototypes.
 */
#ifdef	__STDC__
int	namer_register(char *buf, port_name uport);
char	*perm_print(struct prot *prot);
int	__fd_alloc(port_t portnum);
void	pdev_parse_name(char *name, char **base, int *base_sz, long *unit,
	                char **type, int *type_sz, long *suffix);
void	pdisk_proc(msg_t *msg, struct cam_file *file);
void	pgen_proc(msg_t *msg, struct cam_file *file);
void	pdisk_read_capacity(union cam_pdevice *pdev);
void	pdisk_read_part_table(union cam_pdevice *pdev);
void	pgen_read_capacity(union cam_pdevice *pdev);
#endif

/*
 * pdev_init()
 *	Peripheral driver initialization.
 */
static	long pdev_init()
{
	union	cam_pdevice *pdev;
	struct	scsi_inq_data *inq_data;
	int	bus, target, lun, size;
	char	unsigned cam_status, scsi_status;
	CCB	ccb;
	extern	int cam_max_path_id;
	static	char *myname = "pdev_init";

	cam_fmt_ccb_header(XPT_GDEV_TYPE, 0, 0, &ccb);
	for(bus = 0; bus < cam_max_path_id; bus++) {
	    ccb.header.path_id = bus;
    	    for(target = 0; target < CAM_MAX_TARGET; target++) {
		ccb.header.target = target;
		for(lun = 0; lun < CAM_MAX_LUN; lun++) {
			ccb.header.lun = lun;
			(void)xpt_action(&ccb);
			if(ccb.header.cam_status != CAM_REQ_CMP)
				continue;
/*
 * Make sure the device type is within range.
 */
			if(ccb.getdev.dev_type >= cam_ndev_names) {
				cam_error(0, myname,
				   "device [%d/%d/%d] - type %d not recognized",
				   bus, target, lun, ccb.getdev.dev_type);
				continue;
			}
/*
 * Found something, update the peripheral device table.
 */
			size = sizeof(*pdev) + sizeof(struct prot);
			if((pdev = cam_alloc_mem(size, NULL, 0)) == NULL)
				return(CAM_ENOMEM);
			pdev->header.type = ccb.getdev.dev_type;
			pdev->header.devid = CAM_MKDEVID(bus, target, lun, 0);
			pdev->header.name = cam_dev_names[ccb.getdev.dev_type];
			pdev->header.osd = (void *)(pdev + 1);
			bcopy((char *)&cam_prot, pdev->header.osd,
			      sizeof(cam_prot));
			pdev->header.next = NULL;
/*
 * Update the peripheral device table.
 */
			if(cam_pdevices == NULL)
				cam_pdevices = cam_last_pdevice = pdev;
			else {
				cam_last_pdevice->header.next = pdev;
				cam_last_pdevice = pdev;
			}
/*
 * Test for unit ready.
 */
			(void)cam_tur(pdev->header.devid,
			              &cam_status, &scsi_status);
/*
 * Handle device specific initialization.
 */
			switch(pdev->header.type) {
			case SCSI_DIRECT:
			case SCSI_CDROM:
			case SCSI_WORM:
			case SCSI_OPTICAL:
				pdev->header.class = CAMPC_DISK;
/*
 * Find out if the disk's media is removable.
 */
				inq_data = (struct scsi_inq_data *)
				                     ccb.getdev.inquiry_data;
				pdev->disk.removable = inq_data->rmb;
/*
 * Get the logical block size and number of blocks.
 */
				pdisk_read_capacity(pdev);
/*
 * Read in the partition table.
 */
				pdisk_read_part_table(pdev);
				break;
			}
		}
	    }
	}
	return(CAM_SUCCESS);
}

/*
 * pdev_open
 *	Top level open code.
 */
static	long pdev_open(msg_t *msg, struct cam_file *file)
{
	char	*name = msg->m_buf, *base;
	union	cam_pdevice *pdev;
	long	unit;
	int	base_sz, type_sz;
	int	bus, target, lun;
	CAM_DEV devid;
/*
 * Open is normally called after a connect.
 */
	if(file->devid != CAM_ROOTDIR)
		return(CAM_EINVAL);
/*
 * Extract the various device name components.
 * Check the first part of the device name.
 */
	pdev_parse_name(name, &base, &base_sz, &unit, NULL, &type_sz, NULL);
	if(strlen(name) < 3)
		return(CAM_ENOENT);
/*
 * Convert the unit number to bus, target, lun.
 */
	bus = unit / 10;
	target = unit % 10;
	lun = 0;
/*
 * Grab peripheral device pointer.
 */
	devid = CAM_MKDEVID(bus, target, lun, 0);
	for(pdev = cam_pdevices; pdev != NULL; pdev = pdev->header.next)
		if(devid == pdev->header.devid)
			break;
	if(pdev == NULL)
		return(CAM_ENOENT);
/*
 * Check the device name.
 */
	if(strncmp(base, pdev->header.name, strlen(pdev->header.name)) != 0)
		return(CAM_ENOENT);
/*
 * Set up the initial file parameters.
 */
	file->pdev = pdev;
	file->pdev_ops = NULL;
	file->devid = CAM_MKDEVID(bus, target, lun, CAM_WHOLE_DISK);
	file->completion = cam_complete;
	return(CAM_SUCCESS);
}

/*
 * pdev_stat()
 *	Return a buffer containing 'stat' data for the root node of
 *	the input file.
 */
static	void pdev_stat(struct msg *msg, struct cam_file *file)
{
	struct prot *p;
	struct	part **parts;
	enum	cam_pdrv_classes class;
	uint	size, node, ptnidx, pextoffs, dev;
	CAM_DEV	devid;
	char	buf[MAXSTAT], type;
	static	char *myname = "pdev_stat";
	extern	int cam_max_path_id;

	if(file->pdev != NULL)
		class = file->pdev->header.class;
	else
		class = CAMPC_GEN;

	if((devid = file->devid) == CAM_ROOTDIR) {
		size = cam_max_path_id;		/* number of host adaptors */
		node = 0;
		type = 'd';
		pextoffs = 0;
		dev = cam_port_name;
	 	p = &cam_prot;
	} else if(class == CAMPC_DISK) {
		dev = devid;
		node = CAM_BUS(devid) * 1000 + CAM_TARGET(devid) * 100 +
		       CAM_LUN(devid) * 10 + CAM_PARTITION(devid);

		if((ptnidx = CAM_PARTITION(devid)) == CAM_WHOLE_DISK) {
			size = file->pdev->disk.nblocks;
		} else {
			parts = (struct part **)file->pdev->disk.partitions;
			if(parts[ptnidx] != NULL) {
				size = parts[ptnidx]->p_len;
 				pextoffs = parts[ptnidx]->p_extoffs;
			} else {
				size = 0;
 				pextoffs = 0;
			}
		}
		size *= file->pdev->disk.blklen;
		type = 's';
		p = (struct prot *)file->pdev->header.osd;
	} else {
		dev = devid;
		node = CAM_BUS(devid) * 1000 + CAM_TARGET(devid) * 100 +
		       CAM_LUN(devid) * 10 + CAM_PARTITION(devid);

		size = file->pdev->generic.nblocks * file->pdev->generic.blklen;
		type = 's';
		p = (struct prot *)file->pdev->header.osd;
	}

	sprintf(buf, "size=%d\ntype=%c\nowner=1/1\ninode=%d\npextoffs=%d\n"
	        "dev=%d\n", size, type, node, pextoffs, dev);
	strcat(buf, perm_print(p));
	CAM_DEBUG(CAM_DBG_MSG, myname, "stat = \"%s\"", buf);
	msg->m_buf = buf;
	msg->m_buflen = strlen(buf);
	msg->m_nseg = 1;
	msg->m_arg = msg->m_arg1 = 0;
	cam_msg_reply(msg, CAM_SUCCESS);
}

/*
 * add_name()
 *	Helper for pdev_unitdir(). Add another name to the input buffer.
 *	If there's not enough room extend the buffer.
 *
 * Returns 1 if buffer can't be extended, 0 on success.
 */
static	int add_name(char **buf, char *name, uint *len)
{
	uint	left, newlen, next;

	next = 0;
	if(*len > 0)
		next = strlen(*buf);
	left = *len - next;
	if(left < (strlen(name) + 2)) {
		if(*len == 0)
			*buf = NULL;
		newlen = *len + 128;
		if((*buf = cam_alloc_mem(newlen, (void *)*buf, 0)) == NULL) 
			return(1);
		*len = newlen;
		**buf = '\0';
	}
	strcat(&(*buf)[next], name);
	strcat(&(*buf)[next], "\n");
	return(0);
}

/*
 * pdev_unitdir
 *	Copy all the device names for the current unit into a buffer.
 *	The buffer is allocated locally and must be freed by the caller.
 *	The length parameter must be initialized to 0 before this function
 *	is called for the first unit.
 *
 * Returns CAM_SUCCESS on success and CAM_ENOMEM if the buffer can't be
 * extended.
 */
static	long pdev_unitdir(char *name, uint unit,
	                  struct part **parts, uint nparts,
	                  char **buffer, uint *length, uint *count)
{
	uint	ptidx;
/*
 * Scan partition table. Add valid partition names.
 */
	for (ptidx = 0; ptidx < nparts; ptidx++) {
		if(parts[ptidx] == NULL)
			continue;
		if(!parts[ptidx]->p_val)
			continue;
		if(add_name(buffer, parts[ptidx]->p_name, length))
			return(CAM_ENOMEM);
		(*count)++;
	}

	return(CAM_SUCCESS);
}

/*
 * pdev_readdir
 *	Find the device names for all peripheral devices.
 */
static	void pdev_readdir(msg_t *msg, struct cam_file *file)
{
	union	cam_pdevice *pdev;
	struct	part **parts;
	char	*buffer;
	uint	length, unit, count;
	long	status;
	char	tmp[32];

	length = 0;
	buffer = NULL;

	if(file->position == 0) {
	    for(pdev = cam_pdevices; pdev != NULL; pdev = pdev->header.next) {
/*
 * TODO: make bus and possibly lun part of "unit".
 */
		unit = CAM_TARGET(pdev->header.devid);
/*
 * Do peripheral device specific processing.
 */
		count = 0;
		status = CAM_SUCCESS;
		switch(pdev->header.type) {
		case SCSI_DIRECT:
			parts = (struct part **)pdev->disk.partitions;
			status = pdev_unitdir(pdev->header.name, unit, parts,
			                      MAX_PARTS, &buffer,
			                      &length, &count);
			break;
		default:
			sprintf(tmp, "%s%d", pdev->header.name, unit);
			if(add_name(&buffer, tmp, &length)) {
				status = CAM_ENOMEM;
				break;
			}
			count++;
		}

		if(status != CAM_SUCCESS)
			break;

		file->position += count;
	    }
	} else
	    file->position = 0;

	if(length == 0) {
/*
 * EOF.
 */
		msg->m_nseg = msg->m_arg = msg->m_arg1 = 0;
	} else {
/*
 * Send the result.
 */
		msg->m_buf = buffer;
		msg->m_arg = msg->m_buflen = length;
		msg->m_nseg = 1;
		msg->m_arg1 = 0;
	}
	cam_msg_reply(msg, CAM_SUCCESS);

	cam_free_mem(buffer, 0);
}

/*
 * pdev_rwio
 *	Read/write common code.
 */
void	pdev_rwio(msg_t *msg, struct cam_file *file)
{
	void	*sg_list;
	struct	cam_request *request;
	uint16	sg_count;
	seg_t	seg;
	long	status;
	int	cam_flags;
	static	char *myname = "pdev_rwio";

	if((msg->m_op == FS_READ) && (file->devid == CAM_ROOTDIR)) {
		cam_error(0, myname, "can't readdir from disk driver");
		status = CAM_EINVAL;
		cam_msg_reply(msg, status);
		return;
	}
/*
 * Need a peripheral device structure.
 */
	if(file->pdev == NULL) {
		cam_msg_reply(msg, CAM_ENXIO);
		return;
	}

	if(msg->m_nseg == 0) {
/*
 * No buffer provided. Allocate one here.
 */
		seg.s_buflen = msg->m_arg;
		seg.s_buf = cam_alloc_mem(seg.s_buflen, NULL, 0);
		if(seg.s_buf == NULL)
			status = CAM_ENOMEM;
		else
			status = cam_mk_sg_list((void *)&seg, 1,
			                    (CAM_SG_ELEM **)&sg_list,
			                     &sg_count);
	} else {
		status = cam_mk_sg_list((void *)msg->m_seg, msg->m_nseg,
		                        (CAM_SG_ELEM **)&sg_list,
	        	                &sg_count);
	}
	if(status != CAM_SUCCESS) {
		cam_msg_reply(msg, status);
		return;
	}
/*
 * Allocate a request structure.
 */
	request = cam_alloc_mem(sizeof(struct cam_request), NULL, 0);
	if(request == NULL) {
		cam_error(0, myname, "request allocation error");
		status = CAM_ENOMEM;
		cam_msg_reply(msg, status);
		return;
	}
/*
 * Fill in the request structure.
 */
	request->devid = file->devid;
	request->file = file;
	request->pdev = file->pdev;
	request->msg = *msg;

	cam_flags = (msg->m_op == FS_READ ? CAM_DIR_IN : CAM_DIR_OUT);
/*
 * Cam_mk_sg_list() built an array of buffer/length entries, so set the
 * CAM_SG_VALID flag, then start the I/O.
 */
	cam_flags |= CAM_SG_VALID;
	status = (*file->pdev_ops->rdwr)(request, cam_flags, sg_list, sg_count);
/*
 * Only reply if start I/O failed.
 */
	if(status != CAM_SUCCESS) {
		cam_msg_reply(msg, status);
/*
 * If not successful, free resources normally free'd by the completion
 * routine.
 */
		cam_free_mem(request, 0);
	}
}

/*
 * pdev_passthru
 *	Pass a copy of the client's CCB to the XPT layer. Wait for the
 *	result.
 */
static	void pdev_passthru(msg_t *msg, struct cam_file *file)
{
	CCB	*ccb, *uccb;
	void	*buffer;
	long	status = CAM_SUCCESS;
	int	buflen, wait_flag;
	static	char *myname = "cam_passthru";

	uccb = (CCB *)msg->m_seg[0].s_buf;
	if((ccb = xpt_ccb_alloc()) == NULL) {
		status = CAM_ENOMEM;
	} else if(msg->m_seg[0].s_buflen > sizeof(CCB)) {
		status = CAM_EINVAL;
	} else {
/*
 * Get a copy of the CCB. Get a pointer to the I/O buffer.
 */
		bcopy(uccb, ccb, msg->m_seg[0].s_buflen);
		buffer = NULL;
		if(msg->m_nseg > 1) {
			buffer = msg->m_seg[1].s_buf;
			buflen = msg->m_seg[1].s_buflen;
		} else {
			buffer = NULL;
			buflen = 0;
		}
/*
 * Do XPT function code specific processing.
 */
		switch(ccb->header.fcn_code) {
		case XPT_GDEV_TYPE:
			if(buffer == NULL)
				status = CAM_EINVAL;
			else {
				ccb->getdev.inquiry_data = buffer;
				wait_flag = FALSE;
			}
			break;
		case XPT_SCSI_IO:
			ccb->scsiio.sg_list = buffer;
			ccb->scsiio.xfer_len = buflen;
			wait_flag = TRUE;
			break;
		default:
			status = CAM_ENOENT;
		}
/*
 * If everything is OK so far, send down the CCB and wait for the operation
 * to complete, if necessary.
 */
		if(status == CAM_SUCCESS) {
	 		if((status = xpt_action(ccb)) != CAM_SUCCESS) {
 				status = CAM_EIO;
 			} else {
				if(wait_flag)
	 				cam_ccb_wait(ccb);
			}
 		}
/*
 * Post processing.
 */
		if(status == CAM_SUCCESS) {
		    switch(ccb->header.fcn_code) {
		    case XPT_GDEV_TYPE:
			break;
		    case XPT_SCSI_IO:
			uccb->header.cam_status = ccb->header.cam_status;
			uccb->scsiio.scsi_status = ccb->scsiio.scsi_status;
			break;
		    }
		}
	}
/*
 * Free the CCB, if necessary.
 */
	xpt_ccb_free(ccb);
/*
 * Send back the message replay.
 */
	cam_msg_reply(msg, status);
}

void	main(argc, argv)
int	argc;
char	**argv;
{
	char	**av;
	struct	cam_file *file;
	struct	cam_qmsg *qmsg;
	void	(*handler)();
	int	ac, irq, retry;
	long	status;
	struct	msg msg;
	static	char *myname = "cam";

/*
 * Initialize syslog.
 */
	openlog("scsi", LOG_PID, LOG_DAEMON);

	for(ac = 1, av = &argv[1]; ac < argc; ac++, av++) {
		if(strcmp(*av, "-d") == 0) {
			ac++; av++;
			if(sscanf(*av, "0x%x", &cam_debug_flags) != 1)
				(void)sscanf(*av, "%d", &cam_debug_flags);
		} else if(strcmp(*av, "-nobootbrst") == 0) {
			cam_params.nobootbrst = TRUE;
		} else
			syslog(LOG_ERR, "parameter %s not recognized", *av);
	}
/*
 * Allocate handle->file hash table.  8 is just a guess
 * as to what we'll have to handle.
 */
        cam_filehash = hash_alloc(8);
	if (cam_filehash == 0) {
		syslog(LOG_ERR, "file hash not allocated");
		exit(1);
        }
/*
 * Enable DMA for the current process.
 */
	if(enable_dma(0) < 0) {
		syslog(LOG_ERR, "SCSI DMA not enabled");
		exit(1);
	}
/*
 * Get a port for the CAM server.
 */
	cam_port = msg_port((port_name)0, &cam_port_name);
/*
 * Register the CAM driver.
 * Move entry out of disk and change name to XPT.
 */
	for(retry = 3; retry > 0; retry--) {
		if (namer_register("cam", cam_port_name) >= 0)
			break;
		cam_msleep(100);
	}
	if(retry <= 0) {
		cam_error(0, "CAM", "can't register name");
		exit(1);
	}
/*
 * Initialize the message queue.
 */
	CAM_INITQUE(&cam_msgq);
/*
 * Initialize XPT and SIM layers.
 * Move to peripheral driver's.
 */
	if(xpt_init() != CAM_SUCCESS) {
		perror("CAM XPT initialization error");
		exit(1);
	}
/*
 * Initialize the peripheral driver.
 */
	if(pdev_init() != CAM_SUCCESS) {
		perror("Peripheral driver initialization error");
		exit(1);
	}
/*
 * Start a timer thread.
 */
	tfork(cam_timer_thread, 0);
/*
 * Message receive loop.
 */
	for(;;) {
/*
 * If the message queue is not empty, get a message from the queue.
 * Otherwise, receive a message.
 */
		qmsg = NULL;
		if(!CAM_EMPTYQUE(&cam_msgq)) {
			qmsg = (struct cam_qmsg *)cam_msgq.q_forw;
			CAM_REMQUE(&qmsg->head);
			msg = qmsg->msg;
		} else {
			if(msg_receive(cam_port, &msg) < 0) {
				perror("CAM message receive error");
				continue;
			}
		}
		file = hash_lookup(cam_filehash, msg.m_sender);
		switch(msg.m_op) {
		case M_CONNECT:
			if(file != NULL)
				cam_msg_reply(&msg, CAM_EBUSY);
			else {
				file = new_client(&msg, cam_filehash,
				                  &cam_prot);
/*
 * Fill in the completion field in case I/O is done on the file handle
 * before it has been opened.
 */
				if(file != NULL) {
					file->completion = cam_complete;
				}
			}
			break;
		case M_DUP:		/* File handle dup during exec() */
			dup_client(&msg, cam_filehash, file);
			break;
		case M_ISR:
			irq = msg.m_arg;
			if((handler = cam_irq_table[irq].handler) != NULL)
				(*handler)(irq, 0);
			break;
		case CAM_TIMESTAMP:
			cam_proc_timer();
			break;
		case CAM_PASSTHRU:
			pdev_passthru(&msg, file);
			break;
		case FS_STAT:
			pdev_stat(&msg, file);
			break;
		case FS_WSTAT:		/* Writes stats */
			cam_msg_reply(&msg, CAM_EINVAL);
			break;
		case FS_OPEN:
			if((status = pdev_open(&msg, file)) != CAM_SUCCESS) {
				cam_msg_reply(&msg, status);
				break;
			}
/*
 * Fill in the appropriate jump table.
 */
			switch(file->pdev->header.class) {
			case CAMPC_DISK:
				file->pdev_ops = &pdisk_ops;
				break;
			default:
				file->pdev_ops = &pgen_ops;
				break;
			}

			status = (*file->pdev_ops->open)(file, msg.m_buf);
			if(status == CAM_SUCCESS)
				msg.m_arg = msg.m_arg1 = 0;
			cam_msg_reply(&msg, status);
			break;
		case FS_ABSREAD:	/* Set position, then read */
		case FS_ABSWRITE:	/* Set position, then write */
			if((file == NULL) || (msg.m_arg1 < 0)) {
				cam_msg_reply(&msg, CAM_EINVAL);
				break;
			}
			file->position = msg.m_arg1;
			msg.m_op = ((msg.m_op == FS_ABSREAD) ?
			             FS_READ : FS_WRITE);

			/* VVV fall into VVV */

		case FS_WRITE:
		case FS_READ:
/*
 * Filter out READDIR's.
 */
			if((msg.m_op == FS_READ) &&
			   (file->devid == CAM_ROOTDIR)) {
				pdev_readdir(&msg, file);
				break;
			}

			pdev_rwio(&msg, file);
			break;

		case FS_SEEK:
			if((file == NULL) || (msg.m_arg < 0))
				status = CAM_EINVAL;
			else {
				file->position = msg.m_arg;
				status = CAM_SUCCESS;
			}
			cam_msg_reply(&msg, status);
			break;

		case M_DISCONNECT:	/* Client done */
			status = CAM_SUCCESS;
			if((file->pdev_ops != NULL) &&
			   !(file->flags & CAM_FILE_COPY))
				status = (*file->pdev_ops->close)(file);
			break;

		default:
			cam_error(0, "main", "unknown operation");
			cam_msg_reply(&msg, CAM_EINVAL);
		}
/*
 * Free file info on disconnect.
 */
		if(msg.m_op == M_DISCONNECT)
			dead_client(&msg, cam_filehash, file);
/*
 * Free the queue'd message, if necessary.
 */
		if(qmsg != NULL)
			cam_free_mem(qmsg, 0);
	}

	exit(0);
}

