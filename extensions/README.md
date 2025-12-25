# Rebol Extensions

Extensions are like modules that can implement some of their functionality
using native code.

Currently, extensions are kept in the main repository to make it easier to
keep them up-to-date, when the internal API or build mechanisms change.  Once
those stabilize, they should be their own individual projects with their own
issue trackers and maintainers.

Extensions and the libraries they link to may have their own licenses besides
Ren-C's LGPL, so see the LICENSE.txt file in the extension subdirectory for
that information.


## Building

The build process offers three ways to build an extension, as specified in
the "extensions" section of the config (or EXTENSIONS command-line switch).

* `+` means to build the extension directly into the executable or library.
* `*` means to build as a DLLs that can be distributed separately and then
      loaded optionally when needed.
* `-` means do not build the extension at all.

Passing configuration parameters to the per-extension make script is a work
in progress.  In the meantime, environment variables can be a good way to
configure extensions with complex building needs.

## Extension Configuration (via %make-spec.r)

Extensions are configured by means of a file in the extension's directory
named %make-spec.r (it's a historical name that has just stuck around, and
should probably be changed before the project gains a wider audience).

The spec is executed as the body of a MAKE OBJECT!, hence it is able to
run code... and top-level SET-WORD will become keys in the object that
represents the extension.

### use-librebol

Extensions can choose to use either the user-friendly %rebol.h API:

   use-librebol: 'yes

Or the complex internal %sys-core.h API:

   use-librebol: 'no

Using the latter allows the extension to implement natives whose performance
characteristics are identical to natives which are implemented in the core.
(Though this is not recommended for most tasks, as the internal API changes
frequently--and is easier to get wrong if one is not fairly well versed in how
Rebol is implemented.)

(Note: Even if USE-LIBREBOL is selected, it *should* be possible to include
the full core API.  That feature is currently in flux.)


### cflags

This is simply a BLOCK! of flags that are passed as strings to the compiler.

You can use literal strings, or tags which specify the compiler that the
switch is for (such as <msc:xxx> or <gcc:xxx>).  For instance:

    cflags: ["-Isome-dir/" <msc:/wd4324>]

This will literally put the -I for the include path, and if you are compiling
with Microsoft C then it will get /wd4324.

(There are also some cflags that translate into the appropriate switch for
each compiler, such as <no-unused-parameter>.  See %tools/cflags-map.r)

### definitions:

This lets you do #define options--similar to if you were to pass something
like "-DSOME_DEFINE=1" as a cflag, except you don't have to worry about the
specifics of whether the compiler uses "-D" or "/D" etc.

    definitions: [
        "SOME_DEFINE=1"
        OTHER_DEFINE=0  ; WORD! is legal too (turned into TEXT!)
    ]

### includes:

This is the path for include files.  The paths are relative to the extension's
directory...or you can use an absolute path (one that starts with /)

    includes: compose [
        %subdir/include/  ; subdirectory of the extension
        (join library-path %include/)  ; frequently you'll compose paths in
    ]

### sources:

This is the list of sources for the extension.  The files you put in here
will be scanned for DECLARE_NATIVE() and IMPLEMENT_GENERIC().

You can specify a single file:

    sources: 'mod-vector.c

Or you can specify a file with flags.  Flags appear in blocks after the file
to which they apply:

    sources: [mod-locale.c [<msc:/wd4204>]]

You can mix files with blocks with those without it:

    sources: [
        mod-filesystem.c
        p-file.c [<msc:/wd5220>]
        p-dir.c [<msc:/wd5220>]
        file-posix.c [<msc:/wd5220>]
    ]

(Note that you can specify files with TUPLE! or PATH!, not just FILE!.  A
Bootstrap issue means that what would be TUPLE! will be loaded as a PATH,
e.g. `[mod-locale.c]` => `[mod-locale/c]`.  This is compensated for by the
TO FILE! mechanics in bootstrap, which assumes the last slash in a path
is for the extension.)

### depends:

These are source files that will not be scanned for DECLARE_NATIVE() or
IMPLEMENT_GENERIC(), etc.  These are typically third-party sources which
would not contain any such definitions:

    depends: [
        mbedtls/library/rsa.c  [#no-c++]
        mbedtls/library/rsa_alt_helpers.c  [#no-c++]
        mbedtls/library/oid.c  [#no-c++]
        tf_snprintf.c  [#no-c++]
    ]

## ldflags:

Like cflags, these are literal flags passed to the linker by string.

### libraries:

Libraries is an abstraction of ldflags that lets you avoid the specific
linker flag for including the library.

    libraries: switch platform-config.os-base [
        'Windows [%odbc32]
    ] else [
        compose [
            %odbc (if yes? user-config.odbc-requires-ltdl [%ltdl])
        ]
    ]

### searches:

!!! Searches seems to correspond to the -L switch.  Strange name to have to
remember, and so the make-spec.r have been doing that as an ldflag, but this
then has to be platform specific.  But generally you already need platform
specific code to know where to look!

### options:

Options is a feature which is supposed to let you advertise what options an
extension has for further configuring it.  This hasn't been really hammered
out yet... but it was thought to look something like:

    options: [
        option-name [word! logic?] ()
    ]
