/*
 * camvsta.h - VSTa CAM types and definitions.
 */

#ifndef	__CAMVSTA_H__
#define	__CAMVSTA_H__

#include <hash.h>
#include <sys/msg.h>
#include <sys/perm.h>
#include <mach/dpart.h>
#include "scsivsta.h"
#include "insque.h"

/*
 * CAM specific message operations.
 */
#define	CAM_MSGOP(_code)	(('C' << 8) | (_code))
#define	CAM_TIMESTAMP		CAM_MSGOP(0)
#define	CAM_PASSTHRU		CAM_MSGOP(1)

/*
 * CAM device number.
 */
typedef	unsigned long CAM_DEV;

#define	CAM_PATH_ID(_id)	(((_id) >> 14) & 0xff)
#define	CAM_BUS(_id)		CAM_PATH_ID(_id)
#define	CAM_TARGET(_id)		(((_id) >> 11) & 7)
#define	CAM_LUN(_id)		(((_id) >> 8) & 7)
#define	CAM_FLAGS(_id)		((_id) & 0xff)
#define	CAM_PARTITION(_id)	CAM_FLAGS(_id)
#define	CAM_MKDEVID(_bus, _tar, _lun, _data)				\
		((((_bus) & 0xff) << 14) | (((_tar) & 7) << 11) |	\
		 (((_lun) & 7) << 8) | ((_data) & 0xff))

#define	CAM_WHOLE_DISK		WHOLE_DISK

/*
 * Open file state. One instance per connection.
 */
struct	cam_file {
	CAM_DEV	devid;			/* CAM device number */
	uint	flags;			/* User access bits */
	uint	uperm;			/* permissions */
	long	unsigned position;	/* file position */
	void	(*completion)();	/* I/O completion function */
	union	cam_pdevice *pdev;	/* peripheral device pointer */
	struct	cam_pdev_ops *pdev_ops;	/* peripheral device functions */
};

/*
 * File flags.
 */
#define	CAM_FILE_COPY		1	/* M_DUP'ed file copy */

/*
 * Queue'd message structure.
 */
struct	cam_qmsg {
	struct	q_header head;
	msg_t	msg;
};

/*
 * CAM request structure.
 */
struct	cam_request {
	struct	q_header head;		/* request queue */
	struct	cam_file *file;		/* associated file structure */
	union	cam_pdevice *pdev;	/* peripheral device pointer */
	CAM_DEV	devid;			/* CAM device number */
	long	unsigned offset;	/* offset from beginning of device */
	msg_t	msg;			/* request message */
};

/*
 * Peripheral driver dispatch structure.
 */
struct	cam_pdev_ops {
#ifdef	__STDC__
	long (*open)(struct cam_file *file, char *name);
	long (*close)(struct cam_file *file);
	long (*rdwr)(struct cam_request *request, int cam_flags,
	             void *sg_list, uint16 sg_count);
#else
	long	(*open)();
	long	(*close)();
	long	(*rdwr)();
#endif
};

/*
 * Options (from the command line), limits, etc.
 */
struct	cam_params {
	long	maxio;			/* max. transfer length per I/O */
	int	nobootbrst;		/* no BUS RESET on boot */
};

/*
 * Connected-but-not-opened device ID.
 */
#define	CAM_ROOTDIR		-1

/*
 * Default sector (block) size.
 */
#define	CAM_BLKSIZ		512

/*
 * Static maximum I/O size.
 */
#define	CAM_MAXIO		4096

/*
 * Function return codes.
 */
enum	cam_rtn_status {
	CAM_SUCCESS		= 0,	/* all's well */
	CAM_EPERM,			/* permission denied */
	CAM_ESRCH,			/* no entry */
	CAM_EINVAL,			/* invalid argument */
	CAM_E2BIG,			/* too big */
	CAM_ENOMEM,			/* no memory */
	CAM_EBUSY,			/* busy */
	CAM_ENOSPC,			/* no space left on device */
	CAM_ENOTDIR,			/* not a directory */
	CAM_EEXIST,			/* already exists */
	CAM_EIO,			/* I/O error */
	CAM_ENXIO,			/* no io */
	CAM_ENMFILE,			/* no more files */
	CAM_ENOENT,			/* no entry */
	CAM_EBADF,			/* bad file */
	CAM_EBALIGN,			/* blk align */

	CAM_INTRMED_GOOD,		/* I/O will be completed later */
	CAM_NO_REPLY,			/* don't send a reply */
};

/*
 * CCB flags.
 */
#define	CAM_LOCAL_BUF		1

#ifndef	FALSE
#define	FALSE	0
#define	TRUE	(!FALSE)
#endif

/*
 * Avoids problems including <stddef.h>.
 */
#ifndef	NULL
#define	NULL	0
#endif

/*
 * Queue macros.
 */
#define	CAM_INSQUE(_elem, _pred)	insque(_elem, _pred)
#define	CAM_REMQUE(_elem)		remque(_elem)
#define	CAM_EMPTYQUE(_head)						\
		((_head)->q_forw == (struct q_header *)(_head))
#define	CAM_INITQUE(_head)						\
		((_head)->q_forw = (_head)->q_back = (struct q_header *)(_head))

/*
 * IRQ to IRQ-handler lookup.
 */
struct	cam_irq_entry {
	void	(*handler)();
	long	arg;
};

#define	CAM_NIRQ		16	/* maximum number of IRQ's */

/* IRQ to IRQ-handler lookup table */
extern	struct cam_irq_entry cam_irq_table[];

/*
 * Function prototypes.
 */
#ifdef	__STDC__
struct	cam_file *new_client(struct msg *m, struct hash *table,
	                     struct prot *prot);
void	dup_client(struct msg *m, struct hash *table, struct cam_file *fold);
void	dead_client(struct msg *m, struct hash *table, struct cam_file *f);
void	init_part(uint unit, struct part *parts, char *name,
	          char *secbuf, ulong *start_sect);

void	cam_msg_reply(msg_t *msg, long status);
void	pdisk_read_capacity(union cam_pdevice *pdev);
void	pdev_rwio(msg_t *msg, struct cam_file *file);
#endif	/*__STDC__*/

#endif	/*__CAMVSTA_H__*/

