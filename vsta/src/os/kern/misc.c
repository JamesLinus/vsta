/*
 * misc.c
 *	Miscellaneous support routines
 */
#include <sys/types.h>
#include <sys/assert.h>
#include <sys/percpu.h>
#include <sys/proc.h>
#include <sys/thread.h>
#include <sys/fs.h>

#define NUMBUF (16)	/* Buffer size for numbers */

extern void dbg_enter(), putchar();
void strcpy();

/*
 * get_ustr()
 *	Get a counted user string, enforce sanity
 */
get_ustr(char *kstr, int klen, void *ustr, int ulen)
{
	int x;

	if ((ulen+1) > klen) {
		return(err(EINVAL));
	}
	if (copyin(ustr, kstr, ulen)) {
		return(err(EFAULT));
	}
	kstr[ulen] = '\0';
	if (strlen(kstr) < 1) {
		return(err(EINVAL));
	}
	return(0);
}

/*
 * num()
 *	Convert number to string
 */
static void
num(char *buf, uint x, uint base)
{
	char *p = buf+NUMBUF;
	uint c, len = 1;

	*--p = '\0';
	do {
		c = (x % base);
		if (c < 10) {
			*--p = '0'+c;
		} else {
			*--p = 'a'+(c-10);
		}
		len += 1;
		x /= base;
	} while (x != 0);
	bcopy(p, buf, len);
}

/*
 * puts()
 *	Print out a string
 */
void
puts(char *s)
{
	char c;

	while (c = *s++) {
		putchar(c);
	}
}

/*
 * printf()
 *	Very small subset printf()
 */
static void
do_print(char *buf, char *fmt, int *args)
{
	char *p = fmt, c;
	char numbuf[NUMBUF];

	while (c = *p++) {
		if (c != '%') {
			*buf++ = c;
			continue;
		}
		switch (c = *p++) {
		case 'd':
			num(numbuf, *args++, 10);
			strcpy(buf, numbuf);
			buf += strlen(buf);
			break;
		case 'x':
			num(numbuf, *args++, 16);
			strcpy(buf, numbuf);
			buf += strlen(buf);
			break;
		case 's':
			strcpy(buf, (char *)(*args++));
			buf += strlen(buf);
			break;
		default:
			*buf++ = c;
			break;
		}
	}
	*buf = '\0';
}

/*
 * sprintf()
 *	Print into a string
 */
void
sprintf(char *buf, char *fmt, int arg1, ...)
{
	do_print(buf, fmt, &arg1);
}

/*
 * printf()
 *	Print onto console
 */
void
printf(char *fmt, int arg1, ...)
{
	char buf[132];

	do_print(buf, fmt, &arg1);
	puts(buf);
}

/*
 * panic()
 *	Print message and crash
 */
void
panic(char *msg, int arg1, ...)
{
	char buf[132];

	cli();
	do_print(buf, msg, &arg1);
	puts(buf);
	printf("\n", 0);
	for (;;) {
		dbg_enter();
	}
}

/*
 * err()
 *	Central routine to record error for current thread
 *
 * Always returns -1, which is the return value for a syscall
 * which has an error.
 */
err(char *msg)
{
	extern void mach_flagerr();

	strcpy(curthread->t_err, msg);
	mach_flagerr(curthread->t_uregs);
	return(-1);
}

/*
 * strcpy()
 *	We don't use the C library, so need to provide our own
 */
void
strcpy(char *dest, char *src)
{
	while (*dest++ = *src++)
		;
}

/*
 * strlen()
 *	Length of string
 */
strlen(char *p)
{
	int x = 0;

	while (*p++)
		++x;
	return(x);
}

/*
 * canget()
 *	Common function to estimate how powerful a thread is
 */
static int
canget(int bit)
{
	int perms;
	static struct prot rootprot =
		{2, 0, {1, 1}, {ACC_READ, ACC_WRITE}};

	perms = perm_calc(curthread->t_proc->p_ids, PROCPERMS, &rootprot);
	if (!(perms & bit)) {
		err(EPERM);
		return(0);
	}
	return(1);
}

