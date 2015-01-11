/*
 * vstasrv.c - VSTa server utilities.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/fs.h>
#include "camvsta.h"


/*
 * new_client()
 *	Create new per-connect structure
 */
struct	cam_file *new_client(struct msg *m, struct hash *table,
	                     struct prot *prot)
{
	struct cam_file *f;
	struct perm *perms;
	int uperms, nperms, desired;

	/*
	 * See if they're OK to access
	 */
	perms = (struct perm *)m->m_buf;
	nperms = (m->m_buflen)/sizeof(struct perm);
	uperms = perm_calc(perms, nperms, prot);
	desired = m->m_arg & (ACC_WRITE|ACC_READ|ACC_CHMOD);
	if ((uperms & desired) != desired) {
		cam_msg_reply(m, CAM_EPERM);
		return(NULL);
	}

	/*
	 * Get data structure
	 */
	if ((f = malloc(sizeof(struct cam_file))) == 0) {
		cam_msg_reply(m, CAM_ENOMEM);
		return(NULL);
	}

	/*
	 * Fill in fields.
	 */
	bzero(f, sizeof(*f));
	f->uperm = uperms;
	f->devid = CAM_ROOTDIR;

	/*
	 * Hash under the sender's handle
	 */
        if (hash_insert(table, m->m_sender, f)) {
		free(f);
		cam_msg_reply(m, CAM_ENOMEM);
		return(NULL);
	}

	/*
	 * Return acceptance
	 */
	msg_accept(m->m_sender);

	return(f);
}

/*
 * dup_client()
 *	Duplicate current file access onto new session
 */
void
dup_client(struct msg *m, struct hash *table, struct cam_file *fold)
{
	struct cam_file *f;

	/*
	 * Get data structure
	 */
	if ((f = malloc(sizeof(struct cam_file))) == 0) {
		cam_msg_reply(m, CAM_ENOMEM);
		return;
	}

	/*
	 * Fill in fields.  Simply duplicate old file.
	 */
	*f = *fold;

	/*
	 * Hash under the sender's handle
	 */
        if (hash_insert(table, m->m_arg, f)) {
		free(f);
		cam_msg_reply(m, CAM_ENOMEM);
		return;
	}

	m->m_arg = m->m_buflen = m->m_nseg = m->m_arg1 = 0;
	cam_msg_reply(m, CAM_SUCCESS);
}

/*
 * dead_client()
 *	Someone has gone away.  Free their info.
 */
void
dead_client(struct msg *m, struct hash *table, struct cam_file *f)
{
	(void)hash_delete(table, m->m_sender);
	free(f);
}
