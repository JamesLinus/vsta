/*
 * el3.c
 *	3Com509 Etherlink III Ethernet adapter
 *	Version 0.1
 *	Author:	Kristoffer Branemyr <ztion@swipnet.se>
 *
 * This level of code assumes a single transfer is active at a time.
 */
#include <sys/types.h>
#include <sys/fs.h>
#include <sys/assert.h>
#include <sys/seg.h>
#include <sys/msg.h>
#include <sys/syscall.h>
#include <mach/io.h>
#include <llist.h>
#include <time.h>
#include <std.h>
#include <stdio.h>
#include "el3.h"

#define ETHER_MIN_LEN	60	/* KLUDGE: should be defined elsewhere */
#define ETHER_MAX_LEN	1500	/* KLUDGE: should be defined elsewhere */

static int id_port = 0x110;


unsigned short read_eeprom (int ioaddr, int index)
{
	outportw (ioaddr + 10, EEPROM_READ + index);
	usleep (500);
	return inportw (ioaddr + 12);

}
unsigned short id_read_eeprom (int index)
{
	int bit, word = 0;


	outportb (id_port, EEPROM_READ + index);
	usleep (500);

	for (bit = 15; bit >= 0; bit--) {
		word = (word << 1) + (inportb (id_port) & 0x01);
	}

	return word;
}

void update_stats (struct adapter *ap)
{
	int ioaddr = ap->a_base;


	outportw (ioaddr + EL3_CMD, StatsDisable);
	EL3WINDOW(6);
	inportb (ioaddr + 0);
	inportb (ioaddr + 1);
	inportb (ioaddr + 2);
	inportb (ioaddr + 3);
	inportb (ioaddr + 4);
	inportb (ioaddr + 5);
	inportb (ioaddr + 6);
	inportb (ioaddr + 7);
	inportb (ioaddr + 8);
	inportw (ioaddr + 10);
	inportw (ioaddr + 12);

	EL3WINDOW(1);
	outportw (ioaddr + EL3_CMD, StatsEnable);
	return;
}

/* el3_close_adapter()
 *	Closes the adapter and resets it.
 *
 */
void el3_close_adapter (struct adapter *ap)
{
	int ioaddr;


	ioaddr = ap->a_base;

	outportw (ioaddr + EL3_CMD, StatsDisable);

	outportw (ioaddr + EL3_CMD, RxDisable);
	outportw (ioaddr + EL3_CMD, TxDisable);

	if (ap->a_if_port == 3) {
		outportw (ioaddr + EL3_CMD, StopCoax);
	}
	else if (ap->a_if_port == 0) {
		EL3WINDOW(4);
		outportw (ioaddr + WN4_MEDIA, inportw (ioaddr + WN4_MEDIA) & ~MEDIA_TP);
	}

	/* Disable the IRQ */
	EL3WINDOW(0);
	outportw (ioaddr + WN0_IRQ, 0x0F00);
}

/* el3_probe()
 *	Probe for 3c509 adapters
 *
 */
int el3_probe (struct adapter *ap)
{
	int ioaddr, irq, if_port, iobase;
	int i;
	static int current_tag = 0;
	unsigned short hwaddr[3];
	short lrs_state = 0xff;


	ap->a_base = 0;
	ap->a_irq = 0;

	enable_io (0x279, 0x279);
	enable_io (0xA79, 0xA79);
	/* Reset the ISA PnP mechanism */
	outportb (0x279, 0x02);
	outportb (0xA79, 0x02);

	enable_io (id_port, id_port + 16);
	outportb (id_port, 0x00);
	outportb (id_port, 0xFF);
	if (inportb (id_port) & 0x01)
		printf ("Got id port\n");

	outportb (id_port, 0x00);
	outportb (id_port, 0x00);
	for (i = 0; i < 255; i++) {
		outportb (id_port, lrs_state);
		lrs_state <<= 1;
		lrs_state = lrs_state & 0x100 ? lrs_state ^ 0xCF : lrs_state;
	}

	if (current_tag == 0) {
		outportb (id_port, 0xD0);
	} else {
		outportb (id_port, 0xD8);
	}

	if (id_read_eeprom (7) != 0x6D50) {
		return 0;
	}

	for (i = 0; i < 3; i++) {
		hwaddr[i] = htons (id_read_eeprom (i));
	}
	memcpy (ap->a_addr, hwaddr, 6);

	iobase = id_read_eeprom (8);
	if_port = iobase >> 14;
	ioaddr = 0x200 + ((iobase & 0x1F) << 4);
	ap->a_base = ioaddr;

	irq = id_read_eeprom (9) >> 12;
	ap->a_irq = irq;

	outportb (id_port, 0xD0 + ++current_tag);

	outportb (id_port, (ioaddr >> 4) | 0xE0);

	EL3WINDOW(0);
	if (inportw (ioaddr) != 0x6D50)
		return 0;

	outportw (ioaddr + WN0_IRQ, 0x0F00);

	return 1;
}

