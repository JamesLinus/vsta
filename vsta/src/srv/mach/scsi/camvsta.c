/*
 * camvsta.c - VSTa specific CAM code.
 */
#include <stdlib.h>
#include <sys/msg.h>
#include <sys/fs.h>
#include <sys/mman.h>
#include "cam.h"

/* IRQ to IRQ-handler lookup table */
struct	cam_irq_entry cam_irq_table[CAM_NIRQ];

extern	port_t cam_port;		/* Port CAM receives contacts through */

/*
 * Queue messages until they can be handled.
 */
struct	q_header cam_msgq;

/*
 * Function prototypes.
 */
#ifdef	__STDC__
int	enable_isr(port_t port, int irq);
int	enable_io(int low, int high);
int	page_wire(void *va, void **pa);
int	page_release(uint handle);
int	__msleep(uint msecs);
#endif

/*
 * Cam_alloc_mem - CAM memory allocator/reallocator.
 */
void	*cam_alloc_mem(size_t size, void *ptr, unsigned int align)
{
	void	*p;
	static	char *myname = "cam_alloc_mem";

	if(ptr != NULL) {
		if(align != 0) {
			cam_error(0, myname, "can't align reallocations");
			return(NULL);
		}
		if((p = (void *)realloc(ptr, size)) == NULL)
			return(NULL);
	} else if(align == 0) {
		if((p = (void *)malloc(size)) == NULL)
			return(NULL);
	} else if(align == NBPG) {
		if((p = (void *)mmap(0, btop(size + (NBPG - 1)),
		                     (PROT_READ | PROT_WRITE),
		                     MAP_ANON, 0, 0)) == NULL) {
			cam_error(CAM_PRINT_SYSERR, myname, "mmap error");
			return(NULL);
		}
	} else {
/*
 * Only alignment is page.
 */
		cam_error(0, myname, "invalid alignment request");
		return(NULL);
	}

	return(p);
}

/*
 * Cam_free_mem - free memory allocated by cam_alloc_mem().
 */
void	cam_free_mem(void *ptr, unsigned int align)
{
	if(align == 0)
		free(ptr);
	else
		munmap(ptr, 0);
}

/*
 * Cam_enable_io - enable I/O access for the input range of addresses.
 */
long	cam_enable_io(int low, int high)
{
	if(enable_io(low, high) < 0) {
		cam_error(CAM_PRINT_SYSERR, "cam_enable_io", "enable_io error");
		return(CAM_EIO);
	}
	return(CAM_SUCCESS);
}

/*
 * Cam_enable_isr - enable and set up an interrupt handler for the
 * input interrupt.
 */
long	cam_enable_isr(int intr, long arg, void (*handler)())
{
	static	char *myname = "cam_enable_isr";

	if(intr >= CAM_NIRQ) {
		cam_error(0, myname, "interrupt out of range");
		return(CAM_EINVAL);
	}

	CAM_DEBUG(CAM_DBG_MSG, myname, "enable_isr on level %d", intr);

	if(enable_isr(cam_port, intr) < 0) {
		cam_error(CAM_PRINT_SYSERR, myname, "enable isr error");
		return(CAM_EIO);
	}

	cam_irq_table[intr].handler = handler;
	cam_irq_table[intr].arg = arg;

	return(CAM_SUCCESS);
}

/*
 * Cam_page_wire - wire down the page associated with the input
 * virtual and return the corresponding physical address.
 */
long	cam_page_wire(void *va, void **pa, int *handle)
{
	static	char *myname = "cam_page_wire";

	if((*handle = page_wire(va, pa)) < 0) {
		cam_error(CAM_PRINT_SYSERR, myname, "page wire error");
		return(CAM_EIO);
	}
	return(CAM_SUCCESS);
}

/*
 * Cam_page_release - unwire the page slot associated w/ 'handle'.
 */
long	cam_page_release(int handle)
{
	static	char *myname = "cam_page_release";

	if(page_release(handle) < 0) {
		cam_error(CAM_PRINT_SYSERR, myname, "page release error");
		return(CAM_EIO);
	}
	return(CAM_SUCCESS);
}

/*
 * cam_sleep
 *	Wait for the specified number of seconds.
 */
void	cam_sleep(int seconds)
{
	sleep(seconds);
}

/*
 * cam_msleep
 *	Wait for the specified number of milli-seconds.
 */
void	cam_msleep(int msecs)
{
	__msleep(msecs);
}

/*
 * cam_iodone
 *	Request completion function.
 */
