/*
 * stat.c
 *	Do the stat function
 */
#include <sys/param.h>
#include <sys/perm.h>
#include <sys/fs.h>
#include <stdio.h>
#include <std.h>
#include "rs232.h"
#include "fifo.h"

extern char *perm_print();

extern struct prot rs232_prot;
extern uint accgen;
extern int irq, iobase;
extern int baud, databits, stopbits, parity;
extern int rx_fifo_threshold, tx_fifo_threshold;
extern uchar dsr, dtr, cts, rts, dcd, ri;
extern struct fifo *inbuf, *outbuf;
extern int uart;
extern port_name rs232port_name;

static char parity_names[5][5] = {"none", "even", "odd", "zero", "one"};
uchar onlcr = 1;	/* Convert \n to \r\n on output */
static int rows = 24,		/* Geometry */
	cols = 80;
uint nsel;			/* # of select() clients */
struct llist selectors;		/* List of all select() clients */

/*
 * rs232_stat()
 *	Do stat requests
 */
void
rs232_stat(struct msg *m, struct file *f)
{
	char buf[MAXSTAT];

	if (!(f->f_flags & ACC_READ)) {
		msg_err(m->m_sender, EPERM);
		return;
	}
	rs232_getinsigs();
	sprintf(buf,
		"size=0\ntype=c\nowner=0\ninode=0\ngen=%d\n%s" \
		"uart=%s\nbaseio=0x%x\nirq=%d\n" \
		"rxfifothr=%d\ntxfifothr=%d\noverruns=%ld\n" \
		"baud=%d\ndatabits=%d\nstopbits=%s\nparity=%s\n" \
		"dsr=%d\ndtr=%d\ncts=%d\nrts=%d\ndcd=%d\nri=%d\n" \
		"inbuf=%d\noutbuf=%d\nonlcr=%d\nrows=%d\ncols=%d\n",
		accgen, perm_print(&rs232_prot),
		uart_names[uart],
		iobase, irq,
		rx_fifo_threshold, tx_fifo_threshold, overruns,
		baud, databits,
		(stopbits == 1 ? "1" : (databits == 5 ? "1.5" : "2")),
		parity_names[parity],
		dsr, dtr, cts, rts, dcd, ri,
		inbuf->f_cnt, outbuf->f_cnt, onlcr, rows, cols);
	m->m_buf = buf;
	m->m_arg = m->m_buflen = strlen(buf);
	m->m_nseg = 1;
	m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}

/*
 * update_select()
 *	Send out new select events
 */
void
update_select(void)
{
	struct llist *l;
	struct file *f;
	uint event;

	/*
	 * If no selectors, bail out quickly
	 */
	if (nsel == 0) {
		return;
	}

	/*
	 * Figure out which events to hand off
	 */
	event = 0;
	if (inbuf->f_cnt > 0) {
		event |= ACC_READ;
	}
	if (outbuf->f_cnt < outbuf->f_size) {
		event |= ACC_WRITE;
	}

	/*
	 * No events--don't bother
	 */
	if (event == 0) {
		return;
	}

	/*
	 * Tell our clients
	 */
	for (l = LL_NEXT(&selectors); l != &selectors; l = LL_NEXT(l)) {
		f = l->l_data;
		sc_event(&f->f_selfs, event);
	}
}

/*
 * rs232_wstat()
 *	Allow writing of supported stat messages - rather a lot of them :-)
 */
void
rs232_wstat(struct msg *m, struct file *f)
{
	char *field, *val;
	int t;

	/*
	 * See if common handling code can do it
	 */
	if (do_wstat(m, &rs232_prot, f->f_flags, &field, &val) == 0)
		return;

	/*
	 * See if it's a select() client
	 */
	if (sc_wstat(m, &f->f_selfs, field, val) == 0) {
		f->f_sentry = ll_insert(&selectors, f);
		nsel += 1;
		update_select();
		return;
	}

	/*
	 * Process each kind of field we can write
	 */
	if (!strcmp(field, "rts")) {
		/*
		 * Set the RTS state - default is to enabled
		 */
		int newrts;
		
		newrts = val ? atoi(val) : 1;
		rs232_setrts(newrts);
	} else if (!strcmp(field, "dtr")) {
		/*
		 * Set the DTR state - default is to enabled
		 */
		int newdtr;
		
		newdtr = val ? atoi(val) : 1;
		rs232_setdtr(newdtr);
	} else if (!strcmp(field, "onlcr")) {
		/*
		 * Set new ONLCR; default is on
		 */
		onlcr = (val != 0) ? (atoi(val) ? 1 : 0) : 1;
	} else if (!strcmp(field, "gen")) {
		/*
		 * Set access-generation field
		 */
		if (val) {
			accgen = atoi(val);
		} else {
			accgen += 1;
		}
		f->f_gen = accgen;
	} else if (!strcmp(field, "baud")) {
		/*
		 * Set the connection baud rate
		 */
		int brate;

		brate = val ? atoi(val) : 9600;
		rs232_baud(brate);
	} else if (!strcmp(field, "databits")) {
		/*
		 * Set the number of data bits
		 */
		int dbits;

		dbits = val ? atoi(val) : 8;
		if (dbits < 5 || dbits > 8) {
			/*
			 * Not a value we support - fail!
			 */
			msg_err(m->m_sender, EINVAL);
			return;
		}
		rs232_databits(dbits);
	} else if (!strcmp(field, "stopbits")) {
		/*
		 * Set the number of stop bits
		 */
		int sbits;

		if (!strcmp(val, "2") || !strcmp(val, "1.5")) {
			sbits = 2;
		} else if (!strcmp(val, "1")) {
			sbits = 1;
		} else {
			/*
			 * Not a value we support - fail!
			 */
			msg_err(m->m_sender, EINVAL);
			return;
		}
		rs232_stopbits(sbits);
	} else if (!strcmp(field, "parity")) {
		/*
		 * Set the type of parity to be used
		 */
		int i, ptype = -1;

		for (i = 0; i < 5; i++) {
			if (!strcmp(val, parity_names[i])) {
				ptype = i;
				break;
			}
		}
		if (ptype == -1) {
			/*
			 * Not a value we support - fail!
			 */
			msg_err(m->m_sender, EINVAL);
			return;
		}
		rs232_parity(ptype);
	} else if (!strcmp(field, "rxfifothr")) {
		/*
		 * Set the UART receiver FIFO threshold
		 */
		t = val ? atoi(val) : 0;
		if (rs232_setrxfifo(t)) {
			/*
			 * We don't support that value on this UART
			 */
			msg_err(m->m_sender, EINVAL);
			return;
		}
	} else if (!strcmp(field, "rows")) {
		if (!val || (sscanf(val, "%d", &t) != 1)) {
			msg_err(m->m_sender, EINVAL);
			return;
		}
		rows = t;
	} else if (!strcmp(field, "cols")) {
		if (!val || (sscanf(val, "%d", &t) != 1)) {
			msg_err(m->m_sender, EINVAL);
			return;
		}
		cols = t;
	} else {
		/*
		 * Not a field we support - fail!
		 */
		msg_err(m->m_sender, EINVAL);
		return;
	}

	/*
	 * Return success
	 */
	m->m_buflen = m->m_nseg = m->m_arg = m->m_arg1 = 0;
	msg_reply(m->m_sender, m);
}
