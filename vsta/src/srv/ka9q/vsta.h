#ifndef VSTA_H
#define VSTA_H
/*
 * vsta.h
 *	VSTa-specific definitions
 */
#include <sys/types.h>

/*
 * Our private hack mutex package
 */
typedef unsigned char lock_t;

extern void init_lock(lock_t *), p_lock(volatile lock_t *),
	v_lock(lock_t *);

/*
 * Shared data
 */
extern lock_t ka9q_lock;

/*
 * Functions
 */
extern void vsta_daemon(voidfun), do_mainloop(void),
	vsta_daemon_done(pid_t);

#endif /* VSTA_H */