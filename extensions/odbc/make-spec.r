Rebol [
    name: ODBC
    notes: --[
        See %extensions/README.md for the format and fields of this file

     A. On macOS (at least Apple Silicon) the default you get if you say
        `-lodbc` is a dynamically linked libodbc.  You have to literally give
        the path to the static %libodbc.a library as a file.  We use CALL and
        ask Homebrew where it's installed.

        Note that if you *were* going to try using `-lodbc`, you'd have to add
        a library search path as well, e.g. `-L/opt/homebrew/lib`.  That's
        because assuming you install ODBC on a Mac using "homebrew", the
        "System Integrity Protection" model prevents Apple Silicon M1/M2/etc.
        versions of macOS from allowing it to install libraries in /usr/local
    ]--
]

options: [
    odbc-requires-ltdl [logic?] ()
]

use-librebol: 'yes  ; ODBC is a great example of not depending on %sys-core.h !

includes: switch platform-config.os-base [
    'macOS [
        [%/opt/homebrew/include/]  ; needed for Apple Silicon builds
    ]
]

sources: [
    mod-odbc.c [
        ;
        ; ODBCGetTryWaitValue() is prototyped as a C++-style void argument
        ; function, as opposed to ODBCGetTryWaitValue(void), which is the
        ; right way to do it in C.  But we can't change <sqlext.h>, so
        ; disable the warning.
        ;
        ;     'function' : no function prototype given:
        ;     converting '()' to '(void)'
        ;
        <msc:/wd4255>

        ; The ODBC include also uses nameless structs/unions, which are a
        ; non-standard extension.
        ;
        ;     nonstandard extension used: nameless struct/union
        ;
        <msc:/wd4201>
    ]
]

libraries: switch platform-config.os-base [
    'Windows [
        [%odbc32]
    ]
    'macOS [
        ; [%odbc]  ; would be dynamic linked, see [A] above
        []
    ]
] else [
    ; On some systems (32-bit Ubuntu 12.04), odbc requires ltdl
    ;
    compose [
        %odbc (? if yes? user-config.odbc-requires-ltdl [%ltdl])
    ]
]


ldflags: switch platform-config.os-base [
    ;
    ; 1. POSIX: "Upon successful canonicalization, realpath shall write the
    ;    canonicalized pathname, followed by a <newline> character..."
    ;
    ; 2. No trailing slash from realpath.  (Don't know if using realpath is
    ;    necessary here but AI suggested it.)
    ;
    'macOS [  ; see [A] above, -lodbc would give us dynamic linking
        let odbcpath: ""
        call:shell:output --[realpath "$(brew --prefix unixodbc)"]-- odbcpath

        assert [newline = take:last odbcpath]  ; [1]
        assert [#"/" <> last odbcpath]  ; [2]

        let ltdlpath: ""
        call:shell:output --[realpath "$(brew --prefix libtool)"]-- ltdlpath

        assert [newline = take:last ltdlpath]  ; [1]
        assert [#"/" <> last ltdlpath]  ; [2]

        print ["Brew ODBC on Mac Found in:" odbcpath]
        print ["Brew Libtool Dynamic Loader on Mac Found in:" ltdlpath]

        reduce [
            join odbcpath "/lib/libodbc.a"
            join ltdlpath "/lib/libltdl.a"

            "-liconv"  ; international conversion, e.g. iconv_close()
        ]
    ]
]
