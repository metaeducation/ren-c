REBOL [
    Title: "Top-Level Script for building Rebol"
    File: %make.r
    Rights: {
        Rebol 3 Language Interpreter and Run-time Environment
        "Ren-C" branch @ https://github.com/metaeducation/ren-c

        Copyright 2012-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    Description: {
        See notes on building in README.md
    }
]

if not find words of :import [product] [  ; See %import-shim.r
    do load append copy system/script/path %tools/import-shim.r
]

import <tools/bootstrap-shim.r>
import <tools/common.r>  ; Note: sets up `repo-dir`
import <tools/systems.r>

rebmake: import <tools/rebmake.r>


=== {ADJUST TO AN OUT-OF-SOURCE BUILD IF RUN FROM REPOSITORY's DIRECTORY} ===

; NOTE NEW BEHAVIOR IN REN-C, DIFFERENT FROM HISTORICAL REBOL:
;
; When you run a Ren-C script from the command line, the `current-path` is the
; directory where the user was when the script launched.
;
; We default to building in a subdirectory called %build/ if they launch from
; the repository directory itself.  Otherwise we build wherever they are.

if repo-dir = what-dir [
    launched-from-root: true
    output-dir: join repo-dir %build/
    make-dir output-dir
] else [
    output-dir: what-dir  ; out-of-source build
    launched-from-root: false
]

tools-dir: join repo-dir %tools/
src-dir: join repo-dir %src/


=== {GLOBALS} ===

; The file list for the core .c files comes from %file-base.r, while the
; file list for extensions comes from that extension's %make-spec.r
;
file-base: make object! load (join repo-dir %tools/file-base.r)

; Start out with a default configuration (may be overridden)
;
user-config: make object! load (join repo-dir %configs/default-config.r)


=== {SPLIT ARGS INTO OPTIONS AND COMMANDS} ===

; Note that we should have launched %make.r with the notion of the current
; directory (WHAT-DIR) being the same as what the user had on the command line.
; So relative paths should be interpreted the same.

; args are:
; [OPTION | COMMAND] ...
; COMMAND = WORD
; OPTION = 'NAME=VALUE' | 'NAME: VALUE'
;
args: parse-args system/script/args  ; either from command line or DO/ARGS

; now args are ordered and separated by bar:
; [NAME VALUE ... '| COMMAND ...]
;
if commands: try find/only args '| [
    options: copy/part args commands
    commands: next commands
]
else [
    options: args
]

for-each [name value] options [
    switch name [
        'CONFIG [
            ; A config file can inherit from other configurations with the
            ; `Inherits: %some-config.r` header option.
            ;
            config: to-file value

            saved-dir: what-dir

            ; Because the goal is to create an OBJECT! from the top-level
            ; declarations in the file, this has to build an inheritance stack.
            ; It must tunnel to the innermost configs and run those first...
            ; otherwise, the outermost configs wouldn't have the innermost
            ; config fields visible when the run.
            ;
            config-stack: copy []
            loop [:config] [
                path+file: split-path config
                path: path+file/1
                file: path+file/2
                change-dir path
                append config-stack ^(transcode read file)

                ; !!! LOAD has changed between bootstrap versions, for the
                ; handling of the /HEADER.  This hacks it together by doing a
                ; transcode + intern.  Revisit and use actual LOAD with mods in
                ; the bootstrap shim.  `Inherits:` may actually be good as
                ; a first-class module feature, not config-specific.
                ;
                assert ['REBOL = first last config-stack]
                config: select ensure block! second last config-stack 'Inherits
                take/part last config-stack 2  ; drop the REBOL [...] header
            ]
            loop [not empty? config-stack] [
                user-config: make user-config intern take/last config-stack
            ]

            change-dir saved-dir
        ]
        'EXTENSIONS [
            ; [+|-|*] [NAME {+|-|*|[modules]}]...
            use [ext-file user-ext][
                user-ext: load value
                if not block? user-ext [
                    fail [
                        "Selected extensions must be a block, not"
                        (type of user-ext)
                    ]
                ]
                all [
                    not empty? user-ext
                    find/only [+ - *] user-ext/1
                ] then [
                    value: take user-ext
                ]
                for-each [name value] user-ext [
                    user-config/extensions/:name: value
                ]
            ]
        ]
    ] else [
        set in user-config (to-word replace/all to text! name #"_" #"-")
            attempt [load-value value] else [value]
    ]
]


=== {CHANGE DIRECTORY} ===

; The baseline directory we are in during the build is where we are writing
; the output products.
;
; But we don't want to actually change to that directory until all the paths
; in the command line were processed, because that was the user's concept of
; where `../` or other relative paths were relative to.

change-dir output-dir

; We relativize `repo-dir` to the output directory, where the build process
; is being run.  Using relative paths helps gloss over some Windows and Linux
; differences on file paths.  It's also more readable to have short filenames
; like `..\src\foo.c` instead of `C:\Wherever\Deep\Path\src\foo.c` on
; compiliation command lines.
;
repo-dir: relative-to-path repo-dir output-dir


=== {PROCESS COMMANDS} ===

if commands [user-config/target: load commands]


=== {MODULES AND EXTENSIONS} ===

system-config: config-system user-config/os-id
rebmake/set-target-platform system-config/os-base

to-obj-path: func [
    file [any-string!]
][
    let ext: find-last file #"."
    remove/part ext (length of ext)
    join %objs/ head-of append ext rebmake/target-platform/obj-suffix
]

gen-obj: func [
    return: "Rebmake specification object for OBJ"
        [object!]
    s "file representation, or list if block"
        [file! text! word! path! tuple! block!]
    /dir "directory" [any-string!]
    /D "definitions" [block!]
    /I "includes" [block!]
    /F "cflags" [block!]
    /main "for main object"
][
    let prefer-O2: false  ; overrides -Os to give -O2, e.g. for %c-eval.c
    let standard: user-config/standard  ; may have a per-file override
    let rigorous: user-config/rigorous  ; may have a per-file override
    let cplusplus: false  ; determined for just this file
    let flags: make block! 8

    ; Microsoft shouldn't bother having the C warning that foo() in standard
    ; C doesn't mean the same thing as foo(void), when in their own published
    ; headers (ODBC, Windows.h) they treat them interchangeably.  See for
    ; instance EnableMouseInPointerForThread().  Or ODBCGetTryWaitValue().
    ;
    ; Just disable the warning, and hope the Linux build catches most of it.
    ;
    ;     'function' : no function prototype given:
    ;     converting '()' to '(void)'
    ;
    append flags <msc:/wd4255>

    ; Warnings when __declspec(uuid(x)) is used on types, or __declspec is
    ; used before linkage specifications, etc. etc.  These are violated
    ; e.g. by older versions of %shlobj.h and %ocidl.h.  You can get them if
    ; you use something like a Windows XP-era SDK with a more modern Visual
    ; Studio compiler (e.g. 2019, which deprecated support for targeting XP).
    ;
    append flags [<msc:/wd4917> <msc:/wd4768> <msc:/wd4091>]

    ; The May 2018 update of Visual Studio 2017 added a warning for when you
    ; use an #ifdef on something that is #define'd, but 0.  Then the internal
    ; %yvals.h in MSVC tests #ifdef __has_builtin, which has to be defined
    ; to 0 to work in MSVC.  Disable the warning for now.
    ;
    append flags <msc:/wd4574>

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

    if block? s [
        for-each flag next s [
            switch flag [
                #no-c++ [
                    ;
                    ; !!! The cfg-cplusplus flag is currently set if any files
                    ; are C++.  This means that it's a fair indication that
                    ; a previous call to this routine noticed a C++ compiler
                    ; is in effect, e.g. the config maps `gcc` tool to `%g++`.
                    ;
                    if cfg-cplusplus [
                        standard: 'c

                        ; Here we inject "compile as c", but to limit the
                        ; impact (e.g. on C compilers that don't know what -x
                        ; is) we only add the flag if it's a C++ build.  MSVC
                        ; does not need this because it uses the same
                        ; compiler and only needs switches to turn C++ *on*.
                        ;
                        append flags <gnu:-x c>
                    ]
                ]
            ]
        ]
    ]

    ; Add flags to take into account whether building as C or C++, and which
    ; version of the standard.  Note if the particular file is third party and
    ; can only be compiled as C, we may have overridden that above.
    ;
    insert flags try switch standard [
        'c [
            _
        ]
        'gnu89 'c99 'gnu99 'c11 [
            reduce [
                to tag! unspaced ["gnu:--std=" standard]
                ;
                ; Note: MSVC does not have any conformance to C99 or C11 at
                ; time of writing, and will complain about /std:c99 ... it only
                ; supports /std:c++11 and /std:c++17 so we basically have to
                ; just ignore the C versioning in MSVC.
            ]
        ]
        'c++ [
            cfg-cplusplus: cplusplus: true
            [
                <gnu:-x c++>
                <msc:/TP>
            ]
        ]
        'c++98 'c++0x 'c++11 'c++14 'c++17 'c++20 'c++latest [
            cfg-cplusplus: cplusplus: true
            compose [
                ; Compile C files as C++.
                ;
                ; !!! Original code appeared to make it so that if a Visual
                ; Studio project was created, `/TP` option gets removed and it
                ; was translated into XML under the <CompileAs> option.  But
                ; that meant extensions weren't getting the option, so it has
                ; been disabled pending review.
                ;
                ; !!! For some reason, clang has deprecated`-x c++`, though
                ; it still works.  It is not possible to disable the warning,
                ; so RIGOROUS can not be used with clang in C++ builds...
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

                ; There is a shim for `nullptr` used, that's warned about even
                ; when building as pre-C++11 where it was introduced, unless
                ; you disable that warning.
                ;
                ((if user-config/standard = 'c++98 [<gnu:-Wno-c++0x-compat>]))

                ; Note: The C and C++ user-config/standards do not dictate if
                ; `char` is signed or unsigned.  If you think environments
                ; all settled on them being signed, they're not... Android NDK
                ; uses unsigned:
                ;
                ; http://stackoverflow.com/questions/7414355/
                ;
                ; In order to give the option some exercise, make GCC C++
                ; builds use unsigned chars.
                ;
                <gnu:-funsigned-char>

                ; MSVC never bumped the __cplusplus version past 1997, even if
                ; you compile with C++17.  Hence CPLUSPLUS_11 is used by Rebol
                ; code as the switch for most C++ behaviors, and we have to
                ; define that explicitly.
                ;
                <msc:/DCPLUSPLUS_11=1>
            ]
        ]

        fail [
            "STANDARD should be one of"
            "[c gnu89 gnu99 c99 c11 c++ c++11 c++14 c++17 c++latest]"
            "not" (user-config/standard)
        ]
    ]

    ; The `rigorous: yes` setting in the config turns the warnings up to where
    ; they are considered errors.  However, there are a *lot* of warnings
    ; when you turn things all the way up...and not all of them are relevant.
    ; Still we'd like to get the best information from any good ones, so
    ; they're turned off on a case-by-case basis.
    ;
    append flags try switch rigorous [
        #[true] 'yes 'on 'true [
            compose [
                <gnu:-Werror> <msc:/WX>  ; convert warnings to errors

                ; If you use pedantic in a C build on an older GNU compiler,
                ; (that defaults to thinking it's a C89 compiler), it will
                ; complain about using `//` style comments.  There is no
                ; way to turn this complaint off.  So don't use pedantic
                ; warnings unless you're at c99 or higher, or C++.
                ;
                ((
                    if not find/only [c gnu89] standard [
                        <gnu:--pedantic>
                    ]
                ))

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
                ((
                    either all [
                        cplusplus
                        find app-config/definitions "NDEBUG"
                    ][
                        <gnu:-Wcast-qual>
                    ][
                        <gnu:-Wno-cast-qual>
                    ]
                ))

                ;   'bytes' bytes padding added after construct 'member_name'
                ;
                ; Disable warning C4820; just tells you struct is not an
                ; exactly round size for the platform.
                ;
                <msc:/wd4820>

                ; Without disabling this, you likely get:
                ;
                ;   '_WIN32_WINNT_WIN10_TH2' is not defined as a preprocessor
                ;   macro, replacing with '0' for '#if/#elif'
                ;
                ; Seems to be some mistake on Microsoft's part, that some
                ; report can be remedied by using WIN32_LEAN_AND_MEAN:
                ;
                ; https://stackoverflow.com/q/11040133/
                ;
                ; But then if you include <winioctl.h> (where the problem is)
                ; you'd still have it.
                ;
                <msc:/wd4668>

                ; There are a currently a lot of places where `int` is passed
                ; to REBLEN, where the signs mismatch.  Disable C4365:
                ;
                ;  'action' : conversion from 'type_1' to 'type_2',
                ;  signed/unsigned mismatch
                ;
                ; and C4245:
                ;
                ;  'conversion' : conversion from 'type1' to 'type2',
                ;  signed/unsigned mismatch
                ;
                <msc:/wd4365> <msc:/wd4245>
                <gnu:-Wsign-compare>

                ; The majority of Rebol's C code was written with little
                ; attention to overflow in arithmetic.  In many places a
                ; bigger type is converted into a smaller type without an
                ; explicit cast.  (REBI64 => SQLUSMALLINT, REBINT => REBYTE).
                ; Disable C4242:
                ;
                ;   'identifier' : conversion from 'type1' to 'type2',
                ;   possible loss of data
                ;
                ; The issue needs systemic review.
                ;
                <msc:/wd4242>
                <gnu:-Wno-conversion> <gnu:-Wno-strict-overflow>
                ;<gnu:-Wstrict-overflow=5>

                ; When an inline function is not referenced, there can be a
                ; warning about this; but it makes little sense to do so since
                ; there are a many standard library functions in includes that
                ; are inline which one does not use (C4514):
                ;
                ;   'function' : unreferenced inline function has been removed
                ;
                ; Inlining is at the compiler's discretion, it may choose to
                ; ignore the `inline` keyword.  Usually it won't tell you it
                ; did, but disable the warning that tells you (C4710):
                ;
                ;   function' : function not inlined
                ;
                ; There's also an "informational" warning telling you that a
                ; function was chosen for inlining when it wasn't requested,
                ; so disable that also (C4711):
                ;
                ;   function 'function' selected for inline expansion
                ;
                <msc:/wd4514>
                <msc:/wd4710>
                <msc:/wd4711>

                ; It's useful to know when function pointers are assigned to
                ; an incompatible type of function pointer.  But Rebol relies
                ; on the ability to have a kind of "void*-for-functions", e.g.
                ; CFUNC, which holds arbitrary function pointers.  There seems
                ; to be no way to get function pointer type checking allowing
                ; downcasts and upcasts from just that pointer type, so it
                ; has to be completely disabled (or managed with #pragma,
                ; which we seek to avoid using in the codebase)
                ;
                ;  'operator/operation' : unsafe conversion from
                ;  'type of expression' to 'type required'
                ;
                <msc:/wd4191>

                ; Though we make sure all enum values are handled with a
                ; `default:`, this warning doesn't let you use default:` at
                ; all...forcing every case to be handled explicitly.
                ;
                ;   enumerator 'identifier' in switch of enum 'enumeration'
                ;   is not explicitly handled by a case label
                ;
                <msc:/wd4061>

                ; setjmp() / longjmp() can't be combined with C++ objects due
                ; to bypassing destructors.  Yet Microsoft's compiler seems to
                ; think even "POD" (plain-old-data) structs qualify as
                ; "C++ objects", so they run destructors (?)
                ;
                ;   interaction between 'function' and C++ object destruction
                ;   is non-portable
                ;
                ; This is lousy, since it would be a VERY useful warning, if
                ; not as uninformative as "your C++ program is using setjmp".
                ;
                ; https://stackoverflow.com/q/45384718/
                ;
                <msc:/wd4611>

                ; Assignment within conditional expressions is tolerated in
                ; core if parentheses are used.  `if ((x = 10) != y) {...}`
                ;
                ;   assignment within conditional expression
                ;
                <msc:/wd4706>

                ; gethostbyname() is deprecated by Microsoft, but dealing with
                ; that is not a priority now.  It's supposed to be replaced
                ; with getaddrinfo() or GetAddrInfoW().  This bypasses the
                ; deprecation warning for now via a #define
                ;
                <msc:/D_WINSOCK_DEPRECATED_NO_WARNINGS>

                ; This warning happens a lot in a 32-bit builds if you use
                ; float instead of double in Microsoft Visual C++:
                ;
                ;  storing 32-bit float result in memory, possible loss
                ;  of performance
                ;
                <msc:/wd4738>

                ; For some reason, even if you don't actually invoke moves or
                ; copy constructors, MSVC warns you that you wouldn't be able
                ; to if you ever did.  :-/
                ;
                <msc:/wd5026>
                <msc:/wd4626>
                <msc:/wd5027>
                <msc:/wd4625>

                ; If a function hasn't been explicitly declared as nothrow,
                ; passing it to extern "C" routines gets a warning.  This is a
                ; C codebase being built as C++, so there shouldn't be throws.
                ;
                <msc:/wd5039>

                ; Microsoft's own xlocale/xlocnum/etc. files trigger SEH
                ; warnings in VC2017 after an update.  Apparently they don't
                ; care--presumably because they're focused on VC2019 now.
                ;
                <msc:/wd4571>

                ; Same deal with format strings not being string literals.
                ; Headers in string from MSVC screws this up.
                ;
                <msc:/wd4774>

                ; There's really no winning with Spectre mitigation warnings.
                ; Early on it seemed simple changes could make them go away:
                ;
                ; https://stackoverflow.com/q/50399940/
                ;
                ; But each version of the compiler adds more, thus it looks
                ; like if you use a comparison operator you will get these.
                ; It's a losing battle, so just disable the warning.
                ;
                <msc:/wd5045>

                ;   Arithmetic overflow: Using operator '*' on a 4 byte value
                ;   and then casting the result to a 8 byte value. Cast the
                ;   value to the wider type before calling operator '*' to
                ;   avoid overflow
                ;
                ; Overflow issues are widespread in Rebol, and this warning
                ; is not particularly high priority in the scope of what the
                ; project is exploring.  Disable for now.
                ;
                <msc:/wd26451>

                ;   The enum type xxx is unscoped. Prefer 'enum class' over
                ;   'enum'
                ;   xxx is uninitialized.  Always initialize a member...
                ;
                ; Ren-C is C, so C++-specific warnings when building as C++
                ; are not relevant.
                ;
                <msc:/wd26812>
                <msc:/wd26495>

                ; Implicit conversion from `int` to `REBD32`, possible loss.
                ;
                <msc:/wd5219>
            ]
        ]
        _ #[false] 'no 'off 'false [
            _
        ]

        fail ["RIGOROUS [yes no \logic!\] not" (rigorous)]
    ]

    ; Now add the flags for the project overall.
    ;
    append flags try F

    ; Now add build flags overridden by the inclusion of the specific file
    ; (e.g. third party files we don't want to edit to remove warnings from)
    ;
    if block? s [
        for-each flag next s [
            append flags try (switch flag [  ; weird bootstrap ELSE needs ()
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

                #prefer-O2-optimization [
                    prefer-O2: true
                    _
                ]

                #no-c++ [
                    standard: 'c
                    _
                ]
            ] else [
                try ensure [text! tag!] flag
            ])
        ]
        s: s/1
    ]

    ; With the flags and settings ready, make a rebmake object and ask it
    ; to build the requested object file.
    ;
    return make rebmake/object-file-class compose [
        assert [any [file? s path? s tuple? s word? s]]
            ; ^-- e.g. TUPLE! like `foo.o`

        ; !!! Note: The TEXT! => FILE! conversions of dir and src-dir are
        ; important, because if source here comes back as a text something
        ; filters it out.  rebmake is a hairball that is destined for complete
        ; rewrite or the recycle bin...so investigating to find why TEXT! isn't
        ; an error and just gets dropped on the floor to make a bad makefile
        ; isn't a priority at the instant I'm writing this comment.  Just
        ; convert the dirs to FILE!.
        ;
        source: case [
            dir [
                assert [any [file? dir text? dir]]
                if text? dir [
                    dir: to file! dir
                ]
                join dir (to file! s)
            ]
            main [to-file s]
        ] else [
            assert [any [file? src-dir text? src-dir]]
            if text? src-dir [
                src-dir: to file! src-dir
            ]

            join src-dir (to file! s)
        ]

        output: to-obj-path to text! ;\
            either main [
                join %main/ to file! (last ensure path! s)
            ] [s]
        cflags: either empty? flags [_] [flags]
        definitions: try D
        includes: try I
        ((if prefer-O2 [[optimization: #prefer-O2-optimization]]))
    ]
]

extension-class: make object! [
    class: #extension
    name: ~
    loadable: yes ;can be loaded at runtime

    mode: _  ; [<builtin> <dynamic>] or unused

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

    hook: _  ; FILE! of extension-specific Rebol script to run during rebmake

    ; Internal Fields

    sequence: _ ; the sequence in which the extension should be loaded
    visited: false

    directory: meth [
        return: [text!]  ; Should this be [file!]?
    ][
        lowercase to text! name  ; !!! Should remember where it was found
    ]
]


=== {SCAN TO BUILD LIST OF AVAILABLE EXTENSIONS} ===

extensions: copy []

parse-ext-build-spec: function [
    return: [object!]
    spec [block!]
][
    ext: make extension-class spec

    if in ext 'options [
        ensure block! ext/options
        parse2 ext/options [
            while [
                word! block! opt text! set config: group!
            ]
        ] else [
            fail ["Could not parse extension build spec" mold spec]
        ]

        if set? 'config [
            do as block! config  ; Note: old Ren-Cs disallowed DO of GROUP!
        ]
    ]

    return ext
]

use [extension-dir entry][
    extension-dir: join repo-dir %extensions/
    for-each entry read extension-dir [
        all [
            dir? entry
            find read (join extension-dir entry) %make-spec.r
        ] then [
            spec: load join (join extension-dir entry) %make-spec.r

            ; !!! The specs use `repo-dir` and some other variables.
            ; Splice those in for starters, but will need deep thought.
            ;
            insert spec compose [
                repo-dir: (repo-dir)
                system-config: (system-config)
                user-config: (user-config)
            ]

            parsed: parse-ext-build-spec spec
            assert [not parsed/mode]  ; no build mode assigned at first yet
            append extensions parsed
        ]
    ]
]

extension-names: map-each x extensions [to-lit-word x/name]


=== {TARGETS} ===

; Collected here so they can be used with `--help targets`

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
        rebmake/makefile/generate %makefile solution
    ]
    'nmake [
        rebmake/nmake/generate %makefile solution
    ]
    'vs2019
    'visual-studio [
        x86: try if system-config/os-name = 'Windows-x86 ['x86]
        rebmake/visual-studio/generate/(x86) output-dir solution
    ]
    'vs2015 [
        x86: try if system-config/os-name = 'Windows-x86 ['x86]
        rebmake/vs2015/generate/(x86) output-dir solution
    ]
    'vs2017 [
        fail [
            "Only minor changes needed to support vs2017, please get in"
            "touch if this is an issue and you need it for some reason."
        ]
    ]
]

