#ifndef _PORT_H
#define _PORT_H
/*
 * port.h
 *	Internal structure for a communication port
 */
#include <sys/types.h>
#include <sys/mutex.h>
#include <sys/seg.h>

struct port {
	lock_t p_lock;		/* Mutex for modifying port */
	uchar p_flags;		/* See below */
	sema_t p_sema;		/* For serializing receivers */
	sema_t p_wait;		/* For sleeping to receive a message */
	sema_t p_mapsema;	/* Mutex for p_maps */
	struct hash		/* Map file-ID -> pset */
		*p_maps;
	struct sysmsg		/* FIFO list of messages */
		*p_hd,
		*p_tl;
	struct portref		/* Linked list of references to this port */
		*p_refs;
	port_name p_name;	/* Name of this port */
};

/*
 * Bits in p_flags
 */
#define P_CLOSING 1		/* Port is shutting down */
#define P_ISR 2			/* Port has an ISR vectored to it */

/*
 * Flag value for struct port's p_map fields indicating that we're not
 * allowing a map hash to be created any more.
 */
#define NO_MAP_HASH ((struct hash *)1)

/*
 * Per-connected-port structure.  The protocol offered through
 * such ports requires that we maintain a mutex.
 */
struct portref {
	sema_t p_sema;		/* Only one I/O through a port at a time */
	struct port *p_port;	/* The port we access */
	lock_t p_lock;		/* Master mutex */
	uchar p_state,		/* State & flags; see below */
		p_flags,
		p_dummy1;
	ulong p_refs;		/* # references */
	sema_t p_iowait;	/* Where proc sleeps for I/O */
	sema_t p_svwait;	/*  ...server, while client copies out */
	struct sysmsg		/* The message descriptor */
		*p_msg;
	struct portref		/* Linked list of refs to a port */
		*p_next,
		*p_prev;
	struct segref		/* Segments mapped from server's msg_receive */
		p_segs;
};

/*
 * Special value for a portref pointer to reserve a slot
 */
#define PORT_RESERVED ((struct portref *)1)

/*
 * Values for p_state
 */
#define PS_IOWAIT 1	/* I/O sent, waiting for completion */
#define PS_IODONE 2	/*  ...received */
#define PS_ABWAIT 3	/* M_ABORT sent, waiting for acknowledgement */
#define PS_ABDONE 4	/*  ...received */
#define PS_OPENING 5	/* M_CONNECT sent */
#define PS_CLOSING 6	/* M_DISCONNECT sent */

/*
 * Bits in p_flags
 */
#define PF_NODUP (0x1)	/* Don't allow duplicates (dup/fork/etc) */

#ifdef KERNEL
extern struct portref *dup_port(struct portref *);
extern ulong fork_ports(sema_t *, struct portref **, struct portref **, uint);
extern struct portref *alloc_portref(void);
extern void shut_client(struct portref *);
extern int shut_server(struct port *);
extern void free_portref(struct portref *);
extern int kernmsg_send(struct portref *, int, long *);
extern struct port *alloc_port(void);
extern void exec_cleanup(struct port *);
extern struct portref *find_portref(struct proc *, port_t),
	*delete_portref(struct proc *, port_t);
extern struct port *find_port(struct proc *, port_t),
	*delete_port(struct proc *, port_t);
extern void mmap_cleanup(struct port *);
extern void ref_port(struct port *, struct portref *),
	deref_port(struct port *, struct portref *);
#endif

#endif /* _PORT_H */
