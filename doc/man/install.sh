#!/bin/sh
root=$1
dirs="1 2 3 6"

cp map $root/doc/man
for x in $dirs
do
	# Create the man dir for this section
	mkdir -p $root/doc/man/$x

	# Make sure no stale, old man pages are present
	rm -f $root/doc/man/$x/*.$x

	# Walk our man pages, converting from nroff source
	for y in $x/*.$x
	do
		nroff -man $y > $root/doc/man/$y
	done
done
exit 0
