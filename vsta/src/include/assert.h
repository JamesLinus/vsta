#ifndef _ASSERT_H
#define _ASSERT_H
/*
 * assert.h
 *	User version
 *
 * There's also one in sys/, used by the kernel
 */
extern void abort(void);

#define assert(cond) {if (!(cond)) abort();}

#endif /* _ASSERT_H */
