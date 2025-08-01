Rebol [
    title: "TCC Extension"
    name: TCC

    type: module
    version: 1.0.0
    license: "Apache 2.0"

    description: --[
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
    ]--
]


compile: func [
    "Compiles one or more native functions at the same time, with options"

    return: []
    compilables "Functions from MAKE-NATIVE, TEXT! strings of code, ..."
    :settings [block!] --[
        The block supports the following dialect:
            options [block! text!]
            include-path [block! file! text!]
            library-path [block! file! text!]
            library [block! file! text!]
            runtime-path [file! text!]
            librebol-path [file! text!]
            output-type ['memory 'exe 'dll 'obj 'preprocess]
            output-file [file! text!]
            debug [word!]  ; !!! currently unimplemented
    ]--
    :files "COMPILABLES represents a list of disk files (TEXT! paths)"
    :inspect "Return the C source code as text, but don't compile it"
    :nostdlib "Do not include <stdlib.h> automatically with librebol"
][
    if 0 = length of compilables [
        panic ["COMPILABLES must have at least one element"]
    ]

    ; !!! `settings` BLOCK! is preprocessed into a `config` OBJECT! for
    ; COMPILE*.  It's difficult to check a passed-in object for validity as
    ; well as add to it when needed:
    ; https://github.com/rebol/rebol-issues/issues/2334

    settings: default [[]]
    let config: make object! [
        ;
        ; This is a BLOCK! of TEXT!s (compiler switches).
        ;
        ; NOTE: Any double-quotes in this list are assumed to be for wrapping
        ; filenames with spaces in them.  So if you want a literal double
        ; quote in a -D instruction for a #define, it must be escaped with
        ; a backslash here.  The C99 command has to throw them in, because
        ; they would have been taken out by the shell processing.
        ;
        options: copy []

        include-path: copy []  ; block! of text!s (local directories)
        library-path: copy []  ; block! of text!s (local directories)
        library: copy []  ; block of text!s (local filenames)
        runtime-path: null  ; sets "CONFIG_TCCDIR" at runtime, text! or null
        librebol-path: null  ; alternative to "LIBREBOL_INCLUDE_DIR"
        output-type: null  ; will default to MEMORY
        output-file: null  ; not needed if MEMORY
    ]

    let b: settings
    until [tail? b] [
        let key: b.1
        let var: get inside config (match word! key else [
            panic ["COMPILE:SETTINGS parameter" key "is not supported"]
        ])
        b: next b
        if tail? b [
            panic ["Missing argument to" key "in COMPILE"]
        ]

        let arg: b.1
        b: next b

        if block? var [  ; at present, this always means multiple paths
            for-each 'item compose [(arg)] [
                switch:type item [
                    text! []
                    file! [item: file-to-local:full item]
                    panic ["Invalid item type for" key "-" type of item]
                ]

                ; If the var is supposed to be paths, should we check them
                ; here?  Doesn't work for lib short names or options, so
                ; maybe best to just let TCC deliver those errors.

                append var item
            ]
        ] else [  ; single settings
            if var [panic [key "multiply specified"]]

            switch key [
                'output-type [
                    if not word? arg [
                        panic [key "must be WORD!"]
                    ]
                    config.output-type: arg
                ]
                'output-file 'runtime-path 'librebol-path [
                    config.(key): switch:type arg [
                        file! [arg]
                        text! [local-to-file arg]
                        panic [key "must be TEXT! or FILE!"]
                    ]
                ]
                panic  ; unreachable
            ]
        ]
    ]

    config.output-type: default ['memory]

    if config.output-type <> 'memory [
        if not config.output-file [
            panic "If OUTPUT-TYPE is not MEMORY then OUTPUT-FILE must be set"
        ]
        config.output-file: my file-to-local:full
    ]

    ; !!! The pending concept is that there are embedded files in the TCC
    ; extension, and these files are extracted to the local filesystem in
    ; order to make them available.  This idea is being implemented, and it
    ; would mean adding to the include and lib directories.
    ;
    ; For now, if the options don't specify a `runtime-dir` use CONFIG_TCCDIR,
    ; which is a standard setting.

    config.runtime-path: default [any [
        local-to-file opt get-env "CONFIG_TCCDIR"  ; (backslashes on windows)

        ; !!! Guessing is probably a good idea in the long term, but in the
        ; short term it just creates unpredictability.  Avoid for now.
        ;
        ; match exists? %/usr/lib.x86_64-linux-gnu/tcc/
        ; match exists? %/usr/local/lib.tcc/
    ]]

    if not config.runtime-path [
        panic [
            "CONFIG_TCCDIR must be set in the environment or RUNTIME-PATH"
            "must be provided in the :SETTINGS"
        ]
    ]

    if not dir? config.runtime-path [
        print [
            "Runtime path" config.runtime-path "doesn't end in a slash,"
            "which violates the DIR? protocol, but as CONFIG_TCCDIR is often"
            "documented as being used without slashes we're allowing it."
        ]
        append config.runtime-path "/"
    ]

    if not exists? compose %(config.runtime-path)/include/ [
        panic [
            "Runtime path" config.runtime-path "does not have an %include/"
            "directory.  It should have files like %stddef.h and %stdarg.h"
            "because TCC has its own definition of macros like va_arg(), that"
            "use internal helpers like __va_start that are *not* in GNU libc"
            "or the Microsoft C runtime."
        ]
    ]

    ; If we are on Windows and no explicit include or lib paths were provided,
    ; try using the win32/include path.  %encap-tcc-resources.r puts those
    ; into the resource zip, and it gives *some* Win32 entry points.
    ;
    ; https://repo.or.cz/tinycc.git/blob/HEAD:/win32/tcc-win32.txt
    ;
    ; This is a convenience to make simple examples work.  Once someone starts
    ; wanting to control the paths directly they may want to omit these hacky
    ; stubs entirely (e.g. use the actual Windows SDK files, maybe?)

    if 3 = fourth system.version [  ; e.g. Windows (32-bit or 64-bit)
        if empty? config.include-path [
            append config.include-path file-to-local:full (
                compose %(config.runtime-path)/win32/include/
            )
        ]
        if empty? config.library-path [
            append config.library-path file-to-local:full (
                compose %(config.runtime-path)/win32/library/
            )
        ]

        ; !!! For unknown reasons, on Win32 it does not seem that the API
        ; call to `tcc_set_lib_path()` sets the CONFIG_TCCDIR in such a way
        ; that TCC can find %libtcc1.a.  So adding the runtime path as a
        ; normal library directory.
        ;
        insert config.library-path file-to-local:full config.runtime-path
    ]

    ; Note: The few header files in %tcc/include/ must out-prioritize the ones
    ; in the standard distribution, e.g. %/usr/include/stdarg.h.  TCC's files
    ; must basically appear first in the `-I` include paths, which makes
    ; them *out-prioritize the system headers* (!)
    ;
    ; https://stackoverflow.com/questions/53154898/

    insert config.include-path file-to-local:full (
        compose %(config.runtime-path)/include/
    )

    ; The other complicating factor is that once emitted code has these
    ; references to TCC-specific internal routines not in libc, the
    ; tcc_relocate() command used by the extension (API link step) has to
    ; be able to find definitions for them.  They live in %libtcc1.a,
    ; which is generally located in the CONFIG_TCCDIR.

    case [
        "1" = get-env "REBOL_TCC_EXTENSION_32BIT_ON_64BIT" [
            ;
            ; This is the verbatim list of library overrides that `-v` dumps
            ; on 64-bit multilib Liux compiling `int main() {}` with -m32:
            ;
            ;     gcc -v -m32 main.c -o main
            ;
            ; Otherwise, if you're trying to run a 32-bit Rebol on 64-bits
            ; then the link step of TCC would try to link to the 64-bit libs.
            ; This rarely comes up, because most people runnning a 32-bit
            ; Rebol are only doing so because they can't run on 64-bits.  But
            ; if you cross-compile to 32-bit and want to test, you need this.
            ;
            ; Better suggestions on how to do this are of course welcome.  :-/
            ;
            insert config.library-path spread [
                "/usr/lib.gcc/x86_64-linux-gnu/7/32/"
                "/usr/lib.gcc/x86_64-linux-gnu/7/../../../../lib32/"
                "/lib.../lib32/"
                "/usr/lib.../lib32/"
            ]
        ]

        ; !!! Note: The executable `tcc` (which implements POSIX command line
        ; conventions) uses libtcc to do its compilation.  But it adds lots of
        ; logic regarding computing standard include directories.  e.g. in
        ; %tcc.h you see things like:
        ;
        ;     #  define CONFIG_TCC_SYSINCLUDEPATHS \
        ;            "{B}/include" \
        ;         ":" ALSO_TRIPLET(CONFIG_SYSROOT "/usr/local/include") \
        ;         ":" ALSO_TRIPLET(CONFIG_SYSROOT "/usr/include")
        ;
        ;    #  define CONFIG_TCC_LIBPATHS \
        ;            ALSO_TRIPLET(CONFIG_SYSROOT "/usr/" CONFIG_LDDIR) \
        ;        ":" ALSO_TRIPLET(CONFIG_SYSROOT "/" CONFIG_LDDIR) \
        ;        ":" ALSO_TRIPLET(CONFIG_SYSROOT "/usr/local/" CONFIG_LDDIR)
        ;
        ; A bit of digging shows "triplet" might be <architecture>-<OS>-<lib>,
        ; e.g. `x86_64-linux-gnu`, and CONFIG_LDDIR is probably lib.  These
        ; directories are not automatic when using libtcc.  The Rebol COMPILE
        ; command would hopefully have enough smarts to be at least as good as
        ; the tcc command line tool...for now this works around it enough to
        ; help get the bootstrap demo going.
        ;
        4 = fourth system.version [  ; Linux
            let lddir: "lib"
            let triplet: if 40 = fifth system.version [  ; 64-bit
                "x86_64-linux-gnu"
            ]
            insert config.library-path spread compose [
                (unspaced ["/usr/" lddir])
                (? if triplet [unspaced ["/usr/" lddir "/" triplet]])
                (unspaced ["/" lddir])
                (? if triplet [unspaced ["/" lddir "/" triplet]])
                (unspaced ["/usr/local/" lddir])
                (? if triplet [unspaced ["/usr/local/" lddir "/" triplet]])
            ]
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

    let use-librebol: null

    compilables: map-each 'item compilables [
        if match [word! tuple!] :item [item: get inside compilables item]

        switch:type item [
            frame! [
                use-librebol: okay
                item
            ]
            text! [
                item
            ]
            file! [  ; Just an example idea... reading disk files?
                as text! read item
            ]
            panic ["COMPILABLES currently must be TEXT!/FRAME!/FILE!"]
        ]
    ]

    if use-librebol [
        insert compilables trim:auto mutable --[
            /* TCC's override of <stddef.h> defines int64_t in a way that
             * might not be compatible with glibc's <stdint.h> (which at time
             * of writing defines it as a `__int64_t`.)  You might get:
             *
             *     /usr/include/x86_64-linux-gnu/bits/stdint-intn.h:27:
             *         error: incompatible redefinition of 'int64_t'
             *
             * Since TCC's stddef.h has what rebol.h needs in it anyway, try
             * just including that.  Otherwise try getting a newer version of
             * libtcc, a different gcc install, or just disable warnings.
             */
            #define LIBREBOL_NO_STDINT 1
            #include <stddef.h>

            #define LIBREBOL_BINDING_NAME()  librebol_binding
            #include "rebol.h"

            RebolContext* librebol_binding = 0;  /* review... */
        ]--

        ; The nostdlib feature is specific to a bare-bones demo environment
        ; with only the r3 executable and the TCC-specific encap files.  The
        ; C code in such a environment will depend on libRebol for input and
        ; output, e.g. `rebElide("print")` vs. printf().
        ;
        ; While this may sound limiting in terms of "C as exposure to native
        ; functionality" it has two main applications:
        ;
        ; * Making it easy to test that compilation is working on devices in
        ;   CI tests without having to install a toolchain; especially useful
        ;   in ARM emulation.
        ;
        ; * Offering C as raw access to assembly for algorithms like crypto
        ;   and hashes, which really just need the math part of C.
        ;
        ; Note that libRebol provides memory allocation via rebAlloc() and
        ; rebFree(), so it's possible to write code that is not completely
        ; trivial...though you'd run into walls by not having the likes of
        ; memcpy() or memmove().  :-/  Long story short: useful for testing.
        ;
        if nostdlib [
            ;
            ; Needs to go before the librebol inclusion that was put at the
            ; head of the compilables above!
            ;
            insert compilables trim:auto mutable "#define LIBREBOL_NO_STDLIB"

            ; TCC adds -lc (by calling `tcc_add_library_err(s1, "c");`) by
            ; default during link, unless you override it with this switch.
            ;
            append config.options "-nostdlib"
        ]

        ; We want to embed and ship "rebol.h" automatically.  But as a first
        ; step, try overriding with the LIBREBOL_INCLUDE_DIR environment
        ; variable, if it wasn't explicitly passed in the options.

        config.librebol-path: default [any [
            get-env "LIBREBOL_INCLUDE_DIR"

            ; Guess it is in the runtime directory (%encap-tcc-resources.r
            ; puts it into the root of the zip file at the moment)
            ;
            config.runtime-path
        ]]

        ; We are going to test for %rebol.h in the path, so need a FILE!
        ;
        if not config.librebol-path [
            panic [
                "LIBREBOL_INCLUDE_DIR currently must be set either as an"
                "environment variable or as LIBREBOL-PATH in /OPTIONS so"
                -[that the TCC extension knows where to find "rebol.h"]-
                "(e.g. in %make/prep/include)"
            ]
        ]
        switch:type config.librebol-path [
            text! [config.librebol-path: my local-to-file]
            file! []
            panic ["Invalid LIBREBOL_INCLUDE_DIR:" config.librebol-path]
        ]

        if not dir? config.librebol-path [
            panic [
                "LIBREBOL_INCLUDE_DIR or LIBREBOL-PATH" config.librebol-path
                "should end in a slash to follow the DIR? protocol"
            ]
        ]

        if not exists? compose %(config.librebol-path)/rebol.h [
            panic [
                "Looked for %rebol.h in" config.librebol-path "and did not"
                "find it.  Check your definition of LIBREBOL_INCLUDE_DIR"
            ]
        ]

        insert config.include-path file-to-local config.librebol-path
    ]

    ; Having paths as Rebol FILE! is useful for doing work, but the TCC calls
    ; want local paths.  Convert.
    ;
    config.runtime-path: my file-to-local:full
    config.librebol-path: ~#[taken into account]#~  ; COMPILE* does not read

    let result: compile* // [
        compilables
        config
        files: files
        inspect: inspect
        librebol: use-librebol
    ]

    if inspect [
        print "== COMPILE/INSPECT CONFIGURATION =="
        probe config
        print "== COMPILE/INSPECT CODE =="
        print result
    ]
]


