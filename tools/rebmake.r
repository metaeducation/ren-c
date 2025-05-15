Rebol [
    file: %rebmake.r
    title: "Rebol-Based C/C++ Makefile and Project File Generator"

    type: module
    name: Rebmake

    rights: --[
        Copyright 2017 Atronix Engineering
        Copyright 2017-2025 Ren-C Open Source Contributors
    ]--

    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--

    description: --[
        R3-Alpha's bootstrap process depended on the GNU Make Tool, with a
        makefile generated from minor adjustments to a boilerplate copy of
        the makefile text.  As needs grew, a second build process arose
        which used CMake...which was also capable of creating files for
        various IDEs, such as Visual Studio.

        %rebmake.r arose to try and reconcile these two build processes, and
        eliminate dependency on an external make tool completely.  It can
        generate makefiles for GNU Make or Microsoft's Nmake, or just conduct
        a full build by invoking compiler processes and command lines itself.

        In theory this code is abstracted such that it could be used by other
        projects.  In practice, it is tailored to the specific needs and
        settings of the Rebol project.
    ]--

    warning: --[
        This code is not representative of modern practices, because it has
        to run in a very old bootstrap executable.  It is also very much a
        hodgepodge just to keep things running.  It's the absolute wrong place
        to be looking for exemplary Ren-C code.
    ]--
]

if not find (words of import/) 'into [  ; See %import-shim.r
    do <import-shim.r>
]

import <bootstrap-shim.r>

default-compiler: null
default-stripper: null

target-platform: null

map-files-to-local: func [
    return: [block!]
    files [<opt-out> file! block!]
][
    if not block? files [files: reduce [files]]
    return map-each 'f files [
        file-to-local f
    ]
]

ends-with?: func [
    return: [logic?]
    s [any-string?]
    suffix [<undo-opt> any-string?]
][
    return to-logic any [  ; TO-LOGIC for bootstrap (xxx? returns #[true])
        null? suffix
        empty? suffix
        suffix ?= (skip tail of s negate length of suffix)  ; ?= lax compare
    ]
]

filter-flag: func [
    return: [null? text! file!]
    flag "If TAG! then <prefix:flag>, e.g. <gcc:-Wno-unknown-warning>"
        [tag! text! file!]
    prefixes "gnu -> GCC-compatible compilers, msc -> Microsoft C"
        [text! block!]
][
    if not tag? flag [return flag]  ; no filtering

    let header
    let option
    parse3:match as text! flag [  ; !!! should ACROSS of TAG! make TEXT! ?
        header: across to ":"
        ":" option: across to <end>
    ] else [
        panic ["Tag must be <prefix:flag> ->" (flag)]
    ]

    return all [
        find (blockify prefixes) header
        option
    ]
]

run-command: func [
    return: [text!]
    cmd [block! text!]
][
    let x: copy ""
    call:shell:output cmd x
    return trim:with x "^/^M"
]

pkg-config: func [  ; !!! Note: Does not appear to be used
    return: [text! block!]
    pkg [any-string?]
    var [word!]
    lib [any-string?]
][
    let [dlm opt]
    switch var [
        'includes [
            dlm: "-I"
            opt: "--cflags-only-I"
        ]
        'searches [
            dlm: "-L"
            opt: "--libs-only-L"
        ]
        'libraries [
            dlm: "-l"
            opt: "--libs-only-l"
        ]
        'cflags [
            dlm: null
            opt: "--cflags-only-other"
        ]
        'ldflags [
            dlm: null
            opt: "--libs-only-other"
        ]
        panic ["Unsupported pkg-config word:" var]
    ]

    let x: run-command spaced [pkg lib]

    if not dlm [
        return x
    ]

    let ret: make block! 1
    let item
    parse3 x [
        some [
            thru dlm
            item: across to [dlm | <end>] (
                append ret to file! item
            )
        ]
    ]
    return ret
]

platform-class: make object! [
    name: ~
    exe-suffix: ~
    dll-suffix: ~
    archive-suffix: ~  ;static library
    obj-suffix: ~

    gen-cmd-create: ~
    gen-cmd-delete: ~
    gen-cmd-strip: ~
]

unknown-platform: make platform-class [
    name: 'unknown
]

posix: make platform-class [
    name: 'POSIX
    exe-suffix: ""
    dll-suffix: ".so"
    obj-suffix: ".o"
    archive-suffix: ".a"

    gen-cmd-create: method [
        return: [text!]
        cmd [object!]
    ][
        return either dir? cmd.file [
            spaced ["mkdir -p" cmd.file]
        ][
            spaced ["touch" cmd.file]
        ]
    ]

    gen-cmd-delete: method [
        return: [text!]
        cmd [object!]
    ][
        return spaced ["rm -fr" cmd.file]
    ]

    gen-cmd-strip: method [
        return: [text!]
        cmd [object!]
    ][
        if let tool: any [get $cmd.strip, get $default-stripper] [
            let b: ensure block! tool/commands cmd.file opt cmd.options
            assert [1 = length of b]
            return b.1
        ]
        return ""
    ]
]

