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
        generate project files for Microsoft Visual Studio, makefiles for
        GNU Make or Microsoft's Nmake, or just carry out a full build by
        invoking compiler processes and command lines itself.

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

map-files-to-local: func [
    return: [block!]
    files [file! block!]
    <local>
    f
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
    if not tag? flag [return flag] ;-- no filtering

    parse to text! flag [
        copy header: to ":"
        ":" copy option: to end
    ] or [
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

    gen-cmd-create:
    gen-cmd-delete:
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
            return tool/commands/params cmd/file opt cmd/options
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
        -- "Note: STRIP command not implemented for MSVC"
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
    class-name: 'project-class
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
    class-name: 'solution-class
]

ext-dynamic-class: make object! [
    class-name: 'ext-dynamic-class
    output: _
    flags: _ ;static?
]

ext-static-class: make object! [
    class-name: 'ext-static-class
    output: _
    flags: _ ;static?
]

application-class: make project-class [
    class-name: 'application-class
    type: 'application
    generated?: false

    linker: _
    searches: _
    ldflags: _

    link: meth [][
        linker/link output depends ldflags
    ]

    command: meth [
        <local>
        ld
    ][
        ld: any [linker default-linker]
        ld/command
            output
            depends
            searches
            ldflags
    ]

]

dynamic-library-class: make project-class [
    class-name: 'dynamic-library-class
    type: 'dynamic
    generated?: false
    linker: _

    searches: _
    ldflags: _
    link: meth [][
        linker/link output depends ldflags
    ]

    command: method [
        <with>
        default-linker
    ][
        l: any [linker default-linker]
        l/command/dynamic
            output
            depends
            searches
            ldflags
    ]
]

object-library-class: make project-class [
    class-name: 'object-library-class
    type: 'object
]

compiler-class: make object! [
    class-name: 'compiler-class
    name: _
    id: _ ;flag prefix
    version: _
    exec-file: _
    compile: meth [
        output [file!]
        source [file!]
        include [file! block!]
        definition [any-string!]
        cflags [any-string!]
    ][
    ]

    command: meth [
        output
        source
        includes
        definitions
        cflags
    ][
    ]
    ;check if the compiler is available
    check: method [path [any-string! blank!]] []
]

gcc: make compiler-class [
    name: 'gcc
    id: "gnu"
    check: method [
        /exec path [file! blank!]
        <local>
        w
        <with>
        version
        exec-file
        <static>
        digit (charset "0123456789")
    ][
        version: copy ""
        attempt [
            call/output reduce [exec-file: any [all [exec path] "gcc"] "--version"] version
            parse version [
                {gcc (GCC) } 
                copy major: some digit #"."
                copy minor: some digit #"."
                copy macro: some digit
                to end
            ] then [
                version: reduce [
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
        output [file! text!]
        source [file! text!]
        /I includes
        /D definitions
        /F cflags
        /O opt-level
        /g debug
        /PIC
        /E
        <local>
        flg
        fs
    ][
        if file? output [output: file-to-local output]
        if file? source [source: file-to-local source]
        spaced [
            case [
                file? exec-file [file-to-local exec-file]
                exec-file [exec-file]
                default [to text! name]
            ]
            either E ["-E"]["-c"]

            if PIC ["-fPIC"]
            if I [
                spaced [
                    map-each inc (map-files-to-local includes) [
                        unspaced ["-I" inc]
                    ]
                ]
            ]
            if D [
                spaced [
                    map-each flg definitions [
                        if flg: filter-flag flg id [unspaced ["-D" flg]]
                    ]
                ]
            ]
            if O [
                case [
                    opt-level = true ["-O2"]
                    opt-level = false ["-O0"]
                    integer? opt-level [unspaced ["-O" opt-level]]
                    find ["s" "z" "g" 's 'z 'g] opt-level [
                        unspaced ["-O" opt-level]
                    ]

                    fail ["unrecognized optimization level:" opt-level]
                ]
            ]
            opt if g [ ;-- "" doesn't vaporize in old Ren-C, _ doesn't in new
                case [
                    blank? debug [] ;FIXME: _ should be passed in at all
                    debug = true ["-g -g3"]
                    debug = false []
                    integer? debug [unspaced ["-g" debug]]

                    fail ["unrecognized debug option:" debug]
                ]
            ]
            if all [F block? cflags][
                spaced map-each flg cflags [
                    filter-flag flg id
                ]
            ]

            "-o" case [
                E [output]
                ends-with? output target-platform/obj-suffix [output]
                default [unspaced [output target-platform/obj-suffix]]
            ]

            source
        ]
    ]
]

tcc: make compiler-class [
    name: 'tcc
    id: "tcc"

    command: meth [
        output
        source
        /E {Preprocess}
        /I includes
        /D definitions
        /F cflags
        /O opt-level
        /g debug
        /PIC
        <local>
        flg
        fs
    ][
        spaced [
            any [exec-file "tcc"]
            either E ["-E"]["-c"]

            if PIC ["-fPIC"]
            if I [
                spaced [
                    map-each inc (map-files-to-local includes) [
                        unspaced ["-I" inc]
                    ]
                ]
            ]
            if D [
                spaced [
                    map-each flg definitions [
                        if flg: filter-flag flg id [unspaced ["-D" flg]]
                    ]
                ]
            ]
            if O [
                case [
                    opt-level = true ["-O2"]
                    opt-level = false ["-O0"]
                    integer? opt-level [unspaced ["-O" opt-level]]

                    fail ["unknown optimization level" opt-level]
                ]
            ]
            opt if g [ ;-- "" doesn't vaporize in old Ren-C, _ doesn't in new
                case [
                    blank? debug [] ;FIXME: _ should be passed in at all
                    debug = true ["-g"]
                    debug = false []
                    integer? debug [unspaced ["-g" debug]]

                    fail ["unrecognized debug option:" debug]
                ]
            ]
            if all [F block? cflags][
                spaced map-each flg cflags [
                    filter-flag flg id
                ]
            ]

            "-o" case [
                E [output]
                ends-with? output target-platform/obj-suffix [output]
                default [unspaced [output target-platform/obj-suffix]]
            ]

            source
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
    command: meth [
        output [file! text!]
        source
        /I includes
        /D definitions
        /F cflags
        /O opt-level
        /g debug
        /PIC {Ignored for cl}
        /E
        <local>
        flg
        fs
    ][
        spaced [
            case [
                file? exec-file [file-to-local exec-file]
                exec-file [exec-file]
                default [{cl}]
            ]
            "/nologo" ; don't show startup banner
            either E ["/P"]["/c"]

            if I [
                spaced map-each inc (map-files-to-local includes) [
                    unspaced ["/I" inc]
                ]
            ]
            if D [
                spaced map-each flg definitions [
                    if flg: filter-flag flg id [unspaced ["/D" flg]]
                ]
            ]
            if O [
                case [
                    opt-level = true ["/O2"]
                    all [
                        opt-level
                        not zero? opt-level
                    ][
                        unspaced ["/O" opt-level]
                    ]
                ]
            ]
            opt if g [ ;-- "" doesn't vaporize in old Ren-C, _ doesn't in new 
                ;print mold debug
                case [
                    blank? debug [] ;FIXME: _ shouldn't be passed in at all
                    any [
                        debug = true
                        integer? debug ;-- doesn't map to a CL option
                    ][
                        "/Od /Zi"
                    ]
                    debug = false []
                    
                    fail ["unrecognized debug option:" debug]
                ]
            ]
            if all [F block? cflags][
                spaced map-each flg cflags [
                    filter-flag flg id
                ]
            ]

            unspaced [
                either E ["/Fi"]["/Fo"]
                case [
                    E [output]
                    ends-with? output target-platform/obj-suffix [output]
                    default [
                        unspaced [output target-platform/obj-suffix]
                    ]
                ]
            ]

            either file? source [file-to-local source][source]
        ]
    ]
]

linker-class: make object! [
    class-name: 'linker-class
    name: _
    id: _ ;flag prefix
    version: _
    link: meth [
    ][
    ]
    commands: meth [
        output [file!]
        depends [block! blank!]
        searches [block! blank!]
        ldflags [block! any-string! blank!]
    ][
    ]

    check: does []
]

ld: make linker-class [
    name: 'ld
    version: _
    exec-file: _
    id: "gnu"
    command: meth [
        output [file!]
        depends [block! blank!]
        searches [block! blank!]
        ldflags [block! any-string! blank!]
        /dynamic
        <local>
        dep
        suffix
    ][
        suffix: either dynamic [
            target-platform/dll-suffix
        ][
            target-platform/exe-suffix
        ]
        spaced [
            case [
                file? exec-file [file-to-local exec-file]
                exec-file [exec-file]
                default [{gcc}]
            ]
            
            if dynamic ["-shared"]
            
            "-o" file-to-local either ends-with? output suffix [
                output
            ][
                unspaced [output suffix]
            ]

            if block? searches [
                spaced [
                    map-each search (map-files-to-local searches) [
                        unspaced ["-L" search]
                    ]
                ]
            ]

            if block? ldflags [
                spaced map-each flg ldflags [
                    filter-flag flg id
                ]
            ]

            if block? depends [
                spaced map-each dep depends [accept dep]
            ]
        ]
    ]

    accept: meth [
        return: [<opt> text!]
        dep [object!]
        <local>
        ddep
        lib
    ][
        opt switch dep/class-name [
            'object-file-class [
                comment [ ;-- !!! This was commented out, why?
                    if find words-of dep 'depends [
                        for-each ddep dep/depends [
                            dump ddep
                        ]
                    ]
                ]
                file-to-local dep/output
            ]
            'ext-dynamic-class [
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
            'ext-static-class [
                file-to-local dep/output
            ]
            'object-library-class [
                spaced map-each ddep dep/depends [
                    file-to-local ddep/output
                ]
            ]
            'application-class [
                ;pass
            ]
            'var-class [
                ;pass
            ]
            'entry-class [
                ;pass
            ]

            default [
                dump dep
                fail "unrecognized dependency"
            ]
        ]
    ]

    check: meth [
        /exec path [file! blank!]
    ][
        version: copy ""
        ;attempt [
            path: either exec [path]["gcc"]
            call/output reduce [path "--version"] version
            exec-file: path
        ;]
    ]
]

