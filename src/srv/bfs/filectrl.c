/*
 * Filename:	filectrl.c
 * Developed:	Dave Hudson <dave@humbug.demon.co.uk>
 * Originated:	Andy Valencia
 * Last Update: 23rd February 1994
 * Implemented:	GNU GCC version 2.5.7
 *
 * Description:	Routines for managing file details and file system allocation
 * space.
 *
 * We handle directory entries (via the block cache), block allocation and
 * inodes.  The inodes are not stored as part of the file system but are
 * only present in memory.  This is because they contain a lot of information
 * that simplifies the operation of the fs, but which can be regenerated when
 * the fs is started.  This is a slightly unusual approach, but one that is
 * valid for a simple fs that can only have a maximum of a few hundred files
 */


#include <std.h>
#include <stdio.h>
#include <sys/assert.h>
#include "bfs.h"


extern struct super *sblock;	/* Pointer to the fs superblock */
static struct inode **ilist;	/* List of all inodes in the fs */
extern void *shandle;
static struct inode *spc_inode;	/* Inode managing the fs free space */


/*
 * ino_ref()
 *	Bump reference on inode object
 */
void
ino_ref(struct inode *i)
{
	ASSERT_DEBUG(i != NULL, "bfs: ino_ref: null inode");
	i->i_refs++;
	ASSERT_DEBUG(i->i_refs != 0, "bfs ino_ref: wrapped");
}


/*
 * ino_deref()
 *	Bump reference down on inode object
 */
void
ino_deref(struct inode *i)
{
	ASSERT_DEBUG(i != NULL, "bfs: ino_ref: null inode");
	ASSERT_DEBUG(i->i_refs != 0, "bfs ino_deref: wrapped");
	i->i_refs--;
}


/*
 * dir_mapslot()
 *	Work out where an inode should find it's directory data
 *
 * As we map each inode directly to it's directory slot we can vastly
 * simplify things and allow this sort of operation.  It wouldn't be this
 * simple in a hierarchichal fs!  All we do here is write this extra info
 * into the inode structure.  We assume that the i_num field has already
 * been set correctly.
 */
static void dir_mapslot(struct inode *i)
{
	int x, dpb;
	
	x = i->i_num;
	ASSERT_DEBUG(x < sblock->s_ndirents, "bfs dir_mapslot: inode overrun");
	dpb = BLOCKSIZE / sblock->s_direntsize;
	i->i_dirblk = sblock->s_dirstart + x / dpb;
	i->i_diroff = (x % dpb) * sblock->s_direntsize; 
}


/*
 * ino_lookup()
 *	Scan inode/directory entries for given name
 *
 * Return a pointer to an inode entry if we find an entry with a
 * matching name, otherwise we return NULL
 */
struct inode *
ino_lookup(char *name)
{
	int x;
	struct inode *si;
	
	for (x = 0; x < sblock->s_ndirents; x++) {
		si = ilist[x];
		if (si != NULL) {
			if (!strcmp(name, si->i_name)) {
				return si;
			}
		}
	}
	return NULL;
}


/*
 * ino_copy()
 *	Given an inode number, make a snapshot of the current inode entry
 *
 * Returns 0 on success, 1 on bad inode number and 2 on a NULL inode.
 */
int
ino_copy(int inum, struct inode *i)
{
	if ((inum < 0) || (inum >= sblock->s_ndirents)) {
		return 1;
	}
	
	if (ilist[inum] == NULL ) {
		return 2;
	}
	*i = *ilist[inum];
	return 0;
}


/*
 * ino_find()
 *	Given an inode number, return an inode pointer
 */
struct inode *
ino_find(uint inum)
{
	return ilist[inum];
}


/*
 * ino_new()
 *	Create a new inode entry
 *
 * Return a pointer to the inode on success, NULL otherwise.
 */
struct inode *
ino_new(char *name)
{
	int x;
	struct inode *i;

	for (x = 0; x < sblock->s_ndirents; x++) {
		i = ilist[x];
		if (i == NULL) {
			/*
			 * We have a free inode so we create a new one!
			 */
			i = (struct inode *)malloc(sizeof(struct inode));
			ilist[x] = i;
			i->i_num = x;
			dir_mapslot(i);
			if (strlen(name) > BFSNAMELEN - 1) {
				/*
				 * Truncate the filename if it's too long
				 */
				name[BFSNAMELEN - 1] = '\0';
			}
			memset(i->i_name, '\0', BFSNAMELEN);
			strcpy(i->i_name, name);
			i->i_refs = 0;
			i->i_start = 0;
			i->i_blocks = 0;
			i->i_fsize = 0;
			i->i_next = I_FREE;
			i->i_prev = I_FREE;
			return i;
		}
	}

	return NULL;		/* Exit on failure */
}
 
 
/*
 * ino_clear()
 *	Indicate that an inode is no longer needed and can be cleared down.
 *
 * Before we erase anything we check that the inode is not in use elsewhere
 * as we'd like to keep it if it is still needed :-)  We shouldn't have
 * any blocks allocated to the inode when we get here - they should have
 * already been "blk_trunc"'d!
 */
