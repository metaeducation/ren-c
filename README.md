![Ren/C logo](https://raw.githubusercontent.com/metaeducation/ren-c/master/ren-c-logo.png)

# Ren/C

**Ren/C** is an interim fork of the open sourced
[Rebol](http://en.wikipedia.org/wiki/Rebol) codebase.  It has many goals:

* To create a library form of the interpreter, which is focused on providing
the full spectrum of Rebol's internals to other projects.  This is to open
up possibilities for linking to new IDEs, debuggers, and consoles (such as
[Ren Garden](https://www.youtube.com/watch?v=0exDvv5WEv4)).  It will also be
the basis for language bindings like [Ren/C++](http://rencpp.hostilefork.com/).

* To un-fork the divergence between the community build of Rebol based on the
[12-Dec-2012 open-sourced code](http://www.rebol.com/cgi-bin/blog.r?view=0519)
and the codebase built on pre-open-source code at Saphirion, later maintained
by [Atronix Engineering](http://atronixengineering.com/downloads.html).

* To accelerate the process by which consensus features and fixes are
integrated into the language.  Though debates continue over features in
[Rebol and Red Chat](http://rebolsource.net/go/chat-faq), many changes are
ready and agreed upon--some submitted as patches.  Yet they haven't had a
focal location where they can be launched and people get new builds.

* To integrate the test suite into the build, and make the bar obvious that
contributions must meet by keeping it at zero errors.  The Valgrind and
Address Sanitizer tools are being integrated into the build and test process,
with modifications to the code to prevent masking bugs.

* To provide an option for stable open-source bootstrapping to be used by the
[Red Language](http://www.red-lang.org/p/about.html), which is currently
bootstrapped from closed-source Rebol2.  *(Red's roadmap goal is to move
directly to a self-hosting state from the Rebol2 codebase.  This may be
a poorer option than moving to an improved Rebol3 as an interim step.)*


## Building

It's often as simple as [downloading an interpreter](http://rebolsource.net),
renaming it to `r3-make` or `r3-make.exe`, and putting it in the `%make/`
subdirectory.  Then run:

	 make -f makefile.boot

The platform to target will be assumed to be the same as the build type of
the `r3-make` you use.  If your needs are more complex *(such as doing a
cross-compilation)*, see the comments in the bootstrap makefile:

[%src/make/makefile.boot](https://github.com/metaeducation/ren-c/blob/master/make/makefile.boot)


## Methodology

The Atronix/Saphirion build diverged from Rebol at a point in time prior to
its release as an Apache-licensed open-source project.  Their build had a
graphical user interface and several other additional features, but was only
available for Windows and Linux.

Ren/C split out a "Core" build from the Atronix/Saphirion branch, which runs
as a console process and does not require GUI support.  It was then audited to
build under strict ANSI C89, C99, and C11.  It also added the *option* to build
as strict ISO C++98, C++11, and C++14.  The goal is to take advantage of
stronger type-checking and metaprogramming, while still retaining the ability
to do a complete build on very old compilers when `__cplusplus` is not defined.

Consequently, Ren/C brings all the non-GUI features added by Atronix and
Saphirion to core builds for other systems (Mac 32-bit and 64-bit, HaikuOS,
Raspberry Pi, etc.)  It also allows users who are not interested in the GUI to
use lighter builds on Windows and Linux.

Besides building under these ranges of languages, Ren/C can do so under both
GCC and Clang with zero warnings (with as strict warnings as reasonable).
[Significant changes](https://github.com/metaeducation/ren-c/pull/12) are
needed to do this, which are being given heavy thought on how to make things
simpler, clearer, and better-checked.  Across the board the code is more
readable than what came in, with notable simplifications and improved checks.

*(Note: Ultimately the goal is that Ren/C will not be something Rebol end-users
will be aware of, but a library facing those building software that links to
the evaluator.  Hence systems like Rebol and Ren Garden would be branded as
interfaces and consoles using the core interpreter, and Ren/C would contain
no 'main.c'.  Getting to that point will take a while, and in the meantime
Ren/C produces a traditional Rebol executable as part of its build process.)*


## Features

New features available in Ren/C's console builds vs. the open-sourced Rebol
codebase of 12-Dec-2012 are:

* HTTPS support as a protocol written in Rebol code, empowered by underlying
cryptography libraries incorporated the C code.

* An implementation of LIBRARY!, which allows Rebol to load	a DLL or shared
library and then directly call functions in it.  This is accomplished with the
["FFI"](https://en.wikipedia.org/wiki/Foreign_function_interface) (Foreign
Function Interface) and new data types for representing C-level constructs
like ROUTINE! and STRUCT!.

> Note: Building Ren/C with FFI currently requires additional steps or package,
> installation, as the FFI library has not been extracted into code following
> Rebol's build process.

* CALL with /INPUT /OUTPUT /ERROR

* UDP Network Scheme

* Ability to make use of native ("__builtin") 64-bit math, if it is available

*(Additionally there is serial port support on Linux and Windows.)*


## Platforms

As of 16-Jul-2015, Ren/C has been verified as reaching the goal of building
across the standards-compliant spectrum of C or C++ without warnings on
these desktop platforms:

* Linux 32-bit, libcc 2.11 (`make make OS_ID=0.3.04`)
* Linux 64-bit (`make make OS_ID=0.4.40`)
* Windows 32-bit (`make make OS_ID=0.3.01`)
* Windows 64-bit (`make make OS_ID=0.3.02`)
* OS/X 32-bit (`make make OS_ID=0.2.05`)
* OS/X 64-bit (`make make OS_ID=0.2.40`)

It has additionally been built for:

* ARM Linux on Raspberry Pi (`make make OS_ID=0.4.21`)
* OS/X PowerPC (`make make OS_ID=0.2.04`)
* HaikuOS (`make make OS_ID=0.5.75`)

Here are the warnings enabled (manually in the makefile, at the moment):

> --pedantic -Wextra -Wall -Wchar-subscripts -Wwrite-strings
> -Wdeclaration-after-statement -Wundef -Wformat=2 -Wdisabled-optimization
> -Wcast-qual -Wlogical-op -Wstrict-overflow=5 -Wredundant-decls -Woverflow
> -Wpointer-arith -Wall -Wparentheses -Wmain -Wsign-compare -Wtype-limits
> -Wpointer-sign

These warnings are disabled (manually in the makefile, at the moment):

> -Wno-unused-variable -Wno-unused-parameter -Wno-long-long -Wno-switch


## Viability

It's important to mention that the features written by Saphirion and Atronix
were added using a methodology driven more by customer needs vs. philosophical
or language purity.  Several of the features rely on an incomplete codec
extension model in Rebol3, which was not clearly articulated...nor is it
apparent what separation of concerns it set out to guarantee.  Many other
aspects--such as adding additional device ports--is not as decoupled or modular
as it should be.

Disclaimer aside, the features reflect concrete needs of the user base, and
*have been used in deployment*.  While the implementations may not be perfect,
they're building in a more rigorous compilation environment, and have already
been organized a bit.  So they can be studied for the proper model of extension
and extraction, starting from a basis of code that does function.


## Community

If you have been invited as a stakeholder in this project with write
access to the repository, it is not necessarily for your C programming
skillset.  Rather it is a way of asking for your buy-in with a sort of
"paying it forward"...and trusting you with the power to (for instance)
triage and dismiss issues out of the Issue ticketing database.

Discussion for this project is on [SO Chat](http://rebolsource.net/go/chat-faq)
