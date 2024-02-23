## TCC User Natives Extension

Copyright (C) 2016 Atronix Engineering
Copyright (C) 2016-2019 Ren-C Open Source Contributors

Distributed under the Apache 2.0 License
TCC (and libtcc) are distributed under the Lesser GPL (LGPL) license

### Purpose

This extension provides the ability in usermode Rebol to implement new native
functions in C, without rebuilding the interpreter itself.  It relies only
on the Rebol executable, and doesn't require any separate compiler toolchain
to be installed on the system (!).

These "user natives" are created with an ordinary spec block describing their
interface, just like FUNCTION.  But instead of having a BLOCK! of Rebol code
as their body, they have a string of TEXT!.  This is C code which has access
to Rebol APIs for extracting information from the native's parameters, as well
as for making Rebol values to return to the caller.

(!) - See caveats in the section titled "Standalone Compilation"

### Building the Extension

The C compilation facility is accomplished using the "Tiny C" compiler's
library for embedding in programs, called "libtcc", which must be installed
while building Rebol to make the extension:

http://bellard.org/tcc
https://repo.or.cz/tinycc.git/blob_plain/HEAD:/libtcc.h
https://repo.or.cz/tinycc.git/blob_plain/HEAD:/tests/libtcc_test.c

During the make process, you must set the CONFIG_TCCDIR environment variable
variable to where TCC-specific .h and linker definition files are located.
(This is a standard variable name, used by compiled TCC programs when finding
their runtimes, see `tcc_add_lib_dir()`.)  Files from this directory are
bundled into the EXE or DLL with "encapping", so they don't have to be
downloaded separately to achieve basic compilation.  If %libtcc1.a is not in
that directory, then TCC_LIBTCC1_FILE must also be specified to where it is.

On Linux, this means the TCC lib *and TCC itself* must both be installed to
find the files to bundle.  e.g. on Linux:

    sudo apt-get install libtcc-dev
    sudo apt-get install tcc

The first line permits linking the compiler from the extension, with "-ltcc".
The second line gets not only the TCC executable (which you may or may not
care about), but more importantly the libtcc1.a and some %includes/ that are
typically installed in a directory with a name like:

    /usr/lib/x86_64-linux-gnu/tcc/

So that is the directory CONFIG_TCCDIR must be set to.

On Windows, there are many more needed includes and `.def` files.  These are
found in the TCC source package under the %win32 directory, in %include/ and
%lib/ directories.  If you built TCC yourself, this should be where you set
TCC_RESOURCES_DIR to...and TCC_LIBTCC1_FILE should indicate %libtcc1.a where
it wound up.  If building with MinGW using the standard configuration, that
could be in root directory of the source.

(Note: This TCC extension has not been built in Visual Studio yet, to the best
of my knowledge, and there is no standardized distribution of the library
for MSVC.)

### Standalone Compilation

The TCC library can build code directly into memory and execute it, without
touching the disk (via `tcc_set_output_type(state, TCC_OUTPUT_MEMORY)`).  Any
C functions or data you want to be linked to the in-memory compilation can
be added with `tcc_add_symbol(name, pointer)`.  This theoretically enables the
desirable property of being able to have a single EXE file for Rebol with a C
compiler in it, with no need for any other development tool installation.

IN THEORY, that works.  It works quite well for exporting libRebol hooks
directly to the natives--Rebol builds a list of them dynamically anyway, and
the extension already has them in hand, e.g.:

    tcc_add_symbol("rebRun", &rebAPI->rebRun);

But for things other than libRebol, there are complications:

1. Any real C program requires the C standard library (malloc(), strlen(),
   etc.)  While the Rebol executable includes *many* of these functions, it
   doesn't use *all* of them.  Even so, adding them to a table with their
   string names would require storing that table mapping names to addresses,
   and extracting those names out of libraries.  This would basically be
   reinventing the linker, which is not a good use of time or effort.

2. If one tried to bundle standard include files (such as %stdio.h) into the
   extension, TCC won't find them when you say `#include <stdio.h>` in your
   code--it only looks on disk.  So if you have a program with such a syntax,
   it has to be on your filesystem OR the extension would have to do its own
   substitutions.  This would basically be reinventing the preprocessor (which
   is also not a good use of time or effort).

3. There are some language features that compilers implement with helper
   functions instead of direct assembly generation.  Theoretically, a compiler
   can turn `1 + 2` into a call to an `__add` function (it would be slow).
   But other more reasonable examples conjure internals like `__va_start` or
   `__udivmoddi4`.  These are compiler-specific, so TCC's versions live in a
   library called %libtcc1.a, and you won't find that pre-installed on your
   Linux's /usr/lib.  These symbols can't be linked into the extension to
   be exported with tcc_add_symbol()--these internals can't safely be linked
   directly into a GCC-built interpreter.

On Linux systems, (1) and (2) are fairly easy to address by just having the
"basic" headers you would expect someone to have on a system on disk.  This
is standard practice--and it's why a TCC compiler install on linux has almost
no "include/*" or "lib/*" files of its own.  The few it does have relate to
point (3), where it overrides things like how va_lists work in <stdarg.h>.