void
ino_clear(struct inode *i)
{
	ASSERT_DEBUG(i->i_num != ROOTINODE, "bfs ino_clear: root inode");
	ASSERT_DEBUG(i->i_prev == I_FREE, "bfs: ino_clear: i_prev used");
	ASSERT_DEBUG(i->i_next == I_FREE, "bfs: ino_clear: i_next used");

	if (i->i_refs > 0)
		return;

	ilist[i->i_num] = NULL;
	free(i);
}


/*
 * ino_dirty()
 *	Purge a directory entry back to the fs
 * 
 * Whenever we have completed a series of modifications to some inode
 * data we need to write them back to the disk in the directory.
 */
void
ino_dirty(struct inode *i)
{
	struct dirent *d;
	void *handle;
	
	/*
	 * First off grab the directory info off the disk
	 */
	handle = bget(i->i_dirblk);
	if (handle == NULL ) {
		perror("bfs ino_dirty");
		exit(1);
	}
	d = (struct dirent *)((char *)bdata(handle) + i->i_diroff);

	/*
	 * Then write the new information into the buffer
	 */
	memcpy(d->d_name, i->i_name, BFSNAMELEN);
	d->d_inum = i->i_num;
	d->d_start = i->i_start;
	d->d_len = i->i_fsize;

	/*
	 * Then mark the buffer dirty and free the space
	 */
	bdirty(handle);
	bfree(handle);
}


/*
 * ino_addblklist()
 *	Sort out the specified inode's block list references
 *
 * We assume that the callers to this routine know what they're doing and
 * only try to insert valid inode data into the list.  We essentially need
 * to know the inode number and it's starting block number
 */
static void
ino_addblklist(struct inode *i)
{
	struct inode *srch;
	int found = 0;

	srch = ilist[ROOTINODE];
	while ((srch->i_next != I_FREE) && !found) {
		srch = ilist[srch->i_next];
		if (srch->i_start > i->i_start) {
			/*
			 * We've found the entry after where we want to
			 * insert in the list, so flag it and go back one
			 */
			found = 1;
			srch = ilist[srch->i_prev];
		}
	}

	i->i_next = srch->i_next;
	i->i_prev = srch->i_num;
	i->i_blocks = srch->i_blocks - (i->i_start - srch->i_start);
	srch->i_blocks = (i->i_start - srch->i_start);
	if (i->i_next != I_FREE) {
		ilist[i->i_next]->i_prev = i->i_num;
	} else {
		/*
		 * We've found the inode that's managing the free space.
		 * Ensure that this is advertised
		 */
		spc_inode = i;
	}
	srch->i_next = i->i_num;
}


/*
 * ino_init()
 *	Initialize inode list
 *
 * We allocate one pointer for every inode in the fs.  This would be
 * inefficient for a general purpose fs, but we're never going to see that
 * many inodes so we'll just carry on as if nothing's wrong :-)
 *
 */
