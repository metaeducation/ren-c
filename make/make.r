REBOL []

;;;; DO & IMPORT ;;;;
do %r2r3-future.r
do %common.r
do %systems.r
file-base: make object! load %file-base.r

; !!! Since %rebmake.r is a module,
    ; it presents a challenge for the "shim" code
    ; when it depends on a change.
    ; This needs to be addressed in a generic way,
    ; but that requires foundational work on modules.

append lib compose [
    file-to-local-hack: (:file-to-local)
    local-to-file-hack: (:local-to-file)
]
rebmake: import %rebmake.r

;;;; GLOBALS 
config-dir: %.
base-dir: pwd
user-config: make object! load config-dir/default-config.r

;;;; PROCESS ARGS
; args are:
; [CONFIG | OPTION | COMMAND] ...
; COMMAND = WORD
; OPTION = 'NAME=VALUE' | 'NAME: VALUE'
; CONFIG = 'config=CONFIG-FILE' | 'config: CONFIG-FILE'
args: parse-args/all system/options/args
; now args are ordered and separated by bar:
; [NAME VALUE ... '| COMMAND ...]
either commands: find args '| [
    options: copy/part args commands
    commands: next commands
] [options: args]
; now args are splitted in options and commands

; process configs first
for-each [name value] options [
    if name = 'CONFIG [
        user-config: make user-config load config-dir/:value
    ]
]

; then process options
; Allow any of the settings in user-config
; to be overwritten by command line options
for-each [name value] options [
    switch/default name [
        CONFIG [
            ;pass
        ]
        EXTENSIONS [
            use [ext-file user-ext][
                either any [
                    exists? ext-file: to file! value
                    exists? ext-file: to file! config-dir/(value)
                ][
                    user-ext: make object! load ext-file
                    unless all [
                        find? words-of user-ext 'extensions
                        block? user-ext/extensions
                    ][
                        fail ["Malformated extension selection file, it needs to be 'EXTENSIONS: []'" (mold user-ext)]
                    ]
                    user-config/extensions: user-ext/extensions
                ][
                    user-ext: load value
                    unless block? user-ext [
                        fail [
                            "Selected extensions must be a block, not"
                            (type-of user-ext)
                        ]
                    ]
                    user-config/extensions: user-ext
                ]
            ]
        ]
    ][
        set in user-config (to-word replace/all to string! name #"_" #"-")
            load value
    ]
]

; process commands
if not empty? commands [user-config/target: load commands]

;;;; MODULES & EXTENSIONS
system-config: config-system user-config/os-id
rebmake/set-target-platform system-config/os-base

to-obj-path: func [
    file [any-string!]
    ext:
][
    ext: find/last file #"."
    remove/part ext (length-of ext)
    join-of %objs/ head-of append ext rebmake/target-platform/obj-suffix
]

gen-obj: func [
    s
    /dir directory [any-string!]
    /D definitions [block!]
    <local>
    flags
][
    flags: make block! 8
    if block? s [
        for-each flag next s [
            append flags opt switch/default flag [
                <no-uninitialized> [
                    [
                        <gnu:-Wno-uninitialized>

                        ;-Wno-unknown-warning seems to only modify the
                        ; immediately following option
                        ;
                        ;<gnu:-Wno-unknown-warning>
                        ;<gnu:-Wno-maybe-uninitialized>

                        <msc:/wd4701> <msc:/wd4703>
                    ]
                ]
                <implicit-fallthru> [
                    [
                        <gnu:-Wno-unknown-warning>
                        <gnu:-Wno-implicit-fallthrough>
                    ]
                ]
                <no-unused-parameter> [
                    <gnu:-Wno-unused-parameter>
                ]
                <no-shift-negative-value> [
                    <gnu:-Wno-shift-negative-value>
                ]
                <no-make-header> [
                    ;for make-header. ignoring
                    _
                ]
                <no-unreachable> [
                    <msc:/wd4702>
                ]
                <no-hidden-local> [
                    <msc:/wd4456>
                ]
                <no-constant-conditional> [
                    <msc:/wd4127>
                ]
            ][
                really [string! tag!] flag
            ]
        ]
        s: s/1
    ]

    make rebmake/object-file-class compose/only [
        source: to-file join-of either dir [directory][%../src/] s
        output: to-obj-path to string! s
        cflags: either empty? flags [_] [flags]
        definitions: (to-value :definitions)
    ]
]

libuuid-objs: map-each s [
    %uuid/libuuid/gen_uuid.c
    %uuid/libuuid/unpack.c
    %uuid/libuuid/pack.c
    %uuid/libuuid/randutils.c
][
    gen-obj/dir s "../src/extensions/"
]

module-class: make object! [
    class-name: 'module-class
    name: _
    depends: _
    source: _ ;main script

    includes: _
    definitions: _
    cflags: _

    searches: _
    libraries: _
    ldflags: _
]

extension-class: make object! [
    class-name: 'extension-class
    name: _
    loadable: yes ;can be loaded at runtime
    modules: _
    source: _
    init: _ ;init-script
]

;libffi
cfg-ffi: make object! [
    cflags:
    includes:
    definitions:
    ldflags:
    libraries:
    searches: _
]
either block? user-config/with-ffi [
    cfg-ffi: make cfg-ffi user-config/with-ffi
    cfg-ffi/libraries: map-each lib cfg-ffi/libraries [
        case [
            file? lib [
                make rebmake/ext-dynamic-class [
                    output: lib
                    flags: either user-config/with-ffi = 'static [[static]][_]
                ]
            ]
            all [
                    object? lib
                    find [dynamic-library-class static-library-class] lib/class-name
            ][
                lib
            ]
            true [
                fail ["Libraries can only be file! or static/dynamic library object, not" lib]
            ]
        ]
    ]
][
    switch/default user-config/with-ffi [
        static dynamic [
            for-each var [includes cflags searches ldflags][
                x: rebmake/pkg-config
                    any [user-config/pkg-config {pkg-config}]
                    var
                    %libffi
                unless empty? x [
                    set (in cfg-ffi var) x
                ]
            ]

            libs: rebmake/pkg-config
                any [user-config/pkg-config {pkg-config}]
                'libraries
                %libffi

            cfg-ffi/libraries: map-each lib libs [
                make rebmake/ext-dynamic-class [
                    output: lib
                    flags: either user-config/with-ffi = 'static [[static]][_]
                ]
            ]
        ]
        _ no off false #[false] [
            ;pass
        ]
    ][
        fail [
            "WITH-FFI should be one of [dynamic static no]"
            "not" (user-config/with-ffi)
        ]
    ]
]

