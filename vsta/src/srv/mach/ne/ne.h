#ifndef _NE_H
#define _NE_H
/*
 * ne.h
 *	Novell NE2000 Ethernet adapter definitions
 */
#include <sys/types.h>
#include <sys/perm.h>
#include <llist.h>
#include "if_ether.h"

#define NE_RANGE	0x1f	/* range of addresses used be adapter */

#define NNE		1	/* Max # NE2000 units supported */
#define NCONNECTS	16	/* Max # connections supported */

#define ne_data		0x10	/* Data Transfer port */
#define ne_reset	0x1f	/* Card Reset port */

#define	PKTSZ	3*512
#define	TBUF	(16*1024)	/* Starting location of Transmit Buffer */
#define	RBUF	(16*1024+PKTSZ)	/* Starting location of Receive Buffer */
#define	RBUFEND	(32*1024)	/* Ending location of Transmit Buffer */

/*
 * National Semiconductor DS8390 Ethernet Chip
 * Register and bit definitions
 */

/*
 * Page register offset values
 */
#define ds_cmd		0x00		/* Command register: 		*/
#define  DSCM_STOP	 0x01		/*	Stop controller		*/
#define  DSCM_START	 0x02		/*	Start controller	*/
#define  DSCM_TRANS	 0x04		/*	Transmit packet		*/
#define  DSCM_RREAD	 0x08		/*	Remote read 		*/
#define  DSCM_RWRITE	 0x10		/*	Remote write 		*/
#define  DSCM_NODMA	 0x20		/*	No Remote DMA present	*/
#define  DSCM_PG0	 0x00		/*	Select Page 0		*/
#define  DSCM_PG1	 0x40		/*	Select Page 1		*/
#define  DSCM_PG2	 0x80		/*	Select Page 2?		*/

#define ds0_pstart	0x01		/* Page Start register		*/
#define ds0_pstop	0x02		/* Page Stop register		*/
#define ds0_bnry	0x03		/* Boundary Pointer		*/

#define ds0_tsr		0x04		/* Transmit Status (read-only)	*/
#define	 DSTS_PTX	 0x01		/*  Successful packet transmit  */ 
#define	 DSTS_COLL	 0x04		/*  Packet transmit w/ collision*/ 
#define	 DSTS_COLL16	 0x04		/*  Packet had >16 collisions & fail */ 
#define	 DSTS_UND	 0x20		/*  FIFO Underrun on transmission*/ 

#define ds0_tpsr	ds0_tsr		/* Transmit Page (write-only)	*/
#define ds0_tbcr0	0x05		/* Transmit Byte count, low  WO	*/
#define ds0_tbcr1	0x06		/* Transmit Byte count, high WO	*/

#define ds0_isr		0x07		/* Interrupt status register	*/
#define	 DSIS_RX	 0x01		/*  Successful packet reception */ 
#define	 DSIS_TX	 0x02		/*  Successful packet transmission  */ 
#define	 DSIS_RXE	 0x04		/*  Packet reception  w/error   */ 
#define	 DSIS_TXE	 0x08		/*  Packet transmission  w/error*/ 
#define	 DSIS_ROVRN	 0x10		/*  Receiver overrun in the ring*/
#define	 DSIS_CTRS	 0x20		/*  Diagnostic counters need attn */
#define	 DSIS_RDC	 0x40		/*  Remote DMA Complete         */
#define	 DSIS_RESET	 0x80		/*  Reset Complete              */

#define ds0_rsar0	0x08		/* Remote start address low  WO	*/
#define ds0_rsar1	0x09		/* Remote start address high WO	*/
#define ds0_rbcr0	0x0A		/* Remote byte count low     WO	*/
#define ds0_rbcr1	0x0B		/* Remote byte count high    WO	*/

#define ds0_rsr		0x0C		/* Receive status            RO	*/
#define	 DSRS_RPC	 0x01		/*  Received Packet Complete    */

#define ds0_rcr		ds0_rsr		/* Receive configuration     WO */
#define  DSRC_SEP	 0x01		/* Save error packets		*/
#define  DSRC_AR	 0x02		/* Accept Runt packets		*/
#define  DSRC_AB	 0x04		/* Accept Broadcast packets	*/
#define  DSRC_AM	 0x08		/* Accept Multicast packets	*/
#define  DSRC_PRO	 0x10		/* Promiscuous physical		*/
#define  DSRC_MON	 0x20		/* Monitor mode			*/

