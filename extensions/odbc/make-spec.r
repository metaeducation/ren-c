Rebol [
    name: ODBC
    notes: "See %extensions/README.md for the format and fields of this file"
]

options: [
    odbc-requires-ltdl [logic?] ()
]

use-librebol: 'yes  ; ODBC is a great example of not depending on %sys-core.h !

includes: switch platform-config.os-base [
    'MacOS [
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
    'MacOS [
        [%odbc]
    ]
] else [
    ; On some systems (32-bit Ubuntu 12.04), odbc requires ltdl
    ;
    compose [
        %odbc (? if yes? user-config.odbc-requires-ltdl [%ltdl])
    ]
]
