REBOL []

; **SENSITIVE MAGIC LINE OF VOODOO** - see "Usage" in %bootstrap-shim.r
(change-dir do join copy system/script/path %tools/bootstrap-shim.r)

do <tools/common.r>

do <tools/platforms.r>
file-base: make object! load <tools/file-base.r>

; See notes on %rebmake.r for why it is not a module at this time, due to the
; need to have it inherit the shim behaviors of IF, CASE, FILE-TO-LOCAL, etc.
;
; rebmake: import <tools/rebmake.r>
do <tools/rebmake.r>

;;;; GLOBALS

if what-dir = repo-dir [
    print ["BUILDING FROM REPO-DIR:" repo-dir]
    print "(...so assuming you want build products in /BUILD subdirectory)"
    output-dir: join repo-dir %build/
    make-dir output-dir
] else [
    output-dir: what-dir
]

src-dir: join repo-dir %src/

user-config: make object! load join repo-dir %configs/default-config.r

;;;; PROCESS ARGS
; args are:
; [OPTION | COMMAND] ...
; COMMAND = WORD
; OPTION = 'NAME=VALUE' | 'NAME: VALUE'
args: parse-args system/options/args
; now args are ordered and separated by bar:
; [NAME VALUE ... '| COMMAND ...]
either commands: null-to-blank find args '| [
    options: copy/part args commands
    commands: next commands
] [options: args]
; now args are splitted in options and commands

for-each [name value] options [
    switch name [
        'CONFIG 'LOAD 'DO [
            user-config: make user-config load to-file value
        ]
        'EXTENSIONS [
            ; [+|-|*] [NAME {+|-|*|[modules]}]...
            use [ext-file user-ext][
                user-ext: load value
                if word? user-ext [user-ext: reduce [user-ext]]
                if not block? user-ext [
                    fail [
                        "Selected extensions must be a block, not"
                        (type of user-ext)
                    ]
                ]
                all [
                    not empty? user-ext
                    find [+ - *] user-ext/1
                ] then [
                    value: take user-ext
                ]
                for-each [name value] user-ext [
                    user-config/extensions/:name: value
                ]
            ]
        ]
        default [
            set in user-config (to-word replace/all to text! name #"_" #"-")
                load value
        ]
    ]
]

; process commands
if not empty? maybe+ commands [user-config/target: load commands]

;;;; MODULES & EXTENSIONS
system-config: config-system user-config/os-id
rebmake/set-target-platform system-config/os-base

to-obj-path: func [
    file [any-string!]
    <local> ext
][
    ext: find/last file #"."
    if not ext [
        print ["File with no extension" mold file]
    ]
    remove/part ext (length of ext)
    join %objs/ head-of append ext rebmake/target-platform/obj-suffix
]

