REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "System build targets"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        These are target system definitions used to build Rebol with a
        various compilers and libraries.  A longstanding historical numbering
        scheme of `0.X.Y` is currently used.  X is a kind of generic indicator
        of the vendor or OS, and Y is a variant in architecture or linkage.
        If you examine `system/version` in Rebol, these numbers are the two at
        the tail of the tuple.  (The earlier tuple values indicate the Rebol
        interpreter version itself.)

        To try and keep things simple, this is just a short "dialected" list
        of memoized build settings for each target.  The memos translate into
        input to the make system, and that input mapping is after the table:

            #DEFINITIONS - e.g. #BEN becomes `#define ENDIAN_BIG`
            <CFLAGS> - switches that affect the C compilation command line
            /LDFLAGS - switches that affect the linker command line
            %LIBRARIES - what libraries to include

        If you have a comment to make about a build, add it in the form of a
        memo item that is a no-op, so the table is brief as possible.
    }
    Notes: {
        A binary release archive for Rebol 1.2 and 2.5 is at:

        http://rebol.com/release-archive.html

        Between versions 1 and 2 there were no conflicting usages of IDs.  But
        for unknown reasons, R3-Alpha repurposed 0.4.03 and 0.4.04.  These had
        been "Linux DEC Alpha" and "Linux PPC" respectively, but became
        "Linux x86 libc6 2.5" and "Linux x86 libc6 2.11".

        R3-Alpha was released on many fewer systems than previous versions.
        It demanded a 64-bit `long long` integer type from the C compiler, and
        additionally some platforms were just too old to be deemed relevant.
        However, there's probably no serious barrier to building the current
        sources on most older machines--if someone were interested.
    }
]


