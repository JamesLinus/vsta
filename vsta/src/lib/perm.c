/*
 * perm.c
 *	Permission/protection mapping
 *
 * This is mostly a study in frustration; the POSIX style of interface
 * disables much of the expressive power of the VSTa protection system.
 */

/*
 * umask()
 *	Set default protection mask
 */
umask(int newmask)
{
	return(0600);
}

/*
 * chmod()
 *	Change mode of file
 */
chmod(char *file, int mode)
{
	return(0);
}
