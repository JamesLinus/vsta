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
	struct sysmsg		/* FIFO list of messages */
		*p_hd,
		*p_tl;
	sema_t p_sema;		/* For serializing receivers */
	sema_t p_wait;		/* For sleeping to receive a message */
	ushort p_flags;		/* See below */
	struct segref		/* Incoming messages mapped */
		p_segs;
	struct portref		/* Linked list of references to this port */
		*p_refs;
};

/*
 * Bits in p_flags
 */
#define P_ISR 2			/* Port has an ISR vectored to it */

/*
 * Per-connected-port structure.  The protocol offered through
 * such ports requires that we maintain a mutex.
 */
struct portref {
	sema_t p_sema;		/* Only one I/O through a port at a time */
	struct port *p_port;	/* The port we access */
	lock_t p_lock;		/* Master mutex */
	sema_t p_iowait;	/* Where proc sleeps for I/O */
	sema_t p_svwait;	/*  ...server, while client copies out */
	int p_state;		/* See below */
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

#ifdef KERNEL
extern struct portref *dup_port(struct portref *);
extern void fork_ports(struct portref **, struct portref **, uint);
extern struct portref *alloc_portref(void);
extern int shut_client(struct portref *), shut_server(struct port *);
extern void free_portref(struct portref *);
extern int kernmsg_send(struct portref *, int, long *);
#endif

#endif /* _PORT_H */