gen-obj: func [
    s
    /dir directory [any-string!]
    /D definitions [block!]
    /I includes [block!]
    /F cflags [block!]
    /main ; for main object
    <local>
    flags
][
    flags: make block! 8

    ; Microsoft shouldn't bother having the C warning that foo() in standard
    ; C doesn't mean the same thing as foo(void), when in their own published
    ; headers (ODBC, Windows.h) they treat them interchangeably.  See for
    ; instance EnableMouseInPointerForThread().  Or ODBCGetTryWaitValue().
    ;
    ; Just disable the warning, and hope the Linux build catches most of it.
    ;
    ;     'function' : no function prototype given:
    ;     converting '()' to '(void)'

    append flags <msc:/wd4255>

    ; The May 2018 update of Visual Studio 2017 added a warning for when you
    ; use an #ifdef on something that is #define'd, but 0.  Then the internal
    ; %yvals.h in MSVC tests #ifdef __has_builtin, which has to be defined
    ; to 0 to work in MSVC.  Disable the warning for now.
    ;
    append flags <msc:/wd4574>

    ; !!! MSVC added warnings for "Spectre mitigation".  The main branch of
    ; Ren-C worked around these to try and avoid performance hits, but
    ; patching that onto the older R3C would be work for little benefit...as
    ; it only matters if /Qspectre builds are done of R3C (which they probably
    ; will not)...and would only mean a small slowdown if they were.  Disable
    ; these warnings.

    ; Building as C++ using nmake seems to trigger this warning to say there
    ; is no exception handling policy in place.  We don't use C++ exceptions
    ; in the C++ build, so we ignore the warning...but if exceptions were used
    ; there'd have to be an implementation choice made there.
    ;
    append flags <msc:/wd4577>

    ; There's a warning on reinterpret_cast between related classes, trying to
    ; suggest you use static_cast instead.  This complicates the `cast` macro
    ; tricks, which just use reinterpret_cast.
    ;
    append flags <msc:/wd4946>

    ; There's a warning on reinterpret_cast between related classes, trying to
    ; suggest you use static_cast instead.  This complicates the `cast` macro
    ; tricks, which just use reinterpret_cast.
    ;
    append flags <msc:/wd5045>

    ; !!! Using MSVC 2019 to try and build an upgraded MSVC 2017 solution
    ; seems to trigger problems with #pragma warning(push) and warning(pop),
    ; which developer forums confirm is some kind of Windows platform issue.
    ; This only happens in backwards-builds for R3C, disable warning pair.
    ;
    append flags <msc:/wd5031>
    append flags <msc:/wd5032>

    ;   Arithmetic overflow: Using operator '*' on a 4 byte value
    ;   and then casting the result to a 8 byte value. Cast the
    ;   value to the wider type before calling operator '*' to
    ;   avoid overflow
    ;
    ; Overflow issues are widespread in Rebol, and this warning is not
    ; particularly high priority in the scope of what the project is
    ; exploring.  Disable for now.
    ;
    <msc:/wd26451>

    ; Later MSVCs complain if you use an `enum` instead of an `enum class`.
    ; Since Ren-C wants to build C as C++, this isn't a useful warning.
    ;
    append flags <msc:/wd26812>

    if block? s [
        for-each flag next s [
            append flags degrade switch flag [
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
                <no-sign-compare> [
                    [
                        <gnu:-Wno-sign-compare>
                        <msc:/wd4388>
                        <msc:/wd4018>  ; a 32-bit variant of the error
                    ]
                ]
                <implicit-fallthru> [
                    [
                        <gnu:-Wno-unknown-warning>
                        <gnu:-Wno-implicit-fallthrough>
                        <msc:/wd5262>
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
                    '~void~
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

                default [
                    ensure [text! tag!] flag
                ]
            ]
        ]
        s: s/1
    ]

    if F [append flags :cflags]

    make rebmake/object-file-class compose/only [
        source: to-file case [
            dir [join directory s]
            main [s]
            default [join src-dir s]
        ]
        output: to-obj-path to text! ;\
            either main [
                join %main/ (last ensure path! s)
            ] [s]
        cflags: either empty? flags [_] [flags]
        definitions: (get 'definitions else [_])
        includes: (get 'includes else [_])
    ]
]

extension-class: make object! [
    class: #extension
    name: _
    modules: _
    source: _ ; main script
    depends: _ ; additional C files compiled in
    requires: _ ; it might require other extensions

    includes: _
    definitions: _
    cflags: _

    searches: _
    libraries: _
    ldflags: _

    ;internal
    sequence: _ ; the sequence in which the extension should be loaded
    visited: false
]

available-extensions: copy []

; Discover extensions:
use [extension-dir entry][
    extension-dir: repo-dir/extensions/%
    for-each entry read extension-dir [
        ;print ["entry:" mold entry]
        all [
            dir? entry
            find read rejoin [extension-dir entry] %make-spec.r
        ] then [
            append available-extensions make extension-class load rejoin [
                extension-dir entry/make-spec.r
            ]
        ]
    ]
]

extension-names: map-each x available-extensions [to-lit-word x/name]

;;;; TARGETS

targets: [
    'clean [
        rebmake/execution/run make rebmake/solution-class [
            depends: reduce [
                clean
            ]
        ]
    ]
    'prep [
        rebmake/execution/run make rebmake/solution-class [
            depends: flatten reduce [
                vars
                prep
                t-folders
                dynamic-libs
            ]
        ]
    ]
    'app 'executable 'r3 [
        rebmake/execution/run make rebmake/solution-class [
            depends: flatten reduce [
                vars
                t-folders
                dynamic-libs
                app
            ]
        ]
    ]
    'library [
        rebmake/execution/run make rebmake/solution-class [
            depends: flatten reduce [
                vars
                t-folders
                dynamic-libs
                library
            ]
        ]
    ]
    'all 'execution [
        rebmake/execution/run make rebmake/solution-class [
            depends: flatten reduce [
                clean
                prep
                vars
                t-folders
                dynamic-libs
                app
            ]
        ]
    ]
    'makefile [
        rebmake/makefile/generate (join output-dir %makefile) solution
    ]
    'nmake [
        rebmake/nmake/generate (join output-dir %makefile) solution
    ]
]
target-names: make block! 16
for-each x targets [
    if lit-word? x [
        append target-names to word! x
        append target-names '|
    ] else [
        take/last target-names
        append target-names newline
    ]
]