target-names: make block! 16
for-each x targets [
    if lit-word? x [
        append target-names ^(noquote x)
        append target-names [|]
    ] else [
        take/last target-names
        append target-names newline
    ]
]


=== {HELP} ===

indent: func [
    text [text!]
    /space
][
    replace/all text ;\
        either space [" "] [newline]
        "^/    "
]

help-topics: reduce [

; Note: Use only 1 indentation level in help strings

'usage copy {=== USAGE ===^/
    > PATH/TO/r3-make PATH/TO/make.r [CONFIG | OPTION | TARGET ...]^/
NOTE 1: current dir is the build dir,
    that will contain all generated stuff
    (%prep/, %objs/, %makefile, %r3 ...)
    You can have multiple build dirs.^/
NOTE 2: but if the current dir is the "root" dir
    (where make.r is), then the build dir is %build/^/
NOTE 3: order of configs and options IS relevant^/
MORE HELP:^/
    { -h | -help | --help } { HELP-TOPICS }
    }

'targets unspaced [{=== TARGETS ===^/
    }
    indent form target-names
    ]

'configs unspaced [ {=== CONFIGS ===^/
    { config: | load: | do: } PATH/TO/CONFIG-FILE^/
FILES IN %make/configs/ SUBFOLDER:^/
    }
    indent/space form sort map-each x ;\
        load (join repo-dir %configs/)
        [to-text x]
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
    [FLAG] [ NAME {FLAG|[MODULES]} ... ]^/
