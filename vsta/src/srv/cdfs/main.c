/*
 * main.c
 *	Main loop for message processing
 *	Adapted from dos/main.c
 */
#include <stdio.h>
#include <std.h>
#include <time.h>
#include <string.h>
#include <sys/fs.h>
#include <sys/perm.h>
#include <sys/namer.h>
#include <fcntl.h>
#include <hash.h>
#include <syslog.h>
#include "cdfs.h"

int	blkdev;				/* Device this FS is mounted upon */
port_t	rootport;			/* Port we receive contacts through */
static	struct hash *filehash;		/* Handle->filehandle mapping */

/*
uint	cdfs_debug_flags = CDFS_DBG_ALL;
 */
uint	cdfs_debug_flags = 0;

/*
 * Default protection for all CDFS files: everybody can read.
 */
static struct prot cdfs_default_prot = {
	2,
	ACC_READ | ACC_EXEC,
	{1,		1},
	{0,		0}
};

/*
 * Protection for CDFS server: everybody can read, only
 * group 1.1 (sys.sys) can write.
 */
static struct prot cdfs_server_prot = {
	2,
	ACC_READ | ACC_EXEC,
	{1,		1},
	{0,		ACC_WRITE}
};

/*
 * The CDFS incore "superblock".
 */
struct	cdfs cdfs;

/*
 * Function prototypes.
 */
extern port_t path_open(char *, int);
extern int __fd_alloc(port_t);
int	valid_fname(char *buf, int bufsize);
char	*perm_print(struct prot *prot);

/*
 * cdfs_init_file - initialize a CDFS file structure.
 */
void	cdfs_init_file(struct cdfs *cdfs, int perm, struct cdfs_file *file)
{
	memset(file, 0, sizeof(*file));
	file->cdfs = cdfs;
	file->node = cdfs->root_dir;
}

/*
 * cdfs_msg_reply
 *	Send a reply based on the input message and CDFS status.
 */
void	cdfs_msg_reply(msg_t *msg, long status)
{
	switch(status) {
	case CDFS_SUCCESS:
		msg_reply(msg->m_sender, msg);
		break;
	case CDFS_EPERM:
		msg_err(msg->m_sender, "permission denied");
		break;
	case CDFS_EIO:
		msg_err(msg->m_sender, "I/O error");
		break;
	case CDFS_ENOENT:
		msg_err(msg->m_sender, "no entry");
		break;
	case CDFS_ENOMEM:
		msg_err(msg->m_sender, "memory allocation error");
		break;
	case CDFS_ENOTDIR:
		msg_err(msg->m_sender, "not a directory");
		break;
	case CDFS_EINVAL:
		msg_err(msg->m_sender, "invalid argument");
		break;
	case CDFS_ROFS:
		msg_err(msg->m_sender, "read only file system");
		break;
	default:
		msg_err(msg->m_sender, "unknown error");
	}
}

/*
 * cdfs_blkdev_open
 *	Open the block device that supplies bytes to CDFS.
 */
static	int cdfs_blkdev_open(char *blkdev_path)
{
	port_t	port;
	int	fd, retries;
/*
 * If the block device is specified as a path, open it. Otherwise,
 * use path open to convert the server:device string to a port and
 * associate the port with a file descriptor.
 */
	if(strchr(blkdev_path, ':') == NULL) {
		fd = open(blkdev_path, O_RDONLY);
	} else {
		for(retries = 10; retries > 0; retries -= 1) {
			if((port = path_open(blkdev_path, ACC_READ)) < 0)
				sleep(1);
			else
				break;
		}
		if(port < 0) {
			cdfs_error(0, "CDFS", "can't connect to block device");
			return(-1);
		}
		fd = __fd_alloc(port);
	}

	return(fd);
}

/*
 * cdfs_seek()
 *	Set file position
 */