systems: [

    Amiga: 1
    ;-------------------------------------------------------------------------
    0.1.01 _ "m68k20+"
        ; was: "Amiga V2.0-3.1 68020+"

    0.1.02 _ "m68k"
        ; was: "Amiga V2.0-3.1 68000"

    0.1.03 amiga/posix "ppc"
        #SGD #BEN #LLC #F64 <NPS> <HID> /HID /DYN %M

    Macintosh: 2
    ;-------------------------------------------------------------------------
    0.2.01 _ "mac-ppc"
        ; was: "Macintosh* PPC" (not known what "*" meant)

    0.2.02 _ "mac-m68k"
        ; was: "Macintosh 68K"

    0.2.03 _ "mac-misc"
        ; was: "Macintosh, FAT PPC, 68K"

    0.2.04 osx-ppc/osx "osx-ppc"
        #SGD #BEN #LLC #F64 <NCM> /HID /DYN %M
        ; originally targeted OS X 10.2

    0.2.05 osx-x86/osx "osx-x86"
        #SGD #LEN #LLC #NSER #F64 <NCM> <NPS> <ARC> /HID /ARC /DYN %M

    0.2.40 osx-x64/osx _
        #SGD #LEN #LLC #NSER #F64 <NCM> <NPS> /HID /DYN %M

    Windows: 3
    ;-------------------------------------------------------------------------
    0.3.01 windows-x86/windows "win32-x86"
        #SGD #LEN #UNI #F64 #W32 #NSEC <WLOSS> /CON /S4M %W32 %M
        ; was: "Microsoft Windows XP/NT/2K/9X iX86"

    0.3.02 _ "dec-alpha"
        ; was: "Windows Alpha NT DEC Alpha"

    0.3.40 windows-x64/windows "win32-x64"
        #SGD #LEN #UNI #F64 #W32 #LLP64 #NSEC <WLOSS> /CON /S4M %W32 %M

    Linux: 4
    ;-------------------------------------------------------------------------
    0.4.01 _ "libc5-x86"
        ; was: "Linux Libc5 iX86 1.2.1.4.1 view-pro041.tar.gz"

    0.4.02 linux-x86/linux "libc6-2-3-x86"  ; gliblc-2.3
        #SGD #LEN #LLC #NSER #F64 <M32> <NSP> <UFS> /M32 %M %DL

    0.4.03 linux-x86/linux "libc6-2-5-x86"  ; gliblc-2.5
        #SGD #LEN #LLC #F64 <M32> <UFS> /M32 %M %DL

    0.4.04 linux-x86/linux "libc6-2-11-x86"  ; glibc-2.11
        #SGD #LEN #LLC #F64 #PIP2 <M32> <HID> /M32 /HID /DYN %M %DL

    0.4.05 _ _
        ; was: "Linux 68K"

    0.4.06 _ _
        ; was: "Linux Sparc"

    0.4.07 _ _
        ; was: "Linux UltraSparc"

    0.4.08 _ _
        ; was: "Linux Netwinder Strong ARM"

    0.4.09 _ _
        ; was: "Linux Cobalt Qube MIPS"

    0.4.10 linux-ppc/linux "libc6-ppc"
        #SGD #BEN #LLC #F64 #PIP2 <HID> /HID /DYN %M %DL

    0.4.11 linux-ppc64/linux "libc6-ppc64"
        #SGD #BEN #LLC #F64 #PIP2 #LP64 <HID> /HID /DYN %M %DL

    0.4.20 linux-arm/linux "libc6-arm"
        #SGD #LEN #LLC #F64 #PIP2 <HID> /HID /DYN %M %DL

    0.4.21 linux-arm/linux _  ; for modern Android builds, see Android section
        #SGD #LEN #LLC #F64 #PIP2 <HID> <PIE> /HID /DYN %M %DL

    0.4.22 linux-aarch64/linux "libc6-aarch64"
        #SGD #LEN #LLC #F64 #PIP2 #LP64 <HID> /HID /DYN %M %DL

    0.4.30 linux-mips/linux "libc6-mips"
        #SGD #LEN #LLC #F64 #PIP2 <HID> /HID /DYN %M %DL

    0.4.31 linux-mips32be/linux "libc6-mips32be"
        #SGD #BEN #LLC #F64 #PIP2 <HID> /HID /DYN %M %DL

    0.4.40 linux-x64/linux "libc-x64"
        #SGD #LEN #LLC #F64 #PIP2 #LP64 <HID> /HID /DYN %M %DL

    0.4.60 linux-axp/linux "dec-alpha"
        #SGD #LEN #LLC #F64 #PIP2 #LP64 <HID> /HID /DYN %M %DL

    0.4.61 linux-ia64/linux "libc-ia64"
        #SGD #LEN #LLC #F64 #PIP2 #LP64 <HID> /HID /DYN %M %DL

    BeOS: 5
    ;-------------------------------------------------------------------------
    0.5.1 _ _
        ; labeled "BeOS R5 PPC" in Rebol 1.2, but "BeOS R4 PPC" in Rebol 2.5

    0.5.2 _ _
        ; was: "BeOS R5 iX86"

    0.5.75 haiku/posix "x86-32"
        #SGD #LEN #LLC %NWK

    BSDi: 6
    ;-------------------------------------------------------------------------
    0.6.01 _ "x86"
        ; was: "BSDi iX86"

    FreeBSD: 7
    ;-------------------------------------------------------------------------
    0.7.01 _ "x86"
        ; was: "Free BSD iX86"

    0.7.02 freebsd-x86/posix "elf-x86"
        #SGD #LEN #LLC #F64 %M

    0.7.40 freebsd-x64/posix _
        #SGD #LEN #LLC #F64 #LP64 %M

    NetBSD: 8
    ;-------------------------------------------------------------------------
    0.8.01 _ "x86"
        ; was: "NetBSD iX86"

    0.8.02 _ "ppc"
        ; was: "NetBSD PPC"

    0.8.03 _ "m68k"
        ; was: "NetBSD 68K"

    0.8.04 _ "dec-alpha"
        ; was: "NetBSD DEC Alpha"

    0.8.05 _ "sparc"
        ; was: "NetBSD Sparc"

    OpenBSD: 9
    ;-------------------------------------------------------------------------
    0.9.01 _ "x86"
        ; was: "OpenBSD iX86"

    0.9.02 _ "ppc"
        ; Not mentioned in archive, but stubbed in R3-Alpha's %platforms.r

    0.9.03 _ "m68k"
        ; was: "OpenBSD 68K"

    0.9.04 openbsd-x86/posix "elf-x86"
        #SGD #LEN #LLC #F64 %M

    0.9.05 _ "sparc"
        ; was: "OpenBSD Sparc"

    0.9.40 openbsd-x64/posix "elf-x64"
        #SGD #LEN #LLC #F64 #LP64 %M

    Sun: 10
    ;-------------------------------------------------------------------------
    0.10.01 _ "sparc"
        ; was: "Sun Solaris Sparc"

    0.10.02 _ _
        ; was: "Solaris iX86"

    SGI: 11
    ;-------------------------------------------------------------------------
    0.11.0 _ _
        ; was: "SGI IRIX SGI"

    HP: 12
    ;-------------------------------------------------------------------------
    0.12.0 _ _
        ; was: "HP HP-UX HP"

    Android: 13
    ;-------------------------------------------------------------------------
    0.13.01 android-arm/android "arm"
        #SGD #LEN #LLC #F64 <HID> <PIC> /HID /DYN %M %DL %LOG

    0.13.02 android5-arm/android _
        #SGD #LEN #LLC #F64 <HID> <PIC> /HID /PIE /DYN %M %DL %LOG

    Syllable: 14
    ;-------------------------------------------------------------------------
    0.14.01 syllable-dtp/posix _
        #SGD #LEN #LLC #F64 <HID> /HID /DYN %M %DL

    0.14.02 syllable-svr/linux _
        #SGD #LEN #LLC #F64 <M32> <HID> /HID /DYN %M %DL

    WindowsCE: 15
    ;-------------------------------------------------------------------------
    0.15.01 _ "sh3"
        ; was: "Windows CE 2.0 SH3"

    0.15.02 _ "mips"
        ; was: "Windows CE 2.0 MIPS"

    0.15.05 _ "arm"
        ; was: "Windows CE 2.0 Strong ARM, HP820"

    0.15.06 _ "sh4"
        ; was: "Windows CE 2.0 SH4"

    Emscripten: 16
    ;-------------------------------------------------------------------------
    0.16.01 asyncify/emscripten "asyncify"
        #SG? #LEN

    0.16.02 pthread/emscripten "emscripten"
        #SG? #LEN

    0.16.03 node/emscripten "nodejs"
        #SG? #LEN

    AIX: 17
    ;-------------------------------------------------------------------------
    0.17.0 _ _
        ; was: "IBM AIX RS6000"

    SCO-Unix: 19
    ;-------------------------------------------------------------------------
    0.19.0 _ _
        ; was: "SCO Unix iX86"

    QNX: 22
    ;-------------------------------------------------------------------------
    0.22.0 _ _
        ; was: "QNX RTOS iX86"

    SCO-Server: 24
    ;-------------------------------------------------------------------------
    0.24.0 _ _
        ; was: "SCO Open Server iX86"

    Tao: 27
    ;-------------------------------------------------------------------------
    0.27.0 _ _
        ; was: "Tao Elate/Intent VP"

    RTP: 28
    ;-------------------------------------------------------------------------
    0.28.0 _ _
        ; was: "RTP iX86"
]