FLAG:
    + => builtin
    - => disable
    * => dynamic^/
NOTE: 1st 'anonymous' FLAG, if present, set the default^/
NAME: one of
    }
    indent delimit " | " extension-names
    {^/
EXAMPLES:
    extensions: +
    => enable all extensions as builtin
    extensions: "- gif + jpg * png [lodepng]"
    => disable all extensions but gif (builtin),jpg and png (dynamic)^/
CURRENT VALUE:
    }
    indent mold user-config/extensions
    newline
    ]
]

; dynamically fill help topics list ;-)
replace help-topics/usage "HELP-TOPICS" ;\
    form append (map-each x help-topics [either text? x ['|] [x]]) [all]

help: function [topic [text! blank!]] [
    topic: try attempt [to-word topic]
    print ""
    case [
        topic = 'all [
            for-each [topic msg] help-topics [
                print msg
            ]
        ]
        msg: select help-topics topic [
            print msg
        ]
    ] else [
        print help-topics/usage
    ]
]

; process help: {-h | -help | --help} [TOPIC]

iterate commands [
    if find ["-h" "-help" "--help"] commands/1 [
        help try commands/2
        quit
    ]
]


=== {DETECT TOOLCHAIN FOR BUILDING} ===

if launched-from-root [
    print ["Launched from root dir, so building in:" output-dir]
]

set-exec-path: func [
    return: <none>
    tool [object!]
    path
][
    if path [
        if not match [file! text!] path [
            fail "Tool path has to be a file!"
        ]
        tool/exec-file: path
    ]
]

parse2 user-config/toolset [
    while [
        'gcc opt set cc-exec [file! | text! | blank!] (
            rebmake/default-compiler: rebmake/gcc
        )
        | 'clang opt set cc-exec [file! | text! | blank!] (
            rebmake/default-compiler: rebmake/clang
        )
        | 'cl opt set cc-exec [file! | text! | blank!] (
            rebmake/default-compiler: rebmake/cl
        )
        | 'ld opt set linker-exec [file! | text! | blank!] (
            rebmake/default-linker: rebmake/ld
        )
        | 'llvm-link opt set linker-exec [file! | text! | blank!] (
            rebmake/default-linker: rebmake/llvm-link
        )
        | 'link opt set linker-exec [file! | text! | blank!] (
            rebmake/default-linker: rebmake/link
        )
        | 'strip opt set strip-exec [file! | text! | blank!] (
            rebmake/default-strip: rebmake/strip
            rebmake/default-strip/options: [<gnu:-S> <gnu:-x> <gnu:-X>]
            if get 'strip-exec [
                set-exec-path rebmake/default-strip strip-exec
            ]
        )
    ]
    pos:  ; <here>
] else [
    fail ["failed to parse toolset at:" mold pos]
]


