#!/bin/sh
set -e
set -x
cd doc
make $*
cd ..
cd boot
make $*
cd ..
cd src
sh mkall.sh $*
cd ..
exit 0