;;;; GO!

set-exec-path: func [
    return: [~]
    tool [object!]
    path
][
    if path [
        if not file? path [
            fail "Tool path has to be a file!"
        ]
        tool/exec-file: path
    ]
]

parse2 user-config/toolset [
    opt some [
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
        | pos: (
            if not tail? pos [fail ["failed to parset toolset at:" mold pos]]
        )
    ]
]

; sanity checking the compiler and linker

rebmake/default-compiler: default [fail "Compiler is not set"]
rebmake/default-linker: default [fail "Default linker is not set"]

switch rebmake/default-compiler/name [
    'gcc [
        if rebmake/default-linker/name != 'ld [
            fail [
                "Incompatible compiler (GCC) and linker:"
                    rebmake/default-linker/name
            ]
        ]
    ]
    'clang [
        if not find [ld llvm-link] rebmake/default-linker/name [
            fail [
                "Incompatible compiler (CLANG) and linker:"
                rebmake/default-linker/name
            ]
        ]
    ]
    'cl [
        if rebmake/default-linker/name != 'link [
            fail [
                "Incompatible compiler (CL) and linker:"
                rebmake/default-linker/name
            ]
        ]
    ]

    fail ["Unrecognized compiler (gcc, clang or cl):" cc]
]

all [set? 'cc-exec  cc-exec] then [
    set-exec-path rebmake/default-compiler cc-exec
]
all [set? 'linker-exec  linker-exec] then [
    set-exec-path rebmake/default-linker linker-exec
]

app-config: make object! [
    cflags: make block! 8
    ldflags: make block! 8
    libraries: make block! 8
    debug: off
    optimization: 2
    definitions: copy []
    includes: reduce [src-dir/include %prep/include]
    searches: make block! 8
]

cfg-sanitize: false
cfg-symbols: false
switch user-config/debug [
    #[false] 'no 'false 'off 'none [
        append app-config/definitions ["NDEBUG"]
        app-config/debug: off
    ]
    #[true] 'yes 'true 'on [
        app-config/debug: on
    ]
    'asserts [
        ; /debug should only affect the "-g -g3" symbol inclusions in rebmake.
        ; To actually turn off asserts or other checking features, NDEBUG must
        ; be defined.
        ;
        app-config/debug: off
    ]
    'symbols [ ; No asserts, just symbols.
        app-config/debug: on
        append app-config/definitions ["NDEBUG"]
    ]
    'normal [
        cfg-symbols: true
        app-config/debug: on
    ]
    'sanitize [
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
    'callgrind [
        cfg-symbols: true
        append app-config/definitions ["NDEBUG"]
        append app-config/cflags "-g" ;; for symbols
        app-config/debug: off

        ; Include debugging features which do not in-and-of-themselves affect
        ; runtime performance (DEBUG_TRACK_CELLS would be an example of
        ; something that significantly affects runtime)
        ;
        append app-config/definitions ["DEBUG_STDIO_OK"]
        append app-config/definitions ["DEBUG_PROBE_OK"]

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

    fail ["unrecognized debug setting:" user-config/debug]
]

switch user-config/optimize [
    #[false] 'false 'no 'off 0 [
        app-config/optimization: false
    ]
    1 2 3 4 "s" "z" "g" 's 'z 'g [
        app-config/optimization: user-config/optimize
    ]
]

