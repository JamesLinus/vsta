/*
 * ne.c
 *	NE2000 Ethernet adapter
 *
 * This level of code assumes a single transfer is active at a time.
 */
#include <sys/types.h>
#include <sys/fs.h>
#include <sys/assert.h>
#include <sys/seg.h>
#include <sys/msg.h>
#include <mach/io.h>
#include <llist.h>
#include <time.h>
#include "ne.h"

#define ETHER_MIN_LEN	60	/* KLUDGE: should be defined elsewhere */
#define ETHER_MAX_LEN	1500	/* KLUDGE: should be defined elsewhere */

/*
 * nefetch()
 *	Fetch from onboard ROM/RAM
 */
static void
nefetch(struct adapter *ap, char *up, int ad, int len)
{
	int nec;
	uchar cmd;

	nec = ap->a_base;

	cmd = inportb(nec+ds_cmd);
	outportb(nec+ds_cmd, DSCM_NODMA|DSCM_PG0|DSCM_START);

	/* Setup remote dma */
	outportb(nec+ds0_isr, DSIS_RDC);
	outportb(nec+ds0_rbcr0, len);
	outportb(nec+ds0_rbcr1, len >> 8);
	outportb(nec+ds0_rsar0, ad);
	outportb(nec+ds0_rsar1, ad >> 8);

	/* Execute & extract from card */
	outportb(nec+ds_cmd, DSCM_RREAD|DSCM_PG0|DSCM_START);
	repinsw(nec+ne_data, up, len/2);

	/* get last byte if odd count */
	if (len & 1) {
		*(up+len-1) = inportb(nec+ne_data);
	}

	/* Wait till done, then shutdown feature */
	while ((inportb(nec+ds0_isr) & DSIS_RDC) == 0)
		;
	outportb(nec+ds0_isr, DSIS_RDC);
	outportb(nec+ds_cmd, cmd);
}

/*
 * ne_configure()
 *	Initialization of interface
 *
 * Set up initialization block and transmit/receive descriptor rings.
 */
void
ne_configure(struct adapter *ap)
{
	int i, nec;

	nec = ap->a_base;

	/*
	 * Steps from NS data sheets to put chip on active network
	 */
	/* 1 */
	outportb(nec+ds_cmd, DSCM_NODMA|DSCM_PG0|DSCM_STOP);

	/* 2 - Word Transfers, Burst Mode Select, Fifo at 8 bytes */
	outportb(nec+ds0_dcr, DSDC_WTS|DSDC_BMS|DSDC_FT1);

	/* 3 */
	outportb(nec+ds0_rbcr0, 0);
	outportb(nec+ds0_rbcr1, 0);

	/* 4 */
	outportb(nec+ds0_rcr, DSRC_MON);

	/* 5 */
	outportb(nec+ds0_tcr, DSTC_LB0);

	/* 6 */
	outportb(nec+ds0_pstart, RBUF/DS_PGSIZE);
	outportb(nec+ds0_pstop, RBUFEND/DS_PGSIZE);
	outportb(nec+ds0_bnry, RBUF/DS_PGSIZE);

	/* 7 - Clear any pending interrupts */
	outportb(nec+ds0_isr, 0xff);

	/* 8 - Allow interrupts */
	outportb(nec+ds0_imr, 0xff);

	/* 9 */
	outportb(nec+ds_cmd, DSCM_NODMA|DSCM_PG1|DSCM_STOP);

	/* set physical address on ethernet */
	for (i = 0; i < 6; i++)
		outportb(nec+ds1_par0+i, ap->a_addr[i]);

	/* clr logical address hash filter for now */
	for (i = 0; i < 8; i++)
		outportb(nec+ds1_mar0+i, 0xff);

	outportb(nec+ds1_curr, RBUF/DS_PGSIZE);

	/* 10 */
	outportb(nec+ds_cmd, DSCM_NODMA|DSCM_PG0|DSCM_START);

	/* 11 - Turn off loopback mode */
	outportb(nec+ds0_tcr, 0);

	/* done */

	/* Enable receive packet buffering for addressed and broadcast pkts */
	outportb(nec+ds0_rcr, DSRC_AB);

	ap->a_page = RBUF/DS_PGSIZE;
}