On Windows, (1) is complicated because TCC can't use Visual Studio's .LIB
files (or MinGW's, even).  There, it does ship with its own directory full
of files...providing things like stdio.h and winapi/windows.h:

https://repo.or.cz/tinycc.git/tree/HEAD:/win32

As for (3), it seems nice if one can avoid asking a user to have installed
odd dependencies that are particular to TCC.  This was initially addressed by
forking the TCC codebase, and adding a new API for giving back a table of
symbols and functions.  Having to maintain that fork creates an undesirable
dependency--so a suggestion has been made to the TCC developers, to allow
registering hooks to fulfill library requests or include files:

http://lists.nongnu.org/archive/html/tinycc-devel/2018-12/msg00011.html

(A copy of this suggestion is included at the bottom of this file.)

That would provide a workaround for letting a host who has embedded blobs
for headers and libs provide those to the build process.

But until that feature is implemented, the extension expects you to have
a suitable TCC library locally.  An approximation could be achieved by
packing up the necessary files into an embedded ZIP archive.  Then, COMPILE
could extract them into a temporary directory, and add them to the includes
and libs... thus avoiding reimplementing the preprocessor or linker.

!!! Work on this feature is in the formative stage.

### API Usage Considerations

Symbol linkage to the internal libRebol API is automatically provided by the
extension. This is the ergonomic external API (e.g. %rebol.h), not the full
internal implementation API (e.g. %sys-core.h).

But the initial implementation of user natives *DID* make the full internal
API available.  This involved invoking TCC during the build process to
pre-process all of the many files included by %sys-core.h into a single blob,
and then pack it into the executable.  It also made every API and constant
available in the symbol table--with its name, to use with tcc_add_symbol().

This predated libRebol's modern form, so it was the only choice at the time.
But now it does not make sense to call into %sys-core.h from user natives.
If anyone is actually seeking some "optimal efficiency" that the core API
can offer, TCC is not an optimizing compiler in the first place.  Any serious
larger-scale initiative would use the extension model, with a "fancier"
compiler and debugger on hand.

libRebol also has a drastically smaller footprint for the header (as %rebol.h
is a single standalone file).  Not having a large table of every internal API
and constant in the system--along with its name--saves space as well.  So
only the libRebol is offered, as the most practical option.

TCC supports C99, which gives variadic macros--which are critical to build
the core and the API.

### Future Directions

It would be interesting to see if a Rebol with TCC embedded could pass thru
command line parameters to the compiler in such a way as to process plain
C files as a substitute for `gcc`, as the `tcc` command line can do.  This
would potentially enable a demo of Rebol building itself with only its own
executable (used for make and compile purposes).

However, this would be mostly a stunt--as users would like Rebol to be
optimized.  It's not a priority, but a curious experimenter might try to mimic
TCC's POSIX command line features via PARSE as a first step:

http://pubs.opengroup.org/onlinepubs/9699919799/utilities/c99.html

### Cache of Improvement Suggestion

Put here in case the mailing list link goes missing.

> I have a suggestion (that may have been made before?) which would
> allow libtcc API users to register callbacks that fulfill headers or
> includes from binary blobs they have on hand.  For instance:
>
>    const char *tcc_include_callback(
>        void *opaque,
>        const char *include,
>        int quoted // e.g. 1 for "foo.h" and 0 for <foo.h>
>    ){
>        if (!quoted && !strcmp(include, "stdarg.h"))
>             return embedded_stdarg;
>        return NULL; // not a match, use default handling
>     }
>
>    const char* tcc_lib_callback(
>        void *opaque,
>        const char *lib
>    ){
>         if (!strcmp(lib, "libtcc1.a"))
>              return embedded_libtcc1;
>         return NULL; // not a match, use default handling
>    }
>
>    tcc_set_include_func(state, opaque, &tcc_include_callback);
>    tcc_set_lib_func(state, opaque, &tcc_lib_callback);
>
> An interpreter I work on currently achieves this with a more narrow
> method.  That method also involves adding to the TCC API...but it's
> more invasive.  The API exports a table of everything that's in
> libtcc1.a, along with its symbol name, e.g.:
>
>    int n = 0;
>    for (n = 0; tcc_libtcc1_names[n] != NULL; ++n)
>        tcc_add_symbol(tcc_libtcc1_names[n], tcc_libtcc1_funcs[n]);
>
> That's less general, and adds a "large" table to libtcc that not
> everyone may want to use.  My alternate proposal seems lighter,
> probably wouldn't be as disruptive to the build process, and would
> help solve some other problems.
>
> The main thing I'd like to avoid is not being stuck with a dependency
> that is a fork of TCC which is not being maintained (...by the "mob"?
> :-P)  So I'm wondering if there's any chance of getting such a feature
> approved by the people who are committing to and tending TCC on a
> regular basis.  Or perhaps another feature which satisfies the desire?
>
> I'd be happy to be involved in adding and testing such a thing, if
> people felt it is the kind of thing that would be a good change.
