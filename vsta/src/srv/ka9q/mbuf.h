#ifndef MBUF_H
#define MBUF_H

/* Basic message buffer structure */
struct mbuf {
	struct mbuf *next;	/* Links mbufs belonging to single packets */
	struct mbuf *anext;	/* Links packets on queues */
	char *data;		/* Active working pointers */
	int16 size;		/* Size of associated data buffer */
	int16 cnt;
};

#define	NULLBUF	(struct mbuf *)0
#define	NULLBUFP (struct mbuf **)0

extern void enqueue(struct mbuf **, struct mbuf *),
	hex_dump(), ascii_dump(),
	append(struct mbuf **, struct mbuf *);
extern struct mbuf *alloc_mbuf(int16),
	*free_mbuf(struct mbuf *),
	*dequeue(struct mbuf **),
	*copy_p(struct mbuf *, int16),
	*free_p(struct mbuf *),
	*qdata(char *, int16),
	*pushdown(struct mbuf *, int16);
int16 pullup(struct mbuf **, char *, int16),
	dup_p(struct mbuf **, struct mbuf *, int16, int16),
	len_mbuf(struct mbuf *),
	dqdata(struct mbuf *, char *, unsigned int),
	len_q(struct mbuf *);
int32 pull32(struct mbuf **);
int16 pull16(struct mbuf **);
char pullchar(struct mbuf **),
	*put16(char *, int16),
	*put32(char *, int32);
#define	AUDIT(bp)	audit(bp,__FILE__,__LINE__)

#endif /* MBUF_H */
