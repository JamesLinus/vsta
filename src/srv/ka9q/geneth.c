/*
 * geneth.c
 *	Generic interface to a /dev/ether type of driver on VSTa
 */
#include <sys/msg.h>
#include <sys/fs.h>
#include <fcntl.h>
#include <stdio.h>
#include <std.h>
#include "eth.h"
#include "global.h"
#include "mbuf.h"
#include "enet.h"
#include "iface.h"
#include "timer.h"
#include "arp.h"
#include "trace.h"
#include "vsta.h"

extern struct interface *ifaces;
extern int pether(), gaether(), enet_send(), enet_output();
static struct mbuf *rxqueue;
static port_t ethport, ethport_rx;

#define MTU (1524)		/* MTU off ether */
#define MAXBUF (64)		/* Max packets buffered in driver */

/*
 * MAC address for interface
 */
unsigned char eth_hwaddr[6] = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 };

/* 
 * eth_raw()
 *	Write a packet to ether interface
 */
int
eth_raw(struct interface *i, struct mbuf *bp)
{
	int size = 0, count = 0, res;
	struct mbuf *b = bp;
	unsigned char *p, *overbuf = 0;
	struct msg m;

	dump(i, IF_TRACE_OUT, TRACE_ETHER, bp);

	/*
	 * Point segments of VSTa message into mbuf chain, build
	 * a message for the ether driver.
	 */
	while (b) {
		/*
		 * Ew.  If the response takes more elements than our
		 * scatter/gather can handle, concatentate the last
		 * buffers into a single malloc()'ed one.
		 */
		if (count == (MSGSEGS-1)) {
			int overflow;
			uchar *p;

			/*
			 * Accumulate data into "overbuf", with "overflow"
			 * tabulating the amount of data contained.  Note
			 * that "overbuf"'s address can change due to
			 * realloc().
			 */
			overflow = 0;
			while (b) {
				int off;
				uchar *p;

				/*
				 * Hold the current count, generate the
				 * new count, and get a buffer of the
				 * new needed size.
				 */
				off = overflow;
				overflow += b->cnt;
				p = overbuf;
				overbuf = realloc(overbuf, overflow);

				/*
				 * Point to where the new space starts.
				 * Bomb if out of memory.
				 */
				if (!overbuf) {
					printf("No memory in geneth\n");
					free_p(bp);
					free(p);
					return(-1);
				}
				p = overbuf + off;

				/*
				 * Copy this buffer into place, and walk
				 * to next buffer.
				 */
				bcopy(b->data, p, b->cnt);
				b = b->next;
			}

			/*
			 * Tack this accumulated buffer on as the
			 * last one, and finish the loop.
			 */
			m.m_seg[count].s_buf = overbuf;
			m.m_seg[count].s_buflen = overflow;
			count += 1;
			size += overflow;
			break;
		}

		/*
		 * Point the scatter/gather into this next
		 * data element.
		 */
		m.m_seg[count].s_buf = b->data;
		m.m_seg[count].s_buflen = b->cnt;
		count += 1;
		size += b->cnt;
		b = b->next;
	}

	/*
	 * Build FS write message
	 */
	m.m_op = FS_WRITE;
	m.m_nseg = count;
	m.m_arg = size;

	/*
	 * Off it goes...
	 */
	res = msg_send(ethport, &m);
	free_p(bp);
	if (overbuf) {
		free(overbuf);
	}
	return((res > 0) ? 0 : -1);
}

/*
 * eth_stop()
 *	Shut down access to the device
 */
int
eth_stop(int dev)
{
	close(dev);
	return (0);
}

/*
 * eth_recv()
 *	Receive another packet from the ethernet
 *
 * The actual receipt of packets is done in a thread context.
 * They are then queued and made available to this routine.
 * Returns non-zero if any packets were processed, otherwise 0.
 */
int
eth_recv(struct interface *i)
{
	struct mbuf *bp, *bpn;

	/*
	 * Grab the queue of packets.  Zero it.
	 */
	bp = rxqueue;
	if (bp == 0) {
		return(0);
	}
	rxqueue = 0;

	/*
	 * Feed in the list of packets
	 */
	while (bp) {
		bpn = bp->anext;
		bp->anext = 0;
		dump(i, IF_TRACE_IN, TRACE_ETHER, bp);
		eproc(i, bp);
		bp = bpn;
	}
	return(1);
}

/*
 * eth_recv_daemon()
 */