linux: make posix [
    name: 'Linux
]

haiku: make posix [
    name: 'Haiku
]

android: make linux [
    name: 'Android
]

emscripten: make posix [
    name: 'Emscripten
    exe-suffix: ".wasm"
    dll-suffix: ".js"  ; !!! We want libr3.js for "main" lib, but .so for rest
]

osx: make posix [
    name: 'OSX
    dll-suffix: ".dylib"  ; !!! This was .dyn - but no one uses that
]

windows: make platform-class [
    name: 'Windows

    exe-suffix: ".exe"
    dll-suffix: ".dll"
    obj-suffix: ".obj"
    archive-suffix: ".lib"

    gen-cmd-create: method [
        return: [text!]
        cmd [object!]
    ][
        let f: file-to-local cmd.file
        if #"\" = last f [remove back tail of f]
        return either dir? cmd.file [
            spaced ["if not exist" f "mkdir" f]
        ][
            unspaced ["echo . 2>" f]
        ]
    ]

    gen-cmd-delete: method [
        return: [text!]
        cmd [object!]
    ][
        let f: file-to-local cmd.file
        if #"\" = last f [remove back tail of f]
        return either dir? cmd.file [
            ;
            ; Note: If you have Git shell tools installed on Windows, then
            ; `rmdir` here might run `C:\Program Files\Git\usr\bin\rmdir.EXE`
            ; and not understand the /S /Q flags.  `rd` is an alias.
            ;
            spaced ["if exist" f "rd /S /Q" f]
        ][
            spaced ["if exist" f "del /Q" f]
        ]
    ]

    gen-cmd-strip: method [
        return: [text!]
        cmd [object!]
    ][
        print "Note: STRIP command not implemented for MSVC"
        return ""
    ]
]

set-target-platform: func [
    return: []
    platform
][
    switch platform [
        'Posix [
            target-platform: posix
        ]
        'Linux [
            target-platform: linux
        ]
        'Haiku [
            target-platform: haiku
        ]
        'Android [
            target-platform: android
        ]
        'Windows [
            target-platform: windows
        ]
        'MacOS [
            target-platform: osx
        ]
        'Emscripten [
            target-platform: emscripten
        ]
    ] else [
        print ["Unknown platform:" platform "falling back to POSIX"]
        target-platform: posix
    ]
]

project-class: make object! [
    class: #project
    name: null
    type: null  ;  dynamic, static, object or application
    depends: null  ; a dependency could be a library, object file
    output: null  ; file path
    basename: null   ; output without extension part
    generated: 'no
    implib: null  ; Windows exe/lib with exported symbols generates implib file

    post-build-commands: null  ; commands to run after the "build" command

    compiler: null

    ; common settings applying to all included obj-files
    ; setting inheritage:
    ; they can only be inherited from project to obj-files
    ; _not_ from project to project.
    ; They will be applied _in addition_ to the obj-file level settings
    ;
    includes: null
    definitions: null
    cflags: null

    ; These can be inherited from project to obj-files and will be overwritten
    ; at the obj-file level
    ;
    optimization: null
    debug: null
]

solution-class: make project-class [
    class: #solution
]

ext-dynamic-class: make object! [
    class: #dynamic-extension
    output: null
    flags: null  ;static?
]

ext-static-class: make object! [
    class: #static-extension
    output: null
    flags: null  ;static?
]

application-class: make project-class [
    class: #application
    type: 'application
    generated: 'no

    searches: null
    ldflags: null

    command: method [
        return: [text!]
        <with> default-compiler
    ][
        let cc: any [.compiler, default-compiler]
        return cc.link // [
            .output, .depends, .searches, .ldflags,
            :debug on? .debug
        ]
    ]

]

dynamic-library-class: make project-class [
    class: #dynamic-library
    type: 'dynamic
    generated: 'no

    searches: null
    ldflags: null

    command: method [
        return: [text!]
        <with>
        default-compiler
    ][
        let cc: any [.compiler, default-compiler]
        return cc.link // [
            .output, .depends, .searches, .ldflags
            :debug on? .debug
            :dynamic okay
        ]
    ]
]

; !!! This is an "object library" class which seems to be handled in some of
; the same switches as #static-library.  But there is no static-library-class
; for some reason, despite several #static-library switches.  What is the
; reasoning behind this?
;
object-library-class: make project-class [
    class: #object-library
    type: 'object
]

compiler-class: make object! [
    class: #compiler
    name: null
    id: null  ; flag prefix
    version: null
    exec-file: null

    check: method [
        "Check if the compiler is available"
        return: []
        exec -[Executable path (can be text!, e.g. "r3 --do c99 --")]-
            [<undo-opt> file! text!]
    ][
        panic ~<archetype check invoked>~
    ]

    compile: method [
        return: [text!]
        output [file!]
        source [file!]
        includes [file! block!]
        definitions [text! tag! block!]
        cflags [text! tag! block!]
    ][
        panic ~<archetype compile invoked>~
    ]

    link: method [
        return: [null? block!]
        output [file!]
        depends [<undo-opt> block!]
        searches [<undo-opt> block!]
        ldflags [<undo-opt> block! any-string?]
    ][
        panic ~<archetype link invoked>~
    ]
]


