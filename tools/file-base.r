Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "Source File Database"
    rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributos
        REBOL is a trademark of REBOL Technologies
    }
    license: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    purpose: {
        Lists of files used for creating makefiles.
    }
]

; NOTE: In the following file list, a (+) preceding a file is indicative that
; the file is generated.
core: [
    ; (A)???
    %a-constants.c
    %a-globals.c
    %a-lib.c

    ; (B)oot
    %b-init.c

    ; (C)ore
    %c-bind.c
    %c-do.c
    %c-context.c
    %c-error.c
    %c-eval.c
    %c-function.c
    %c-path.c
    %c-port.c
    %c-signal.c
    %c-specialize.c
    %c-value.c
    %c-word.c

    ; (D)ebug
    %d-crash.c
    %d-dump.c
    %d-eval.c
    %d-print.c
    %d-stack.c
    %d-stats.c

    ; (F)???
    %f-blocks.c
    [
        %f-dtoa.c

        ; f-dtoa.c comes from a third party, and should be updated from their
        ; code if they change their policies, including Spectre mitigation:
        ;
        <msc:/wd5045> ;-- https://stackoverflow.com/q/50399940
        <msc:/wd4146>  ; unary minus operator applied to unsigned type

        <gcc:-Wno-cast-qual>  ; e.g. `*sp = (char*)s0 - 1;`
        <gcc:-Wno-unused-const-variable>  ; e.g. `tinytens`, `bigtens`, `tens`

        <no-sign-compare>
        <no-uninitialized>
        <implicit-fallthru>
    ]
    [
        %f-enbase.c

        ; At time of writing there are 4 Spectre mitigations, which should
        ; be looked at and rewritten when there is time:
        ;
        <msc:/wd5045> ;-- https://stackoverflow.com/q/50399940
    ]
    %f-extension.c
    %f-int.c
    %f-math.c
    %f-modify.c
    [
        %f-qsort.c
        <clang:-Wno-null-pointer-subtraction>
    ]
    %f-random.c
    %f-round.c
    %f-series.c
    %f-stubs.c

    ; (L)exer
    %l-scan.c
    %l-types.c

    ; (M)emory
    %m-gc.c
    [%m-pools.c <no-uninitialized>]
    %m-series.c
    %m-stacks.c

    ; (N)atives
    %n-control.c
    %n-data.c
    %n-do.c
    %n-error.c
    %n-function.c
    %n-io.c
    %n-loop.c
    %n-math.c
    %n-protect.c
    %n-reduce.c
    %n-sets.c
    %n-strings.c
    %n-system.c

    ; (P)orts
    %p-console.c
    %p-dir.c
    %p-dns.c
    %p-event.c
    %p-file.c
    %p-net.c

    ; (S)trings
    %s-cases.c
    %s-crc.c
    %s-file.c
    %s-find.c
    %s-make.c
    %s-mold.c
    %s-ops.c
    %s-unicode.c

    ; (T)ypes
    %t-bitset.c
    %t-blank.c
    %t-block.c
    %t-char.c
    %t-datatype.c
    %t-date.c
    %t-decimal.c
    %t-event.c
    %t-function.c
    %t-integer.c
    %t-logic.c
    %t-map.c
    %t-object.c
    %t-pair.c
    %t-port.c
    %t-string.c
    %t-time.c
    %t-tuple.c
    %t-typeset.c
    %t-varargs.c
    %t-word.c

    ; (U)??? (3rd-party code extractions)
    %u-compress.c
    [%u-md5.c <implicit-fallthru>]
    %u-parse.c
    [
        %u-sha1.c
        <implicit-fallthru>
        <no-hidden-local>
    ][
        %u-zlib.c

        <no-make-header>
        <implicit-fallthru>
        <no-constant-conditional>

        <clang:-Wno-unused-const-variable>
        <clang:-Wno-strict-prototypes>  ; in C mode, has () instead of (void)

        ; Zlib is an active project so it would be worth it to check to see
        ; if minor patches for suverting Spectre mitigation would be taken.
        ;
        <msc:/wd5045> ;-- https://stackoverflow.com/q/50399940
    ]
]

; Files created by the make-boot process
;
generated: [
    %tmp-boot-block.c
    %tmp-dispatchers.c
]

made: [
    %make-boot.r         core/tmp-boot-block.c
    %make-headers.r      include/tmp-internals.h

    %make-host-init.r    include/tmp-host-start.inc
    %make-os-ext.r       include/host-lib.h
    %make-librebol.r     include/rebol.h
]

;
; NOTE: In the following file lists, a (+) preceding a file is indicative that
; it is to be searched for comment blocks around the function prototypes
; that indicate the function is to be gathered to be put into the host-lib.h
; exports.  (This is similar to what make-headers.r does when it runs over
; the Rebol Core sources, except for the host.)
;

main: %host-main.c

os: [
    + %host-device.c
    %host-table.c
    %dev-net.c
    %dev-dns.c
]

os-windows: [
    + windows/host-lib.c
    windows/dev-stdio.c
    windows/dev-file.c
    windows/dev-event.c
]

os-posix: [
    posix/host-readline.c
    posix/dev-stdio.c
    posix/dev-event.c
    posix/dev-file.c

    + posix/host-browse.c
    + posix/host-process.c
    + posix/host-time.c
    + posix/host-exec-path.c
]

os-osx: [
    ; OSX uses the POSIX file I/O for now
    posix/host-readline.c
    posix/dev-stdio.c
    posix/dev-event.c
    posix/dev-file.c

    + posix/host-browse.c
    + posix/host-process.c
    + posix/host-time.c
    + osx/host-exec-path.c
]

; The Rebol open source build did not differentiate between linux and simply
; posix builds.  However Atronix R3/View uses a different `os-base` name.
; make.r requires an `os-(os-base)` entry here for each named target.
;
os-linux: [
    ; Linux uses the POSIX file I/O for now
    posix/host-readline.c
    posix/dev-stdio.c
    posix/dev-file.c

    ; It also uses POSIX for most host functions
    + posix/host-process.c
    + posix/host-time.c
    + posix/host-exec-path.c

    ; Linux has some kind of MIME-based opening vs. posix /usr/bin/open
    + linux/host-browse.c

    ; Atronix dev-event.c for linux depends on X11, and core builds should
    ; not be using X11 as a dependency (probably)
    posix/dev-event.c

    ; Linux supports siginfo_t-style signals
    linux/dev-signal.c
]

; cloned from os-linux TODO: check'n'fix !!
os-android: [
    ; Android uses the POSIX file I/O for now
    posix/host-readline.c
    posix/dev-stdio.c
    posix/dev-file.c

    ; It also uses POSIX for most host functions
    + posix/host-process.c
    + posix/host-time.c
    + posix/host-exec-path.c

    ; Android  has some kind of MIME-based opening vs. posix /usr/bin/open
    + linux/host-browse.c

    ; Atronix dev-event.c for linux depends on X11, and core builds should
    ; not be using X11 as a dependency (probably)
    posix/dev-event.c

    ; Android don't supports siginfo_t-style signals
    ; linux/dev-signal.c
]

boot-files: [
    %version.r
]

mezz-files: [
    ;-- There were some of these in the R3/View build
]

prot-files: [
    %prot-http.r
]

tools: [
    %make-host-init.r
    %make-host-ext.r
]
