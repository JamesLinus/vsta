#ifndef __MACHINE_H__
#define __MACHINE_H__

/* 
 * These may give a slight performance improvement by avoiding the function
 * call overhead
 */
static inline void outportb(unsigned short port, char value)
{
__asm__ __volatile__ ("outb %%al,%%dx"
                ::"a" ((char) value),"d" ((unsigned short) port));
}

static inline unsigned int inportb(unsigned short port)
{
        unsigned int _v;
__asm__ __volatile__ ("inb %%dx,%%al"
                :"=a" (_v):"d" ((unsigned short) port),"0" (0));
        return _v;
}

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