cc: make compiler-class [
    name: null  ; derived classes (gcc, clang, tcc override)
    id: null
    exec-file: %cc

    check: method [
        "Assigns .exec-file, extracts the compiler version"
        return: []
        exec [<undo-opt> file! text!]
    ][
        .exec-file: any [exec, .exec-file]

        if .name [  ; don't need to try and guess what kind of compiler it is
            return ~
        ]

        ; Try auto-detecting what kind of compiler it is
        ;
        version: copy ""
        call:output [(.exec-file) "--version"] version
        case [
            find version "clang" [
                .name: clang.name
                .id: clang.id
            ]
            ; gcc co-opts name of its executable, so when run as cc it will
            ; say "cc (Ubuntu ...) 13.3.0" etc.  But always cites FSF.
            ;
            find version "Free Software Foundation" [
                .name: gcc.name
                .id: gcc.id
            ]
            find:match version "tcc" [
                .name: tcc.name
                .id: tcc.id
            ]
        ]
        then [
            print [.name "detected from COMPILER-PATH:" .exec-file]
        ]
        else [
            .name: "cc"
            .id: "cc"
            print ["Unknown C compiler, COMPILER-PATH:" .exec-file]
        ]

      comment [  ; versioning
        let digit: charset "0123456789"
        let letter: charset [#"a" - #"z" #"A" - #"Z"]
        parse3:match version [
            "gcc (" some [letter | digit | #"_"] ")" space
            major: across some digit "."
            minor: across some digit "."
            macro: across some digit
            to <end>
        ] then [
            version: reduce [  ; !!! It appears this is not used (?)
                to integer! major
                to integer! minor
                to integer! macro
            ]
        ]
      ]
    ]

    compile: method [
        return: [text!]
        output [file!]
        source [file!]
        :I "includes" [block!]
        :D "definitions" [block!]
        :F "cflags" [block!]
        :O "opt-level" [word! integer!]
        :g "debug" [onoff?]
        :PIC "https://en.wikipedia.org/wiki/Position-independent_code"
        :E "only preprocessing"
    ][
        return spaced collect [
            keep any [
                file-to-local:pass opt .exec-file
                as text! .name  ; the "gcc" may get overridden as "g++"
            ]

            keep either E ["-E"]["-c"]

            if PIC [
                keep "-fPIC"
            ]
            if I [
                for-each 'inc (map-files-to-local I) [
                    keep unspaced ["-I" inc]
                ]
            ]
            if D [
                for-each 'flg D [
                    if word? flg [flg: as text! flg]

                    ; !!! For cases like `#include MBEDTLS_CONFIG_FILE` then
                    ; quotes are expected to work in defines...but when you
                    ; pass quotes on the command line it's different than
                    ; in of a visual studio project (for instance) because
                    ; bash strips them out unless escaped with backslash.
                    ; This is a stopgap workaround that ultimately would
                    ; permit cross-platform {MBEDTLS_CONFIG_FILE="filename.h"}
                    ;
                    flg: replace copy flg -["]- -[\"]-

                    ; Note: bootstrap executable hangs on:
                    ;
                    ;     keep unspaced [
                    ;         "-D" (filter-flag flg id else [continue])
                    ;     ]
                    ;
                    if flg: filter-flag flg .id [
                        keep unspaced ["-D" flg]
                    ]
                ]
            ]
            if O [
                case [
                    integer? O [keep unspaced ["-O" O]]
                    find ["s" "z" "g" 's 'z 'g] O [
                        keep unspaced ["-O" O]
                    ]

                    panic ["unrecognized optimization level:" O]
                ]
            ]
            if not null? g [
                case [
                    on? g [keep "-g -g3"]
                    off? g []
                    integer? g [keep unspaced ["-g" g]]  ; not triggered?

                    panic ["unrecognized debug option:" g]
                ]
            ]
            if F [
                for-each 'flg F [
                    keep opt filter-flag flg .id
                ]
            ]

            keep "-o"

            output: file-to-local output

            any [
                E
                ends-with? output opt target-platform.obj-suffix
            ] then [
                keep output
            ] else [
                keep [output target-platform.obj-suffix]
            ]

            keep file-to-local source
        ]
    ]

    link: link-backup: method [  ; !!! hacky inheritance mechanism
        return: [text!]
        output [file!]
        depends [<undo-opt> block!]
        searches [<undo-opt> block!]
        ldflags [<undo-opt> block! any-string?]
        :dynamic
        :debug
    ][
        let suffix: either dynamic [
            target-platform.dll-suffix
        ][
            target-platform.exe-suffix
        ]
        return spaced collect [
            keep file-to-local:pass .exec-file

            ; !!! This was breaking emcc.  However, it is needed in order to
            ; get shared libraries on Posix.  That feature is being resurrected
            ; so turn it back on.
            ; https://github.com/emscripten-core/emscripten/issues/11814
            ;
            if dynamic [keep "-shared"]

            keep "-o"

            output: file-to-local output
            either ends-with? output opt suffix [
                keep output
            ][
                keep unspaced [output suffix]
            ]

            for-each 'search (opt map-files-to-local opt searches) [
                keep unspaced ["-L" search]
            ]

            for-each 'flg ldflags [
                keep opt filter-flag flg .id
            ]

            for-each 'dep depends [
                keep opt .accept dep
            ]
        ]
    ]

    accept: method [
        return: [null? text!]
        dep [object!]
    ][
        return degrade switch dep.class [
            #object-file [
                file-to-local dep.output
            ]
            #dynamic-extension [
                either tag? dep.output [
                    if let lib: filter-flag dep.output .id [
                        unspaced ["-l" lib]
                    ]
                    else [
                        reify null
                    ]
                ][
                    spaced [
                        if dep.flags [
                            if find dep.flags 'static ["-static"]
                        ]
                        unspaced ["-l" dep.output]
                    ]
                ]
            ]
            #static-extension [
                file-to-local dep.output
            ]
            #object-library [
                spaced map-each 'ddep dep.depends [
                    file-to-local ddep.output
                ]
            ]
            #application [
                '~null~
            ]
            #variable [
                '~null~
            ]
            #entry [
                '~null~
            ]
            (elide dump dep)
            panic "unrecognized dependency"
        ]
    ]
]