void
eth_recv_daemon(void)
{
	int len, count = 0;
	struct mbuf *head = 0, *bp, *tail;
	struct msg m;

	/*
	 * Loop receiving frames
	 */
	for (;;) {
		/*
		 * Get packet mbuf and data for MTU
		 */
		p_lock(&ka9q_lock);
		bp = alloc_mbuf(MTU);
		v_lock(&ka9q_lock);
		if (!bp) {
			sleep(1);
			continue;
		}

		/* 
		 * Send message
		 */
again:		m.m_op = FS_READ;
		m.m_buf = bp->data;
		m.m_buflen = MTU;
		m.m_nseg = 1;
		m.m_arg = MTU;
		m.m_arg1 = 0;
		len = msg_send(ethport_rx, &m);
		if (len < 0) {
			p_lock(&ka9q_lock);
			perror("eth_recv_daemon");
			msg_disconnect(ethport);
			free_mbuf(bp);
			v_lock(&ka9q_lock);
			msg_disconnect(ethport_rx);
			ethport = ethport_rx = 0;
			_exit(0);
		}

		/*
		 * Start discarding packets when we've queued up
		 * too many without processing them.
		 */
		if (count >= MAXBUF) {
			goto again;
		}

		/*
		 * Add to our list, set up the mbuf structures
		 */
		if (head == 0) {
			head = tail = bp;
		} else {
			tail->anext = bp;
			tail = bp;
		}
		bp->anext = NULL;
		bp->next = NULL;
		bp->size = len;
		bp->cnt = len;
		count += 1;

		/*
		 * If we've assembled some frames, and all previous
		 * frames have been processed, then put these on the
		 * queue and run the main processing loop.
		 */
		if (rxqueue == 0) {
			rxqueue = head;
			head = 0;
			count = 0;
			p_lock(&ka9q_lock);
			while (do_mainloop())
				;
			v_lock(&ka9q_lock);
		}
	}
}

/*
 * eth_attach()
 *	Start up the ether interface
 */
eth_attach(int argc, char **argv)
{
	struct interface *if_eth;
	int ethfd, i, dig[6];
	char *macaddr;

	/*
	 * Only one (for now XXX)
	 */
	if (ethport) {
		printf("eth_attach: already attached\n");
		return(-1);
	}

	/*
	 * Allocate interface, initialize data structure
	 */
	if_eth = calloc(1, sizeof(struct interface));
	if (if_eth) {
		if_eth->name = strdup(argv[1]);
	}
	if (!if_eth || !if_eth->name) {
		printf("eth_attach: no memory!\n");
		if (if_eth) {
			free(if_eth);
		}
		return(-1);
	}
	if_eth->hwaddr = (char *)eth_hwaddr;
	if_eth->mtu = atoi(argv[2]);
	if_eth->send = enet_send;
	if_eth->output = enet_output;
	if_eth->raw = eth_raw;
	if_eth->recv = eth_recv;
	if_eth->stop = eth_stop;
	ethfd = open(argv[3], O_RDWR);
	if (ethfd < 0) {
		perror (argv[3]);
		free(if_eth->name);
		free(if_eth);
		return(-1);
	}
	ethport = __fd_port(ethfd);

	/*
	 * Extract MAC address from driver
	 */
	macaddr = rstat(ethport, "macaddr");
	if (!macaddr) {
		printf("eth_attach: can't get MAC address\n");
		msg_disconnect(ethport);
		free(if_eth->name);
		free(if_eth);
		return(-1);
	}
	if (sscanf(macaddr ,"%x.%x.%x.%x.%x.%x", &dig[0], &dig[1],
			&dig[2], &dig[3], &dig[4], &dig[5]) != 6) {
		printf("eth_attach: Ethernet address must be 6 "
			 "hex octets in format 1.2.3.4.5.6\n");
		return(-1);
	}
	for (i = 0; i < 6; i++) {
		eth_hwaddr[i] = dig[i];
	}

	/*
	 * Get a distinct client port so we can receive and send
	 * concurrently.
	 */
	ethport_rx = clone(ethport);

	/*
	 * Wire for ARP handling
	 */
	arp_init(ARP_ETHER, EADDR_LEN, IP_TYPE, ARP_TYPE,
		ether_bdcst, pether, gaether);

	/*
	 * Add to list of all interfaces
	 */
	if_eth->next = ifaces;
	ifaces = if_eth;

	/*
	 * Launch daemon
	 */
	(void)vsta_daemon(eth_recv_daemon);

	return 0;
}
