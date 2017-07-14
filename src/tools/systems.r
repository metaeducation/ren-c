REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "System build targets"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        These are the target system definitions used to build REBOL
        with a variety of compilers and libraries.  We prefer to keep it
        simple like this rather than using a complex configuration tool
        that could make it difficult to support REBOL on older platforms.

        Note that these numbers for the OS are the minor numbers at the
        tail of the system/version tuple.  (The first tuple values are
        used for the Rebol code version itself.)

        If you have a comment to make about a build, make it in the
        form of a flag...even if the functionality for that flag is a no-op
        (signaled by a BLANK!).  This keeps the table clean and readable.

        This file uses a table format processed by routines in %common.r,
        so be sure to include that via DO before calling CONFIG-SYSTEM.
    }
]

systems: [
    ;-------------------------------------------------------------------------
    [id         os-name         os-base    definitions             cflags
                 libraries      ldflags]
    ;-------------------------------------------------------------------------
    0.1.03      amiga           posix   [BEN LLC F64]               [NPS HID]
                [M]             [HID DYN]
    ;-------------------------------------------------------------------------
    0.2.04      osx-ppc         osx     [BEN LLC F64]               [NCM]
                [M]             [HID DYN]

    0.2.05      osx-x86         osx     [LEN LLC NSER F64]          [NCM NPS ARC]
                [M]             [HID ARC DYN]

    0.2.40      osx-x64         osx     [LEN LLC NSER F64]          [NCM NPS]
                [M]             [HID DYN]

    ;-------------------------------------------------------------------------
    0.3.01      windows-x86     windows [LEN UNI F64 W32 NSEC]      [WLOSS]
                [W32 M]         [CON S4M]

    0.3.40      windows-x64     windows [LEN UNI F64 W32 LLP64 NSEC] [WLOSS]
                [W32 M]         [CON S4M]

    ;-------------------------------------------------------------------------
    0.4.02      linux-x86       linux   [LEN LLC NSER F64]          [M32 NSP UFS]
                [M DL]          [M32];gliblc-2.3

    0.4.03      linux-x86       linux   [LEN LLC F64]               [M32 UFS]
                [M DL]          [M32];gliblc-2.5

    0.4.04      linux-x86       linux   [LEN LLC F64 PIP2]          [M32 HID]
                [M DL]          [M32 HID DYN];glibc-2.11

    0.4.10      linux-ppc       linux   [BEN LLC F64 PIP2]          [HID]
                [M DL]          [HID DYN]

    0.4.11      linux-ppc64     linux   [BEN LLC F64 PIP2 LP64]     [HID]
                [M DL]          [HID DYN]

    0.4.20      linux-arm       linux   [LEN LLC F64 PIP2]          [HID]
                [M DL]          [HID DYN]

    0.4.21      linux-arm       linux   [LEN LLC F64 PIP2]          [HID PIE]
                [M DL]          [HID DYN]   ;android

    0.4.22      linux-aarch64   linux   [LEN LLC F64 PIP2 LP64]     [HID]
                [M DL]          [HID DYN]

    0.4.30      linux-mips      linux   [LEN LLC F64 PIP2]          [HID]
                [M DL]          [HID DYN]

    0.4.31      linux-mips32be  linux   [BEN LLC F64 PIP2]          [HID]
                [M DL]          [HID DYN]

    0.4.40      linux-x64       linux   [LEN LLC F64 PIP2 LP64]     [HID]
                [M DL]          [HID DYN]

    0.4.60      linux-axp       linux   [LEN LLC F64 PIP2 LP64]     [HID]
                [M DL]          [HID DYN]

    0.4.61      linux-ia64      linux   [LEN LLC F64 PIP2 LP64]     [HID]
                [M DL]          [HID DYN]

    ;-------------------------------------------------------------------------
    0.5.75      haiku           posix   [LEN LLC]                   []
                [NWK]           []

    ;-------------------------------------------------------------------------
    0.7.02      freebsd-x86     posix   [LEN LLC F64]               []
                [M]             []

    0.7.40      freebsd-x64     posix   [LEN LLC F64 LP64]          []
                [M]             []
    ;-------------------------------------------------------------------------
    0.9.04      openbsd-x86     posix   [LEN LLC F64]               []
                [M]             []

    0.9.40      openbsd-x64     posix   [LEN LLC F64 LP64]          []
                [M]             []

    ;-------------------------------------------------------------------------
    0.13.01     android-arm     android [LEN LLC F64]               [HID PIC]
                [M DL LOG]      [HID DYN]

    ;-------------------------------------------------------------------------
    0.13.02     android5-arm    android [LEN LLC F64]               [HID PIC]
                [M DL LOG]      [HID PIE DYN]

    ;-------------------------------------------------------------------------
    0.14.01     syllable-dtp    posix   [LEN LLC F64]               [HID]
                [M DL]             [HID DYN]

    0.14.02     syllable-svr    linux   [LEN LLC F64]               [M32 HID]
                [M DL]             [HID DYN]
]

