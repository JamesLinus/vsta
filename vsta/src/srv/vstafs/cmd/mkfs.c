/*
 * mkfs.c
 *	Write an initial filesystem image onto the named file
 */
#include <sys/types.h>
#include <stdio.h>
#include "../vstafs.h"

static FILE *fp;	/* Device we're filling in */

/*
 * write_header()
 *	Write the initial sector
 */
static void
write_header(ulong nsec)
{
	struct fs f;

	fseek(fp, (off_t)BASE_SEC*SECSZ, 0);
	f.fs_magic = FS_MAGIC;
	f.fs_size = nsec;
	f.fs_extsize = EXTSIZ;
	f.fs_free = FREE_SEC;
	fwrite(&f, sizeof(struct fs), 1, fp);
}

/*
 * write_root()
 *	Write the root directory--initially, no entries
 */
static void
write_root(void)
{
	struct fs_file fs;

	fseek(fp, (off_t)ROOT_SEC*SECSZ, 0);
	fs.fs_prev = 0;
	fs.fs_rev = 1;
	fs.fs_len = sizeof(struct fs_file);
	fs.fs_type = FT_DIR;
	fs.fs_nlink = 1;
	fs.fs_prot.prot_len = 2;
	fs.fs_prot.prot_default = ACC_READ;
	fs.fs_prot.prot_bits[0] = 0;
	fs.fs_prot.prot_id[0] = 1;
	fs.fs_prot.prot_bits[1] = ACC_WRITE|ACC_CHMOD;
	fs.fs_prot.prot_id[1] = 1;
	fs.fs_owner = 0;
	fs.fs_nblk = 1;
	fs.fs_blks[0].a_start = ROOT_SEC;
	fs.fs_blks[0].a_len = 1;
	fwrite(&fs, sizeof(struct fs_file), 1, fp);
}

/*
 * write_freelist()
 *	Put rest of blocks into initial freelist
 */
static void
write_freelist(ulong nsec)
{
	struct free f;

	fseek(fp, (off_t)FREE_SEC*SECSZ, 0);
	bzero(&f, sizeof(f));
	f.f_next = 0;
	f.f_nfree = 1;
	f.f_free[0].a_start = FREE_SEC+1;
	f.f_free[0].a_len = nsec-(FREE_SEC+1);
	fwrite(&f, sizeof(struct free), 1, fp);
}

int
main(int argc, char **argv)
{
	ulong x, nsec;
	char sec[SECSZ];

	/*
	 * Check/parse args
	 */
	if (argc != 3) {
		printf("Usage is: %s <device> <nsector>\n", argv[0]);
		exit(1);
	}
	if (sscanf(argv[2], "%ld", &nsec) != 1) {
		printf("Bad <nsector>: %s\n", argv[2]);
		exit(1);
	}
	fp = fopen(argv[1], "wb");
	if (fp == 0) {
		perror(argv[1]);
		exit(1);
	}

	/*
	 * Preallocate sectors
	 */
	bzero(sec, SECSZ);
	printf("Pre-allocate: "); fflush(stdout);
	for (x = 0; x < nsec; ++x) {
		fwrite(sec, SECSZ, 1, fp);
		/* Put a marker each 8K */
		if ((x % ((8*1024)/SECSZ)) == 0) {
			printf("."); fflush(stdout);
		}
	}

	/*
	 * Write data structures
	 */
	write_header(nsec);
	write_root();
	write_freelist(nsec);
	fclose(fp);
	return(0);
}