=== {SANITY CHECK COMPILER AND LINKER} ===

if not rebmake/default-compiler [fail "Compiler is not set"]
if not rebmake/default-linker [fail "Default linker is not set"]

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

if get 'cc-exec [
    set-exec-path rebmake/default-compiler cc-exec
]
if get 'linker-exec [
    set-exec-path rebmake/default-linker linker-exec
]


=== {GENERATE OVERALL APPLICATION CONFIGURATION} ===

; This appears to put together a baseline of settings that are passed by
; default when building all object files.  So if you requested a debug build
; it would add the compile switch for `-g`, and this would wind up trickling
; down to all the extensions.

app-config: make object! [
    cflags: make block! 8
    ldflags: make block! 8
    libraries: make block! 8
    debug: off
    optimization: 2
    definitions: copy []
    includes: reduce [(join src-dir %include/) %prep/include/]
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
        cfg-symbols: true
        append app-config/definitions ["NDEBUG"]
    ]
    'normal [
        cfg-symbols: true
        app-config/debug: on
    ]
    'sanitize [
        ;
        ; MSVC has added support for address sanitizer, but at time of writing
        ; it's limited to 32-bit non-debug builds with non-DLL runtimes:
        ;
        ; https://devblogs.microsoft.com/cppblog/addresssanitizer-asan-for-windows-with-msvc/
        ;
        ; You have to build 32-bit with /MT (not /MD) and /fsanitize=address,
        ; then link with /whole-archive:clang_rt.asan-i386.lib.  Testing it
        ; manually shows that it does work--albeit it seems to exhaust memory
        ; while running the test suite (at least with the settings out of the
        ; box).  If someone is so inclined they can modify the generator for
        ; this, but the linux build probably catches most things, and it seems
        ; Dr. Memory is likely a better bet for Windows, despite being slower:
        ;
        ; https://drmemory.org/
        ;
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
    ; A special CALLGRIND native is included which allows metrics gathering to
    ; be turned on and off.  Needs <valgrind/callgrind.h> which should be
    ; installed when you install the valgrind package.
    ;
    ; To start valgrind in a mode where it's not gathering at the outset:
    ;
    ; valgrind --tool=callgrind --dump-instr=yes --collect-atstart=no ./r3
    ;
    ; Then use CALLGRIND ON and CALLGRIND OFF.  To view the callgrind.out
    ; file, one option is to use KCacheGrind.
    ;
    'callgrind [
        cfg-symbols: true
        append app-config/cflags "-g"  ; for symbols
        app-config/debug: off

        append app-config/definitions [
            "NDEBUG"  ; disable assert(), and many other general debug checks

            ; Include debugging features which do not in-and-of-themselves
            ; affect runtime performance (DEBUG_TRACK_EXTEND_CELLS would be an
            ; example of something that significantly affects runtime, and
            ; even things like DEBUG_FRAME_LABELS adds a tiny bit!)
            ;
            "DEBUG_HAS_PROBE=1"
            "DEBUG_FANCY_PANIC=1"
            "DEBUG_USE_UNION_PUNS=1"
            "INCLUDE_C_DEBUG_BREAK_NATIVE=1"

            ; Adds CALLGRIND, see REBNATIVE(callgrind) for implementation
            ;
            "INCLUDE_CALLGRIND_NATIVE=1"
        ]
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

