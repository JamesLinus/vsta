#ifndef _STD_H
#define _STD_H
/*
 * std.h
 *	Another standards-driven file, I think
 *
 * See comments for unistd.h
 */
#include <sys/types.h>
#include <string.h>

/*
 * Routine templates
 */
extern void *malloc(unsigned int), *realloc(void *, unsigned int);
extern void *calloc(unsigned int, unsigned int);
extern void free(void *);
extern char *strerror( /* ... */ );
extern int fork(void), vfork(void), tfork(voidfun, ulong);
extern long __cwd_size(void);
extern void __cwd_save(char *);
extern char *__cwd_restore(char *);
extern char *getcwd(char *, int);
extern int dup(int), dup2(int, int);
extern int execl(const char *, const char *, ...),
	execv(const char *, char * const *),
	execlp(const char *, const char *, ...),
	execvp(const char *, char * const *);
extern char *getenv(const char *);
extern int setenv(const char *, const char *),
	putenv(const char *);
extern pid_t getpid(void), gettid(void), getppid(void), getpgrp(void);
extern int atoi(const char *);
extern double atof(const char *);
extern long atol(const char *);
extern void perror(const char *);
extern uint sleep(uint);
extern int chdir(const char *);
extern int rmdir(const char *);
extern void *bsearch(const void *key, const void *base, size_t nmemb,
		     size_t size, int (*compar)(const void *, const void *));
extern void qsort(void *base, int n, unsigned size,
		  int (*compar)(void *, void *));
extern long strtol(const char *s, char **ptr, int base);
extern unsigned long strtoul(const char *s, char **ptr, int base);
extern int getdtablesize(void);
extern int system(const char *);
extern void yield(void);
extern int atexit(voidfun);
extern int usleep(uint), nice(int);

/*
 * GNU C has managed to change this one the last three times I moved
 * forward compilers, so don't blink your eyes.
 */
extern void exit(int), _exit(int);

/*
 * exit() values
 */
#define EXIT_SUCCESS (0)
#define EXIT_FAILURE (1)

/*
 * Trap access to the environment from lame programs which think
 * it has to be a static situation.  We have to hope that they
 * include std.h/stdlib.h, because there's no formal indication
 * of how environ[] is defined.  This violates the name space
 * pollution rules, but we're stuck.
 */
extern const char **__get_environ(void);
#define environ (__get_environ())

#endif /* _STD_H */
