#!/bin/sh
set -e
set -x
for x in doc boot lib etc
do
	cd $x
	make $*
	cd ..
done
cd src
sh mkall.sh $*
cd ..
exit 0