cfg-cplusplus: false  ; gets set to true if linked as c++ overall

; pre-vista switch
; Example. Mingw32 does not have access to windows console api prior to vista.
;
cfg-pre-vista: false
append app-config/definitions switch user-config/pre-vista [
    #[true] 'yes 'on 'true [
        cfg-pre-vista: true
        compose [
            "PRE_VISTA"
        ]
    ]
    _ #[false] 'no 'off 'false [
        cfg-pre-vista: false
        _
    ]

    fail ["PRE-VISTA [yes no \logic!\] not" (user-config/pre-vista)]
]


append app-config/ldflags switch user-config/static [
    _ 'no 'off 'false #[false] [
        ;pass
        _
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


=== {"ADD SYSTEM SETTINGS"} ===

; Not quite sure what counts as system definitions (?)  Review.

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

add-app-def copy system-config/definitions
add-app-cflags copy system-config/cflags
add-app-lib copy system-config/libraries
add-app-ldflags copy system-config/ldflags

print ["definitions:" mold app-config/definitions]
print ["includes:" mold app-config/includes]
print ["libraries:" mold app-config/libraries]
print ["cflags:" mold app-config/cflags]
print ["ldflags:" mold app-config/ldflags]
print ["debug:" mold app-config/debug]
print ["optimization:" mold app-config/optimization]

append app-config/definitions reduce [
    unspaced ["TO_" uppercase to-text system-config/os-base "=1"]
    unspaced [
        "TO_" (uppercase replace/all to-text system-config/os-name "-" "_") "=1"
    ]
]

; Add user settings (already can be BLANK!, use TRY to emphasize they should
; be NULL, but older bootstrap executable did not support it)
;
append app-config/definitions try user-config/definitions
append app-config/includes try user-config/includes
append app-config/cflags try user-config/cflags
append app-config/libraries try user-config/libraries
append app-config/ldflags try user-config/ldflags

libr3-core: make rebmake/object-library-class [
    name: 'libr3-core
    definitions: join ["REB_API"] app-config/definitions

    ; might be modified by the generator, thus copying
    includes: join app-config/includes %prep/core

    ; might be modified by the generator, thus copying
    cflags: copy app-config/cflags

    optimization: app-config/optimization
    debug: app-config/debug
    depends: map-each w file-base/core [
        gen-obj/dir w (join src-dir %core/)
    ]
    append depends map-each w file-base/generated [
        gen-obj/dir w "prep/core/"
    ]
]

main: make libr3-core [
    name: 'main

    definitions: join ["REB_CORE"] app-config/definitions
    includes: join app-config/includes %prep/main  ; generator may modify
    cflags: copy app-config/cflags  ; generator may modify

    depends: reduce [
        either user-config/main [
            gen-obj/main user-config/main
        ][
            gen-obj/dir file-base/main (join src-dir %main/)
        ]
    ]
]

pthread: make rebmake/ext-dynamic-class [
    output: %pthread
    flags: [static]
]


=== {GATHER LIST OF FOLDERS THAT MUST BE CREATED} ===

; Analyze what directories were used in this build's entry from %file-base.r
; to add those obj folders.  So if the `%generic/host-xxx.c` is listed,
; this will make sure `%objs/generic/` is in there.
;
; Historical Rebol had a completely flat file structure.  Ren-C introduced
; hierarchy due to needing to organize more files, and also to allow for a
; place to put individual README.md for a group of related files and explain
; why those files are together.
;
; The compiler will not create folders for objs on its own, so this has to be
; done by a separate step by a makefile or using the interpreter's filesystem
; directory creation command.
;
; !!! This is an inelegant "interim" hack which gives subfolders for the obj
; files that have paths in them in %file-base.r

folders: copy [%objs/ %objs/main/]

add-new-obj-folders: function [
    return: <none>
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
            (elide dump lib)
            fail ["unexpected class"]
        ]

        for-each obj lib [
            dir: first split-path obj/output
            if not find folders dir [
                append folders dir
            ]
        ]
    ]
]


