#
# mkdist
#	Shell script to build the distribution filesets
#

# Copyright/license files
LAW=readme license

# Staging area mounted here
DEST=/dist

# Binary distribution
LIB=lib/bison.hairy lib/bison.simple lib/crt0.o lib/crt0srv.o \
	lib/ld.a lib/ld.shl lib/libc.a lib/libc.shl lib/libc_s.a \
	lib/libcurses.a lib/libdpart.a lib/libg.a lib/libgcc.a \
	lib/libm.a lib/libregexp.a lib/libsrv.a lib/libtermcap.a \
	lib/libusr.a lib/termcap lib/units.lib lib/libtermcap.shl \
	lib/libm.shl lib/libregexp.shl lib/libregex.a lib/magic

BIN=$(LAW) bin boot doc etc include $(LIB)

# Core servers
SRCSRV=src/srv/bfs src/srv/cdfs src/srv/devnull src/srv/dos \
	src/srv/env src/srv/mach src/srv/namer src/srv/pipe \
	src/srv/proc src/srv/sema src/srv/swap src/srv/tmpfs \
	src/srv/vstafs src/srv/tick src/srv/selfs

# Core source distribution
SRC=$(LAW) mkdist.mk rcs src/makefile.all \
	src/bin/adb src/bin/init src/bin/login \
	src/bin/cmds src/bin/time src/include src/lib src/os \
	$(SRCSRV) src/boot.386

# Networking
NET=src/srv/ka9q

# Make (a simple/fast one, and then GNU's)
MAKE=src/bin/ports/make src/bin/ports/gmake

# Text utilities
TXT=src/bin/ports/less src/bin/ports/grep src/bin/ports/rh \
	src/bin/ports/sed src/bin/ports/tar src/bin/ports/awk \
	src/bin/ports/fileutl src/bin/ports/textutil \
	src/bin/ports/find src/bin/ports/patch \
	src/bin/ports/ctags src/bin/ports/rcs5.11 \
	src/bin/ports/m4 src/bin/ports/roff

# Text formatting utilities
ROFF=src/bin/roff src/bin/ports/nroff lib/troff

# Shells
SH=src/bin/ports/ash src/bin/testsh src/bin/ports/rc

# Editors
ED=src/bin/ports/emacs src/bin/ports/ed src/bin/ports/vim \
	src/bin/ports/vim-5.7

# Games
FUN=src/bin/ports/backgammon src/bin/ports/chess-5.00

# "bc" calculator
BC=src/bin/ports/bc

# GNU zip and friends
GZIP=src/bin/ports/gzip src/bin/ports/unzip

# "sc" spreadsheet
SC=src/bin/ports/sc

# Smalltalk
SMALL=src/bin/ports/small src/bin/ports/smalltalk-1.8.3

# GNU C, and related language tools
GCC=src/bin/ports/gcc2 src/bin/ports/binutl2 src/bin/ports/gdb

# MGR windowing system
MGR=mgr lib/libbitblit.a lib/libmgr.a

# Compiler tools
LANG=src/bin/ports/flex src/bin/ports/bison src/bin/ports/yacc lib/libfl.a

# Python
PYTHON=src/bin/ports/python lib/python15

# Diff utilities
DIFF=src/bin/ports/diffutl

# Graphics
GRAPHICS=src/bin/ports/svgalib src/bin/ports/jpeg6b lib/libjpeg.a

# Simulators
SIM=src/bin/ports/sim_2.3d

# Miscellaneous
MISC=src/bin/ports/units src/bin/ports/expr src/bin/ports/file-3.22

# Sample accounts
ACCOUNT=root guest

# Default: make a distribution
dist: bindist srcdist make txt sh ed fun bc gzip sc small gcc \
	mgrdist lang net python diff account graphics sim misc

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

python:
	tar -cvf - $(PYTHON) | gzip -9 > $(DEST)/python.tz

diff:
	tar -cvf - $(DIFF) | gzip -9 > $(DEST)/diff.tz

account:
	tar -cvf - $(ACCOUNT) | gzip -9 > $(DEST)/account.tz

graphics:
	tar -cvf - $(GRAPHICS) | gzip -9 > $(DEST)/graphics.tz

sim:
	tar -cvf - $(SIM) | gzip -9 > $(DEST)/sim.tz

misc:
	tar -cvf - $(MISC) | gzip -9 > $(DEST)/misc.tz
