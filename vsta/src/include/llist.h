#ifndef _LLIST_H
#define _LLIST_H
/*
 * llist.h
 *	Data structure for doing doubly-linked lists
 */

/*
 * Structure of both list head (l_data not used) and list elements
 */
struct llist {
	struct llist *l_forw,
		*l_back;
	void *l_data;
};

/*
 * ll_init()
 *	Initialize list head
 */
void ll_init(struct llist *);

/*
 * ll_insert()
 *	Insert datum at given place in list
 */
struct llist *ll_insert(struct llist *, void *);

/*
 * ll_delete()
 *	Remove node from linked list, free storage
 */
void ll_delete(struct llist *);

/*
 * ll_movehead()
 *	Move the list head to another place within the list
 *
 * This is needed to keep linked aging lists "fair" by making the
 * scan not keep considering the same things towards the front
 * of he list.
 */
void ll_movehead(struct llist *, struct llist *);

#endif /* _LLIST_H */
