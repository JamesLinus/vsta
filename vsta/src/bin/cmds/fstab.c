/*
 * Filename:	fstab.c
 * Author:	Dave Hudson <dave@humbug.demon.co.uk>
 * Started:	1st May 1994
 * Last Update: 2nd May 1994
 * Implemented:	GNU GCC 1.42 (VSTa v1.3.1 port)
 *
 * Description:	Utility to read the current mount table.  Based on an idea
 *		from Andy Valencia's testsh builtin command "fstab".
 */
#include <mnttab.h>
#include <stdio.h>
#include <sys/msg.h>

/*
 * External variables - these provide the details of the mounted ports
 */
static struct mnttab *__mnttab;
static int __nmnttab;

/*
 * usage()
 *	When the user's tried something illegal we tell them what was valid
 */
static void
usage(char *util_name)
{
	fprintf(stderr, "Usage: %s [-f | -p]\n", util_name);
	exit(1);
}

/*
 * list_by_fd()
 *	List the mounted ports by file descriptor
 */
static void
list_by_fd(void)
{
	struct mnttab *m;
	struct mntent *me;

	for (m = __mnttab; m < __mnttab + __nmnttab; m++) {
		for (me = m->m_entries; me; me = me->m_next) {
			printf("%d ", me->m_port);
		}
		printf("on %s\n", m->m_name);
	}
}

/*
 * list_by_portname()
 *	List the mounted ports by "portname" number
 */
static void
list_by_portname(void)
{
	struct mnttab *m;
	struct mntent *me;

	for (m = __mnttab; m < __mnttab + __nmnttab; m++) {
		for (me = m->m_entries; me; me = me->m_next) {
			printf("%d ", msg_portname(me->m_port));
		}
		printf("on %s\n", m->m_name);
	}
}

/*
 * main()
 *	Sounds like a good place to start things :-)
 */
int
main(int argc, char **argv)
{
	/*
	 * Scan for command line options
	 */
	if (argc > 2) {
		usage(argv[0]);
	}

	/*
	 * Get library pointers for mount table
	 */
	__get_mntinfo(&__nmnttab, &__mnttab);

	if (argc == 1) {
		/*
		 * Handle the default condition
		 */
		list_by_portname();
	}

	if (argv[1][0] != '-') {
		usage(argv[0]);
	}

	switch(argv[1][1]) {
	case 'p' :			/* List by portname */
		list_by_portname();
		break;

	case 'f' :			/* List by fd */
		list_by_fd();
		break;

	default :			/* Unknown parameter */
		usage(argv[0]);
		break;
	}

	return(0);
}
