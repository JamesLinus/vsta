/*
 * shlib.c
 *	Boot loader
 *
 * To allow future policy on library searches to remain open, we
 * use a two-step process for loading shared libraries.  The first
 * step dynamically maps code in from the filesystem and runs it.
 * This code then takes whatever actions are needed to load the ultimate
 * library based on its name.  As always, everything in computer
 * science is solved with an extra level of indirection.
 *
 * Because this code runs without any other library available, it can
 * call only certain system calls and its own private routines.
 *
 * On success, it returns the address at which the library was mapped.
 * On failure, it returns 0.
 */
#include <sys/fs.h>
#include <sys/ports.h>
#include <sys/mman.h>
#include <mach/aout.h>

/*
 * strlen()
 *	Length of string
 */
static int
strlen(char *p)
{
	int x = 0;

	while (*p++)
		++x;
	return(x);
}

/*
 * atoi()
 *	Convert ASCII to integer
 */
static int
atoi(char *p)
{
	int val = 0;
	char c;

	while (c = *p++) {
		val = val*10 + (c - '0');
	}
	return(val);
}

/*
 * walk()
 *	Walk a level down on an open port
 *
 * Returns 1 on failure, 0 on success.
 */
static int
walk(port_t port, char *name)
{
	struct msg m;

	m.m_op = FS_OPEN;
	m.m_buf = name;
	m.m_buflen = strlen(name)+1;
	m.m_nseg = 1;
	m.m_arg = ACC_READ;
	m.m_arg1 = 0;
	if (msg_send_shl(port, &m) < 0) {
		return(1);
	}
	return(0);
}

/*
 * receive()
 *	Get some data out of the named port
 *
 * Returns 1 on failure, 0 on success.  On a read shorter than
 * requested, the buffer is null-terminated.
 */
static int
receive(port_t port, void *buf, int len)
{
	struct msg m;
	int x;

	m.m_op = FS_READ | M_READ;
	m.m_buf = buf;
	m.m_buflen = len;
	m.m_nseg = 1;
	m.m_arg = len;
	m.m_arg1 = 0;
	x = msg_send_shl(port, &m);
	if (x < 0) {
		return(1);
	}
	if (x < len) {
		((char *)buf)[x] = '\0';
	}
	return(0);
}

/*
 * _load()
 *	First-level shlib loader
 */
void *
_load(char *p)
{
	port_t port;
	port_name rootname;
	struct aout aout;
	void *addr, *addr2;
	char buf[16];
	int x;
	void *(*loadfn)();
	extern void *_mmap_shl();

	/*
	 * Open /namer/fs/root, get the port_name for the root
	 * filesystem.
	 */
	port = msg_connect_shl(PORT_NAMER, ACC_READ);
	if (port < 0) {
		return(0);
	}
	x = walk(port, "fs") || walk(port, "root") ||
			receive(port, buf, sizeof(buf));
	msg_disconnect_shl(port);
	if (x) {
		return(0);
	}
	rootname = (port_name)atoi(buf);

	/*
	 * Open vsta/lib/load.shl from the root filesystem; this is
	 * our primary shlib loader.  Read the a.out header from it
	 * to find out how big it is & where it wants to run.
	 */
	port = msg_connect_shl(rootname, ACC_READ);
	x = walk(port, "vsta") || walk(port, "lib") ||
			walk(port, "ld.shl") ||
			receive(port, &aout, sizeof(aout));
	if (x) {
		msg_disconnect_shl(port);
		return(0);
	}

	/*
	 * Map load.shl into its desired location.  This is a text-only
	 * object which holds all its data on its stack.
	 * An error here likely indicates that the boot loader is
	 * already in the address space.  If the boot loader library
	 * is, in fact, absent from the system, we will find
	 * virtually nothing operational anyway.  So we ignore
	 * the error return.
	 */
	addr = (void *)(aout.a_entry - sizeof(struct aout));
	(void)_mmap_shl(addr, sizeof(aout) + aout.a_text,
		PROT_READ, MAP_FILE, port, 0L);
	msg_disconnect_shl(port);

	/*
	 * Call loader with desired module
	 */
	loadfn = *(void **)aout.a_entry;
	addr2 = (*loadfn)(rootname, p);

	/*
	 * Clean up loader mapping, return address adjusted
	 * to point to the jump table.
	 */
	munmap_shl(addr, sizeof(aout) + aout.a_text);
	if (addr2 == 0) {
		_notify_shl(0, 0, "kill", 4);
		return(0);
	}
	return((char *)addr2 + sizeof(aout));
}
