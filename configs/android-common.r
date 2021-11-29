REBOL [
    File: %android-common.r
]

os-id: 0.13.2


; The Android NDK is the dev kit for building C/C++ programs for Android.
; (The Android SDK is for building Java programs, plus tools like emulators.)
;
; Regardless of whether you are cross-compiling from Windows/Mac/Linux or
; building on a phone itself, an NDK version must be installed.  There is no
; standardized environment variable for NDK installs, we use ANDROID_NDK_ROOT
; because it lines up well with the SDK's standardized ANDROID_SDK_ROOT.
;
; NDK (r)evisions have labels like `r13` or `r21d`.  This label is not to be
; confused with the "API Level" numbers...each NDK offers multiple API levels:
;
; https://developer.android.com/ndk/downloads/revision_history
;
ndk-root: local-to-file try get-env "ANDROID_NDK_ROOT" else [
    fail "Must set ANDROID_NDK_ROOT environment variable to build for Android"
]
(
    if #"/" <> last ndk-root [
        print "ANDROID_NDK_ROOT environment variable doesn't end in slash"
        print "(Allowing it, but Rebol convention is directories end in slash)"
        append ndk-root #"/"
    ]
)


; Revision `r11` and newer have a source.properties file at the root of
; the NDK we can use to get the version.
;
; https://stackoverflow.com/a/43182345
;
; Sample contents:
;
;     Pkg.Desc = Android NDK
;     Pkg.Revision = 13.1.3345770
;
ndk-version: make object! [major: minor: patch: _]
(
    use [major minor patch] [
        parse2 as text! read (join ndk-root %source.properties) [
            thru "Pkg.Revision = "
            copy major: to "." skip (major: to integer! major)
            copy minor: to "." skip (minor: to integer! minor)
            copy patch: to [end | newline] (patch: to integer! patch)
            to end
        ] else [
            fail "Can't parse source.properties in ANDROID_NDK_ROOT directory"
        ]
        print ["NDK VERSION DETECTED:" unspaced [major "." minor "." patch]]
        ndk-version/major: major
        ndk-version/minor: minor
        ndk-version/patch: patch
    ]
)