static	void cdfs_seek(struct msg *m, struct cdfs_file *f)
{
	if (m->m_arg < 0) {
		msg_err(m->m_sender, EINVAL);
		return;
	}
	f->position = m->m_arg;
	m->m_buflen = m->m_arg = m->m_arg1 = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * cdfs_connect() - create a new per-connect file structure.
 */
static	void cdfs_connect(struct msg *m)
{
	struct cdfs_file *f;
	struct perm *perms;
	int uperms, nperms;

	/*
	 * See if they're OK to access
	 */
	perms = (struct perm *)m->m_buf;
	nperms = (m->m_buflen)/sizeof(struct perm);
	uperms = perm_calc(perms, nperms, &cdfs_server_prot);
	if ((m->m_arg & ACC_WRITE) && !(uperms & ACC_WRITE)) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Get data structure
	 */
	if ((f = malloc(sizeof(struct cdfs_file))) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Fill in fields.  Note that our buffer is the
	 * information on the permissions our client
	 * possesses.  For an M_CONNECT, the message is
	 * from the kernel, and trusted.
	 */
	cdfs_init_file(&cdfs, uperms, f);

	/*
	 * Hash under the sender's handle
	 */
        if (hash_insert(filehash, m->m_sender, f)) {
		free(f);
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Return acceptance
	 */
	msg_accept(m->m_sender);
}

/*
 * cdfs_disconnect() - free resources, do close processing.
 * Called in response to a M_DISCONNECT message. Counter part
 * to M_CONNECT and M_DUP messages.
 */
static	void cdfs_disconnect(struct msg *m, struct cdfs_file *f)
{
	if(hash_delete(filehash, m->m_sender) == 0) {
		free(f);
	}
}

/*
 * cdfs_dup_client()
 *	Duplicate current file access onto new session
 *
 * This is more of a Plan9 clone operation.  The intent is
 * to not share a struct file, so that when you walk it down
 * a level or seek it, you don't affect the thing you cloned
 * off from.
 *
 * This is a kernel-generated message; the m_sender is the
 * current user; m_arg specifies a handle which will be used
 * if we complete the operation with success.
 */
static	void cdfs_dup_client(struct msg *m, struct cdfs_file *fold)
{
	struct cdfs_file *f;
	extern void iref();

	/*
	 * Get data structure
	 */
	if ((f = malloc(sizeof(struct cdfs_file))) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Fill in fields.  Note that our buffer is the
	 * information on the permissions our client
	 * possesses.  For an M_CONNECT, the message is
	 * from the kernel, and trusted.
	 */
	*f = *fold;
	f->flags |= CDFS_FILE_COPY;

	/*
	 * Hash under the sender's handle
	 */
        if (hash_insert(filehash, m->m_arg, f)) {
		free(f);
		msg_err(m->m_sender, ENOMEM);
		return;
	}

	/*
	 * Return acceptance
	 */
	m->m_arg = m->m_arg1 = m->m_buflen = m->m_nseg = 0;
	msg_reply(m->m_sender, m);
}

/*
 * cdfs_get_prot - convert ISO 9660 file attributes to VSTa protections.
 */
static	void cdfs_get_prot(struct cdfs_file *file, struct prot *vstprot)
{
	uchar	*vstacc;
	mode_t	mode;

	if(file->flags & CDFS_POSIX_ATTRS) {
		memset((void *)vstprot, 0, sizeof(struct prot));
		vstprot->prot_len = 2;
		mode = file->posix_attrs.mode;

		vstacc = &vstprot->prot_default;
		if(mode & S_IROTH) *vstacc |= ACC_READ;
		if(mode & S_IWOTH) *vstacc |= ACC_WRITE;
		if(mode & S_IXOTH) *vstacc |= ACC_EXEC;

		vstprot->prot_id[0] = file->posix_attrs.gid;
		vstacc = &vstprot->prot_bits[0];
		if(mode & S_IRGRP) *vstacc |= ACC_READ;
		if(mode & S_IWGRP) *vstacc |= ACC_WRITE;
		if(mode & S_IXGRP) *vstacc |= ACC_EXEC;

		vstprot->prot_id[1] = file->posix_attrs.uid;
		vstacc = &vstprot->prot_bits[1];
		if(mode & S_IRUSR) *vstacc |= ACC_READ;
		if(mode & S_IWUSR) *vstacc |= ACC_WRITE;
		if(mode & S_IXUSR) *vstacc |= ACC_EXEC;
	} else if(file->flags & CDFS_EXTND_ATTRS) {
		*vstprot = cdfs_default_prot;	/* XXX */
		vstprot->prot_id[0] = isonum_723(file->extnd_attrs.group);
		vstprot->prot_id[1] = isonum_723(file->extnd_attrs.owner);
	} else {
		*vstprot = cdfs_default_prot;
	}
}

/*
 * cdfs_stat()
 *	Build stat string for file, send back
 */
void	cdfs_stat(struct msg *msg, struct cdfs_file *file)
{
	struct	iso_directory_record *dp = &file->node;
	time_t	time;
	int	unsigned owner, group;
	char	result[MAXSTAT], file_type;
	struct	prot prot;
	static	char *myname = "cdfs_stat";
	static	char *fmt = "%ssize=%d\ntype=%c\n"
	                    "owner=%d/%d\ninode=%d\nmtime=%ld\n";

	CDFS_DEBUG_FCN_ENTRY(myname);
/*
 * Get the owner, group, and file type.
 */
	if(file->flags & CDFS_POSIX_ATTRS) {
		owner = file->posix_attrs.uid;
		group = file->posix_attrs.gid;
		file_type = ((file->posix_attrs.mode & S_IFDIR) ? 'd' : 'f');
	} else if(file->flags & CDFS_EXTND_ATTRS) {
		owner = isonum_723(file->extnd_attrs.owner);
		group = isonum_723(file->extnd_attrs.group);
	        file_type = ((dp->flags[0] & ISO_DIRECTORY) ? 'd' : 'f');
	} else {
		owner = cdfs_default_prot.prot_id[1];
		group = cdfs_default_prot.prot_id[0];
	        file_type = ((dp->flags[0] & ISO_DIRECTORY) ? 'd' : 'f');
	}
/*
 * Get the file protections and the date.
 */
	cdfs_get_prot(file, &prot);

	if(!cdfs_cvt_date((union iso_date *)dp->date, 7,
	                  file->cdfs->flags & CDFS_HIGH_SIERRA,
	                  1990, &time))
		time = 0;

	sprintf(result, fmt, perm_print(&prot), isonum_733(dp->size),
	        file_type, owner, group, isonum_733(file->node.extent), time);

	msg->m_buf = result;
	msg->m_buflen = strlen(result);
	msg->m_nseg = 1;
	msg->m_arg = msg->m_arg1 = 0;
	cdfs_msg_reply(msg, CDFS_SUCCESS);

	CDFS_DEBUG_FCN_EXIT(myname, CDFS_SUCCESS);
}

/*
 * cdfs_main()
 *	Endless loop to receive and serve requests
 */
static	void cdfs_main(char *blkdev_path)
{
	struct cdfs_file *f;
	char	*buffer, *buf2 = NULL;
	long	status, count;
	int	x;
	struct msg msg;

loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(rootport, &msg);
	if (x < 0) {
		perror("cdfs: msg_receive");
		goto loop;
	}

	/*
	 * If we've received more than a buffer of data, pull it in
	 * to a dynamic buffer.
	 */
	if (msg.m_nseg > 1) {
		buf2 = malloc(x);
		if (buf2 == 0) {
			msg_err(msg.m_sender, E2BIG);
			goto loop;
		}
		if (seg_copyin(msg.m_seg,
				msg.m_nseg, buf2, x) < 0) {
			msg_err(msg.m_sender, strerror());
			goto loop;
		}
		msg.m_buf = buf2;
		msg.m_buflen = x;
		msg.m_nseg = 1;
	}

	/*
	 * Categorize by basic message operation
	 */
	f = hash_lookup(filehash, msg.m_sender);
	switch (msg.m_op) {
	case M_CONNECT:		/* New client */
		/*
		 * Open the block device.
		 */
		if((blkdev = cdfs_blkdev_open(blkdev_path)) < 0) {
			cdfs_error(0, "CDFS", "can't open block device");
			msg_err(msg.m_sender, EINVAL);
		} else
		/*
		 * Read in the "super block", verify that its valid, etc.
		 */
		if(cdfs_mount(&cdfs) != CDFS_SUCCESS) {
			cdfs_error(0, "CDFS", "can't read super block");
			msg_err(msg.m_sender, EINVAL);
		} else {
			/*
			 * Create a file structure for this connection.
			 */
			cdfs_connect(&msg);
		}
		break;
	case M_DISCONNECT:	/* Client done */
		/*
		 * Close the block device and unmount the file system if
		 * this is the M_CONNECT'ed file.
		 * Free the file structure.
		 */
		if(!(f->flags & CDFS_FILE_COPY)) {
			close(blkdev);
			cdfs_unmount(&cdfs);
		}
		cdfs_disconnect(&msg, f);
		break;
	case M_DUP:		/* File handle dup during exec() */
		cdfs_dup_client(&msg, f);
		break;
	case M_ABORT:		/* Aborted operation */
		/*
		 * All operations are sync, so they're done
		 */
		msg_reply(msg.m_sender, &msg);
		break;
	case FS_OPEN:		/* Look up file from directory */
		if (!valid_fname(msg.m_buf, msg.m_buflen)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		status = cdfs_open(f, msg.m_buf, msg.m_arg);
		if(status == CDFS_SUCCESS) {
			msg.m_nseg = msg.m_arg1 = msg.m_arg = 0;
			f->cdfs = &cdfs;
		}
		cdfs_msg_reply(&msg, status);
		break;

	case FS_ABSREAD:	/* Set position, then read */
		if (msg.m_arg1 < 0) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		f->position = msg.m_arg1;
		/* VVV fall into VVV */
	case FS_READ:		/* Read file */
		count = msg.m_arg;
		if((buffer = malloc(count)) == NULL) {
			cdfs_msg_reply(&msg, CDFS_ENOMEM);
			break;
		}
		if(*f->node.flags & ISO_DIRECTORY)
			status = cdfs_read_dir(f, buffer, &count);
		else
			status = cdfs_read(f, buffer, &count);
		if(status == CDFS_SUCCESS) {
			msg.m_arg = msg.m_buflen = count;
			msg.m_buf = buffer;
			msg.m_nseg = (count ? 1 : 0);
			msg.m_arg1 = 0;
		}
		cdfs_msg_reply(&msg, status);

		free(buffer);

		break;

	case FS_SEEK:		/* Set new file position */
		cdfs_seek(&msg, f);
		break;
	case FS_STAT:		/* Tell about file */
		cdfs_stat(&msg, f);
		break;
	default:		/* Unknown */
	case FS_FID:		/* File ID */
	case FS_ABSWRITE:	/* Set position, then write */
	case FS_WRITE:		/* Write file */
	case FS_REMOVE:		/* Get rid of a file */
	case FS_RENAME:		/* Rename file/dir */
		msg_err(msg.m_sender, EINVAL);
		break;
	}

	/*
	 * Free dynamic storage if in use
	 */
	if (buf2) {
		free(buf2);
		buf2 = 0;
	}
	goto loop;
}

/*
 * usage()
 *	Tell how to use the thing
 */
static void
usage(void)
{
	printf("Usage is: cdfs -p <portpath> <fsname>\n");
	printf(" or: cdfs <filepath> <fsname>\n");
	exit(1);
}

/*
 * main()
 *	Startup of a boot filesystem
 *
 * A CDFS instance expects to start with a command line:
 *	$ cdfs <block class> <block instance> <filesystem name>
 */
int
main(int argc, char *argv[])
{
	port_name fsname;
	int	x;
	char	*namer_name = NULL, *blkdev_path = NULL;
	void	binit();

	/*
	 * Initialize syslog.
	 */
	openlog("cdfs", LOG_PID, LOG_DAEMON);

	/*
	 * Check arguments
	 */
	if (argc == 3) {
		blkdev_path = argv[1];
		namer_name = argv[2];
	} else if (argc == 4) {
		/*
		 * Version of invocation where service is specified
		 */
		if (strcmp(argv[1], "-p")) {
			usage();
		}
		blkdev_path = argv[2];
		namer_name = argv[3];
	} else {
		usage();
	}

	/*
	 * Allocate data structures we'll need
	 */
        filehash = hash_alloc(31);
	if (filehash == 0) {
		syslog(LOG_ERR, "file hash not allocated");
		exit(1);
        }

	/*
	 * Initialize the buffer cache.
	 */
	binit();

	/*
	 * Block device looks good.  Last check is that we can register
	 * with the given name.
	 */
	rootport = msg_port((port_name)0, &fsname);
	x = namer_register(namer_name, fsname);
	if (x < 0) {
		cdfs_error(0, "CDFS", "can't register name: %s", namer_name);
		exit(1);
	}

	/*
	 * Start serving requests for the filesystem
	 */
	cdfs_main(blkdev_path);
	return(0);
}

