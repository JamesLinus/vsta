/*
 * mount.c
 *	Routines for manipulating mount table
 */
#include <mnttab.h>
#include <std.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/fs.h>

struct mnttab *__mnttab;
int __nmnttab = 0;

/*
 * mountport()
 *	Like mount(), but mount on given port
 */
mountport(char *point, port_t port)
{
	int x;
	struct mnttab *mt;
	struct mntent *me;

	/*
	 * Scan mount table for this point
	 */
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
	}

	/*
	 * Get memory for mntent
	 */
	me = malloc(sizeof(struct mntent));
	if (!me) {
		return(-1);
	}
	me->m_port = port;

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
			return(-1);
		}
		__mnttab = mt;

		/*
		 * Add new mount slot to end of table
		 */
		mt += __nmnttab;
		__nmnttab += 1;

		/*
		 * Fill in slot with name
		 */
		mt->m_name = strdup(point);
		if (mt->m_name == 0) {
			/*
			 * What a really great time to run out of memory.
			 * Leave the mount table at its new size; malloc
			 * can handle this next time around.
			 */
			__nmnttab -= 1;
			free(me);
			return(-1);
		}
		mt->m_len = strlen(mt->m_name);
		mt->m_entries = 0;
	}

	/*
	 * Add our entry to the slot
	 */
	me->m_next = mt->m_entries;
	mt->m_entries = me;

	return(0);
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
	if (mountport(point, __fd_port(fd)) < 0) {
		close(fd);
		return(-1);
	}
	return(fd);
}

/*
 * umount()
 *	Delete given entry from mount list
 *
 * If fd is -1, all mounts at the given point are removed.  Otherwise
 * only the mount with the given port (fd) will be removed.  XXX we
 * need to hunt down the FDL entry as well.
 */
umount(char *point, int fd)
{
	int x;
	struct mnttab *mt;
	struct mntent *me, *men;
	port_t port;

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

		port = __fd_port(fd);
		mp = &mt->m_entries;
		for (me = mt->m_entries; me; me = me->m_next) {
			/*
			 * When spotted, patch out of list.
			 */
			if (me->m_port == port) {
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
		msg_disconnect(me->m_port);
		free(me);
	}
	free(mt->m_name);
	__nmnttab -= 1;
	bcopy(mt+1, mt, __nmnttab - (mt-__mnttab));
	return(0);
}

/*
 * __mount_size()
 *	Tell how big the save state of the mount table would be
 */
ulong
__mount_size(void)
{
	ulong len;
	uint x;
	struct mnttab *mt;

	/*
	 * Count of mnttab slots
	 */
	len = sizeof(ulong);

	/*
	 * For each mount table slot
	 */
	for (x = 0; x < __nmnttab; ++x) {
		struct mntent *me;

		mt = &__mnttab[x];
		len += (strlen(mt->m_name)+1);
		len += sizeof(uint);	/* Count of mntent's here */
		for (me = mt->m_entries; me; me = me->m_next) {
			len += sizeof(port_t);
		}
	}
	return(len);
}

/*
 * __mount_save()
 *	Save mount table state into byte array
 */
void
__mount_save(char *p)
{
	uint x, l;
	struct mnttab *mt;

	/*
	 * Count of mnttab slots
	 */
	*(ulong *)p = __nmnttab;
	p += sizeof(ulong);

	/*
	 * For each mount table slot
	 */
	for (x = 0; x < __nmnttab; ++x) {
		struct mntent *me;
		ulong *lp;

		mt = &__mnttab[x];

		/*
		 * Copy in string
		 */
		l = strlen(mt->m_name)+1;
		bcopy(mt->m_name, p, l);
		p += l;

		/*
		 * Record where mntent count will go
		 */
		lp = (ulong *)p;
		p += sizeof(ulong);
		l = 0;

		/*
		 * Scan mntent's, storing port # in place
		 */
		for (me = mt->m_entries; me; me = me->m_next) {
			*(port_t *)p = me->m_port;
			p += sizeof(port_t);
			l += 1;
		}

		/*
		 * Back-patch mntent count now that we know
		 */
		*lp = l;
	}
}

/*
 * __mount_restore()
 *	Restore mount state from byte array
 */
char *
__mount_restore(char *p)
{
	ulong x, len;
	uint l;
	struct mnttab *mt;

	/*
	 * Count of mnttab slots
	 */
	__nmnttab = len = *(ulong *)p;
	p += sizeof(ulong);

	/*
	 * Get mnttab
	 */
	__mnttab = malloc(sizeof(struct mnttab) * len);
	if (__mnttab == 0) {
		abort();
	}

	/*
	 * For each mount table slot
	 */
	for (x = 0; x < len; ++x) {
		struct mntent *me, **mp;
		uint y;

		mt = &__mnttab[x];

		/*
		 * Copy in string
		 */
		l = strlen(p)+1;
		mt->m_name = malloc(l);
		if (mt->m_name == 0) {
			abort();
		}
		bcopy(p, mt->m_name, l);
		p += l;

		/*
		 * Get mntent count
		 */
		l = *(ulong *)p;
		p += sizeof(ulong);

		/*
		 * Generate mntent's
		 */
		mp = &mt->m_entries;
		for (y = 0; y < l; ++y) {
			/*
			 * Get next mntent
			 */
			me = malloc(sizeof(struct mntent));
			if (me == 0) {
				abort();
			}

			/*
			 * Tack onto linked list
			 */
			me->m_port = *(port_t *)p;
			*mp = me;
			mp = &me->m_next;
			p += sizeof(port_t);
		}

		/*
		 * Terminate with null
		 */
		*mp = 0;
	}
	return(p);
}

/*
 * init_mount()
 *	Read mntrc, add any mount entries described within
 */
void
init_mount(char *mntrc)
{
	FILE *fp;
	char *p, buf[80];
	char *sympath, *point;
	port_name name;
	port_t port;

	if ((fp = fopen(mntrc, "r")) == 0) {
		return;
	}
	while (fgets(buf, sizeof(buf), fp)) {
		/*
		 * Ignore comment lines
		 */
		buf[strlen(buf)-1] = '\0';
		if ((buf[0] == '\0') || (buf[0] == '#')) {
			continue;
		}

		/*
		 * Carve out first field--path to name in namer
		 * database.
		 */
		sympath = buf;
		p = strchr(buf, ':');
		if (p == 0) {
			continue;
		}

		/*
		 * Second field is where to mount
		 */
		*p++ = '\0';
		point = p;

		/*
		 * Look up port #
		 */
		name = namer_find(sympath);
		if (name < 0) {
			printf("Unknown resource: %s\n", sympath);
			continue;
		}

		/*
		 * Connect to server
		 */
		port = msg_connect(name, ACC_READ);
		if (port < 0) {
			printf("Can't connect to: %s\n", sympath);
		}

		/*
		 * Mount it
		 */
		mountport(point, port);
	}
}