system-definitions: make object! [
    LP64: "__LP64__"              ; 64-bit, and 'void *' is sizeof(long)
    LLP64: "__LLP64__"            ; 64-bit, and 'void *' is sizeof(long long)

    ; !!! There is a reason these are not BIG_ENDIAN and LITTLE_ENDIAN; those
    ; terms are defined in some code that Rebol uses a shared include with
    ; as integer values (like `#define BIG_ENDIAN 7`).
    ;
    BEN: "ENDIAN_BIG"             ; big endian byte order
    LEN: "ENDIAN_LITTLE"          ; little endian byte order

    ; !!! This doesn't seem to be used anywhere in the code.
    ;
    LLC: "HAS_LL_CONSTS"          ; supports e.g. 0xffffffffffffffffLL
    ;LL?: _                       ; might have LL consts, reb-config.h checks

    ; See C_STACK_OVERFLOWING for an explanation of the dodgy technique used
    ; to try and preempt a C stackoverflow crash with a trappable error. 
    ;
    SGD: "OS_STACK_GROWS_DOWN"    ; most widespread choice in C compilers
    ;SGU: "OS_STACK_GROWS_UP"     ; rarer (Debian HPPA, some emscripten/wasm)
    SG?: _                        ; try to detect growth direction at runtime

    W32: <msc:WIN32>              ; aes.c requires this
    UNI: "UNICODE"                ; win32 wants it
    F64: "_FILE_OFFSET_BITS=64"   ; allow larger files

    ; MSC deprecates all non-*_s version string functions.  Ren-C has been
    ; constantly tested with ASAN, which should mitigate the issue somewhat.
    ;
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
        ; conversion from 'type1' to 'type2', possible loss of data
        ;
        <msc:/wd4244>

        ; conversion from 'size_t' to 'type', possible loss of data
        ;
        <msc:/wd4267>
    ]
]

linker-flags: make object! [
    M32: <gnu:-m32>
    ARC: <gnu:-arch i386>
    PIE: <gnu:-pie>
    HID: <gnu:-fvisibility=hidden>  ; all syms are hidden

    ; The `-rdynamic` option is not a POSIX C option, but makes symbols from
    ; the executable visible.  This is generally used for debugging (e.g.
    ; backtrace()), but may have interesting applications in letting a file
    ; be both an executable and a shared library providing libRebol services.
    ; https://stackoverflow.com/q/36692315
    ;
    DYN: <gnu:-rdynamic>

    CON: [<gnu:-mconsole> <msc:/subsystem:console>]
    S4M: [<gnu:-Wl,--stack=4194300> <msc:/stack:4194300>]
]

