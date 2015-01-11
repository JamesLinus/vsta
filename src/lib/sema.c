/*
 * sema.c
 *	User-level semaphores
 */
#include <sys/fs.h>
#include <sema.h>
#include <alloc.h>

/*
 * alloc_sema()
 *	Create new semaphore
 */
struct sema *
alloc_sema(int init_val)
{
	struct sema *s;
	port_t port;

	/*
	 * Get data structure
	 */
	s = malloc(sizeof(struct sema));
	if (s == 0) {
		return(0);
	}
	init_lock(&s->s_locked);
	s->s_val = init_val;

	/*
	 * Attach to sema server, get two ports
	 */
	s->s_port = path_open("fs/sema:clone",
		ACC_READ | ACC_WRITE | ACC_CREATE);
	if (s->s_port < 0) {
		free(s);
		return(0);
	}
	s->s_portmaster = clone(s->s_port);

	return(s);
}

/*
 * send()
 *	Send a message without segments
 */
static int
send(port_t port, uint op)
{
	struct msg m;

	m.m_op = op;
	m.m_arg = m.m_arg1 = m.m_nseg = 0;
	return(msg_send(port, &m));
}

/*
 * p_sema()
 *	Enter semaphore
 */
int
p_sema(struct sema *s)
{
	int val;

	p_lock(&s->s_locked);
	val = (s->s_val -= 1);
	v_lock(&s->s_locked);
	if (val >= 0) {
		return(0);
	}
	if (send(s->s_port, FS_READ)) {
		return(-1);
	}
	return(0);
}

/*
 * v_sema()
 *	Leave semaphore
 */
void
v_sema(struct sema *s)
{
	int val;

	p_lock(&s->s_locked);
	val = (s->s_val += 1);
	v_lock(&s->s_locked);
	if (val <= 0) {
		(void)send(s->s_portmaster, FS_SEEK);
	}
}
