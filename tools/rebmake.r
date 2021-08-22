REBOL [
    File: %rebmake.r
    Title: {Rebol-Based C/C++ Makefile and Project File Generator}

    Type: module
    Name: Rebmake

    Rights: {
        Copyright 2017 Atronix Engineering
        Copyright 2017-2018 Ren-C Open Source Contributors
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        R3-Alpha's bootstrap process depended on the GNU Make Tool, with a
        makefile generated from minor adjustments to a boilerplate copy of
        the makefile text.  As needs grew, a second build process arose
        which used CMake...which was also capable of creating files for
        various IDEs, such as Visual Studio.

        %rebmake.r arose to try and reconcile these two build processes, and
        eliminate dependency on an external make tool completely.  It can
        generate project files for Microsoft Visual Studio, makefiles for
        GNU Make or Microsoft's Nmake, or just carry out a full build by
        invoking compiler processes and command lines itself.

        In theory this code is abstracted such that it could be used by other
        projects.  In practice, it is tailored to the specific needs and
        settings of the Rebol project.
    }
]

do %import-shim.r
import %bootstrap-shim.r

default-compiler: _
default-linker: _
default-strip: _
target-platform: _

map-files-to-local: func [
    return: [block!]
    files [file! block!]
][
    if not block? files [files: reduce [files]]
    return map-each f files [
        file-to-local f
    ]
]

ends-with?: func [
    return: [logic!]
    s [any-string!]
    suffix [blank! any-string!]
][
    return did any [
        blank? suffix
        empty? suffix
        suffix = (skip tail-of s negate length of suffix)
    ]
]

filter-flag: function [
    return: [<opt> text! file!]
    flag [tag! text! file!]
        {If TAG! then must be <prefix:flag>, e.g. <gnu:-Wno-unknown-warning>}
    prefix [text!]
        {gnu -> GCC-compatible compilers, msc -> Microsoft C}
][
    if not tag? flag [return flag]  ; no filtering

    parse to text! flag [
        copy header: to ":"
        ":" copy option: to end
    ] else [
        fail ["Tag must be <prefix:flag> ->" (flag)]
    ]

    return all [
        prefix = header
        to-text option
    ]
]

run-command: func [
    cmd [block! text!]
][
    let x: copy ""
    call/shell/output cmd x
    trim/with x "^/^M"
]

pkg-config: func [
    return: [text! block!]
    pkg [any-string!]
    var [word!]
    lib [any-string!]
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
            dlm: _
            opt: "--cflags-only-other"
        ]
        'ldflags [
            dlm: _
            opt: "--libs-only-other"
        ]
        fail ["Unsupported pkg-config word:" var]
    ]

    let x: run-command spaced reduce [pkg opt lib]
    ;dump x
    either dlm [
        let ret: make block! 1
        let item
        parse x [
            some [
                thru dlm
                copy item: to [dlm | end] (
                    ;dump item
                    append ret to file! item
                )
            ]
            end
        ]
        ret
    ][
        x
    ]
]

platform-class: make object! [
    name: _
    exe-suffix: null
    dll-suffix: null
    archive-suffix: null  ;static library
    obj-suffix: null

    gen-cmd-create: _
    gen-cmd-delete: _
    gen-cmd-strip: _
]

unknown-platform: make platform-class [
    name: 'unknown
]

posix: make platform-class [
    name: 'POSIX
    dll-suffix: ".so"
    obj-suffix: ".o"
    archive-suffix: ".a"

    gen-cmd-create: meth [
        return: [text!]
        cmd [object!]
    ][
        return either dir? cmd/file [
            spaced ["mkdir -p" cmd/file]
        ][
            spaced ["touch" cmd/file]
        ]
    ]

    gen-cmd-delete: meth [
        return: [text!]
        cmd [object!]
    ][
        return spaced ["rm -fr" cmd/file]
    ]

    gen-cmd-strip: meth [
        return: [text!]
        cmd [object!]
    ][
        if let tool: any [:cmd/strip :default-strip] [
            let b: ensure block! tool/commands/params cmd/file opt cmd/options
            assert [1 = length of b]
            return b/1
        ]
        return ""
    ]
]

linux: make posix [
    name: 'Linux
]

android: make linux [
    name: 'Android
]

emscripten: make posix [
    name: 'Emscripten
    exe-suffix: ".js"
    dll-suffix: ".js"
]

osx: make posix [
    name: 'OSX
    dll-suffix: ".dyn"
]