; !!! In the original rebmake.r, tcc was a full copy of the GCC code, while
; clang was just `make gcc [name: 'clang]`.  TCC was not used as a compiler
; for Rebol itself--only to do some preprocessing of %sys-core.i, but this
; mechanism is no longer used (see %extensions/tcc/README.md)

gcc: make cc [
    name: 'gcc
    id: ["gcc" "gnu"]  ; apply all <gcc:XXX> and <gnu:XXX> flags
    exec-file: %gcc
]

g++: make cc [
    name: 'gcc
    id: ["gcc" "gnu"]  ; apply all <gcc:XXX> and <gnu:XXX> flags
    exec-file: %g++
]

tcc: make gcc [
    name: 'tcc
    id: "tcc"
    exec-file: %tcc
]

clang: make gcc [
    name: 'clang
    id: ["gcc" "clang"]
    exec-file: %clang
]

clang++: make gcc [
    name: 'clang
    id: ["gcc" "clang"]
    exec-file: %clang++
]

emcc: make gcc [
    name: 'emcc
    id: ["gcc" "clang"]
    exec-file: %emcc

    link: method [
        return: [text!]
        output [file!]
        depends [<undo-opt> block!]
        searches [<undo-opt> block!]
        ldflags [<undo-opt> block! any-string?]
        :dynamic
        :debug
    ][
        ; custom link behavior could go here

        return link-backup // [
            output depends searches ldflags :dynamic dynamic :debug debug
        ]
    ]
]