void	cam_iodone(struct cam_request *request)
{
	CAM_SG_ELEM *sg_list;
	msg_t	msg;
	static	char *myname = "cam_iodone";

	CAM_DEBUG_FCN_ENTRY(myname);

	sg_list = (CAM_SG_ELEM *)request->sg_list;
/*
 * Send the reply.
 */
	msg.m_sender = request->msg.m_sender;
	if(request->status == CAM_SUCCESS) {
/*
 * Determine the transfer count.
 */
		msg.m_arg = request->bcount - request->bresid;
/*
 * If the CAM server allocated the read buffer and if data was transfered,
 * fill in the read buffer parameters.
 */
		if((request->msg.m_nseg == 0) && (msg.m_arg != 0)) {
			msg.m_nseg = 1;
			msg.m_buf = (void *)sg_list->sg_address;
			msg.m_buflen = msg.m_arg;
		} else {
			msg.m_nseg = 0;
		}
		msg.m_arg1 = 0;
		cam_msg_reply(&msg, CAM_SUCCESS);
/*
 * Update the file position.
 */
		if(request->file != NULL)
			request->file->position += msg.m_arg;
	} else {
		cam_msg_reply(&msg, request->status);
	}
/*
 * Deallocate resources.
 */
	if(request->msg.m_nseg == 0)
		cam_free_mem((void *)sg_list->sg_address, 0);

	if(request->cam_flags & CAM_SG_VALID)
		cam_free_mem((void *)sg_list, 0);

	cam_free_mem(request, 0);

	cam_debug(CAM_DBG_FCN_EXIT, myname, "exitting");
}

/*
 * cam_complete
 *	CCB I/O completion function.
 */
void	cam_complete(register CCB *ccb)
{
	struct	cam_request *request;
	static	char *myname = "cam_complete";

	CAM_DEBUG_FCN_ENTRY(myname);

	if(ccb->header.fcn_code != XPT_SCSI_IO)
		cam_error(0, myname, "!XPT_SCSI_IO");

	request = (struct cam_request *)ccb->scsiio.reqmap;
	if((CAM_CCB_STATUS(ccb) != CAM_REQ_CMP) ||
	   (ccb->scsiio.scsi_status != SCSI_GOOD))
		request->status = CAM_EIO;

	cam_iodone(request);

	cam_debug(CAM_DBG_FCN_EXIT, myname, "exitting");
}

/*
 * Cam_ccb_wait - wait for the callback on the input CCB to complete.
 */
long	cam_ccb_wait(CCB *ccb)
{
	void	(*handler)();
	struct	cam_qmsg *qmsg;
	struct	msg msg;
	int	irq;
	static	char *myname = "cam_ccb_wait";

	do {
		if(msg_receive(cam_port, &msg) < 0) {
			perror("CAM message receive error");
			continue;
		}
		switch(msg.m_op) {
		case M_ISR:
			irq = msg.m_arg;
			if((handler = cam_irq_table[irq].handler) != NULL)
				(*handler)(irq, cam_irq_table[irq].arg);
			else
				cam_error(0, myname, "spurious interrupt (%d)",
				          irq);
			break;
		default:
/*
 * Can't process the message right now. Queue it and let the top level
 * code handle it later.
 */
			qmsg = cam_alloc_mem(sizeof(struct cam_qmsg), NULL, 0);
			if(qmsg == NULL) {
				cam_error(0, myname, "message queue error");
				break;
			}
			qmsg->msg = msg;
			CAM_INSQUE(&qmsg->head, cam_msgq.q_back);
		}
	} while(ccb->header.cam_status == CAM_REQ_INPROG);

	return(CAM_SUCCESS);
}

/*
 * cam_get_msg - get the next VSTa message. Look at the message
 * queue first. If the message queue is empty, read a message
 * from 'cam_port'.
 */
bool	cam_get_msg(struct msg *msg)
{
	struct	cam_qmsg *qmsg;
/*
 * If the message queue is not empty, get a message from the queue.
 * Otherwise, receive a message.
 */
	qmsg = NULL;
	if(!CAM_EMPTYQUE(&cam_msgq)) {
		qmsg = (struct cam_qmsg *)cam_msgq.q_forw;
		CAM_REMQUE(&qmsg->head);
		*msg = qmsg->msg;
		cam_free_mem(qmsg, 0);
	} else {
		if(msg_receive(cam_port, msg) < 0) {
			perror("CAM message receive error");
			return(FALSE);
		}
	}
	return(TRUE);
}