llvm-link: make linker-class [
    name: 'llvm-link
    version: _
    exec-file: _
    id: "llvm"
    command: meth [
        output [file!]
        depends [block! blank!]
        searches [block! blank!]
        ldflags [block! any-string! blank!]
        /dynamic
        <local>
        dep
        suffix
    ][
        suffix: either dynamic [
            target-platform/dll-suffix
        ][
            target-platform/exe-suffix
        ]
        spaced [
            case [
                file? exec-file [file-to-local exec-file]
                exec-file [exec-file]
                default [{llvm-link}]
            ]
            "-o" file-to-local either ends-with? output suffix [
                output
            ][
                unspaced [output suffix]
            ]

            ; llvm-link doesn't seem to deal with libraries
            comment [
                if block? searches [
                    spaced [
                        map-each search (map-files-to-local searches) [
                            unspaced ["-L" search]
                        ]
                    ]
                ]
            ]

            if block? ldflags [
                spaced map-each flg ldflags [
                    filter-flag flg id
                ]
            ]

            if block? depends [
                spaced map-each dep depends [accept dep]
            ]
        ]
    ]

    accept: meth [
        return: [<opt> text!]
        dep [object!]
        <local>
        ddep
        lib
    ][
        opt switch dep/class-name [
            'object-file-class [
                comment [ ;-- !!! This was commented out, why?
                    if find words-of dep 'depends [
                        for-each ddep dep/depends [
                            dump ddep
                        ]
                    ]
                ]
                file-to-local dep/output
            ]
            'ext-dynamic-class [
                ;ignored
            ]
            'ext-static-class [
                ;ignored
            ]
            'object-library-class [
                spaced map-each ddep dep/depends [
                    file-to-local ddep/output
                ]
            ]
            'application-class [
                ;pass
            ]
            'var-class [
                ;pass
            ]
            'entry-class [
                ;pass
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
    command: meth [
        output [file!]
        depends [block! blank!]
        searches [block! blank!]
        ldflags [block! any-string! blank!]
        /dynamic
        <local>
        dep
        suffix
    ][
        suffix: either dynamic [
            target-platform/dll-suffix
        ][
            target-platform/exe-suffix
        ]
        spaced [
            case [
                file? exec-file [file-to-local exec-file]
                exec-file [exec-file]
                default [{link}]
            ]
            "/NOLOGO"
            if dynamic ["/DLL"]
            unspaced [
                "/OUT:" file-to-local either ends-with? output suffix [
                    output
                ][
                    unspaced [output suffix]
                ]
            ]

            if block? searches [
                spaced [
                    map-each search (map-files-to-local searches) [
                        unspaced ["/LIBPATH:" search]
                    ]
                ]
            ]

            if block? ldflags [
                spaced map-each flg ldflags [
                    filter-flag flg id
                ]
            ]

            if block? depends [
                spaced map-each dep depends [opt accept dep]
            ]
        ]
    ]

    accept: meth [
        return: [<opt> text! file!]
        dep [object!]
        <local>
        ddep
    ][
        opt switch dep/class-name [
            'object-file-class [
                comment [ ;-- !!! This was commented out, why?
                    if find words-of dep 'depends [
                        for-each ddep dep/depends [
                            dump ddep
                        ]
                    ]
                ]
                file-to-local to-file dep/output
            ]
            'ext-dynamic-class [
                comment [import file] ;-- static property is ignored

                either tag? dep/output [
                    filter-flag dep/output id
                ][
                    ;dump dep/output
                    either ends-with? dep/output ".lib" [
                        dep/output
                    ][
                        join-of dep/output ".lib"
                    ]
                ]
            ]
            'ext-static-class [
                file-to-local dep/output
            ]
            'object-library-class [
                spaced map-each ddep dep/depends [
                    file-to-local to-file ddep/output
                ]
            ]
            'application-class [
                file-to-local any [dep/implib join-of dep/basename ".lib"]
            ]
            'var-class [
                ;pass
            ]
            'entry-class [
                ;pass
            ]
            default [
                dump dep
                fail "unrecognized dependency"
            ]
        ]
    ]
]

