/*
 * camerror.c - print/log CAM error messages.
 */
#include <stdio.h>
#include <std.h>
#include <syslog.h>
#include "cam.h"

#if	defined(__STDC__)
#if	!defined(__VSTA__)

#include <stdarg.h>
void	cam_error(uint32 flags, char *myname, char *fmt, ...)
{
	va_list	ap;

	fprintf(stderr, "%s: ", myname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	if(flags & CAM_PRINT_SYSERR)
		perror("");
}

cam_debug(uint32 when, char *myname, char *fmt, ...)
{
	va_list	ap;

	if(when & cam_debug_flags) {
		fprintf(stderr, "%s: ", myname);
		if(fmt != NULL) {
			va_start(ap, fmt);
			vfprintf(stderr, fmt, ap);
			va_end(ap);
		}
		fprintf(stderr, "\n");
	}
}

#else	/*__VSTA__*/

#include <stdarg.h>

static	char msgbuf[128];

void	cam_error(uint32 flags, char *myname, char *fmt, ...)
{
	va_list	ap;
	int	a1, a2, a3, a4;

	sprintf(msgbuf, "%s: ", myname);
	va_start(ap, fmt);
	a1 = va_arg(ap, int); a2 = va_arg(ap, int);
	a3 = va_arg(ap, int); a4 = va_arg(ap, int);
	sprintf(&msgbuf[strlen(msgbuf)], fmt, a1, a2, a3, a4);
	strcat(msgbuf, "\n");
	syslog(LOG_ERR, msgbuf);
	if(flags & CAM_PRINT_SYSERR)
		syslog(LOG_ERR, "%s\n", strerror());
}

void	cam_debug(uint32 when, char *myname, char *fmt, ...)
{
	va_list	ap;
	int	a1, a2;

	if(when & cam_debug_flags) {
		sprintf(msgbuf, "%s: ", myname);
		if(fmt != NULL) {
			va_start(ap, fmt);
			a1 = va_arg(ap, int);
			a2 = va_arg(ap, int);
			sprintf(&msgbuf[strlen(msgbuf)], fmt, a1, a2);
			va_end(ap);
		}
		strcat(msgbuf, "\n");
		syslog(LOG_DEBUG, msgbuf);
	}
}

#endif	/*__VSTA__*/
#else	/*__STDC__*/

#include <stdarg.h>
void	cam_error(flags, char *myname, fmt)
uint32	flags;
char	*fmt;
{
	va_list	ap;

	fprintf(stderr, "%s: ", myname);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	if(flags & CAM_PRINT_SYSERR)
		perror("");
}

cam_debug(when, myname, fmt)
uint32	when;
char	*myname, fmt;
{
	va_list	ap;

	if(when & cam_debug_flags) {
		fprintf(stderr, "%s: ", myname);
		if(fmt != NULL) {
			va_start(ap, fmt);
			vfprintf(stderr, fmt, ap);
			va_end(ap);
		}
		fprintf(stderr, "\n");
	}
}

#endif	/*__STDC__*/

