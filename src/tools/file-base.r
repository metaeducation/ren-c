REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Source File Database"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributos
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        Lists of files used for creating makefiles.
    }
]

; NOTE: In the following file list, a (+) preceding a file is indicative that
; the file is generated.
core: [
    ; (A)???
    a-constants.c
    a-globals.c
    a-lib.c

    ; (B)oot
    b-init.c

    ; (C)ore
    c-bind.c
    c-do.c
    c-context.c
    c-error.c
    c-eval.c
    c-function.c
    c-path.c
    c-port.c
    c-signal.c
    c-value.c
    c-word.c

    ; (D)ebug
    d-break.c
    d-crash.c
    d-dump.c
    d-eval.c
    d-legacy.c
    d-print.c
    d-stack.c
    d-trace.c

    ; (F)???
    f-blocks.c
    f-deci.c
    [f-dtoa.c <no-uninitialized>]
    f-enbase.c
    f-extension.c
    f-int.c
    f-math.c
    f-modify.c
    f-qsort.c
    f-random.c
    f-round.c
    f-series.c
    f-stubs.c

    ; (L)exer
    l-scan.c
    l-types.c

    ; (M)emory
    m-gc.c
    [m-pools.c <no-uninitialized>]
    m-series.c
    m-stacks.c

    ; (N)atives
    n-control.c
    n-data.c
    n-do.c
    n-error.c
    n-function.c
    n-io.c
    n-loop.c
    n-math.c
    n-native.c
    n-protect.c
    n-reduce.c
    n-sets.c
    n-strings.c
    n-system.c
    n-textcodecs.c ; !!! should be moved to extensions

    ; (P)orts
    p-clipboard.c
    p-console.c
    p-dir.c
    p-dns.c
    p-event.c
    p-file.c
    p-net.c
    p-serial.c
    p-signal.c
;   p-timer.c ;--Marked as unimplemented

    ; (S)trings
    s-cases.c
    s-crc.c
    s-file.c
    s-find.c
    s-make.c
    s-mold.c
    s-ops.c
    s-trim.c
    s-unicode.c

    ; (T)ypes
    t-bitset.c
    t-blank.c
    t-block.c
    t-char.c
    t-datatype.c
    t-date.c
    t-decimal.c
    t-event.c
    t-function.c
    t-gob.c
    [t-image.c <no-uninitialized>]
    t-integer.c
    t-library.c
    t-logic.c
    t-map.c
    t-money.c
    t-object.c
    t-pair.c
    t-port.c
    t-routine.c
    t-string.c
    t-struct.c
    t-time.c
    t-tuple.c
    t-typeset.c
    t-varargs.c
    t-vector.c
    t-word.c

    ; (U)??? (3rd-party code extractions)
    u-compress.c
    u-md5.c
    u-parse.c
    u-sha1.c
    u-zlib.c
]

; Files created by the make-boot process
;
generated: [
    tmp-boot-block.c
    tmp-evaltypes.c
    tmp-maketypes.c
    tmp-comptypes.c
]

modules: [
    ;name module-file other-files
    Crypt ../extensions/mod-crypt.c [
        ../codecs/aes/aes.c
        ../codecs/bigint/bigint.c
        ../codecs/dh/dh.c
        ../codecs/rc4/rc4.c
        ../codecs/rsa/rsa.c
        ../codecs/sha256/sha256.c
    ]

    LodePNG ../extensions/mod-lodepng.c [../codecs/png/lodepng.c]

    uPNG ../extensions/u-png.c []

    GIF ../extensions/mod-gif.c []

    JPG ../extensions/mod-jpg.c [
        ;
        ; The JPG sources come from elsewhere; invasive maintenance for
        ; compiler rigor is not worthwhile to be out of sync with original.
        ;
        [../extensions/u-jpg.c <no-unused-parameter> <no-shift-negative-value>]
    ]

    BMP ../extensions/mod-bmp.c []
]

extensions: [
    ; [+ (builtin) | - (not builtin)] ext-name ext-file modules (defined in modules)
    + Crypt ../extensions/ext-crypt.c [Crypt]
    + PNG ../extensions/ext-png.c [LodePNG uPNG]
    + GIF ../extensions/ext-gif.c [GIF]
    + JPG ../extensions/ext-jpg.c [JPG]
    + BMP ../extensions/ext-bmp.c [BMP]
]

