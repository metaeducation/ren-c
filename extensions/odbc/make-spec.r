REBOL []

name: 'ODBC

source: [
    %odbc/mod-odbc.c

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

includes: [
    %prep/extensions/odbc ;for %tmp-ext-odbc-init.inc
]

libraries: switch platform-config/os-base [
    'Windows [
        [%odbc32]
    ]
] else [
    ; On some systems (32-bit Ubuntu 12.04), odbc requires ltdl
    ;
    assert [logic? user-config/odbc-requires-ltdl]
    compose [
        %odbc (if user-config/odbc-requires-ltdl [%ltdl])
    ]
]

options: [
    odbc-requires-ltdl [logic!] ()
]