strip-class: make object! [
    class-name: 'linker-class
    name: _
    id: _ ;flag prefix
    exec-file: _
    options: _
    commands: method [
        target [file!]
        /params flags [block! any-string! blank!]
        <local>
        flag
    ][
        spaced [
            case [
                file? exec-file [file-to-local exec-file]
                exec-file [exec-file]
                default [ {strip} ]
            ]
            if flags: any [
                all [params flags]
                options
            ][
                switch type of flags [
                    block! [
                        spaced map-each flag flags [
                            filter-flag flag id
                        ]
                    ]
                    text! [
                        flags
                    ]
                ]
            ]
            file-to-local target
        ]
    ]
]

strip: make strip-class [
    id: "gnu"
]

; includes/definitions/cflags will be inherited from its immediately ancester
object-file-class: make object! [
    class-name: 'object-file-class
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

    compile: meth [][
        compiler/compile
    ]

    command: meth [
        /I ex-includes
        /D ex-definitions
        /F ex-cflags
        /O opt-level
        /g dbg
        /PIC ;Position Independent Code
        /E {only preprocessing}
        <local> cc
    ][
        cc: any [compiler default-compiler]
        cc/command/I/D/F/O/g/(try all [PIC 'PIC])/(try all [E 'E]) output source
            case [
                all [I includes][join-of includes ex-includes]
                I [ex-includes]
                default [includes]
            ]
            case [
                all [D definitions][join-of definitions ex-definitions]
                D [ex-definitions]
                default [definitions]
            ]
            case [
                ;putting cflags after ex-cflags so that it has a chance to overwrite
                all [F cflags][join-of ex-cflags cflags]
                F [ex-cflags]
                default [cflags]
            ]

            ; current setting overwrites /refinement
            ; because the refinements are inherited from the parent
            opt either O [either optimization [optimization][opt-level]][optimization]
            opt either g [either debug [debug][dbg]][debug]
    ]

    gen-entries: meth [
        parent [object!]
        /PIC
        <local>
        args
    ][
        assert [
            find [
                application-class
                dynamic-library-class
                static-library-class
                object-library-class
            ] parent/class-name
        ]

        make entry-class [
            target: output
            depends: append-of either depends [depends][[]] source
            commands: command/I/D/F/O/g/(try all [
                any [
                    PIC
                    parent/class-name = 'dynamic-library-class
                ]
                'PIC
            ])
                opt parent/includes
                opt parent/definitions
                opt parent/cflags
                opt parent/optimization
                opt parent/debug
        ]
    ]
]