/*
 * cam_mk_sg_list()
 *	Build a CAM scatter/gather list.
 *
 * Fill in the input scatter/gather list and transfer length field from the
 * input OSD prototype. Allocate a CAM scatter/gather list, copy information
 * from 'proto' to the list, and set 'sg_count' to the number of entries.
 */
long	cam_mk_sg_list(void *proto, int count,
	               CAM_SG_ELEM **sg_list, uint16 *sg_count)
{
	seg_t	*m_seg = (seg_t *)proto;
	int	i;

	if(count == 0)
		count = 1;

	*sg_list = cam_alloc_mem(sizeof(CAM_SG_ELEM) * count, NULL, 0);
	if(*sg_list == NULL) {
		return(CAM_ENOMEM);
	}

	*sg_count = count;

	for(i = 0; i < count; i++, sg_list++) {
		(*sg_list)->sg_address = m_seg[i].s_buf;
		(*sg_list)->sg_length = (uint32)m_seg[i].s_buflen;
	}

	return(CAM_SUCCESS);
}

/*
 * cam_get_sgbcount - determine the total number of bytes associated
 * with the input Scatter/Gather list by summing the 'sg_length'
 * fields of each element.
 */
uint32	cam_get_sgbcount(CAM_SG_ELEM *sg_list, uint16 sg_count)
{
	uint32	bcount = 0;
	int	i;

	for(i = 0; i < sg_count; i++)
		bcount += sg_list[i].sg_length;
	return(bcount);
}

/*
 * cam_timer_thread
 *	Send a periodic timestamp message to the main server thread.
 */
void	cam_timer_thread()
{
	port_name pn;
	port_t	port;
	bool	error = FALSE;
	struct	msg msg;
	static	char *myname = "cam_timer_thread";
/*
 * Initialize the timestamp message.
 */
	msg.m_op = CAM_TIMESTAMP;
	msg.m_buf = NULL;
	msg.m_buflen = msg.m_nseg = msg.m_arg =	msg.m_arg1 = 0;
/*
 * Get a reference to CAM's port.
 */
	if((pn = namer_find("cam")) < 0) {
		cam_error(0, myname, "can't find CAM port name");
		error = TRUE;
	} else if((port = msg_connect(pn, ACC_READ)) < 0) {
		cam_error(0, myname, "can't connect to CAM port");
		error = TRUE;
	}
/*
 * Timer loop ...
 */
	while(error == FALSE) {
		sleep(1);
		if(msg_send(port, &msg) < 0) {
			cam_error(0, myname, "timestamp message send error");
			error = TRUE;
		}
	}
	cam_error(0, myname, "CAM timer disabled");
}

/*
 * cam_msg_reply
 *	Send a reply based on the input message and CAM status.
 */
void	cam_msg_reply(msg_t *msg, long status)
{
	int	reply_status;

	switch(status) {
	case CAM_SUCCESS:
		reply_status = msg_reply(msg->m_sender, msg);
		if(reply_status != 0)
			cam_error(0, "cam_msg_reply",  "msg_reply() error %d",
			          reply_status);
		break;
	case CAM_EPERM:
		msg_err(msg->m_sender, EPERM);
		break;
	case CAM_ESRCH:
		msg_err(msg->m_sender, ESRCH);
		break;
	case CAM_EINVAL:
		msg_err(msg->m_sender, EINVAL);
		break;
	case CAM_E2BIG:
		msg_err(msg->m_sender, E2BIG);
		break;
	case CAM_ENOMEM:
		msg_err(msg->m_sender, ENOMEM);
		break;
	case CAM_EBUSY:
		msg_err(msg->m_sender, EBUSY);
		break;
	case CAM_ENOSPC:
		msg_err(msg->m_sender, ENOSPC);
		break;
	case CAM_ENOTDIR:
		msg_err(msg->m_sender, ENOTDIR);
		break;
	case CAM_EEXIST:
		msg_err(msg->m_sender, EEXIST);
		break;
	case CAM_EIO:
		msg_err(msg->m_sender, EIO);
		break;
	case CAM_ENXIO:
		msg_err(msg->m_sender, ENXIO);
		break;
	case CAM_ENMFILE:
		msg_err(msg->m_sender, "no more dev");
		break;
	case CAM_ENOENT:
		msg_err(msg->m_sender, ENOENT);
		break;
	case CAM_EBALIGN:
		msg_err(msg->m_sender, EBALIGN);
		break;
	case CAM_EROFS:
		msg_err(msg->m_sender, EROFS);
		break;
	case CAM_INTRMED_GOOD:
		break;
	default:
		cam_error(0, "cam_msg_reply", "unknown status %d", status);
		msg_err(msg->m_sender, "unknown error");
	}
}