/*
 * ne_init()
 *	Initialize our Ethernet controller
 *
 * Initialize controller and read Ethernet Address from rom.
 * Card is not ready for transfers; ne_configure must be called to setup that.
 *
 */
void
ne_init(struct adapter *ap)
{
	int nec;
	int val,i;
	unsigned short boarddata[16];

	nec = ap->a_base;

	/* Reset the bastard */
	val = inportb(nec+ne_reset);
	__msleep(2000);	/* was DELAY(2000000) */
	outportb(nec+ne_reset,val);

	outportb(nec+ds_cmd, DSCM_STOP|DSCM_NODMA);
	
	i = 1000000;
	while ((inportb(nec+ds0_isr) & DSIS_RESET) == 0 && i-- > 0)
		;
	if (i < 0) {
		return;
	}

	outportb(nec+ds0_isr, 0xff);

	/* Word Transfers, Burst Mode Select, Fifo at 8 bytes */
	outportb(nec+ds0_dcr, DSDC_WTS|DSDC_BMS|DSDC_FT1);

	outportb(nec+ds_cmd, DSCM_NODMA|DSCM_PG0|DSCM_STOP);
	__msleep(100);	/* was DELAY(10000) */

	/* Check cmd reg and fail if not right */
	if ((i=inportb(nec+ds_cmd)) != (DSCM_NODMA|DSCM_PG0|DSCM_STOP))
		return;

	outportb(nec+ds0_tcr, 0);
	outportb(nec+ds0_rcr, DSRC_MON);
	outportb(nec+ds0_pstart, RBUF/DS_PGSIZE);
	outportb(nec+ds0_pstop, RBUFEND/DS_PGSIZE);
	outportb(nec+ds0_bnry, RBUF/DS_PGSIZE);
	outportb(nec+ds0_imr, 0);			/* no interrupts */
	outportb(nec+ds0_isr, 0);
	outportb(nec+ds_cmd, DSCM_NODMA|DSCM_PG1|DSCM_STOP);
	outportb(nec+ds1_curr, RBUF/DS_PGSIZE);
	outportb(nec+ds_cmd, DSCM_NODMA|DSCM_PG0|DSCM_STOP);

	/* Extract board address */
#if 0
	nefetch (ap, ap->a_addr, 0, 3);
#endif
	nefetch (ap, (char *)boarddata, 0, sizeof(boarddata));
	for (i=0; i < 6; i++) {
		ap->a_addr[i] = boarddata[i];
	}
}

/*
 * Setup output on interface.
 * Get another datagram to send off of the interface queue,
 * and map it to the interface before starting the output.
 */
void
ne_start(struct adapter *ap, struct file *f)
{
	struct msg *m = &f->f_msg;
	seg_t *segp;
	uchar cmd;
	ushort oddword;
	int seg, len, have_oddword = 0;
	register nec = ap->a_base;

	/*
	 * Get length of complete packet
	 */
	len = 0;
	for (seg = 0; seg < m->m_nseg; seg++) {
		len += m->m_seg[seg].s_buflen;
	}
	if (len & 1) {
		len++;		/* must be even */
	}

	cmd = inportb(nec+ds_cmd);		/* ??? */
	outportb(nec+ds_cmd, DSCM_NODMA|DSCM_PG0|DSCM_START);

	/* Setup for remote dma */
	outportb(nec+ds0_isr, DSIS_RDC);
	outportb(nec+ds0_rbcr0, len);
	outportb(nec+ds0_rbcr1, len >> 8);
	outportb(nec+ds0_rsar0, TBUF);
	outportb(nec+ds0_rsar1, TBUF >> 8);

	/* Execute & stuff to card */
	outportb(nec+ds_cmd, DSCM_RWRITE|DSCM_PG0|DSCM_START);

	/*
	 * Copy the remaining segments into the transmit buffer
	 * Watch out for odd-length segments.
	 */
	segp = 0;
	oddword = 0;
	for (seg = 0; seg < m->m_nseg; seg++) {
		segp = &m->m_seg[seg];

		if (have_oddword) {
			oddword |= *((char *)segp->s_buf);
			outportw(nec+ne_data, oddword);
			have_oddword = 0;
			segp->s_buf += 1;
			segp->s_buflen -= 1;
		}
		if (segp->s_buflen >= 2) {
			repoutsw(nec+ne_data, segp->s_buf, segp->s_buflen / 2);
		}
		if (segp->s_buflen & 1) {
			oddword |= (((char *)segp->s_buf)[segp->s_buflen-1] << 8);
			have_oddword = 1;
		}
	}
	if (segp && have_oddword) {
		oddword |= *((char *)segp->s_buf);
		outportw(nec+ne_data, oddword);
		have_oddword = 0;
	}

	/* Wait till done, then shutdown feature */
	while ((inportb(nec+ds0_isr) & DSIS_RDC) == 0)
		;
	outportb(nec+ds0_isr, DSIS_RDC);
	outportb(nec+ds_cmd, cmd);		/* ??? */

	/*
	 * Init transmit length registers, and set transmit start flag.
	 */
	if (len < ETHER_MIN_LEN) {
		len = ETHER_MIN_LEN;
	}
	outportb(nec+ds0_tbcr0, len & 0xff);
	outportb(nec+ds0_tbcr1, (len >> 8) & 0xff);
	outportb(nec+ds0_tpsr, TBUF / DS_PGSIZE);
	outportb(nec+ds_cmd, DSCM_TRANS|DSCM_NODMA|DSCM_START);
}

