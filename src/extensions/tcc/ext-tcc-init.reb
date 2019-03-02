REBOL [
    Title: "TCC Extension"
    Name: TCC

    Type: Module
    Options: [isolate]
    Version: 1.0.0
    License: {Apache 2.0}

    Description: {
        The COMPILE usermode function is the front-end to the actual COMPILE*
        native, which interfaces directly with the libtcc API.  The front
        end's job is basically to do all the work that *can* be done in Rebol,
        so the amount of low-level C code is minimized.

        Among COMPILE's duties:

        * Transform any Rebol FILE! paths into TEXT! via FILE-TO-LOCAL, so
          that the paths contain the expected backslashes on Windows, etc.

        * For options that can be a list, make sure any missing options are
          turned into an empty block, and single options are turned into a
          single-element block.  This makes fewer cases for the C to handle.

        * If it's necessary to inject any automatic headers (e.g. to add
          {#include "rebol.h"} for accessing the libRebol API) then it does
          that here, instead of making that decision in the C code.

        * Extract embedded header files and library files to the local file
          system.  These allow a Rebol executable that is distributed as a
          single EXE to function as a "standalone" compiler.

          !!! ^-- Planned future feature: See README.md.

        These features could become more imaginative, considering COMPILE as
        a generic "make" service for an embedded TCC.  It could do multiple
        compilation and linking units, allow you to use include files via
        URL!, etc.  For now, it is primarily in service to user natives.
    }
]


compile: function [
    {Compiles one or more native functions at the same time, with options.}

    return: <void>
    compilables "Functions from MAKE-NATIVE, TEXT! strings of code, ..."
    /with
    settings [block!] {
        The block supports the following dialect:
            options [text!]
            include-path [block! file! text!]
            library-path [block! file! text!]
            library [block! file! text!]
            runtime-path [file! text!]
            debug [word! logic!] ;; currently unimplemented
    }
    /inspect "Return the C source code as text, but don't compile it"
][
    ; !!! Due to module dependencies there's some problem with GET-ENV not
    ; being available in some builds (e.g. on Travis).  It gets added to
    ; lib but is somehow not available here.  This is a bug to look into.
    ;
    get-env: :lib/get-env

    if 0 = length of compilables [
        fail ["COMPILABLES must have at least one element"]
    ]

    ; !!! BLOCK! is preprocessed into an OBJECT! for COMPILE*.  It would be
    ; difficult to check a passed-in object for validity as well as add to
    ; it when needed: https://github.com/rebol/rebol-issues/issues/2334

    settings: default [[]]
    config: make object! [
        options: copy []  ; block! of text!s (compiler switches)
        include-path: copy []  ; block! of text!s (local directories)
        library-path: copy []  ; block! of text!s (local directories)
        library: copy []  ; block of text!s (local filenames)
        runtime-path: _  ; sets "CONFIG_TCCDIR" at runtime, text! or blank
        debug: _  ; logic!
    ]

    b: settings
    while [not tail? b] [
        var: (select config try match word! key: b/1) else [
            fail [{COMPILE/OPTIONS parameter} key {is not supported}]
        ]
        b: next b
        if tail? b [
            fail [{Missing argument to} key {in COMPILE}]
        ]

        arg: b/1
        b: next b

        if block? var [  ; at present, this always means multiple paths
            for-each item compose [(arg)] [
                switch type of item [
                    text! []
                    file! [item: file-to-local/full item]
                    fail ["Invalid item type for" key "-" type of item]
                ]

                ; If the var is supposed to be paths, should we check them
                ; here?  Doesn't work for lib short names or options, so
                ; maybe best to just let TCC deliver those errors.

                append var item
            ]
        ] else [
            switch key [
                'runtime-path [
                    if var [fail "RUNTIME-PATH multiply specified"]
                    config/runtime-path: switch type of arg [
                        file! [file-to-local/full arg]
                        text! [arg]
                        fail "RUNTIME-PATH must be TEXT! or FILE!"
                    ]
                ]
                'debug [  ; !!! Currently not supported by COMPILE*
                    if var [fail "DEBUG multiply specified"]
                    debug: switch arg [
                        #[true] 'true [true]
                        #[false] 'false [false]
                        fail "DEBUG must be LOGIC!"
                    ]
                ]
                fail  ; unreachable
            ]
        ]
    ]

    config/debug: default [false]

    ; !!! The pending concept is that there are embedded files in the TCC
    ; extension, and these files are extracted to the local filesystem in
    ; order to make them available.  This idea is being implemented, and it
    ; would mean adding to the include and lib directories.
    ;
    ; For now, we trust CONFIG_TCCDIR, which is a standard setting.  If that
    ; is not provided, we make guesses.

    if config-tccdir: try any [
        get-env "CONFIG_TCCDIR"  ; "local" TEXT! (backslashes on windows)
        file-to-local try match exists? %/usr/lib/x86_64-linux-gnu/tcc/
        file-to-local try match exists? %/usr/local/lib/tcc/
    ][
        ; TCC has its own definition of macros like va_arg(), which use
        ; internal helpers like __va_start, and those are *not* part of
        ; GNU libc.  This leads to two complicating factors.  One is that
        ; there are a few TCC header files which must out-prioritize the
        ; ones in the standard distribution, e.g. <stdarg.h>.  These files
        ; must basically appear first in the `-I` include paths, which makes
        ; them *out-prioritize the system headers* (!)  So when a file in
        ; /usr/include does `#include <stdarg.h>`, it will get that from
        ; the override directory and not /usr/include:
        ;
        ; https://stackoverflow.com/questions/53154898/

        insert config/include-path config-tccdir/include

        ; The other complicating factor is that once emitted code has these
        ; references to TCC-specific internal routines not in libc, the
        ; tcc_relocate() command inside the extension (API link step) has to
        ; be able to find definitions for them.  They live in %libtcc1.a,
        ; which is generally located in the CONFIG_TCCDIR.

        config/runtime-path: default [config-tccdir]
    ]

    if "1" = get-env "REBOL_TCC_EXTENSION_32BIT_ON_64BIT" [
        ;
        ; This is the verbatim list of library overrides that `-v` dumps out
        ; on 64-bit multilib Travis when compiling `int main() {}` with -m32:
        ;
        ;     gcc -v -m32 main.c -o main
        ;
        ; Otherwise, if you're trying to run a 32-bit Rebol on a 64-bit system
        ; then the link step of TCC would try to link to the 64-bit libs.
        ; This doesn't usually come up, because most people runnning a 32-bit
        ; Rebol are only doing so because they *can't* run on 64-bits.  But
        ; Travis containers are only 64-bit, so this helps test 32-bit builds.
        ;
        ; Better suggestions on how to do this are of course welcome.  :-/
        ;
        insert config/library-path [
            "/usr/lib/gcc/x86_64-linux-gnu/7/32/"
            "/usr/lib/gcc/x86_64-linux-gnu/7/../../../../lib32/"
            "/lib/../lib32/"
            "/usr/lib/../lib32/"
        ]
    ]

    ; If there are any "user natives" mentioned, tell COMPILE* to expose the
    ; internal libRebol symbols to the compiled code.
    ;
    ; !!! This can only work for in-memory compiles, e.g. TCC_OUTPUT_MEMORY.
    ; If COMPILE* allowed building an executable on disk (it should!) then
    ; you would need an actual copy of librebol.a to do this, which if it
    ; were shipped embedded would basically double the size of the Rebol
    ; executable (since that's the whole interpreter!).  A better option
    ; would be to encap the executable you already have as a copy with the
    ; natives loaded into it.

    librebol: _

    compilables: map-each item compilables [
        item: maybe if match [word! path!] :item [get item]

        switch type of :item [
            action! [
                librebol: /librebol
                :item
            ]
            text! [
                item
            ]
            file! [  ; Just an example idea... reading disk files?
                as text! read item
            ]
            default [
                fail ["COMPILABLES currently must be TEXT!/ACTION!/FILE!"]
            ]
        ]
    ]

    if librebol [
        insert compilables trim/auto mutable {
            /* TCC's override of <stddef.h> defines int64_t in a way that
             * might not be compatible with glibc's <stdint.h> (which at time
             * of writing defines it as a `__int64_t`.)  You might get:
             *
             *     /usr/include/x86_64-linux-gnu/bits/stdint-intn.h:27:
             *         error: incompatible redefinition of 'int64_t'
             *
             * Since TCC's stddef.h has what rebol.h needs in it anyway, try
             * just including that.  Otherwise try getting a newer version of
             * libtcc, a different gcc install, or just disable warnings.)
             */
            #define LIBREBOL_NO_STDINT
            #include <stddef.h>
            #include "rebol.h"
        }

        ; We want to embed and ship "rebol.h" automatically.  But as a first
        ; step, try using the TOP_DIR variable to find the generated file,
        ; since that is what Travis uses.

        if not librebol-include-path: any [
            local-to-file try get-env "LIBREBOL_INCLUDE_DIR"
            match exists? %/home/hostilefork/Projects/ren-c/make/prep/include
        ][
            fail [
                {LIBREBOL_INCLUDE_DIR currently must be `export`-ed via an}
                {environment variable so that the TCC extension knows where}
                {to find "rebol.h" (e.g. in %make/prep/include)}
            ]
        ]

        if not exists? librebol-include-path/rebol.h [
            fail [
                {Looked for %rebol.h in} librebol-include-path {and did not}
                {find it.  Check your definition of LIBREBOL_INCLUDE_DIR}
            ]
        ]

        insert config/include-path file-to-local librebol-include-path
    ]

    result: compile*/(inspect)/(librebol) compilables config

    if inspect [
        print "== COMPILE/INSPECT CONFIGURATION =="
        probe config
        print "== COMPILE/INSPECT CODE =="
        print result
    ]
]


sys/export [compile]
