/*
 * Filename:	mkbfs.c
 * Originated:	Andy Valencia
 * Updated By:	Dave Hudson <dave@humbug.demon.co.uk>
 * Last Update: 11th February 1994
 * Implemented:	GNU GCC version 2.5.7
 *
 * Description: Create a BFS filesystem
 *
 * This guy assumes he can just fopen(..., "w") the named file, and make
 * himself a filesystem.  He writes all the blocks, which is probably not
 * desirable for a native mkfs.  This does however allow a bfs to be built
 * on top of an existing fs - very useful for avoiding repartitioning, or
 * for running bfs where no fs partitioning mechanism exists (PC FDDs).
 */


#include <std.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../bfs.h"


void
main(int argc, char **argv)
{
	FILE *fp;
	struct super sb;
	struct dirent d;
	uchar block[BLOCKSIZE];
	int x, nblocks, dblocks = 4;

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
	 * Zero out the whole thing first.
	 */
	memset(block, '\0', BLOCKSIZE);
	for (x = 0; x < nblocks; ++x) {
		if (fwrite(block, sizeof(char), BLOCKSIZE, fp)
		    		!= BLOCKSIZE) {
			perror(argv[1]);
			exit(1);
		}
	}
	fseek(fp, 0, SEEK_SET);

	/*
	 * Create a superblock and write it out
	 */
	sb.s_magic = SMAGIC;
	sb.s_blocks = nblocks;
	sb.s_supstart = 0;
	sb.s_supblocks = BLOCKS(sizeof(struct super));
	sb.s_dirstart = sb.s_supstart + sb.s_supblocks;
	sb.s_dirblocks = dblocks;
	sb.s_datastart = sb.s_dirblocks + sb.s_dirblocks;
	sb.s_datablocks = sb.s_free = nblocks - dblocks - sb.s_supblocks;
	sb.s_nextfree = dblocks + sb.s_dirstart;
	sb.s_ndirents = dblocks * (BLOCKSIZE / sizeof(struct dirent));
	sb.s_direntsize = sizeof(struct dirent);
	if (fwrite(&sb, sizeof(sb), 1, fp) != 1) {
		perror(argv[1]);
		exit(1);
	}

	/*
	 * Write directory entries until we can't fit any more in
	 * sb.s_dirblocks.  No directory entry must cross a block boundary,
	 * so we skip to beginning of next block in case we hit an alignment
	 * problem.
	 */
	x = 0;
	memset(d.d_name, '\0', BFSNAMELEN);
	d.d_start = d.d_len = 0;
	fseek(fp, sb.s_dirstart * BLOCKSIZE, SEEK_SET);
	while ((ftell(fp) + sizeof(d)) <= ((dblocks + 1) * BLOCKSIZE)) {
		int curblk, endblk;

		curblk = ftell(fp) / BLOCKSIZE;
		endblk = (ftell(fp) + sizeof(d) - 1) / BLOCKSIZE;
		if (curblk != endblk) {
			fseek(fp, endblk * BLOCKSIZE, SEEK_SET);
		}
		d.d_inum = x++;
printf("%d %d!  ", d.d_inum, ftell(fp));
		if (fwrite(&d, sizeof(d), 1, fp) != 1) {
			perror(argv[1]);
			exit(1);
		}
	}
}