for-each [category entries] file-base [
    if find/only [generated made] category [
        continue  ; these categories are taken care of elsewhere
    ]
    switch type of entries [
        word!  ; if bootstrap
        tuple! [  ; if generic-tuple enabled
            assert [entries = 'main.c]  ; !!! anomaly, ignore it for now
        ]
        block! [
            for-each entry entries [
                entry: maybe if block? entry [first entry]
                switch type of entry [
                    word!  ; if bootstrap executable
                    tuple! [  ; if generic-tuple enabled
                        ; assume taken care of
                    ]
                    path! [
                        dir: first split-path to file! entry
                        if not find folders dir [
                            append folders join %objs/ dir
                        ]
                    ]
                    fail
                ]
            ]
        ]
        fail
    ]
]


=== {DETERMINE SETTINGS FOR THE EXTENSIONS} ===

; The user can ask for an extension to be `-` (not built at all) or `+` (which
; is built into the executable) or `*` (built as dll or so dynamically, and
; can be selectively loaded by the interpreter).
;
; This translates to an ext/mode of <builtin>, <dynamic>, or blank.

assert [map? user-config/extensions]
for-each ext extensions [
    if mode: try select user-config/extensions ext/name [
        ;
        ; Bootstrap executable (at least on mac) had problems setting to null.
        ; Try workaround.
        ;
        append user-config/extensions reduce [ext/name _]
    ]
    switch mode [
        _  ; Currently the default is built in.  Shouldn't be!
        '+ [
            ext/mode: <builtin>
        ]
        '* [
            if not ext/loadable [
                fail [{Extension} name {is not dynamically loadable}]
            ]
            ext/mode: <dynamic>
        ]
        '- [
            ext/mode: _
        ]
        fail ["Unrecognized extension mode:" mold mode]
    ]
]
for-each [name val] user-config/extensions [
    if not blank? val [
        fail [{Unrecognized extension name:} name]
    ]
]

for-each [mode label] [
    <builtin> {BUILTIN EXTENSIONS:}
    <dynamic> {DYNAMIC EXTENSIONS:}
][
    print label
    print mold collect [
        for-each ext extensions [
            if ext/mode = mode [
                keep ^(ext/name)
            ]
        ]
    ]
]


add-project-flags: func [
    return: <none>
    project [object!]
    /I "includes" [block!]
    /D "definitions" [block!]
    /c "cflags" [block!]
    /O "optimization" [any-value!] ; !!! types?
    /g "debug" [any-value!]  ; !!! types?
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
            append project/definitions D
        ] else [
            ensure blank! project/definitions
            project/definitions: try D
        ]
    ]

    if I [
        if block? project/includes [
            append project/includes I
        ] else [
            ensure blank! project/includes
            project/includes: try I
        ]
    ]
    if c [
        if block? project/cflags [
            append project/cflags c
        ] else [
            ensure blank! project/cflags
            project/cflags: try c
        ]
    ]
    if g [project/debug: g]
    if O [project/optimization: O]
]


