#ifndef VSTA_H
#define VSTA_H
/*
 * vsta.h
 *	VSTa-specific definitions
 */
#include <sys/types.h>
#include <lock.h>

/*
 * Shared data
 */
extern lock_t ka9q_lock;

/*
 * Functions
 */
extern uint vsta_daemon(voidfun);
extern void vsta_daemon_done(uint);
extern int do_mainloop(void);
extern void stop_kbio(void);

#endif /* VSTA_H */
