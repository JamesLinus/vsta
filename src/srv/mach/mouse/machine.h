#ifndef __MACHINE_H__
#define __MACHINE_H__

static inline unsigned int 
set_semaphore(volatile unsigned int * p, unsigned int newval)
{
	unsigned int semval = newval;
__asm__ __volatile__ ("xchgl %2, %0\n"
			 : /* outputs: semval   */ "=r" (semval)
			 : /* inputs: newval, p */ "0" (semval), "m" (*p)
			);	/* p is a var, containing an address */
	return semval;
}

#endif /* __MACHINE_H__ */