#define ds0_tcr		0x0D		/* Transmit configuration    WO */
#define  DSTC_CRC	0x01		/* Inhibit CRC			*/
#define  DSTC_LB0	0x02		/* Encoded Loopback Control	*/
#define  DSTC_LB1	0x04		/* Encoded Loopback Control	*/
#define  DSTC_ATD	0x08		/* Auto Transmit Disable	*/
#define  DSTC_OFST	0x10		/* Collision Offset Enable	*/

#define ds0_rcvalctr	ds0_tcr		/* Receive alignment err ctr RO */

#define ds0_dcr		0x0E		/* Data configuration	     WO */
#define  DSDC_WTS	 0x01		/* Word Transfer Select		*/
#define  DSDC_BOS	 0x02		/* Byte Order Select		*/
#define  DSDC_LAS	 0x04		/* Long Address Select		*/
#define  DSDC_BMS	 0x08		/* Burst Mode Select		*/
#define  DSDC_AR	 0x10		/* Autoinitialize Remote	*/
#define  DSDC_FT0	 0x20		/* Fifo Threshold Select	*/
#define  DSDC_FT1	 0x40		/* Fifo Threshold Select	*/

#define ds0_rcvcrcctr	ds0_dcr		/* Receive CRC error counter RO */

#define ds0_imr		0x0F		/* Interrupt mask register   WO	*/
#define  DSIM_PRXE	 0x01		/*  Packet received enable	*/
#define  DSIM_PTXE	 0x02		/*  Packet transmitted enable	*/
#define  DSIM_RXEE	 0x04		/*  Receive error enable	*/
#define  DSIM_TXEE	 0x08		/*  Transmit error enable	*/
#define  DSIM_OVWE	 0x10		/*  Overwrite warning enable	*/
#define  DSIM_CNTE	 0x20		/*  Counter overflow enable	*/
#define  DSIM_RDCE	 0x40		/*  Dma complete enable		*/

#define ds0_rcvfrmctr	ds0_imr		/* Receive Frame error cntr  RO */


#define ds1_par0	ds0_pstart	/* Physical address register 0	*/
				/* Physical address registers 1-4 	*/
#define ds1_par5	ds0_tbcr1	/* Physical address register 5	*/
#define ds1_curr	ds0_isr		/* Current page (receive unit)  */
#define ds1_mar0	ds0_rsar0	/* Multicast address register 0	*/
				/* Multicast address registers 1-6 	*/
#define ds1_mar7	ds0_imr		/* Multicast address register 7	*/
#define ds1_curr	ds0_isr		/* Current page (receive unit)  */

#define DS_PGSIZE	256		/* Size of RAM pages in bytes	*/

/*
 * Packet receive header, 1 per each buffer page used in receive packet
 */
struct prhdr {
	uchar	pr_status;	/* is this a good packet, same as ds0_rsr */
	uchar	pr_nxtpg;	/* next page of packet or next packet */
	uchar	pr_sz0;
	uchar	pr_sz1;
};

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

#define PKT_BUFSIZE 2048
struct adapter {
	int a_base;	/* base I/O address */
	int a_irq;	/* interrupt */
	uchar a_addr[6];/* ethernet address */

	int a_page;	/* current page being filled */
	int a_ba;	/* byte addr in buffer ram of inc pkt */
	struct prhdr a_ph;	/* hardware header of incoming packet */
	struct ether_header a_eh;	/* header of incoming packet */
	char *a_pktbuf;	/* packet buffer for incoming packet */
};

extern int ne_send_up(char *, int, int);
extern void ne_rw(struct msg *, struct file *),
	ne_init(struct adapter *),
	ne_configure(struct adapter *),
	ne_isr(void),
	ne_readdir(struct msg *, struct file *),
	ne_stat(struct msg *, struct file *),
	ne_wstat(struct msg *, struct file *),
	ne_read(struct msg *, struct file *),
	ne_write(struct msg *, struct file *),
	ne_open(struct msg *, struct file *),
	ne_close(struct file *),
	rw_init(void),
	ne_start(struct adapter *, struct file *),
	run_queue(int);
extern struct adapter adapters[];
extern struct llist files;
extern int tx_busy[];
extern struct prot ne_prot;
extern ulong dropped;
extern struct llist readers, writers[];
extern void *pak_pool;

#endif /* _NE_H */