; Microsoft CL compiler
cl: make compiler-class [
    name: 'cl
    id: "msc" ; match all flags like <msc:XXX>
    exec-file: %cl.exe

    check: method [
        "Assigns .exec-file, extracts the compiler version"
        return: []
        exec [<undo-opt> file! text!]
    ][
        .exec-file: any [exec, .exec-file]

        ; TBD: extract version
    ]

    compile: method [
        return: [text!]
        output [file!]
        source
        :I "includes" [block!]
        :D "definitions" [block!]
        :F "cflags" [block!]
        :O "opt-level" [word! integer!]
        :g "debug" [word! integer!]
        :PIC "https://en.wikipedia.org/wiki/Position-independent_code"
        ; Note: PIC is ignored for this Microsoft CL compiler handler
        :E "only preprocessing"
    ][
        return spaced collect [
            keep any [(file-to-local:pass opt .exec-file) "cl"]
            keep "/nologo"  ; don't show startup banner (must be lowercase)
            keep either E ["/P"]["/c"]

            ; NMAKE is not multi-core, only CL.EXE is when you pass it more
            ; than one file at a time with /MP.  To get around this, you can
            ; use Qt's JOM which is a drop-in replacement for NMAKE that does
            ; parallel building.  But it requires /FS "force synchronous pdb"
            ; so that the multiple CL calls don't try and each lock the pdb.
            ;
            keep "/FS"

            if I [
                for-each 'inc (map-files-to-local I) [
                    keep unspaced ["/I" inc]
                ]
            ]
            if D [
                for-each 'flg D [
                    if word? flg [flg: as text! flg]

                    ; !!! For cases like `#include MBEDTLS_CONFIG_FILE` then
                    ; quotes are expected to work in defines...but when you
                    ; pass quotes on the command line it's different than
                    ; in of a visual studio project (for instance) because
                    ; bash strips them out unless escaped with backslash.
                    ; This is a stopgap workaround that ultimately would
                    ; permit cross-platform {MBEDTLS_CONFIG_FILE="filename.h"}
                    ;
                    flg: replace copy flg -["]- -[\"]-

                    ; Note: bootstrap executable hangs on:
                    ;
                    ;     keep unspaced [
                    ;         "/D" (filter-flag flg id else [continue])
                    ;     ]
                    ;
                    if flg: filter-flag flg .id [
                        keep unspaced ["/D" flg]
                    ]
                ]
            ]
            if O [
                keep unspaced ["/O" O]
            ]
            if not null? g [
                case [
                    off? g []
                    any [
                        on? g  ; only on and off are passed in...
                        integer? g  ; doesn't map to a CL option
                    ][
                        keep "/Od /Zi"
                    ]

                    panic ["unrecognized debug option:" g]
                ]
            ]
            if F [
                for-each 'flg F [
                    keep opt filter-flag flg .id
                ]
            ]

            output: file-to-local output
            keep unspaced [
                either E ["/Fi"]["/Fo"]
                any [
                    E
                    ends-with? output opt target-platform.obj-suffix
                ] then [
                    output
                ] else [
                    unspaced [output target-platform.obj-suffix]
                ]
            ]

            keep file-to-local:pass source
        ]
    ]

    link: method [
        return: [text!]
        output [file!]
        depends [<undo-opt> block!]
        searches [<undo-opt> block!]
        ldflags [<undo-opt> block! any-string?]
        :dynamic
        :debug
    ][
        let suffix: either dynamic [
            target-platform.dll-suffix
        ][
            target-platform.exe-suffix
        ]
        return spaced collect [
            keep file-to-local:pass .exec-file

            ; don't show startup banner
            ; (link.exe takes uppercase, but cl.exe mandates lowercase!)
            ;
            keep "/nologo"

            ; link.exe takes e.g. `/OUT:r3.exe`, the front end takes `/Fer3`
            ;
            output: file-to-local output
            keep unspaced [
                "/Fe" either ends-with? output opt suffix [
                    output
                ][
                    unspaced [output suffix]
                ]
            ]

            for-each 'dep depends [
                keep opt .accept dep
            ]

            ; /link must precede linker-specific options
            ; it also must be lowercase!s
            ;
            keep "/link"

            if dynamic [keep "/dll"]

            ; https://docs.microsoft.com/en-us/cpp/build/reference/debug-generate-debug-info
            ; /DEBUG must be uppercase when passed to cl.exe
            ;
            if debug [keep "/DEBUG"]

            for-each 'search (opt map-files-to-local opt searches) [
                keep unspaced ["/libpath:" search]
            ]

            for-each 'flg ldflags [
                keep opt filter-flag flg .id
            ]
        ]
    ]

    accept: method [
        return: [null? text!]
        dep [object!]
    ][
        return degrade switch dep.class [
            #object-file [
                file-to-local to file! dep.output
            ]
            #dynamic-extension [
                comment [import file]  ; static property is ignored

                reify either tag? dep.output [
                    filter-flag dep.output .id
                ][
                    ;dump dep.output
                    file-to-local:pass either ends-with? dep.output ".lib" [
                        dep.output
                    ][
                        join dep.output ".lib"
                    ]
                ]
            ]
            #static-extension [
                file-to-local dep.output
            ]
            #object-library [
                spaced map-each 'ddep dep.depends [
                    file-to-local to file! ddep.output
                ]
            ]
            #application [
                file-to-local any [dep.implib, join dep.basename ".lib"]
            ]
            #variable [
                '~null~
            ]
            #entry [
                '~null~
            ]
            (elide dump dep)
            panic "unrecognized dependency"
        ]
    ]
]


strip-class: make object! [
    class: #strip
    name: null
    id: null  ; flag prefix
    exec-file: null
    options: null
    commands: method [
        return: [block!]
        target [file!]
        params [<undo-opt> block! any-string?]
    ][
        return reduce [spaced collect [
            keep any [(file-to-local:pass opt .exec-file) "strip"]
            params: default [options]
            switch try type-of params [  ; switch:type not in bootstrap
                null [noop]
                block! [
                    for-each 'flag params [
                        keep filter-flag flag .id
                    ]
                ]
                text! [
                    keep params
                ]
                panic
            ]
            keep file-to-local target
        ]]
    ]
    check: does [
        ...  ; overridden
    ]
]

strip: make strip-class [
    id: ["gcc" "gnu"]
    check: method [
        return: []
        exec [<undo-opt> file! text!]
    ][
        .exec-file: exec: default ["strip"]
    ]
]

