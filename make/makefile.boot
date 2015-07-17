# Bootstrap Makefile for the Rebol Interpreter Core (a.k.a. Ren/C)
# This manually produced file was created 17-Jul-2015/10:20:03-04:00

# This makefile is tracked in version control, and can be used to kick off
# a build process.  To do so you can either copy it to 'makefile' and type
# 'make', or pass a command line switch to tell it to use this file:
#
#	make -f makefile.boot
#
# What will happen is that it will first kick off a call to:
#
#	make -f makefile.boot make
#
# This runs a Rebol script in the %src/tools directory called %make-make.r
# which will generate a platform-specific makefile.  Since it is a Rebol
# script, you will need a Rebol3 interpreter...and it expects you to have
# one in the %make/ directory called 'r3-make' (or 'r3-make.exe' on Windows)
#
# The next thing it will do is run 'make r3' using the new makefile:
#
#	make r3
#
# For most purposes this should "just work".  The platform detection is very
# simple: it assumes that you want to build a version that's the same as what
# the 'r3-make' interpreter was built with.  However, you may be wanting to
# "cross-compile" Rebol's generated code to copy %src/include/* over to a
# machine that needs an executable to bootstrap.  (Or maybe it just guessed
# wrong.)  In which case you should check the %src/tools/systems.r file, and
# provide an OS_ID from the table.  For example, Linux with clib 2.5:
#
#    make -f makefile.boot make OS_ID=0.4.3
#
# (Note: These numbers are what appear at the tail of a full Rebol version
# number.  So you might find the ones above in a tuple like `2.101.0.4.3`,
# where the first numbers are referring to the version of the actual Rebol
# codebase itself.  This tuple can be retrieved as `system/version`.)
#
# Once the auto-generated makefile has been produced, it retains the ability
# to regenerate itself.  So you should be able to invoke `make make` or
# `make make OS_ID=##.##.##` without needing to use the bootstrap makefile.
# It also contains additional directions for other options.
#
# Rebol's bootstrapping scripts are supposed to be kept stable, even in the
# presence of language changes.  So you *should* even be able to use an old
# executable from the pre-open-source Rebol3 downloads on rebol.com:
#
#	http://www.rebol.com/r3/downloads.html
#
# (At least, in theory.  If you notice bootstrap with an old interpreter is
# broken on your system, please report it!  Few are testing old binaries.)
#
# For a more recent download, try getting your r3-make from:
#
#	http://rebolsource.net/
#
# !!! Efforts to be able to have Rebol build itself using itself (without a
# make tool, and perhaps even without a separate C toolchain) are being
# considered.  If you want to chime in on that, or need support while
# building, please come chime in on chat:
#
#	http://rebolsource.net/go/chat-faq
#

# UP - some systems do not use ../
UP= ..
# CD - some systems do not use ./
CD= ./
# Special tools:
T= $(UP)/src/tools

# http://stackoverflow.com/a/12099167/211160
ifeq ($(OS),Windows_NT)
	BIN_SUFFIX = .exe
else
	BIN_SUFFIX =
endif

REBOL_TOOL= r3-make
REBOL= $(CD)$(REBOL_TOOL) -qs

### Build targets:
top: $(REBOL_TOOL)$(BIN_SUFFIX)
	$(MAKE) -f makefile.boot make OS_ID=$(OS_ID)
	$(MAKE) prep
	$(MAKE) clean
	$(MAKE) r3

all: $(REBOL_TOOL)$(BIN_SUFFIX)
	$(MAKE) -f makefile make OS_ID=$(OS_ID)
	$(MAKE) all

make: $(REBOL_TOOL)$(BIN_SUFFIX)
	$(REBOL) $T/make-make.r $(OS_ID)

$(REBOL_TOOL)$(BIN_SUFFIX):
	@echo
	@echo "*** ERROR: Missing $(REBOL_TOOL) to build various tmp files."
	@echo "*** Download Rebol 3 and copy it here as $(REBOL_TOOL), then"
	@echo "*** make prep. Or, make prep on some other machine and copy"
	@echo "*** the src/include files here. See comments in %makefile.boot"
	@echo "*** for further information."
	@echo
# REVIEW: is false the best way to return an error code?
	false

%:: $(REBOL_TOOL)$(BIN_SUFFIX)
	@echo
	@echo "*** The %makefile.boot bootstrapping makefile only handles 'make',"
	@echo "*** 'make make', or 'make make OS_ID=##.##.##' (where the #"
	@echo "*** values come from system identification numbers in the"
	@echo "*** %src/tools/systems.r file).  Please use one of those commands"
	@echo "*** to configure generation of a %makefile before trying another"
	@echo "*** make target."
	@echo
# REVIEW: is false the best way to return an error code?
	false
