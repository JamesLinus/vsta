/*
 * tty.c
 *	Routines for doing I/O to TTY-like devices
 *
 * In VSTa, the TTY driver knows nothing about line editing and
 * so forth--the user code handles it instead.  These are the default
 * routines, which do most of what you'd expect.  They use a POSIX
 * TTY interface and do editing, modes, and so forth right here.
 *
 * This module assumes there's just a single TTY.  It gets harder
 * to associate state back to various file descriptors.  We could
 * stat the device and hash on device/inode, but that would have to
 * be done per fillbuf, which seems wasteful.  This should suffice
 * for most applications; I'll think of something for the real
 * solution.
 */
#include <sys/fs.h>
#include <termios.h>
#include <fdl.h>
#include <std.h>
#include <stdio.h>
#include "getline.h"

#define PROMPT_SIZE (80)	/* Max bytes expected in prompt */

/*
 * Current tty state, initialized for line-by-line
 */
static struct termios tty_state = {
	ISTRIP|ICRNL,		/* c_iflag */
	OPOST|ONLCR,		/* c_oflag */
	CS8|CREAD|CLOCAL,	/* c_cflag */
	ECHOE|ECHOK|ECHO|
		ISIG|ICANON,	/* c_lflag */
				/* c_cc[] */
{'\4', '\n', '\10', '\27', '\30', '\3', '\37', '\21', '\23', '\1', 0},
	B9600,			/* c_ispeed */
	B9600			/* c_ospeed */
};

/*
 * Where we stash unconsumed TTY data
 */
typedef struct {
	uint f_cnt;
	char *f_pos;
	uint f_bufsz;
	char f_buf[BUFSIZ];
} TTYBUF;

/*
 * This is global instead of within the TTYBUF, as it needs to
 * be coordinated between stdin and stdout.
 */
static char f_prompt[PROMPT_SIZE];

/*
 * init_port()
 *	Create TTYBUF for per-TTY state
 */
static
init_port(struct port *port)
{
	TTYBUF *t;

	t = port->p_data = malloc(sizeof(TTYBUF));
	if (!t) {
		return(1);
	}
	t->f_cnt = 0;
	t->f_bufsz = BUFSIZ;
	t->f_pos = t->f_buf;
	return(0);
}

/*
 * canon()
 *	Do input processing when canonical input is set
 */
static int
canon(struct termios *t, TTYBUF *fp, struct port *port)
{
	char *prompt;

	/*
	 * Calculate the prompt by digging through the last
	 * write() to see what would be on the current output
	 * line.
	 */
	prompt = strrchr(f_prompt, '\n');
	if (prompt) {
		prompt += 1;
	} else {
		prompt = f_prompt;
	}

	/*
	 * Use getline() to fill the buffer.  Add to history.
	 * Note that we keep our own f_buf, but don't use it
	 * since getline() gave us his.
	 */
	fp->f_pos = getline(prompt);
	fp->f_cnt = strlen(fp->f_pos);
	if (fp->f_cnt > 0) {
		gl_histadd(fp->f_pos);
	}

	return(0);
}

/*
 * non_canon()
 *	Do input when ICANON turned off
 *
 * This mode is complex and wonderful.  The code here does its best
 * for the common cases (especially VMIN==1, VTIME==0!) but does not
 * pretend to handle all those timing-sensitive modes.
 */
static int
non_canon(struct termios *t, TTYBUF *fp, struct port *port)
{
	int x;
	struct msg m;

	/*
	 * Set up
	 */
	fp->f_cnt = 0;
	fp->f_pos = fp->f_buf;

	/*
	 * Unlimited time, potentially limited data
	 */
	if (t->c_cc[VTIME] == 0) {
		/*
		 * Build request.  Read as much as we can get.
		 */
		m.m_op = FS_READ|M_READ;
		m.m_buf = fp->f_buf;
		if (t->c_cc[VMIN] == 0) {
			m.m_arg = 0;
		} else {
			m.m_arg = BUFSIZ;
		}
		m.m_buflen = BUFSIZ;
		m.m_nseg = 1;
		m.m_arg1 = 0;
		x = msg_send(port->p_port, &m);
		if (x <= 0) {
			return(-1);
		}

		/*
		 * Flag data buffered.  If ECHO, echo it back now.
		 */
		fp->f_cnt += x;
		if (t->c_lflag & ECHO) {
			(void)write(1, fp->f_buf, x);
		}
		return(0);
	}

	/*
	 * Others not supported (yet)
	 */
	return(-1);
}

/*
 * Macro to do one-time setup of a file descriptor for a TTY
 */
#define SETUP(port) \
	if (port->p_data == 0) { \
		if (init_port(port)) { \
			return(-1); \
		} \
	}

/*
 * __tty_read()
 *	Fill buffer from TTY-type device
 */
__tty_read(struct port *port, void *buf, uint nbyte)
{
	struct termios *t = &tty_state;
	TTYBUF *fp;
	int error, cnt;

	/*
	 * Do one-time setup if needed, get pointer to state info
	 */
	SETUP(port);
	fp = port->p_data;

	/*
	 * Load next buffer if needed
	 */
	if (fp->f_cnt == 0) {
		/*
		 * Non-canonical processing get its own routine
		 */
		if ((t->c_lflag & ICANON) == 0) {
			error = non_canon(t, fp, port);
		} else {
			error = canon(t, fp, port);
		}
	}

	/*
	 * I/O errors get caught here
	 */
	if (error && (fp->f_cnt == 0)) {
		return(-1);
	}

	/*
	 * Now that we have a buffer with the needed bytes, copy
	 * out as many as will fit and are available.
	 */
	cnt = MIN(fp->f_cnt, nbyte);
	bcopy(fp->f_pos, buf, cnt);
	fp->f_pos += cnt;
	fp->f_cnt -= cnt;

	return(cnt);
}