; includes/definitions/cflags will be inherited from its immediately ancester
object-file-class: make object! [
    class: #object-file
    compiler: null
    cflags: null
    definitions: ~
    source: ~
    output: ~
    basename: null  ; output without extension part
    optimization: null
    debug: null
    includes: null
    generated: 'no
    depends: null

    compile: method [
        return: [text!]
        :I "extra includes" [block!]
        :D "extra definitions" [block!]
        :F "extra cflags (override)" [block!]
        :O "opt-level" [word! integer!]
        :g "dbg" [word! integer!]
        :PIC "https://en.wikipedia.org/wiki/Position-independent_code"
        :E "only preprocessing"
    ][
        let cc: any [compiler, default-compiler]

        if optimization = #prefer-O2-optimization [
            any [
                not O
                O = "s"
            ] then [
                O: 2  ; don't override e.g. "-Oz"
            ]
            optimization: 0
        ]

        return cc.compile // [
            .output
            .source

            :I compose [(opt spread .includes) (opt spread I)]
            :D compose [(opt spread .definitions) (opt spread D)]
            :F compose [(opt spread F) (opt spread .cflags)]
                                                ; ^-- reverses priority, why?

            ; "current setting overwrites :refinement"
            ; "because the refinements are inherited from the parent" (?)

            :O any [O, .optimization]
            :g any [g, .debug]

            :PIC PIC
            :E E
        ]
    ]

    gen-entries: method [
        return: [object!]
        parent [object!]
        :PIC "https://en.wikipedia.org/wiki/Position-independent_code"
    ][
        assert [
            find [
                #application
                #dynamic-library
                #static-library
                #object-library
            ] parent.class
        ]

        return make entry-class [
            target: .output
            depends: append (copy any [.depends []]) .source
            commands: reduce [.compile // [
                :I opt parent.includes
                :D opt parent.definitions
                :F opt parent.cflags
                :O opt parent.optimization
                :g opt parent.debug
                :PIC any [PIC, parent.class = #dynamic-library]
            ]]
        ]
    ]
]

entry-class: make object! [
    class: #entry
    target: ~
    depends: null
    commands: ~
    generated: 'no
]

var-class: make object! [
    class: #variable
    name: ~
    value: null  ; behavior is `any [.value, default]`, so start as null
    default: ~
    generated: 'no
]

cmd-create-class: make object! [
    class: #cmd-create
    file: ~
]

cmd-delete-class: make object! [
    class: #cmd-delete
    file: ~
]

cmd-strip-class: make object! [
    class: #cmd-strip
    file: ~
    options: null
    strip: null
]

generator-class: make object! [
    class: #generator

    vars: make map! 128

    gen-cmd-create: null
    gen-cmd-delete: null
    gen-cmd-strip: null

    gen-cmd: method [
        return: [text!]
        cmd [object!]
    ][
        return switch cmd.class [
            #cmd-create [
                apply any [
                    get $.gen-cmd-create
                    get $target-platform/gen-cmd-create
                ][
                    cmd
                ]
            ]
            #cmd-delete [
                apply any [
                    get $.gen-cmd-delete
                    get $target-platform/gen-cmd-delete
                ][
                    cmd
                ]
            ]
            #cmd-strip [
                apply any [
                    get $.gen-cmd-strip
                    get $target-platform/gen-cmd-strip
                ] compose [
                    cmd
                ]
            ]

            panic ["Unknown cmd class:" cmd.class]
        ]
    ]

    do-substitutions: method [
        "Substitute variables (recursively) in the command with its value"

        return: [null? object! any-string?]
        cmd [object! any-string?]
    ][
        ; !!! These were previously static, but bootstrap executable's non
        ; gathering function form could not handle statics.
        ;
        let letter: charset [#"a" - #"z" #"A" - #"Z"]
        let digit: charset "0123456789"

        if object? cmd [
            assert [
                find [
                    #cmd-create #cmd-delete #cmd-strip
                ] cmd.class
            ]
            cmd: .gen-cmd cmd
        ]
        if not cmd [return null]

        let stop: 'no
        let name
        let val
        while [no? stop][
            stop: 'yes
            parse3:match cmd [
                opt some [
                    change [
                        [
                            "$(" name: across some [letter | digit | #"_"] ")"
                            | "$" name: across letter
                        ] (
                            val: file-to-local:pass select vars name
                            stop: 'no
                        )
                    ] (val)
                    | one
                ]
            ] else [
                panic ["failed to do var substitution:" cmd]
            ]
        ]
        return cmd
    ]

    prepare: method [
        return: []
        solution [object!]
    ][
        if find words-of solution 'output [
            .setup-outputs solution
        ]
        flip-flag solution 'no

        if find words-of solution 'depends [
            for-each 'dep (opt solution.depends) [
                if dep.class = #variable [
                    append vars spread reduce [
                        dep.name
                        any [dep.value, dep.default]
                    ]
                ]
            ]
        ]
    ]

    flip-flag: method [
        return: []
        project [object!]
        to [yesno?]
    ][
        all [
            find words-of project 'generated
            to != project.generated
        ] then [
            project.generated: to
            if find words-of project 'depends [
                for-each 'dep project.depends [
                    flip-flag dep to
                ]
            ]
        ]
    ]

    setup-output: method [
        return: []
        project [object!]
    ][
        assert [project.class]
        let suffix: switch project.class [
            #application [target-platform.exe-suffix]
            #dynamic-library [target-platform.dll-suffix]
            #static-library [target-platform.archive-suffix]
            #object-library [target-platform.archive-suffix]
            #object-file [target-platform.obj-suffix]
        ] else [
            return ~
        ]

        let basename
        case [
            null? project.output [
                switch project.class [
                    #object-file [
                        project.output: copy project.source
                    ]
                    #object-library [
                        project.output: to text! project.name
                    ]

                    panic ["Unexpected project class:" (project.class)]
                ]

                let output-ext: find-last project.output #"."
                if output-ext [
                    remove output-ext
                ]

                basename: project.output
                project.output: join basename suffix
            ]
            ends-with? project.output opt suffix [
                basename: either suffix [
                    copy:part project.output
                        (length of project.output) - (length of suffix)
                ][
                    copy project.output
                ]
            ]
        ] else [
            basename: project.output
            project.output: join basename suffix
        ]

        project.basename: basename
    ]

    setup-outputs: method [
        "Set the output and implib for the project tree"
        return: []
        project [object!]
    ][
        ;print ["Setting outputs for:"]
        ;dump project
        switch project.class [
            #application
            #dynamic-library
            #static-library
            #solution
            #object-library [
                if yes? project.generated [return ~]
                .setup-output project
                project.generated: 'yes
                for-each 'dep project.depends [
                    .setup-outputs dep
                ]
            ]
            #object-file [
                .setup-output project
            ]
        ] else [return ~]
    ]
]