/*
 * el3_configure()
 *	Initialization of interface
 *
 * Set up initialization block and transmit/receive descriptor rings.
 */
void
el3_configure(struct adapter *ap)
{
	int i, ioaddr;


	ioaddr = ap->a_base;

	EL3WINDOW(0);
	outportw (ioaddr + 4, 0x0001);


	/* set IRQ line */
	outportw (ioaddr + WN0_IRQ, (ap->a_irq << 12) | 0x0F00);

	/* set the station address */
	EL3WINDOW(2);

	for (i = 0; i < 6; i++) {
		outportb (ioaddr + i, ap->a_addr[i]);
	}

	if (ap->a_if_port == 3) {
		/* start the thinnet transceiver. */
		outportw (ioaddr + EL3_CMD, StartCoax);
	}
	else if (ap->a_if_port == 0) {
		EL3WINDOW (4);
		outportw (ioaddr + WN4_MEDIA, inportw (ioaddr + WN4_MEDIA) | MEDIA_TP);
	}

	/* clear all stats */
	outportw (ioaddr + EL3_CMD, StatsDisable);
	EL3WINDOW(6);
	for (i = 0; i < 9; i++)
		inportb (ioaddr + i);
	inportw (ioaddr + 10);
	inportw (ioaddr + 12);

	EL3WINDOW(1);

	outportw (ioaddr + EL3_CMD, SetRxFilter | RxStation | RxBroadcast);
	outportw (ioaddr + EL3_CMD, StatsEnable);

	outportw (ioaddr + EL3_CMD, RxEnable);
	outportw (ioaddr + EL3_CMD, TxEnable);

	outportw (ioaddr + EL3_CMD, SetStatusEnb | 0xFF);
	/* Ack all pending events */
	outportw (ioaddr + EL3_CMD, AckIntr | IntLatch | TxAvailable | RxEarly | IntReq);
	outportw (ioaddr + EL3_CMD, SetIntrEnb | IntLatch | TxAvailable | TxComplete | RxComplete | StatsFull);


	ap->a_page = RBUF/256;
}

/*
 * el3_init()
 *	Initialize our Ethernet controller
 *
 * Initialize controller and read Ethernet Address from rom.
 * Card is not ready for transfers; el3_configure must be called to setup that.
 *
 */
void
el3_init(struct adapter *ap)
{
	int ioaddr;

	/*
	 * Get initial buffer
	 */
	ap->a_pktbuf = malloc(PKT_BUFSIZE);

	ioaddr = ap->a_base;

	outportw (ioaddr + EL3_CMD, TxReset);
	outportw (ioaddr + EL3_CMD, RxReset);
	outportw (ioaddr + EL3_CMD, SetStatusEnb | 0x00);


}

/*
 * Setup output on interface.
 * Get another datagram to send off of the interface queue,
 * and map it to the interface before starting the output.
 */
void
el3_start(struct adapter *ap, struct file *f)
{
	struct msg *m = &f->f_msg;
	ushort oddword;
	ushort len, tx_status;
	int i, y = 0;
	uint seg, have_oddword, ioaddr = ap->a_base;
	unsigned char buffer[1500];

	/*
	 * Get length of complete packet
	 */
	len = 0;
	for (seg = 0; seg < m->m_nseg; seg++) {
		len += m->m_seg[seg].s_buflen;
	}

#ifdef EL3_DEBUG
	printf ("send: len = %d\n", len);
#endif /* EL3_DEBUG */

	outportw (ioaddr + TX_FIFO, len);
	outportw (ioaddr + TX_FIFO, 0x00);


	/*
	 * Copy the remaining segments into the transmit buffer
	 * Watch out for odd-length segments.
	 */
	oddword = 0;
	have_oddword = 0;
	for (seg = 0; seg < m->m_nseg; seg++) {
		uchar *p;
		uint slen;
		seg_t *segp;

		/*
		 * Pull buffer/len into local registers
		 */
		segp = &m->m_seg[seg];
		p = segp->s_buf;
		slen = segp->s_buflen;

#ifdef EL3_DEBUG
		printf ("loop\n");
#endif /* EL3_DEBUG */
		i = 0;
		while (i < slen) {
#ifdef EL3_DEBUG
			printf ("%X ", p[i]);
#endif /* EL3_DEBUG */
			buffer[y] = p[i];
			y++;
			i++;
		}

		/*
		 * Write any complete words
		 */
		//repoutsl (ioaddr + TX_FIFO, p, slen >> 2);

	}

	repoutsl (ioaddr + TX_FIFO, buffer, (len + 3) >> 2);


	if (inportw (ioaddr + TX_FREE) > 1536) {
		tx_busy[0] = 0;
	}
	else {
		outportw (ioaddr + EL3_CMD, SetTxThreshold + 1536);
	}

	i = 4;
	while (--i > 0 && (tx_status = inportb (ioaddr + TX_STATUS)) > 0) {
		if (tx_status & 0x30) outportw (ioaddr + EL3_CMD, TxReset);
		if (tx_status & 0x3C) outportw (ioaddr + EL3_CMD, TxEnable);
		outportb (ioaddr + TX_STATUS, 0x00);
	}	
}