#define min(a,b)	((a) <= (b) ? (a) : (b))

/* buffer successor/predecessor in ring? */
#define succ(n) (((n) + 1 >= RBUFEND/DS_PGSIZE) ? RBUF/DS_PGSIZE : (n) + 1)
#define pred(n) (((n) - 1 < RBUF/DS_PGSIZE) ? RBUFEND/DS_PGSIZE - 1 : (n) - 1)

/*
 * nerecv()
 *	Ethernet interface receiver interface
 *
 * If input error just drop packet.
 * Otherwise examine packet to determine type.  If can't determine length
 * from type, then have to drop packet.  Othewise decapsulate
 * packet based on type and pass to type specific higher-level
 * input routine.
 */
void
nerecv(struct adapter *ap)
{
	int len;

	/* ATTN: count packet */
	len = ap->a_ph.pr_sz0 + (ap->a_ph.pr_sz1 << 8);
	if(len < ETHER_MIN_LEN || len > ETHER_MAX_LEN)
		return;

	/* this need not be so torturous - one/two bcopys at most into mbufs */
	nefetch(ap, ap->a_pktbuf, ap->a_ba,
		min(len, DS_PGSIZE - sizeof(ap->a_ph)));
	if (len > DS_PGSIZE - sizeof(ap->a_ph)) {
		int l = len - (DS_PGSIZE - sizeof(ap->a_ph)), b;
		char *p = ap->a_pktbuf + (DS_PGSIZE - sizeof(ap->a_ph));

		ap->a_page = succ(ap->a_page);
		b = ap->a_page*DS_PGSIZE;
		
		while (l >= DS_PGSIZE) {
			nefetch(ap, p, b, DS_PGSIZE);
			p += DS_PGSIZE;
			l -= DS_PGSIZE;
			ap->a_page = succ(ap->a_page);
			b = ap->a_page*DS_PGSIZE;
		}
		if (l > 0) {
			nefetch(ap, p, b, l);
		}
	}

	/* don't forget checksum! */
	/* len -= sizeof(struct ether_header) + sizeof(long); */
	ne_send_up(ap->a_pktbuf, len);
}

/*
 * ne_isr()
 *	Controller interrupt
 */
