REBOL []
do %common.r
do %systems.r
file-base: make object! load %file-base.r

rebmake: import %rebmake.r

config-dir: %../../make
print pwd
user-config: make object! load config-dir/default-config.r

; Load user defined config.r
args: parse-args system/options/args
if select args 'CONFIG [
    user-config: make user-config load config-dir/(args/CONFIG)
]

; Allow any of the settings in user-config to be overwritten by command line
; options
for-each [name value] args [
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
                        fail ["Selected extensions must be a block, not" (type-of user-ext)]
                    ]
                    user-config/extensions: user-ext
                ]
            ]
        ]
    ][
        set in user-config (to-word replace to string! name #"_" #"-")
            load value
    ]
]

dump user-config

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

to-obj-path: func [
    file [any-string!]
][
    join-of %objs/ replace (last split-path file) ".c" rebmake/target-platform/obj-suffix
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
        append flags []
        for-each flag next s [
            append flags opt select [
                <no-uninitialized> [
                    <gnu:-Wno-uninitialized>
                    ;-Wno-unknown-warning seems to only modify the immidiately following option
                    ;<gnu:-Wno-unknown-warning> <gnu:-Wno-maybe-uninitialized>
                ]
                <implicit-fallthru> [
                    <gnu:-Wno-unknown-warning> <gnu:-Wno-implicit-fallthrough>
                ]
                <no-unused-parameter> [<gnu:-Wno-unused-parameter>]
                <no-shift-negative-value> [<gnu:-Wno-shift-negative-value>]
            ] flag
        ]
        s: s/1
    ]

    make rebmake/object-file-class compose/only [
        source: to-file join-of either dir [directory][%../src/] s
        output: join-of %objs/ replace to string! s ".c" rebmake/target-platform/obj-suffix
        cflags: either empty? flags [_] [flags]
        definitions: (to-value :definitions)
    ]
]

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
            append project/includes includes
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
    ;dump mod

    assert-no-blank-inside mod/includes
    assert-no-blank-inside mod/definitions
    assert-no-blank-inside mod/depends
    if block? mod/libraries [assert-no-blank-inside mod/libraries]
    assert-no-blank-inside mod/cflags
    ret: make rebmake/object-library-class [
        name: mod/name
        depends: map-each s (append reduce [mod/source] opt mod/depends) [
            ;dump s
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
                        ] lib/class-name [
                            lib
                        ]
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
    ]

    ret
]

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
            ;if blank? obj [continue]
            ;dump obj
            dir: first split-path obj/output
            ;dump dir
            unless find? folders dir [
                append folders dir
            ]
        ]
    ]
]

set [cc cc-exe] any [
    find user-config/toolset 'gcc
    find user-config/toolset 'cl
]

set [linker linker-exe] any [
    find user-config/toolset 'ld
    find user-config/toolset 'link
]

set [strip strip-exe] any [
    find user-config/toolset 'strip
]

switch cc [
    gcc [
        rebmake/default-compiler: rebmake/gcc
        set-exec-path rebmake/gcc cc-exe

        if linker != 'ld [
            fail ["GCC can only work with ld, not" linker]
        ]
        rebmake/default-linker: rebmake/ld
        set-exec-path rebmake/ld linker-exe
    ]
    cl [
        rebmake/default-compiler: rebmake/cl
        set-exec-path rebmake/cl cc-exe

        if linker != 'link [
            fail ["CL can only work with link, not" linker]
        ]
        rebmake/default-linker: rebmake/link
        set-exec-path rebmake/link linker-exe
    ]
][
    fail ["Unrecognized compiler (gcc or cl):" cc]
]

if strip [
    rebmake/default-strip: rebmake/strip
    rebmake/default-strip/options: [<gnu:-S> <gnu:-x> <gnu:-X>]
    set-exec-path rebmake/default-strip strip-exe
]

system-config: config-system user-config/os-id
dump system-config
rebmake/set-target-platform system-config/os-base

