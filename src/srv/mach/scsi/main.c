/*
 * main.c - top level VSTa interface and peripheral driver code.
 */
#include <stdio.h>
#include <stdlib.h>
#include <std.h>
#include <hash.h>
#include <syslog.h>
#include <ctype.h>
#include <sys/msg.h>
#include <sys/fs.h>
#include <sys/perm.h>
#include <mach/dpart.h>
#include <sys/syscall.h>
#include "cam.h"
#include "mtio.h"

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
struct	cam_params cam_params = {
	CAM_MAXIO,			/* max. transfer length per I/O */
	0,				/* no BUS RESET on boot */
	60				/* tape rewind max. (seconds) */
};

/*
 * The peripheral device table.
 */
union	cam_pdevice *cam_pdevices = NULL, *cam_last_pdevice = NULL;

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
extern	struct cam_pdev_ops pdisk_ops, ptape_ops, pgen_ops;

extern	struct	q_header cam_msgq;

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
			inq_data = (struct scsi_inq_data *)
				                     ccb.getdev.inquiry_data;
/*
 * Found something, update the peripheral device table.
 */
			size = sizeof(*pdev) + sizeof(struct prot);
			if((pdev = cam_alloc_mem(size, NULL, 0)) == NULL)
				return(CAM_ENOMEM);
			bzero((void *)pdev, size);
			pdev->header.type = ccb.getdev.dev_type;
			pdev->header.devid = CAM_MKDEVID(bus, target, lun, 0);
			pdev->header.name = cam_dev_names[ccb.getdev.dev_type];
			pdev->header.osd = (void *)(pdev + 1);
			bcopy((char *)&cam_prot, pdev->header.osd,
			      sizeof(cam_prot));
			pdev->header.next = NULL;
/*
 * Save the SCSI device information in the peripheral device header.
 */
			strncpy(pdev->header.vendor_id,
			        (char *)inq_data->vendor_id,
			        sizeof(inq_data->vendor_id));
			strncpy(pdev->header.prod_id,
			        (char *)inq_data->prod_id,
			        sizeof(inq_data->prod_id));
			strncpy(pdev->header.prod_rev,
			        (char *)inq_data->prod_rev,
			        sizeof(inq_data->prod_rev));
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
			case SCSI_SEQUENTIAL:
				pdev->header.class = CAMPC_TAPE;
				break;
			default:
				pdev->header.class = CAMPC_GEN;
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
	long	unit, index;
	int	base_sz, type_sz;
	int	bus, target, lun, flags = 0;
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
	index = 0;
	pdev_parse_name(name, &base, &base_sz, &unit, NULL, &type_sz, &index);
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
	if((pdev->header.type == SCSI_SEQUENTIAL) && (*base == 'n')) {
		flags |= CAM_NOREWIND_FLAG;
		base++;
		base_sz--;
	}
	if(strncmp(base, pdev->header.name, strlen(pdev->header.name)) != 0)
		return(CAM_ENOENT);
/*
 * Determine the device ID flags.
 */
	if(pdev->header.type == SCSI_SEQUENTIAL) {
		flags |= index;
	} else
		flags = CAM_WHOLE_DISK;
/*
 * Set up the initial file parameters.
 */
	file->pdev = pdev;
	file->pdev_ops = NULL;
	file->devid = CAM_MKDEVID(bus, target, lun, flags);
	file->completion = cam_complete;
	file->mode = msg->m_arg;
	return(CAM_SUCCESS);
}

/*
 * pdev_stat()
 *	Return a buffer containing 'stat' data for the root node of
 *	the input file.
 */