cfg-cplusplus: false
;standard
append app-config/cflags degrade switch user-config/standard [
    'c [
        '~void~
    ]
    'gnu89 'c99 'gnu99 'c11 [
        to tag! unspaced ["gnu:--std=" user-config/standard]
    ]
    'c++ [
        cfg-cplusplus: true
        [
            <gnu:-x c++>
            <msc:/TP>
        ]
    ]
    'c++98 'c++0x 'c++11 'c++14 'c++17 'c++latest [

        cfg-cplusplus: true
        compose [
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

            ; C++ standard, MSVC only supports "c++14/17/latest"
            ;
            (to tag! unspaced ["gnu:--std=" user-config/standard])
            (to tag! unspaced [
                "msc:/std:" lowercase to text! user-config/standard
            ])

            ; There is a shim for `nullptr` used, which is warned about even
            ; when building as pre-C++11 where it was introduced, unless you
            ; disable that warning.
            ;
            (if user-config/standard = 'c++98 [<gnu:-Wno-c++0x-compat>])

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

    fail [
        "STANDARD should be one of"
        "[c gnu89 gnu99 c99 c11 c++ c++11 c++14 c++17 c++latest]"
        "not" (user-config/standard)
    ]
]

; pre-vista switch
; Example. Mingw32 does not have access to windows console api prior to vista.
;
cfg-pre-vista: false
append app-config/definitions degrade switch user-config/pre-vista [
    #[true] 'yes 'on 'true [
        cfg-pre-vista: true
        compose [
            "PRE_VISTA"
        ]
    ]
    _ #[false] 'no 'off 'false [
        cfg-pre-vista: false
        '~void~
    ]

    fail ["PRE-VISTA [yes no \logic!\] not" (user-config/pre-vista)]
]

cfg-rigorous: false
append app-config/cflags degrade switch user-config/rigorous [
    #[true] 'yes 'on 'true [
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
                any [
                    cfg-cplusplus  not find [c gnu89] user-config/standard
                ] then [
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
            ; passed to REBLEN, where the signs mismatch.  Disable C4365:
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
            ; REBINT => Byte).  Disable C4242:
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

            ; implicit fall-through looking for [[fallthrough]]
            ;
            <msc:/wd5262>

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

            ; Implicit conversion from `int` to `REBD32`, possible loss.
            ;
            <msc:/wd5219>

            ; const variable is not used, triggers in MS's type_traits
            ;
            <msc:/wd5264>
        ]
    ]
    _ #[false] 'no 'off 'false [
        cfg-rigorous: false
        '~void~
    ]

    fail ["RIGOROUS [yes no \logic!\] not" (user-config/rigorous)]
]

append app-config/ldflags degrade switch user-config/static [
    _ 'no 'off 'false #[false] [
        ;pass
        '~void~
    ]
    'yes 'on #[true] [
        compose [
            <gnu:-static-libgcc>
            (if cfg-cplusplus [<gnu:-static-libstdc++>])
            (if cfg-sanitize [<gnu:-static-libasan>])
        ]
    ]

    fail ["STATIC must be yes, no or logic! not" (user-config/static)]
]