available-modules: reduce [
    ;name module-file other-files
    mod-crypt: make module-class [
        name: 'Crypt
        source: %crypt/mod-crypt.c
        includes: copy [
            ;
            ; Added so `#include "bigint/bigint.h` can be found by %rsa.h
            ; and `#include "rsa/rsa.h" can be found by %dh.c
            ;
            %../src/extensions/crypt
        ]
        depends: [
            %crypt/aes/aes.c
            %crypt/bigint/bigint.c
            %crypt/dh/dh.c
            %crypt/rc4/rc4.c
            %crypt/rsa/rsa.c
            %crypt/sha256/sha256.c
        ]
    ]

    mod-process: make module-class [
        name: 'Process
        source: %process/mod-process.c
    ]

    mod-view: make module-class [
        name: 'View
        source: %view/mod-view.c

        ; The Windows REQUEST-FILE does not introduce any new dependencies.
        ; REQUEST-DIR depends on OLE32 for CoInitialize() because it is done
        ; with some weird COM shell API.  Linux of course introduces a GTK
        ; dependency, so that is not included by default in the core.
        ;
        ; For now just enable REQUEST-FILE on Windows if the view module is
        ; included, because it doesn't bring along any extra dependencies.
        ;
        libraries: (comment [to-value switch system-config/os-base [
            Windows [
                ; You would currently have to define USE_WINDOWS_DIRCHOOSER
                ; to try out the REQUEST-DIR code.
                ;
                [%Ole32]
            ]

            ; Note: It seemed to help to put this at the beginning of the
            ; compiler and linking command lines:
            ;
            ;     g++ `pkg-config --cflags --libs gtk+-3.0` ...
            ;
            ; You would currently have to define USE_GTK_FILECHOOSER to get
            ; the common dialog code in REQUEST-FILE.
            ;
            Linux [
                [%gtk-3 %gobject-2.0 %glib-2.0]
            ]
        ]] blank)
    ]

    mod-lodepng: make module-class [
        name: 'LodePNG
        source: %png/mod-lodepng.c
        definitions: copy [
            ;
            ; Rebol already includes zlib, and LodePNG is hooked to that
            ; copy of zlib exported as part of the internal API.
            ;
            "LODEPNG_NO_COMPILE_ZLIB"

            ; LodePNG doesn't take a target buffer pointer to compress "into".
            ; Instead, you hook it by giving it an allocator.  The one used
            ; by Rebol backs the memory with a series, so that the image data
            ; may be registered with the garbage collector.
            ;
            "LODEPNG_NO_COMPILE_ALLOCATORS"

            ; With LodePNG, using C++ compilation creates a dependency on
            ; std::vector.  This is conditional on __cplusplus, but there's
            ; an #ifdef saying that even if you're compiling as C++ to not
            ; do this.  It's not an interesting debug usage of C++, however,
            ; so there's no reason to be doing it.
            ;
            "LODEPNG_NO_COMPILE_CPP"
        ]
        depends: [
            [
                %png/lodepng.c

                ; The LodePNG module has local scopes with declarations that
                ; alias declarations in outer scopes.  This can be confusing,
                ; so it's avoided in the core, but LodePNG is maintained by
                ; someone else with different standards.
                ;
                ;    declaration of 'identifier' hides previous
                ;    local declaration
                ;
                <msc:/wd4456>

                ; This line causes the warning "result of 32-bit shift
                ; implicitly converted to 64-bits" in MSVC 64-bit builds:
                ;
                ;     size_t palsize = 1u << mode_out->bitdepth;
                ;
                ; It could be changed to `((size_t)1) << ...` and avoid it.
                ;
                <msc:/wd4334>

                ; There is a casting away of const qualifiers, which is bad,
                ; but the PR to fix it has not been merged to LodePNG master.
                ;
                <gnu:-Wno-cast-qual>
            ]
        ]
    ]

    mod-gif: make module-class [
        name: 'GIF
        source: %gif/mod-gif.c
    ]

    mod-bmp: make module-class [
        name: 'BMP
        source: %bmp/mod-bmp.c
    ]

    mod-locale: make module-class [
        name: 'Locale
        source: [
            %locale/mod-locale.c

            ; The locale module uses non-constant aggregate initialization,
            ; e.g. LOCALE_WORD_ALL is defined as Ext_Canons_Locale[4], but
            ; is assigned as `= {{LOCALE_WORD_ALL, LC_ALL}...}` to a struct.
            ; For the moment, since it's just the locale module, disable the
            ; warning, though we don't want to use nonstandard C as a general
            ; rule in the core.
            ;
            ;    nonstandard extension used : non-constant aggregate
            ;    initializer
            ;
            <msc:/wd4204>
        ]
    ]

    mod-jpg: make module-class [
        name: 'JPG
        source: %jpg/mod-jpg.c
        depends: [
            ;
            ; The JPG sources come from elsewhere; invasive maintenance for
            ; compiler rigor is not worthwhile to be out of sync with original.
            ;
            [
                %jpg/u-jpg.c

                <gnu:-Wno-unused-parameter> <msc:/wd4100>

                <gnu:-Wno-shift-negative-value>

                ; "conditional expression is constant"
                ;
                <msc:/wd4127>
            ]
        ]
    ]

    mod-uuid: make module-class [
        name: 'UUID
        source: %uuid/mod-uuid.c
        includes: [%../src/extensions/uuid/libuuid]
        depends: to-value switch system-config/os-base [
            linux [
                libuuid-objs
            ]
        ]

        libraries: to-value switch system-config/os-base [
            Windows [
                [%rpcrt4]
            ]
        ]
        ldflags: to-value switch system-config/os-base [
            OSX [
                ["-framework CoreFoundation"]
            ]
        ]
    ]

    mod-odbc: make module-class [
        name: 'ODBC
        source: [
            %odbc/mod-odbc.c

            ; ODBCGetTryWaitValue() is prototyped as a C++-style void argument
            ; function, as opposed to ODBCGetTryWaitValue(void), which is the
            ; right way to do it in C.  But we can't change <sqlext.h>, so
            ; disable the warning.
            ;
            ;     'function' : no function prototype given:
            ;     converting '()' to '(void)'
            ;
            <msc:/wd4255>

            ; The ODBC include also uses nameless structs/unions, which are a
            ; non-standard extension.
            ;
            ;     nonstandard extension used: nameless struct/union
            ;
            <msc:/wd4201>
        ]
        libraries: to-value switch/default system-config/os-base [
            Windows [
                [%odbc32]
            ]
        ][
            ; On some systems (32-bit Ubuntu 12.04), odbc requires ltdl
            append-of [%odbc] unless find [no false off _ #[false]] user-config/odbc-requires-ltdl [%ltdl]
        ]
    ]

    mod-ffi: make module-class [
        name: 'FFI
        source: %ffi/mod-ffi.c
        depends: [
            %ffi/t-struct.c
            %ffi/t-routine.c
        ]
        includes: cfg-ffi/includes
        cflags: cfg-ffi/cflags
        searches: cfg-ffi/searches
        ldflags: cfg-ffi/ldflags
        libraries: cfg-ffi/libraries
        ; Currently the libraries are specified by the USER-CONFIG/WITH-FFI
        ; until that logic is moved to something here.  So if you are going
        ; to build the FFI module, you need to also set WITH-FFI (though
        ; setting WITH-FFI alone will not get you the module)
    ]

    mod-debugger: make module-class [
        name: 'Debugger
        source: %debugger/mod-debugger.c
        includes: copy [
            %../src/extensions/debugger
        ]
        depends: [
        ]
    ]
]

available-extensions: reduce [
    ext-crypt: make extension-class [
        name: 'Crypt
        loadable: no ;tls depends on this, so it has to be builtin
        modules: reduce [
            mod-crypt
        ]
        source: %crypt/ext-crypt.c
        init: %crypt/ext-crypt-init.reb
    ]

    ext-process: make extension-class [
        name: 'Process
        modules: reduce [
            mod-process
        ]
        source: %process/ext-process.c
        init: %process/ext-process-init.reb
    ]

    ext-view: make extension-class [
        name: 'View
        modules: reduce [
            mod-view
        ]
        source: %view/ext-view.c
        init: %view/ext-view-init.reb
    ]

    ext-png: make extension-class [
        name: 'PNG
        modules: reduce [
            mod-lodepng
        ]
        source: %png/ext-png.c
    ]

    ext-gif: make extension-class [
        name: 'GIF
        modules: reduce [
            mod-gif
        ]
        source: %gif/ext-gif.c
    ]

    ext-jpg: make extension-class [
        name: 'JPG
        modules: reduce [
            mod-jpg
        ]
        source: %jpg/ext-jpg.c
    ]

    ext-bmp: make extension-class [
        name: 'BMP
        modules: reduce [
            mod-bmp
        ]
        source: %bmp/ext-bmp.c
    ]

    ext-locale: make extension-class [
        name: 'Locale
        modules: reduce [
            mod-locale
        ]
        source: %locale/ext-locale.c
        init: %locale/ext-locale-init.reb
    ]

    ext-uuid: make extension-class [
        name: 'UUID
        modules: reduce [
            mod-uuid
        ]
        source: %uuid/ext-uuid.c
        init: %uuid/ext-uuid-init.reb
    ]

    ext-odbc: make extension-class [
        name: 'ODBC
        modules: reduce [
            mod-odbc
        ]
        source: %odbc/ext-odbc.c
        init: %odbc/ext-odbc-init.reb
    ]

    ext-ffi: make extension-class [
        name: 'FFI
        modules: reduce [
            mod-ffi
        ]
        source: %ffi/ext-ffi.c
        init: %ffi/ext-ffi-init.reb
    ]

    ext-debugger: make extension-class [
        name: 'Debugger
        modules: reduce [
            mod-debugger
        ]
        source: %debugger/ext-debugger.c
        init: %debugger/ext-debugger-init.reb
    ]
]
extension-names: map-each x available-extensions [to-lit-word x/name]

;;;; TARGETS
; I need targets here, for gathering names
; and use they with --help targets ...
targets: [
    clean [
        rebmake/execution/run make rebmake/solution-class [
            depends: reduce [
                clean
            ]
        ]
    ]
    prep [
        rebmake/execution/run make rebmake/solution-class [
            depends: flatten reduce [
                vars
                prep
                t-folders
                dynamic-libs
            ]
        ]
    ]
    r3
    execution [
        rebmake/execution/run make rebmake/solution-class [
            depends: flatten reduce [
                vars
                prep
                t-folders
                app
                dynamic-libs
            ]
        ]
    ]
    makefile [
        rebmake/makefile/generate %makefile solution
    ]
    nmake [
        rebmake/nmake/generate %makefile solution
    ]
    vs2017
    visual-studio [
        rebmake/visual-studio/generate/(all [system-config/os-name = 'Windows-x86 'x86]) %make solution
    ]
    vs2015 [
        rebmake/vs2015/generate/(all [system-config/os-name = 'Windows-x86 'x86]) %make solution
    ]
]
target-names: make block! 16
for-each x targets [
    either word? x [
        append target-names x
        append target-names '|
    ][
        take/last target-names
        append target-names
        newline
    ]
]

;;;; HELP ;;;;
indent: func [
    text [string!]
    /space
][
    replace/all text ;\
        either space [" "] [newline]
        "^/    "
]

help-topics: reduce [
;; !! Only 1 indentation level in help strings !!

'usage copy {=== USAGE ===^/
    > cd PATH/TO/REN-C/make
    then:
    > ./r3-make make.r [CONFIG | OPTION | TARGET ...]^/
MORE HELP:^/
    { -h | -help | --help } { HELP-TOPICS }
    }

'targets unspaced [{=== TARGETS ===^/
    }
    indent form target-names
    ]

'configs unspaced [ {=== CONFIGS ===^/
    config: CONFIG-FILE^/
FILES IN config/ SUBFOLDER:^/
    }
    indent/space form sort map-each x ;\
        load %configs/
        [to-string x]
    newline ]

'options unspaced [ {=== OPTIONS ===^/
CURRENT VALUES:^/
    }
    indent mold/only body-of user-config
    {^/
NOTES:^/
    - names are case-insensitive
    - `_` instead of '-' is ok
    - NAME=VALUE is the same as NAME: VALUE
    - e.g `OS_ID=0.4.3` === `os-id: 0.4.3`
    } ]

'os-id unspaced [ {=== OS-ID ===^/
CURRENT OS:^/
    }
    indent mold/only body-of config-system user-config/os-id
    {^/
LIST:^/
    OS-ID:  OS-NAME:}
    indent form collect [for-each-system s [
        keep unspaced [
            newline format 8 s/id s/os-name
        ]
    ]]
    newline
    ]

'extensions unspaced [{=== EXTENSIONS ===^/
    "[FLAG NAME MODULE ...]"^/
FLAG:
    - => disable
    * => dynamic
    + => builtin (default)^/
NAME:
    }
    indent delimit extension-names " | "
    {^/
CURRENT VALUE:
    }
    indent mold user-config/extensions
    newline
    ]
]
; dynamically fill help topics list ;-)
replace help-topics/usage "HELP-TOPICS" ;\
    form append map-each x help-topics [either string? x ['|] [x]] 'all

help: function [topic [string! blank!]] [
    topic: attempt [to-word topic]
    print ""
    case [
        topic = 'all [forskip help-topics 2 [
            print help-topics/2
        ] ]
        msg: select help-topics topic [
            print msg
        ]
        /else [print help-topics/usage]
    ]
]

; process help: {-h | -help | --help} [TOPIC]
if commands [forall commands [
    if find ["-h" "-help" "--help"] first commands
    [help second commands quit]
]]

;;;; GO!

set-exec-path: proc [
    tool [object!]
    path
][
    if path [
        unless file? path [
            fail "Tool path has to be a file!"
        ]
        tool/exec-file: path
    ]
]

parse user-config/toolset [
    any [
        'gcc opt set cc-exec [file! | blank!] (
            rebmake/default-compiler: rebmake/gcc
        )
        | 'clang opt set cc-exec [file! | blank!] (
            rebmake/default-compiler: rebmake/clang
        )
        | 'cl opt set cc-exec [file! | blank!] (
            rebmake/default-compiler: rebmake/cl
        )
        | 'ld opt set linker-exec [file! | blank!] (
            rebmake/default-linker: rebmake/ld
        )
        | 'llvm-link opt set linker-exec [file! | blank!] (
            rebmake/default-linker: rebmake/llvm-link
        )
        | 'link opt set linker-exec [file! | blank!] (
            rebmake/default-linker: rebmake/link
        )
        | 'strip opt set strip-exec [file! | blank!] (
            rebmake/default-strip: rebmake/strip
            rebmake/default-strip/options: [<gnu:-S> <gnu:-x> <gnu:-X>]
            if all [set? 'strip-exec strip-exec][
                set-exec-path rebmake/default-strip strip-exec
            ]
        )
        | pos: (unless tail? pos [fail ["failed to parset toolset at:" mold pos]])
    ]
]

; sanity checking the compiler and linker
if blank? rebmake/default-compiler [
    fail ["Compiler is not set"]
]
if blank? rebmake/default-linker [
    fail ["Default linker is not set"]
]

switch/default rebmake/default-compiler/name [
    gcc [
        if rebmake/default-linker/name != 'ld [
            fail ["Incompatible compiler (GCC) and linker: " rebmake/default-linker/name]
        ]
    ]
    clang [
        unless find [ld llvm-link] rebmake/default-linker/name [
            fail ["Incompatible compiler (CLANG) and linker: " rebmake/default-linker/name]
        ]
    ]
    cl [
        if rebmake/default-linker/name != 'link [
            fail ["Incompatible compiler (CL) and linker: " rebmake/default-linker/name]
        ]
    ]
][
    fail ["Unrecognized compiler (gcc, clang or cl):" cc]
]
if all [set? 'cc-exec cc-exec][
    set-exec-path rebmake/default-compiler cc-exec
]
if all [set? 'linker-exec linker-exec][
    set-exec-path rebmake/default-linker linker-exec
]

app-config: make object! [
    cflags: make block! 8
    ldflags: make block! 8
    libraries: make block! 8
    debug: off
    optimization: 2
    definitions: copy []
    includes: copy [%../src/include]
    searches: make block! 8
]

cfg-sanitize: false
cfg-symbols: false
switch/default user-config/debug [
    #[false] no false off none [
        append app-config/definitions ["NDEBUG"]
        app-config/debug: off
    ]
    #[true] yes true on [
        app-config/debug: on
    ]
    asserts [
        ; /debug should only affect the "-g -g3" symbol inclusions in rebmake.
        ; To actually turn off asserts or other checking features, NDEBUG must
        ; be defined.
        ;
        app-config/debug: off
    ]
    symbols [
        cfg-symbols: true
        app-config/debug: on
    ]
    sanitize [
        app-config/debug: on
        cfg-symbols: true
        cfg-sanitize: true
        append app-config/cflags <gnu:-fsanitize=address>
        append app-config/ldflags <gnu:-fsanitize=address>
    ]

    ; Because it has symbols but no debugging, the callgrind option can also
    ; be used when trying to find bugs that only appear in release builds or
    ; higher optimization levels.
    ;
    callgrind [
        cfg-symbols: true
        append app-config/definitions ["NDEBUG"]
        append app-config/definitions ["REN_C_STDIO_OK"] ;; for debugging
        append app-config/cflags "-g" ;; for symbols
        app-config/debug: off

        ; A special CALLGRIND native is included which allows metrics
        ; gathering to be turned on and off.  Needs <valgrind/callgrind.h>
        ; which should be installed when you install the valgrind package.
        ;
        ; To start valgrind in a mode where it's not gathering at the outset:
        ;
        ; valgrind --tool=callgrind --dump-instr=yes --collect-atstart=no ./r3
        ;
        append app-config/definitions ["INCLUDE_CALLGRIND_NATIVE"]
    ]
][
    fail ["unrecognized debug setting:" user-config/debug]
]

switch user-config/optimize [
    #[false] false no off 0 [
        app-config/optimization: false
    ]
    1 2 3 4 "s" [
        app-config/optimization: user-config/optimize
    ]
]


cfg-cplusplus: false
;standard
append app-config/cflags opt switch/default user-config/standard [
    c [
        _
    ]
    gnu89 c99 gnu99 c11 [
        to tag! unspaced ["gnu:--std=" user-config/standard]
    ]
    c++ [
        cfg-cplusplus: true
        [
            <gnu:-x c++>
            <msc:/TP>
        ]
    ]
    c++98 c++0x c++11 c++14 c++17 c++latest [

        cfg-cplusplus: true
        reduce [
            ; Compile C files as C++.
            ;
            ; !!! The original code appeared to make it so that if a Visual
            ; Studio project was created, the /TP option gets removed and it
            ; was translated into XML under the <CompileAs> option.  But
            ; that meant extensions weren't getting the option, so it has
            ; been disabled pending review.
            ;
            ; !!! For some reason, clang has deprecated this ability, though
            ; it still works.  It is not possible to disable the deprecation,
            ; so RIGOROUS can not be used with clang when building as C++...
            ; the files would (sadly) need to be renamed to .cpp or .cxx
            ;
            <msc:/TP>
            <gnu:-x c++>

            ; C++ standard (MSVC only supports "c++14/17/latest")
            ;
            to tag! unspaced ["gnu:--std=" user-config/standard]
            to tag! unspaced [
                "msc:/std:" lowercase to string! user-config/standard
            ]

            ; Note: The C and C++ user-config/standards do not dictate if
            ; `char` is signed or unsigned.  Lest anyone think environments
            ; all settled on them being signed, they're not... Android NDK
            ; uses unsigned:
            ;
            ; http://stackoverflow.com/questions/7414355/
            ;
            ; In order to give the option some exercise, make GCC C++ builds
            ; use unsigned chars.
            ;
            <gnu:-funsigned-char>
 
            ; MSVC never bumped their __cplusplus version past 1997, even if
            ; you compile with C++17.  Hence CPLUSPLUS_11 is used by Rebol
            ; code as the switch for most C++ behaviors, and we have to
            ; define that explicitly.
            ;
            <msc:/DCPLUSPLUS_11>
        ]
    ]
][
    fail [
        "STANDARD should be one of"
        "[c gnu89 gnu99 c99 c11 c++ c++11 c++14 c++17 c++latest]"
        "not" (user-config/standard)
    ]
]

cfg-rigorous: false
append app-config/cflags opt switch/default user-config/rigorous [
    #[true] yes on true [
        cfg-rigorous: true
        compose [
            <gnu:-Werror> <msc:/WX>;-- convert warnings to errors

            ; If you use pedantic in a C build on an older GNU compiler,
            ; (that defaults to thinking it's a C89 compiler), it will
            ; complain about using `//` style comments.  There is no
            ; way to turn this complaint off.  So don't use pedantic
            ; warnings unless you're at c99 or higher, or C++.
            ;
            (
                if any [
                    cfg-cplusplus | not find [c gnu89] user-config/standard
                ][
                    <gnu:--pedantic>
                ]
            )

            <gnu:-Wextra>
            <gnu:-Wall> <msc:/Wall>

            <gnu:-Wchar-subscripts>
            <gnu:-Wwrite-strings>
            <gnu:-Wundef>
            <gnu:-Wformat=2>
            <gnu:-Wdisabled-optimization>
            <gnu:-Wlogical-op>
            <gnu:-Wredundant-decls>
            <gnu:-Woverflow>
            <gnu:-Wpointer-arith>
            <gnu:-Wparentheses>
            <gnu:-Wmain>
            <gnu:-Wtype-limits>
            <gnu:-Wclobbered>

            ; Neither C++98 nor C89 had "long long" integers, but they
            ; were fairly pervasive before being present in the standard.
            ;
            <gnu:-Wno-long-long>

            ; When constness is being deliberately cast away, `m_cast` is
            ; used (for "m"utability).  However, this is just a plain cast
            ; in C as it has no const_cast.  Since the C language has no
            ; way to say you're doing a mutability cast on purpose, the
            ; warning can't be used... but assume the C++ build covers it.
            ;
            ; !!! This is only checked by default in *release* C++ builds,
            ; because the performance and debug-stepping impact of the
            ; template stubs when they aren't inlined is too troublesome.
            (
                either all [
                    cfg-cplusplus
                    find app-config/definitions "NDEBUG"
                ][
                    <gnu:-Wcast-qual>
                ][
                    <gnu:-Wno-cast-qual>
                ]
            )

            ;     'bytes' bytes padding added after construct 'member_name'
            ;
            ; Disable warning C4820; just tells you struct is not an exactly
            ; round size for the platform.
            ;
            <msc:/wd4820>

            ; Without disabling this, you likely get:
            ;
            ;     '_WIN32_WINNT_WIN10_TH2' is not defined as a preprocessor
            ;     macro, replacing with '0' for '#if/#elif'
            ;
            ; Which seems to be some mistake on Microsoft's part, that some
            ; report can be remedied by using WIN32_LEAN_AND_MEAN:
            ;
            ; https://stackoverflow.com/q/11040133/
            ;
            ; But then if you include <winioctl.h> (where the problem occurs)
            ; you'd still have it.
            ;
            <msc:/wd4668>

            ; There are a currently a lot of places in the code where `int` is
            ; passed to REBCNT, where the signs mismatch.  Disable C4365:
            ;
            ;    'action' : conversion from 'type_1' to 'type_2',
            ;    signed/unsigned mismatch
            ;
            ; and C4245:
            ;
            ;    'conversion' : conversion from 'type1' to 'type2',
            ;    signed/unsigned mismatch
            ;
            <msc:/wd4365> <msc:/wd4245>
            <gnu:-Wsign-compare>

            ; The majority of Rebol's C code was written with little
            ; attention to overflow in arithmetic.  There are a lot of places
            ; in the code where a bigger type is converted into a smaller type
            ; without an explicit cast.  (e.g. REBI64 => SQLUSMALLINT,
            ; REBINT => REBYTE).  Disable C4242:
            ;
            ;     'identifier' : conversion from 'type1' to 'type2', possible
            ;     loss of data
            ;
            ; The issue needs systemic review.
            ;
            <msc:/wd4242>
            <gnu:-Wno-conversion> <gnu:-Wno-strict-overflow>
            ;<gnu:-Wstrict-overflow=5>

            ; When an inline function is not referenced, there can be a
            ; warning about this; but it makes little sense to do so since
            ; there are a lot of standard library functions in includes that
            ; are inline which one does not use (C4514):
            ;
            ;     'function' : unreferenced inline function has been removed
            ;
            ; Inlining is at the compiler's discretion, it may choose to
            ; ignore the `inline` keyword.  Usually it won't tell you it did
            ; this, but disable the warning that tells you (C4710):
            ;
            ;     function' : function not inlined
            ;
            ; There's also an "informational" warning telling you that a
            ; function was chosen for inlining when it wasn't requested, so
            ; disable that also (C4711):
            ;
            ;     function 'function' selected for inline expansion
            ;
            <msc:/wd4514>
            <msc:/wd4710>
            <msc:/wd4711>

            ; It's useful to be told when a function pointer is assigned to
            ; an incompatible type of function pointer.  However, Rebol relies
            ; on the ability to have a kind of "void*-for-functions", e.g.
            ; CFUNC, which holds arbitrary function pointers.  There seems to
            ; be no way to enable function pointer type checking that allows
            ; downcasts and upcasts from just that pointer type, so it pretty
            ; much has to be completely disabled (or managed with #pragma,
            ; which we seek to avoid using in the codebase)
            ;
            ;    'operator/operation' : unsafe conversion from
            ;    'type of expression' to 'type required'
            ;
            <msc:/wd4191>

            ; Though we make sure all enum values are handled at least with a
            ; default:, this warning basically doesn't let you use default:
            ; at all...forcing every case to be handled explicitly.
            ;
            ;     enumerator 'identifier' in switch of enum 'enumeration'
            ;     is not explicitly handled by a case label
            ;
            <msc:/wd4061>

            ; setjmp() and longjmp() cannot be combined with C++ objects due
            ; to bypassing destructors.  Yet the Microsoft compiler seems to
            ; think even "POD" (plain-old-data) structs qualify as
            ; "C++ objects", so they run destructors (?)
            ;
            ;     interaction between 'function' and C++ object destruction
            ;     is non-portable
            ;
            ; This is lousy, because it would be a VERY useful warning, if it
            ; weren't as uninformative as "your C++ program is using setjmp".
            ;
            ; https://stackoverflow.com/q/45384718/
            ;
            <msc:/wd4611>

            ; Assignment within conditional expressions is tolerated in the
            ; core so long as parentheses are used.  if ((x = 10) != y) {...}
            ;
            ;     assignment within conditional expression
            ;
            <msc:/wd4706>

            ; gethostbyname() is deprecated by Microsoft, but dealing with
            ; that is not a present priority.  It is supposed to be replaced
            ; with getaddrinfo() or GetAddrInfoW().  This bypasses the
            ; deprecation warning for now via a #define
            ;
            <msc:/D_WINSOCK_DEPRECATED_NO_WARNINGS>

            ; This warning happens a lot in a 32-bit build if you use float
            ; instead of double in Microsoft Visual C++:
            ;
            ;    storing 32-bit float result in memory, possible loss
            ;    of performance
            ;
            <msc:/wd4738>

            ; For some reason, even if you don't actually invoke moves or
            ; copy constructors, MSVC warns you that you wouldn't be able to
            ; if you ever did.  :-/
            ;
            <msc:/wd5026>
            <msc:/wd4626>
            <msc:/wd5027>
            <msc:/wd4625>

            ; If a function hasn't been explicitly declared as nothrow, then
            ; passing it to extern "C" routines gets a warning.  This is a C
            ; codebase being built as C++, so there shouldn't be throws.
            ;
            <msc:/wd5039>
        ]
    ]
    _ #[false] no off false [
        cfg-rigorous: false
        _
    ]
][
    fail ["RIGOROUS must be yes, no, or logic! not" (user-config/rigorous)]
]

