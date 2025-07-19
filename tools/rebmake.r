Rebol [
    file: %rebmake.r
    title: {Rebol-Based C/C++ Makefile and Project File Generator}

    ; !!! Making %rebmake.r a module means it gets its own copy of lib, which
    ; creates difficulties for the bootstrap shim technique.  Changing the
    ; semantics of lib (e.g. how something fundamental like IF or CASE would
    ; work) could break the mezzanine.  For the time being, just use DO to
    ; run it in user, as with other pieces of bootstrap.
    ;
    ;-- type: module --

    rights: {
        Copyright 2017 Atronix Engineering
        Copyright 2017-2018 Rebol Open Source Developers
    }
    license: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    description: {
        R3-Alpha's bootstrap process depended on the GNU Make Tool, with a
        makefile generated from minor adjustments to a boilerplate copy of
        the makefile text.  As needs grew, a second build process arose
        which used CMake...which was also capable of creating files for
        various IDEs, such as Visual Studio.

        %rebmake.r arose to try and reconcile these two build processes, and
        eliminate dependency on an external make tool completely.  It can
        generate a makefile for GNU Make or Microsoft's Nmake, or just carry
        out a build by invoking compiler processes and command lines itself.

        (At one time it could generate Microsoft Visual Studio projects, but
        that ability was removed as it was massive and not part of the core
        interests of the project to maintain, as VS versions advanced.)

        In theory this code is abstracted such that it could be used by other
        projects.  In practice, it is tailored to the specific needs and
        settings of the Rebol project.
    }
]