made: [
    make-boot.r         core/tmp-boot-block.c
    make-headers.r      include/tmp-funcs.h

    make-host-init.r    include/host-init.h
    make-os-ext.r       include/host-lib.h
    make-reb-lib.r      include/reb-lib.h
]

;
; NOTE: In the following file lists, a (+) preceding a file is indicative that
; it is to be searched for comment blocks around the function prototypes
; that indicate the function is to be gathered to be put into the host-lib.h
; exports.  (This is similar to what make-headers.r does when it runs over
; the Rebol Core sources, except for the host.)
;

os: [
    host-main.c
    + host-device.c
    host-stdio.c
    host-table.c
    dev-net.c
    dev-dns.c
]

os-windows: [
    + generic/host-memory.c

    + windows/host-lib.c
    windows/dev-stdio.c
    windows/dev-file.c
    windows/dev-event.c
    windows/dev-clipboard.c
    windows/dev-serial.c
]

os-posix: [
    + generic/host-memory.c
    + generic/host-locale.c
    generic/iso-639.c
    generic/iso-3166.c
    + generic/host-gob.c

    + stub/host-encap.c

    posix/host-readline.c
    posix/dev-stdio.c
    posix/dev-event.c
    posix/dev-file.c

    + posix/host-browse.c
    + posix/host-config.c
    + posix/host-error.c
    + posix/host-library.c
    + posix/host-process.c
    + posix/host-time.c
]

os-osx: [
    + generic/host-memory.c
    + generic/host-locale.c
    generic/iso-639.c
    generic/iso-3166.c
    + generic/host-gob.c

    + stub/host-encap.c

    ; OSX uses the POSIX file I/O for now
    posix/host-readline.c
    posix/dev-stdio.c
    posix/dev-event.c
    posix/dev-file.c
    posix/dev-serial.c

    + posix/host-browse.c
    + posix/host-config.c
    + posix/host-error.c
    + posix/host-library.c
    + posix/host-process.c
    + posix/host-time.c
]

; The Rebol open source build did not differentiate between linux and simply
; posix builds.  However Atronix R3/View uses a different `os-base` name.
; make-make.r requires an `os-(os-base)` entry here for each named target.
;
os-linux: [
    + generic/host-memory.c
    + generic/host-locale.c
    generic/iso-639.c
    generic/iso-3166.c
    + generic/host-gob.c

    ; Linux uses the POSIX file I/O for now
    posix/host-readline.c
    posix/dev-stdio.c
    posix/dev-file.c

    ; It also uses POSIX for most host functions
    + posix/host-config.c
    + posix/host-error.c
    + posix/host-library.c
    + posix/host-process.c
    + posix/host-time.c

    ; Linux has some kind of MIME-based opening vs. posix /usr/bin/open
    + linux/host-browse.c

    ; Atronix dev-event.c for linux depends on X11, and core builds should
    ; not be using X11 as a dependency (probably)
    posix/dev-event.c

    ; dev-serial should work on Linux and posix
    posix/dev-serial.c

    ; Linux has support for ELF format encapping
    + linux/host-encap.c

    ; Linux supports siginfo_t-style signals
    linux/dev-signal.c
]

; cloned from os-linux TODO: check'n'fix !!
os-android: [ 
    + generic/host-memory.c
    + generic/host-locale.c
    generic/iso-639.c
    generic/iso-3166.c
    + generic/host-gob.c

    ; Android uses the POSIX file I/O for now
    posix/host-readline.c
    posix/dev-stdio.c
    posix/dev-file.c

    ; It also uses POSIX for most host functions
    + posix/host-config.c
    + posix/host-error.c
    + posix/host-library.c
    + posix/host-process.c
    + posix/host-time.c

    ; Android  has some kind of MIME-based opening vs. posix /usr/bin/open
    + linux/host-browse.c

    ; Atronix dev-event.c for linux depends on X11, and core builds should
    ; not be using X11 as a dependency (probably)
    posix/dev-event.c

    ; Serial should work on Android too
    posix/dev-serial.c

    ; Android has support for ELF format encapping
    + linux/host-encap.c

    ; Android don't supports siginfo_t-style signals
    ; linux/dev-signal.c
]

boot-files: [
    version.r
]

mezz-files: [
    ;-- There were some of these in the R3/View build
]

prot-files: [
    prot-tls.r
    prot-http.r
]

tools: [
    make-host-init.r
    make-host-ext.r
    form-header.r
]