windows: make platform-class [
    name: 'Windows

    exe-suffix: ".exe"
    dll-suffix: ".dll"
    obj-suffix: ".obj"
    archive-suffix: ".lib"

    gen-cmd-create: meth [
        return: [text!]
        cmd [object!]
    ][
        let f: file-to-local cmd/file
        if #"\" = last f [remove back tail-of f]
        return either dir? cmd/file [
            spaced ["if not exist" f "mkdir" f]
        ][
            unspaced ["echo . 2>" f]
        ]
    ]

    gen-cmd-delete: meth [
        return: [text!]
        cmd [object!]
    ][
        let f: file-to-local cmd/file
        if #"\" = last f [remove back tail-of f]
        return either dir? cmd/file [
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

    gen-cmd-strip: meth [
        return: [text!]
        cmd [object!]
    ][
        print "Note: STRIP command not implemented for MSVC"
        return ""
    ]
]

set-target-platform: func [
    platform
][
    switch platform [
        'posix [
            target-platform: posix
        ]
        'linux [
            target-platform: linux
        ]
        'android [
            target-platform: android
        ]
        'windows [
            target-platform: windows
        ]
        'osx [
            target-platform: osx
        ]
        'emscripten [
            target-platform: emscripten
        ]
    ] else [
        print ["Unknown platform:" platform "falling back to POSIX"]
        target-platform: posix
    ]
]

project-class: make object! [
    class: #project
    name: _
    id: _
    type: _ ; dynamic, static, object or application
    depends: _ ;a dependency could be a library, object file
    output: _ ;file path
    basename: _ ;output without extension part
    generated?: false
    implib: _ ;for windows, an application/library with exported symbols will generate an implib file

    post-build-commands: _ ; commands to run after the "build" command

    compiler: _

    ; common settings applying to all included obj-files
    ; setting inheritage:
    ; they can only be inherited from project to obj-files
    ; _not_ from project to project.
    ; They will be applied _in addition_ to the obj-file level settings
    includes: _
    definitions: _
    cflags: _

    ; These can be inherited from project to obj-files and will be overwritten
    ; at the obj-file level
    optimization: _
    debug: _
]

solution-class: make project-class [
    class: #solution
]

ext-dynamic-class: make object! [
    class: #dynamic-extension
    output: _
    flags: _ ;static?
]

ext-static-class: make object! [
    class: #static-extension
    output: _
    flags: _ ;static?
]

application-class: make project-class [
    class: #application
    type: 'application
    generated?: false

    linker: _
    searches: _
    ldflags: _

    link: meth [return: <none>] [
        linker/link output depends ldflags
    ]

    command: meth [return: [text!]] [
        let ld: any [
            linker
            default-linker
        ]
        return ld/command/debug
            output
            depends
            searches
            ldflags
            debug
    ]

]