system-libraries: make object! [
    ;
    ; Math library, needed only when compiling with GCC
    ; (Haiku has it in libroot)
    ;
    M: <gnu:m>

    DL: "dl" ; dynamic lib
    LOG: "log" ; Link with liblog.so on Android
    
    W32: ["wsock32" "comdlg32" "user32" "shell32" "advapi32"]
    
    NWK: "network" ; Needed by HaikuOS
]


for-each-system: function [
    {Use PARSE to enumerate the systems, and set 'var to a record object}

    return: <void>
    'var [word!]
    body [block! action!]
        {Body of code to run for each system}
][
    s: make object! [
        platform-name: _
        platform-number: _
        id: _
        os: _
        os-name: _
        os-base: _
        build-label: _
        definitions: _
        cflags: _
        libraries: _
        ldflags: _
    ]

    parse systems in s [ some [
        set platform-name set-word! (
            platform-name: to-word platform-name
        )
        set platform-number integer!
        any [
            set id tuple!
            [blank! (
                os: _
                os-name: _
                os-base: _
            )
                |
            set os path! (
                os-name: os/1
                os-base: os/2
            )]
            [
                blank! (build-label: _)
                    |
                set build-label text! (
                    build-label: to-word build-label
                )
            ]
            copy definitions [any issue!] (
                definitions: map-each x definitions [to-word x]
            )
            copy cflags [any tag!] (
                cflags: map-each x cflags [to-word to-text x]
            )
            copy ldflags [any refinement!] (
                ldflags: map-each x ldflags [to-word x]
            )
            copy libraries [any file!] (
                libraries: map-each x libraries [to-word to-text x]
            )

            (
                if os [
                    set var s
                    do body
                ]
            )
        ]
    ] end ] else [
        fail "Couldn't parse systems.r table"
    ]
]


; Do a little bit of sanity-checking on the systems table
use [
    unknown-flags used-flags build-flags word context
][
    used-flags: copy []
    for-each-system s [
        assert in s [
            word? platform-name
            integer? platform-number
            any [
                word? build-label
                blank? build-label
            ]
            tuple? id
            all [
                id/1 = 0
                id/2 = platform-number
            ]
            (to-text os-name) == (lowercase to-text os-name)
            (to-text os-base) == (lowercase to-text os-base)
            not find (to-text os-base) charset [#"-" #"_"]
            block? definitions
            block? cflags
            block? libraries
            block? ldflags
        ]

        for-each flag s/definitions [assert [word? flag]]
        for-each flag s/cflags [assert [word? flag]]
        for-each flag s/libraries [assert [word? flag]]
        for-each flag s/ldflags [assert [word? flag]]

        for-each [word context] compose/only [
            definitions (system-definitions)
            libraries (system-libraries)
            cflags (compiler-flags)
            ldflags (linker-flags)
        ][
            ; Exclude should mutate (CC#2222), but this works either way
            unknown-flags: exclude (
                    unknown_flags: copy any [build-flags: get in s word []]
                )
                words-of context
            if not empty? unknown-flags [
                print mold unknown-flags
                fail ["Unknown" word "used in %systems.r specification"]
            ]
            used-flags: union used-flags any [build-flags []]
        ]
    ]

    unused-flags: exclude compose [
        ((words-of compiler-flags))
        ((words-of linker-flags))
        ((words-of system-definitions))
        ((words-of system-libraries))
    ] used-flags

    if not empty? unused-flags [
        print mold unused-flags
        fail "Unused flags in %systems.r specifications"
    ]
]


config-system: function [
    {Return build configuration information}
    hint [blank! text! tuple!]
        {Version ID (blank means guess)}
][
    version: switch type of hint [
        blank! [  ; Try same version as this r3-make was built with
            to tuple! reduce [0 system/version/4 system/version/5]
        ]
        text! [load hint]
        tuple! [hint]
    ]

    if not tuple? version [
        fail ["Expected OS_ID tuple like 0.3.1, not:" version]
    ]

    result: _
    for-each-system s [
        if s/id = version [
            result: copy s  ; could RETURN, but sanity-check whole table
        ]
    ]

    if not result [
        fail [
            {No table entry for} version {found in systems.r}
        ]
    ]

    result
]
