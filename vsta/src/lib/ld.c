/*
 * ld.c
 *	Second-level shlib loader
 *
 * This is the second level of loading a shared library.  The first
 * level loads this module, and passes it the name to be found.  This
 * code finds that name, maps it in (along with its copy-on-write data
 * and ZFOD BSS) and returns the address it was mapped at.
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

#define PAGESIZE (4096)		/* XXX should be from a .h */

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
 * _load2()
 *	Second-level shlib loader
 *
 * As an optimization, we're passed the port_name of the root filesystem.
 * This module will generally need this, the first level already looked
 * it up to find *us*, so we save some extra cycles.
 */
void *
_load2(port_name rootname, char *p)
{
	port_t port;
	struct aout aout;
	void *addr_text, *addr_data, *addr_bss;
	ulong size_text;
	int x;
	extern void *_mmap_shl();

	/*
	 * Open vsta/lib/<lib> from the root filesystem
	 * Read the a.out header from it
	 * to find out how big it is & where it wants to run.
	 */
	port = msg_connect_shl(rootname, ACC_READ);
	x = walk(port, "vsta") || walk(port, "lib") ||
			walk(port, p) ||
			receive(port, &aout, sizeof(aout));
	if (x) {
		msg_disconnect_shl(port);
		return(0);
	}

	/*
	 * Map it into its desired location.  This involves mapping
	 * text read-only, data after text read/write copy-on-write,
	 * and BSS right after data with zero-fill-on-demand.
	 */
	size_text = sizeof(aout) + aout.a_text;
	addr_text = _mmap_shl((void *)(aout.a_entry - sizeof(struct aout)),
		size_text, PROT_READ, MAP_FILE, port, 0L);
	if (addr_text == 0) {
		goto err;
	}
	if (aout.a_data > 0) {
		addr_data = _mmap_shl(addr_text + size_text,
			aout.a_data, PROT_READ | PROT_WRITE,
			MAP_FILE | MAP_PRIVATE, port, size_text);
		if (addr_data == 0) {
			goto err;
		}
		if (aout.a_bss > 0) {
			addr_bss = _mmap_shl(addr_data + aout.a_data,
				roundup(aout.a_bss, PAGESIZE),
				PROT_READ | PROT_WRITE,
				MAP_ANON, 0, 0L);
			if (addr_bss == 0) {
				goto err;
			}
		}
	}

	/*
	 * All mapped in, disconnect from server and return base
	 * of the mappings.
	 */
	msg_disconnect_shl(port);
	return(addr_text);

err:
	/*
	 * Failed to load, clean up
	 */
	msg_disconnect_shl(port);
	if (addr_text)
		munmap_shl(addr_text, size_text);
	if (addr_data)
		munmap_shl(addr_data, aout.a_data);
	/* bss_addr can't be non-0 here, ever */
	return(0);
}
