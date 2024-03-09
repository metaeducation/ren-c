REBOL [
    File: %rebmake.r
    Title: {Rebol-Based C/C++ Makefile and Project File Generator}

    ; !!! Making %rebmake.r a module means it gets its own copy of lib, which
    ; creates difficulties for the bootstrap shim technique.  Changing the
    ; semantics of lib (e.g. how something fundamental like IF or CASE would
    ; work) could break the mezzanine.  For the time being, just use DO to
    ; run it in user, as with other pieces of bootstrap.
    ;
    ;-- Type: 'module --

    Rights: {
        Copyright 2017 Atronix Engineering
        Copyright 2017-2018 Rebol Open Source Developers
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
    return: [~null~ text! file!]
    flag [tag! text! file!]
        {If TAG! then must be <prefix:flag>, e.g. <gnu:-Wno-unknown-warning>}
    prefix [text!]
        {gnu -> GCC-compatible compilers, msc -> Microsoft C}
][
    if not tag? flag [return flag] ;-- no filtering

    parse/match to text! flag [
        copy header: to ":"
        ":" copy option: to end
    ] else [
        fail ["Tag must be <prefix:flag> ->" (flag)]
    ]

    return all [prefix = header | to-text option]
]

run-command: function [
    cmd [block! text!]
][
    x: copy ""
    call/wait/shell/output cmd x
    trim/with x "^/^M"
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
            b: ensure block! tool/commands/params cmd/file maybe- cmd/options
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
        either dir? cmd/file [
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

    link: method [return: [~]] [
        linker/link output depends ldflags
    ]

    command: method [return: [text!]] [
        ld: linker or [default-linker]
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
    link: method [return: [~]] [
        linker/link output depends ldflags
    ]

    command: method [
        return: [text!]
        <with>
        default-linker
    ][
        l: linker or [default-linker]
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
        return: [~]
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
        path [<maybe> any-string!]
    ][
    ]
]

gcc: make compiler-class [
    name: 'gcc
    id: "gnu"
    check: method [
        return: [logic!]
        /exec path [file!]
        <static>
        digit (charset "0123456789")
    ][
        version: copy ""
        attempt [
            exec-file: path: default ["gcc"]
            call/output reduce [path "--version"] version
            parse/match version [
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
        /I includes
        /D definitions
        /F cflags
        /O opt-level
        /g debug
        /PIC
        /E
    ][
        collect-text [
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
                    if find [gcc g++ cl] name [
                        flg: replace/all copy flg {"} {\"}
                    ]

                    keep ["-D" (filter-flag flg id else [continue])]
                ]
            ]
            if O [
                case [
                    opt-level = true [keep "-O2"]
                    opt-level = false [keep "-O0"]
                    integer? opt-level [keep ["-O" opt-level]]
                    find ["s" "z" "g" 's 'z 'g] opt-level [
                        keep ["-O" opt-level]
                    ]

                    fail ["unrecognized optimization level:" opt-level]
                ]
            ]
            if g [
                case [
                    debug = true [keep "-g -g3"]
                    debug = false []
                    integer? debug [keep ["-g" debug]]

                    fail ["unrecognized debug option:" debug]
                ]
            ]
            if F [
                for-each flg cflags [
                    keep maybe- filter-flag flg id
                ]
            ]

            keep "-o"

            output: file-to-local output

            if (E or [ends-with? output target-platform/obj-suffix]) [
                keep output
            ] else [
                keep [output target-platform/obj-suffix]
            ]

            keep file-to-local source
        ]
    ]
]

tcc: make compiler-class [
    name: 'tcc
    id: "tcc"

    ;; Note: For the initial implementation of user natives, TCC has to be run
    ;; as a preprocessor for %sys-core.h, to expand its complicated inclusions
    ;; into a single file which could be embedded into the executable.  The
    ;; new plan is to only allow "rebol.h" in user natives, which would mean
    ;; that TCC would not need to be run during the make process.  However,
    ;; for the moment TCC is run to do this preprocessing even when it is not
    ;; the compiler being used for the actual build of the interpreter.
    ;;
    command: method [
        return: [text!]
        output [file!]
        source [file!]
        /E {Preprocess}
        /I includes
        /D definitions
        /F cflags
        /O opt-level
        /g debug
        /PIC
    ][
        collect-text [
            keep ("tcc" unless file-to-local/pass exec-file)
            keep either E ["-E"]["-c"]

            if PIC [keep "-fPIC"]
            if I [
                for-each inc (map-files-to-local includes) [
                    keep ["-I" inc]
                ]
            ]
            if D [
                for-each flg definitions [
                    keep ["-D" (filter-flag flg id else [continue])]
                ]
            ]
            if O [
                case [
                    opt-level = true [keep "-O2"]
                    opt-level = false [keep "-O0"]
                    integer? opt-level [keep ["-O" opt-level]]

                    fail ["unknown optimization level" opt-level]
                ]
            ]
            if g [
                case [
                    debug = true [keep "-g"]
                    debug = false []
                    integer? debug [keep ["-g" debug]]

                    fail ["unrecognized debug option:" debug]
                ]
            ]
            if F [
                for-each flg cflags [
                    keep maybe- filter-flag flg id
                ]
            ]

            keep "-o"

            output: file-to-local output

            if (E or [ends-with? output target-platform/obj-suffix]) [
                keep output
            ] else [
                keep [output target-platform/obj-suffix]
            ]

            keep file-to-local source
        ]
    ]
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
        /I includes
        /D definitions
        /F cflags
        /O opt-level
        /g debug
        /PIC {Ignored for cl}
        /E
    ][
        collect-text [
            keep ("cl" unless file-to-local/pass exec-file)
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
                    flg: replace/all copy flg {"} {\"}

                    keep ["/D" (filter-flag flg id else [continue])]
                ]
            ]
            if O [
                case [
                    opt-level = true [keep "/O2"]
                    opt-level and [not zero? opt-level] [
                        keep ["/O" opt-level]
                    ]
                ]
            ]
            if g [
                case [
                    any [
                        debug = true
                        integer? debug ;-- doesn't map to a CL option
                    ][
                        keep "/Od /Zi"
                    ]
                    debug = false []

                    fail ["unrecognized debug option:" debug]
                ]
            ]
            if F [
                for-each flg cflags [
                    keep maybe- filter-flag flg id
                ]
            ]

            output: file-to-local output
            keep unspaced [
                either E ["/Fi"]["/Fo"]
                if (E or [ends-with? output target-platform/obj-suffix]) [
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
        return: [~]
    ]
    commands: method [
        return: [~null~ block!]
        output [file!]
        depends [block! blank!]
        searches [block! blank!]
        ldflags [block! any-string! blank!]
    ][
        ... ;-- overridden
    ]
    check: does [
        ... ;-- overridden
    ]
]

ld: make linker-class [
    ;;
    ;; Note that `gcc` is used as the ld executable by default.  There are
    ;; some switches (such as -m32) which it seems `ld` does not recognize,
    ;; even when processing a similar looking link line.
    ;;
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

            if dynamic [keep "-shared"]

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
                keep maybe- filter-flag flg id
            ]

            for-each dep depends [
                keep accept dep
            ]
        ]
    ]

    accept: method [
        return: [~null~ text!]
        dep [object!]
    ][
        degrade switch dep/class [
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
                ][
                    unspaced [
                        if find dep/flags 'static ["-static "]
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
                fail "unrecognized dependency"
            ]
        ]
    ]

    check: method [
        return: [logic!]
        /exec path [file!]
    ][
        version: copy ""
        ;attempt [
            exec-file: path: default ["gcc"]
            call/output reduce [path "--version"] version
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
                keep maybe- filter-flag flg id
            ]

            for-each dep depends [
                keep accept dep
            ]
        ]
    ]

    accept: method [
        return: [~null~ text!]
        dep [object!]
    ][
        degrade switch dep/class [
            #object-file [
                file-to-local dep/output
            ]
            #dynamic-extension [
                '~null~
            ]
            #static-extension [
                '~null~
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
                keep maybe- filter-flag flg id
            ]

            for-each dep depends [
                keep maybe- accept dep
            ]
        ]
    ]

    accept: method [
        return: [~null~ text!]
        dep [object!]
    ][
        degrade switch dep/class [
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
        /params flags [block! any-string!]
    ][
        reduce [collect-text [
            keep ("strip" unless file-to-local/pass exec-file)
            flags: default [options]
            switch type of flags [
                block! [
                    for-each flag flags [
                        keep maybe- filter-flag flag id
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

    compile: method [return: [~]] [
        compiler/compile
    ]

    command: method [
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
        cc/command/I/D/F/O/g/(maybe+ PIC)/(maybe+ E) output source
            <- compose [(maybe- includes) (if I [ex-includes])]
            <- compose [(maybe- definitions) (if D [ex-definitions])]
            <- compose [(if F [ex-cflags]) (maybe- cflags)] ;; ex-cflags override

            ; current setting overwrites /refinement
            ; because the refinements are inherited from the parent
            maybe- either O [either optimization [optimization][opt-level]][optimization]
            maybe- either g [either debug [debug][dbg]][debug]
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

        make entry-class [
            target: output
            depends: append-of either depends [depends][[]] source
            commands: reduce [command/I/D/F/O/g/(
                maybe+ all [PIC or [parent/class = #dynamic-library] 'PIC]
            )
                maybe- parent/includes
                maybe- parent/definitions
                maybe- parent/cflags
                maybe- parent/optimization
                maybe- parent/debug
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
                applique any [:gen-cmd-create :target-platform/gen-cmd-create] compose [cmd: (cmd)]
            ]
            #cmd-delete [
                applique any [:gen-cmd-delete :target-platform/gen-cmd-delete] compose [cmd: (cmd)]
            ]
            #cmd-strip [
                applique any [:gen-cmd-strip :target-platform/gen-cmd-strip] compose [cmd: (cmd)]
            ]

            fail ["Unknown cmd class:" cmd/class]
        ]
    ]

    reify: method [
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

        stop: false
        while [not stop][
            stop: true
            parse/match cmd [
                while [
                    change [
                        [
                            "$(" copy name: some [letter | digit | #"_"] ")"
                            | "$" copy name: letter
                        ] (val: localize select vars name | stop: false)
                    ] val
                    | skip
                ]
            ] else [
                fail ["failed to do var substitution:" cmd]
            ]
        ]
        cmd
    ]

    prepare: method [
        return: [~]
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
                if output-ext: find/last project/output #"." [
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
                        fail ["Unknown entry/target type" entry/target]
                    ]
                    ensure [block! blank!] entry/depends
                    for-each w entry/depends [
                        switch w/class [
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
                ensure [block! blank!] entry/commands
                for-each cmd entry/commands [
                    c: ((match text! cmd) else [gen-cmd cmd]) else [continue]
                    if empty? c [continue] ;; !!! Review why this happens
                    keep [tab c] ;; makefiles demand TAB codepoint :-(
                ]
            ]

            fail ["Unrecognized entry class:" entry/class]
        ] keep ""] ;-- final keep just adds an extra newline

        ;; !!! Adding an extra newline here unconditionally means variables
        ;; in the makefile get spaced out, which isn't bad--but it wasn't done
        ;; in the original rebmake.r.  This could be rethought to leave it
        ;; to the caller to decide to add the spacing line or not
    ]

    emit: method [
        return: [~]
        buf [binary!]
        project [object!]
        /parent parent-object
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
                        depends: append objs map-each ddep dep/depends [
                            if ddep/class <> #object-library [ddep]
                        ]
                        commands: append reduce [dep/command] maybe- dep/post-build-commands
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
                            append buf gen-rule obj/gen-entries/(maybe+ all [
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
        return: [~]
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
                        cmd: reify cmd
                        print ["Running:" cmd]
                        call/wait/shell cmd
                    ]
                ][
                    cmd: reify target/commands
                    print ["Running:" cmd]
                    call/wait/shell cmd
                ]
            ]
            default [
                dump target
                fail "Unrecognized target class"
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
                        run-target obj/gen-entries/(maybe+ all [
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
                fail ["unrecognized project type:" project/class]
            ]
        ]
    ]
]

] ;-- end of `rebmake: make object!` workaround for lack of `Type: 'module`