=== {REORDER EXTENSIONS BASED ON THEIR "REQUIRES" DEPENDENCIES} ===

; When built in extensions are loading, they can say what other extensions
; they require.  Here a sequence is calculated to make sure they are loaded
; in the right order.
;
; Calculating this ahead of time is a limited approach, as it doesn't help
; when a dynamically loaded extension requires some other extension (which
; may or may not be dynamic.)

calculate-sequence: function [
    return: [integer!]
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
        for-each b extensions [
            if b/name = req [
                seq: seq + (
                    (match integer! b/sequence) else [calculate-sequence b]
                )
                break
            ]
        ] then [  ; didn't BREAK, so no match found
            fail ["unrecoginized dependency" req "for" ext/name]
        ]
    ]
    return ext/sequence: seq + 1
]

for-each ext extensions [calculate-sequence ext]
sort/compare extensions func [a b] [a/sequence < b/sequence]


=== {PRODUCE COMPILER/LINKER PROCESSABLE OBJECTS FROM EXTENSION SPECS} ===

; The #extension object corresponds roughly to the make-spec.r typed in; e.g.
; it might list `%odbc32` in the `libraries` section as a FILE!.  But the lower
; levels of the build process don't handle `libraries:`, only `dependencies:`,
; where it wants that expressed as a #dynamic-extension OBJECT!.  And a
; dependency on a source .c FILE! similarly needs to be turned to a dependency
; on an OBJECT! representing the #object-file that it generates.
;
; Here we are responsible of that translation from #extension (which
; the LD, LINK, and LLVM handlers don't understand) into #object-library
; (which is broken down more fundamentally and they can understand)

builtin-ext-objlibs: copy []  ; #object-library for each built in extension
dynamic-ext-objlibs: copy []  ; #object-library for each built in extension

dynamic-libs: copy []  ; #dynamic-library for each DLL