dynamic-library-class: make project-class [
    class: #dynamic-library
    type: 'dynamic
    generated?: false
    linker: _

    searches: _
    ldflags: _
    link: meth [return: <none>] [
        linker/link output depends ldflags
    ]

    command: meth [
        return: [text!]
        <with>
        default-linker
    ][
        let l: any [
            linker
            default-linker
        ]
        return l/command/dynamic
            output
            depends
            searches
            ldflags
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
    name: _
    id: _ ;flag prefix
    version: _
    exec-file: _
    compile: meth [
        return: <none>
        output [file!]
        source [file!]
        include [file! block!]
        definition [any-string!]
        cflags [any-string!]
    ][
    ]

    command: meth [
        return: [text!]
        output
        source
        includes
        definitions
        cflags
    ][
    ]
    ;check if the compiler is available
    check: meth [
        return: [logic!]
        path [<blank> any-string!]
    ][
        fail ~tbd~
    ]
]

gcc: make compiler-class [
    name: 'gcc
    id: "gnu"
    check: meth [
        return: [logic!]
        /exec [file!]
    ][
        ; !!! This used to be static, but the bootstrap executable's non
        ; gathering form could not do <static>
        ;
        let digit: charset "0123456789"

        version: copy ""
        attempt [
            exec-file: exec: default ["gcc"]
            call/output [(exec) "--version"] version
            parse version [
                {gcc (GCC)} space
                copy major: some digit "."
                copy minor: some digit "."
                copy macro: some digit
                to end
            ] then [
                version: reduce [  ; !!! It appears this is not used (?)
                    to integer! major
                    to integer! minor
                    to integer! macro
                ]
                true
            ] else [
                false
            ]
        ]
    ]

    command: meth [
        return: [text!]
        output [file!]
        source [file!]
        /I "includes" [block!]
        /D "definitions" [block!]
        /F "cflags" [block!]
        /O "opt-level" [any-value!]  ; !!! datatypes?
        /g "debug" [any-value!]  ; !!! datatypes?
        /PIC "https://en.wikipedia.org/wiki/Position-independent_code"
        /E "only preprocessing"
    ][
        return collect-text [
            keep any [
                (file-to-local/pass exec-file)
                (to text! name)  ; the "gcc" may get overridden as "g++"
            ]

            keep either E ["-E"]["-c"]

            if PIC [
                keep "-fPIC"
            ]
            if I [
                for-each inc (map-files-to-local I) [
                    keep ["-I" inc]
                ]
            ]
            if D [
                for-each flg D [
                    ;
                    ; !!! For cases like `#include MBEDTLS_CONFIG_FILE` then
                    ; quotes are expected to work in defines...but when you
                    ; pass quotes on the command line it's different than
                    ; inside of a visual studio project (for instance) because
                    ; bash strips them out unless escaped with backslash.
                    ; This is a stopgap workaround that ultimately would
                    ; permit cross-platform {MBEDTLS_CONFIG_FILE="filename.h"}
                    ;
                    if find/only [gcc g++ cl] name [
                        flg: replace/all copy flg {"} {\"}
                    ]

                    keep ["-D" (filter-flag flg id else [continue])]
                ]
            ]
            if O [
                case [
                    O = true [keep "-O2"]
                    O = false [keep "-O0"]
                    integer? O [keep ["-O" O]]
                    find ["s" "z" "g" 's 'z 'g] O [
                        keep ["-O" O]
                    ]

                    fail ["unrecognized optimization level:" O]
                ]
            ]
            if g [
                case [
                    g = true [keep "-g -g3"]
                    g = false []
                    integer? g [keep ["-g" g]]

                    fail ["unrecognized debug option:" g]
                ]
            ]
            if F [
                for-each flg F [
                    keep try filter-flag flg id
                ]
            ]

            keep "-o"

            output: file-to-local output

            any [
                E
                ends-with? output target-platform/obj-suffix
            ] then [
                keep output
            ] else [
                keep [output target-platform/obj-suffix]
            ]

            keep file-to-local source
        ]
    ]
]

; !!! In the original rebmake.r, tcc was a full copy of the GCC code, while
; clang was just `make gcc [name: 'clang]`.  TCC was not used as a compiler
; for Rebol itself--only to do some preprocessing of %sys-core.i, but this
; mechanism is no longer used (see %extensions/tcc/README.md)

tcc: make gcc [
    name: 'tcc
]

clang: make gcc [
    name: 'clang
]

; Microsoft CL compiler
cl: make compiler-class [
    name: 'cl
    id: "msc" ;flag id
    command: meth [
        return: [text!]
        output [file!]
        source
        /I "includes" [block!]
        /D "definitions" [block!]
        /F "cflags" [block!]
        /O "opt-level" [any-value!]  ; !!! datatypes?
        /g "debug" [any-value!]  ; !!! datatypes?
        /PIC "https://en.wikipedia.org/wiki/Position-independent_code"
        ; Note: PIC is ignored for this Microsoft CL compiler handler
        /E "only preprocessing"
    ][
        return collect-text [
            keep any [(file-to-local/pass exec-file) "cl"]
            keep "/nologo"  ; don't show startup banner (must be lowercase)
            keep either E ["/P"]["/c"]

            if I [
                for-each inc (map-files-to-local I) [
                    keep ["/I" inc]
                ]
            ]
            if D [
                for-each flg D [
                    ; !!! For cases like `#include MBEDTLS_CONFIG_FILE` then
                    ; quotes are expected to work in defines...but when you
                    ; pass quotes on the command line it's different than
                    ; inside of a visual studio project (for instance) because
                    ; bash strips them out unless escaped with backslash.
                    ; This is a stopgap workaround that ultimately would
                    ; permit cross-platform {MBEDTLS_CONFIG_FILE="filename.h"}
                    ;
                    flg: replace/all copy flg {"} {\"}

                    keep ["/D" (filter-flag flg id else [continue])]
                ]
            ]
            if O [
                case [
                    O = true [keep "/O2"]
                    all [O (not zero? O)] [
                        keep ["/O" O]
                    ]
                ]
            ]
            if g [
                case [
                    any [
                        g = true
                        integer? g  ; doesn't map to a CL option
                    ][
                        keep "/Od /Zi"
                    ]
                    debug = false []

                    fail ["unrecognized debug option:" g]
                ]
            ]
            if F [
                for-each flg F [
                    keep try filter-flag flg id
                ]
            ]

            output: file-to-local output
            keep unspaced [
                either E ["/Fi"]["/Fo"]
                any [
                    E
                    ends-with? output target-platform/obj-suffix
                ] then [
                    output
                ] else [
                    unspaced [output target-platform/obj-suffix]
                ]
            ]

            keep file-to-local/pass source
        ]
    ]
]

linker-class: make object! [
    class: #linker
    name: _
    id: _ ;flag prefix
    version: _
    link: meth [
        return: <none>
    ][
        ...  ; overridden
    ]
    commands: meth [
        return: [<opt> block!]
        output [file!]
        depends [block! blank!]
        searches [block! blank!]
        ldflags [block! any-string! blank!]
    ][
        ...  ; overridden
    ]
    check: does [
        ...  ; overridden
    ]
]

ld: make linker-class [
    ;
    ; Note that `gcc` is used as the ld executable by default.  There are
    ; some switches (such as -m32) which it seems `ld` does not recognize,
    ; even when processing a similar looking link line.
    ;
    name: 'ld
    version: _
    exec-file: _
    id: "gnu"
    command: meth [
        return: [text!]
        output [file!]
        depends [block! blank!]
        searches [block! blank!]
        ldflags [block! any-string! blank!]
        /dynamic
        /debug [logic!]
    ][
        let suffix: either dynamic [
            try target-platform/dll-suffix
        ][
            try target-platform/exe-suffix
        ]
        return collect-text [
            keep any [(file-to-local/pass exec-file) "gcc"]

            ; !!! This breaks emcc at the moment; no other DLLs are being
            ; made, so leave it off.
            ; https://github.com/emscripten-core/emscripten/issues/11814
            ;
            comment [
                if dynamic [keep "-shared"]
            ]

            keep "-o"

            output: file-to-local output
            either ends-with? output suffix [
                keep output
            ][
                keep [output suffix]
            ]

            for-each search (map-files-to-local searches) [
                keep ["-L" search]
            ]

            for-each flg ldflags [
                keep try filter-flag flg id
            ]

            for-each dep depends [
                keep try accept dep
            ]
        ]
    ]

    accept: meth [
        return: [<opt> text!]
        dep [object!]
    ][
        return (opt switch dep/class [
            #object-file [
                file-to-local dep/output
            ]
            #dynamic-extension [
                either tag? dep/output [
                    if let lib: filter-flag dep/output id [
                        unspaced ["-l" lib]
                    ]
                ][
                    spaced [
                        if find dep/flags 'static ["-static"]
                        unspaced ["-l" dep/output]
                    ]
                ]
            ]
            #static-extension [
                file-to-local dep/output
            ]
            #object-library [
                spaced map-each ddep dep/depends [
                    file-to-local ddep/output
                ]
            ]
            #application [
                _
            ]
            #variable [
                _
            ]
            #entry [
                _
            ]
        ] else [
            dump dep
            fail "unrecognized dependency"
        ])
    ]

    check: meth [
        return: [logic!]
        /exec [file!]
    ][
        let version: copy ""
        ;attempt [
            exec-file: exec: default ["gcc"]
            call/output [(exec) "--version"] version
        ;]
    ]
]

