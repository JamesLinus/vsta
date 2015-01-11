/*
 * insque.h - C library insque()/remque() definitions.
 */

#ifndef	__INSQUE_H__
#define	__INSQUE_H__

struct	q_header {
	struct	q_header *q_forw, *q_back;
};

struct	q_elem {
	struct	q_elem *q_forw, *q_back;
	char	q_data[1];
};

#ifdef	__STDC__
void	insque(struct q_header *element, struct q_header *pred);
void	remque(struct q_header *element);
#endif

#endif	/*__INSQUE_H__*/

