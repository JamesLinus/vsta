/*
 * dir.c
 *	Routines for providing readdir() family of functions
 *
 * Considerably complicated by the fact that we must maintain the
 * illusion of the presence of "directories" when in fact we are
 * only seeing parts of prefixes of paths in the mount table.
 */
#include <dirent.h>
#include <mnttab.h>
#include <std.h>

extern char *__cwd;
extern struct mnttab *__mnttab;
extern int __nmnttab;

/*
 * opendir()
 *	Open access to named directory
 */
DIR *
opendir(char *path)
{
	DIR *d;
	char *p;
	extern char *__cwd;

	/*
	 * Bogus.
	 */
	if (!path || !path[0]) {
		return(0);
	}

	/*
	 * Get private, writable copy of path.  Flatten to absolute.
	 */
	if (path[0] == '/') {
		p = strdup(path);
	} else {
		p = malloc(strlen(path) + strlen(__cwd) + 2);
		if (p ) {
			sprintf(p, "%s/%s", __cwd, path);
		}
	}

	if (p == 0) {
		return(0);
	}
	if (__dotdot(p)) {
		free(p);
		return(0);
	}

	/*
	 * All directory paths should end in "/".  Add one if
	 * it isn't present.
	 */
	if (p && (p[strlen(p)-1] != '/')) {
		char *q;

		q = realloc(p, strlen(p)+2);
		if (!q) {
			free(p);
			return(0);
		}
		p = q;
		strcat(p, "/");
	}

	/*
	 * Get a DIR
	 */
	d = malloc(sizeof(DIR));
	if (d == 0) {
		free(p);
		return(0);
	}

	/*
	 * Initialize
	 */
	bzero(d, sizeof(DIR));
	d->d_path = p;
	return(d);
}

/*
 * closedir()
 *	Free storage and close directory
 */
closedir(DIR *d)
{
	int x;

	if (d->d_fp) {
		x = fclose(d->d_fp);
	} else {
		x = 0;
	}
	free(d->d_path);
	free(d);
	return(x);
}

/*
 * readent()
 *	Read next dirent from open directory
 *
 * We use a buffered interface for efficiency
 */
static
readent(DIR *d)
{
	int l, c;
	char *p;
	struct dirent *de = &d->d_de;

	/*
	 * Read up to next newline into buffer
	 */
	p = de->d_name;
	l = 0;
	while ((c = getc(d->d_fp)) != EOF) {
		if (c == '\n') {
			break;
		}
		if (++l < _NAMLEN) {
			*p++ = c;
		}
	}

	/*
	 * Return end-of-dir indication
	 */
	if (c == EOF) {
		return(1);
	}

	/*
	 * Fill in length, also null-terminate string
	 */
	de->d_namlen = (p - de->d_name);
	*p++ = '\0';

	return(0);
}

/*
 * readdir()
 *	Read next directory entry
 *
 * Two kinds of entries are returned.  First, we walk our way
 * through the mount table, reading entries from each port mounted
 * under the given mount point.  Once we've finished those, we
 * then return pseudo-directories for paths which are longer
 * than our own.
 */
struct dirent *
readdir(DIR *d)
{
	int x, len;
	struct mnttab *mt;
	struct mntent *me;
	port_t port;
	char *p;

retry:
	/*
	 * If we currently have a dir open, read the next entry
	 * from it.
	 */
	if (d->d_fp) {
		/*
		 * Try to read another entry.  If we fail, advance
		 * state and fall into code below to move to next
		 * potential source of directory entries.
		 */
		if (readent(d)) {
			/*
			 * Don't advance if this is a one-only read
			 * of a subdir under a mount point--we're done
			 * after the single directory.
			 */
			if (d->d_state >= 0) {
				d->d_state += 1;
			}
			fclose(d->d_fp);
			d->d_fp = 0;
		} else {
			/*
			 * Otherwise update position and return
			 * next entry.
			 */
			d->d_elems += 1;
			return(&d->d_de);
		}
	}

	/*
	 * First phase--walk through mount entries
	 */
	if (d->d_state >= 0) {
		int longest;
		int tgtlen;

		/*
		 * If we don't find an exact match, we'll use
		 * this to open the actual dir.
		 */
		longest = -1;
		len = 0;
		for (x = 0; x < __nmnttab; ++x) {
			mt = &__mnttab[x];

			/*
			 * Go for it on exact match
			 */
			if (!strcmp(d->d_path, mt->m_name)) {
				/*
				 * Advance to next entry appropriate
				 * based on our state.
				 */
				me = mt->m_entries;
				for (x = 0; me && (x < d->d_state); ++x) {
					me = me->m_next;
				}

				/*
				 * If we have another, open it up and
				 * use it.
				 */
				if (me) {
					int fd;

					port = clone(me->m_port);
					fd = __fd_alloc(port);
					d->d_fp = fdopen(fd, "r");
					goto retry;
				}

				/*
				 * Otherwise done with try mounts, fall
				 * into mount table code below.
				 */
				break;
			}

			/*
			 * If longest initial match, record
			 */
			tgtlen = strlen(mt->m_name);
			if ((tgtlen > len) && !strncmp(d->d_path,
					mt->m_name, tgtlen)) {
				longest = x;
				len = tgtlen;
			}
		}

		/*
		 * Flag initial step in second phase
		 */
		d->d_state = -1;

		/*
		 * If we didn't get an exact match, but there's
		 * a mount point which contains us (i.e., the thing
		 * we're looking at is a subdir under a mount point),
		 * then riffle through this before firing the second
		 * phase.
		 */
		if (longest != -1) {
			if (d->d_fp = fopen(d->d_path, "r")) {
				goto retry;
			}
		}
	}

	/*
	 * Second phase--simulate directory entries for path strings
	 * longer than our own.  This is flagged by starting with -1
	 * and counting downwards as we work our way through the mount
	 * table.  "x" is set to a 0-based ascending value derived from
	 * the negative-going count.
	 */
	x = (-1 - (d->d_state));
	len = strlen(d->d_path);
	for (; x < __nmnttab; ++x) {
		mt = &__mnttab[x];
		d->d_state -= 1;

		/*
		 * Filter all but those with our leading prefix
		 * but having a greater length.
		 */
		if (strncmp(mt->m_name, d->d_path, len) ||
				(strlen(mt->m_name) <= len)) {
			/*
			 * Continue search
			 */
			continue;
		}

		/*
		 * We have a live one.  Copy it into place and
		 * truncate it to just the element which lines up
		 * with our current directory.
		 */
		strcpy(d->d_de.d_name, mt->m_name+len);
		p = strchr(d->d_de.d_name, '/');
		if (p) {
			*p++ = '\0';
		}
		return(&d->d_de);
	}
	return(0);
}

/*
 * seekdir()
 *	Seek to position in directory
 */
void
seekdir(DIR *d, long pos)
{
	int x;

	/*
	 * Close any currently open file
	 */
	if (d->d_fp) {
		fclose(d->d_fp);
		d->d_fp = 0;
	}

	/*
	 * Reset state of DIR
	 */
	d->d_state = 0;
	d->d_elems = 0;

	/*
	 * Skip forward the requested number
	 */
	for (x = 0; x < pos; ++x) {
		(void)readdir(d);
	}
}

/*
 * rewinddir()
 *	Seek to beginning of directories
 */
void
rewinddir(DIR *d)
{
	seekdir(d, 0L);
}

/*
 * telldir()
 *	Return index for seekdir()
 */
long
telldir(DIR *d)
{
	return(d->d_elems);
}
