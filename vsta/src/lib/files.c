/*
 * files.c
 *	Support routines, mostly for sanity checking
 */

/*
 * valid_fname()
 *	Given a message buffer, catch some common crockery
 *
 * Not a completely honest routine name, since we ensure the
 * the presence of a terminating null, too.
 */
valid_fname(char *buf, int bufsize)
{
	if (bufsize > 128) {
		return(0);
	}
	if (buf[bufsize-1] != '\0') {
		return(0);
	}
	return(1);
}
