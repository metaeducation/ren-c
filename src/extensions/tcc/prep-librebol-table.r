REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Scan rebol.h for API entry points to export via tcc_add_symbol()"
    File: %prep-librebol-table.r
    Rights: {
        Copyright 2017 Atronix Engineering
        Copyright 2017-2018 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        The TCC extension compiles user natives into memory directly.  These
        natives are linked against some libs that are on disk (the extension
        is packaged with some of these libraries that it extracts and puts
        on disk to use).  However, the API entry points for rebol.h are
        connected to the running executable directly with tcc_add_symbol().

        It might be slightly easier if the build process had a product which
        was a table of the API entry points, vs. scanning rebol.h.  But that
        would just add one more file to the project...it's likely better to
        treat the header as an interface.
    }
    Needs: 2.100.100
]

; This script is run from %src/extensions/tcc
; For now, just go back up a few steps

change-dir %../../../

do %make/tools/r2r3-future.r

do %make/tools/common.r
args: parse-args system/options/args

do %make/tools/common-emitter.r

do %make/tools/systems.r
system-config: config-system args/OS_ID

all [
    config-tccdir: try local-to-file try get-env "CONFIG_TCCDIR"
    exists? config-tccdir or [
        print ["CONFIG_TCCDIR setting is invalid" config-tccdir]
        false
    ]

    tcc-libtcc1-file: as file! try (
        get-env "TCC_LIBTCC1_FILE" else [config-tccdir/libtcc1.a]
    )
    exists? tcc-libtcc1-file or [
        print ["TCC_LIBTCC1_FILE setting is invalid" tcc-libtcc1-file]
        false
    ]
] else [
    fail [
        "TCC Extension requires CONFIG_TCCDIR environment variable to be"
        "set.  This is semi-standardized, as it's where compiled TCC"
        "programs look for the runtime (see `tcc_set_lib_dir()`).  If the"
        "file %libtcc1.a is somewhere other than that directory, then"
        "TCC_LIBTCC1_FILE should be its location.  See the README:"
        http://github.com/metaeducation/ren-c/src/extensions/tcc/README.md
    ]
]

; !!! Some list of properties should be bound into the prep code as being
; already available, so each prep doesn't have to re-discover them.
;
top-dir: try as file! try get-env "TOP_DIR"
exists? top-dir or [
    print ["TOP_DIR does not exist" top-dir]
    fail [
        "TOP_DIR should be set to where the git repository for Rebol was"
        "cloned into.  (This should be REBOL_SOURCE_DIR or something"
        "more clear, but it's what was configured on Travis CI)"
    ]
]

; Embedded files for supporting basic compilation that are extracted to the
; local filesystem when running a COMPILE.  A more elegant solution than
; extraction would involve changes to TCC, proposed here:
;
; http://lists.nongnu.org/archive/html/tinycc-devel/2018-12/msg00011.html
;
encap: compose [
    (top-dir/make/prep/include/rebol.h)

    ((switch system-config/os-base [
        ;
        ; https://repo.or.cz/tinycc.git/blob/HEAD:/win32/tcc-win32.txt
        ;
        'Windows [
            reduce [
                config-tccdir/include ;; typically %(...)/win32/include
                config-tccdir/lib ;; typically %(...)/win32/lib
            ]
        ]

        ; Only a few special headers needed for TCC, that are selectively
        ; used to override the system standard ones with `-I`:
        ;
        ; https://repo.or.cz/tinycc.git/tree/HEAD:/include
        ; https://stackoverflow.com/q/53154898
        ;
        'Posix
        'Android
        'Linux [
            config-tccdir/include ;; somewhere in /usr/lib, or tcc root

            ; No additional libs (besides libtcc1.a), TCC is compatible with
            ; GNU libc and other libraries on standard linux distributions.
        ]

        default [
            fail [
                "Only Linux and Windows are supported by the TCC extension"
                "(OS X looks to be possible, but it just hasn't been tried)"
            ]
        ]
    ]))

    (tcc-libtcc1-file)  ; See README.md for an explanation of %libtcc1.a
]

print "== IF TCC EXTENSION ENCAPPING WERE IMPLEMENTED, IT WOULD STORE THIS =="
print mold encap


comment [
print "------ Building embedded header file"
args: parse-args system/options/args
output-dir: system/options/path/prep
-- args output-dir
wait 5

** mkdir/deep output-dir/core

** e: (make-emitter
**    "Embedded sys-core.h" output-dir/prep/extensions/tcc/tmp-librebol-table.c)

** print "------ Writing embedded header file"
** e/write-emitted
]