static	void pdev_stat(struct msg *msg, struct cam_file *file)
{
	struct	prot *p;
	struct	part **parts;
	union	cam_pdevice *pdev = file->pdev;
	enum	cam_pdrv_classes class;
	uint	size, node, ptnidx, pextoffs, dev;
	CAM_DEV	devid;
	char	buf[MAXSTAT], type;
	static	char *myname = "pdev_stat";
	extern	int cam_max_path_id;

	if(pdev != NULL)
		class = pdev->header.class;
	else
		class = CAMPC_GEN;

	if((devid = file->devid) == CAM_ROOTDIR) {
		size = cam_max_path_id;		/* number of host adaptors */
		node = 0;
		type = 'd';
		pextoffs = 0;
		dev = cam_port_name;
	 	p = &cam_prot;
	} else {
/*
 * Fill in the parameters common to all CAM devices.
 */
		dev = devid;
		node = CAM_BUS(devid) * 1000 + CAM_TARGET(devid) * 100 +
		       CAM_LUN(devid) * 10 + CAM_PARTITION(devid);
		size = 0;
		pextoffs = 0;
		p = (struct prot *)pdev->header.osd;
		type = 's';
/*
 * Fill in the device specific parameters.
 */
		if(class == CAMPC_DISK) {

			if((ptnidx = CAM_PARTITION(devid)) == CAM_WHOLE_DISK) {
				size = pdev->disk.nblocks;
			} else {
				parts = (struct part **)pdev->disk.partitions;
				if(parts[ptnidx] != NULL) {
					size = parts[ptnidx]->p_len;
 					pextoffs = parts[ptnidx]->p_extoffs;
				}
			}
			size *= pdev->header.blklen;
		} else if(class != CAMPC_TAPE) {
			size = pdev->generic.nblocks * pdev->header.blklen;
		}
	}

	sprintf(buf, "size=%d\ntype=%c\nowner=1/1\ninode=%d\npextoffs=%d\n"
	        "dev=%d\n", size, type, node, pextoffs, dev);
	strcat(buf, perm_print(p));
	if(pdev != NULL) {
/*
 * More device specific stuff.
 */
		sprintf(&buf[strlen(buf)], "id=%s %s %s\n",
		        pdev->header.vendor_id, pdev->header.prod_id,
		        pdev->header.prod_rev);
		if(class == CAMPC_TAPE) {
			sprintf(&buf[strlen(buf)],
			        "density=%d\nblklen=%d\ndevspc=%d\n",
			        pdev->tape.modes[CAM_FLAGS(devid)].density,
			        pdev->tape.modes[CAM_FLAGS(devid)].blklen,
			        pdev->tape.modes[CAM_FLAGS(devid)].devspc);
		}
	}
	CAM_DEBUG(CAM_DBG_MSG, myname, "stat = \"%s\"", buf);
	msg->m_buf = buf;
	msg->m_buflen = strlen(buf);
	msg->m_nseg = 1;
	msg->m_arg = msg->m_arg1 = 0;
	cam_msg_reply(msg, CAM_SUCCESS);
}

/*
 * pdev_wstat()
 *	Write state/control information.
 */
