#!/bin/sh
root=$1
dirs="1 2 6"

cp map $root/doc/man
for x in $dirs
do
	mkdir -p $root/doc/man/$x
	cp $x/*.$x $root/doc/man/$x
done
exit 0