; https://developer.android.com/ndk/guides/other_build_systems
;
; Pathological cases could arise where you want to use a Windows system to
; build the prep files to go take and put on a Linux system that you are
; going to use to cross-compile to produce an interpreter for Android.  But
; for now let's just assume that if you're cross-compiling to Android and
; you are on a Windows machine, you intend to run the make on Windows.  (!)
;
detect-ndk-host: func [
    return: [word!]
] [
    switch let os: system/version/4 [
        2 ['darwin-x86_64]  ; Mac
        3 ['windows-x86_64]  ; Windows  !!! 32bit is just `windows`, ignore atm
        4 ['linux-x86_64]  ; Linux
        fail ["Unsupported Cross-Compilation Host:" system/version]
    ]
]


; The Android NDK was changed to use clang as of 18b, prior to that it was gcc.
;
; https://android.googlesource.com/platform/ndk.git/+/master/docs/ClangMigration.md
;
; This returns the same tool for both compiling and linking (since GCC and
; Clang both dispatch to the linker).  If you try to call the linker directly,
; it is more informative by telling you things it doesn't understand that the
; front end does...but it seems there's directory set up done by the clang
; and gcc you miss out on, so it won't find libraries and has other errors.
;
tool-for-host: func [
    return: [file! text!]
    tool [tag!] "<COMPILER> or <LINKER>"
    /host [word!] "defaults to detecting current system, e.g. linux-x86_64"
    /abi [word!] "defaults to 32-bit ARM (arm-linux-androideabi)"
] [
    host: default [detect-ndk-host]
    abi: default ['arm-linux-androideabi]  ; But ARM64 is up-and-coming... :-/

    let path: either ndk-version/major < 18 [  ; GCC build still available
        ;
        ; !!! Review the 4.9 non-sequitur in this older method.  What's that?
        ;
        join ndk-root reduce [
             %toolchains/ (abi) "-4.9" "/prebuilt/"
                (host) "/" %bin/
                    (abi) -gcc
        ]
    ] [
        ; Although ld and other tools for arm have names like:
        ;
        ;   arm-linux-androideabi-ld
        ;
        ; The clang filenames have an API Level number in them, and on the arm
        ; abi in particular they add "v7", e.g.
        ;
        ;   armv7a-linux-androideabi29-clang  ; 29 is an android-api-level
        ;
        all [
            abi = 'arm-linux-androideabi
        ] then [
            abi: 'armv7a-linux-androideabi
        ]

        join ndk-root reduce [
            %toolchains/llvm/prebuilt/ (host) "/" %bin/
                (abi) (android-api-level) "-clang"
        ]

        ; Note: there's an alternative new naming method offering a single
        ; `clang` with ABI name passed as a `-target`.
        ;
        ; https://developer.android.com/ndk/guides/other_build_systems
        ;
        ; We use the glommed together name, for consistency (and makes it
        ; easier to test if the variation we want exists).
    ]

    if not exists? path [
        fail ["TOOL-FOR-HOST could not find:" mold path]
    ]

    ; It turns out that clang is dispatching to GNU's `ld` version of the
    ; linker instead of the LLVM `ld.lld` in the same directory:
    ;
    ; https://stackoverflow.com/questions/38287464/with-clang-to-arm-wrong-linker
    ;
    ; They presumably made that choice for a reason?  So we don't override it
    ; by default, but this is where you could do so with `-fuse-ld=lld`.
    ;
    ; (Note that returning `ld.lld` as the linker directly has the problems
    ; mentioned about not having library directories and such set up by the
    ; clang front end, so slipping the option into clang is the right way.)
    ;
    comment [
        all [
            ndk-version/major >= 18
            tool = <linker>  ; will give warning if used with plain compile
        ] then [
            return spaced [path "-fuse-ld=lld"]
        ]
    ]

    return path
]


; The NDK revisions (e.g. r13 or r21b) are distinct from platform "API levels".
; e.g. NDK r21b offers suppport for API levels android-16 thru android-29
; (but android-20 and android-25 are missing, because 20 is exclusive to
; "Android Wear" and not in the normal NDK...and 25 just seems to have been
; deprecated for some reason).
;
; Google Play requires new applications uploaded to be at least android-29
; at time of writing:
;
; https://developer.android.com/distribute/best-practices/develop/target-sdk
;
; For the moment, we assume if you installed an NDK so old that it has the
; GCC toolchain, then you're trying to be retro on purpose and build for old
; hardware.  So we continue to target android-19 as a proof-of-low-dependency.
; Otherwise, android-29 is used to ensure we can build things for Google Play.
;
; !!! This should probably have a better defaulting mechanism...maybe even
; querying the internet to find out what Google Play accepts at a minimum (!)
; but also looking to see what the maximum offered by your installed NDK
; has in its $NDK/platforms directory.  But just get it working for now...
;
android-api-level: either ndk-version/major < 18 [
    19  ; A low common denominator
][
    29  ; Make sure we can build something Google Play Store could accept
]


; Prior to revision r14 the include headers could be found broken out on a
; per-platform basis, just like the lib headers were.  So for instance you
; would find several versions of <stdio.h> in $NDK/platforms/xxx.
;
; After r14, the headers were unified for all targets into a single
; directory, of $NDK/sysroot
;
; https://android.googlesource.com/platform/ndk/+/master/docs/UnifiedHeaders.md
;
; The new unified header usage became mandatory in r16.
;
sysroot-for-compile: func [
    return: [text!]
] [
    let path: spaced [
        "--sysroot" (file-to-local either ndk-version/major < 18 [
             [
                ;
                ; Old convention: per-API-Level header files
                ;
                (ndk-root) platforms / android- (android-api-level) /arch-arm
            ]
        ][
            ; New convention: headers unified, with differences controlled by
            ; the preprocessor defines, e.g. `-D__ANDROID_API__=29`
            ;
            join ndk-root %sysroot
        ])
    ]

    return path
]


; lib files contain machine code and thus obviously need to vary per platform
; for the link.  Currently we're only focusing on targeting ARM.
;
; Beware if you get the message:
;
;     ld.lld: error: unable to find library -lunwind
;
; This happens in the new toolchain if you are using the wrong sysroot in
; a link, e.g. the %platforms/android-XX/arch-arm from the older GCC
; toolchain.  Although GCC was deleted, that stuff is still there...and
; it will doesn't have the LLVM unwind information!
;
; Note that while unwinding is typically used in C++ programs, Android links it
; in as well...because on many Android devices the CPU lacks a hardware integer
; divide instruction.  So a bit of code in libgcc.a explicitly throws SIGFPE:
;
; https://gcc.gnu.org/legacy-ml/gcc-help/2012-03/msg00371.html
;
; libgcc is a small library that clang also uses; it implicitly puts it on the
; link line as `-lgcc`.  Not to be confused with `glibc`.  You can disable
; the inclusion of -lgcc (and thus libunwind) in C programs with linker option
; `-rtlib=compiler-rt`...but then you can't do things like integer division.
;
sysroot-for-link: func [
    return: [text!]
    /host [word!] "defaults to detecting current system, e.g. linux-x86_64"
] [
    host: default [detect-ndk-host]

    let path: spaced [
        "--sysroot" (file-to-local either ndk-version/major < 18 [
            ;
            ; Old convention: linker sysroot lives in %platforms/
            ;
            join ndk-root reduce [
                %platforms/ "android-" (android-api-level) "/" "arch-arm"
            ]
        ][
            ; New convention: linker sysroot lives in %toolchains/llvm/
            ;
            join ndk-root reduce [
                %toolchains/llvm/prebuilt/ (host) "/" %sysroot/
            ]
        ])
    ]

    return path
]
