#ifndef _PROC_H
#define _PROC_H
/*
 * proc.h
 *	Data structures for the process server
 */
#include <sys/msg.h>
#include <sys/perm.h>
#include <sys/param.h>
#include <sys/pstat.h>

struct file;

struct file_ops {
	void (*open)(struct msg *, struct file *);
	void (*seek)(struct msg *, struct file *);
	void (*read)(struct msg *, struct file *, uint);
	void (*write)(struct msg *, struct file *, uint);
	void (*stat)(struct msg *, struct file *);
	void (*wstat)(struct msg *, struct file *);
};

extern struct file_ops root_ops, proc_ops, kernel_ops, no_ops;

/*
 * An open file
 */
struct file {
	struct perm f_perms[PROCPERMS];
				/* Our abilities */
	int f_nperm;		/* Number of client permissions */
	off_t f_pos;		/* Position in internal node */
	int f_mode;		/* Abilities WRT current node */
	pid_t f_proclist[NPROC];
				/* Current process list */
	int f_active;		/* Do we have an active transfer */
	struct pstat_proc f_proc;
				/* Current process' pstat_proc */
	struct prot f_prot;	/* File protection information */
	pid_t f_pid;		/* Current pid */
	struct file_ops *f_ops;	/* "vtable" for current file */
	struct pstat_kernel f_kern;
				/* Kernel stat info */
};

#if 0
/*
 * Some function prototypes
 */
extern proc_seek(struct msg *, struct file *);
#endif

extern void proc_inval(struct msg *, struct file *),
	proc_inval_rw(struct msg *, struct file *, uint),
	proc_seek(struct msg *, struct file *),
	emulate_client_perms(struct file *),
	release_client_perms(struct file *);
extern int proclist_pstat(struct file *),
	proc_pstat(struct file *),
	kernel_pstat(struct file *);

#define MIN(a,b) ((a)<(b)?(a):(b))

#endif /* _PROC_H */
