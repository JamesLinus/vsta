#!/bin/sh
#
# man.sh
#	Simple driver to get man pages
#

# Manual pages live here
db=/vsta/doc/man

# Known manual sections
dirs="1 2 3 6"

# If "man 1 foo", look for a foo.1 man page directly
# Otherwise, search all sections
if [ $# -gt 1 ]
then
	# If it's -k, return all matching entries
	if [ $1 = "-k" ]
	then
		for y in $dirs
		do
			cd $db/$y
			for z in *.$y
			do
				echo $z
			done
		done | grep $2
		exit 0
	fi

	# Else it's a section/entry
	sec=$1
	entry=$2
else
	entry=$1
	for sec in $dirs
	do
		if [ -f $db/$sec/$entry.$sec ]
		then
			break
		fi
	done
fi

# If couldn't find it, bail
f=$db/$sec/$entry.$sec
if [ ! -r $f ]
then
	echo $entry": unknown man page"
	exit 1
fi

# Choose pager, else default to "less"
if [ -z "$PAGER" ]
then
	PAGER=less
fi

# View the page
exec $PAGER $f