for-each ext extensions [
    assert [ext/class = #extension]  ; basically what we read from %make-spec.r

    if not ext/mode [continue]  ; blank means extension not in use

    assert [find [<builtin> <dynamic>] ext/mode]

    ext-objlib: make rebmake/object-library-class [  ; #object-library
        name: ext/name
        depends: map-each s (append reduce [ext/source] try ext/depends) [
            dep: case [
                match [file! block!] s [
                    gen-obj/dir s (join repo-dir %extensions/)
                ]
                all [
                    object? s
                    find [#object-library #object-file] s/class
                ][
                    s
                    ; #object-library has already been taken care of above
                    ; if s/class = #object-library [s]
                ]
                (elide dump s)
                fail [type of s "can't be a dependency of a module"]
            ]
        ]
        libraries: try all [
            ext/libraries
            map-each lib ext/libraries [
                case [
                    file? lib [
                        make rebmake/ext-dynamic-class [
                            output: lib
                        ]
                    ]
                    (object? lib) and (
                        find [#dynamic-extension #static-extension] lib/class
                    )[
                        lib
                    ]
                    fail ["unrecognized library" lib "in extension" ext]
                ]
            ]
        ]

        includes: ext/includes
        definitions: ext/definitions
        cflags: ext/cflags
        searches: ext/searches
    ]

    ; %prep-extensions.r creates a temporary .c file which contains the
    ; collated information for the module (compressed script and spec bytes,
    ; array of dispatcher CFUNC pointers for the natives) and RX_Collate
    ; function.  It is located in the %prep/ directory for the extension.
    ;
    ext-name-lower: lowercase copy to text! ext/name
    ext-init-source: as file! unspaced [
        "tmp-mod-" ext-name-lower "-init.c"
    ]
    append ext-objlib/depends gen-obj/dir/I/D/F
        ext-init-source
        unspaced ["prep/extensions/" ext-name-lower "/"]
        opt ext/includes
        opt ext/definitions
        opt ext/cflags

    ; Here we graft things like the global debug settings and optimization
    ; flags onto the extension.  This also lets it find the core include files
    ; like %sys-core.h or %reb-config.h.  Long term, most-if-not-all extensions
    ; should be restricted to use the public API and not %sys-core.h
    ;
    ; Not only do these settings get used by the mod-xxx.c file, but they are
    ; propagated to the dependencies (I think?)
    ;
    ; We add a #define of either REB_API or REB_EXT based on if it's a DLL
    ;
    add-project-flags/I/D/c/O/g ext-objlib
        app-config/includes
        compose [
            ((either ext/mode = <builtin> ["REB_API"] ["REB_EXT"]))
            ((app-config/definitions))
        ]
        app-config/cflags
        app-config/optimization
        app-config/debug

    if ext/mode = <builtin> [
        append builtin-ext-objlibs ext-objlib

        ; While you can have varied compiler switches in effect for individual
        ; C files to make OBJs, you only get one set of linker settings to make
        ; one executable.  It's a limitation of the <builtin> method.
        ;
        ; !!! searches seems to correspond to the -L switch.  Strange name to
        ; have to remember, and so the make-spec.r have been doing that as an
        ; ldflag, but this then has to be platform specific.  But generally you
        ; already need platform-specific code to know where to look.
        ;
        append app-config/libraries try ext-objlib/libraries
        append app-config/ldflags try ext/ldflags
        append app-config/searches try ext/searches
    ]
    else [
        append dynamic-ext-objlibs ext-objlib

        ; We need to make a new "Project" abstraction to represent the DLL

        ext-proj: make rebmake/dynamic-library-class [
            name: join either system-config/os-base = 'windows ["r3-"]["libr3-"]
                lowercase to text! ext/name
            output: to file! name
            depends: compose [
                ((ext-objlib))  ; !!! Will this pull in all of extensions deps?
                ;
                ; (app) all dynamic extensions depend on r3, but app not ready
                ; so the dependency is added at a later phase below
                ;
                ((opt app-config/libraries))
                ((opt ext-objlib/libraries))
            ]
            post-build-commands: either cfg-symbols [
                _
            ][
                reduce [
                    make rebmake/cmd-strip-class [
                        file: join output try rebmake/target-platform/dll-suffix
                    ]
                ]
            ]

            ldflags: compose [
                ((opt ext/ldflags))
                ((app-config/ldflags))

                ; GCC has this but Clang does not, and currently Clang is
                ; being called through a gcc alias.  Review.
                ;
                ;<gnu:-Wl,--as-needed>  ; Switch ignores linking unused libs
            ]
        ]

        ; !!! It's not clear if this is really needed (?)
        ;
        add-project-flags/I/D/c/O/g ext-proj
            app-config/includes
            join ["REB_EXT"] app-config/definitions
            app-config/cflags
            app-config/optimization
            app-config/debug

        append dynamic-libs ext-proj  ; need to add app as a dependency later
    ]

    ; For all the obj files needed by this extension, ensure the output dir
    ; is known to neeed to be created
    ;
    add-new-obj-folders (reduce [ext-objlib]) folders
]


=== {RUN THE MAKE (INVOKE COMPILER WITH CALL -OR- GENERATE MAKEFILE} ===

; Here we run MAKE which will not do the actual compilation if you ask the
; TARGET to be a makefile.  It only runs the compilation if you are using
; Rebmake to run the compiler via CALL.

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
                try rebmake/target-platform/exe-suffix
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
        value: join src-dir %tools/
    ]
    make rebmake/var-class [
        name: {GIT_COMMIT}
        default: either user-config/git-commit [user-config/git-commit][{unknown}]
    ]
]

prep: make rebmake/entry-class [
    target: 'prep ; phony target

    commands: collect [
        keep: adapt :keep [value: spaced :value]

        keep [{$(REBOL)} join tools-dir %make-natives.r]
        keep [{$(REBOL)} join tools-dir %make-headers.r]
        keep [
            {$(REBOL)} join tools-dir %make-boot.r
            unspaced [{OS_ID=} system-config/id]
            {GIT_COMMIT=$(GIT_COMMIT)}
        ]
        keep [
            {$(REBOL)} join tools-dir %make-reb-lib.r
            unspaced [{OS_ID=} system-config/id]
        ]

        for-each ext extensions [
            keep [
                {$(REBOL)} join tools-dir %prep-extension.r
                unspaced [{MODULE=} ext/name]
                unspaced [{SRC=extensions/} switch type of ext/source [
                    file! [ext/source]
                    block! [first find ext/source file!]
                    fail "ext/source must be BLOCK! or FILE!"
                ]]
                unspaced [{OS_ID=} system-config/id]
            ]

            if ext/hook [
                ;
                ; This puts a "per-extension" script into the commands to
                ; run on prep.  It runs after the core prep, so that it can
                ; assume things like %rebol.h are available.  (That is
                ; necessary for things like the TCC extension being able to
                ; compile in const data for the header, and tables of API
                ; functions to make available with `tcc_add_symbol()`)
                ;
                hook-script: file-to-local/full (
                    join repo-dir reduce [
                        "extensions/" (ext/directory) (ext/hook)
                    ]
                )
                keep [
                    {$(REBOL)} hook-script
                    unspaced [{OS_ID=} system-config/id]
                ]
            ]
        ]

        keep [
            {$(REBOL)} join tools-dir %make-boot-ext-header.r
            unspaced [
                {EXTENSIONS=} delimit ":" map-each ext extensions [
                    if ext/mode = <builtin> [to text! ext/name]
                ]
            ]
        ]

        keep [{$(REBOL)} join src-dir %main/prep-main.reb]
    ]
    depends: reduce [
        reb-tool
    ]
]

app: make rebmake/application-class [
    name: 'r3-exe
    output: %r3 ;no suffix
    depends: compose [
        (libr3-core)
        ((builtin-ext-objlibs))
        ((app-config/libraries))
        (main)
    ]
    post-build-commands: either cfg-symbols [
        _
    ][
        reduce [
            make rebmake/cmd-strip-class [
                file: join output try rebmake/target-platform/exe-suffix
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

; Now that app is created, make it a dependency of all the dynamic libs
; See `accept` method handling of #application for pulling in import lib
;
for-each proj dynamic-libs [
    append proj/depends app
]

library: make rebmake/dynamic-library-class [
    name: 'libr3
    output: %libr3 ;no suffix
    depends: compose [
        (libr3-core)
        ((builtin-ext-objlibs))
        ((app-config/libraries))
    ]
    searches: app-config/searches
    ldflags: app-config/ldflags
    cflags: app-config/cflags
    optimization: app-config/optimization
    debug: app-config/debug
    includes: app-config/includes
    definitions: app-config/definitions
]

top: make rebmake/entry-class [
    target: 'top  ; phony target

    depends: flatten reduce
        either tmp: select user-config 'top
        [either block? tmp [tmp] [reduce [tmp]]]
        [[ app dynamic-libs ]]

    commands: []
]

t-folders: make rebmake/entry-class [
    target: 'folders  ; phony target

    ; Sort it so that the parent folder gets created first
    ;
    commands: map-each dir sort folders [
        make rebmake/cmd-create-class compose [
            file: (dir)
        ]
    ]
]

clean: make rebmake/entry-class [
    target: 'clean  ; phony target

    commands: reduce [
        make rebmake/cmd-delete-class [file: %objs/]
        make rebmake/cmd-delete-class [file: %prep/]
        make rebmake/cmd-delete-class [
            file: join %r3 try rebmake/target-platform/exe-suffix
        ]
        make rebmake/cmd-delete-class [file: %libr3.*]
    ]
]

check: make rebmake/entry-class [
    target: 'check  ; phony target

    depends: join dynamic-libs app

    commands: collect [
        keep make rebmake/cmd-strip-class [
            file: join app/output try rebmake/target-platform/exe-suffix
        ]
        for-each s dynamic-libs [
            keep make rebmake/cmd-strip-class [
                file: join s/output try rebmake/target-platform/dll-suffix
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
        builtin-ext-objlibs
        libr3-core
        main
        app
        library
        dynamic-libs
        dynamic-ext-objlibs  ; !!! Necessary?
        check
        clean
    ]

    debug: app-config/debug
]

target: user-config/target
if not block? target [target: reduce [target]]
for-each t target [
    switch t targets else [
        fail [
            newline
            newline
            "UNSUPPORTED TARGET" user-config/target newline
            "TRY --HELP TARGETS" newline
        ]
    ]
]
