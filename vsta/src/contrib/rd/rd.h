#ifndef _RD_SERVER_H
#define _RD_SERVER_H

#define MAX_DISKS	16

#define NODE_ROOT 0x01
#define NODE_VALID 0x02
#define NODE_LOAD  0x04

struct node {

	char n_name[NAMESZ];	/* name of this node */
	ushort n_flags;	

	/* if not root */
	char n_image[MAXPATH]; /* image file to load */
	uint n_size;	/* size of ramdisk */
	char *n_buf;    /* there is is */
};

#define IS_DIR(n) ((n)->n_flags & NODE_ROOT)

struct file {
	uint f_gen;
	uint f_flags;
	struct node *f_node;
	off_t f_pos;
};

void rd_stat(struct msg *m, struct file *f);
void rd_rw(struct msg *m, struct file *f);
void rd_open(struct msg *m, struct file *f);
void rd_load(struct node *n);

#endif /* _RD_SERVER_H */
