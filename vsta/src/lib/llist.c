/*
 * llist.c
 *	Routines for doing linked list operations
 */
#include <llist.h>

/*
 * ll_init()
 *	Initialize list head
 */
void
ll_init(struct llist *l)
{
	l->l_forw = l->l_back = l;
#ifdef DEBUG
	l->l_data = 0;
#endif
}

/*
 * ll_insert()
 *	Insert datum at given place in list
 *
 * Returns new node, or 0 for failure
 */
struct llist *
ll_insert(struct llist *l, void *d)
{
	struct llist *lnew;
	struct llist *l_back = l->l_back;
	extern void *malloc();

	lnew = malloc(sizeof(struct llist));
	if (lnew == 0)
		return 0;
	lnew->l_data = d;

	lnew->l_forw = l;
	lnew->l_back = l_back;
	l_back->l_forw = lnew;
	l->l_back = lnew;
	return(lnew);
}

/*
 * ll_delete()
 *	Remove node from linked list, free storage
 */
void
ll_delete(struct llist *l)
{
	struct llist *l_forw = l->l_forw;
	struct llist *l_back = l->l_back;

	l_back->l_forw = l_forw;
	l_forw->l_back = l_back;
#ifdef DEBUG
	l->l_forw = l->l_back = 0;
	l->l_data = 0;
#endif
	free(l);
}

/*
 * ll_movehead()
 *	Move head of list to place within
 */
void
ll_movehead(struct llist *head, struct llist *l)
{
	struct llist *l_back = l->l_back;
	struct llist *l_forw = l->l_forw;
	struct llist *head_forw = head->l_forw;

	l_back->l_forw = l_forw;
	l_forw->l_back = l_back;
	l->l_forw = head_forw;
	l->l_back = head;
	head_forw->l_back = l;
	head->l_forw = l;
}
