#
# mkdist
#	Shell script to build the distribution filesets
#

# Copyright/license files
LAW=readme license

# Staging area mounted here
DEST=/dist

# Binary distribution
BIN=$(LAW) bin boot doc etc grub include lib

# Core servers
SRCSRV=src/srv/bfs src/srv/cdfs src/srv/devnull src/srv/dos \
	src/srv/env src/srv/mach src/srv/namer src/srv/pipe \
	src/srv/proc src/srv/sema src/srv/swap src/srv/tmpfs \
	src/srv/vstafs

# Core source distribution
SRC=$(LAW) mkdist.mk src/bin/adb src/bin/init src/bin/login src/bin/perf \
	src/bin/cmds src/bin/time src/lib src/os \
	$(SRCSRV) src/boot.386

# Networking
NET=src/srv/ka9q

# Make (a simple/fast one, and then GNU's)
MAKE=src/bin/make src/bin/gmake

# Text utilities
TXT=src/bin/roff src/bin/less src/bin/grep src/bin/rh \
	src/bin/sed src/bin/tar src/bin/awk src/bin/fileutl \
	src/bin/textutil src/bin/find

# Shells
SH=src/bin/ash src/bin/testsh src/bin/rc src/bin/sh

# Editors
ED=src/bin/emacs src/bin/ed src/bin/vim

# Games
FUN=src/bin/backgamm

# "bc" calculator
BC=src/bin/bc

# GNU zip
GZIP=src/bin/gzip

# "sc" spreadsheet
SC=src/bin/sc

# Smalltalk
SMALL=src/bin/small

# GNU C, and related language tools
GCC=src/bin/gcc2 src/bin/binutl2 src/bin/gdb

# MGR windowing system
MGR=mgr

# Compiler tools
LANG=src/bin/flex src/bin/bison src/bin/yacc

# Default: make a distribution
dist: bindist srcdist make txt sh ed fun bc gzip sc small gcc \
	mgrdist lang net

bindist:
	tar -cvf - $(BIN) | gzip -9 > $(DEST)/vsta.tz

srcdist:
	tar -cvf - $(SRC) | gzip -9 > $(DEST)/vsta_src.tz

make:
	tar -cvf - $(MAKE) | gzip -9 > $(DEST)/make.tz

txt:
	tar -cvf - $(TXT) | gzip -9 > $(DEST)/text.tz

sh:
	tar -cvf - $(SH) | gzip -9 > $(DEST)/shell.tz

ed:
	tar -cvf - $(ED) | gzip -9 > $(DEST)/editor.tz

fun:
	tar -cvf - $(FUN) | gzip -9 > $(DEST)/games.tz

bc:
	tar -cvf - $(BC) | gzip -9 > $(DEST)/bc.tz

gzip:
	tar -cvf - $(GZIP) | gzip -9 > $(DEST)/gzip.tz

sc:
	tar -cvf - $(SC) | gzip -9 > $(DEST)/sc.tz

small:
	tar -cvf - $(SMALL) | gzip -9 > $(DEST)/small.tz

gcc:
	tar -cvf - $(GCC) | gzip -9 > $(DEST)/gcc.tz

mgrdist:
	tar -cvf - $(MGR) | gzip -9 > $(DEST)/mgr.tz

lang:
	tar -cvf - $(LANG) | gzip -9 > $(DEST)/lang.tz

net:
	tar -cvf - $(NET) | gzip -9 > $(DEST)/ka9q.tz
