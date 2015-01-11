/*
 * main.c
 *	Main message handling
 */
#include <sys/msg.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <sys/namer.h>
#include <alloc.h>
#include <hash.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <fcntl.h>
#include <sys/assert.h>
#include <sys/syscall.h>
#include <mach/dma.h>
#include "rd.h"

static struct hash *filehash;	/* Map session->context structure */

/* array of disks, last one is reserved as  root */
struct node ramdisks[MAX_DISKS + 1];


port_t rdport;			/* Port we receive contacts through */
port_name rdport_name;		/* ... its name */
char rd_name[NAMESZ] = "disk/rd";
				/* Port namer name for this server */

/*
 * Default protection for ram drives:  anybody can read/write, sys
 * can chmod them.
 */
struct prot rd_prot = {
	1,
	ACC_READ | ACC_WRITE,
	{1},
	{ACC_CHMOD}
};


/*
 * new_client()
 *	Create new per-connect structure
 */
static void
new_client(struct msg *m)
{
	struct file *f;
	struct perm *perms;
	int uperms, nperms, desired;

	/*
	 * See if they're OK to access
	 */
	perms = (struct perm *)m->m_buf;
	nperms = (m->m_buflen)/sizeof(struct perm);
	uperms = perm_calc(perms, nperms, &rd_prot);
	desired = m->m_arg & (ACC_WRITE | ACC_READ | ACC_CHMOD);
	if ((uperms & desired) != desired) {
		msg_err(m->m_sender, EPERM);
		return;
	}

	/*
	 * Get data structure
	 */
	if ((f = malloc(sizeof(struct file))) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Fill in fields, start off at root
	 */
	
	bzero(f, sizeof(*f));
	f->f_flags = uperms;
	f->f_node = &ramdisks[MAX_DISKS];
	
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
 * dup_client()
 *	Duplicate current file access onto new session
 */
static void
dup_client(struct msg *m, struct file *fold)
{
	struct file *f;

	/*
	 * Get data structure
	 */
	if ((f = malloc(sizeof(struct file))) == 0) {
		msg_err(m->m_sender, strerror());
		return;
	}

	/*
	 * Fill in fields.  Simply duplicate old file.
	 */
	*f = *fold;

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
	m->m_arg = m->m_buflen = m->m_nseg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}


/*
 * dead_client()
 *	Someone has gone away.  Free their info.
 */
static void
dead_client(struct msg *m, struct file *f)
{
	(void)hash_delete(filehash, m->m_sender);
	free(f);
}


/*
 * rd_main()
 *	Endless loop to receive and serve requests
 */
static void
rd_main()
{
	struct msg msg;
	int x;
	struct file *f;
	char *buf = 0;

loop:
	/*
	 * Receive a message, log an error and then keep going
	 */
	x = msg_receive(rdport, &msg);
	if (x < 0) {
		syslog(LOG_ERR, "msg_receive");
		goto loop;
	}

	/*
	 * Must fit in one buffer.
	 */
	if (msg.m_nseg > 1) {
		msg_err(msg.m_sender, EINVAL);
		goto loop;
	}

	/*
	 * Categorize by basic message operation
	 */
	f = hash_lookup(filehash, msg.m_sender);
	msg.m_op &= MSG_MASK;
	switch (msg.m_op) {
	case M_CONNECT:		/* New client */
		new_client(&msg);
		break;
		
	case M_DISCONNECT:	/* Client done */
		dead_client(&msg, f);
		break;

	case M_DUP:		/* File handle dup during exec() */
		dup_client(&msg, f);
		break;

	case M_ABORT:		/* Aborted operation */
		/*
		 * Hunt down any active operation for this
		 * handle, and abort it.  Then answer with
		 * an abort acknowledge.
		 */
		msg_reply(msg.m_sender, &msg);
		break;

	case FS_SEEK:		/* Set position */
		if (!f || (msg.m_arg < 0)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		/* check that we don't seek past send */
		if((f->f_node->n_flags) & NODE_ROOT == 0) {
			if(msg.m_arg > (f->f_node->n_size - 1)) {
				msg_err(msg.m_sender, EINVAL);
				break;
			}
		}
		f->f_pos = msg.m_arg;
		msg.m_arg = msg.m_arg1 = msg.m_nseg = 0;
		(void)msg_reply(msg.m_sender, &msg);
		break;

	case FS_ABSREAD:	/* Set position, then read */
	case FS_ABSWRITE:	/* Set position, then write */
		if (!f || (msg.m_arg1 < 0)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		f->f_pos = msg.m_arg1;

		/* don't seek past end */
		if((f->f_node->n_flags) & NODE_ROOT == 0) {
			if(f->f_pos > (f->f_node->n_size - 1)) {
				f->f_pos = f->f_node->n_size - 1;
			}
		}
		msg.m_op = ((msg.m_op == FS_ABSREAD) ? FS_READ : FS_WRITE);
		/* VVV fall into VVV */

	case FS_READ:		/* Read the ramdisk */
	case FS_WRITE:		/* Write the ramdisk */
		rd_rw(&msg, f);
		break;

	case FS_STAT:		/* Get stat of file */
		rd_stat(&msg, f);
		break;

	case FS_WSTAT:		/* Writes stats */
		/* tbd: rd_wstat(&msg, f); */
		break;

	case FS_OPEN:		/* Move from dir down into drive */
		if (!valid_fname(msg.m_buf, x)) {
			msg_err(msg.m_sender, EINVAL);
			break;
		}
		rd_open(&msg, f);
		break;

	default:		/* Unknown */
		msg_err(msg.m_sender, EINVAL);
		break;
	}
	if (buf) {
		free(buf);
		buf = 0;
	}
	goto loop;
}


/*
 * parse_options()
 *	Parse the initial command line options
 *	size of ramdisk (defaults to 4MB)
 *	possible compressed image to load
 */
static void
parse_options(int argc, char **argv)
{
	int i,id,disksize;
	char *check, *check2;
	struct node *n;
				
	i = 1;
	while(i < argc) {
	  /* new ramdisk */
		if(!strncmp(argv[i],"ram",3)) {		
			
		  /* get N for ramN */
			id = (int)strtol(&argv[i][3], &check, 0);
			if(check == &argv[i][3] || *check != '=' ||
				id < 0 || id > (MAX_DISKS -1)) {
				fprintf(stderr,"rd: invalid device " \
					"'%s' - aborting\n",argv[i]);
				exit(1);
			}
			/* check if already in use */
			if(ramdisks[id].n_flags & NODE_VALID) {
				fprintf(stderr,"rd: duplicate device " \
				"'%s' - aborting\n",argv[i]);
				exit(1);
			}
			
			/* get size */
			check++;
			disksize = (int)strtol(check, &check2, 0);
			if(check2 == check || *check2 != '\0') {
				fprintf(stderr,"rd: invalid size " \
				"'%s' - aborting\n",check);
				exit(1);
			}
			
			/* create node for this disk */
			n = &ramdisks[id];
			n->n_flags |= NODE_VALID;
			n->n_size = disksize;
			n->n_buf = malloc(n->n_size);
			if(n->n_buf == NULL) {
				fprintf(stderr,"rd: unable to alloc ramdisk"\
					"  - aborting.\n");
				exit(1);
			}
			/* clear */
			bzero(n->n_buf,n->n_size);
			sprintf(n->n_name,"ram%d",id);
		}	
		else
		  if(!strncmp(argv[i],"load",4)) { 
		    /* load image into existing ramdisk */
			id = (int)strtol(&argv[i][4], &check, 0);
			if(check == &argv[i][4] || *check != '=' ||
				id < 0 || id > (MAX_DISKS -1)) {
				fprintf(stderr,"rd: invalid device for image " \
					"'%s' - aborting\n",argv[i]);
				exit(1);
			}
			/* check if already in use */
			if(ramdisks[id].n_flags & NODE_LOAD) {
				fprintf(stderr,"rd: duplicate image " \
				"'%s' - aborting\n",argv[i]);
				exit(1);
			}
			
			/* check points to path */
			check++;
			if(strlen(check) == 0) {
				fprintf(stderr,"rd: must specify image file" \
					" - aborting\n");
				exit(1);
			}
			n = &ramdisks[id];
			/* copy image file name, to be loaded later */
			strcpy(n->n_image,check);
			n->n_flags |= NODE_LOAD;
			
		}
		else {
			fprintf(stderr,"rd: unknown option '%s' - aborting\n",
				argv[i]);
			exit(1);
		}
		i++;	
	}
	
	/*
	 * now loop over array, load any images specified
	 *
	 */
	for(i = 0; i < MAX_DISKS; i++) {
		n = &ramdisks[i];
		if(n->n_flags & NODE_LOAD) {
			if((n->n_flags & NODE_VALID) == 0) {
				fprintf(stderr,"rd: load%d indicated, but "\
					"no ram drive created - aborting\n");
				exit(1);
			}
			
			/* ok, load this puppy up */
			rd_load(n);
		}	
	}
}


/*
 * main()
 *	Startup of the ramdisk server
 */
void
main(int argc, char **argv)
{
	struct node *n;

	/*
	 * Initialise syslog
	 */
	openlog("rd", LOG_PID, LOG_DAEMON);

	bzero(&ramdisks,(MAX_DISKS + 1)*sizeof(struct node));

	/* create root node */
	ramdisks[MAX_DISKS].n_flags = NODE_ROOT | NODE_VALID;
	strcpy(ramdisks[MAX_DISKS].n_name,"/");

	/*
	 * Get size, image to load, etc. and create
	 */
	parse_options(argc, argv);

	/*
	 * Allocate handle->file hash table.  8 is just a guess
	 * as to what we'll have to handle.
	 */
        filehash = hash_alloc(8);
	if (filehash == 0) {
		syslog(LOG_ERR, "file hash not allocated");
		exit(1);
        }

	/*
	 * Get a port for the ramdisk task
	 */
	rdport = msg_port((port_name)0, &rdport_name);

	/*
	 * Register as ram disk drives
	 */
	if (namer_register(rd_name, rdport_name) < 0) {
		syslog(LOG_ERR, "can't register name '%s'", rd_name);
		exit(1);
	}

	/*
	 * Init our data structures.
	 */
	/* rd_init(); */

	/*
	 * Start serving requests for the filesystem
	 */
	rd_main();
}
