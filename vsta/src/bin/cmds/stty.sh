#!/bin/sh
#
# stty.sh
#	Emulate common stty command functions
#
# "stty sane" is especially useful....
#
if [ -z "$*" ]
then
	vals=`stat -`
	for x in $vals ; do eval $x ; done
	if [ ! -z "$baud" ]
	then
		echo -n " "speed" "$baud
	fi
	if [ ! -z "$onlcr" ]
	then
		if [ $onlcr -gt 0 ]
		then
			echo -n " onlcr"
		fi
	fi
	echo ""
else
	for x
	do
		case $x in
		-xkeys) stat -w - xkeys=0 ;;	# Extended keys
		xkeys) stat -w - xkeys=1 ;;
		-isig) stat -w - isig=0 ;;	# Signal generation from keys
		isig) stat -w - isig=1 ;;
		-onlcr) stat -w - onlcr=0 ;;	# Auto-CR addition
		onlcr) stat -w - onlcr=1 ;;
		sane) stat -w - reset ;;
		*) echo Unknown option: $x ;;
		esac
	done
fi
