#ifndef _NAMER_H
#define _NAMER_H
/*
 * namer.h
 *	Data structures and values for talking to the system namer
 *
 * The namer is responsible for mapping names into port addresses
 * so you can msg_connect() to them.
 */
#include <sys/msg.h>

port_name namer_find(char *);
int namer_register(char *, port_name);

#ifdef _NAMER_H_INTERNAL
#include <sys/types.h>
#include <sys/perm.h>
#include <sys/param.h>
#include <lib/llist.h>

/*
 * An open file
 */
struct file {
	struct node *f_node;	/* Current node we address */
	struct perm		/* Our abilities */
		f_perms[PROCPERMS];
	int f_nperm;		/*  ...# active */
	off_t f_pos;		/* Position in internal node */
	int f_mode;		/* Abilities WRT current node */
};

/*
 * A node in our name tree
 */
struct node {
	struct prot n_prot;	/* Protection of this node */
	char n_name[NAMESZ];	/* Name of node */
	ushort n_internal;	/* Internal node? */
	port_name n_port;	/*  ...if not, port name for leaf */
	struct llist n_elems;	/* Linked list of elements under node */
	uint n_refs;		/* # references to this node */
	struct llist *n_list;	/* Our place in our parent's list */
	uint n_owner;		/* Owner UID */
};

#endif /* _NAMER_H_INTERNAL */

#endif /* _NAMER_H */
