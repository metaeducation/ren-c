Rebol [
    title: "TCC Extension Rebmake Compiling/Linking Information"
    name: TCC
    notes: --[
        See %extensions/README.md for the format and fields of this file"

     A. If you installed libtcc with `sudo apt-get libtcc-dev`, then the
        switches for `-ltcc` and `#include "libtcc.h" should just work.

        Otherwise, you have to set environment variables to where TCC is:

            export LIBTCC_INCLUDE_DIR=...
            export LIBTCC_LIB_DIR=...

        But for convenience, we try to use CONFIG_TCCDIR if you have that set
        *and* it has %libtcc.h in it.  Then it's *probably* a directory TCC
        was cloned and built in--not just where the helper library libtcc1.a
        was installed.
    ]--
]

requires: 'Filesystem  ; uses LOCAL-TO-FILE

sources: %mod-tcc.c

includes: []

config-tccdir-with-libtcc-h: all [  ; do some guesswork, see [A] above
    ;
    ; CONFIG_TCCDIR will have backslashes on Windows, use LOCAL-TO-FILE on it.
    ;
    let config-tccdir: local-to-file opt (get-env "CONFIG_TCCDIR")

    elide if #"/" <> last config-tccdir [
        print "NOTE: CONFIG_TCCDIR environment variable doesn't end in '/'"
        print "That's *usually* bad, but since TCC documentation tends to"
        print "suggest you write it that way, this extension allows it."
        print unspaced ["CONFIG_TCCDIR=" config-tccdir]
        append config-tccdir "/"  ; normalize to the standard DIR? rule
    ]

    exists? (join config-tccdir %libtcc.h)
    <- config-tccdir
]

libtcc-include-dir: any [
    local-to-file opt get-env "LIBTCC_INCLUDE_DIR"
    config-tccdir-with-libtcc-h
]

libtcc-lib-dir: any [
    local-to-file opt get-env "LIBTCC_LIB_DIR"
    config-tccdir-with-libtcc-h
]


cflags: compose [
    (? if libtcc-include-dir [
        unspaced ["-I" -["]- file-to-local libtcc-include-dir -["]-]
    ])
]

ldflags: compose [
    (? if libtcc-lib-dir [
        unspaced [{-L} -["]- file-to-local libtcc-lib-dir -["]-]
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
    (? if not find [Windows Android] platform-config.os-base [%pthread])
]
