#
# mkdist
#	Shell script to build the distribution filesets
#

# Staging directory for binary distribution
ROOT=/vsta

# Non-CVS ports of stuff
PORTS=src/bin/ports

# Copyright/license files
LAW=README LICENSE

# Staging area mounted here
DEST=/dist

# Binary distribution
LIB=lib/bison.hairy lib/bison.simple lib/crt0.o lib/crt0srv.o \
	lib/ld.a lib/ld.shl lib/libc.a lib/libc.shl lib/libc_s.a \
	lib/libcurses.a lib/libdpart.a lib/libg.a lib/libgcc.a \
	lib/libm.a lib/libregexp.a lib/libsrv.a lib/libtermcap.a \
	lib/libusr.a lib/termcap lib/units.lib lib/libtermcap.shl \
	lib/libm.shl lib/libregexp.shl lib/libregex.a lib/magic \
	lib/libfl.a lib/libjpeg.a

BIN=$(LAW) bin boot doc etc include $(LIB)

# Make (a simple/fast one, and then GNU's)
MAKE=$(PORTS)/make $(PORTS)/gmake

# Text utilities
TXT=$(PORTS)/less $(PORTS)/grep $(PORTS)/rh $(PORTS)/sed $(PORTS)/tar \
	$(PORTS)/awk $(PORTS)/fileutl $(PORTS)/textutil $(PORTS)/find \
	$(PORTS)/patch $(PORTS)/ctags $(PORTS)/rcs5.11 $(PORTS)/m4 \
	$(PORTS)/roff

# Shells
SH=$(PORTS)/rc

# Editors
ED=$(PORTS)/emacs $(PORTS)/ed $(PORTS)/vim $(PORTS)/vim-5.7 $(PORTS)/teco

# Games
FUN=$(PORTS)/backgammon $(PORTS)/chess-5.00

# "bc" calculator
BC=$(PORTS)/bc

# GNU zip and friends
GZIP=$(PORTS)/gzip $(PORTS)/unzip $(PORTS)/arc521

# "sc" spreadsheet
SC=$(PORTS)/sc

# Smalltalk
SMALL=$(PORTS)/small $(PORTS)/smalltalk-1.8.3 $(PORTS)/tiny4.0

# GNU C, and related language tools
GCC=$(PORTS)/gcc2 $(PORTS)/binutl2 $(PORTS)/gdb

# MGR windowing system
MGR=mgr

# Compiler tools
LANG=$(PORTS)/flex $(PORTS)/bison $(PORTS)/yacc

# Python
PYTHON=$(PORTS)/python $(ROOT)/lib/python15

# Diff utilities
DIFF=$(PORTS)/diffutl

# Graphics
GRAPHICS=$(PORTS)/svgalib $(PORTS)/jpeg6b

# Simulators
SIM=$(PORTS)/sim_2.3d

# Miscellaneous
MISC=$(PORTS)/units $(PORTS)/expr $(PORTS)/file-3.22 $(PORTS)/rolodex

# Sample accounts
ACCOUNT=root guest

# Miscellaneous programming languages
MISCLANG=$(PORTS)/pfe

# Source distribution
SRC=make txt sh ed fun bc gzip sc small gcc \
	mgrdist lang net python diff account graphics sim misc \
	srccvs misclang

# Default: make a distribution
dist: bindist $(SRC)

# Create backup... leave off binary distribution, save the rest
backup: $(SRC)

#
# The following are targets which do the actual tarring up of
# files into archives.
#
bindist:
	cd $(ROOT); tar -cvf - $(BIN) | gzip -9 > $(DEST)/vsta.tz

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
	cd $(ROOT); tar -cvf - $(ACCOUNT) | gzip -9 > $(DEST)/account.tz

graphics:
	tar -cvf - $(GRAPHICS) | gzip -9 > $(DEST)/graphics.tz

sim:
	tar -cvf - $(SIM) | gzip -9 > $(DEST)/sim.tz

misc:
	tar -cvf - $(MISC) | gzip -9 > $(DEST)/misc.tz

misclang:
	tar -cvf - $(MISCLANG) | gzip -9 > $(DEST)/misclang.tz

#
# The CVS source control tree behind the main VSTa source tree
#
srccvs:
	cd /cvs ; tar -cvf - . | gzip -9 > $(DEST)/vsta_cvs.tz