system-definitions: make object! [
    LP64: "__LP64__"              ; 64-bit, and 'void *' is sizeof(long)
    LLP64: "__LLP64__"            ; 64-bit, and 'void *' is sizeof(long long)

    BEN: "ENDIAN_BIG"             ; big endian byte order
    LEN: "ENDIAN_LITTLE"          ; little endian byte order

    LLC: "HAS_LL_CONSTS"          ; supports e.g. 0xffffffffffffffffLL
    ;LL?: _                       ; might have LL consts, reb-config.h checks

    W32: <msc:WIN32>              ; aes.c requires this
    UNI: "UNICODE"                ; win32 wants it
    F64: "_FILE_OFFSET_BITS=64"   ; allow larger files

    ; MSC deprecates all non-*_s version string functions
    ; As Ren-C has been constantly tested with ASAN, this shouldn't be an issue.
    NSEC: <msc:_CRT_SECURE_NO_WARNINGS>


    ; There are variations in what functions different compiler versions will
    ; wind up linking in to support the same standard C functions.  This
    ; means it is not possible to a-priori know what libc version that
    ; compiler's build product will depend on when using a shared libc.so
    ;
    ; To get a list of the glibc stubs your build depends on, run this:
    ;
    ;     objdump -T ./r3 | fgrep GLIBC
    ;
    ; Notably, increased security measures caused functions like poll() and
    ; longjmp() to link to checked versions available only in later libc,
    ; or to automatically insert stack_chk calls for stack protection:
    ;
    ; http://stackoverflow.com/a/35404501/211160
    ; http://unix.stackexchange.com/a/92780/118919
    ;
    ; As compilers evolve, the workarounds to make them effectively cross
    ; compile to older versions of the same platform will become more complex.
    ; Switches that are needed to achieve this compilation may not be
    ; supported by old compilers.  This simple build system is not prepared
    ; to handle both "platform" and "compiler" variations; each OS_ID is
    ; intended to be used with the standard compiler for that platform.
    ;
    PIP2: "USE_PIPE2_NOT_PIPE"    ; pipe2() linux only, glibc 2.9 or later
    NSER:                         ; strerror_r() in glibc 2.3.4, not 2.3.0
        "USE_STRERROR_NOT_STRERROR_R"
]

compiler-flags: make object! [
    M32: <gnu:-m32>                 ;use 32-bit memory model
    ARC: <gnu:-arch i386>           ; x86 32 bit architecture (OSX)
    HID: <gnu:-fvisibility=hidden>  ; all sysms are hidden
    NPS: <gnu:-Wno-pointer-sign>    ; OSX fix
    PIE: <gnu:-fPIE>                ; position independent (executable)
    NCM: <gnu:-fno-common>          ; lib cannot have common vars
    UFS: <gnu:-U_FORTIFY_SOURCE>    ; _chk variants of C calls

    ; See comments about the glibc version above
    NSP: <gnu:-fno-stack-protector> ; stack protect pulls in glibc 2.4 calls
    PIC: <gnu:-fPIC>                ; Android requires this

    WLOSS: [
        <msc:/wd4244>               ; conversion' conversion from 'type1' to 'type2', possible loss of data
        <msc:/wd4267>               ; var' : conversion from 'size_t' to 'type', possible loss of data
    ]
]

system-libraries: make object! [
    M: <gnu:m>                      ; Math library (Haiku has it in libroot), needed only when compiled with GCC
    DL: "dl"                        ; dynamic lib
    LOG: "log"                      ; Link with liblog.so on Android
    W32: ["wsock32" "comdlg32" "user32" "shell32" "advapi32"]
    NWK: "network"                  ; Nedded by HaikuOS
]

linker-flags: make object! [
    M32: <gnu:-m32>
    ARC: <gnu:-arch i386>
    PIE: <gnu:-pie>
    HID: <gnu:-fvisibility=hidden>  ; all sysms are hidden
    DYN: <gnu:-rdynamic>

    CON: [<gnu:-mconsole> <msc:/subsystem:console>]
    S4M: [<gnu:-Wl,--stack=4194300> <msc:/stack:4194300>]
]

; A little bit of sanity-checking on the systems table
use [unknown-flags used-flags build-flags word context] [
    used-flags: copy []
    for-each-record rec systems [
        ;print ["rec =>" mold rec]
        assert [
            | tuple? rec/id
            | (to-string rec/os-name) == (lowercase to-string rec/os-name)
            | (to-string rec/os-base) == (lowercase to-string rec/os-base)
            | not find (to-string rec/os-base) charset [#"-" #"_"]
            | block? rec/definitions
            | block? rec/cflags
            | block? rec/libraries
            | block? rec/ldflags
        ]

        for-each flag rec/definitions [assert [word? flag]]
        for-each flag rec/cflags [assert [word? flag]]
        for-each flag rec/libraries [assert [word? flag]]
        for-each flag rec/ldflags [assert [word? flag]]

        for-each [word context] reduce [
            'definitions system-definitions
            'libraries system-libraries
            'cflags     compiler-flags
            'ldflags    linker-flags
        ][
            ; Exclude should mutate (CC#2222), but this works either way
            unknown-flags: exclude (
                    unknown_flags: copy any [build-flags: get in rec word []]
                )
                words-of context
            unless empty? unknown-flags [
                print mold unknown-flags
                fail ["Unknown" word "used in %systems.r specification"]
            ]
            used-flags: union used-flags any [build-flags []]
        ]

    ]

    unused-flags: exclude compose [
        (words-of compiler-flags)
        (words-of linker-flags)
        (words-of system-definitions)
        (words-of system-libraries)
    ] used-flags

    if not empty? unused-flags [
        print mold unused-flags
        fail "Unused flags in %systems.r specifications"
    ]
]


config-system: function [
    {Return build configuration information}
    hint [blank! string! tuple!]
        {Version ID (blank means guess)}
][
    version: case [
        blank? hint [
            ;
            ; Try same version as this r3-make was built with
            ;
            to tuple! reduce [0 system/version/4 system/version/5]
        ]
        string? hint [
            load hint
        ]
        tuple? hint [
            hint
        ]
    ]

    unless tuple? version [
        fail [
            "Expected OS_ID tuple like 0.3.1, not:" version
        ]
    ]

    unless result: find-record-unique systems 'id version [
        fail [
            {No table entry for} version {found in systems.r}
        ]
    ]

    result
]
