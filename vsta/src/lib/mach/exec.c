/*
 * exec.c
 *	Wrapper for kernel exec() function
 */
#include <sys/mman.h>
#include <sys/exec.h>
#include <fcntl.h>
#include <std.h>
#include <mach/aout.h>
#include <sys/param.h>

/*
 * execl()
 *	Execute a file with some arguments
 */
execl(char *file, char *arg0, ...)
{
	int fd;
	uint plen;
	ulong narg;
	struct aout a;
	struct mapfile mf;
	char *p, **pp;

	/*
	 * Open the file we're going to run
	 */
	fd = open(file, O_READ);
	if (fd < 0) {
		return(-9);
	}

	/*
	 * Read the header
	 */
	if (read(fd, &a, sizeof(a)) != sizeof(a)) {
		close(fd);
		return(-2);
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
	narg = 0;
	for (pp = &arg0; *pp; ++pp) {
		narg += 1;
		plen += (strlen(*pp)+1);
	}

	/*
	 * Create a shared mmap() area, assemble our arguments
	 */
	p = mmap(0, plen, PROT_READ|PROT_WRITE,
		MAP_ANON|MAP_SHARED, 0, 0L);
	if (p == 0) {
		return(-3);
	}
	*(ulong *)p = narg;
	plen = sizeof(ulong);
	for (pp = &arg0; *pp; ++pp) {
		uint plen2;

		plen2 = strlen(*pp)+1;
		bcopy(*pp, p+plen, plen2);
		plen += plen2;
	}

	/*
	 * Here we go!
	 */
	return(exec(__fd_port(fd), &mf, p));
}
