/*
 * perm.c
 *	Permission/protection mapping
 *
 * This is mostly a study in frustration; the POSIX style of interface
 * disables much of the expressive power of the VSTa protection system.
 */
#include <sys/fs.h>
#include <sys/perm.h>
#include <unistd.h>
#include <fcntl.h>
#include <stat.h>
#include <string.h>

/*
 * umask()
 *	Set default protection mask
 */
int
umask(int newmask)
{
	return(0022);
}

/*
 * chmod()
 *	Change mode of file
 */
int
chmod(const char *file, int mode)
{
	int x, fd;

	fd = open(file, O_CHMOD);
	if (fd < 0) {
		return(-1);
	}
	x = fchmod(fd, mode);
	close(fd);
	return(x);
}

/*
 * map_perm()
 *	Convert POSIX mode bits into access bits in a VSTa prot label
 */
static void
map_perm(uchar *protp, int mode)
{
	uint give = 0;

	*protp &= ~(ACC_READ | ACC_WRITE | ACC_EXEC);
	if (mode & S_IREAD)  give |= ACC_READ;
	if (mode & S_IWRITE)  give |= ACC_WRITE;
	if (mode & S_IEXEC)  give |= ACC_EXEC;
	*protp |= give;
}

/*
 * fchmod()
 *	Change mode of open file
 *
 * Our mapping of POSIXese to VSTa is as follows:
 *	"Other" affects the default protection granted
 *	"Group" affects all intermediate protections
 *	"User" affects the final, most specific match
 */
int
fchmod(int fd, int mode)
{
	char *prot, buf[PERMLEN*8];
	uchar prots[PERMLEN+1];
	uint nprot, x;

	/*
	 * Get current protections
	 */
	prot = rstat(__fd_port(fd), "acc");
	if (prot == 0) {
		return(__seterr(EINVAL));
	}

	/*
	 * Explode out to prots[]
	 */
	nprot = 0;
	while ((nprot < (PERMLEN + 1)) && *prot && (*prot != '\n')) {
		prots[nprot++] = atoi(prot);
		prot = strchr(prot, '/');
		if (!prot) {
			break;
		}
		prot += 1;
	}

	/*
	 * Now mung up the protections based on a mapping of POSIX
	 * protection concepts.
	 */

	/*
	 * "User".  Apllies to the most specific.
	 */
	map_perm(&prots[nprot-1], mode);

	/*
	 * "Group".  Applies to all of the rest except the most specific.
	 */
	mode <<= 3;
	for (x = 1; x < (nprot-1); ++x) {
		map_perm(&prots[x], mode);
	}

	/*
	 * "Other".  Affects first (default) protection slot
	 */
	mode <<= 3;
	map_perm(&prots[0], mode);

	/*
	 * Build a "acc=X" to reflect potentially changed protection
	 */
	strcpy(buf, "acc=");
	for (x = 0; x < nprot; ++x) {
		sprintf(buf + strlen(buf), "%s%d",
			x ? "/" : "",
			prots[x]);
	}
	strcat(buf, "\n");

	/*
	 * Send it back
	 */
	return(wstat(__fd_port(fd), buf));
}

/*
 * access()
 *	Tell if we can access a file
 *
 * Since we don't have much clue about the protection hierarchy of
 * the file, we make the simplifying assumption that if it's writable
 * to anybody, it's writable to us.  This is not at all correct, but
 * the alternative is to implement quite a bit of filesystem
 * protection logic.  This routine used to just open the file for
 * the requested mode, but for W_OK that would update the mtime
 * of the file, which is not the correct behavior.
 */
int
access(const char *file, int mode)
{
	struct stat sb;

	if (stat(file, &sb) < 0) {
		return(-1);
	}
	if (mode & W_OK) {
		return((sb.st_mode & 0222) ? 0 : __seterr(EPERM));
	} else {
		return((sb.st_mode & 0444) ? 0 : __seterr(EPERM));
	}
}

/*
 * chown()
 *	Change the owner of a file
 */
int
chown(const char *path, uid_t owner, gid_t group)
{
	return(0);
}