makefile: make generator-class [
    is-nmake: 'no  ; Generating for Microsoft nmake

    ; by default makefiles are for POSIX platform
    ; these GETs are null-tolerant
    ;
    /gen-cmd-create: get $posix.gen-cmd-create
    /gen-cmd-delete: get $posix.gen-cmd-delete
    /gen-cmd-strip: get $posix.gen-cmd-strip

    gen-rule: method [
        return: "Possibly multi-line text for rule, with extra newline @ end"
            [text!]
        entry [object!]
    ][
        return delimit:tail newline collect [switch entry.class [

            ; Makefile variable, defined on a line by itself
            ;
            #variable [
                keep spaced either entry.value [
                    [entry.name "=" entry.value]
                ][
                    [entry.name either yes? .is-nmake ["="]["?="] entry.default]
                ]
            ]

            #entry [
                ;
                ; First line in a makefile entry is the target followed by
                ; a colon and a list of dependencies.  Usually the target is
                ; a file path on disk, but it can also be a "phony" target
                ; that is just a word:
                ;
                ; https://stackoverflow.com/q/2145590/
                ;
                keep spaced collect [
                    case [
                        word? entry.target [  ; like `clean` in `make clean`
                            keep unspaced [entry.target ":"]
                            keep ".PHONY"
                        ]
                        file? entry.target [
                            keep unspaced [file-to-local entry.target ":"]
                        ]
                        panic ["Unknown entry.target type" entry.target]
                    ]
                    for-each 'w (opt entry.depends) [
                        switch select (match object! w else [[]]) 'class [
                            #variable [
                                keep unspaced ["$(" w.name ")"]
                            ]
                            #entry [
                                keep to-text w.target
                            ]
                            #dynamic-extension #static-extension [
                                ; only contribute to command line
                            ]
                        ] else [
                            keep case [
                                file? w [file-to-local w]
                                file? w.output [file-to-local w.output]
                            ] else [w.output]
                        ]
                    ]
                ]

                ; After the line with its target and dependencies are the
                ; lines of shell code that run to build the target.  These
                ; may use escaped makefile variables that get substituted.
                ;
                if entry.commands [
                    for-each 'cmd (ensure block! entry.commands) [
                        let c: any [
                            match text! cmd
                            .gen-cmd cmd
                            continue
                        ]
                        if empty? c [continue]  ; !!! Review why this happens
                        keep unspaced [tab c]  ; makefiles demand TAB :-(
                    ]
                ]
            ]

            panic ["Unrecognized entry class:" entry.class]
        ] keep ""]  ; final keeps just adds extra newline

        ; !!! Adding an extra newline here unconditionally means variables
        ; in the makefile get spaced out, which isn't bad--but it wasn't done
        ; in the original rebmake.r.  This could be rethought to leave it
        ; to the caller to decide to add the spacing line or not
    ]

    emit: method [
        return: []
        buf [blob!]
        project [object!]
        :parent [object!]  ; !!! Not heeded?
    ][
        for-each 'dep project.depends [
            if not object? dep [continue]
            if not find [#dynamic-extension #static-extension] dep.class [
                if yes? dep.generated [
                    continue
                ]
                dep.generated: 'yes
            ]
            switch dep.class [
                #application
                #dynamic-library
                #static-library [
                    let objs: make block! 8
                    for-each 'obj dep.depends [
                        if obj.class = #object-library [
                            append objs spread obj.depends
                        ]
                    ]
                    append buf .gen-rule make entry-class [
                        target: dep.output
                        depends: append copy objs (
                            spread map-each 'ddep dep.depends [
                                if ddep.class <> #object-library [ddep]
                            ]
                        )
                        commands: append reduce [dep/command] opt (
                            spread dep.post-build-commands
                        )
                    ]
                    emit buf dep
                ]
                #object-library [
                    comment [
                        ; !!! Said "No nested object-library-class allowed"
                        ; but was commented out (?)
                        assert [dep.class != #object-library]
                    ]
                    for-each 'obj dep.depends [
                        assert [obj.class = #object-file]
                        if no? obj.generated [
                            obj.generated: 'yes
                            append buf (.gen-rule obj.gen-entries // [
                                dep
                                :PIC (project.class = #dynamic-library)
                            ])
                        ]
                    ]
                ]
                #object-file [
                    append buf .gen-rule dep/gen-entries project
                ]
                #entry #variable [
                    append buf .gen-rule dep
                ]
                #dynamic-extension #static-extension [
                    ; nothing to do
                ]
                (elide dump dep)
                panic ["unrecognized project type:" dep.class]
            ]
        ]
    ]

    generate: method [
        return: []
        output [file!]
        solution [object!]
    ][
        let buf: make blob! 2048
        assert [solution.class = #solution]

        .prepare solution

        .emit buf solution

        write output append buf "^/^/.PHONY:"
    ]
]