void
ino_init(void)
{
	int x, icount;
	struct inode *i;
	void *handle;
	struct dirent *d;

	icount = sblock->s_ndirents;

	ilist = (struct inode **)malloc(sizeof(struct inode *) * icount);
	if (ilist == NULL) {
		perror("bfs iinit ilist");
		exit(1);
	}
	for (x = 1; x < icount; x++) {
		ilist[x] = NULL;
	}

	/*
	 * We need to allocate the root inode for the fs now as it contains
	 * all of the block usage data for an empty filesystem
	 */
	ilist[ROOTINODE] = i = (struct inode *)malloc(sizeof(struct inode));
	if (i == NULL) {
		perror("bfs iinit ilist[ROOTINODE]");
		exit(1);
	}
	i->i_num = 0;
	dir_mapslot(i);
	memset(i->i_name, '\0', BFSNAMELEN);
	i->i_refs = 0;
	i->i_start = sblock->s_dirstart;;
	i->i_blocks = sblock->s_datablocks + sblock->s_dirblocks;
	i->i_fsize = BLOCKSIZE * sblock->s_dirblocks;
	i->i_next = I_FREE;
	i->i_prev = I_FREE;
	spc_inode = i;

	/*
	 * We now need to scan all of the directory space on the disk and
	 * establish what all of the inode contents should be
	 */
	for (x = 1; x < icount; x++) {
		/*
		 * We first make space for a prospective inode entry
		 */
		i = (struct inode *)malloc(sizeof(struct inode));
		if (i == NULL ) {
			perror("bfs init ilist[x]");
			exit(1);
		}
		i->i_num = x;
		dir_mapslot(i);

		/*
		 * We now scan the directory entry on the disk
		 */
		handle = bget(i->i_dirblk);
		if (handle == NULL ) {
			perror("bfs ino_init blk handle");
			exit(1);
		}
		d = (struct dirent *)((char *)bdata(handle) + i->i_diroff);
		if (d->d_name[0] != '\0') {
			strcpy(i->i_name, d->d_name);
			i->i_start = d->d_start;
			i->i_fsize = d->d_len;
			if (i->i_num != d->d_inum) {
				perror("bfs ino_init inode mismatch");
				exit(1);
			}
			ilist[x] = i;

			/*
			 * Next we need to check whether this inode entry
			 * needs to be inserted into the list of allocated
			 * blocks of memory
			 */
			if (i->i_start != 0) {
				ino_addblklist(i);
			}
		} else {
			free(i);
		}
	}
}


/*
 * blk_trunc()
 *	Remove the blocks from under the named file
 *
 * The freed blocks are allocated back onto the previous inode's managed
 * list.  Note that we don't mark the inode dirty.  This is because the
 * caller will often *further* dirty the inode, so no need to do it twice.
 */
void
blk_trunc(struct inode *i)
{
	int blocks;

	/*
	 * Add blocks worth of storage back onto free count
	 */
	blocks = BLOCKS(i->i_fsize);
	sblock->s_free += blocks;

	/*
	 * Flag start/len as 0.  blk_alloc() will notice this
	 * and update where the free blocks start.
	 */
	i->i_start = i->i_fsize = 0;
	bdirty(shandle);

	/*
	 * Sort out all references to our former block neighbours
	 */
	if (i->i_prev != I_FREE) {
		ilist[i->i_prev]->i_next = i->i_next;
		ilist[i->i_prev]->i_blocks += i->i_blocks;
		if (i == spc_inode) {
			spc_inode = ilist[i->i_prev];
		}
	}
	if (i->i_next != I_FREE ) {
		ilist[i->i_next]->i_prev = i->i_prev;
	}

	/*
	 * Finally remove references to our former neighbours
	 */
	i->i_next = I_FREE;
	i->i_prev = I_FREE;
}


/*
 * blk_alloc()
 *	Request an existing file have its allocation increased
 *
 * Returns 0 on success, 1 on failure.
 */
int
blk_alloc(struct inode *i, uint newsize)
{
	/*
	 * If the start is 0, it's "truncated" or new, so we update
	 * it to the current place our free blocks start, and put it into
	 * the block allocation list.
	 */
	if (i->i_start == 0) {
		ASSERT_DEBUG(i->i_fsize == 0, "blk_alloc: trunc with length");
		i->i_start = spc_inode->i_start 
				+ BLOCKS(spc_inode->i_fsize);
		ino_addblklist(i);
	}

	/*
	 * We now decide what alterations need to be made to the block
	 * allocations within the inode.  First we check that we have enough
	 * space available to allocate the required data
	 */
	if (BLOCKS(newsize) > i->i_blocks) {
		/*
		 * We really ought to be far more intelligent about what
		 * we do here - my ideas so far are to first try and
		 * grab some space from a near neighbour - if one has some.
		 * Next we try moving the file (or if a neighbour's smaller
		 * and freeing the space will be enough we move the
		 * neighbour) to the largest free slot available (if that's
		 * big enough) and finally if all else fails we'll compact
		 * the whole fs down!
		 */
		return 1;
	}

	/*
	 * OK, so there's enough space to allocate the required blocks.
	 * Now we juggle the block details within the inode to reflect the
	 * changes
	 */
	i->i_fsize = newsize;

	/*
	 * Mark the inode dirty.  Superblock probably also dirty; mark it so.
	 */
	ino_dirty(i);
	bdirty(shandle);

	return 0;
}