append app-config/ldflags opt switch/default user-config/static [
    _ no off false #[false] [
        ;pass
        _
    ]
    yes on #[true] [
        compose [
            <gnu:-static-libgcc>
            (if cfg-cplusplus [<gnu:-static-libstdc++>])
            (if cfg-sanitize [<gnu:-static-libasan>])
        ]
    ]
][
    fail ["STATIC must be yes, no or logic! not" (user-config/static)]
]

;TCC
cfg-tcc: _
case [
    any [
        file? user-config/with-tcc
        find? [yes on true #[true]] user-config/with-tcc
    ][
        tcc-rootdir: either file? user-config/with-tcc [
            first split-path user-config/with-tcc
        ][
            %../external/tcc/
        ]
        cfg-tcc: make object! [
            exec-file: join-of tcc-rootdir any [get-env "TCC" %tcc]
            includes: [%../external/tcc]
            searches: reduce [tcc-rootdir]
            libraries: reduce [tcc-rootdir/libtcc1.a tcc-rootdir/libtcc.a]
            cpp-flags: get-env "TCC_CPP_EXTRA_FLAGS" ; extra cpp flags passed to tcc for preprocess %sys-core.i
        ]
        if block? cfg-tcc/libraries [
            cfg-tcc/libraries: map-each lib cfg-tcc/libraries [
                either file? lib [
                    either rebmake/ends-with? lib ".a" [
                        make rebmake/ext-static-class [
                            output: lib
                        ]
                    ][
                        make rebmake/ext-dynamic-class [
                            output: lib
                        ]
                    ]
                ][
                    lib
                ]
            ]
        ]

        for-each word [includes searches libraries] [
            append get in app-config word
                opt get in cfg-tcc word
        ]
        append app-config/definitions [ {WITH_TCC} ]
        append file-base/generated [
            %tmp-symbols.c
            %tmp-embedded-header.c
        ]
    ]
    find? [no off false #[false] _] user-config/with-tcc [
        ;pass
    ]
    true [
        fail [
            "WITH-TCC must be yes or no]"
            "not" (user-config/with-tcc)
        ]
    ]
]

assert-no-blank-inside: proc [
    block [block! blank!]
    <local> e
][
    if blank? block [leave]

    for-each e block [
        if blank? e [
            dump block
            fail "No blanks allowed"
        ]
    ]
]

print/only ["Sanity checking on user config ... "]
;add user settings
for-each word [definitions includes cflags libraries ldflags][
    assert-no-blank-inside get in user-config word
]
append app-config/definitions opt user-config/definitions
append app-config/includes opt user-config/includes
append app-config/cflags opt user-config/cflags
append app-config/libraries opt user-config/libraries
append app-config/ldflags opt user-config/ldflags
print ["Good"]

;add system settings
add-app-def: adapt specialize :append [series: app-config/definitions] [
    value: flatten/deep reduce bind value system-definitions
]
add-app-cflags: adapt specialize :append [series: app-config/cflags] [
    value: if block? value [flatten/deep reduce bind value compiler-flags]
]
add-app-lib: adapt specialize :append [series: app-config/libraries] [
    value: either block? value [
        value: flatten/deep reduce bind value system-libraries
        map-each w flatten value [
            make rebmake/ext-dynamic-class [
                output: w
            ]
        ]
    ][
        assert [any-string? value]
        make rebmake/ext-dynamic-class [
            output: value
        ]
    ]
]

add-app-ldflags: adapt specialize :append [series: app-config/ldflags] [
    value: if block? value [flatten/deep reduce bind value linker-flags]
]

print/only ["Sanity checking on system config ... "]
for-each word [definitions cflags libraries ldflags][
    assert-no-blank-inside get in system-config word
]
add-app-def copy system-config/definitions
add-app-cflags copy system-config/cflags
add-app-lib copy system-config/libraries
add-app-ldflags copy system-config/ldflags
print ["Good"]

print/only ["Sanity checking on app config ... "]
assert-no-blank-inside app-config/definitions
assert-no-blank-inside app-config/includes
assert-no-blank-inside app-config/cflags
assert-no-blank-inside app-config/libraries
assert-no-blank-inside app-config/ldflags
print ["Good"]

print ["definitions:" mold app-config/definitions]
print ["includes:" mold app-config/includes]
print ["libraries:" mold app-config/libraries]
print ["cflags:" mold app-config/cflags]
print ["ldflags:" mold app-config/ldflags]
print ["debug:" mold app-config/debug]
print ["optimization:" mold app-config/optimization]

append app-config/definitions reduce [
    unspaced ["TO_" uppercase to-string system-config/os-base]
    unspaced ["TO_" uppercase replace/all to-string system-config/os-name "-" "_"]
]

libr3-core: make rebmake/object-library-class [
    name: 'libr3-core
    definitions: join-of ["REB_API"] app-config/definitions
    includes: copy app-config/includes ;might be modified by the generator, thus copying
    cflags: copy app-config/cflags ;might be modified by the generator, thus copying
    optimization: app-config/optimization
    debug: app-config/debug
    depends: map-each w append-of file-base/core file-base/generated [
        gen-obj/dir w "../src/core/"
    ]
]

os-file-block: get bind
    (to word! append-of "os-" system-config/os-base)
    file-base

remove-each plus os-file-block [plus = '+] ;remove the '+ sign, we don't care here
remove-each plus file-base/os [plus = '+] ;remove the '+ sign, we don't care here
libr3-os: make libr3-core [
    name: 'libr3-os
    definitions: join-of ["REB_CORE"] app-config/definitions
    includes: copy app-config/includes ;might be modified by the generator, thus copying
    cflags: copy app-config/cflags ;might be modified by the generator, thus copying
    depends: map-each s append copy file-base/os os-file-block [
        gen-obj/dir s "../src/os/"
    ]
]

pthread: make rebmake/ext-dynamic-class [
    output: %pthread
    flags: [static]
]

;extensions
builtin-extensions: copy available-extensions
dynamic-extensions: make block! 8
for-each [action name modules] user-config/extensions [
    switch/default action [
        + [; builtin
            ;pass, default action
        ]
        * - [
            item: _
            for-next builtin-extensions [
                if builtin-extensions/1/name = name [
                    item: take builtin-extensions
                    if all [
                        not item/loadable
                        action = '*
                    ][
                        fail [{Extension} name {is not dynamically loadable}]
                    ]
                ]
            ]
            unless item [
                fail [{Unrecognized extension name:} name]
            ]

            if action = '* [;dynamic extension
                selected-modules: either blank? modules [
                    ; all modules in the extension
                    item/modules
                ][
                    map-each m item/modules [
                        if find? modules m/name [
                            m
                        ]
                    ]
                ]

                if empty? selected-modules [
                    fail [
                        {No modules are selected,}
                        {check module names or use '-' to remove}
                    ]
                ]
                item/modules: selected-modules
                append dynamic-extensions item
            ]
        ]
    ][
        fail ["Unrecognized extension action:" mold action]
    ]
]

print ["Builtin extensions"]
for-each ext builtin-extensions [
    print/only unspaced ["ext: " ext/name ": ["]
    for-each mod ext/modules [
        print/only [mod/name space]
    ]
    print ["]"]
]
print ["Dynamic extensions"]
for-each ext dynamic-extensions [
    print/only unspaced ["ext: " ext/name ": ["]
    for-each mod ext/modules [
        print/only [mod/name space]
    ]
    print ["]"]
]

all-extensions: join-of builtin-extensions dynamic-extensions

add-project-flags: proc [
    project [object!]
    /I includes
    /D definitions
    /c cflags
    /O optimization
    /g debug
][
    assert [
        find? [
            dynamic-library-class
            object-library-class
            static-library-class
            application-class
        ] project/class-name
    ]

    if D [
        assert-no-blank-inside definitions
        either block? project/definitions [
            append project/definitions definitions
        ][
            project/definitions: definitions
        ]
    ]

    if I [
        assert-no-blank-inside includes
        either block? project/includes [
            either locked? project/includes [
                project/includes: join-of project/includes includes
            ][
                append project/includes includes
            ]
        ][
            project/includes: includes
        ]
    ]
    if c [
        assert-no-blank-inside cflags
        either block? project/cflags [
            append project/cflags cflags
        ][
            project/cflags: cflags
        ]
    ]
    if g [project/debug: debug]
    if O [project/optimization: optimization]
]

process-module: func [
    mod [object!]
    <local>
    s
    ret
][
    assert [mod/class-name = 'module-class]
    assert-no-blank-inside mod/includes
    assert-no-blank-inside mod/definitions
    assert-no-blank-inside mod/depends
    if block? mod/libraries [assert-no-blank-inside mod/libraries]
    if block? mod/cflags [assert-no-blank-inside mod/cflags]
    ret: make rebmake/object-library-class [
        name: mod/name
        depends: map-each s (append reduce [mod/source] opt mod/depends) [
            case [
                any [file? s block? s][
                    gen-obj/dir s "../src/extensions/"
                ]
                all [object? s
                    find? [
                        object-library-class
                        object-file-class
                    ] s/class-name
                ][
                    s
                    ;object-library-class has already been taken care of above
                    ;if s/class-name = 'object-file-class [s]
                ]
                true [
                    dump s
                    fail [type-of s "can't be a dependency of a module"]
                ]
            ]
        ]
        libraries: to-value if mod/libraries [
            map-each lib mod/libraries [
                case [
                    file? lib [
                        make rebmake/ext-dynamic-class [
                            output: lib
                        ]
                    ]
                    all [object? lib
                        find? [
                            ext-dynamic-class
                            ext-static-class
                        ] lib/class-name
                    ][
                        lib
                    ]
                    true [
                        dump lib
                        fail "unrecognized module library"
                    ]
                ]
            ]
        ]

        includes: mod/includes
        definitions: mod/definitions
        cflags: mod/cflags
        searches: mod/searches
    ]

    ret
]

ext-objs: make block! 8
for-each ext builtin-extensions [
    mod-obj: _
    for-each mod ext/modules [
        ;extract object-library, because an object-library can't depend on another object-library
        if all [block? mod/depends
            not empty? mod/depends][
            append ext-objs map-each s mod/depends [
                if all [
                    object? s
                    s/class-name = 'object-library-class
                ][
                    s
                ]
            ]
        ]

        append ext-objs mod-obj: process-module mod
        if mod-obj/libraries [
            assert-no-blank-inside mod-obj/libraries
            append app-config/libraries mod-obj/libraries
        ]

        if mod/searches [
            assert-no-blank-inside mod/searches
            append app-config/searches mod/searches
        ]

        if mod/ldflags [
            if block? mod/ldflags [assert-no-blank-inside mod/ldflags]
            append app-config/ldflags mod/ldflags
        ]

        ; Modify module properties
        add-project-flags/I/D/c/O/g mod-obj
            app-config/includes
            join-of ["REB_API"] app-config/definitions
            app-config/cflags
            app-config/optimization
            app-config/debug
    ]
    if ext/source [
        append any [all [mod-obj mod-obj/depends] ext-objs] gen-obj/dir ext/source "../src/extensions/"
    ]
]

vars: reduce [
    reb-tool: make rebmake/var-class [
        name: {REBOL_TOOL}
        value: any [
                user-config/rebol-tool
                unspaced [{./r3-make} rebmake/target-platform/exe-suffix]
            ]
    ]
    make rebmake/var-class [
        name: {REBOL}
        value: {$(REBOL_TOOL) -qs}
    ]
    make rebmake/var-class [
        name: {T}
        value: {../src/tools}
    ]
    make rebmake/var-class [
        name: {GIT_COMMIT}
        default: either user-config/git-commit [user-config/git-commit][{unknown}]
    ]
]

prep: make rebmake/entry-class [
    target: "prep"
    commands: compose [
        {$(REBOL) make-natives.r}
        {$(REBOL) make-headers.r}
        (unspaced [ {$(REBOL) make-boot.r OS_ID=} system-config/id { GIT_COMMIT=$(GIT_COMMIT)}])
        {$(REBOL) make-host-init.r}
        {$(REBOL) make-os-ext.r}
        {$(REBOL) make-host-ext.r}
        {$(REBOL) make-reb-lib.r}
        (
            cmds: make block! 8
            for-each ext all-extensions [
                for-each mod ext/modules [
                    append cmds unspaced [
                        {$(REBOL) make-ext-natives.r MODULE=} mod/name { SRC=../src/extensions/}
                            case [
                                file? mod/source [
                                    mod/source
                                ]
                                block? mod/source [
                                    first find mod/source file!
                                ]
                                true [
                                    fail "mod/source must be BLOCK! or FILE!"
                                ]
                            ]
                        { OS_ID=} system-config/id
                    ]
                ]
                if ext/init [
                    append cmds unspaced [
                        {$(REBOL) make-ext-init.r SRC=../src/extensions/} ext/init
                    ]
                ]
            ]
            cmds
        )
        (
            unspaced [
                {$(REBOL) make-boot-ext-header.r EXTENSIONS=}
                delimit map-each ext builtin-extensions [
                    to string! ext/name
                ] #","
            ]
        )
        (
            if cfg-tcc [
                sys-core-i: make rebmake/object-file-class [
                    compiler: make rebmake/tcc [
                        exec-file: cfg-tcc/exec-file
                    ]
                    output: %../src/include/sys-core.i
                    source: %../src/include/sys-core.h
                    definitions: join-of app-config/definitions [ {REN_C_STDIO_OK} ]
                    includes: append-of app-config/includes [%../external/tcc %../external/tcc/include]
                    cflags: append-of append-of [ {-dD} {-nostdlib} ] opt cfg-ffi/cflags opt cfg-tcc/cpp-flags
                ]
                reduce [
                    sys-core-i/command/E
                    {$(REBOL) make-embedded-header.r}
                ]
            ]
        )
    ]
    depends: reduce [
        reb-tool
    ]
]

; Analyze what directories were used in this build's entry from %file-base.r
; to add those obj folders.  So if the `%generic/host-memory.c` is listed,
; this will make sure `%objs/generic/` is in there.

add-new-obj-folders: procedure [
    objs
    folders
    <local>
    lib
    obj
][
    for-each lib objs [
        switch/default lib/class-name [
            object-file-class [
                lib: reduce [lib]
            ]
            object-library-class [
                lib: lib/depends
            ]
        ][
            dump lib
            fail ["unexpected class"]
        ]

        for-each obj lib [
            dir: first split-path obj/output
            unless find? folders dir [
                append folders dir
            ]
        ]
    ]
]

folders: copy [%objs/]
for-each file os-file-block [
    ;
    ; For better or worse, original R3-Alpha didn't use FILE! in %file-base.r
    ; for filenames.  Note that `+` markers should be removed by this point.
    ;
    assert [any [word? file | path? file]]
    file: join-of %objs/ file
    assert [file? file]

    path: first split-path file
    if not find folders path [
        append folders path
    ]
]
add-new-obj-folders ext-objs folders

app: make rebmake/application-class [
    name: 'main
    output: %r3 ;no suffix
    depends: compose [
        (libr3-core)
        (libr3-os)
        (ext-objs)
        (app-config/libraries)
    ]
    post-build-commands: either cfg-symbols [
        _
    ][
        reduce [
            make rebmake/cmd-strip-class [
                file: join-of output opt rebmake/target-platform/exe-suffix
            ]
        ]
    ]

    searches: app-config/searches
    ldflags: app-config/ldflags
    cflags: app-config/cflags
    optimization: app-config/optimization
    debug: app-config/debug
    includes: app-config/includes
    definitions: app-config/definitions
]


dynamic-libs: make block! 8
ext-libs: make block! 8
ext-ldflags: make block! 8
ext-dynamic-objs: make block! 8
for-each ext dynamic-extensions [
    mod-objs: make block! 8
    for-each mod ext/modules [
        append mod-objs mod-obj: process-module mod
        append ext-libs opt mod-obj/libraries

        if mod/ldflags [
            assert-no-blank-inside mod/ldflags
            append ext-ldflags mod/ldflags
        ]

        ; Modify module properties
        add-project-flags/I/D/c/O/g mod-obj
            app-config/includes
            join-of ["EXT_DLL"] app-config/definitions
            app-config/cflags
            app-config/optimization
            app-config/debug
    ]

    append ext-dynamic-objs copy mod-objs

    if ext/source [
        append mod-objs gen-obj/dir/D ext/source "../src/extensions/" ["EXT_DLL"]
    ]
    append dynamic-libs ext-proj: make rebmake/dynamic-library-class [
        name: join-of either system-config/os-base = 'windows ["r3-"]["libr3-"]
            lowercase to string! ext/name
        output: to file! name
        depends: append compose [
            (mod-objs)
            (app) ;all dynamic extensions depend on r3
            (app-config/libraries)
        ] ext-libs

        post-build-commands: either cfg-symbols [
            _
        ][
            reduce [
                make rebmake/cmd-strip-class [
                    file: join-of output opt rebmake/target-platform/dll-suffix
                ]
            ]
        ]

        ldflags: append-of either empty? ext-ldflags [[]][ext-ldflags] [<gnu:-Wl,--as-needed>]
    ]

    add-project-flags/I/D/c/O/g ext-proj
        app-config/includes
        join-of ["EXT_DLL"] app-config/definitions
        app-config/cflags
        app-config/optimization
        app-config/debug

    add-new-obj-folders mod-objs folders
]

top: make rebmake/entry-class [
    target: "top"
    depends: flatten reduce [app dynamic-libs]
]

t-folders: make rebmake/entry-class [
    target: "folders"
    commands: map-each dir sort folders [;sort it so that the parent folder gets created first
        make rebmake/cmd-create-class compose [
            file: (dir)
        ]
    ]
]

clean: make rebmake/entry-class [
    target: "clean"
    commands: append flatten reduce [
        make rebmake/cmd-delete-class [file: %objs/]
        make rebmake/cmd-delete-class [file: join-of %r3 opt rebmake/target-platform/exe-suffix]
        make rebmake/cmd-delete-class [file: %../src/include/tmp-*.h]
        use [s][
            map-each s [
                %host-lib.h
                %host-table.inc
                %reb-evtypes.h
                %reb-lib.h
                %tmp-reb-lib-table.inc
                %reb-types.h
            ][
                make rebmake/cmd-delete-class [file: join-of %../src/include/ s]
            ]
        ]
        use [s][
            map-each s file-base/generated [
                make rebmake/cmd-delete-class [file: join-of %../src/core/ s]
            ]
        ]
        use [s][
            map-each s dynamic-libs [
                make rebmake/cmd-delete-class [file: join-of s/output opt rebmake/target-platform/dll-suffix]
            ]
        ]
    ] if system-config/os-base != 'Windows [
        [
            {find ../src -name 'tmp-*' -exec rm -f {} \;}
            {grep -l "AUTO-GENERATED FILE" ../src/include/*.h |grep -v sys-zlib.h|xargs rm 2>/dev/null || true}
        ]
    ]
]

check: make rebmake/entry-class [
    target: "check"
    depends: join-of dynamic-libs app
    commands: append reduce [
        make rebmake/cmd-strip-class [
            file: join-of app/output opt rebmake/target-platform/exe-suffix
        ]
    ] map-each s dynamic-libs [
        make rebmake/cmd-strip-class [
            file: join-of s/output opt rebmake/target-platform/dll-suffix
        ]
    ]
]

solution: make rebmake/solution-class [
    name: 'app
    depends: flatten reduce [
        vars
        top
        t-folders
        prep
        ext-objs
        libr3-core
        libr3-os
        app
        dynamic-libs
        ext-dynamic-objs
        check
        clean
    ]
    debug: app-config/debug
]

target: user-config/target
if not block? target [target: reduce [target]]
forall target [
    switch/default target/1 targets [fail [
        newline
        newline
        "UNSUPPORTED TARGET"
        user-config/target
        newline
        "TRY --HELP TARGETS"
        newline
    ] ]
]