llvm-link: make linker-class [
    name: 'llvm-link
    version: _
    exec-file: _
    id: "llvm"
    command: meth [
        return: [text!]
        output [file!]
        depends [block! blank!]
        searches [block! blank!]
        ldflags [block! any-string! blank!]
        /dynamic
        /debug [logic!]
    ][
        let suffix: either dynamic [
            target-platform/dll-suffix
        ][
            target-platform/exe-suffix
        ]

        return collect-text [
            keep any [(file-to-local/pass exec-file) "llvm-link"]

            keep "-o"

            output: file-to-local output
            either ends-with? output suffix [
                keep output
            ][
                keep [output suffix]
            ]

            ; llvm-link doesn't seem to deal with libraries
            comment [
                for-each search (map-files-to-local searches) [
                    keep ["-L" search]
                ]
            ]

            for-each flg ldflags [
                keep try filter-flag flg id
            ]

            for-each dep depends [
                keep try accept dep
            ]
        ]
    ]

    accept: meth [
        return: [<opt> text!]
        dep [object!]
    ][
        return (opt switch dep/class [
            #object-file [
                file-to-local dep/output
            ]
            #dynamic-extension [
                _
            ]
            #static-extension [
                _
            ]
            #object-library [
                spaced map-each ddep dep/depends [
                    file-to-local ddep/output
                ]
            ]
            #application [
                _
            ]
            #variable [
                _
            ]
            #entry [
                _
            ]
            (elide dump dep)
            fail "unrecognized dependency"
        ])
    ]
]