nmake: make makefile [
    is-nmake: 'yes

    ; reset them, so they will be chosen by the target platform
    gen-cmd-create: null
    gen-cmd-delete: null
    gen-cmd-strip: null
]

; For mingw-make on Windows
mingw-make: make makefile [
    ; reset them, so they will be chosen by the target platform
    gen-cmd-create: null
    gen-cmd-delete: null
    gen-cmd-strip: null
]

; Execute the command to generate the target directly
;
export execution: make generator-class [
    host: switch system.platform.1 [
        'Windows [windows]
        'Linux [linux]
        'Haiku [haiku]
        'MacOS [osx]
        'Android [android]
    ] else [
        print [
            "Untested platform" system.platform "- assume POSIX compilant"
        ]
        posix
    ]

    ; these GETs are null tolerant
    ;
    gen-cmd-create: get $host.gen-cmd-create
    gen-cmd-delete: get $host.gen-cmd-delete
    gen-cmd-strip: get $host.gen-cmd-strip

    run-target: method [
        return: []
        target [object!]
        :cwd "change working directory"  ; !!! Not heeded (?)
            [file!]
    ][
        switch target.class [
            #variable [
                ; already been taken care of by PREPARE
            ]
            #entry [
                if all [
                    not word? target.target
                    ; so you can use words for "phony" targets
                    exists? to file! target.target
                ][
                    ; TODO: Check timestamp to see if it needs to be updated
                    return ~
                ]
                either block? target.commands [
                    for-each 'cmd target.commands [
                        cmd: .do-substitutions cmd
                        print ["Running:" cmd]
                        call:shell cmd
                    ]
                ][
                    let cmd: .do-substitutions target.commands
                    print ["Running:" cmd]
                    call:shell cmd
                ]
            ]
            (elide dump target)
            panic "Unrecognized target class"
        ]
    ]

    run: method [
        return: []
        project [object!]
        :parent "parent project"
            [object!]
    ][
        ;dump project
        if not object? project [return ~]

        .prepare project

        if not find [#dynamic-extension #static-extension] project.class [
            if yes? project.generated [return ~]
            project.generated: 'yes
        ]

        switch project.class [
            #application
            #dynamic-library
            #static-library [
                let objs: make block! 8
                for-each 'obj project.depends [
                    if obj.class = #object-library [
                        append objs spread obj.depends
                    ]
                ]
                for-each 'dep project.depends [
                    .run:parent dep project
                ]
                .run-target make entry-class [
                    target: project.output
                    depends: append copy project.depends objs  ; JOIN reduces
                    commands: reduce [project/command]
                ]
            ]
            #object-library [
                for-each 'obj project.depends [
                    assert [obj.class = #object-file]
                    if no? obj.generated [
                        obj.generated: 'yes
                        .run-target obj.gen-entries // [
                            project
                            :PIC (parent.class = #dynamic-library)
                        ]
                    ]
                ]
            ]
            #object-file [
                assert [parent]
                .run-target project/gen-entries p-project
            ]
            #entry #variable [
                .run-target project
            ]
            #dynamic-extension #static-extension [
                ; nothing to do
            ]
            #solution [
                for-each 'dep project.depends [
                    .run dep
                ]
            ]
            (elide dump project)
            panic ["unrecognized project type:" project.class]
        ]
    ]
]