#define min(a,b)	((a) <= (b) ? (a) : (b))

/* buffer successor/predecessor in ring? */
#define succ(n) (((n) + 1 >= RBUFEND/DS_PGSIZE) ? RBUF/DS_PGSIZE : (n) + 1)
#define pred(n) (((n) - 1 < RBUF/DS_PGSIZE) ? RBUFEND/DS_PGSIZE - 1 : (n) - 1)

/*
 * el3recv()
 *	Ethernet interface receiver interface
 *
 * If input error just drop packet.
 * Otherwise examine packet to determine type.  If can't determine length
 * from type, then have to drop packet.  Othewise decapsulate
 * packet based on type and pass to type specific higher-level
 * input routine.
 */
void
el3recv(struct adapter *ap)
{
	int len;
#ifdef EL3_DEBUG
	int i;
#endif /* EL3_DEBUG */
	short rx_status;
	int ioaddr = ap->a_base;


	while ((rx_status = inportw (ioaddr + RX_STATUS)) > 0) {
		if (rx_status & 0x4000) {
			outportw (ioaddr + EL3_CMD, RxDiscard);
		} else {
			len = rx_status & 0x7FF;
#ifdef EL3_DEBUG
			printf ("recv: len = %d\n", len);
#endif /* EL3_DEBUG */
			
			repinsl (ioaddr + RX_FIFO, ap->a_pktbuf, (len + 3) >> 2);
			outportw (ioaddr + EL3_CMD, RxDiscard);

#ifdef EL3_DEBUG
			i = 0;
			while (i < len) {
				printf ("%X ", ap->a_pktbuf[i]);
				i++;
			}
#endif /* EL3_DEBUG */

			/*
			 * If the upper level wants to hold onto it (queueing for
			 * its clients), get another buffer
			 */
			if (el3_send_up(ap->a_pktbuf, len, 0)) {
				/*
				 * Use our private packet pool if possible,
				 * otherwise get more from malloc().
				 */
				if (!pak_pool) {
					ap->a_pktbuf = malloc(PKT_BUFSIZE);
				} else {
					ap->a_pktbuf = pak_pool;
					pak_pool = *(void **)pak_pool;
				}
			}
		}
	}

}

/*
 * el3_isr()
 *	Controller interrupt
 */
void
el3_isr(void)
{
	int unit = 0;				/* KLUDGE: hardcoded */
	struct adapter *ap = &adapters[unit];
	int status, i = 4;
	short tx_status;
	int ioaddr = ap->a_base;

	while ((status = inportw (ioaddr + EL3_STATUS)) &
		(IntLatch | RxComplete | StatsFull)) {

		if (status & RxComplete) {
			el3recv (ap);
		}
		
		if (status & TxAvailable) {
			outportw (ioaddr + EL3_CMD, AckIntr | TxAvailable);
			tx_busy[unit] = 0;
		}

		if (status & (AdapterFailure | RxEarly | StatsFull | TxComplete)) {
			if (status & StatsFull) {
				update_stats (ap);
			}
			if (status & RxEarly) {
				el3recv (ap);
				outportw (ioaddr + EL3_CMD, AckIntr | RxEarly);
			}
			if (status & TxComplete) {
				i = 4;
				while (--i > 0 && (tx_status = inportb (ioaddr + TX_STATUS)) > 0) {
					if (tx_status & 0x30) outportw (ioaddr + EL3_CMD, TxReset);
					if (tx_status & 0x3C) outportw (ioaddr + EL3_CMD, TxEnable);
					outportw (ioaddr + TX_STATUS, 0x00);
				}
			}
			if (status & AdapterFailure) {
				outportw (ioaddr + EL3_CMD, RxReset);
				outportw (ioaddr + EL3_CMD, SetRxFilter | RxStation | RxBroadcast);
				outportw (ioaddr + EL3_CMD, RxEnable);
				outportw (ioaddr + EL3_CMD, AckIntr | AdapterFailure);
			}
		}

		if (--i < 0) {
			printf ("infinite loop in interrupt, status %4.4x.\n", status);
			outportw (ioaddr + EL3_CMD, AckIntr | 0xFF);
			break;
		}

		outportw (ioaddr + EL3_CMD, AckIntr | IntReq | IntLatch);
	}


	/* Any more to send? */
	if (tx_busy[unit] == 0)
		run_queue(unit);

}