add-app-def: adapt specialize :append [series: app-config/definitions] [
    value: flatten/deep reduce bind value system-definitions
]
add-app-cflags: adapt specialize :append [series: app-config/cflags] [
    value: if block? value [flatten/deep reduce bind value compiler-flags]
]
add-app-lib: adapt specialize :append [series: app-config/libraries] [
    value: either block? value [
        value: flatten/deep reduce bind value system-libraries
        ;dump value
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

available-modules: reduce [
    ;name module-file other-files
    mod-crypt: make module-class [
        name: 'Crypt
        source: %crypt/mod-crypt.c
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

    mod-lodepng: make module-class [
        name: 'LodePNG
        source: %png/mod-lodepng.c
        depends: [
            %png/lodepng.c
        ]
    ]

    mod-upng: make module-class [
        name: 'uPNG
        source: %png/u-png.c
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
        source: %locale/mod-locale.c
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
                <no-unused-parameter>
                <no-shift-negative-value>
            ]
        ]
    ]

    mod-uuid: make module-class [
        name: 'UUID
        source: %uuid/mod-uuid.c
        includes: copy [%../src/extensions/uuid/libuuid]
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
]

;dump mod-uuid

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

    ext-png: make extension-class [
        name: 'PNG
        loadable: no ; Depends on symbols, like z_inflate(End), that are not exported
        modules: reduce [
            mod-lodepng
            mod-upng
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
]

;dump ext-uuid
;for-each m ext-uuid/modules [
;    dump m
;]

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
        app-config/debug: on
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

        ; Note: The C and C++ user-config/standards do not dictate if `char` is signed
        ; or unsigned.  Lest anyone think all environments have settled on
        ; them being signed, they're not... Android NDK uses unsigned:
        ;
        ; http://stackoverflow.com/questions/7414355/
        ;
        ; In order to give the option some exercise, make the C++11 builds
        ; and above use unsigned chars.
        ;
        cfg-cplusplus: true
        reduce [
            <gnu:-x c++>
            to tag! unspaced ["gnu:--std=" user-config/standard]
            <gnu:-funsigned-char>
            <msc:/TP>
            to tag! unspaced ["msc:/std:" lowercase to string! user-config/standard];only supports "c++14/17/latest"
        ]
    ]
][
    fail [
        "STANDARD should be one of [c gnu89 gnu99 c99 c11 c++ c++11 c++14 c++17 c++latest]"
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
            <gnu:-Wsign-compare>
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
            (either cfg-cplusplus [<gnu:-Wcast-qual>] [<gnu:-Wno-cast-qual>])

            ; The majority of Rebol's C code was written with little
            ; attention to overflow in arithmetic.  Frequently REBUPT
            ; is assigned to REBCNT, size_t to REBYTE, etc.  The issue
            ; needs systemic review, but will be most easy to do so when
            ; the core is broken out fully from less critical code
            ; in optional extensions.
            ;
            ;<gnu:-Wstrict-overflow=5>
            <gnu:-Wno-conversion>
            <gnu:-Wno-strict-overflow>
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

;libffi
libffi-cflags: _
either block? user-config/with-ffi [
    ffi-proto: make object! [
        cflags:
        includes:
        definitions:
        ldflags:
        libraries:
        searches: _
    ]
    ffi: make ffi-proto user-config/with-ffi
    for-each word words-of ffi-proto [
        append get in app-config word
            opt get in ffi word
    ]
    append app-config/definitions [ {HAVE_LIBFFI_AVAILABLE} ]
][
    switch/default user-config/with-ffi [
        static dynamic [
            for-each var [includes cflags searches ldflags][
                x: rebmake/pkg-config
                    any [user-config/pkg-config {pkg-config}]
                    var
                    %libffi
                ;dump x
                unless empty? x [
                    append (get in app-config var) x
                ]
            ]

            libs: rebmake/pkg-config
                any [user-config/pkg-config {pkg-config}]
                'libraries
                %libffi

            append app-config/libraries map-each lib libs [
                make rebmake/ext-dynamic-class [
                    output: lib
                    flags: either user-config/with-ffi = 'static [[static]][_]
                ]
            ]
            append app-config/definitions [ {HAVE_LIBFFI_AVAILABLE} ]
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


prin ["Sanity checking on user config ... "]
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
prin ["Sanity checking on system config ... "]
for-each word [definitions cflags libraries ldflags][
    assert-no-blank-inside get in system-config word
]
add-app-def copy system-config/definitions
add-app-cflags copy system-config/cflags
add-app-lib copy system-config/libraries
add-app-ldflags copy system-config/ldflags
print ["Good"]

prin ["Sanity checking on app config ... "]
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
                ;dump item
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
    prin unspaced ["ext: " ext/name ": ["]
    for-each mod ext/modules [
        prin [mod/name space]
    ]
    print ["]"]
]
print ["Dynamic extensions"]
for-each ext dynamic-extensions [
    prin unspaced ["ext: " ext/name ": ["]
    for-each mod ext/modules [
        prin [mod/name space]
    ]
    print ["]"]
]

all-extensions: join-of builtin-extensions dynamic-extensions

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

        if mod/ldflags [
            ;dump mod/ldflags
            assert-no-blank-inside mod/ldflags
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
        {$(REBOL) $T/make-natives.r}
        {$(REBOL) $T/make-headers.r}
        (unspaced [ {$(REBOL) $T/make-boot.r OS_ID=} system-config/id { GIT_COMMIT=$(GIT_COMMIT)}])
        {$(REBOL) $T/make-host-init.r}
        {$(REBOL) $T/make-os-ext.r}
        {$(REBOL) $T/make-host-ext.r}
        {$(REBOL) $T/make-reb-lib.r}
        (
            cmds: make block! 8
            for-each ext all-extensions [
                for-each mod ext/modules [
                    append cmds unspaced [
                        {$(REBOL) $T/make-ext-natives.r MODULE=} mod/name { SRC=../extensions/} mod/source { OS_ID=} system-config/id
                    ]
                ]
                if ext/init [
                    append cmds unspaced [
                        {$(REBOL) $T/make-ext-init.r SRC=../extensions/} ext/init
                    ]
                ]
            ]
            cmds
        )
        (
            unspaced [
                {$(REBOL) $T/make-boot-ext-header.r EXTENSIONS=}
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
                    cflags: append-of append-of [ {-dD} {-nostdlib} ] opt libffi-cflags opt cfg-tcc/cpp-flags
                ]
                reduce [
                    sys-core-i/command/E
                    {$(REBOL) $T/make-embedded-header.r}
                ]
            ]
        )
    ]
    depends: reduce [
        reb-tool
    ]
]

;print mold prep/commands

folders: copy [%objs/ %objs/generic/]
if find? [linux android osx] system-config/os-base [append folders [%objs/posix/]]
if find? [android] system-config/os-base [append folders [%objs/linux/]]
append folders join-of %objs/ [system-config/os-base #"/"]

add-new-obj-folders ext-objs folders
;print ["ext-objs: (" length ext-objs ")" mold ext-objs]
;print ["app-config/ldflags:" mold app-config/ldflags]
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
        ;dump mod
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

;dump folders
;print ["dynamic-libs:" mold dynamic-libs]

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
                %reb-evtypes.h
                %reb-lib.h
                %reb-lib-lib.h
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

;print ["check:" mold check]

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

switch/default user-config/target [
    clean [
        change-dir %../../make
        rebmake/execution/run make rebmake/solution-class [
            depends: reduce [
                clean
            ]
        ]
    ]
    execution [
        change-dir %../../make
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
        rebmake/makefile/generate %../../make/makefile solution
    ]
    nmake [
        rebmake/nmake/generate %../../make/makefile solution
    ]
    vs2017
    visual-studio [
        rebmake/visual-studio/generate/(all [system-config/os-name = 'Windows-x86 'x86]) %../../make solution
    ]
    vs2015 [
        rebmake/vs2015/generate/(all [system-config/os-name = 'Windows-x86 'x86]) %../../make solution
    ]
][
    fail [
        "Unsupported target (execution, makefile or nmake):"
        user-config/target
    ]
]
