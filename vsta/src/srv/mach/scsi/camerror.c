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

void	cam_info(uint32 flags, char *fmt, ...)
{
	va_list	ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
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

void	cam_info(uint32 flags, char *fmt, ...)
{
	va_list	ap;
	int	a1, a2, a3, a4;

	va_start(ap, fmt);
	a1 = va_arg(ap, int); a2 = va_arg(ap, int);
	a3 = va_arg(ap, int); a4 = va_arg(ap, int);
	sprintf(msgbuf, fmt, a1, a2, a3, a4);
	strcat(msgbuf, "\n");
	syslog(LOG_INFO, msgbuf);
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

void	cam_info(flags, fmt)
uint32	flags;
char	*fmt;
{
	va_list	ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
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

/*
 * cam_print_sense
 *	Print REQUEST SENSE data using CAM error logging functions.
 */
void	cam_print_sense(void (*prfcn)(), uint32 flags,
	                struct scsi_reqsns_data *snsdata)
{
	static	char *snskeys[] = {
		"NO SENSE", "RECOVERED ERROR", "NOT READY", "MEDIUM ERROR",
		"HARDWARE ERROR", "ILLEGAL REQUEST", "UNIT ATTENTION",
		"DATA PROTECT", "BLANK CHECK", "VENDOR-SPECIFIC",
		"COPY ABORTED", "ABORTED COMMAND", "EQUAL", "VOLUME OVERFLOW",
		"MISCOMPARE", "RESERVED"
	};

	(*prfcn)(flags, "valid\t\t= %d", snsdata->valid);
	(*prfcn)(flags, "error code\t= 0x%x", snsdata->error_code);
	(*prfcn)(flags, "segment num\t= 0x%x", snsdata->segnum);
	(*prfcn)(flags, "Filemark\t= %d", snsdata->filemark);
	(*prfcn)(flags, "EOM\t\t= %d", snsdata->eom);
	(*prfcn)(flags, "ILI\t\t= %d", snsdata->ili);
	(*prfcn)(flags, "adsns_len\t= %d", snsdata->adsns_len);
	(*prfcn)(flags, "sense key\t= 0x%x ", snsdata->snskey);
	if(snsdata->snskey < (sizeof(snskeys) / sizeof(snskeys[0])))
		(*prfcn)(flags, "(%s)", snskeys[snsdata->snskey]);
	else
		(*prfcn)(flags, "");
	(*prfcn)(flags, "sense code\t= 0x%x", snsdata->snscode);
	(*prfcn)(flags, "sense qual\t= 0x%x", snsdata->snsqual);
	(*prfcn)(flags, "fru_code\t= 0x%x", snsdata->fru_code);
}

/*
 * cam_check_error
 * 	Check the function return, CAM, and scsi status codes.
 *	Print an error message if one or more is not OK.
 */
int	cam_check_error(myname, msg, rtn_status, cam_status, scsi_status)
char	*myname, *msg;
long	rtn_status;
int	unsigned cam_status, scsi_status;
{
	if((rtn_status != CAM_SUCCESS) || (scsi_status != SCSI_GOOD) ||
	   ((cam_status != CAM_REQ_INPROG) && (cam_status != CAM_REQ_CMP))) {
		cam_error(0, myname, "%s. rtn = 0x%x, cam = 0x%x, scsi = 0x%x",
		          msg, rtn_status, cam_status, scsi_status);
		return(TRUE);
	} else
		return(FALSE);
}