; Microsoft linker
link: make linker-class [
    name: 'link
    id: "msc"
    version: _
    exec-file: _
    command: meth [
        return: [text!]
        output [file!]
        depends [block! blank!]
        searches [block! blank!]
        ldflags [block! any-string! blank!]
        /dynamic
        /debug [logic!]
    ][
        let suffix: either dynamic [
            target-platform/dll-suffix
        ][
            target-platform/exe-suffix
        ]
        return collect-text [
            keep any [(file-to-local/pass exec-file) "link"]

            ; https://docs.microsoft.com/en-us/cpp/build/reference/debug-generate-debug-info
            if debug [keep "/DEBUG"]

            keep "/NOLOGO"  ; don't show startup banner (link takes uppercase!)
            if dynamic [keep "/DLL"]

            output: file-to-local output
            keep [
                "/OUT:" either ends-with? output suffix [
                    output
                ][
                    unspaced [output suffix]
                ]
            ]

            for-each search (map-files-to-local searches) [
                keep ["/LIBPATH:" search]
            ]

            for-each flg ldflags [
                keep try filter-flag flg id
            ]

            for-each dep depends [
                keep try accept dep
            ]
        ]
    ]

    accept: meth [
        return: [<opt> text!]
        dep [object!]
    ][
        return (opt switch dep/class [
            #object-file [
                file-to-local to-file dep/output
            ]
            #dynamic-extension [
                comment [import file]  ; static property is ignored

                either tag? dep/output [
                    try filter-flag dep/output id
                ][
                    ;dump dep/output
                    file-to-local/pass either ends-with? dep/output ".lib" [
                        dep/output
                    ][
                        join dep/output ".lib"
                    ]
                ]
            ]
            #static-extension [
                file-to-local dep/output
            ]
            #object-library [
                spaced map-each ddep dep/depends [
                    file-to-local to-file ddep/output
                ]
            ]
            #application [
                file-to-local any [dep/implib join dep/basename ".lib"]
            ]
            #variable [
                _
            ]
            #entry [
                _
            ]
            (elide dump dep)
            fail "unrecognized dependency"
        ])
    ]
]

strip-class: make object! [
    class: #strip
    name: _
    id: _  ; flag prefix
    exec-file: _
    options: _
    commands: meth [
        return: [block!]
        target [file!]
        /params [block! any-string!]
    ][
        reduce [collect-text [
            keep any [(file-to-local/pass exec-file) "strip"]
            params: default [options]
            switch type of params [
                block! [
                    for-each flag params [
                        keep filter-flag flag id
                    ]
                ]
                text! [
                    keep params
                ]
            ]
            keep file-to-local target
        ]]
    ]
]

strip: make strip-class [
    id: "gnu"
]