static	void pdev_wstat(struct msg *msg, struct cam_file *file)
{
	char	*cmd, *params, *end;
	int	cmdlen;
	long	status, op, cmdval, prmval;
	enum	cam_pdrv_classes class;
	union {
		struct	mtop mtop;
	} cmdargs;
	char	pnm[32];

	status = CAM_SUCCESS;
/*
 * Get a pointer to the command string.
 */
	cmd = end = msg->m_buf;
	while(isalpha(*end) || (isdigit(*end) && (end != cmd)) ||
	      (*end == '_'))
		end++;
	if((cmdlen = end - cmd) == 0) {
		cam_msg_reply(msg, CAM_EINVAL);
		return;
	}
/*
 * Get a pointer to the first parameter string.
 * Parse the parameter name and value fields.
 */
	*pnm = '\0';
	prmval = 0;
	if((params = strchr(end, '=')) != NULL) {
		do {
			params++;
		} while(isspace(*params));
		if(!isdigit(*params))
			(void)sscanf(params, "%s", pnm);
		if(sscanf(&params[strlen(pnm)], "0x%x", &prmval) != 1)
			(void)sscanf(&params[strlen(pnm)], "%d", &prmval);
	}

/*
 * Do device specific conversions.
 */
	if(file->pdev != NULL)
		class = file->pdev->header.class;
	else
		class = CAMPC_GEN;
	switch(class) {
	case CAMPC_TAPE:
		if(strncmp(cmd, "MTIOCTOP", cmdlen) == 0) {
			cmdval = MTIOCTOP;
			if(strlen(pnm) == 0)
				status = CAM_EINVAL;
			else if(strcmp(pnm, "MTWEOF") == 0)
				op = MTWEOF;
			else if(strcmp(pnm, "MTFSF") == 0)
				op = MTFSF;
			else if(strcmp(pnm, "MTBSF") == 0)
				op = MTBSF;
			else if(strcmp(pnm, "MTFSR") == 0)
				op = MTFSR;
			else if(strcmp(pnm, "MTBSR") == 0)
				op = MTBSR;
			else if(strcmp(pnm, "MTREW") == 0)
				op = MTREW;
			else if(strcmp(pnm, "MTOFFL") == 0)
				op = MTOFFL;
			else if(strcmp(pnm, "MTNOP") == 0)
				op  = MTNOP;
			else if(strcmp(pnm, "MTCACHE") == 0)
				op = MTCACHE;
			else if(strcmp(pnm, "MTNOCACHE") == 0)
				op = MTNOCACHE;
			else if(strcmp(pnm, "MTSETBSIZ") == 0)
				op = MTSETBSIZ;
			else if(strcmp(pnm, "MTSETDNSTY") == 0)
				op = MTSETDNSTY;
			else if(strcmp(pnm, "MTSETDRVBUFFER") == 0)
				op = MTSETDRVBUFFER;
			else
				status = CAM_EINVAL;

			cmdargs.mtop.mt_op = op;
			cmdargs.mtop.mt_count = prmval;
		} else
			status = CAM_EINVAL;

		break;
	default:
		status = CAM_EINVAL;
	}

	if(status == CAM_SUCCESS)
		status = (*file->pdev_ops->ioctl)(file, cmdval,
		                                  (void *)&cmdargs);

	cam_msg_reply(msg, status);
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
	char	*tempbuf;

	next = 0;
	if(*len > 0)
		next = strlen(*buf);
	left = *len - next;
	if(left < (strlen(name) + 2)) {
		if(*len == 0)
			*buf = NULL;
		newlen = *len + 128;
		if((tempbuf = cam_alloc_mem(newlen, (void *)*buf, 0)) == NULL) 
			return(1);
		*buf = tempbuf;
/*
 * If it's a new buffer, it needs to be zereoed out.
 */
		(*buf)[*len] = '\0'; 

		*len = newlen;
	}
	strcat(&(*buf)[next], name);
	strcat(&(*buf)[next], "\n");
	return(0);
}

/*
 * pdev_read_diskdir
 *	Copy all the device names for the current unit into a buffer.
 *	The buffer is allocated locally and must be freed by the caller.
 *	The length parameter must be initialized to 0 before this function
 *	is called for the first unit.
 *
 * Returns CAM_SUCCESS on success and CAM_ENOMEM if the buffer can't be
 * extended.
 */