void
ne_isr(void)
{
	int unit = 0;				/* KLUDGE: hardcoded */
	struct adapter *ap = &adapters[unit];
	uchar cmd, isr;
	register nec = ap->a_base;

	/* Save cmd, clear interrupt */
	cmd = inportb(nec+ds_cmd);
loop:
	isr = inportb(nec+ds0_isr);
	outportb(nec+ds_cmd,DSCM_NODMA|DSCM_START);
	outportb(nec+ds0_isr, isr);

	/* Receiver error */
	if (isr & DSIS_RXE) {
		/* need to read these registers to clear status */
		(void) inportb(nec + ds0_rsr);
		(void) inportb(nec + 0xD);
		(void) inportb(nec + 0xE);
		(void) inportb(nec + 0xF);
	}

	/* Receiver ovverun? */
	if (isr & DSIS_ROVRN) {
		/*
		 * Recover from overrun - steps from datasheet
		 */

		/* 1 */
		outportb(nec+ds_cmd, DSCM_NODMA|DSCM_STOP);
		/* 2 */
		outportb(nec+ds0_rbcr0, 0);
		outportb(nec+ds0_rbcr1, 0);
		/* 3 */
		while ((inportb(nec+ds0_isr)&DSIS_RESET) == 0)
			;
		/* 4 */
		outportb(nec+ds0_tcr, DSTC_LB0);
		/* 5 */
		outportb(nec+ds_cmd, DSCM_START|DSCM_NODMA);
		/* 6 - Remove packet(s) */
	}

	/* We received something; rummage thru tiny ring buffer */
	if (isr & (DSIS_RX|DSIS_RXE|DSIS_ROVRN)) {
		uchar pend, lastfree;

		outportb(nec+ds_cmd, DSCM_START|DSCM_NODMA|DSCM_PG1);
		pend = inportb(nec+ds1_curr);
		outportb(nec+ds_cmd, DSCM_START|DSCM_NODMA|DSCM_PG0);
		lastfree = inportb(nec+ds0_bnry);

		/* Have we wrapped? */
		if (lastfree >= RBUFEND/DS_PGSIZE)
			lastfree = RBUF/DS_PGSIZE;
		if (pend < lastfree && ap->a_page < pend) {
			lastfree = ap->a_page;
		} else if (ap->a_page > lastfree) {
			lastfree = ap->a_page;
		}

		/* Something in the buffer? */
		while (pend != lastfree) {
			uchar nxt;

			ap->a_ph.pr_status = 0;
			ap->a_ph.pr_nxtpg = 0;
			ap->a_ph.pr_sz0 = 0;
			ap->a_ph.pr_sz1 = 0;

			/* Extract header from microcephalic board */
			nefetch(ap, (char *)&ap->a_ph, lastfree*DS_PGSIZE,
				sizeof(ap->a_ph));
			ap->a_ba = lastfree*DS_PGSIZE+sizeof(ap->a_ph);

			/* Incipient paranoia */
			if (ap->a_ph.pr_status == DSRS_RPC
			    || ap->a_ph.pr_status == 0x21 /* for dequna's */) {
				nerecv(ap);
			}
			nxt = ap->a_ph.pr_nxtpg;

			/* Sanity check */
			if ( nxt >= RBUF/DS_PGSIZE && nxt <= RBUFEND/DS_PGSIZE
				&& nxt <= pend) {
				ap->a_page = nxt;
			} else {
				ap->a_page = nxt = pend;
			}

			/* Set the boundaries */
			lastfree = nxt;
			outportb(nec+ds0_bnry, pred(nxt));
			outportb(nec+ds_cmd, DSCM_START|DSCM_NODMA|DSCM_PG1);
			pend = inportb(nec+ds1_curr);
			outportb(nec+ds_cmd, DSCM_START|DSCM_NODMA|DSCM_PG0);
		}
		outportb(nec+ds_cmd, DSCM_START|DSCM_NODMA);
	}

	/* Packet Transmitted or Transmit error */
	if (isr & (DSIS_TX|DSIS_TXE)) {
		tx_busy[unit] = 0;
		/* Need to read these registers to clear status */
		(void) inportb(nec+ds0_tbcr0);	/* collisions... */
#if 0
		if (isr & DSIS_TXE)
			ns->ns_if.if_oerrors++;
#endif
	}

	/* Receiver ovverun? */
	if (isr & DSIS_ROVRN) {
		/* 7 */
		outportb(nec+ds0_tcr, 0);
	}

	/* Any more to send? */
	outportb(nec+ds_cmd, DSCM_NODMA|DSCM_PG0|DSCM_START);
	if (tx_busy[unit] == 0)
		run_queue(unit);
	outportb(nec+ds_cmd, cmd);
	outportb(nec+ds0_imr, 0xff);

	/* Still more to do? */
	isr = inportb(nec+ds0_isr);
	if (isr)
		goto loop;
}