; includes/definitions/cflags will be inherited from its immediately ancester
object-file-class: make object! [
    class: #object-file
    compiler: _
    cflags: _
    definitions: _
    source: ~
    output: ~
    basename: _  ; output without extension part
    optimization: _
    debug: _
    includes: _
    generated?: false
    depends: _

    compile: meth [return: <none>] [
        compiler/compile
    ]

    command: meth [
        return: [text!]
        /I "extra includes" [block!]
        /D "extra definitions" [block!]
        /F "extra cflags (override)" [block!]
        /O "opt-level" [any-value!]  ; !!! datatypes?
        /g "dbg" [any-value!]  ; !!! datatypes?
        /PIC "https://en.wikipedia.org/wiki/Position-independent_code"
        /E "only preprocessing"
    ][
        let cc: any [compiler default-compiler]

        if optimization = #prefer-O2-optimization [
            any [
                not O
                O = "s"
            ] then [
                O: 2  ; don't override e.g. "-Oz"
            ]
            optimization: false
        ]

        return cc/command/I/D/F/O/g/(PIC)/(E) output source
            compose [((opt includes)) ((opt I))]
            compose [((opt definitions)) ((opt D))]
            compose [((opt F)) ((opt cflags))]  ; extra cflags override

            ; "current setting overwrites /refinement"
            ; "because the refinements are inherited from the parent" (?)

            opt either O [O][optimization]
            opt either g [g][debug]
    ]

    gen-entries: meth [
        return: [object!]
        parent [object!]
        /PIC "https://en.wikipedia.org/wiki/Position-independent_code"
    ][
        assert [
            find [
                #application
                #dynamic-library
                #static-library
                #object-library
            ] parent/class
        ]

        return make entry-class [
            target: output
            depends: append copy either depends [depends][[]] source
            commands: reduce [command/I/D/F/O/g/(
                any [
                    PIC
                    parent/class = #dynamic-library
                ] then [
                    'PIC
                ] else [
                    _
                ]
            )
                opt parent/includes
                opt parent/definitions
                opt parent/cflags
                opt parent/optimization
                opt parent/debug
            ]
        ]
    ]
]

entry-class: make object! [
    class: #entry
    id: _
    target: ~
    depends: _
    commands: ~
    generated?: false
]

var-class: make object! [
    class: #variable
    name: ~
    value: _  ; behavior is `any [value, default]`, so start as blank
    default: ~
    generated?: false
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
    options: _
    strip: _
]

generator-class: make object! [
    class: #generator

    vars: make map! 128

    gen-cmd-create: _
    gen-cmd-delete: _
    gen-cmd-strip: _

    gen-cmd: meth [
        return: [text!]
        cmd [object!]
    ][
        return switch cmd/class [
            #cmd-create [
                applique any [
                    :gen-cmd-create :target-platform/gen-cmd-create
                ] compose [
                    cmd: (cmd)
                ]
            ]
            #cmd-delete [
                applique any [
                    :gen-cmd-delete :target-platform/gen-cmd-delete
                ] compose [
                    cmd: (cmd)
                ]
            ]
            #cmd-strip [
                applique any [
                    :gen-cmd-strip :target-platform/gen-cmd-strip
                ] compose [
                    cmd: (cmd)
                ]
            ]

            fail ["Unknown cmd class:" cmd/class]
        ]
    ]

    reify: meth [
        {Substitute variables in the command with its value}
        {(will recursively substitute if the value has variables)}

        return: [<opt> object! any-string!]
        cmd [object! any-string!]
    ][
        ; !!! These were previously static, but bootstrap executable's non
        ; gathering function form could not handle statics.
        ;
        let letter: charset [#"a" - #"z" #"A" - #"Z"]
        let digit: charset "0123456789"
        let localize: func [v][either file? v [file-to-local v][v]]

        if object? cmd [
            assert [
                find [
                    #cmd-create #cmd-delete #cmd-strip
                ] cmd/class
            ]
            cmd: gen-cmd cmd
        ]
        if not cmd [return null]

        let stop: false
        let name
        let val
        loop [not stop][
            stop: true
            parse cmd [
                while [
                    change [
                        [
                            "$(" copy name: some [letter | digit | #"_"] ")"
                            | "$" copy name: letter
                        ] (
                            val: localize select vars name
                            stop: false
                        )
                    ] (val)
                    | skip
                ]
                end
            ] else [
                fail ["failed to do var substitution:" cmd]
            ]
        ]
        cmd
    ]

    prepare: meth [
        return: <none>
        solution [object!]
    ][
        if find/only words-of solution 'output [
            setup-outputs solution
        ]
        flip-flag solution false

        if find/only words-of solution 'depends [
            for-each dep solution/depends [
                if dep/class = #variable [
                    append vars reduce [
                        dep/name
                        any [
                            dep/value
                            dep/default
                        ]
                    ]
                ]
            ]
        ]
    ]

    flip-flag: meth [
        return: <none>
        project [object!]
        to [logic!]
    ][
        all [
            find/only words-of project 'generated?
            to != project/generated?
        ] then [
            project/generated?: to
            if find/only words-of project 'depends [
                for-each dep project/depends [
                    flip-flag dep to
                ]
            ]
        ]
    ]

    setup-output: meth [
        return: <none>
        project [object!]
    ][
        if not let suffix: find reduce [
            #application try target-platform/exe-suffix
            #dynamic-library try target-platform/dll-suffix
            #static-library try target-platform/archive-suffix
            #object-library try target-platform/archive-suffix
            #object-file try target-platform/obj-suffix
        ] project/class [return]

        suffix: second suffix

        case [
            blank? project/output [
                switch project/class [
                    #object-file [
                        project/output: copy project/source
                    ]
                    #object-library [
                        project/output: to text! project/name
                    ]

                    fail ["Unexpected project class:" (project/class)]
                ]
                if output-ext: find-last project/output #"." [
                    remove output-ext
                ]

                basename: project/output
                project/output: join basename suffix
            ]
            ends-with? project/output try suffix [
                basename: either suffix [
                    copy/part project/output
                        (length of project/output) - (length of suffix)
                ][
                    copy project/output
                ]
            ]
        ] else [
            basename: project/output
            project/output: join basename suffix
        ]

        project/basename: basename
    ]

    setup-outputs: meth [
        {Set the output/implib for the project tree}
        return: <none>
        project [object!]
    ][
        ;print ["Setting outputs for:"]
        ;dump project
        switch project/class [
            #application
            #dynamic-library
            #static-library
            #solution
            #object-library [
                if project/generated? [return]
                setup-output project
                project/generated?: true
                for-each dep project/depends [
                    setup-outputs dep
                ]
            ]
            #object-file [
                setup-output project
            ]
        ] else [return]
    ]
]

