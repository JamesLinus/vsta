/*
 * runrc.c
 *	Run the /vsta/etc/rc init file in an appropriate environment
 */
#include <sys/perm.h>
#include <std.h>

/*
 * init_env()
 *	Enable our abilities, set up our mount table
 */
static void
init_env(void)
{
	port_t port;
	struct perm perm;
	extern void zero_ids();

	/*
	 * Activate root abilities.  Note that sys.sys is still active
	 * in slot 0.
	 */
	zero_ids(&perm, 1);
	perm.perm_len = 0;
	if (perm_ctl(1, &perm, (void *)0) < 0) {
		printf("runrc: can't enable root\n");
		exit(1);
	}

	/*
	 * Initialize our environment
	 */
	setenv_init("/");

	/*
	 * Mount system default stuff.  Remove redundant mount
	 * of root.
	 */
	port = mount_port("/");
	mount_init("/vsta/etc/fstab");
	umount("/", port);
}

main()
{
	init_env();
	execl("/vsta/bin/sh", "sh", "/vsta/etc/rc", (char *)0);
	perror("sh");
	exit(1);
}
