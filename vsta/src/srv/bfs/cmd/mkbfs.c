/*
 * mkfs.c
 *	Create a BFS filesystem
 *
 * This guy assumes he can just fopen(..., "w") the named file, and
 * make himself a filesystem.  He writes all the blocks, which is
 * probably not desirable for a native mkfs.  However, under DOS,
 * this behavior allows us to test with a DOS file playing the part
 * of a block device.  DOS doesn't get sparse files right, in my
 * experience.
 */
#include <stdio.h>
#include "../bfs.h"

main(argc, argv)
	int argc;
	char **argv;
{
	FILE *fp;
	struct super sb;
	struct dirent d;
	char block[BLOCKSIZE];
	int x, nblocks;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s <device> <blocks>\n", argv[0]);
		exit(1);
	}
	if (sscanf(argv[2], "%d", &nblocks) != 1) {
		fprintf(stderr, "Illegal number: %s\n", argv[2]);
		exit(1);
	}
	if ((fp = fopen(argv[1], "wb")) == NULL) {
		perror(argv[1]);
		exit(1);
	}

	/*
	 * Zero out the whole thing first
	 */
	memset(block, '\0', BLOCKSIZE);
	for (x = 0; x < nblocks; ++x) {
		if (fwrite(block, sizeof(char), BLOCKSIZE, fp) !=
				BLOCKSIZE) {
			perror(argv[1]);
			exit(1);
		}
	}
	rewind(fp);

	/*
	 * Create a superblock, write it out.  Note that the dummy
	 * directory entry on the end is not included.
	 */
	sb.s_magic = SMAGIC;
	sb.s_nblocks = nblocks;
	sb.s_free = nblocks-NDIRBLOCKS;
	sb.s_nextfree = NDIRBLOCKS;
	if (fwrite(&sb, sizeof(sb)-sizeof(struct dirent), 1, fp) != 1) {
		perror(argv[1]);
		exit(1);
	}

	/*
	 * Write directory entries until we can't fit any more in
	 * NDIRBLOCKS.  No directory entry crosses a block boundary,
	 * so skip to beginning of next block in these cases.
	 */
	x = 0;
	memset(d.d_name, '\0', NAMELEN);
	d.d_start = d.d_len = 0;
	while ((ftell(fp)+sizeof(d)) < (NDIRBLOCKS*BLOCKSIZE)) {
		int curblk, nextblk;

		curblk = ftell(fp)/BLOCKSIZE;
		nextblk = (ftell(fp)+sizeof(d))/BLOCKSIZE;
		if (curblk != nextblk) {
			fseek(fp, nextblk*BLOCKSIZE, 0);
		}
		d.d_inum = x++;
		if (fwrite(&d, sizeof(d), 1, fp) != 1) {
			perror(argv[1]);
			exit(1);
		}
	}
}