makefile: make generator-class [
    nmake?: false ; Generating for Microsoft nmake

    ;by default makefiles are for POSIX platform
    gen-cmd-create: :posix/gen-cmd-create
    gen-cmd-delete: :posix/gen-cmd-delete
    gen-cmd-strip: :posix/gen-cmd-strip

    gen-rule: meth [
        return: "Possibly multi-line text for rule, with extra newline @ end"
            [text!]
        entry [object!]
    ][
        return newlined collect-lines [switch entry/class [

            ; Makefile variable, defined on a line by itself
            ;
            #variable [
                keep either entry/value [
                    [entry/name "=" entry/value]
                ][
                    [entry/name either nmake? ["="]["?="] entry/default]
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
                keep collect-text [
                    case [
                        word? entry/target [  ; like `clean` in `make clean`
                            keep [entry/target ":"]
                            keep ".PHONY"
                        ]
                        file? entry/target [
                            keep [file-to-local entry/target ":"]
                        ]
                        fail ["Unknown entry/target type" entry/target]
                    ]
                    for-each w (ensure [block! blank!] entry/depends) [
                        switch pick (try match object! w) 'class [
                            #variable [
                                keep ["$(" w/name ")"]
                            ]
                            #entry [
                                keep to-text w/target
                            ]
                            #dynamic-extension #static-extension [
                                ; only contribute to command line
                            ]
                        ] else [
                            keep case [
                                file? w [file-to-local w]
                                file? w/output [file-to-local w/output]
                            ] else [w/output]
                        ]
                    ]
                ]

                ; After the line with its target and dependencies are the
                ; lines of shell code that run to build the target.  These
                ; may use escaped makefile variables that get substituted.
                ;
                for-each cmd (ensure [block! blank!] entry/commands) [
                    let c: ((match text! cmd) else [gen-cmd cmd]) else [continue]
                    if empty? c [continue]  ; !!! Review why this happens
                    keep [tab c]  ; makefiles demand TAB codepoint :-(
                ]
            ]

            fail ["Unrecognized entry class:" entry/class]
        ] keep ""]  ; final keep just adds an extra newline

        ; !!! Adding an extra newline here unconditionally means variables
        ; in the makefile get spaced out, which isn't bad--but it wasn't done
        ; in the original rebmake.r.  This could be rethought to leave it
        ; to the caller to decide to add the spacing line or not
    ]

    emit: meth [
        return: <none>
        buf [binary!]
        project [object!]
        /parent [object!]  ; !!! Not heeded?
    ][
        ;print ["emitting..."]
        ;dump project
        ;if project/generated? [return]
        ;project/generated?: true

        for-each dep project/depends [
            if not object? dep [continue]
            ;dump dep
            if not find [#dynamic-extension #static-extension] dep/class [
                either dep/generated? [
                    continue
                ][
                    dep/generated?: true
                ]
            ]
            switch dep/class [
                #application
                #dynamic-library
                #static-library [
                    let objs: make block! 8
                    ;dump dep
                    for-each obj dep/depends [
                        ;dump obj
                        if obj/class = #object-library [
                            append objs obj/depends
                        ]
                    ]
                    append buf gen-rule make entry-class [
                        target: dep/output
                        depends: join objs map-each ddep dep/depends [
                            if ddep/class <> #object-library [ddep]
                        ]
                        commands: append reduce [dep/command] try dep/post-build-commands
                    ]
                    emit buf dep
                ]
                #object-library [
                    comment [
                        ; !!! Said "No nested object-library-class allowed"
                        ; but was commented out (?)
                        assert [dep/class != #object-library]
                    ]
                    for-each obj dep/depends [
                        assert [obj/class = #object-file]
                        if not obj/generated? [
                            obj/generated?: true
                            append buf gen-rule obj/gen-entries/(try all [
                                project/class = #dynamic-library
                                'PIC
                            ]) dep
                        ]
                    ]
                ]
                #object-file [
                    append buf gen-rule dep/gen-entries project
                ]
                #entry #variable [
                    append buf gen-rule dep
                ]
                #dynamic-extension #static-extension [
                    _
                ]
                (elide dump dep)
                fail ["unrecognized project type:" dep/class]
            ]
        ]
    ]

    generate: meth [
        return: <none>
        output [file!]
        solution [object!]
    ][
        let buf: make binary! 2048
        assert [solution/class = #solution]

        prepare solution

        emit buf solution

        write output append buf "^/^/.PHONY:"
    ]
]

