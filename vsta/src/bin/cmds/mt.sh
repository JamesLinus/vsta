#!/bin/sh
# mt - Magnetic Tape Commands, VSTa style
# Usage:
#	mt [-f tapedev] command [count]
#
# If 'tapedev' is not specified, mt uses the $TAPE environment variable.
# If $TAPE is not specified, the tape device is '/dev/cam/st0'.
#

if test $# -lt 1 -o $# -gt 4 ; then
	echo "Usage: mt [-f tapedev] command [count]"
	exit 1
fi

#
# Initialize variables
#
command=""
count=1
tapedev=""

#
# Parse command line paremeters
#
while test $# -gt 0 ; do
	case $1 in
	-f)
		tapedev=$2
		shift
		shift
		;;
	*)
		if test "$command" = "" ; then
			command=$1
		else
			count=$1
		fi
		shift
		;;
	esac
done

if test "$command" = "" ; then
	echo "mt: no command specified"
	exit 1
fi

if test "$tapedev" = ""; then
	if test "$TAPE" = ""; then
		tapedev=/dev/cam/st0
	else
		tapedev=$TAPE
	fi
fi

#
# Translate command into "mtio.h" command
#
case $command in
weof)
	command=MTWEOF
	;;
fsf)
	command=MTFSF
	;;
bsf)
	command=MTBSF
	;;
fsr)
	command=MTFSR
	;;
bsr)
	command=MTBSR
	;;
rew)
	command=MTREW
	;;
offl)
	command=MTOFFL
	;;
nop)
	command=MTNOP
	;;
cache)
	command=MTCACHE
	;;
nocache)
	command=MTNOCACHE
	;;
setbsiz)
	command=MTSETBSIZ
	;;
setdnsty)
	command=MTSETDNSTY
	;;
setdrvbuffer)
	command=MTSETDRVBUFFER
	;;
*)
	echo "mt: illegal command"
	exit 1
esac

#echo command = $command count = $count tapedev = $tapedev

#
# Do the Magnetic Tape operation
#
stat -w $tapedev "MTIOCTOP=$command $count"

exit 0

