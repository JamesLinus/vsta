#ifndef _EL3_H
#define _EL3_H
/*
 * el3.h
 *	3Com509 Ethernet adapter definitions
 */
#include <sys/types.h>
#include <sys/perm.h>
#include <llist.h>
#include <mach/asm.h>
#include "if_ether.h"

extern unsigned short htons (unsigned short);

#define EL3_RANGE	16	/* range of addresses used be adapter */

#define EL3_DATA 0x00
#define EL3_CMD 0x0E
#define EL3_STATUS 0x0E
#define EEPROM_READ 0x80

#define EL3WINDOW(win_num)	outportw (ioaddr + EL3_CMD, SelectWindow + (win_num))

#define TX_FIFO         0x00
#define RX_FIFO         0x00
#define RX_STATUS       0x08
#define TX_STATUS       0x0B
#define TX_FREE         0x0C            /* Remaining free bytes in Tx buffer. */
#define WN0_IRQ         0x08            /* Window 0: Set IRQ line in bits 12-15. */
#define WN4_MEDIA       0x0A            /* Window 4: Various transcvr/media bits */
#define MEDIA_TP       0x00C0          /* Enable link beat and jabber for 10baseT. */

#define NEL3		1	/* Max # NE2000 units supported */
#define NCONNECTS	16	/* Max # connections supported */

#define el3_data		0x10	/* Data Transfer port */
#define el3_reset	0x1f	/* Card Reset port */

enum c509cmd {
	TotalReset = 0<<11, SelectWindow = 1<<11, StartCoax = 2<<11,
	RxDisable = 3<<11, RxEnable = 4<<11, RxReset = 5<<11, RxDiscard = 8<<11,
	TxEnable = 9<<11, TxDisable = 10<<11, TxReset = 11<<11,
	FakeIntr = 12<<11, AckIntr = 13<<11, SetIntrEnb = 14<<11,
	SetStatusEnb = 15<<11, SetRxFilter = 16<<11, SetRxThreshold = 17<<11,
	SetTxThreshold = 18<<11, SetTxStart = 19<<11, StatsEnable = 21<<11,
	StatsDisable = 22<<11, StopCoax = 23<<11,};

enum c509status {
	IntLatch = 0x0001, AdapterFailure = 0x0002, TxComplete = 0x0004,
	TxAvailable = 0x0008, RxComplete = 0x0010, RxEarly = 0x0020,
	IntReq = 0x0040, StatsFull = 0x0080, CmdBusy = 0x1000, };

enum RxFilter {
	RxStation = 1, RxMulticast = 2, RxBroadcast = 4, RxProm = 8 };


#define	PKTSZ	3*512
#define	TBUF	(16*1024)	/* Starting location of Transmit Buffer */
#define	RBUF	(16*1024+PKTSZ)	/* Starting location of Receive Buffer */
#define	RBUFEND	(32*1024)	/* Ending location of Transmit Buffer */


/*
 * Structure of an attachment
 */
struct attach {
	struct llist *a_entry;	/* Link into list of attachments */
	struct prot a_prot;	/* Protection of attachment */
	ushort a_refs;		/* # references */
	ushort a_unit;		/* Unit # opened */
	uint a_owner;		/* Owner UID */
	ushort a_type;		/* ethernet type desired */
	ushort a_typeset;	/* 1 when type is set; incoming pkts ok */
};

/*
 * Our per-open-file data structure
 */
struct file {
	struct attach	/* Current attachment open */
		*f_file;
	struct perm	/* Things we're allowed to do */
		f_perms[PROCPERMS];
	uint f_nperm;
	uint f_perm;	/*  ...for the current f_file */
	struct msg	/* For writes, segments of data */
		f_msg;	/*  for reads, just reply addr & count */
	uint f_pos;	/* Only for directory reads */
	struct llist	/* Queued for I/O */
		*f_io;
};

/*
 * Values for f_node
 */
#define ROOTDIR (-1)				/* Root */
#define NODE_UNIT(n) ((n >> 4) & 0xFF)		/* Unit # */
#define NODE_CONNECT(n) (n & 0xF)		/* Connection in unit */
#define MKNODE(unit, connect) ((((unit) & 0xFF) << 4) | (connect))

struct prhdr {
	uchar pr_status;
	uchar pr_nxtpg;
	uchar pr_sz0;
	uchar pr_sz1;
};

#define PKT_BUFSIZE 2048
struct adapter {
	int a_base;	/* base I/O address */
	int a_irq;	/* interrupt */
	uchar a_addr[6];/* ethernet address */
	int a_if_port;/* interface type */

	int a_page;	/* current page being filled */
	int a_ba;	/* byte addr in buffer ram of inc pkt */
	struct prhdr a_ph;	/* hardware header of incoming packet */
	struct ether_header a_eh;	/* header of incoming packet */
	unsigned char *a_pktbuf;	/* packet buffer for incoming packet */
};

extern int el3_send_up(char *, int, int);
extern void el3_rw(struct msg *, struct file *),
	el3_init(struct adapter *),
	el3_configure(struct adapter *),
	el3_isr(void),
	el3_readdir(struct msg *, struct file *),
	el3_stat(struct msg *, struct file *),
	el3_wstat(struct msg *, struct file *),
	el3_read(struct msg *, struct file *),
	el3_write(struct msg *, struct file *),
	el3_open(struct msg *, struct file *),
	el3_close(struct file *),
	rw_init(void),
	el3_start(struct adapter *, struct file *),
	run_queue(int);
extern int el3_probe(struct adapter *);
extern void el3_close_adapter(struct adapter *);
extern struct adapter adapters[];
extern struct llist files;
extern int tx_busy[];
extern struct prot el3_prot;
extern ulong dropped;
extern struct llist readers, writers[];
extern void *pak_pool;

#endif /* _EL3_H */