nmake: make makefile [
    nmake?: true

    ; reset them, so they will be chosen by the target platform
    gen-cmd-create: _
    gen-cmd-delete: _
    gen-cmd-strip: _
]

; For mingw-make on Windows
mingw-make: make makefile [
    ; reset them, so they will be chosen by the target platform
    gen-cmd-create: _
    gen-cmd-delete: _
    gen-cmd-strip: _
]

; Execute the command to generate the target directly
;
export execution: make generator-class [
    host: switch system/platform/1 [
        'Windows [windows]
        'Linux [linux]
        'OSX [osx]
        'Android [android]
    ] else [
        print [
            "Untested platform" system/platform "- assume POSIX compilant"
        ]
        posix
    ]

    gen-cmd-create: :host/gen-cmd-create
    gen-cmd-delete: :host/gen-cmd-delete
    gen-cmd-strip: :host/gen-cmd-strip

    run-target: meth [
        return: <none>
        target [object!]
        /cwd "change working directory"  ; !!! Not heeded (?)
            [file!]
    ][
        switch target/class [
            #variable [
                _  ; already been taken care of by PREPARE
            ]
            #entry [
                if all [
                    not word? target/target
                    ; so you can use words for "phony" targets
                    exists? to-file target/target
                ] [return] ;TODO: Check the timestamp to see if it needs to be updated
                either block? target/commands [
                    for-each cmd target/commands [
                        cmd: reify cmd
                        print ["Running:" cmd]
                        call/shell cmd
                    ]
                ][
                    let cmd: reify target/commands
                    print ["Running:" cmd]
                    call/shell cmd
                ]
            ]
            (elide dump target)
            fail "Unrecognized target class"
        ]
    ]

    run: meth [
        return: <none>
        project [object!]
        /parent "parent project"
            [object!]
    ][
        ;dump project
        if not object? project [return]

        prepare project

        if not find [#dynamic-extension #static-extension] project/class [
            if project/generated? [return]
            project/generated?: true
        ]

        switch project/class [
            #application
            #dynamic-library
            #static-library [
                let objs: make block! 8
                for-each obj project/depends [
                    if obj/class = #object-library [
                        append objs obj/depends
                    ]
                ]
                for-each dep project/depends [
                    run/parent dep project
                ]
                run-target make entry-class [
                    target: project/output
                    depends: join project/depends objs
                    commands: reduce [project/command]
                ]
            ]
            #object-library [
                for-each obj project/depends [
                    assert [obj/class = #object-file]
                    if not obj/generated? [
                        obj/generated?: true
                        run-target obj/gen-entries/(try all [
                            parent/class = #dynamic-library
                            'PIC
                        ]) project
                    ]
                ]
            ]
            #object-file [
                assert [parent]
                run-target project/gen-entries p-project
            ]
            #entry #variable [
                run-target project
            ]
            #dynamic-extension #static-extension [
                _
            ]
            #solution [
                for-each dep project/depends [
                    run dep
                ]
            ]
            (elide dump project)
            fail ["unrecognized project type:" project/class]
        ]
    ]
]
