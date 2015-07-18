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

# .FORCE is a file assumed to not exist, and is an idiom in makefiles to
# say "Always generate the target".
.FORCE:

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

REBOL_TOOL= r3-make$(BIN_SUFFIX)
REBOL= $(CD)$(REBOL_TOOL) -qs

# For running tests, ship, build, etc.
R3_TARGET= r3$(BIN_SUFFIX)
R3= $(CD)$(R3_TARGET) -qs

### Build targets:
top: make
	$(MAKE) prep
	$(MAKE) clean
	$(MAKE) $(R3_TARGET)

make: $(REBOL_TOOL) .FORCE
	$(REBOL) $T/make-make.r $(OS_ID)

$(REBOL_TOOL):
	@echo
	@echo "*** ERROR: Missing $(REBOL_TOOL) to build various tmp files."
	@echo "*** Download Rebol 3 and copy it here as $(REBOL_TOOL), then"
	@echo "*** make prep.  Or, make prep on some other machine and copy"
	@echo "*** the src/include files here.  You can download executable"
	@echo "*** images of Rebol for several platforms from:"
	@echo "***"
	@echo "***     http://rebolsource.net"
	@echo "***"
	@echo "*** The bootstrap process is kept simple so it should be able"
	@echo "*** to run even on old Rebol builds prior to open-sourcing:"
	@echo "***"
	@echo "***     http://www.rebol.com/r3/downloads.html"
	@echo "***"
	@echo "*** Visit chat for support: http://rebolsource.net/go/chat-faq"
	@echo
# !!! Is false the best way to return an error code?
	false

# !!! This is supposed to be a catch-all rule.  Not working.  If it did work,
# this is what it should say (more or less)

#%:: $(REBOL_TOOL)$(BIN_SUFFIX)
#	@echo
#	@echo
#	@echo "*** The %makefile.boot bootstrapping makefile only handles an"
#	@echo "*** automatic build with these options:"
#	@echo "***"
#	@echo "***     make -f makefile.boot"
#	@echo "***     make -f makefile.boot OS_ID=##.##.##"
#	@echo "***"
#	@echo "*** The first will assume you want to build the same OS_ID as"
#	@echo "*** what your r3-make is.  The second lets you override what"
#	@echo "*** OS to build for from system identification numbers in the"
#	@echo "*** systems table (see %src/tools/systems.r)"
#	@echo "***"
#	@echo "*** If you want to prepare the platform-specific makefile without"
#	@echo "*** *actually* building, then choose 'makefile' as your target:"
#	@echo "***"
#	@echo "***     make -f makefile.boot make"
#	@echo "***     make -f makefile.boot make OS_ID=##.##.##"
#	@echo "***"
#	@echo "*** Visit chat for support: http://rebolsource.net/go/chat-faq"
#	@echo
# !!! Is false the best way to return an error code?
#	false
