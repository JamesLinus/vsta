/*
 * random.h
 *	Definition of random number generator API
 */
#ifndef _RANDOM_H
#define _RANDOM_H

#include <sys/types.h>

extern int srandom(unsigned x);
extern char *initstate(uint seed, char *arg_state, int n);
extern char * setstate(char *arg_state);
extern long random(void);

#endif /* _RANDOM_H */
