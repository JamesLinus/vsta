/*
 * msg_receive()
 *	Front-end to system call
 *
 * This routine is needed because msg_receive() can receive both segments
 * as well as copies of data into a directly-specified buffer.  We receive
 * a message, copy out any parts for which a buffer was directly specified,
 * fill in segment information for any remaining data, and then return
 * the message to our caller.
 */
msg_receive(port_t port, struct msg *msg)
{
	struct msg m;
	int x;
	extern int _msg_receive();

	/*
	 * If the receive will accept the segments as-is, simply make
	 * the call.
	 */
	if ((msg->m_buflen == 0) && (msg->m_nseg == 0)) {
		return(_msg_receive(port, msg));
	}

	/*
	 * Get a message.  Don't do anything else if an error happens.
	 */
	x = _msg_receive(port, &m);
	if (x < 0) {
		return(x);
	}

	/*
	 * Copy over the easy parts
	 */
	msg->m_op = m.m_op;
	msg->m_arg = m.m_arg;
	msg->m_arg1 = m.m_arg1;

	/*
	 * If we received no segments, don't fuss with buffers
	 */
	if ((m.m_nseg == 0) && (m.m_buflen == 0)) {
		msg->m_buflen = 0;
		msg->m_nseg = 0;
		return(x);
	}

	/*
	 * Otherwise copy out indicated amount.
	 */
	msg->m_nseg =
		copysegs(&m.m_seg0, SEGCNT(&m), &msg->m_seg0, SEGCNT(msg));
	return(x);
}
/*
 * copysegs()
 *	Copy from one array of segments to another
 *
 * If the source has more data than the destination, update the destination's
 * list of segments to represent the remainder.
 *
 * Returns the number of residual segments placed in the "dest" array.
 */
static
copysegs(seg_t *src, int nsrc, seg_t *dest, int ndest)
{
	uint cnt;
	uint offsrc = 0, offdest = 0;
	char *from, *to;
	seg_t *segs;
	int x;

	/*
	 * Record where we will place residual information.  The "+1"
	 * is because the buf/buflen segment is skipped for residual
	 * information.
	 */
	segs = dest+1;

	/*
	 * Skip over an inital zero-length seg_t.  This would be a user
	 * receive into segments with the first segment (m_buflen) set
	 * to zero.  This zero-length seg_t, if it is present, is not
	 * included in the "nsrc" count.
	 */
	if (src->s_buflen == 0) {
		src += 1;
	}

	do {
		/*
		 * Calculate how much we can move this time.  End loop
		 * when out of data on one side.
		 */
		cnt = MIN(src->s_buflen - offsrc, dest->s_buflen - offdest);
		if (cnt == 0) {
			break;
		}

		/*
		 * Copy the memory
		 */
		from = src->s_buf;
		from += offsrc;
		to = dest->s_buf;
		to += offdest;
		bcopy(from, to, cnt);

		/*
		 * Advance the pointer/offset pairs
		 */
		offsrc += cnt;
		if (offsrc > src->s_buflen) {
			src += 1;
			nsrc -= 1;
			offsrc = 0;
		}
		offdest += cnt;
		if (offdest > dest->s_buflen) {
			dest += 1;
			ndest -= 1;
			offdest = 0;
		}
	} while (nsrc && ndest);

	/*
	 * Now we need to fill in any residual segment information.  Residual
	 * information on the destination side is fine.  When there's more
	 * from the source, we have to build a list of segments so the
	 * receiver can peruse them at its leisure.
	 */

	/*
	 * No more data.  No segments need to be described; just return.
	 */
	if (ndest == 0) {
		return(0);
	}

	/*
	 * Place residual of current segment into first slot
	 */
	segs[0].s_buf = dest->s_buf + offdest;
	segs[0].s_buflen = dest->s_buflen - offdest;
	ndest -= 1;
	dest += 1;

	/*
	 * Loop creating segment descriptions for rest
	 */
	for (x = 1; ndest > 0; ++x, --ndest, ++dest) {
		segs[x] = *dest;
	}
	return(x);
}