entry-class: make object! [
    class-name: 'entry-class
    id: _
    target:
    depends:
    commands: _
    generated?: false
]

var-class: make object! [
    class-name: 'var-class
    name:
    value: _
    default: _
    generated?: false
]

cmd-create-class: make object! [
    class-name: 'cmd-create-class
    file: _
]

cmd-delete-class: make object! [
    class-name: 'cmd-delete-class
    file: _
]

cmd-strip-class: make object! [
    class-name: 'cmd-strip-class
    file: _
    options: _
    strip: _
]

generator-class: make object! [
    class-name: 'generator-class

    vars: make map! 128

    gen-cmd-create:
    gen-cmd-delete:
    gen-cmd-strip: _

    gen-cmd: meth [
        cmd [object!]
    ][
        switch cmd/class-name [
            'cmd-create-class [
                apply any [:gen-cmd-create :target-platform/gen-cmd-create] compose [cmd: (cmd)]
            ]
            'cmd-delete-class [
                apply any [:gen-cmd-delete :target-platform/gen-cmd-delete] compose [cmd: (cmd)]
            ]
            'cmd-strip-class [
                apply any [:gen-cmd-strip :target-platform/gen-cmd-strip] compose [cmd: (cmd)]
            ]

            fail ["Unknown cmd class:" cmd/class-name]
        ]
    ]

    reify: method [
        "Substitute variables in the command with its value"
        "will recursively substitue if the value has variables"

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
                    cmd-create-class cmd-delete-class cmd-strip-class
                ] cmd/class-name
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
            ] or [
                fail ["failed to do var substitution:" cmd]
            ]
        ]
        cmd
    ]

    prepare: method [
        return: <void>
        solution [object!]
        <local>
        dep
    ][
        if find words-of solution 'output [
            setup-outputs solution
        ]
        flip-flag solution false

        if find words-of solution 'depends [
            for-each dep solution/depends [
                if dep/class-name = 'var-class [
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
        return: <void>
        project [object!]
        to [logic!]
        <local>
        dep
    ][
        if all [
            find words-of project 'generated?
            to != project/generated?
        ][
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
            'application-class target-platform/exe-suffix
            'dynamic-library-class target-platform/dll-suffix
            'static-library-class target-platform/archive-suffix
            'object-library-class target-platform/archive-suffix
            'object-file-class target-platform/obj-suffix
        ] project/class-name [return]

        suffix: second suffix

        case [
            blank? project/output [
                switch project/class-name [
                    'object-file-class [
                        project/output: copy project/source
                    ]
                    'object-library-class [
                        project/output: to text! project/name
                    ]

                    fail ["Unexpected project class:" (project/class-name)]
                ]
                if output-ext: find/last project/output #"." [
                    remove output-ext
                ]
                basename: project/output
                project/output: join-of basename suffix
            ]
            ends-with? project/output suffix [
                basename: either suffix [
                    copy/part project/output
                        (length of project/output) - (length of suffix)
                ][
                    copy project/output
                ]
            ]
            true [
                basename: project/output
                project/output: join-of basename suffix
            ]
        ]

        project/basename: basename
    ]

    setup-outputs: method [
        {Set the output/implib for the project tree}
        return: <void>
        project [object!]
        <local>
        dep
    ][
        ;print ["Setting outputs for:"]
        ;dump project
        switch project/class-name [
            'application-class
            'dynamic-library-class
            'static-library-class
            'solution-class
            'object-library-class [
                if project/generated? [return]
                setup-output project
                project/generated?: true
                for-each dep project/depends [
                    setup-outputs dep
                ]
            ]

            'object-file-class [
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

    gen-rule: meth [
        entry [object!]
        <local>
        w
        cmd
    ][
        switch entry/class-name [
            'var-class [
                unspaced [
                    entry/name either entry/default [
                        unspaced [either nmake? ["="]["?="] entry/default]
                    ][
                        unspaced ["=" entry/value]
                    ]
                    newline
                ]
            ]
            'entry-class [
                unspaced [
                    either file? entry/target [
                        file-to-local entry/target
                    ][
                        entry/target
                    ]
                    either word? entry/target [": .PHONY"] [":"]
                    space opt case [
                        block? entry/depends [
                            spaced map-each w entry/depends [
                                opt switch w/class-name [
                                    'var-class [
                                        unspaced ["$(" w/name ")"]
                                    ]
                                    'entry-class [
                                        w/target
                                    ]
                                    'ext-dynamic-class 'ext-static-class [
                                        ;only contribute to the command line
                                    ]

                                    default [
                                        case [
                                            file? w [file-to-local w]
                                            file? w/output [
                                                file-to-local w/output
                                            ]
                                            default [
                                                w/output
                                            ]
                                        ]
                                    ]
                                ]
                            ]
                        ]
                        any-string? entry/depends [
                            entry/depends
                        ]
                        blank? entry/depends [
                        ]
                        default [
                            fail ["unrecognized depends for" entry entry/depends]
                        ]
                    ]
                    newline
                    if all [
                        entry/commands
                        not empty? entry/commands
                    ][
                        unspaced [
                            "^-"
                            either block? entry/commands [
                                delimit map-each cmd (map-each cmd entry/commands [
                                    either text? cmd [
                                        cmd
                                    ][
                                        gen-cmd cmd
                                    ]
                                ]) [if not empty? cmd [cmd]] "^/^-"
                            ][
                                either text? entry/commands [
                                    entry/commands
                                ][
                                    gen-cmd entry/commands
                                ]
                            ]
                            newline
                        ]
                    ]
                    newline
                ]
            ]

            fail ["Unrecognized entry class:" entry/class-name]
        ]
    ]

    emit: meth [
        return: <void>
        buf [binary!]
        project [object!]
        /parent parent-object
        <local>
        dep
        obj
        objs
        suffix
    ][
        ;print ["emitting..."]
        ;dump project
        ;if project/generated? [return]
        ;project/generated?: true

        for-each dep project/depends [
            if not object? dep [continue]
            ;dump dep
            if not find [ext-dynamic-class ext-static-class] dep/class-name [
                either dep/generated? [
                    continue
                ][
                    dep/generated?: true
                ]
            ]
            switch dep/class-name [
                'application-class
                'dynamic-library-class
                'static-library-class [
                    objs: make block! 8
                    ;dump dep
                    for-each obj dep/depends [
                        ;dump obj
                        if obj/class-name = 'object-library-class [
                            append objs obj/depends
                        ]
                    ]
                    append buf gen-rule make entry-class [
                        target: dep/output
                        depends: join-of objs map-each ddep dep/depends [
                            if ddep/class-name <> 'object-library-class [ddep]
                        ]
                        commands: append reduce [dep/command] opt dep/post-build-commands
                    ]
                    emit buf dep
                ]

                'object-library-class [
                    ;assert [dep/class-name != 'object-library-class] ;No nested object-library-class allowed
                    for-each obj dep/depends [
                        assert [obj/class-name = 'object-file-class]
                        if not obj/generated? [
                            obj/generated?: true
                            append buf gen-rule obj/gen-entries/(try all [project/class-name = 'dynamic-library-class 'PIC]) dep
                        ]
                    ]
                ]

                'object-file-class [
                    ;print ["generate object rule"]
                    append buf gen-rule dep/gen-entries project
                ]

                'entry-class 'var-class [
                    append buf gen-rule dep
                ]

                'ext-dynamic-class 'ext-static-class [
                    ;pass
                ]

                default [
                    dump dep
                    fail ["unrecognized project type:" dep/class-name]
                ]
            ]
        ]
    ]

    generate: method [
        output [file!]
        solution [object!]
        <with>
        entry-class
    ][
        buf: make binary! 2048
        assert [solution/class-name = 'solution-class]

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

    run-target: meth [
        return: <void>
        target [object!]
        /cwd dir [file!]
        <local>
        cmd
    ][
        switch target/class-name [
            'var-class [
                ;pass: already been taken care of by PREPARE
            ]
            'entry-class [
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

    run: meth [
        return: <void>
        project [object!]
        /parent p-project
        <local>
        dep
        obj
        objs
        suffix
    ][
        ;dump project
        if not object? project [return]

        prepare project

        if not find [ext-dynamic-class ext-static-class] project/class-name [
            if project/generated? [return]
            project/generated?: true
        ]

        switch project/class-name [
            'application-class
            'dynamic-library-class
            'static-library-class [
                objs: make block! 8
                for-each obj project/depends [
                    ;dump obj
                    if obj/class-name = 'object-library-class [
                        append objs obj/depends
                    ]
                ]
                for-each dep project/depends [
                    run/parent dep project
                ]
                run-target make entry-class [
                    target: project/output
                    depends: join-of project/depends objs
                    commands: project/command
                ]
            ]

            'object-library-class [
                for-each obj project/depends [
                    assert [obj/class-name = 'object-file-class]
                    if not obj/generated? [
                        obj/generated?: true
                        run-target obj/gen-entries/(try all [p-project/class-name = 'dynamic-library-class 'PIC]) project
                    ]
                ]
            ]

            'object-file-class [
                ;print ["generate object rule"]
                assert [parent]
                run-target project/gen-entries p-project
            ]

            'entry-class 'var-class [
                run-target project
            ]

            'ext-dynamic-class 'ext-static-class [
                ;pass
            ]

            'solution-class [
                for-each dep project/depends [
                    run dep
                ]
            ]

            default [
                dump project
                fail ["unrecognized project type:" project/class-name]
            ]
        ]
    ]
]