static	long pdev_read_diskdir(char *name, uint unit,
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
 * pdev_read_tapedir
 *	Copy tape device names for the current unit into a buffer.
 *	The buffer is allocated locally and must be freed by the caller.
 *	The length parameter must be initialized to 0 before this function
 *	is called for the first unit.
 *
 * Returns CAM_SUCCESS on success and CAM_ENOMEM if the buffer can't be
 * extended.
 */
static	long pdev_read_tapedir(char *name, uint unit, char **buffer,
	                       uint *length, uint *count)
{
	int	i, j;
	char	tmpbuf[sizeof("nst9999a_XXX0") + 1];

	for(j = 0; j < 2; j++) {
		sprintf(tmpbuf, "%s%s%d", (j > 0 ? "n" : ""), name, unit);
		if(add_name(buffer, tmpbuf, length))
			return(CAM_ENOMEM);
		(*count)++;
	}
	for(i = 0; i < CAM_MAX_TAPE_MODES; i++) {
		for(j = 0; j < 2; j++) {
			sprintf(tmpbuf, "%s%s%d_%d", (j > 0 ? "n" : ""), name,
			        unit, i);
			if(add_name(buffer, tmpbuf, length))
				return(CAM_ENOMEM);
			(*count)++;
		}
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
		case SCSI_OPTICAL:
			parts = (struct part **)pdev->disk.partitions;
			status = pdev_read_diskdir(pdev->header.name, unit,
			                           parts, MAX_PARTS, &buffer,
			                           &length, &count);
			break;
		case SCSI_SEQUENTIAL:
			status = pdev_read_tapedir(pdev->header.name, unit,
			                           &buffer, &length, &count);
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
		msg->m_arg = msg->m_buflen = strlen(buffer);
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
 * Set up the CAM flags.
 * Cam_mk_sg_list() built an array of buffer/length entries, so set the
 * CAM_SG_VALID flag, then start the I/O.
 */
	cam_flags = (msg->m_op == FS_READ ? CAM_DIR_IN : CAM_DIR_OUT);
	cam_flags |= CAM_SG_VALID;
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
 * Fill in the request structure and start I/O.
 */
	request->devid = file->devid;
	request->file = file;
	request->pdev = file->pdev;
	request->status = CAM_SUCCESS;
	request->cam_flags = cam_flags;
	request->sg_list = sg_list;
	request->sg_count = sg_count;
	request->bcount = cam_get_sgbcount(sg_list, sg_count);
	request->bresid = request->bcount;
	request->msg = *msg;

	status = (*file->pdev_ops->rdwr)(request);
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
	int	buflen;

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
			else
				ccb->getdev.inquiry_data = buffer;
			break;
		case XPT_SCSI_IO:
			ccb->scsiio.sg_list = buffer;
			ccb->scsiio.xfer_len = buflen;
			break;
		default:
			status = CAM_ENOENT;
		}
/*
 * If everything is OK so far, send down the CCB and wait for the operation
 * to complete, if necessary.
 */
#ifdef	__xxxold__
		if(status == CAM_SUCCESS) {
	 		if((status = xpt_action(ccb)) != CAM_SUCCESS) {
 				status = CAM_EIO;
 			} else {
				if(wait_flag)
	 				cam_ccb_wait(ccb);
			}
 		}
#else
		if(status == CAM_SUCCESS) {
			if(ccb->header.fcn_code != XPT_SCSI_IO)
	 			status = xpt_action(ccb);
			else
{ if((status = xpt_action(ccb)) == CAM_SUCCESS) cam_ccb_wait(ccb); }
/*
				status = (*file->pdev_ops->ioctl)(file,
				                  CAM_EXEC_IOCCB,
		                                  (void *)ccb);
 */
			if(status != CAM_SUCCESS)
 				status = CAM_EIO;
 		}
#endif
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
	if(ccb != NULL)
		xpt_ccb_free(ccb);
/*
 * Send back the message replay.
 */
	cam_msg_reply(msg, status);
}

/*
 * cam_process_msg - process a VSTa message.
 */
void	cam_process_msg(struct msg *msg)
{
	struct	cam_file *file;
	void	(*handler)();
	long	status;
	int	irq;

	file = hash_lookup(cam_filehash, msg->m_sender);
	switch(msg->m_op) {
	case M_CONNECT:
		if(file != NULL)
			cam_msg_reply(msg, CAM_EBUSY);
		else {
			file = new_client(msg, cam_filehash, &cam_prot);
/*
 * Fill in the completion field in case I/O is done on the file handle
 * before it has been opened.
 * Default to the generic peripheral driver.
 */
			if(file != NULL) {
				file->completion = cam_complete;
				file->pdev_ops = &pgen_ops;
			}
		}
		break;
	case M_DUP:		/* File handle dup during exec() */
		dup_client(msg, cam_filehash, file);
		break;
	case M_ISR:
		irq = msg->m_arg;
		if((handler = cam_irq_table[irq].handler) != NULL)
			(*handler)(irq, 0);
		break;
	case CAM_TIMESTAMP:
		cam_proc_timer();
		break;
	case CAM_PASSTHRU:
		pdev_passthru(msg, file);
		break;
	case FS_STAT:
		pdev_stat(msg, file);
		break;
	case FS_WSTAT:		/* Writes stats */
		pdev_wstat(msg, file);
		break;
	case FS_OPEN:
		if((status = pdev_open(msg, file)) != CAM_SUCCESS) {
			cam_msg_reply(msg, status);
			break;
		}
/*
 * Fill in the appropriate jump table.
 */
		switch(file->pdev->header.class) {
		case CAMPC_DISK:
			file->pdev_ops = &pdisk_ops;
			break;
		case CAMPC_TAPE:
			file->pdev_ops = &ptape_ops;
			break;
		case CAMPC_GEN:
		default:
			file->pdev_ops = &pgen_ops;
			break;
		}

		status = (*file->pdev_ops->open)(file, msg->m_buf);
		if(status == CAM_SUCCESS) {
			file->flags |= CAM_OPEN_FILE;
			msg->m_arg = msg->m_arg1 = msg->m_nseg = 0;
		}
		cam_msg_reply(msg, status);
		break;
	case FS_ABSREAD:	/* Set position, then read */
	case FS_ABSWRITE:	/* Set position, then write */
		if((file == NULL) || (msg->m_arg1 < 0)) {
			cam_msg_reply(msg, CAM_EINVAL);
			break;
		}
		file->position = msg->m_arg1;
		msg->m_op = ((msg->m_op == FS_ABSREAD) ?  FS_READ : FS_WRITE);

		/* VVV fall into VVV */

	case FS_WRITE:
	case FS_READ:
/*
 * Filter out READDIR's.
 */
		if((msg->m_op == FS_READ) && (file->devid == CAM_ROOTDIR)) {
			pdev_readdir(msg, file);
			break;
		}

		pdev_rwio(msg, file);
		break;

	case FS_SEEK:
		if((file == NULL) || (msg->m_arg < 0))
			status = CAM_EINVAL;
		else {
			file->position = msg->m_arg;
			status = CAM_SUCCESS;
		}
		cam_msg_reply(msg, status);
		break;

	case M_DISCONNECT:	/* Client done */
		status = CAM_SUCCESS;
		if((file->pdev_ops != NULL) && (file->flags & CAM_OPEN_FILE)) {
			status = (*file->pdev_ops->close)(file);
			file->flags &= ~CAM_OPEN_FILE;
		}
		break;

	default:
		cam_error(0, "main", "unknown operation");
		cam_msg_reply(msg, CAM_EINVAL);
	}
/*
 * Free file info on disconnect.
 */
	if(msg->m_op == M_DISCONNECT)
		dead_client(msg, cam_filehash, file);
}

void	main(argc, argv)
int	argc;
char	**argv;
{
	char	**av;
	int	ac, retry;
	struct	msg msg;
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
		} else if(strcmp(*av, "-maxio") == 0) {
			ac++; av++;
			if(sscanf(*av, "0x%x", &cam_params.maxio) != 1)
				(void)sscanf(*av, "%d", &cam_params.maxio);
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
		cam_get_msg(&msg);
		cam_process_msg(&msg);
	}

	exit(0);
}