/*
 * isroot()
 *	Tell if the current thread's a big shot
 *
 * Sets err(EPERM) if he isn't.
 */
isroot(void)
{
	return(canget(ACC_WRITE));
}

/*
 * issys()
 *	Like root, but little shots OK too...
 */
issys(void)
{
	return(canget(ACC_READ));
}

/*
 * memcpy()
 *	The compiler can generate these; we use bcopy()
 */
void *
memcpy(void *dest, void *src, uint cnt)
{
	bcopy(src, dest, cnt);
	return(dest);
}

/*
 * strcmp()
 *	Compare two strings, return whether they're equal
 *
 * We don't bother with the distinction of which is "less than"
 * the other.
 */
strcmp(char *s1, char *s2)
{
	while (*s1++ == *s2) {
		if (*s2++ == '\0') {
			return(0);
		}
	}
	return(1);
}

/*
 * strerror()
 *	Return current error string to user
 */
strerror(char *ustr)
{
	int len;

	len = strlen(curthread->t_err);
	if (copyout(ustr, curthread->t_err, len+1)) {
		return(err(EFAULT));
	}
	return(0);
}

/*
 * perm_ctl()
 *	Set a slot to given permission value, or read slot
 */
perm_ctl(int arg_idx, struct perm *arg_perm, struct perm *arg_ret)
{
	uint x;
	struct perm perm;
	struct proc *p = curthread->t_proc;

	/*
	 * Legal slot?
	 */
	if ((arg_idx < 0) || (arg_idx >= PROCPERMS)) {
		return(err(EINVAL));
	}

	/*
	 * Trying to set?
	 */
	if (arg_perm) {
		/*
		 * Argument OK?
		 */
		if (copyin(arg_perm, &perm, sizeof(struct perm))) {
			return(err(EFAULT));
		}

		/*
		 * See if any of our current permissions dominates it
		 */
		p_sema(&p->p_sema, PRILO);
		for (x = 0; x < PROCPERMS; ++x) {
			if (perm_dominates(&p->p_ids[x], &perm)) {
				break;
			}
		}

		/*
		 * If we found one, then we can overwrite the label.
		 * Preserve the UID which allowed this.
		 */
		if (x < PROCPERMS) {
			printf("perm_ctl uid %d\n", perm.perm_uid);
			p->p_ids[arg_idx] = perm;
			if (p->p_ids[x].perm_uid) {
				printf(" ...inherit %d\n",
					p->p_ids[x].perm_uid);
				p->p_ids[arg_idx].perm_uid =
					p->p_ids[x].perm_uid;
			}
		}
		v_sema(&p->p_sema);

		/*
		 * Return result
		 */
		if (x >= PROCPERMS) {
			return(err(EPERM));
		}
	}

	/*
	 * Want a copy of the slot?
	 * XXX worth locking again to avoid race?
	 */
	if (arg_ret) {
		if (copyout(arg_ret, &p->p_ids[arg_idx],
				sizeof(struct perm))) {
			return(err(EFAULT));
		}
	}
	return(0);
}

/*
 * set_cmd()
 *	Set p_cmd[] field of proc
 *
 * Merely an advisory tool; it isn't trusted in any way
 */
set_cmd(char *arg_cmd)
{
	struct proc *p = curthread->t_proc;

	if (copyin(arg_cmd, p->p_cmd, sizeof(p->p_cmd))) {
		return(err(EFAULT));
	}
	return(0);
}

/*
 * getid()
 *	Get PID, TID, or PPID
 */
pid_t
getid(int which)
{
	pid_t pid;

	/*
	 * Easy cases
	 */
	switch (which) {
	case 0:
		return(curthread->t_proc->p_pid);
	case 1:
		return(curthread->t_pid);
	case 2:
		pid = parent_exitgrp(curthread->t_proc->p_parent);
		return((pid > 0) ? pid : 1);
		break;
	default:
		return(err(EINVAL));
	}
}