;add system settings
add-app-def: adapt specialize :append [series: app-config/definitions] [
    value: replace/all (
        flatten/deep reduce bind value system-definitions
    ) blank []
]
add-app-cflags: adapt specialize :append [series: app-config/cflags] [
    value: either block? value [
        replace/all (
            flatten/deep reduce bind value compiler-flags
        ) blank []
    ][
        assert [any-string? value]
    ]
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

write-stdout "Sanity checking on system config.."
; !!! TBD: checks
add-app-def copy system-config/definitions
add-app-cflags copy system-config/cflags
add-app-lib copy system-config/libraries
add-app-ldflags copy system-config/ldflags
print "..Good"

write-stdout "Sanity checking on app config.."
; !!! TBD: checks
print "..Good"

print ["definitions:" mold app-config/definitions]
print ["includes:" mold app-config/includes]
print ["libraries:" mold app-config/libraries]
print ["cflags:" mold app-config/cflags]
print ["ldflags:" mold app-config/ldflags]
print ["debug:" mold app-config/debug]
print ["optimization:" mold app-config/optimization]

append app-config/definitions reduce [
    unspaced ["TO_" uppercase to-text system-config/os-base]
    unspaced ["TO_" uppercase replace/all to-text system-config/os-name "-" "_"]
]

;; Add user settings
;;
append app-config/definitions maybe- user-config/definitions
append app-config/includes maybe- user-config/includes
append app-config/cflags maybe- user-config/cflags
append app-config/libraries maybe- user-config/libraries
append app-config/ldflags maybe- user-config/ldflags

libr3-core: make rebmake/object-library-class [
    name: 'libr3-core
    definitions: app-config/definitions

    ; might be modified by the generator, thus copying
    includes: append-of app-config/includes %prep/core

    ; might be modified by the generator, thus copying
    cflags: copy app-config/cflags

    optimization: app-config/optimization
    debug: app-config/debug
    depends: map-each w file-base/core [
        gen-obj/dir w src-dir/core/%
    ]
    append depends map-each w file-base/generated [
        gen-obj/dir w "prep/core/"
    ]
]

os-file-block: get bind
    (to word! append-of "os-" system-config/os-base)
    file-base

remove-each plus os-file-block [plus = '+] ;remove the '+ sign, we don't care here
remove-each plus file-base/os [plus = '+] ;remove the '+ sign, we don't care here

libr3-os: make libr3-core [
    name: 'libr3-os

    definitions: append copy ["REB_CORE"] app-config/definitions
    includes: append-of app-config/includes %prep/os ; generator may modify
    cflags: copy app-config/cflags ; generator may modify

    depends: map-each s append copy file-base/os os-file-block [
        gen-obj/dir s src-dir/os/%
    ]
]

main: make libr3-os [
    name: 'main

    depends: reduce [
        either user-config/main
        [gen-obj/main user-config/main]
        [gen-obj/dir file-base/main src-dir/os/%]
    ]
]

pthread: make rebmake/ext-dynamic-class [
    output: %pthread
    flags: [static]
]

;extensions
builtin-extensions: copy available-extensions
assert [map? user-config/extensions]
for-each name user-config/extensions [
    action: user-config/extensions/:name
    modules: _
    if block? action [modules: action action: '*]
    switch action [
        '+ [; builtin
            ;pass, default action
        ]
        '- [
            item: _
            iterate builtin-extensions [
                if builtin-extensions/1/name = name [
                    item: take builtin-extensions
                ]
            ]
            if not item [
                fail [{Unrecognized extension name:} name]
            ]
        ]

        fail ["Unrecognized extension action:" mold action]
    ]
]

for-each [label list] reduce [
    {Builtin extensions} builtin-extensions
][
    print label
    for-each ext list [
        print collect [ ;-- CHAR! values don't auto-space in Ren-C PRINT
            keep ["ext:" ext/name #":" space #"["]
            for-each mod (maybe+ ext/modules) [
                keep to-text mod/name
            ]
            keep #"]"
        ]
    ]
]

add-project-flags: func [
    return: [~]
    project [object!]
    /I includes
    /D definitions
    /c cflags
    /O optimization
    /g debug
][
    assert [
        find [
            #dynamic-library
            #object-library
            #static-library
            #application
        ] project/class
    ]

    if D [
        if block? project/definitions [
            append project/definitions definitions
        ] else [
            ensure blank! project/definitions
            project/definitions: definitions
        ]
    ]

    if I [
        if block? project/includes [
            append project/includes includes
        ] else [
            ensure blank! project/includes
            project/includes: includes
        ]
    ]
    if c [
        if block? project/cflags [
            append project/cflags cflags
        ] else [
            ensure blank! project/cflags
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
    assert [mod/class = #extension]
    ret: make rebmake/object-library-class [
        name: mod/name
        depends: map-each s (append reduce [mod/source] maybe- mod/depends) [
            case [
                match [file! block!] s [
                    gen-obj/dir s repo-dir/extensions/%
                ]
                object? s and [find [#object-library #object-file] s/class] [
                    s
                    ; #object-library has already been taken care of above
                    ; if s/class = #object-library [s]
                ]
                default [
                    dump s
                    fail [type of s "can't be a dependency of a module"]
                ]
            ]
        ]
        libraries: null-to-blank all [
            mod/libraries
            map-each lib mod/libraries [
                case [
                    file? lib [
                        make rebmake/ext-dynamic-class [
                            output: lib
                        ]
                    ]
                    object? lib and [
                        find [#dynamic-extension #static-extension] lib/class
                    ][
                        lib
                    ]
                    default [
                        fail [
                            "unrecognized module library" lib
                            "in module" mod
                        ]
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

    ; extract object-library, because an object-library can't depend on
    ; another object-library
    ;
    all [
        block? ext/depends
        not empty? ext/depends
    ] then [
        append ext-objs map-each s ext/depends [
            all [object? s  s/class = #object-library] then [s] else [continue]
        ]
    ]

    append ext-objs mod-obj: process-module ext

    append app-config/libraries maybe- mod-obj/libraries
    append app-config/searches maybe- ext/searches
    append app-config/ldflags maybe- ext/ldflags

    ; Modify module properties
    add-project-flags/I/D/c/O/g mod-obj
        app-config/includes
        app-config/definitions
        app-config/cflags
        app-config/optimization
        app-config/debug

    ; %prep-extensions.r creates a temporary .c file which contains the
    ; collated information for the module (compressed script and spec bytes,
    ; array of dispatcher CFUNC pointers for the natives) and RX_Collate
    ; function.  It is located in the %prep/ directory for the extension.
    ;
    ext-name-lower: lowercase copy to text! ext/name
    ext-init-source: as file! unspaced [
        "tmp-mod-" ext-name-lower "-init.c"
    ]
    append any [all [mod-obj mod-obj/depends] ext-objs] gen-obj/dir/I/D/F
        ext-init-source
        unspaced ["prep/extensions/" ext-name-lower "/"]
        maybe- ext/includes
        maybe- ext/definitions
        maybe- ext/cflags
]


; Reorder builtin-extensions by their dependency
calculate-sequence: function [
    ext
    <local> req b
][
    if integer? ext/sequence [return ext/sequence]
    if ext/visited [fail ["circular dependency on" ext]]
    if blank? ext/requires [ext/sequence: 0 return ext/sequence]
    ext/visited: true
    seq: 0
    if word? ext/requires [ext/requires: reduce [ext/requires]]
    for-each req ext/requires [
        for-each b builtin-extensions [
            if b/name = req [
                seq: seq + (
                    (match integer! b/sequence) else [calculate-sequence b]
                )
                break
            ]
        ] then [ ;-- didn't BREAK, so no match found
            fail ["unrecoginized dependency" req "for" ext/name]
        ]
    ]
    ext/sequence: seq + 1
]

for-each ext builtin-extensions [calculate-sequence ext]
sort/compare builtin-extensions func [a b] [a/sequence < b/sequence]

vars: reduce [
    reb-tool: make rebmake/var-class [
        name: {REBOL_TOOL}
        any [
            'file = exists? value: system/options/boot
            all [
                user-config/rebol-tool
                'file = exists? value: join repo-dir user-config/rebol-tool
            ]
            'file = exists? value: join repo-dir unspaced [
                {r3-make}
                rebmake/target-platform/exe-suffix
            ]
        ] else [
            fail "^/^/!! Cannot find a valid REBOL_TOOL !!^/"
        ]

        ; Originally this didn't transform to a local file path (e.g. with
        ; backslashes instead of slashes on Windows).  There was some reason it
        ; worked in visual studio, but not with nmake.
        ;
        value: file-to-local value
    ]
    make rebmake/var-class [
        name: {REBOL}
        value: {$(REBOL_TOOL) -q}
    ]
    make rebmake/var-class [
        name: {T}
        value: src-dir/tools
    ]
    make rebmake/var-class [
        name: {GIT_COMMIT}
        default: either user-config/git-commit [user-config/git-commit][{unknown}]
    ]
]

prep: make rebmake/entry-class [
    target: 'prep ; phony target

    commands: collect-lines [
        keep [{$(REBOL)} tools-dir/make-natives.r]
        keep [{$(REBOL)} tools-dir/make-headers.r]
        keep [{$(REBOL)} tools-dir/make-boot.r
            unspaced [{OS_ID=} system-config/id]
            {GIT_COMMIT=$(GIT_COMMIT)}
        ]
        keep [{$(REBOL)} tools-dir/make-host-init.r]
        keep [{$(REBOL)} tools-dir/make-os-ext.r]
        keep [{$(REBOL)} tools-dir/make-librebol.r]

        for-each ext builtin-extensions [
            keep [{$(REBOL)} tools-dir/prep-extension.r
                unspaced [{MODULE=} ext/name]
                unspaced [{SRC=extensions/} switch type of ext/source [
                    file! [ext/source]
                    block! [first find ext/source file!]
                    fail "ext/source must be BLOCK! or FILE!"
                ]]
                unspaced [{OS_ID=} system-config/id]
            ]
        ]

        keep [{$(REBOL)} tools-dir/make-extensions-table.r
            unspaced [
                {EXTENSIONS=} delimit ":" map-each ext builtin-extensions [
                    to text! ext/name
                ]
            ]
        ]
    ]
    depends: reduce [
        reb-tool
    ]
]

; Analyze what directories were used in this build's entry from %file-base.r
; to add those obj folders.  So if the `%generic/host-xxx.c` is listed,
; this will make sure `%objs/generic/` is in there.

add-new-obj-folders: function [
    return: [~]
    objs
    folders
    <local>
    lib
    obj
][
    for-each lib objs [
        switch lib/class [
            #object-file [
                lib: reduce [lib]
            ]
            #object-library [
                lib: lib/depends
            ]
            default [
                dump lib
                fail ["unexpected class"]
            ]
        ]

        for-each obj lib [
            dir: split-path obj/output
            if not find folders dir [
                append folders dir
            ]
        ]
    ]
]

folders: copy [%objs/ %objs/main/]
for-each file os-file-block [
    ;
    ; For better or worse, original R3-Alpha didn't use FILE! in %file-base.r
    ; for filenames.  Note that `+` markers should be removed by this point.
    ;
    file: join %objs/ (ensure [word! path!] file)
    path: split-path (ensure file! file)
    find folders path or [append folders path]
]
add-new-obj-folders ext-objs folders

app: make rebmake/application-class [
    name: 'r3-exe
    output: %r3 ;no suffix
    depends: compose [
        (libr3-core)
        (libr3-os)
        (ext-objs)
        (app-config/libraries)
        (main)
    ]
    post-build-commands: either cfg-symbols [
        _
    ][
        reduce [
            make rebmake/cmd-strip-class [
                file: join output maybe- rebmake/target-platform/exe-suffix
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

library: make rebmake/dynamic-library-class [
    name: 'library
    output: %libr3 ;no suffix
    depends: compose [
        (libr3-core)
        (libr3-os)
        (ext-objs)
        (app-config/libraries)
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

top: make rebmake/entry-class [
    target: 'top ; phony target
    depends: flatten reduce [app dynamic-libs]
]

t-folders: make rebmake/entry-class [
    target: 'folders ; phony target
    commands: map-each dir sort folders [;sort it so that the parent folder gets created first
        make rebmake/cmd-create-class compose [
            file: (dir)
        ]
    ]
]

clean: make rebmake/entry-class [
    target: 'clean ; phony target
    commands: flatten reduce [
        make rebmake/cmd-delete-class [file: %objs/]
        make rebmake/cmd-delete-class [file: %prep/]
        make rebmake/cmd-delete-class [
            file: join %r3 maybe- rebmake/target-platform/exe-suffix
        ]
    ]
]

check: make rebmake/entry-class [
    target: 'check ; phony target
    depends: append copy dynamic-libs app
    commands: collect [
        keep make rebmake/cmd-strip-class [
            file: join app/output maybe- rebmake/target-platform/exe-suffix
        ]
        for-each s dynamic-libs [
            keep make rebmake/cmd-strip-class [
                file: join s/output maybe- rebmake/target-platform/dll-suffix
            ]
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
        main
        app
        library
        dynamic-libs
        check
        clean
    ]
    debug: app-config/debug
]

target: user-config/target
if not block? target [target: reduce [target]]
iterate target [
    switch target/1 targets else [
        fail [
            newline
            newline
            "UNSUPPORTED TARGET" user-config/target newline
        ]
    ]
]