rebmake: make object! [ ;-- hack to workaround lack of Type: 'module

default-compiler: null
default-strip: null
target-platform: null

map-files-to-local: function [
    return: [block!]
    files [file! block!]
][
    if not block? files [files: reduce [files]]
    return map-each f files [
        file-to-local f
    ]
]

ends-with?: func [
    return: [~null~ logic!]
    s [any-string!]
    suffix [<opt-out> any-string!]
][
    return did any [
        empty? suffix
        suffix ?= (skip tail-of s negate length of suffix)
    ]
]

filter-flag: function [
    return: [~null~ text! file!]
    flag [tag! text! file!]
        {If TAG! then must be <prefix:flag>, e.g. <gcc:-Wno-unknown-warning>}
    prefixes "gnu -> GCC-compatible compilers, msc -> Microsoft C"
        [text! block!]
][
    if not tag? flag [return flag] ;-- no filtering

    parse2/match to text! flag [
        copy header: to ":"
        ":" copy option: to end
    ] else [
        panic ["Tag must be <prefix:flag> ->" (flag)]
    ]

    return all [
        find (any [match block! prefixes reduce [prefixes]]) header
        option
    ]
]

run-command: function [
    return: [text!]
    cmd [block! text!]
][
    x: copy ""
    call/shell/output cmd x
    return trim/with x "^/^M"
]

platform-class: make object! [
    name: null
    exe-suffix: null
    dll-suffix: null
    archive-suffix: null  ; static library
    obj-suffix: null

    gen-cmd-create: null
    gen-cmd-delete: null
    gen-cmd-strip: null
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
        return either dir? cmd/file [
            spaced ["mkdir -p" cmd/file]
        ][
            spaced ["touch" cmd/file]
        ]
    ]

    gen-cmd-delete: method [
        return: [text!]
        cmd [object!]
    ][
        return spaced ["rm -fr" cmd/file]
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
        return either dir? cmd/file [
            spaced ["if not exist" d "mkdir" d]
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
        return either dir? cmd/file [
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
    return: [~]
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
        default [
            print ["Unknown platform:" platform "falling back to POSIX"]
            target-platform: posix
        ]
    ]
]

project-class: make object! [
    class: #project
    name: null
    id: null
    type: null  ; dynamic, static, object or application
    depends: null  ; a dependency could be a library, object file
    output: null  ; file path
    basename: null  ; output without extension part
    generated?: null
    implib: null  ; for windows, an application/library with exported symbols will generate an implib file

    post-build-commands: null  ; commands to run after the "build" command

    compiler: null

    ; common settings applying to all included obj-files
    ; setting inheritage:
    ; they can only be inherited from project to obj-files
    ; _not_ from project to project.
    ; They will be applied _in addition_ to the obj-file level settings
    includes: null
    definitions: null
    cflags: null

    ; These can be inherited from project to obj-files and will be overwritten
    ; at the obj-file level
    optimization: null
    debug: null
]

solution-class: make project-class [
    class: #solution
]

ext-dynamic-class: make object! [
    class: #dynamic-extension
    output: null
    flags: null  ; static?
]

ext-static-class: make object! [
    class: #static-extension
    output: null
    flags: null  ; static?
]

application-class: make project-class [
    class: #application
    type: 'application
    generated?: null

    searches: null
    ldflags: null

    command: method [return: [text!]] [
        cc: any [compiler default-compiler]
        return cc/link
            output
            depends
            searches
            ldflags
    ]

]

dynamic-library-class: make project-class [
    class: #dynamic-library
    type: 'dynamic
    generated?: null

    searches: null
    ldflags: null

    command: method [
        return: [text!]
        <with>
        default-compiler
    ][
        cc: any [compiler default-compiler]
        return cc/link/dynamic
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
    name: null
    id: null  ; flag prefix
    version: null
    exec-file: null

    ;check if the compiler is available
    check: method [
        return: [~]
        exec [~null~ file! text!]
    ][
        panic "archetype check"
    ]

    compile: method [
        return: [~]
        output [file!]
        source [file!]
        include [file! block!]
        definition [any-string!]
        cflags [any-string!]
    ][
        panic "archetype compile"
    ]

    link: method [
        return: [text!]
        output [file!]
        depends [block! ~null~]
        searches [block! ~null~]
        ldflags [block! any-string! ~null~]
        /dynamic
    ][
        panic "archetype link"
    ]

    accept: method [
        return: [~null~ text!]
        dep [object!]
    ][
        panic "archetype link"
    ]
]

cc: make compiler-class [
    name: 'cc
    id: null

    check: method [
        return: [~]
        exec [~null~ file! text!]
        <static>
        digit (charset "0123456789")
    ][
        exec-file: any [
            exec
            exec-file
            to file! name
        ]

        id: default [[]]

        comment [
            version: copy ""
            sys/util/rescue [
                exec-file: path: default ["gcc"]
                call/output reduce [path "--version"] version
                degrade parse2/match version [
                    {gcc (GCC)} space
                    copy major: some digit #"."
                    copy minor: some digit #"."
                    copy macro: some digit
                    to end
                ] then [
                    version: reduce [ ;; !!!! It appears this is not used (?)
                        to integer! major
                        to integer! minor
                        to integer! macro
                    ]
                    '~okay~
                ] else [
                    '~null~
                ]
            ]
        ]
    ]

    compile: method [
        return: [text!]
        output [file!]
        source [file!]
        /I includes
        /D definitions
        /F cflags
        /O opt-level
        /g debug
        /PIC
        /E
    ][
        return collect-text [
            keep (file-to-local/pass exec-file else [
                to text! name ;; the "gcc" may get overridden as "g++"
            ])

            keep either E ["-E"]["-c"]

            if PIC [
                keep "-fPIC"
            ]
            if I [
                for-each inc (map-files-to-local includes) [
                    keep ["-I" inc]
                ]
            ]
            if D [
                for-each flg definitions [
                    ;
                    ; !!! For cases like `#include MBEDTLS_CONFIG_FILE` then
                    ; quotes are expected to work in defines...but when you
                    ; pass quotes on the command line it's different than
                    ; inside of a visual studio project (for instance) because
                    ; bash strips them out unless escaped with backslash.
                    ; This is a stopgap workaround that ultimately would
                    ; permit cross-platform {MBEDTLS_CONFIG_FILE="filename.h"}
                    ;
                    flg: replace copy flg {"} {\"}

                    keep ["-D" (filter-flag flg id else [continue])]
                ]
            ]
            if O [
                case [
                    opt-level = null [keep "-O0"]
                    opt-level = okay [keep "-O2"]
                    integer? opt-level [keep ["-O" opt-level]]
                    find ["s" "z" "g" 's 'z 'g] opt-level [
                        keep ["-O" opt-level]
                    ]

                    panic ["unrecognized optimization level:" opt-level]
                ]
            ]
            if g [
                case [
                    debug = null []
                    debug = okay [keep "-g -g3"]
                    integer? debug [keep ["-g" debug]]

                    panic ["unrecognized debug option:" debug]
                ]
            ]
            if F [
                for-each flg cflags [
                    keep opt filter-flag flg id
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

    link: method [
        return: [text!]
        output [file!]
        depends [block! ~null~]
        searches [block! ~null~]
        ldflags [block! any-string! ~null~]
        /debug
        /dynamic
    ][
        suffix: either dynamic [
            target-platform/dll-suffix
        ][
            target-platform/exe-suffix
        ]

        return collect-text [
            keep file-to-local/pass exec-file  ; gcc, g++, clang, tcc, etc...

            if debug [keep "-g"]

            if dynamic [keep "-shared"]

            keep "-o"

            output: file-to-local output
            either ends-with? output opt suffix [
                keep output
            ][
                keep [output opt suffix]
            ]

            for-each search (map-files-to-local searches) [
                keep ["-L" search]
            ]

            for-each flg ldflags [
                keep opt filter-flag flg id
            ]

            for-each dep depends [
                keep opt accept dep
            ]
        ]
    ]

    accept: method [
        return: [~null~ text!]
        dep [object!]
    ][
        return degrade switch dep/class [
            #object-file [
                comment [ ;-- !!! This was commented out, why?
                    if find words-of dep 'depends [
                        for-each ddep dep/depends [
                            dump ddep
                        ]
                    ]
                ]
                file-to-local dep/output
            ]
            #dynamic-extension [
                either tag? dep/output [
                    if lib: filter-flag dep/output id [
                        unspaced ["-l" lib]
                    ]
                    else [
                        reify null
                    ]
                ][
                    unspaced [
                        when find (any [dep/flags []]) 'static ["-static "]
                        "-l" dep/output
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
                '~null~
            ]
            #variable [
                '~null~
            ]
            #entry [
                '~null~
            ]
            default [
                dump dep
                panic "unrecognized dependency"
            ]
        ]
    ]
]

gcc: make cc [
    name: 'gcc
    id: ["gcc" "gnu"]
]

g++: make cc [
    name: 'g++
    id: ["gcc" "gnu"]
]

clang: make cc [
    name: 'clang
    id: ["gcc" "clang"]
]

clang++: make cc [
    name: 'clang++
    id: ["gcc" "clang"]
]


tcc: make cc [
    name: 'tcc
    id: "tcc"
]


; Microsoft CL compiler
cl: make compiler-class [
    name: 'cl
    id: "msc" ;flag id
    exec-file: %cl.exe

    check: method [
        return: [~]
        exec [~null~ file! text!]
    ][
        exec-file: any [exec exec-file]
    ]

    compile: method [
        return: [text!]
        output [file!]
        source
        /I includes
        /D definitions
        /F cflags
        /O opt-level
        /g debug
        /PIC {Ignored for cl}
        /E
    ][
        return collect-text [
            keep file-to-local/pass exec-file
            keep "/nologo" ; don't show startup banner
            keep either E ["/P"]["/c"]

            ; NMAKE is not multi-core, only CL.EXE is when you pass it more
            ; than one file at a time with /MP.  To get around this, you can
            ; use Qt's JOM which is a drop-in replacement for NMAKE that does
            ; parallel building.  But it requires /FS "force synchronous pdb"
            ; so that the multiple CL calls don't try and each lock the pdb.
            ;
            keep "/FS"

            if I [
                for-each inc (map-files-to-local includes) [
                    keep ["/I" inc]
                ]
            ]
            if D [
                for-each flg definitions [
                    ; !!! For cases like `#include MBEDTLS_CONFIG_FILE` then
                    ; quotes are expected to work in defines...but when you
                    ; pass quotes on the command line it's different than
                    ; inside of a visual studio project (for instance) because
                    ; bash strips them out unless escaped with backslash.
                    ; This is a stopgap workaround that ultimately would
                    ; permit cross-platform {MBEDTLS_CONFIG_FILE="filename.h"}
                    ;
                    flg: replace copy flg {"} {\"}

                    keep ["/D" (filter-flag flg id else [continue])]
                ]
            ]
            if O [
                case [
                    opt-level = okay [keep "/O2"]
                    all [
                        opt-level
                        not zero? opt-level
                    ][
                        keep ["/O" opt-level]
                    ]
                ]
            ]
            if g [
                case [
                    debug = null []
                    any [
                        debug = okay
                        integer? debug ;-- doesn't map to a CL option
                    ][
                        keep "/Od /Zi"
                    ]

                    panic ["unrecognized debug option:" debug]
                ]
            ]
            if F [
                for-each flg cflags [
                    keep opt filter-flag flg id
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

    link: method [
        return: [text!]
        output [file!]
        depends [block! ~null~]
        searches [block! ~null~]
        ldflags [block! any-string! ~null~]
        /debug
        /dynamic
    ][
        suffix: either dynamic [
            target-platform/dll-suffix
        ][
            target-platform/exe-suffix
        ]
        return collect-text [
            keep file-to-local/pass exec-file  ; cl.exe

            keep "/nologo"  ; link.exe takes uppercase, cl.exe lowercase!

            ; link.exe takes e.g. `/OUT:r3.exe`, the cl.exe takes `/Fer3`
            ;
            output: file-to-local output
            keep [
                "/Fe" either ends-with? output opt suffix [
                    output
                ][
                    unspaced [output opt suffix]
                ]
            ]

            for-each dep depends [
                keep opt accept dep
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

            for-each search (map-files-to-local searches) [
                keep ["/libpath:" search]
            ]

            for-each flg ldflags [
                keep opt filter-flag flg id
            ]

        ]
    ]

    accept: method [
        return: [~null~ text!]
        dep [object!]
    ][
        return degrade switch dep/class [
            #object-file [
                file-to-local to-file dep/output
            ]
            #dynamic-extension [
                comment [import file] ;-- static property is ignored

                either tag? dep/output [
                    reify filter-flag dep/output id
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
                '~null~
            ]
            #entry [
                '~null~
            ]
            default [
                dump dep
                panic "unrecognized dependency"
            ]
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
        /params flags [block! any-string!]
    ][
        return reduce [collect-text [
            keep ("strip" unless file-to-local/pass exec-file)
            flags: default [options]
            switch type of flags [
                block! [
                    for-each flag flags [
                        keep opt filter-flag flag id
                    ]
                ]
                text! [
                    keep flags
                ]
            ]
            keep file-to-local target
        ]]
    ]
]

strip: make strip-class [  ; options were [<gcc:-S> <gcc:-x> <gcc:-X>] ?
    id: "gnu"
]

; includes/definitions/cflags will be inherited from its immediately ancester
object-file-class: make object! [
    class: #object-file
    compiler: null
    cflags: null
    definitions: null
    source: null
    output: null
    basename: null  ; output without extension part
    optimization: null
    debug: null
    includes: null
    generated?: null
    depends: null

    compile: method [
        return: [text!]
        /I ex-includes
        /D ex-definitions
        /F ex-cflags
        /O opt-level
        /g dbg
        /PIC ;Position Independent Code
        /E {only preprocessing}
    ][
        cc: any [compiler default-compiler]
        return cc/compile/I/D/F/O/g/(opt+ all [PIC 'PIC])/(opt+ all [E 'E]) output source
            <- compose [(opt includes) (if I [ex-includes])]
            <- compose [(opt definitions) (if D [ex-definitions])]
            <- compose [(if F [ex-cflags]) (opt cflags)] ;; ex-cflags override

            ; current setting overwrites /refinement
            ; because the refinements are inherited from the parent
            opt either O [either optimization [optimization][opt-level]][optimization]
            opt either g [either debug [debug][dbg]][debug]
    ]

    gen-entries: method [
        return: [object!]
        parent [object!]
        /PIC
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
            depends: append-of either depends [depends][[]] source
            commands: reduce [compile/I/D/F/O/g/(
                opt+ all [any [PIC parent/class = #dynamic-library] 'PIC]
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
    id: null
    target: null
    depends: null
    commands: null
    generated?: null
]

var-class: make object! [
    class: #variable
    name: null
    value: null
    default: null
    generated?: null
]

cmd-create-class: make object! [
    class: #cmd-create
    file: null
]

cmd-delete-class: make object! [
    class: #cmd-delete
    file: null
]

cmd-strip-class: make object! [
    class: #cmd-strip
    file: null
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
        return switch cmd/class [
            #cmd-create [
                applique any [:gen-cmd-create :target-platform/gen-cmd-create] compose [cmd: (cmd)]
            ]
            #cmd-delete [
                applique any [:gen-cmd-delete :target-platform/gen-cmd-delete] compose [cmd: (cmd)]
            ]
            #cmd-strip [
                applique any [:gen-cmd-strip :target-platform/gen-cmd-strip] compose [cmd: (cmd)]
            ]

            panic ["Unknown cmd class:" cmd/class]
        ]
    ]

    substitute: method [
        {Substitute variables in the command with its value}
        {(will recursively substitute if the value has variables)}

        return: [~null~ object! any-string!]
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

        stop: null
        while [not stop][
            stop: okay
            parse2/match cmd [
                while [
                    change [
                        [
                            "$(" copy name: some [letter | digit | #"_"] ")"
                            | "$" copy name: letter
                        ] (
                            val: localize select vars name
                            stop: null
                        )
                    ] val
                    | skip
                ]
            ] else [
                panic ["failed to do var substitution:" cmd]
            ]
        ]
        return cmd
    ]

    prepare: method [
        return: [~]
        solution [object!]
    ][
        if find words-of solution 'output [
            setup-outputs solution
        ]
        flip-flag solution null

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
        return: [~]
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
        return: [~]
        project [object!]
    ][
        if not suffix: find reduce [
            #application (reify target-platform/exe-suffix)
            #dynamic-library (reify target-platform/dll-suffix)
            #static-library (reify target-platform/archive-suffix)
            #object-library (reify target-platform/archive-suffix)
            #object-file (reify target-platform/obj-suffix)
        ] project/class [return ~]

        suffix: degrade second suffix

        case [
            not project/output [
                switch project/class [
                    #object-file [
                        project/output: copy project/source
                    ]
                    #object-library [
                        project/output: to text! project/name
                    ]

                    panic ["Unexpected project class:" (project/class)]
                ]
                if output-ext: find/last project/output #"." [
                    remove output-ext
                ]
                basename: project/output
                project/output: join basename opt suffix
            ]
            ends-with? project/output opt suffix [
                basename: either suffix [
                    copy/part project/output
                        (length of project/output) - (length of suffix)
                ][
                    copy project/output
                ]
            ]
            default [
                basename: project/output
                project/output: join basename opt suffix
            ]
        ]

        project/basename: basename
    ]

    setup-outputs: method [
        {Set the output/implib for the project tree}
        return: [~]
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
                if project/generated? [return ~]
                setup-output project
                project/generated?: okay
                for-each dep project/depends [
                    setup-outputs dep
                ]
            ]
            #object-file [
                setup-output project
            ]
        ] else [
            noop  ; !!! non-exhaustive list?
        ]
    ]
]

makefile: make generator-class [
    nmake?: null ; Generating for Microsoft nmake

    ;by default makefiles are for POSIX platform
    gen-cmd-create: :posix/gen-cmd-create
    gen-cmd-delete: :posix/gen-cmd-delete
    gen-cmd-strip: :posix/gen-cmd-strip

    gen-rule: method [
        return: "Possibly multi-line text for rule, with extra newline @ end"
            [text!]
        entry [object!]
    ][
        return newlined collect-lines [switch entry/class [

            ;; Makefile variable, defined on a line by itself
            ;;
            #variable [
                keep either entry/default [
                    [entry/name either nmake? ["="]["?="] entry/default]
                ][
                    [entry/name "=" entry/value]
                ]
            ]

            #entry [
                ;;
                ;; First line in a makefile entry is the target followed by
                ;; a colon and a list of dependencies.  Usually the target is
                ;; a file path on disk, but it can also be a "phony" target
                ;; that is just a word:
                ;;
                ;; https://stackoverflow.com/q/2145590/
                ;;
                keep collect-text [
                    case [
                        word? entry/target [ ;; like "clean" in `make clean`
                            keep [entry/target ":"]
                            keep ".PHONY"
                        ]
                        file? entry/target [
                            keep [file-to-local entry/target ":"]
                        ]
                        panic ["Unknown entry/target type" entry/target]
                    ]
                    ensure [block! ~null~] entry/depends
                    for-each w opt entry/depends [
                        switch all [object? w w/class] [
                            #variable [
                                keep ["$(" w/name ")"]
                            ]
                            #entry [
                                keep w/target
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

                ;; After the line with its target and dependencies are the
                ;; lines of shell code that run to build the target.  These
                ;; may use escaped makefile variables that get substituted.
                ;;
                ensure [block! ~null~] entry/commands
                for-each cmd opt entry/commands [
                    c: ((match text! cmd) else [gen-cmd cmd]) else [continue]
                    if empty? c [continue] ;; !!! Review why this happens
                    keep [tab c] ;; makefiles demand TAB codepoint :-(
                ]
            ]

            panic ["Unrecognized entry class:" entry/class]
        ] keep ""] ;-- final keep just adds an extra newline

        ;; !!! Adding an extra newline here unconditionally means variables
        ;; in the makefile get spaced out, which isn't bad--but it wasn't done
        ;; in the original rebmake.r.  This could be rethought to leave it
        ;; to the caller to decide to add the spacing line or not
    ]

    emit: method [
        return: [~]
        buf [blob!]
        project [object!]
        /parent parent-object
    ][
        ;print ["emitting..."]
        ;dump project
        ;if project/generated? [return]
        ;project/generated?: okay

        for-each dep project/depends [
            if not object? dep [continue]
            ;dump dep
            if not find [#dynamic-extension #static-extension] dep/class [
                either dep/generated? [
                    continue
                ][
                    dep/generated?: okay
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
                        depends: append objs map-each ddep dep/depends [
                            when ddep/class <> #object-library [ddep]
                        ]
                        commands: append reduce [dep/command] opt dep/post-build-commands
                        assert [not find commands _]
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
                            obj/generated?: okay
                            append buf gen-rule obj/gen-entries/(opt+ all [
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
                    panic ["unrecognized project type:" dep/class]
                ]
            ]
        ]
    ]

    generate: method [
        return: [~]
        output [file!]
        solution [object!]
    ][
        buf: make blob! 2048
        assert [solution/class = #solution]

        prepare solution

        emit buf solution

        write output append buf "^/^/.PHONY:"
    ]
]

nmake: make makefile [
    nmake?: okay

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
        return: [~]
        target [object!]
        /cwd dir [file!]
    ][
        switch target/class [
            #variable [
                _ ;-- already been taken care of by PREPARE
            ]
            #entry [
                if all [
                    not word? target/target
                    ; so you can use words for "phony" targets
                    exists? to-file target/target
                ] [return] ;TODO: Check the timestamp to see if it needs to be updated
                either block? target/commands [
                    for-each cmd target/commands [
                        cmd: substitute cmd
                        print ["Running:" cmd]
                        call/shell cmd
                    ]
                ][
                    cmd: substitute target/commands
                    print ["Running:" cmd]
                    call/shell cmd
                ]
            ]
            default [
                dump target
                panic "Unrecognized target class"
            ]
        ]
    ]

    run: method [
        return: [~]
        project [object!]
        /parent p-project
    ][
        ;dump project
        if not object? project [return]

        prepare project

        if not find [#dynamic-extension #static-extension] project/class [
            if project/generated? [return]
            project/generated?: okay
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
                    depends: append copy project/depends objs
                    commands: reduce [project/command]
                    assert [not find commands _]
                ]
            ]
            #object-library [
                for-each obj project/depends [
                    assert [obj/class = #object-file]
                    if not obj/generated? [
                        obj/generated?: okay
                        run-target obj/gen-entries/(opt+ all [
                            p-project/class = #dynamic-library
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
                panic ["unrecognized project type:" project/class]
            ]
        ]
    ]
]

] ;-- end of `rebmake: make object!` workaround for lack of `Type: 'module`
