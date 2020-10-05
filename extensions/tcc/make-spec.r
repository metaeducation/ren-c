REBOL [
    Title: {TCC Extension Rebmake Compiling/Linking Information}
]

name: 'TCC

source: %tcc/mod-tcc.c

includes: [
    %prep/extensions/tcc
]


; If they installed libtcc with `sudo apt-get libtcc-dev`, then the switches
; for `-ltcc` and `#include "libtcc.h" should just work.  Otherwise, they
; have to do `export LIBTCC_INCLUDE_DIR=...` and `export LIBTCC_LIB_DIR=...`
;
; But for convenience, we try to use CONFIG_TCCDIR if they have that set *and*
; it has %libtcc.h in it.  Then it's *probably* a directory TCC was cloned
; and built in--not just where the helper library libtcc1.a was installed.

config-tccdir-with-libtcc-h: try all [
    ;
    ; CONFIG_TCCDIR will have backslashes on Windows, use LOCAL-TO-FILE on it.
    ;
    config-tccdir: local-to-file (try get-env "CONFIG_TCCDIR")

    elide (if #"/" <> last config-tccdir [
        print "NOTE: CONFIG_TCCDIR environment variable doesn't end in '/'"
        print "That's *usually* bad, but since TCC documentation tends to"
        print "suggest you write it that way, so this extension allows it."
        print unspaced ["CONFIG_TCCDIR=" config-tccdir]
        append config-tccdir "/"  ; normalize to the standard DIR? rule
    ])

    exists? make-file [(config-tccdir) libtcc.h]
    config-tccdir
]

libtcc-include-dir: try any [
    local-to-file try get-env "LIBTCC_INCLUDE_DIR"
    config-tccdir-with-libtcc-h
]

libtcc-lib-dir: try any [
    local-to-file try get-env "LIBTCC_LIB_DIR"
    config-tccdir-with-libtcc-h
]


cflags: compose [
    ;
    ; strtold() is used by TCC in non-Windows builds.  It was a C99 addition,
    ; and Android NDKs (at least older ones) do not have it, so the link
    ; step would fail if someone didn't define it.  So this flag controls
    ; having the TCC extension define a function that just runs strtod().
    ;
    (if "1" = get-env "NEEDS_FAKE_STRTOLD" ["-DNEEDS_FAKE_STRTOLD"])

    (if libtcc-include-dir [
        unspaced [{-I} {"} file-to-local libtcc-include-dir {"}]
    ])
]

ldflags: compose [
    (if libtcc-lib-dir [
        unspaced [{-L} {"} file-to-local libtcc-lib-dir {"}]
    ])
]

libraries: compose [  ; Note: dependent libraries first, dependencies after.
    %tcc

    ; As of 10-Dec-2019, pthreads became a dependency for libtcc on linux:
    ;
    ; https://repo.or.cz/tinycc.git?a=commit;h=72729d8e360489416146d6d4fd6bc57c9c72c29b
    ; https://repo.or.cz/tinycc.git/blobdiff/6082dd62bb496ea4863f8a5501e480ffab775395..72729d8e360489416146d6d4fd6bc57c9c72c29b:/Makefile
    ;
    ; It would be nice if there were some sort of compilation option for the
    ; library that let you pick whether you needed it or not.  But right now
    ; there isn't, so just include pthread.  Note that Android includes the
    ; pthread ability by default, so you shouldn't do -lpthread:
    ;
    ; https://stackoverflow.com/a/38672664/
    ;
    (if not find [Windows Android] system-config/os-base [%pthread])
]

requires: [
    Filesystem  ; uses LOCAL-TO-FILE
]
