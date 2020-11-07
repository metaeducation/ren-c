REBOL [
    File: %rebmake.r
    Title: {Rebol-Based C/C++ Makefile and Project File Generator}

    ; !!! Making %rebmake.r a module means it gets its own copy of lib, which
    ; creates difficulties for the bootstrap shim technique.  Changing the
    ; semantics of lib (e.g. how something fundamental like IF or CASE would
    ; work) could break the mezzanine.  For the time being, just use DO to
    ; run it in user, as with other pieces of bootstrap.
    ;
    ; Type: 'module

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

rebmake: make object! [  ; hack to workaround lack of Type: 'module

default-compiler: _
default-linker: _
default-strip: _
target-platform: _

map-files-to-local: function [
    return: [block!]
    files [file! block!]
][
    if not block? files [files: reduce [files]]
    map-each f files [
        file-to-local f
    ]
]

ends-with?: func [
    return: [logic!]
    s [any-string!]
    suffix [blank! any-string!]
][
    did any [
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

run-command: function [
    cmd [block! text!]
][
    x: copy ""
    call/shell/output cmd x
    trim/with x "^/^M"
]

pkg-config: function [
    return: [text! block!]
    pkg [any-string!]
    var [word!]
    lib [any-string!]
][
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

    x: run-command spaced reduce [pkg opt lib]
    ;dump x
    either dlm [
        ret: make block! 1
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
    exe-suffix: _
    dll-suffix: _
    archive-suffix: _ ;static library
    obj-suffix: _

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

    gen-cmd-create: method [
        return: [text!]
        cmd [object!]
    ][
        either dir? cmd/file [
            spaced ["mkdir -p" cmd/file]
        ][
            spaced ["touch" cmd/file]
        ]
    ]

    gen-cmd-delete: method [
        return: [text!]
        cmd [object!]
    ][
        spaced ["rm -fr" cmd/file]
    ]

    gen-cmd-strip: method [
        return: [text!]
        cmd [object!]
    ][
        if tool: any [:cmd/strip :default-strip] [
            b: ensure block! tool/commands/params cmd/file opt cmd/options
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

    gen-cmd-create: method [
        return: [text!]
        cmd [object!]
    ][
        d: file-to-local cmd/file
        if #"\" = last d [remove back tail-of d]
        either dir? cmd/file [
            spaced ["mkdir" d]
        ][
            unspaced ["echo . 2>" d]
        ]
    ]
    gen-cmd-delete: method [
        return: [text!]
        cmd [object!]
    ][
        d: file-to-local cmd/file
        if #"\" = last d [remove back tail-of d]
        either dir? cmd/file [
            spaced ["rmdir /S /Q" d]
        ][
            spaced ["del" d]
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
        default [
            print ["Unknown platform:" platform "falling back to POSIX"]
            target-platform: posix
        ]
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

    link: method [return: <void>] [
        linker/link output depends ldflags
    ]

    command: method [return: [text!]] [
        ld: any [
            linker
            default-linker
        ]
        ld/command
            output
            depends
            searches
            ldflags
    ]

]

dynamic-library-class: make project-class [
    class: #dynamic-library
    type: 'dynamic
    generated?: false
    linker: _

    searches: _
    ldflags: _
    link: method [return: <void>] [
        linker/link output depends ldflags
    ]

    command: method [
        return: [text!]
        <with>
        default-linker
    ][
        l: any [
            linker
            default-linker
        ]
        l/command/dynamic
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
    compile: method [
        return: <void>
        output [file!]
        source [file!]
        include [file! block!]
        definition [any-string!]
        cflags [any-string!]
    ][
    ]

    command: method [
        return: [text!]
        output
        source
        includes
        definitions
        cflags
    ][
    ]
    ;check if the compiler is available
    check: method [
        return: [logic!]
        path [<blank> any-string!]
    ][
    ]
]

gcc: make compiler-class [
    name: 'gcc
    id: "gnu"
    check: method [
        return: [logic!]
        /exec [file!]
        <static>
        digit (charset "0123456789")
    ][
        version: copy ""
        attempt [
            exec-file: exec: default ["gcc"]
            call/output reduce [exec "--version"] version
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

    command: method [
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
        collect-text [
            keep (file-to-local/pass exec-file else [
                to text! name  ; the "gcc" may get overridden as "g++"
            ])

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
                    if find [gcc g++] name [
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
                    keep filter-flag flg id
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
    command: method [
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
        collect-text [
            keep ("cl" unless file-to-local/pass exec-file)
            keep "/nologo"  ; don't show startup banner
            keep either E ["/P"]["/c"]

            if I [
                for-each inc (map-files-to-local I) [
                    keep ["/I" inc]
                ]
            ]
            if D [
                for-each flg D [
                    keep ["/D" (filter-flag flg id else [continue])]
                ]
            ]
            if O [
                case [
                    O = true [keep "/O2"]
                    O and [not zero? O] [
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
                    keep filter-flag flg id
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
    link: method [][
        return: <void>
    ]
    commands: method [
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
    command: method [
        return: [text!]
        output [file!]
        depends [block! blank!]
        searches [block! blank!]
        ldflags [block! any-string! blank!]
        /dynamic
    ][
        suffix: either dynamic [
            target-platform/dll-suffix
        ][
            target-platform/exe-suffix
        ]
        collect-text [
            keep ("gcc" unless file-to-local/pass exec-file)

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
                keep filter-flag flg id
            ]

            for-each dep depends [
                keep accept dep
            ]
        ]
    ]

    accept: method [
        return: [<opt> text!]
        dep [object!]
    ][
        opt switch dep/class [
            #object-file [
                file-to-local dep/output
            ]
            #dynamic-extension [
                either tag? dep/output [
                    if lib: filter-flag dep/output id [
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
            default [
                dump dep
                fail "unrecognized dependency"
            ]
        ]
    ]

    check: method [
        return: [logic!]
        /exec [file!]
    ][
        version: copy ""
        ;attempt [
            exec-file: exec: default ["gcc"]
            call/output reduce [exec "--version"] version
        ;]
    ]
]

llvm-link: make linker-class [
    name: 'llvm-link
    version: _
    exec-file: _
    id: "llvm"
    command: method [
        return: [text!]
        output [file!]
        depends [block! blank!]
        searches [block! blank!]
        ldflags [block! any-string! blank!]
        /dynamic
    ][
        suffix: either dynamic [
            target-platform/dll-suffix
        ][
            target-platform/exe-suffix
        ]

        collect-text [
            keep ("llvm-link" unless file-to-local/pass exec-file)

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
                keep filter-flag flg id
            ]

            for-each dep depends [
                keep accept dep
            ]
        ]
    ]

    accept: method [
        return: [<opt> text!]
        dep [object!]
    ][
        opt switch dep/class [
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
            default [
                dump dep
                fail "unrecognized dependency"
            ]
        ]
    ]
]

; Microsoft linker
link: make linker-class [
    name: 'link
    id: "msc"
    version: _
    exec-file: _
    command: method [
        return: [text!]
        output [file!]
        depends [block! blank!]
        searches [block! blank!]
        ldflags [block! any-string! blank!]
        /dynamic
    ][
        suffix: either dynamic [
            target-platform/dll-suffix
        ][
            target-platform/exe-suffix
        ]
        collect-text [
            keep (file-to-local/pass exec-file else [{link}])
            keep "/NOLOGO"
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
                keep filter-flag flg id
            ]

            for-each dep depends [
                keep accept dep
            ]
        ]
    ]

    accept: method [
        return: [<opt> text!]
        dep [object!]
    ][
        opt switch dep/class [
            #object-file [
                file-to-local to-file dep/output
            ]
            #dynamic-extension [
                comment [import file]  ; static property is ignored

                either tag? dep/output [
                    filter-flag dep/output id
                ][
                    ;dump dep/output
                    either ends-with? dep/output ".lib" [
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
            default [
                dump dep
                fail "unrecognized dependency"
            ]
        ]
    ]
]

strip-class: make object! [
    class: #strip
    name: _
    id: _ ;flag prefix
    exec-file: _
    options: _
    commands: method [
        return: [block!]
        target [file!]
        /params [block! any-string!]
    ][
        reduce [collect-text [
            keep ("strip" unless file-to-local/pass exec-file)
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
    source: _
    output: _
    basename: _ ;output without extension part
    optimization: _
    debug: _
    includes: _
    generated?: false
    depends: _

    compile: method [return: <void>] [
        compiler/compile
    ]

    command: method [
        return: [text!]
        /I "extra includes" [block!]
        /D "extra definitions" [block!]
        /F "extra cflags (override)" [block!]
        /O "opt-level" [any-value!]  ; !!! datatypes?
        /g "dbg" [any-value!]  ; !!! datatypes?
        /PIC "https://en.wikipedia.org/wiki/Position-independent_code"
        /E "only preprocessing"
    ][
        cc: any [compiler default-compiler]

        if optimization = #prefer-O2-optimization [
            any [
                not set? 'O
                O = "s"
            ] then [
                O: 2  ; don't override e.g. "-Oz"
            ]
            optimization: false
        ]

        cc/command/I/D/F/O/g/(PIC)/(E) output source
            compose [((opt includes)) ((opt I))]
            compose [((opt definitions)) ((opt D))]
            compose [((opt F)) ((opt cflags))]  ; extra cflags override

            ; "current setting overwrites /refinement"
            ; "because the refinements are inherited from the parent" (?)

            opt either O [O][optimization]
            opt either g [g][debug]
    ]

    gen-entries: method [
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

        make entry-class [
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
    target:
    depends:
    commands: _
    generated?: false
]

var-class: make object! [
    class: #variable
    name: _
    value: _
    default: _
    generated?: false
]

cmd-create-class: make object! [
    class: #cmd-create
    file: _
]

cmd-delete-class: make object! [
    class: #cmd-delete
    file: _
]

cmd-strip-class: make object! [
    class: #cmd-strip
    file: _
    options: _
    strip: _
]

generator-class: make object! [
    class: #generator

    vars: make map! 128

    gen-cmd-create: _
    gen-cmd-delete: _
    gen-cmd-strip: _

    gen-cmd: method [
        return: [text!]
        cmd [object!]
    ][
        switch cmd/class [
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

    reify: method [
        {Substitute variables in the command with its value}
        {(will recursively substitute if the value has variables)}

        return: [<opt> object! any-string!]
        cmd [object! any-string!]
        <static>
        letter (charset [#"a" - #"z" #"A" - #"Z"])
        digit (charset "0123456789")
        localize (func [v][either file? v [file-to-local v][v]])
    ][
        if object? cmd [
            assert [
                find [
                    #cmd-create #cmd-delete #cmd-strip
                ] cmd/class
            ]
            cmd: gen-cmd cmd
        ]
        if not cmd [return null]

        stop: false
        while [not stop][
            stop: true
            parse cmd [
                while [
                    change [
                        [
                            "$(" copy name: some [letter | digit | #"_"] ")"
                            | "$" copy name: letter
                        ] (val: localize select vars name | stop: false)
                    ] val
                    | skip
                ]
                end
            ] else [
                fail ["failed to do var substitution:" cmd]
            ]
        ]
        cmd
    ]

    prepare: method [
        return: <void>
        solution [object!]
    ][
        if find words-of solution 'output [
            setup-outputs solution
        ]
        flip-flag solution false

        if find words-of solution 'depends [
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

    flip-flag: method [
        return: <void>
        project [object!]
        to [logic!]
    ][
        all [
            find words-of project 'generated?
            to != project/generated?
        ] then [
            project/generated?: to
            if find words-of project 'depends [
                for-each dep project/depends [
                    flip-flag dep to
                ]
            ]
        ]
    ]

    setup-output: method [
        return: <void>
        project [object!]
    ][
        if not suffix: find reduce [
            #application target-platform/exe-suffix
            #dynamic-library target-platform/dll-suffix
            #static-library target-platform/archive-suffix
            #object-library target-platform/archive-suffix
            #object-file target-platform/obj-suffix
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
            ends-with? project/output suffix [
                basename: either suffix [
                    copy/part project/output
                        (length of project/output) - (length of suffix)
                ][
                    copy project/output
                ]
            ]
            default [
                basename: project/output
                project/output: join basename suffix
            ]
        ]

        project/basename: basename
    ]

    setup-outputs: method [
        {Set the output/implib for the project tree}
        return: <void>
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
            default [
                return
            ]
        ]
    ]
]

makefile: make generator-class [
    nmake?: false ; Generating for Microsoft nmake

    ;by default makefiles are for POSIX platform
    gen-cmd-create: :posix/gen-cmd-create
    gen-cmd-delete: :posix/gen-cmd-delete
    gen-cmd-strip: :posix/gen-cmd-strip

    gen-rule: method [
        return: "Possibly multi-line text for rule, with extra newline @ end"
            [text!]
        entry [object!]
    ][
        newlined collect-lines [switch entry/class [

            ; Makefile variable, defined on a line by itself
            ;
            #variable [
                keep either entry/default [
                    [entry/name either nmake? ["="]["?="] entry/default]
                ][
                    [entry/name "=" entry/value]
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
                        switch w/class [
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
                                default [w/output]
                            ]
                        ]
                    ]
                ]

                ; After the line with its target and dependencies are the
                ; lines of shell code that run to build the target.  These
                ; may use escaped makefile variables that get substituted.
                ;
                for-each cmd (ensure [block! blank!] entry/commands) [
                    c: ((match text! cmd) else [gen-cmd cmd]) else [continue]
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

    emit: method [
        return: <void>
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
                    objs: make block! 8
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
                        commands: append reduce [dep/command] opt dep/post-build-commands
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
                default [
                    dump dep
                    fail ["unrecognized project type:" dep/class]
                ]
            ]
        ]
    ]

    generate: method [
        return: <void>
        output [file!]
        solution [object!]
    ][
        buf: make binary! 2048
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
Execution: make generator-class [
    host: switch system/platform/1 [
        'Windows [windows]
        'Linux [linux]
        'OSX [osx]
        'Android [android]

        default [
           print [
               "Untested platform" system/platform "- assume POSIX compilant"
           ]
           posix
        ]
    ]

    gen-cmd-create: :host/gen-cmd-create
    gen-cmd-delete: :host/gen-cmd-delete
    gen-cmd-strip: :host/gen-cmd-strip

    run-target: method [
        return: <void>
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
                    cmd: reify target/commands
                    print ["Running:" cmd]
                    call/shell cmd
                ]
            ]
            default [
                dump target
                fail "Unrecognized target class"
            ]
        ]
    ]

    run: method [
        return: <void>
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
                objs: make block! 8
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
            default [
                dump project
                fail ["unrecognized project type:" project/class]
            ]
        ]
    ]
]

visual-studio: make generator-class [
    solution-format-version: "12.00"
    tools-version: "15.0"  ; "15.00" warns in 'Detailed' MSBuild output

    ; should autodetect.  These are the choices that upgrading to vs2019 gave.
    ;
    target-win-version: "10.0"  ; vs2017 picked "10.0.17134.0"
    platform-tool-set: "v142"  ; vs2017 picked "v141"

    platform: cpu: "x64"
    build-type: "Release"

    ; To not depend on UUID module, keep a few static UUIDs for our use
    uuid-pool: copy [
        {{feba3ac1-cb28-421d-ae18-f4d85ec86f56}}
        {{ab8d2c55-dd90-4be5-b632-cc5aa9b2ae8f}}
        {{1d7d6eda-d664-4694-95ca-630ee049afe8}}
        {{c8af96e8-7d16-4c98-9c60-6dd9aafec31f}}
        {{a4724751-acc7-4b14-9021-f12744a9c15e}}
        {{1a937e41-3a08-4735-94dd-ab9a4b4df0ea}}
        {{9de42f7c-7060-497a-a1ad-02944afd1fa9}}
        {{49ce80a5-c3f3-4b0a-bbdf-b4efe48f6250}}
        {{b5686769-2039-40d4-bf1d-c0b3df77aa5e}}
        {{fc927e45-049f-448f-87ed-a458a07d532e}}
        {{4127412b-b471-402a-bd18-e891de7842e0}}
        {{0e140421-7f17-49f1-a3ba-0c952766c368}}
        {{2cbec086-bf07-4a0a-bf7a-cc9b450e0082}}
        {{d2d14156-38e0-46b5-a22b-780e8e6d3380}}
        {{01f70fc0-fa70-48f5-ab6c-ecbd9b3b8630}}
        {{ab185938-0cee-4455-8585-d38283d30816}}
        {{5d53ce20-0de9-4df8-9dca-cbc462db399d}}
        {{00cb2282-6568-43e9-a36b-f719dedf86aa}}
        {{cd81af55-2c02-46e9-b5e4-1d74245183e2}}
        {{d670cd39-3fdb-46c7-a63b-d910bcfcd9bf}}
        {{58d19a29-fe72-4c32-97d4-c7eabb3fc22f}}
        {{4ca0596a-61ab-4a05-971d-10f3346f5c3c}}
        {{7d4a3355-74b3-45a3-9fc9-e8a4ef92c678}}
        {{e8b967b5-437e-44ba-aca4-0dbb4e4b4bba}}
        {{14218ad6-7626-4d5f-9ddb-4f1633699d81}}
        {{f7a13215-b889-4358-95fe-a95fd0081878}}
        {{a95d235d-af5a-4b7b-a5c3-640fe34333e5}}
        {{f5c1f9da-c24b-4160-b121-d16d0ae5b143}}
        {{d08ce3e5-c68d-4f2c-b949-95554081ebfa}}
        {{4e9e6993-4898-4121-9674-d9924dcead2d}}
        {{8c972c49-d2ed-4cd1-a11e-5f62a0ca18f6}}
        {{f4af8888-f2b9-473a-a630-b95dc29b33e3}}
        {{015eb329-e714-44f1-b6a2-6f08fcbe5ca0}}
        {{82521230-c50a-4687-b0bb-99fe47ebb2ef}}
        {{4eb6851f-1b4e-4c40-bdb8-f006eca60bd3}}
        {{59a8f079-5fb8-4d54-894d-536b120f048e}}
        {{7f4e6cf3-7a50-4e96-95ed-e001acb44a04}}
        {{0f3c59b5-479c-4883-8d90-33fc6ca5926c}}
        {{44ea8d3d-4509-4977-a00e-579dbf50ff75}}
        {{8782fd76-184b-4f0a-b9fe-260d30bb21ae}}
        {{7c4813f4-6ffb-4dba-8cf5-6b8c0a390904}}
        {{452822f8-e133-47ea-9788-7da10de23dc0}}
        {{6ea04743-626f-43f3-86be-a9fad5cd9215}}
        {{91c41a9d-4f5a-441a-9e80-c51551c754c3}}
        {{2a676e01-5fd1-4cbd-a3eb-461b45421433}}
        {{07bb66be-d5c7-4c08-88cd-534cf18d65c7}}
        {{f3e1c165-8ae5-4735-beb7-ca2d95f979eb}}
        {{608f81e0-3057-4a3b-bb9d-2a8a9883f54b}}
        {{e20f9729-4575-459a-98be-c69167089b8c}}
    ]

    emit: method [
        return: "Dependencies?"
            [block!]
        buf
        project [object!]
    ][
        project-name: if project/class = #entry [
            project/target
        ] else [
            project/name
        ]
        append buf unspaced [
            {Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}")}
                space {=} space {"} to text! project-name {"} {,}
                space {"} project-name {.vcxproj} {"} {,}
                space {"} project/id {"}
                newline
        ]

        ;print ["emitting..."]
        ;dump project
        depends: make block! 8
        for-each dep project/depends [
            if find [
                #object-library
                #dynamic-library
                #static-library
                #application
            ] dep/class [
                ;print ["adding" mold dep]
                append depends dep
            ]
        ]

        if not empty? depends [
            append buf {^-ProjectSection(ProjectDependencies) = postProject^/}
            for-each dep depends [
                if not dep/id [dep/id: take uuid-pool]
                append buf unspaced [
                    tab tab dep/id " = " dep/id newline
                ]
            ]
            append buf {^-EndProjectSection^/}
        ]

        append buf unspaced [
            {EndProject} newline
        ]

        return depends
    ]

    find-compile-as: method [
        return: [<opt> text!]
        cflags [block!]
    ][
        iterate cflags [
            i: filter-flag cflags/1 "msc" else [continue]
            case [
                parse i ["/TP" to end] [
                    comment [remove cflags] ; extensions wouldn't get it
                    return "CompileAsCpp"
                ]
                parse i ["/TC" to end] [
                    comment [remove cflags] ; extensions wouldn't get it
                    return "CompileAsC"
                ]
            ]
        ]
        return null
    ]

    find-stack-size: method [
        return: [<opt> text!]
        ldflags [<blank> block!]
        <static>
        digit (charset "0123456789")
    ][
        iterate ldflags [
            i: filter-flag ldflags/1 "msc" else [continue]
            parse i [
                "/stack:"
                copy size: some digit
                end
            ] then [
                remove ldflags
                return size
            ]
        ]
        return null
    ]

    find-subsystem: method [
        return: [<opt> text!]
        ldflags [<blank> block!]
    ][
        iterate ldflags [
            i: filter-flag ldflags/1 "msc" else [continue]
            parse i [
                "/subsystem:"
                copy subsystem: to end
            ] then [
                remove ldflags
                return subsystem
            ]
        ]
        return null
    ]

    find-optimization: method [
        return: [text!]
        project [object!]
        obj [blank! object!]
    ][
        optimization: all [
            obj
            obj/optimization = #prefer-O2-optimization
            elide (obj/optimization: _)
            project/optimization = 1
        ]
        then [2]
        else [
            either obj [obj/optimization] [project/optimization]
        ]

        switch optimization [
            0 _ 'no 'false 'off #[false] [
                "Disabled"
            ]
            1 ["MinSpace"]
            2 ["MaxSpeed"]
            'x ["Full"]

            fail ["Unrecognized optimization level:" (optimization)]
        ]
    ]

    find-optimization?: method [
        return: [logic!]
        optimization
    ][
        not find [0 _ no false off #[false]] optimization
    ]

    generate-project: method [
        return: <void>
        output-dir [file!] {Solution directory}
        project [object!]
    ][
        project-name: if project/class = #entry [
            project/target
        ] else [
            project/name
        ]
        if project/generated? [
            print ["project" project-name "was already generated"]
            return
        ]

        print ["Generating project file for" project-name]

        project/generated?: true

        if not find [
            #dynamic-library
            #static-library
            #application
            #object-library
            #entry
        ] project/class [
            dump project
            fail ["unsupported project:" (project/class)]
        ]

        project/id: uppercase copy take uuid-pool

        config: unspaced [build-type {|} platform]
        project-dir: unspaced [project-name ".dir\" build-type "\"]

        searches: make text! 1024
        if project/class <> #entry [
            inc: make text! 1024
            for-each i project/includes [
                if i: filter-flag i "msc" [
                    append inc unspaced [file-to-local i ";"]
                ]
            ]
            append inc "%(AdditionalIncludeDirectories)"

            def: make text! 1024
            for-each d project/definitions [
                if d: filter-flag d "msc" [
                    append def unspaced [d ";"]
                ]
            ]
            append def "%(PreprocessorDefinitions)"
            def

            lib: make text! 1024
            for-each d project/depends [
                switch d/class [
                    #dynamic-extension
                    #static-extension
                    #static-library [
                        if ext: filter-flag d/output "msc" [
                            append lib unspaced [
                                ext
                                if not ends-with? ext ".lib" [".lib"]
                                ";"
                            ]
                        ]
                    ]
                    #application [
                        append lib unspaced [any [d/implib unspaced [d/basename ".lib"]] ";"]
                        append searches unspaced [
                            unspaced [d/name ".dir\" build-type] ";"
                        ]
                    ]
                ]
            ]
            if not empty? lib [
                remove back tail-of lib ;move the trailing ";"
            ]

            if find [#dynamic-library #application] project/class [
                for-each s project/searches [
                    if s: filter-flag s "msc" [
                        append searches unspaced [file-to-local s ";"]
                    ]
                ]

                stack-size: try find-stack-size project/ldflags
            ]

            compile-as: try all [
                block? project/cflags
                find-compile-as project/cflags
            ]
        ]

        xml: unspaced [
            {<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="} tools-version {" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="} config {">
      <Configuration>} build-type {</Configuration>
      <Platform>} platform {</Platform>
    </ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <ProjectGUID>} project/id {</ProjectGUID>
    <WindowsTargetPlatformVersion>} target-win-version {</WindowsTargetPlatformVersion>}
    either project/class = #entry [
        unspaced [ {
    <RootNameSpace>} project-name {</RootNameSpace>}
        ]
    ][
        unspaced [ {
    <Platform>} platform {</Platform>
    <Keyword>Win32Proj</Keyword>
    <ProjectName>} project-name {</ProjectName>}
        ]
    ] {
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Label="Configuration">
    <ConfigurationType>} switch project/class [
      #static-library #object-library ["StaticLibrary"]
      #dynamic-library ["DynamicLibrary"]
      #application ["Application"]
      #entry ["Utility"]
      fail ["Unsupported project class:" (project/class)]
] {</ConfigurationType>
    <UseOfMfc>false</UseOfMfc>
    <CharacterSet>Unicode</CharacterSet>
    <PlatformToolset>} platform-tool-set {</PlatformToolset>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <ImportGroup Label="ExtensionSettings">
    <Import Project="$(VCTargetsPath)\BuildCustomizations\masm.props" />
  </ImportGroup>
  <ImportGroup Label="PropertySheets">
    <Import Project="$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props" Condition="exists('$(UserRootDir)\Microsoft.Cpp.$(Platform).user.props')" Label="LocalAppDataPlatform" />
  </ImportGroup>
  <PropertyGroup Label="UserMacros" />
  <PropertyGroup>}
  if project/class != #entry [
      unspaced [ {
    <_ProjectFileVersion>10.0.20506.1</_ProjectFileVersion>
    <OutDir>} project-dir {</OutDir>
    <IntDir>} project-dir {</IntDir>
    <TargetName>} project/basename {</TargetName>
    <TargetExt>} select [static ".lib" object ".lib" dynamic ".dll" application ".exe"] project/type {</TargetExt>}
    ]
  ] {
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>}
    if project/class <> #entry [
        unspaced [ {
      <AdditionalIncludeDirectories>} inc {</AdditionalIncludeDirectories>
      <AssemblerListingLocation>} build-type {/</AssemblerListingLocation>}
      ;RuntimeCheck is not compatible with optimization
      if not find-optimization? project [ {
      <BasicRuntimeChecks>EnableFastChecks</BasicRuntimeChecks>}
      ]
      if compile-as [
          unspaced [ {
      <CompileAs>} compile-as {</CompileAs>}
          ]
      ] {
      <DebugInformationFormat>} if build-type = "debug" ["ProgramDatabase"] {</DebugInformationFormat>
      <ExceptionHandling>Sync</ExceptionHandling>
      <InlineFunctionExpansion>} switch build-type ["debug" ["Disabled"] "release" ["AnySuitable"]] {</InlineFunctionExpansion>
      <Optimization>} find-optimization project _ {</Optimization>
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
      <RuntimeLibrary>MultiThreaded} if build-type = "debug" ["Debug"] {DLL</RuntimeLibrary>
      <RuntimeTypeInfo>true</RuntimeTypeInfo>
      <WarningLevel>Level3</WarningLevel>
      <TreatWarningAsError>
      </TreatWarningAsError>
      <PreprocessorDefinitions>} def {</PreprocessorDefinitions>
      <ObjectFileName>$(IntDir)</ObjectFileName>
      <AdditionalOptions>}
      if project/cflags [
          spaced map-each i project/cflags [
              filter-flag i "msc"
          ]
      ] {</AdditionalOptions>}
        ]
  ] {
    </ClCompile>}
    case [
        find [#dynamic-library #application] project/class [
            unspaced [ {
    <Link>
      <AdditionalOptions> /machine:} cpu { %(AdditionalOptions)</AdditionalOptions>
      <AdditionalDependencies>} lib {</AdditionalDependencies>
      <AdditionalLibraryDirectories>} searches {%(AdditionalLibraryDirectories)</AdditionalLibraryDirectories>
      <GenerateDebugInformation>} either build-type = "debug" ["Debug"]["false"] {</GenerateDebugInformation>
      <IgnoreSpecificDefaultLibraries>%(IgnoreSpecificDefaultLibraries)</IgnoreSpecificDefaultLibraries>
      <ImportLibrary>} project/basename {.lib</ImportLibrary>
      <ProgramDataBaseFile>} project/basename {.pdb</ProgramDataBaseFile>
      }
      if stack-size [
          unspaced [ {<StackReserveSize>} stack-size {</StackReserveSize>} ]
      ]
      {
      <SubSystem>} find-subsystem project/ldflags else ["Console"] {</SubSystem>
      <Version></Version>
    </Link>}
            ]
        ]
        find [#static-library #object-library] project/class [
            unspaced [ {
    <Lib>
      <AdditionalOptions> /machine:} cpu { %(AdditionalOptions)</AdditionalOptions>
    </Lib>}
            ]
        ]
        all [
            #entry = project/class
            project/commands
        ][
            unspaced [ {
    <PreBuildEvent>
      <Command>} delimit newline map-each cmd project/commands [reify cmd] {
      </Command>
    </PreBuildEvent>}
            ]
        ]
    ]
    if all [
        find words-of project 'post-build-commands
        project/post-build-commands
    ][
        unspaced [ {
    <PostBuildEvent>
      <Command>} delimit newline map-each cmd project/post-build-commands [reify cmd] {
      </Command>
    </PostBuildEvent>}
        ]
    ] {
  </ItemDefinitionGroup>
  <ItemGroup>
} use [o sources collected][
    sources: make text! 1024
    for-each o project/depends [
        switch o/class [
            #object-file [
                append sources unspaced [
                    {    <ClCompile Include="} o/source {">^/}
                    use [compile-as][
                        all [
                            block? o/cflags
                            compile-as: find-compile-as o/cflags
                            unspaced [
                                {        <CompileAs>} compile-as {</CompileAs>^/}
                            ]
                        ]
                    ]
                    if o/optimization [
                        unspaced [
                            {        <Optimization>} find-optimization project o {</Optimization>^/}
                        ]
                    ]
                    use [i o-inc][
                        o-inc: make text! 1024
                        for-each i o/includes [
                            if i: filter-flag i "msc" [
                                append o-inc unspaced [file-to-local i ";"]
                            ]
                        ]
                        if not empty? o-inc [
                            unspaced [
                                {        <AdditionalIncludeDirectories>} o-inc "%(AdditionalIncludeDirectories)"
                                {</AdditionalIncludeDirectories>^/}
                            ]
                        ]
                    ]
                    use [d o-def][
                        o-def: make text! 1024
                        for-each d o/definitions [
                            if d: filter-flag d "msc" [
                                append o-def unspaced [d ";"]
                            ]
                        ]
                        if not empty? o-def [
                            unspaced [
                                {        <PreprocessorDefinitions>}
                                o-def "%(PreprocessorDefinitions)"
                                {</PreprocessorDefinitions>^/}
                            ]
                        ]
                    ]
                    if o/output [
                        unspaced [
                            {        <ObjectFileName>}
                            file-to-local o/output
                            {</ObjectFileName>^/}
                        ]
                    ]

                    if o/cflags [
                        collected: map-each i o/cflags [
                            filter-flag i "msc"
                        ]
                        if not empty? collected [
                            unspaced [
                                {        <AdditionalOptions>}
                                spaced compose [
                                    {%(AdditionalOptions)}
                                    ((collected))
                                ]
                                {</AdditionalOptions>^/}
                            ]
                        ]
                    ]
                    {    </ClCompile>^/}
                ]
            ]

            #object-library [
                for-each f o/depends [
                    append sources unspaced [
                        {    <Object Include="} f/output {" />^/}
                    ]
                ]
            ]
        ]
    ]
    sources
  ] {
  </ItemGroup>}
  use [o refs][
    refs: make text! 1024
    for-each o project/depends [
        if find words-of o 'id [
            if not o/id [o/id: take uuid-pool]
            append refs unspaced [ {    <ProjectReference Include="} o/name {.vcxproj" >
      <Project>} o/id {</Project>
    </ProjectReference>^/}
            ]
        ]
    ]
    if not empty? refs [
        unspaced [ {
  <ItemGroup>
} refs
  {</ItemGroup>}
        ]
    ]
  ] {
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
  <ImportGroup Label="ExtensionTargets">
    <Import Project="$(VCTargetsPath)\BuildCustomizations\masm.targets" />
  </ImportGroup>
</Project>
}
        ]

        write out-file: output-dir/(unspaced [project-name ".vcxproj"]) xml
        ;print ["Wrote to" out-file]
    ]

    generate: method [
        return: <void>
        output-dir [file!] {Solution directory}
        solution [object!]
        /x86
    ][
        buf: make binary! 2048
        assert [solution/class = #solution]

        prepare solution

        if solution/debug [build-type: "Debug"]
        if x86 [cpu: "x86" platform: "Win32"]
        config: unspaced [build-type {|} platform]

        append buf unspaced [
            "Microsoft Visual Studio Solution File, Format Version"
                space solution-format-version newline

            ; Visual Studio 2019 added this
            "# Visual Studio Version 16" newline
            "VisualStudioVersion = 16.0.29324.140" newline
            "MinimumVisualStudioVersion = 10.0.40219.1" newline
        ]

        ;print ["vars:" mold vars]

        ; Project section
        projects: collect [
            for-each dep solution/depends [
                if find [
                    #dynamic-library
                    #static-library
                    #object-library
                    #application
                    #entry
                ] dep/class [
                    keep dep
                ]
            ]
        ]

        for-each dep projects [
            generate-project output-dir dep
        ]

        for-each dep projects [
            emit buf dep
        ]

        ; Global section
        append buf unspaced [
            "Global^/"
            "^-GlobalSection(SolutionConfigurationPlatforms) = preSolution^/"
            tab tab config { = } config newline
            "^-EndGlobalSection^/"
            "^-GlobalSection(ProjectConfigurationPlatforms) = postSolution^/"
        ]
        for-each proj projects [
            append buf unspaced [
                tab tab proj/id {.} config {.ActiveCfg = } config newline

                ; Visual Studio 2019 added this
                tab tab proj/id {.} config {.Build.0 = } config newline
            ]
        ]

        append buf "^-EndGlobalSection^/"

        ; Visual Studio 2019 added this
        append buf unspaced [
            {^-GlobalSection(SolutionProperties) = preSolution^/}
            {^-^-HideSolutionNode = FALSE^/}
            {^-EndGlobalSection^/}
            {^-GlobalSection(ExtensibilityGlobals) = postSolution^/}
            {^-^-SolutionGuid = {E262A8CB-4AA6-4845-89B5-BD3D0A43C041}^/}
            {^-EndGlobalSection^/}
        ]

        append buf "EndGlobal^/"

        write output-dir/(unspaced [solution/name ".sln"]) buf
    ]
]

vs2015: make visual-studio [
    platform-tool-set: "v140"
]

]  ; end of `rebmake: make object!` workaround for lack of `Type: 'module`
