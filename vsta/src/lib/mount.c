/*
 * mount.c
 *	Routines for manipulating mount table
 */
#include <mnttab.h>
#include <std.h>
#include <fcntl.h>

struct mnttab *__mnttab;
int __nmnttab = 0;

/*
 * mountport()
 *	Like mount(), but mount on given port
 */
mountport(char *point, port_t fd)
{
	int mntidx, x;
	struct mnttab *mt;
	struct mntent *me;

	/*
	 * Scan mount table for this point
	 */
	mntidx = -1;
	for (x = 0; x < __nmnttab; ++x) {
		int y;

		/*
		 * Compare to entry
		 */
		y = strcmp(point, __mnttab[x].m_name);

		/*
		 * On exact match, end loop
		 */
		if (!y) {
			break;
		}

		/*
		 * Otherwise record where we would need to insert
		 * this entry.
		 */
		if ((y < 0) && (mntidx == -1)) {
			mntidx = y;
		}
	}

	/*
	 * Get memory for mntent
	 */
	me = malloc(sizeof(struct mntent));
	if (!me) {
		close(fd);
		return(-1);
	}
	me->m_fd = fd;

	/*
	 * If needed, insert new mnttab slot
	 */
	if (x >= __nmnttab) {
		/*
		 * Grow mount table
		 */
		mt = realloc(__mnttab, (__nmnttab+1)*sizeof(struct mnttab));
		if (!mt) {
			free(me);
			close(fd);
			return(-1);
		}
		__mnttab = mt;

		/*
		 * Open slot at required index if needed.  Leave "mt"
		 * pointing at it.
		 */
		if (mntidx != -1) {
			mt = __mnttab+mntidx;
			bcopy(mt, mt+1,
				(__nmnttab-mntidx)*sizeof(struct mnttab));
		} else {
			mt = __mnttab+__nmnttab;
		}
		__nmnttab += 1;

		/*
		 * Fill in slot with name
		 */
		mt->m_name = strdup(point);
		mt->m_len = strlen(mt->m_name);
		mt->m_entries = 0;
	}

	/*
	 * Our entry to the slot
	 */
	me->m_next = mt->m_entries;
	mt->m_entries = me;

	return(fd);
}

/*
 * mount()
 *	Mount port from the given lookup onto the named point
 */
mount(char *point, char *what)
{
	int fd;

	/*
	 * Do initial open.
	 */
	if ((fd = open(what, O_READ)) < 0) {
		return(-1);
	}

	/*
	 * Let mountport do the rest
	 */
	return(mountport(point, __fd_port(fd)));
}

/*
 * umount()
 *	Delete given entry from mount list
 *
 * If fd is -1, all mounts at the given point are removed.  Otherwise
 * only the mount with the given port (fd) will be removed.  XXX we
 * need to hunt down the FDL entry as well.
 */
umount(char *point, port_t fd)
{
	int x;
	struct mnttab *mt;
	struct mntent *me, *men;

	/*
	 * Scan mount table for this string
	 */
	for (x = 0; x < __nmnttab; ++x) {
		mt = &__mnttab[x];
		if (!strcmp(point, mt->m_name)) {
			break;
		}
	}

	/*
	 * Not found--fail
	 */
	if (x >= __nmnttab) {
		return(-1);
	}

	/*
	 * If "fd" given, look for particular slot
	 */
	if (fd >= 0) {
		struct mntent **mp;

		mp = &mt->m_entries;
		for (me = mt->m_entries; me; me = me->m_next) {
			/*
			 * When spotted, patch out of list.
			 */
			if (me->m_fd == fd) {
				*mp = me->m_next;
				free(me);

				/*
				 * If mnttab slot now empty, drop down
				 * below to clean it up.
				 */
				if (mt->m_entries == 0) {
					break;
				}
				return(0);
			}
		}
	}

	/*
	 * Dump all entries, remove mnttab slot
	 */
	for (me = mt->m_entries; me; me = men) {
		men = me->m_next;
		msg_disconnect(me->m_fd);
		free(me);
	}
	free(mt->m_name);
	__nmnttab -= 1;
	bcopy(mt+1, mt, __nmnttab - (mt-__mnttab));
	return(0);
}
