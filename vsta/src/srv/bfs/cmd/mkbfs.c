/*
 * Filename:	mkbfs.c
 * Originated:	Andy Valencia
 * Updated By:	Dave Hudson <dave@humbug.demon.co.uk>
 * Last Update: 8th April 1994
 * Implemented:	GNU GCC version 2.5.7
 *
 * Description: Create a bfs filesystem
 *
 * We assume we can just fopen(..., "w") the named file, and make ourselves
 * a filesystem.  We writes all of the blocks if a fs size is specified on the
 * command line, which is probably not desirable for a "mkfs" util.  This does
 * however allow a bfs to be built on top of an existing fs - very useful for
 * avoiding repartitioning, or for running bfs where no fs partitioning
 * mechanism exists (PC FDDs).  If no blocks parameter is spec'd we try and
 * determine the fs size automagically!
 */


#include <fdl.h>
#include <stat.h>
#include <std.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "../bfs.h"


void
main(int argc, char **argv)
{
	FILE *fp;
	int fd;
	int write_all_blocks = 0;
	struct super sb;
	struct dirent d;
	uchar block[BLOCKSIZE];
	int x, nblocks, zblocks, dblocks = 8;
	char *statstr;

	if (argc != 3 && argc != 2) {
		fprintf(stderr,
			"Usage: %s <device_or_fs_file> [number_of_blocks]\n",
			argv[0]);
		exit(1);
	}

	if ((fp = fopen(argv[1], "wb")) == NULL) {
		perror(argv[1]);
		exit(1);
	}

	/*
	 * How many fs blocks are we going for?
	 */
	if (argc == 2) {
		/*
		 * Try to work out the number of fs blocks via a stat call
		 */
		fd = fileno(fp);
		statstr = rstat(__fd_port(fd), "size");
		if (statstr == NULL ) {
			fprintf(stderr,
				"Unable to stat size of: %s\n", argv[1]);
			exit(1);
		}
		sscanf(statstr, "%d", &nblocks);
		nblocks /= BLOCKSIZE;
	} else {
		/*
		 * Get the number of fs blocks off the command line
		 */
		if (sscanf(argv[2], "%d", &nblocks) != 1) {
			fprintf(stderr,
				"Illegal number of blocks: %s\n", argv[2]);
			exit(1);
		}
		write_all_blocks = 1;
	}

	printf("Creating a 'bfs' fs on %s of %d blocks\n", argv[1], nblocks);

	/*
	 * Create superblock details and run sanity checks
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

	if (sb.s_datablocks < MINDATABLOCKS) {
		fprintf(stderr,
			"Too few data blocks (%d) with %d fs blocks\n",
			sb.s_datablocks, nblocks);
		exit(1);
	}

	/*
	 * Zero out the superblock, dir space and possibly all of the fs space
	 */
	memset(block, '\0', BLOCKSIZE);
	zblocks = write_all_blocks ? nblocks : sb.s_supblocks + dblocks;
	for (x = 0; x < zblocks; ++x) {
		fseek(fp, x * BLOCKSIZE, SEEK_SET);
		if (fwrite(block, sizeof(char), BLOCKSIZE, fp)
		    		!= BLOCKSIZE) {
			perror(argv[1]);
			exit(1);
		}
	}
	fseek(fp, 0, SEEK_SET);

	/*
	 * Write the superblock
	 */
	memcpy(block, &sb, sizeof(struct super));
	if (fwrite(block, BLOCKSIZE, 1, fp) != 1) {
		perror(argv[1]);
		exit(1);
	}
	fflush(fp);

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
		if (fwrite(&d, sizeof(d), 1, fp) != 1) {
			perror(argv[1]);
			exit(1);
		}
	}

	fclose(fp);
}
