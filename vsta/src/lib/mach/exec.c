/*
 * exec.c
 *	Wrapper for kernel exec() function
 */
#include <sys/mman.h>
#include <sys/exec.h>
#include <sys/fs.h>	/* For ENOEXEC */
#include <fcntl.h>
#include <std.h>
#include <mach/aout.h>
#include <sys/param.h>
#include <fdl.h>
#include <mnttab.h>

/*
 * execv()
 *	Execute a file with some arguments
 */
execv(const char *file, char * const *argv)
{
	int fd, x;
	uint plen, fdl_len, mnt_len, cwd_len, sig_len;
	ulong narg;
	struct aout a;
	struct mapfile mf;
	char *p, *args;
	port_t port;

	/*
	 * Open the file we're going to run
	 */
	fd = open(file, O_READ);
	if (fd < 0) {
		return(-1);
	}

	/*
	 * Read the header, verify its magic number
	 */
	if ((read(fd, &a, sizeof(a)) != sizeof(a)) ||
			((a.a_info & 0xFFFF) != 0413)) {
		close(fd);
		__seterr(ENOEXEC);
		return(-1);
	}

	/*
	 * Fill in the mapfile description from the a.out header
	 */
	bzero(&mf, sizeof(mf));

	/* Text */
	mf.m_map[0].m_vaddr = (void *)0x1000;
	mf.m_map[0].m_off = 0;
	mf.m_map[0].m_len = btorp(a.a_text + sizeof(a));
	mf.m_map[0].m_flags = M_RO;

	/* Data */
	mf.m_map[1].m_vaddr = (void *)roundup(
		(ulong)(mf.m_map[0].m_vaddr) + ptob(mf.m_map[0].m_len),
		0x400000);
	mf.m_map[1].m_off = mf.m_map[0].m_len;
	mf.m_map[1].m_len = btorp(a.a_data);
	mf.m_map[1].m_flags = 0;

	/* BSS */
	mf.m_map[2].m_vaddr = (char *)(mf.m_map[1].m_vaddr) +
		a.a_data;
	mf.m_map[2].m_off = 0;
	mf.m_map[2].m_len = btorp(a.a_bss);
	mf.m_map[2].m_flags = M_ZFOD;

	/* Entry point */
	mf.m_entry = (void *)(a.a_entry);

	/*
	 * Assemble arguments into a counted array
	 */
	plen = sizeof(ulong);
	for (narg = 0; argv[narg]; ++narg) {
		plen += (strlen(argv[narg])+1);
	}

	/*
	 * Add in length for fdl state and mount table
	 */
	fdl_len = __fdl_size();
	plen += fdl_len;
	mnt_len = __mount_size();
	plen += mnt_len;
	cwd_len = __cwd_size();
	plen += cwd_len;
	sig_len = __signal_size();
	plen += sig_len;

	/*
	 * Create a shared mmap() area
	 */
	args = p = mmap(0, plen, PROT_READ|PROT_WRITE,
		MAP_ANON|MAP_SHARED, 0, 0L);
	if (p == 0) {
		close(fd);
		return(-1);
	}

	/*
	 * Pack our arguments into it
	 */
	*(ulong *)p = narg;
	p += sizeof(ulong);
	for (narg = 0; argv[narg]; ++narg) {
		uint plen2;

		plen2 = strlen(argv[narg])+1;
		bcopy(argv[narg], p, plen2);
		p += plen2;
	}

	/*
	 * Add in our fdl state
	 */
	__fdl_save(p, fdl_len);
	p += fdl_len;

	/*
	 * And our mount state
	 */
	__mount_save(p);
	p += mnt_len;

	/*
	 * And our CWD
	 */
	__cwd_save(p);
	p += cwd_len;

	/*
	 * And our signal state
	 __signal_save(p);
	 p += sig_len;

	/*
	 * Here we go!  Get the port_t for this file, and close the FDL
	 * state for the port so we don't try to preserve it in the new
	 * executable (where exec() has stolen it).
	 */
	port = __fd_port(fd);
	_fdclose(fd);
	return(exec(port, &mf, args));
}

/*
 * execl()
 *	Alternate interface to exec(), simpler to call
 *
 * Interestingly, the arguments are already in the right format
 * on the stack.  We just use a little smoke and we're there.
 */
execl(const char *file, const char *arg0, ...)
{
	return(execv(file, (char **)&arg0));
}