visual-studio: make generator-class [
    solution-format-version: "12.00"
    tools-version: "15.00"
    target-win-version: "10.0.10586.0"
    platform-tool-set: "v141"
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
        buf
        project [object!]
        <local>
        depends
        dep
    ][
        project-name: either project/class-name = 'entry-class [project/target][project/name]
        append buf unspaced [
            {Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "} to text! project-name {",}
            {"} project-name {.vcxproj", "} project/id {"} newline
        ]

        ;print ["emitting..."]
        ;dump project
        depends: make block! 8
        for-each dep project/depends [
            if find [
                object-library-class
                dynamic-library-class
                static-library-class
                application-class
            ] dep/class-name [
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

        depends
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
        ldflags [block!]
        <static>
        digit (charset "0123456789")
    ][
        size: _
        while [not tail? ldflags] [
            ;dump ldflags/1
            if i: filter-flag ldflags/1 "msc" [
                parse i [
                    "/stack:"
                    copy size: some digit
                ] then [
                    remove ldflags
                    return size
                ]
            ]
            ldflags: next ldflags
        ]
        size
    ]

    find-subsystem: method [
        ldflags [block!]
    ][
        subsystem: _
        while [not tail? ldflags] [
            ;dump ldflags/1
            if i: filter-flag ldflags/1 "msc" [
                parse i [
                    "/subsystem:"
                    copy subsystem: to end
                ] then [
                    remove ldflags
                    return subsystem
                ]
            ]
            ldflags: next ldflags
        ]
        subsystem
    ]

    find-optimization: meth [
        optimization
    ][
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

    find-optimization?: meth [
        optimization
    ][
        not find [0 _ no false off #[false]] optimization
    ]

    generate-project: method [
        return: <void>
        output-dir [file!] {Solution directory}
        project [object!]
        <with>
        build-type
        cpu
        platform
        <local>
        project-name
    ][
        project-name: either project/class-name = 'entry-class [project/target][project/name]
        if project/generated? [
            print ["project" project-name "was already generated"]
            return
        ]

        ;print ["Generating project file for" project-name]

        project/generated?: true
        ;print mold project

        either find [
            dynamic-library-class
            static-library-class
            application-class
            object-library-class
            entry-class
        ] project/class-name [
            project/id: take uuid-pool
        ][
            dump project
            fail ["unsupported project:" (project/class-name)]
        ]

        config: unspaced [build-type {|} platform]
        project-dir: unspaced [project-name ".dir\" build-type "\"]

        searches: make text! 1024
        if project/class-name <> 'entry-class [
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
                switch d/class-name [
                    'ext-dynamic-class
                    'ext-static-class
                    'static-library-class [
                        if ext: filter-flag d/output "msc" [
                            append lib unspaced [
                                ext
                                if not ends-with? ext ".lib" [".lib"]
                                ";"
                            ]
                        ]
                    ]
                    'application-class [
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

            if find [dynamic-library-class application-class] project/class-name [
                for-each s project/searches [
                    if s: filter-flag s "msc" [
                        append searches unspaced [file-to-local s ";"]
                    ]
                ]

                stack-size: try all [
                    project/ldflags
                    find-stack-size project/ldflags
                ]
            ]

            compile-as: try all [
                block? project/cflags
                find-compile-as project/cflags
            ]
        ]

        xml: unspaced [
            {<?xml version="1.0" encoding="UTF-8"?>
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
    either project/class-name = 'entry-class [
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
  <ConfigurationType>} switch project/class-name [
      'static-library-class 'object-library-class ["StaticLibrary"]
      'dynamic-library-class ["DynamicLibrary"]
      'application-class ["Application"]
      'entry-class ["Utility"]
      fail ["Unsupported project class:" (project/class-name)]
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
    if project/class-name != 'entry-class [
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
    if project/class-name <> 'entry-class [
        unspaced [ {
      <AdditionalIncludeDirectories>} inc {</AdditionalIncludeDirectories>
      <AssemblerListingLocation>} build-type {/</AssemblerListingLocation>}
      ;RuntimeCheck is not compatible with optimization
      if not find-optimization? project/optimization [ {
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
      <Optimization>} find-optimization project/optimization {</Optimization>
      <PrecompiledHeader>NotUsing</PrecompiledHeader>
      <RuntimeLibrary>MultiThreaded} if build-type = "debug" ["Debug"] {DLL</RuntimeLibrary>
      <RuntimeTypeInfo>true</RuntimeTypeInfo>
      <WarningLevel>Level3</WarningLevel>
      <TreatWarningAsError></TreatWarningAsError>
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
        find [dynamic-library-class application-class] project/class-name [
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
      <SubSystem>} opt either project/ldflags [find-subsystem project/ldflags]["Console"] {</SubSystem>
      <Version></Version>
    </Link>}
            ]
        ]
        find [static-library-class object-library-class] project/class-name [
            unspaced [ {
    <Lib>
      <AdditionalOptions> /machine:} cpu { %(AdditionalOptions)</AdditionalOptions>
    </Lib>}
            ]
        ]
        all [
            find [entry-class] project/class-name
            project/commands
        ][
            unspaced [ {
    <PreBuildEvent>
      <Command>} use [cmd][delimit map-each cmd project/commands [reify cmd] "^M^/"] {
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
      <Command>} use [cmd][delimit map-each cmd project/post-build-commands [reify cmd] "^M^/"] {
      </Command>
    </PostBuildEvent>}
        ]
    ] {
  </ItemDefinitionGroup>
  <ItemGroup>
} use [o sources collected][
    sources: make text! 1024
    for-each o project/depends [
        case [
            o/class-name = 'object-file-class [
                append sources unspaced [
                    {    <ClCompile Include="} o/source {" >^/}
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
                            {        <Optimization>} find-optimization o/optimization {</Optimization>^/}
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
                                    (collected)
                                ]
                                {</AdditionalOptions>^/}
                            ]
                        ]
                    ]
                    {    </ClCompile>^/}
                ]
            ] ;object-file-class
            o/class-name = 'object-library-class [
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
        output-dir [file!] {Solution directory}
        solution [object!]
        /x86
        <with>
        build-type
        cpu
        platform
        <local>
        dep
        projects
        vars
    ][
        buf: make binary! 2048
        assert [solution/class-name = 'solution-class]

        prepare solution

        if solution/debug [build-type: "Debug"]
        if x86 [cpu: "x86" platform: "Win32"]
        config: unspaced [build-type {|} platform]

        append buf unspaced [
            "Microsoft Visual Studio Solution File, Format Version " solution-format-version newline
        ]

        ;print ["vars:" mold vars]

        ; Project section
        projects: make block! 8
        for-each dep solution/depends [
            if find [
                dynamic-library-class
                static-library-class
                object-library-class
                application-class
                entry-class
            ] dep/class-name [
                append projects dep
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
            "^-GlobalSection(SolutionCOnfigurationPlatforms) = preSolution^/"
            tab tab config { = } config newline
            "^-EndGlobalSection^/"
            "^-GlobalSection(SolutionCOnfigurationPlatforms) = postSolution^/"
        ]
        for-each proj projects [
            append buf unspaced [
                tab tab proj/id {.} config {.ActiveCfg = } config newline
            ]
        ]

        append buf unspaced [
            "^-EndGlobalSection^/"
            "EndGlobal"
        ]

        write output-dir/(unspaced [solution/name ".sln"]) buf
    ]
]

vs2015: make visual-studio [
    platform-tool-set: "v140"
]

] ;-- end of `rebmake: make object!` workaround for lack of `Type: 'module`