c99: func [
    "http://pubs.opengroup.org/onlinepubs/9699919799/utilities/c99.html"

    return: "Exit status code (try to match gcc/tcc)"
        [integer!]
    command "POSIX c99 invocation string (system.options.args if <end>)"
        [<end> block! text!]
    :inspect
    :runtime "Alternate way of specifying CONFIG_TCCDIR environment variable"
        [text! file!]
][
    command: default [system.options.args]  ; not set if end
    command: spaced command

    let compilables: copy []

    let nonspacedot: complement charset reduce [space tab cr lf "."]

    let infile: null  ; set to <multi> if multiple input files
    let outfile: null

    let outtype: null  ; default will be EXE (also overridden if `-c` or `-E`)

    let settings: collect [
        let option-no-arg-rule: [option: across to [space | <end>] (
            keep spread compose [options (option)]
        )]

        let option
        let option-with-arg-rule: [
            opt space option: across to [space | <end>] (
                ;
                ; If you do something like `option --[-DSTDIO_H="stdio.h"]--,
                ; TCC seems to process it like `-DSTDIO_H=stdio.h` which won't
                ; work with `#include STDIO_H`.  But if the command line had
                ; said `-DSTDIO_H=\"stdio.h\"` we would be receiving it
                ; after the shell processed it, so those quotes would be
                ; unescaped here.  Add the escaping back in for TCC.
                ;
                replace option -["]- -[\"]-

                keep spread compose [options (option)]
            )
        ]

        let known-extension-rule: ["." ["c" | "a" | "o"] ahead [space | <end>]]

        let filename
        let temp
        let last-pos
        let rule: [
            last-pos: <here>  ; Save for errors

            "-c" (  ; just compile (no link phase)
                keep spread compose [output-type (outtype: 'OBJ)]
                outfile: null  ; don't need to specify
            )
            |
            ahead "-D"  ; #define
            option-with-arg-rule
            |
            "-E" (  ; preprocess only, print to standard output (not file)
                keep spread compose [output-type (outtype: 'PREPROCESS)]
                outfile: null  ; don't need to specify
            )
            |
            ahead "-g"  ; include debug symbols (don't use with -s)
            option-no-arg-rule
            |
            "-I"  ; add directory to search for #include files
            opt space temp: across to [space | <end>] (
                keep spread compose [include-path (temp)]
            )
            |
            "-L"  ; add directory to search for library files
            opt space temp: across to [space | <end>] (
                keep spread compose [library-path (temp)]
            )
            |
            "-l"  ; add library (-llibrary means search for "liblibrary.a")
            opt space temp: across to [space | <end>] (
                keep spread compose [library (temp)]
            )
            |
            ahead "-O"  ; optimization level
            option-with-arg-rule
            |
            "-o"  ; output file (else default should be "a.out")
            opt space outfile: across to [space | <end>] (  ; overwrites a.out
                keep spread compose [output-file (outfile)]
            )
            |
            ahead "-s"  ; strip out any extra information (don't use with -g)
            option-no-arg-rule
            |
            ahead "-U"  ; #undef
            option-with-arg-rule
            |
            ahead "-W"  ; !!! Not technically POSIX, but we use it
            option-with-arg-rule
            |
            "-w" to [space | <end>]  ; !!! Disables warnings, also not POSIX
            (keep [options "-w"])  ; (...but tcc supports it, no arg)
            |
            "-rdynamic"  ; !!! Again, not POSIX, but TCC supports
            (keep [options "-rdynamic"])
            |
            filename: across [
                some [
                    nonspacedot
                    | ahead "." not ahead known-extension-rule skip 1
                ]
                known-extension-rule
            ] (
                append compilables filename
                infile: either infile [<multi>] [filename]
            )
        ]

        parse:case command [some [rule [some space | <end>]]] except [
            panic [
                elide trunc: ~
                "Could not parse C99 command line at:"
                append [_ trunc]: (mold:limit last-pos 40) if trunc ["..."]
            ]
        ]
    ]

    print ["Outtype is" mold reify outtype]

    if not outtype [  ; no -c or -E, so assume EXE
        append settings spread compose [output-type EXE]
    ]

    if not outfile [
        switch outtype [
            'EXE [
                ; Default to a.out (it's the POSIX way, but better that
                ; COMPILE error if you don't give it an output filename than
                ; just guess "a.out", so make that decision in this command)
                ;
                append settings spread compose [output-file ("a.out")]
            ]
            'OBJ [
                if infile != <multi> [
                    parse copy infile [thru change [".c" <end>] (".o")] except [
                        panic "Input file must end in `.c` for use with -c"
                    ]
                ]
            ]
        ]
    ]

    if runtime [  ; overrides search for environment variable CONFIG_TCCDIR
        append settings spread compose [runtime-path (runtime)]
    ]

    if inspect [
        print mold compilables
        print mold settings
    ]

    ; !!! This doesn't return the C source from COMPILE, should it?
    ;
    compile // [
        compilables
        files: okay  ; compilables represents a list of files
        inspect: inspect  ; return C source as text but don't compile it
        settings: settings
    ]

    return 0  ; must translate errors into integer codes...
]


bootstrap: func [
    "Download Rebol sources from GitHub and build using TCC"
    return: []
    :options "Use system.options.ARGS to get additional make.r options"
    :cheat "Use CALL to do the download and unzip vs. internal (faster)"
][
    ; We fetch the .ZIP file of the master branch from GitHub.  Note that this
    ; actually contains a subdirectory called `ren-c-master` which the
    ; source files are in.  We change into that directory.
    ;
    let zipped-url: https://codeload.github.com/metaeducation/ren-c/zip/master
    print ["Downloading and Unzipping Source From:" zipped-url]
    if cheat [
        write %master.zip (read zipped-url)
        call [unzip master.zip]
    ]
    else [
        unzip/quiet %. zipped-url
    ]

    ; We'd like to bundle the contents of the CONFIG_TCCDIR into the
    ; executable as part of the build process for the TCC extension.  The
    ; best way to do that would be a .ZIP file via the encap facility.  Since
    ; that hasn't been done, use fetching from a web build as a proxy for it.
    ;
    unzip/quiet %./tccencap https://metaeducation.s3.amazonaws.com/travis-builds/0.4.40/r3-06ac629-debug-cpp-tcc-encap.zip
    lib/set-env "CONFIG_TCCDIR" file-to-local compose %(what-dir)/tccencap/

    cd ren-c-master

    ; make.r will notice we are in the same directory as itself, and so it
    ; will make a %build/ subdirectory to do the building in.
    ;
    let status: lib/call [
        (system.options.boot) make.r
            "config=configs/bootstrap.r"
            (? if options [spread system.options.args])
    ]
    if status != 0 [
        panic ["BOOTSTRAP command failed with exit status:" status]
    ]
]


export [compile c99 bootstrap]