/*
 * __tty_write()
 *	Flush buffers to a TTY-type device
 *
 * XXX add the needed CRNL conversion and such.
 */
__tty_write(struct port *port, void *buf, uint nbyte)
{
	struct msg m;
	TTYBUF *fp;
	int ret;

	/*
	 * Display string.  Shortcut out if not doing
	 * canonical input processing
	 */
	m.m_op = FS_WRITE;
	m.m_buf = buf;
	m.m_arg = m.m_buflen = nbyte;
	m.m_nseg = 1;
	m.m_arg1 = 0;
	ret = msg_send(port->p_port, &m);
	if ((ret < 0) || ((tty_state.c_lflag & ICANON) == 0)) {
		return(ret);
	}

	/*
	 * Get buffering for prompt, get pointer to port state
	 */
	SETUP(port);
	fp = port->p_data;

	/*
	 * Keep track of byte displayed to cursor on this line.
	 * Needed to provide prompt string for input editing. Bleh.
	 * Note we assume that the last write() will be (at least) the
	 * entire prompt.  We leave digging around for the \n until we
	 * really need to prompt, favoring speeds of write()'s to TTYs
	 * over speed to set up a getline().
	 *
	 * f_prompt is actually PROMPT_SIZE+1 bytes, so there IS
	 * room for the \0 termination.
	 */
	if (nbyte > PROMPT_SIZE) {
		buf = (char *)buf + (nbyte - PROMPT_SIZE);
		nbyte = PROMPT_SIZE;
	}
	bcopy(buf, f_prompt, nbyte);
	f_prompt[nbyte] = '\0';

	return(ret);
}

/*
 * __tty_close()
 *	Free our typing buffer
 */
__tty_close(struct port *port)
{
	free(port->p_data);
	return(0);
}

/*
 * tcsetattr()
 *	Set TTY attributes
 *
 * In addition to recording the state locally, key parts are forwarded
 * to the underlying TTY port server.  This permits this port server--
 * be it a window under MGR or the console server--to know what mode
 * the TTY is in, and what characters should result in a signal.
 *
 * This change is wstat()'ed upward, but no error return is recorded.
 * It is quite possible that the TTY server does not implement interrupt
 * and quit characters; this is fine for TTY servers which primarily offer
 * operation in an embedded mode.
 */
tcsetattr(int fd, int flag, struct termios *t)
{
	struct port *port;
	char buf[128];

	port = __port(fd);
	if (!port || (port->p_read != __tty_read)) {
		return(-1);
	}
	if (t->c_cc[VINTR] != tty_state.c_cc[VINTR]) {
		sprintf(buf, "intr=%d\n", t->c_cc[VINTR]);
		(void)wstat(port->p_port, buf);
	}
	if (t->c_cc[VQUIT] != tty_state.c_cc[VQUIT]) {
		sprintf(buf, "quit=%d\n", t->c_cc[VQUIT]);
		(void)wstat(port->p_port, buf);
	}
	if ((t->c_lflag & (ISIG | ICANON)) !=
			(tty_state.c_lflag & (ISIG | ICANON))) {
		sprintf(buf, "isig=%d\n",
			(t->c_lflag & (ISIG | ICANON)) == (ISIG | ICANON));
		(void)wstat(port->p_port, buf);
	}
	bcopy(t, &tty_state, sizeof(tty_state));
	return(0);
}

/*
 * tcgetattr()
 *	Get current TTY attributes
 */
tcgetattr(int fd, struct termios *t)
{
	struct port *port;

	port = __port(fd);
	if (!port || (port->p_read != __tty_read)) {
		return(-1);
	}
	*t = tty_state;
	return(0);
}

/*
 * cfgetispeed()
 *	Return TTY baud rate
 */
ulong
cfgetispeed(struct termios *t)
{
	return(t->c_ispeed);
}

/*
 * cfgetospeed()
 *	Return TTY baud rate
 */
ulong
cfgetospeed(struct termios *t)
{
	return(t->c_ospeed);
}

/*
 * cfsetispeed()
 *	Set TTY input baud rate
 */
int
cfsetispeed(struct termios *t, ulong speed)
{
	t->c_ispeed = speed;
	return(0);
}

/*
 * cfsetospeed()
 *	Set TTY output baud rate
 */
int
cfsetospeed(struct termios *t, ulong speed)
{
	t->c_ospeed = speed;
	return(0);
}

/*
 * tcgetsize()
 *	Try to divine the geometry of the given display
 */
int
tcgetsize(int fd, int *rowsp, int *colsp)
{
	port_t port;
	char *p;
	int rows, cols;

	port = __fd_port(fd);
	if (port < 0) {
		return(-1);
	}
	p = rstat(port, "rows");
	if (!p || (sscanf(p, "%d", &rows) != 1)) {
		return(-1);
	}
	p = rstat(port, "cols");
	if (!p || (sscanf(p, "%d", &cols) != 1)) {
		return(-1);
	}
	*rowsp = rows;
	*colsp = cols;
	return(0);
}
