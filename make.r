Rebol [
    title: "Top-Level Script for building Rebol"
    file: %make.r
    rights: --[
        Rebol 3 Language Interpreter and Run-time Environment
        "Ren-C" branch @ https://github.com/metaeducation/ren-c

        Copyright 2012-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--
    description: --[
        See notes on building in README.md
    ]--
    warning: --[
        This code is not representative of modern practices, because it has
        to run in a very old bootstrap executable.  It is also very much a
        hodgepodge just to keep things running.  It's the absolute wrong place
        to be looking for exemplary Ren-C code.
    ]--
]

if not find (words of import/) 'into [  ; See %import-shim.r
    do <tools/import-shim.r>
]

import <tools/bootstrap-shim.r>

import <tools/common.r>  ; Note: sets up `repo-dir`
import <tools/common-emitter.r>
import <tools/platforms.r>

rebmake: import <tools/rebmake.r>


=== "ADJUST TO AN OUT-OF-SOURCE BUILD IF RUN FROM REPOSITORY's DIRECTORY" ===

; NOTE NEW BEHAVIOR IN REN-C, DIFFERENT FROM HISTORICAL REBOL:
;
; When you run a Ren-C script from the command line, the `current-path` is the
; directory where the user was when the script launched.
;
; We default to building in a subdirectory called %build/ if they launch from
; the repository directory itself.  Otherwise we build wherever they are.

output-dir: ~

if repo-dir = what-dir [
    output-dir: join repo-dir %build/
    make-dir output-dir

    print ["Launched from root dir, so building in:" output-dir]
] else [
    output-dir: what-dir  ; out-of-source build
]

tools-dir: join repo-dir %tools/
src-dir: join repo-dir %src/


=== "GLOBALS" ===

; The file list for the core .c files comes from %file-base.r, while the
; file list for extensions comes from that extension's %make-spec.r
;
file-base: make object! load3 (join repo-dir %tools/file-base.r)

; Start out with a default configuration (may be overridden)
;
user-config: make object! load3 (join repo-dir %configs/default-config.r)


=== "SPLIT ARGS INTO OPTIONS AND COMMANDS" ===

; Note that we should have launched %make.r with the notion of the current
; directory (WHAT-DIR) being the same as what the user had on the command line.
; So relative paths should be interpreted the same.

; args are:
; [OPTION | COMMAND] ...
; COMMAND = WORD
; OPTION = 'NAME=VALUE' | 'NAME: VALUE'
;
args: parse-args system.script.args  ; either from command line or DO:ARGS

; now args are ordered and separated by bar:
; [NAME VALUE ... '| COMMAND ...]
;
options: commands: ~

if (commands: find args '|) [
    options: copy:part args commands
    commands: next commands
]
else [
    options: args
]

for-each [name value] options [
    switch name [
        'config [
            ; A config file can inherit from other configurations with the
            ; `Inherits: %some-config.r` header option.
            ;
            let config: to file! value

            let saved-dir: what-dir

            ; Because the goal is to create an OBJECT! from the top-level
            ; declarations in the file, this has to build an inheritance stack.
            ; It must tunnel to the innermost configs and run those first...
            ; otherwise, the outermost configs wouldn't have the innermost
            ; config fields visible when the run.
            ;
            let config-stack: copy []
            while [config] [
                let dir: split-path3:file config $file
                change-dir dir
                append config-stack (load3 read file)

                ; !!! LOAD has changed between bootstrap versions, for the
                ; handling of the :HEADER.  This just transcodes the file
                ; again to extract the "Inherits" information.
                ;
                ; Note: Inherits may be a good non-config-specific feature.
                ;
                let temp: transcode read file
                assert ['Rebol = first temp]
                config: select ensure block! second temp 'Inherits
            ]
            until [empty? config-stack] [
                user-config: make user-config take:last config-stack
            ]

            change-dir saved-dir
        ]
        'extensions [
            ; [+|-|*] [NAME --[+|-|*|[modules]]--]...
            use [ext-file user-ext][
                user-ext: load3 value
                if not block? user-ext [
                    panic [
                        "Selected extensions must be a block, not"
                        (type of user-ext)
                    ]
                ]
                all [
                    not empty? user-ext
                    find [+ - *] user-ext.1
                ] then [
                    value: take user-ext
                ]
                for-each [name value] user-ext [
                    user-config.extensions.(name): value
                ]
            ]
        ]
    ] else [
        if not has user-config name [
            panic ["Unknown config option on command line to %make.r:" name]
        ]
        name: as word! replace to text! name #"_" #"-"
        set (has user-config name) transcode:one value  ; !!! else [value] ???
    ]
]


=== "CHANGE DIRECTORY" ===

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


=== "PROCESS COMMANDS" ===

if commands [user-config.target: null]  ; was `target: load commands`  Why? :-/


=== "LOAD CFLAGS DATABASE" ===

; To tame this file a bit, the cflags map is in a separate file.

cflags-map: to map! []
left: right: pos: ~

parse3 load3 (join tools-dir %cflags-map.r) [some [
    pos: <here>
    left: tag! '=> right: [tag! | block!] (
        if tag? right [right: reduce [right]]
        cflags-map.(left): right
    )
    |
    <end> accept (okay)
    |
    (panic ["Malformed %cflags-map.r, near:" mold:limit pos 200])
]]


=== "MODULES AND EXTENSIONS" ===

platform-config: configure-platform opt user-config.os-id
rebmake/set-target-platform platform-config.os-base

to-obj-path: func [
    return: [file!]
    file [any-string?]
][
    let ext: find-last file #"."
    if not ext [
        print ["File with no extension" mold file]
    ]
    remove:part ext (length of ext)
    append ext rebmake.target-platform.obj-suffix
    return join %objs/ file
]

gen-obj: func [
    return: "Rebmake specification object for OBJ"
        [object!]
    name "single file representation (bootstrap says file/c for file.c)"
        [file! path! tuple!]
    dir "subdirectory (if applicable)"
        [<opt> file!]
    options "settings in the compiler-switch dialect"
        [<opt> block!]
    :D "definitions" [block!]
    :I "includes" [block!]
    :F "cflags" [block!]
    :main "for main object"
][
    let file: to file! name
    options: default ['[]]

    file: to file! file  ; bootstrap exe loads (file.c) as (file/c)

    let prefer-O2: 'no  ; overrides -Os to give -O2, e.g. for %c-eval.c
    let standard: user-config.standard  ; may have a per-file override
    let rigorous: user-config.rigorous  ; may have a per-file override
    let cplusplus: 'no  ; determined for just this file
    let flags: make block! 8

    for-each 'item options [
        switch item [
            #no-c++ [
                ;
                ; !!! The cfg-cplusplus flag is currently set if any files
                ; are C++.  This means that it's a fair indication that
                ; a previous call to this routine noticed a C++ compiler
                ; is in effect, e.g. the config maps `gcc` tool to `%g++`.
                ;
                if yes? cfg-cplusplus [
                    standard: 'c

                    ; Here we inject "compile as c", but to limit the
                    ; impact (e.g. on C compilers that don't know what -x
                    ; is) we only add the flag if it's a C++ build.  MSVC
                    ; does not need this because it uses the same
                    ; compiler and only needs switches to turn C++ *on*.
                    ;
                    append flags <gcc:-x c>
                ]
            ]
        ]
    ]

    ; Add flags to take into account whether building as C or C++, and which
    ; version of the standard.  Note if the particular file is third party and
    ; can only be compiled as C, we may have overridden that above.
    ;
    insert flags spread switch standard [
        'c [
            []  ; empty for spread
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
            cfg-cplusplus: cplusplus: 'yes
            [
                <gcc:-x c++>
                <msc:/TP>
            ]
        ]
        'c++98 'c++0x 'c++11 'c++14 'c++17 'c++20 'c++latest [
            cfg-cplusplus: cplusplus: 'yes
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
                <gcc:-x c++>

                ; C++ standard, MSVC only supports "c++14/17/latest"
                ;
                (to tag! unspaced ["gnu:--std=" user-config.standard])
                (to tag! unspaced [
                    "msc:/std:" lowercase to text! user-config.standard
                ])

                ; There is a shim for `nullptr` used, that's warned about even
                ; when building as pre-C++11 where it was introduced, unless
                ; you disable that warning.
                ;
                (? if user-config.standard = 'c++98 [<gcc:-Wno-c++0x-compat>])

                ; Note: The C and C++ standards do not dictate if `char` is
                ; signed or unsigned.  If you think environments all settled
                ; on them being signed...Android NDK uses unsigned:
                ;
                ; http://stackoverflow.com/questions/7414355/
                ;
                ; In order to give the option some exercise, make GCC C++
                ; builds use unsigned chars.
                ;
                <gcc:-funsigned-char>

                ; MSVC never bumped the __cplusplus version past 1997, even if
                ; you compile with C++17.  Hence CPLUSPLUS_11 is used by Rebol
                ; code as the switch for most C++ behaviors, and we have to
                ; define that explicitly.
                ;
                <msc:/DCPLUSPLUS_11=1>
            ]
        ]

        panic [
            "STANDARD should be one of"
            "[c gnu89 gnu99 c99 c11 c++ c++11 c++14 c++17 c++latest]"
            "not" (user-config.standard)
        ]
    ]

    append flags <msc:/FC>  ; absolute paths for error messages

    ; It's legal in the C language for a 0 integer literal to act as NULL.
    ; GCC warns about this, but we leverage it in the C build for polymorphism
    ; in terms of how the fail() macro works.  See NEEDFUL_RESULT_0 for
    ; how this is worked around in C++...but the only thing we can do in the C
    ; build to get the desired polymorphism is disable the warning.
    ;
    if no? cplusplus [
        append flags spread [
            <gcc:-Wno-int-conversion>
        ]
    ]

    ; The `rigorous: 'yes` setting in the config turns the warnings up to where
    ; they are considered errors.  However, there are a *lot* of warnings
    ; when you turn things all the way up...and not all of them are relevant.
    ; Still we'd like to get the best information from any good ones, so
    ; they're turned off on a case-by-case basis.
    ;
    ; NOTE: Microsoft's own header files aren't compliant with the errors in
    ; their own /Wall.  So most people use /W4.  It's a matter of philosophy
    ; whether GCC's definition of /Wall or /W4 is the right one ("all warnings"
    ; could mean even those which might not be useful, in which case Microsoft
    ; is more correct).
    ;
    ; https://stackoverflow.com/a/4001759
    ;
    ; For the moment we still use /Wall and omit any errors triggered in MS's
    ; own headers.  If you want to avoid warning 4668 in Windows headers do:
    ;
    ;   #if defined(_MSC_VER)
    ;       #pragma warning(disable : 4668)  // allow #if of undefined things
    ;   #endif
    ;   #include <SomeDisgracefulWindowsHeader.h>
    ;   #include "library-header-including-windows-headers.h"
    ;   #if defined(_MSC_VER)
    ;      #pragma warning(error : 4668)   // disallow #if of undefined things
    ;   #endif
    ;
    ; Alternately if it's just <windows.h>, these warnings go away with:
    ;
    ;   #define WIN32_LEAN_AND_MEAN
    ;   #include <windows.h>
    ;
    append flags spread switch rigorous [
        'yes [
            compose [
                <gcc:-Werror> <msc:/WX>  ; convert warnings to errors

                ; If you use pedantic in a C build on an older GNU compiler,
                ; (that defaults to thinking it's a C89 compiler), it will
                ; complain about using `//` style comments.  There is no
                ; way to turn this complaint off.  So don't use pedantic
                ; warnings unless you're at c99 or higher, or C++.
                ;
                (? if not find [c gnu89] standard [<gcc:--pedantic>])

                <gcc:-Wextra>

                <gcc:-Wall>
                <msc:/Wall>  ; see note above why we use instead of /W4

                ; MSVC has a static analyzer, but it doesn't seem to catch much
                ; and it trips up on the Windows header files a bit.  Disable
                ; it usually because it just makes the build slower.  But
                ; consider running it now and again (or on the CI builds).
                ;
                (comment <msc:/analyze>)

                <gcc:-Wchar-subscripts>
                <gcc:-Wwrite-strings>
                <gcc:-Wundef>
                <gcc:-Wformat=2>
                <gcc:-Wdisabled-optimization>
                <gcc:-Wredundant-decls>
                <gcc:-Woverflow>
                <gcc:-Wpointer-arith>
                <gcc:-Wparentheses>
                <gcc:-Wmain>
                <gcc:-Wtype-limits>

                ; These are GNU flags only (not in clang)
                ;
                <gnu:-Wlogical-op>
                <gnu:-Wclobbered>

                ; Neither C++98 nor C89 had "long long" integers, but they
                ; were fairly pervasive before being present in the standard.
                ;
                <gcc:-Wno-long-long>

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
                        yes? cplusplus
                        find app-config.definitions "NDEBUG"
                    ][
                        <gcc:-Wcast-qual>
                    ][
                        <gcc:-Wno-cast-qual>
                    ]
                )

                ;   'bytes' bytes padding added after construct 'member_name'
                ;
                ; Disable warning C4820; just tells you struct is not an
                ; exactly round size for the platform.
                ;
                <msc:/wd4820>

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
                <gcc:-Wsign-compare>

                ; The majority of Rebol's C code was written with little
                ; attention to overflow in arithmetic.  In many places a
                ; bigger type is converted into a smaller type without an
                ; explicit cast.  (REBI64 => SQLUSMALLINT, REBINT => Byte).
                ; Disable C4242:
                ;
                ;   'identifier' : conversion from 'type1' to 'type2',
                ;   possible loss of data
                ;
                ; The issue needs systemic review.
                ;
                <msc:/wd4242>
                <gcc:-Wno-conversion> <gcc:-Wno-strict-overflow>
                ;<gcc:-Wstrict-overflow=5>

                ; Warning about std::is_pod<OptionWrapper<const Value*>>
                ; having a different answer in different versions, affects the
                ; UNUSED() variable corrupting in checked builds.
                ;
                <msc:/wd4647>

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
                ; CFunction, which holds arbitrary function pointers.  There seems
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

                ; implicit fall-through looking for [[fallthrough]]
                ;
                <msc:/wd5262>

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

                ; const variable is not used, triggers in MS's type_traits
                ;
                <msc:/wd5264>

                ; We actively use global shadowing as a feature, for the
                ; LIBREBOL_BINDING_NAME mechanic.
                ;
                ; https://forum.rebol.info/t/2157
                ;
                <msc:/wd4459>

                ; "possible change in behavior, change in UDT return calling
                ; convention" happens when you return a user-defined datatype
                ; by value from an operator overload.  There's no excuse for
                ; this being a problem.  Returning user-defined datatypes by
                ; value is fully legitimate modern C++ behavior, and MSVC
                ; is complaining because they did a bad optimization at some
                ; point for stream operators.  Ignore this with prejudice.
                ;
                <msc:/wd4686>
            ]
        ]
        'no [
            []  ; empty for SPREAD
        ]

        panic ["RIGOROUS [yes no] not" (rigorous)]
    ]

    ; Now add the flags for the project overall.
    ;
    append flags opt spread F

    ; Ren-C uses labels stylistically to denote sections of code which may
    ; or may not be jumped to.  This is purposeful, and deemed to be more
    ; important than being warned when a goto label is unused.  It's a
    ; sacrifice in rigor, but makes things elegant.
    ;
    append flags <msc:/wd4102>
    append flags <gcc:-Wno-unused-label>

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
    append flags spread [<msc:/wd4917> <msc:/wd4768> <msc:/wd4091>]

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

    ; C++17 added a weird warning about left to right evaluation order that
    ; triggers with (Option(Xxx*) = maybe Yyy()).  Disable that.
    ;
    append flags <msc:/wd4866>

    ; Now add build flags overridden by the inclusion of the specific file
    ; (e.g. third party files we don't want to edit to remove warnings from)
    ;
    for-each 'item options [
        let mapped: try cflags-map.(item)  ; if found, it's a block
        case [
            mapped [
                append flags spread mapped
            ]
            item = #prefer-O2-optimization [
                prefer-O2: 'yes
            ]
            item = #no-c++ [
                standard: 'c
            ]
            <else> [
                append flags (match [text! tag!] item else [
                    panic ["Bad %make.r file options dialect item:" mold item]
                ])
            ]
        ]
    ]

    ; With the flags and settings ready, make a rebmake object and ask it
    ; to build the requested object file.
    ;
    return make rebmake.object-file-class compose [
        source: to file! unspaced [opt dir, file]
        output: to-obj-path file
        cflags: either empty? flags [_] [flags]
        definitions: D
        includes: I
        (? if yes? prefer-O2 [spread [optimization: #prefer-O2-optimization]])
    ]
]

extension-class: make object! [
    class: #extension
    name: ~
    loadable: 'yes  ; can be loaded at runtime

    mode: null  ; [<builtin> <dynamic>] or unused

    modules: null
    sources: null  ; searched for DECLARE_NATIVE()/IMPLEMENT_GENERIC()
    depends: null  ; additional C files compiled in
    requires: null  ; it might require other extensions

    includes: null
    definitions: null
    cflags: null

    searches: null
    libraries: null
    ldflags: null

    hook: null  ; FILE! of extension-specific Rebol script to run during rebmake

    use-librebol: 'no  ; default right now is use %sys-core.h

    ; Internal Fields

    sequence: null  ; the sequence in which the extension should be loaded
    visited: 'no

    directory: ~
]


=== "SCAN EXTENSIONS, CONSTRUCT OBJECTS FROM %MAKE-SPEC.R FOR USED ONES" ===

; The user can ask for an extension to be `-` (not built at all) or `+` (which
; is built into the executable) or `*` (built as dll or so dynamically, and
; can be selectively loaded by the interpreter).
;
; This translates to an ext.mode of <builtin> or <dynamic>.  (Unused extensions
; do not appear in the extensions array.)

extension-dir: join repo-dir %extensions/

extensions: []
extension-names: []  ; used by HELP to generate list of extensions
unmentioned-extensions: []  ; not mentioned in config or command line
skipped-extensions: []  ; explicitly disabled with "-"

for-each 'entry read extension-dir [
    all [
        dir? entry
        find read (join extension-dir entry) %make-spec.r
    ] else [
        continue
    ]

    let make-spec-file: join (join extension-dir entry) %make-spec.r

    let block: load3:header make-spec-file
    let hdr: first block
    let spec: next block

    let ext-name: try hdr.name
    if (not ext-name) or (not word? ext-name) [
        panic [mold make-spec-file "needs WORD! extension Name: in header"]
    ]

    append extension-names ext-name  ; collect names for HELP (used or not)

    let mode: try user-config.extensions.(ext-name)
    if not mode [
        append unmentioned-extensions ext-name
        continue
    ]

    user-config.extensions.(ext-name): void  ; remove

    switch mode [
        '- [
            append skipped-extensions ext-name
            continue  ; don't run %make-spec.r if unused
        ]
        '+ [mode: <builtin>]  ; clearer below
        '* [mode: <dynamic>]
        panic ["Mode for extension" ext-name "must be [- + *]"]
    ]

    ; !!! The specs use `repo-dir` and some other variables.
    ; Splice those in for starters, but will need deep thought.
    ;
    insert spec spread compose [
        name: (quote ext-name)
        mode: (mode)
        repo-dir: (repo-dir)
        platform-config: (platform-config)
        user-config: (user-config)
        directory: (clean-path join extension-dir entry)
    ]

    ; !!! Note: This import does not work, e.g. it won't import shimmed COMPOSE
    ; into a place where the spec would see it.  What we are looking to do
    ; here is an undeveloped feature, of needing a module environment to
    ; import into but also wanting to use a "base module" of definitions--a
    ; feature only available in objects.  If we could do it, it would probably
    ; be precarious in bootstrap.  Instead, the bootstrap-shim puts its versions
    ; of COMPOSE and PARSE into the lib context.
    ;
    let ext: make extension-class compose [
        comment [import (join (clean-path repo-dir) %tools/bootstrap-shim.r)]
        (spread spec)
    ]

    let config
    if has ext 'options [
        ensure block! ext.options
        config: null  ; default for locals in modern Ren-C
        parse3:match ext.options [
            opt some [
                word! block! opt text! config: group!
            ]
        ] else [
            panic ["Could not parse extension build spec" mold spec]
        ]

        if config [
            eval as block! config
        ]
    ]

    ; Blockify libraries
    ;
    ext.libraries: blockify opt ext.libraries

    if (ext.mode = <dynamic>) and (not ext.loadable) [
        panic ["Extension" name "is not dynamically loadable"]
    ]

    append extensions ext
]

if not empty? user-config.extensions [  ; all found ones were removed
    for-each [name val] user-config.extensions [
        print ["!!! Unrecognized extension name in config:" name]
    ]
    panic "Unrecognized extensions, aborting"
]

extension-names: map-each 'x extension-names [
    quote x  ; in bootstrap, can't (delimit "," pin extension-names)
]


=== "TARGETS" ===

; Collected here so they can be used with `--help targets`

targets: [
    'clean [
        rebmake.execution/run make rebmake.solution-class [
            depends: reduce [
                clean
            ]
        ]
    ]
    'prep [
        rebmake.execution/run make rebmake.solution-class [
            depends: flatten reduce [
                vars
                prep
                t-folders
                dynamic-libs
            ]
        ]
    ]
    'app 'executable 'r3 [
        rebmake.execution/run make rebmake.solution-class [
            depends: flatten reduce [
                vars
                t-folders
                dynamic-libs
                app
            ]
        ]
    ]
    'library [
        rebmake.execution/run make rebmake.solution-class [
            depends: flatten reduce [
                vars
                t-folders
                dynamic-libs
                library
            ]
        ]
    ]
    'all 'execution [
        rebmake.execution/run make rebmake.solution-class [
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
        rebmake.makefile/generate %makefile solution
    ]
    'nmake [
        rebmake.nmake/generate %makefile solution
    ]
]

target-names: make block! 16
for-each 'x targets [
    if lit-word? x [
        append target-names (noquote x)
        append target-names '|
    ] else [
        take:last target-names
        append target-names newline
    ]
]


=== "HELP" ===

indent: func [
    text [text!]
    :space
][
    if not space [
        insert text "    "
    ]
    return apply replace/ [
        text
        either space [" "] [newline]
        "^/    "
    ]
]

help-spec: [
  === USAGE ===
  --[
    > PATH/TO/r3-make PATH/TO/make.r [CONFIG | OPTION | TARGET ...]

    NOTE 1: By default current dir is the build directory.
      This holds all the generated stuff (%prep/, %objs/, %makefile, %r3 ...)
      You can set up multiple out-of-source build directories.

    NOTE 2: But if the current dir is the "root" dir (where %make.r is),
      then the build dir is %build/

    NOTE 3: Order of configs and options IS relevant!

    MORE HELP (via { -h | -help | --help }):
        { $<delimit " | " help-topics> }
  ]--

  === TARGETS ===
  --[
    $<indent form target-names>
  ]--

  === CONFIGS ===
  --[
    { config: | load: | do: } PATH/TO/CONFIG-FILE

    FILES IN %make/configs/ SUBFOLDER:

    $<indent:space form sort map-each 'x
        load3 (join repo-dir %configs/)
        [to-text x]
    >
  ]--

  === OPTIONS ===
  --[
    CURRENT VALUES:

    $<indent mold user-config>

    NOTES:

      Names are case-insensitive
      `_` instead of '-' is ok
      NAME=VALUE (OS_ID=0.4.3) is the same as NAME: VALUE (os-id: 0.4.3)
  ]--

  === OS-ID ===
  --[
    CURRENT OS:

    $<indent mold configure-platform opt user-config.os-id>

    AVAILABLE:

    $<indent delimit newline collect [for-each-platform 'p [
        keep spaced [format 8 p.id p.os-name]
    ]]>
  ]--

  === EXTENSIONS ===
  --[
    [FLAG] [ NAME {FLAG|[MODULES]} ... ]

    FLAG:
      + => builtin
      - => disable
      * => dynamic

    NOTE: 1st 'anonymous' FLAG, if present, set the default

    AVAILABLE:
    $<indent delimit newline extension-names>

    EXAMPLES:
      extensions: +
        => enable all extensions as builtin
      extensions: "- gif + jpg * png [lodepng]"
        => disable all extensions but gif (builtin),jpg and png (dynamic)

    CURRENT VALUE:
    $<indent mold user-config.extensions>
  ]--
]

topic: spec: ~  ; no LET in PARSE3

help-topics: collect [parse3 help-spec [
    some ['=== topic: word! '=== spec: text! (
        keep as text! topic
    )]
    (keep "all")
]]

; Now that HELP-TOPICS is gathered (used by HELP-SPEC), we always make the
; HELP-OBJECT from the spec, vs. just when help is asked for.  This makes it
; (more likely that it will stay working)

help-object: make object! collect [parse3 help-spec [
    some [
        '=== topic: word! (keep setify topic) '===
        spec: text! (
            keep $cscape
            let expanded: spaced ["    ===" topic "===" newline spec]
            keep reduce [expanded]
        )
    ]
]]

topic: spec: ~  ; avoid leaks (FWIW)


help: func [
    return: []
    :topic [text!]
][
    if not topic [
        print help-object.usage
        return
    ]
    topic: to-word topic
    print newline
    if topic = 'all [
        for-each [topic msg] help-object [
            print msg
        ]
        return
    ]
    let msg: select help-object topic
    if not msg [
        print ["Unknown topic:" topic]
        print newline
        print help-object.usage
        return
    ]
    print msg
]

; process help: {-h | -help | --help} [TOPIC]

if commands [
    iterate (pin $commands) [
        if find ["-h" "-help" "--help"] commands.1 [
            if second commands [  ; bootstrap commands.2 errors if null
                help:topic second commands
            ] else [
                help
            ]
            quit 0
        ]
    ]
]


=== "SET UP COMPILER AND STRIPPER" ===

; There used to be an option to specify a linker as well, but trying to call
; linkers manually vs. using the front end is fraught with problems.  (Missing
; libraries or paths to produce a nominal working executable, and knowing the
; incantation to pass to the linker to get a working C++ program out is not
; trivial, vs. just calling G++ or CLANG++ to do the link.)
;
; So it's best to just use the compiler as a linker, and if you have special
; switches you need do pass through for those switches through the front end.

rebmake.default-compiler: pick rebmake (any [
    user-config.compiler
    'cc
]) else [
    panic ["Unknown compiler type in configuration:" mold user-config.compiler]
]
rebmake.default-compiler/check opt user-config.compiler-path

rebmake.default-stripper: pick rebmake (any [
    user-config.stripper
    'strip
]) else [
    panic ["Unknown stripper type in configuration:" mold user-config.stripper]
]
rebmake.default-stripper/check opt user-config.stripper-path


=== "GENERATE OVERALL APPLICATION CONFIGURATION" ===

; This appears to put together a baseline of settings that are passed by
; default when building all object files.  So if you requested a checked build
; it would add the compile switch for `-g`, and this would wind up trickling
; down to all the extensions.

app-config: make object! [
    cflags: make block! 8
    ldflags: make block! 8
    libraries: make block! 8
    debug: 'none
    optimization: 2
    definitions: copy []
    includes: reduce [(join src-dir %include/) %prep/include/]
    searches: make block! 8
]

cfg-sanitize: 'off
cfg-symbols: 'off
switch user-config.debug [
    'none [
        append app-config.definitions "NDEBUG"
        app-config.debug: 'off
    ]
    'asserts [
        ; :debug should only affect the "-g -g3" symbol inclusions in rebmake.
        ; To actually turn off asserts or other checking features, NDEBUG must
        ; be defined.
        ;
        app-config.debug: 'off
    ]
    'symbols [  ; No asserts, just symbols.
        app-config.debug: 'on
        cfg-symbols: 'on
        append app-config.definitions "NDEBUG"
    ]
    'normal [
        cfg-symbols: 'on
        app-config.debug: 'on
    ]
    'sanitize [
        app-config.debug: 'on
        cfg-symbols: 'on
        cfg-sanitize: 'on

        append app-config.cflags <gcc:-fsanitize=address>
        append app-config.ldflags <gcc:-fsanitize=address>

        ; MSVC added support for address sanitizer in 2019.  At first it wasn't
        ; great, but it got improved and got working reasonably in March 2021.
        ; You need to link the libraries statically (/MT) not dynamically (/MD)
        ;
        ; !!! This isn't printing line numbers.  /Zi should be included with -g
        ; and does not help (adding it repeatedly here made symbols vanish?)
        ;
        append app-config.cflags <msc:/fsanitize=address>
        append app-config.cflags <msc:/MT>
        append app-config.ldflags <msc:/DEBUG>  ; for better error reporting
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
        cfg-symbols: 'on
        append app-config.cflags "-g"  ; for symbols
        app-config.debug: 'off

        append app-config.definitions spread [
            "NDEBUG"  ; disable assert(), and many other general debug checks

            ; Include debugging features which do not in-and-of-themselves
            ; affect runtime performance (DEBUG_TRACK_EXTEND_CELLS would be an
            ; example of something that significantly affects runtime, and
            ; even things like DEBUG_LEVEL_LABELS adds a tiny bit!)
            ;
            "DEBUG_HAS_PROBE=1"
            "DEBUG_FANCY_CRASH=1"
            "DEBUG_USE_UNION_PUNS=1"
            "INCLUDE_C_DEBUG_BREAK_NATIVE=1"

            ; Adds CALLGRIND, see DECLARE_NATIVE(CALLGRIND) for implementation
            ;
            "INCLUDE_CALLGRIND_NATIVE=1"
        ]
    ]

    panic ["unrecognized debug setting:" user-config.debug]
]

switch user-config.optimize [
    0 1 2 3 4 's 'z 'g [
        app-config.optimization: user-config.optimize
    ]
] else [
    panic ["Optimization setting unknown:" user-config.optimize]
]

cfg-cplusplus: 'no  ; gets set to true if linked as c++ overall

; pre-vista switch
; Example. Mingw32 does not have access to windows console api prior to vista.
;
cfg-pre-vista: 'no
append app-config.definitions spread switch user-config.pre-vista [
    'yes [
        cfg-pre-vista: 'yes
        compose [
            "PRE_VISTA"
        ]
    ]
    'no [
        cfg-pre-vista: 'no
        []  ; empty for spread
    ]

    panic ["PRE-VISTA [yes no] not" (user-config.pre-vista)]
]


append app-config.ldflags spread switch user-config.static [
    'no [
        []  ; empty for spread
    ]
    'yes [
        compose [
            <gcc:-static-libgcc>
            (? if yes? cfg-cplusplus [<gcc:-static-libstdc++>])
            (? if on? cfg-sanitize [<gcc:-static-libasan>])
        ]
    ]

    panic ["STATIC must be [yes no] not" (user-config.static)]
]


=== "ADD SYSTEM SETTINGS" ===

; Not quite sure what counts as system definitions (?)  Review.

append app-config.definitions spread flatten:deep (
    reduce bind platform-definitions (copy platform-config.definitions)
)

append app-config.cflags spread flatten:deep (  ; !!! can be string?
    reduce bind compiler-flags copy platform-config.cflags
)

append app-config.libraries spread (
    let value: flatten:deep reduce (
        bind platform-libraries copy platform-config.libraries
    )
    map-each 'w flatten value [
        make rebmake.ext-dynamic-class [
            output: w
        ]
    ]
)

append app-config.ldflags spread flatten:deep (
    reduce bind linker-flags copy platform-config.ldflags
)

print ["definitions:" mold app-config.definitions]
print ["includes:" mold app-config.includes]
print ["libraries:" mold app-config.libraries]
print ["cflags:" mold app-config.cflags]
print ["ldflags:" mold app-config.ldflags]
print ["debug:" app-config.debug]
print ["optimization:" app-config.optimization]

append app-config.definitions spread reduce [
    cscape [platform-config
        "TO_${PLATFORM-CONFIG.OS-BASE}=1"
    ]
    cscape [platform-config
        "TO_${PLATFORM-CONFIG.OS-NAME}=1"
    ]
]

; Add user settings (can be null)
;
append app-config.definitions opt spread user-config.definitions
append app-config.includes opt spread user-config.includes
append app-config.cflags opt spread user-config.cflags
append app-config.libraries opt spread user-config.libraries
append app-config.ldflags opt spread user-config.ldflags

libr3-core: make rebmake.object-library-class [
    name: 'libr3-core
    definitions: app-config.definitions

    ; might be modified by the generator, thus copying
    includes: append copy app-config.includes %prep/core

    ; might be modified by the generator, thus copying
    cflags: copy app-config.cflags

    optimization: app-config.optimization
    debug: app-config.debug
    depends: collect [
        let core-dir: join src-dir %core/
        let subdir: null
        let item
        let options
        let item-rule: [
            item: [tuple! | path!] options: try block! (
                keep gen-obj item subdir opt options
            )
        ]
        parse3 file-base.core [some [
            ahead [path! '->] subdir: path! '-> ahead block! into [
                (subdir: join core-dir to file! subdir)
                some item-rule
                (subdir: core-dir)
            ]
            |
            item-rule
        ]
    ]]
    append depends spread map-each 'w file-base.generated [
        gen-obj w %prep/ (<no-options> [])
    ]
]

main: make libr3-core [
    name: 'main

    definitions: append copy ["REB_CORE"] spread app-config.definitions

    ; The generator may modify these.
    ;
    includes: append copy app-config.includes %prep/main
    cflags: copy app-config.cflags

    depends: reduce [
        either user-config.main [
            gen-obj user-config.main (<no-directory> null) (<no-options> [])
        ][
            gen-obj file-base.main (join src-dir %main/) (<no-options> [])
        ]
    ]
]

pthread: make rebmake.ext-dynamic-class [
    output: %pthread
    flags: [static]
]


=== "GATHER LIST OF FOLDERS THAT MUST BE CREATED" ===

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

folders: copy [%objs/ %objs/core/ %objs/main/]

add-new-obj-folders: func [
    return: []
    objs
    folders
    <local>
    lib
    obj
    dir
][
    for-each 'lib objs [
        switch lib.class [
            #object-file [
                lib: reduce [lib]
            ]
            #object-library [
                lib: lib.depends
            ]
            (elide dump lib)
            panic ["unexpected class"]
        ]

        for-each 'obj lib [
            dir: split-path3 obj.output
            if not find folders dir [
                append folders dir
            ]
        ]
    ]
]


for-each [category entries] file-base [
    if category = 'generated  [
        continue  ; taken care of elsewhere
    ]
    let dir
    if match [tuple! path!] entries [  ; main.c
        entries: reduce [entries]
    ]
    parse3 entries [opt some [
        ahead [path! '->] dir: path! '-> block! (
            append folders join %objs/ to file! dir
        )
        |
        [tuple! | path!] opt block!
    ]]
]

print newline

for-each [mode label] [
    <builtin> "BUILTIN (+) EXTENSIONS:"
    <dynamic> "DYNAMIC (*) EXTENSIONS:"
][
    print [label mold collect [
        for-each 'ext extensions [
            if ext.mode = mode [
                keep ext.name
            ]
        ]
    ] newline]
]

print ["SKIPPED (-) EXTENSIONS:" mold skipped-extensions, newline]

print ["UNMENTIONED EXTENSIONS:" mold unmentioned-extensions, newline]

add-project-flags: func [
    return: []
    project [object!]
    :I "includes" [block!]
    :D "definitions" [block!]
    :c "cflags" [block!]
    :O "optimization" [word! integer!]
    :g "debug" [onoff?]
][
    assert [
        find [
            #dynamic-library
            #object-library
            #static-library
            #application
        ] project.class
    ]

    if D [
        if null? project.definitions [
            project.definitions: D
        ]
        else [
            append project.definitions spread D
        ]
    ]

    if I [
        if null? project.includes [
            project.includes: I
        ] else [
            append project.includes spread I
        ]
    ]
    if c [
        if null? project.cflags [
            project.cflags: c
        ]
        else [
            append project.cflags spread c
        ]
    ]
    if not null? g [project.debug: g]  ; could just be IF G in new R3
    if O [project.optimization: O]
]


=== "REORDER EXTENSIONS BASED ON THEIR 'REQUIRES' DEPENDENCIES" ===

; When built in extensions are loading, they can say what other extensions
; they require.  Here a sequence is calculated to make sure they are loaded
; in the right order.
;
; Calculating this ahead of time is a limited approach, as it doesn't help
; when a dynamically loaded extension requires some other extension (which
; may or may not be dynamic.)

calculate-sequence: func [
    return: [integer!]
    ext
][
    if ext.sequence [return ensure integer! ext.sequence]
    if yes? ext.visited [panic ["circular dependency on" ext]]
    if null? ext.requires [ext.sequence: 0 return ext.sequence]
    ext.visited: 'yes
    let seq: 0
    if word? ext.requires [ext.requires: reduce [ext.requires]]
    for-each 'req ext.requires [
        for-each 'b extensions [
            if b.name = req [
                seq: seq + any [
                    match integer! opt b.sequence
                    calculate-sequence b
                ]
                break
            ]
        ] then [  ; didn't BREAK, so no match found
            panic ["unrecoginized dependency" req "for" ext.name]
        ]
    ]
    return ext.sequence: seq + 1
]

for-each 'ext extensions [calculate-sequence ext]
sort:compare extensions func [a b] [return a.sequence < b.sequence]


=== "PRODUCE COMPILER/LINKER PROCESSABLE OBJECTS FROM EXTENSION SPECS" ===

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

for-each 'ext extensions [
    assert [ext.class = #extension]  ; basically what we read from %make-spec.r

    switch ext.mode [
        null [continue]  ; not in use, don't add it to the build process
        <builtin> [noop]
        <dynamic> [noop]
        panic
    ]

    let ext-prep-dir: cscape [ext
        %prep/extensions/$<ext.name>/
    ]

    let ext-objlib: make rebmake.object-library-class [  ; #object-library
        name: ext.name

        depends: collect [let name, let options, let obj, parse3 (
            append copy ext.sources opt spread ext.depends
        ) [some [
            name: [file! | tuple! | path!] options: try [block!] (
                let file: to file! name
                keep gen-obj file ext.directory opt options
            )
            |
            obj: [object!] (
                if find [#object-library #object-file] obj.class [
                    keep s
                    ; #object-library has already been taken care of above
                ]
                else [
                    veto
                ]
            )
            |
            name: one
            (panic [type of name "can't be a dependency of a module"])
        ]]]

        libraries: map-each 'lib opt ext.libraries [
            case [
                file? lib [
                    make rebmake.ext-dynamic-class [
                        output: lib
                    ]
                ]
                (object? lib) and (
                    find [#dynamic-extension #static-extension] lib.class
                )[
                    lib
                ]
                panic ["unrecognized library" lib "in extension" ext]
            ]
        ]

        includes: collect [
            for-each 'inc opt ext.includes [
                ensure file! inc
                if inc.1 = #"/" [  ; absolute path
                    keep inc
                    continue
                ]
                keep join ext.directory inc
            ]
            keep ext-prep-dir  ; to find %prep/extensions/xxx/tmp-mod-xxx.h
            keep ext.directory  ; to find includes in the dir itself
        ]

        definitions: ext.definitions
        cflags: ext.cflags
        searches: ext.searches
    ]

    ; %prep-extensions.r creates a temporary .c file which contains the
    ; collated information for the module (compressed script and spec bytes,
    ; array of dispatcher CFunction pointers for the natives) and RX_Collate
    ; function.  It is located in the %prep/ directory for the extension.
    ;
    let ext-init-source: cscape [ext
        %tmp-mod-$<ext.name>-init.c
    ]
    append ext-objlib.depends gen-obj // [
        ext-init-source
        ext-prep-dir
        []  ; no options dialect
        I: ext.includes
        D: ext.definitions
        F: ext.cflags
    ]

    ; Here we graft things like the global debug settings and optimization
    ; flags onto the extension.  This also lets it find the core include files
    ; like %sys-core.h or %reb-config.h.  Long term, most-if-not-all extensions
    ; should be restricted to use the public API and not %sys-core.h
    ;
    ; Not only do these settings get used by the mod-xxx.c file, but they are
    ; propagated to the dependencies (I think?)
    ;
    ; If we're building as a DLL, we need to #define LIBREBOL_USES_API_TABLE
    ; to 1, because there's no generalized
    ;
    add-project-flags // [
        ext-objlib
        I: app-config.includes
        D: compose [
            (? if ext.mode = <dynamic> ["LIBREBOL_USES_API_TABLE=1"])
            (spread app-config.definitions)
        ]
        c: app-config.cflags
        O: app-config.optimization
        g: app-config.debug
    ]

    if ext.mode = <builtin> [
        append builtin-ext-objlibs opt ext-objlib

        ; While you can have varied compiler switches in effect for individual
        ; C files to make OBJs, you only get one set of linker settings to make
        ; one executable.  It's a limitation of the <builtin> method.
        ;
        ; !!! searches seems to correspond to the -L switch.  Strange name to
        ; have to remember, and so the make-spec.r have been doing that as an
        ; ldflag, but this then has to be platform specific.  But generally you
        ; already need platform-specific code to know where to look.
        ;
        append app-config.libraries opt spread ext-objlib.libraries
        append app-config.ldflags opt spread ext.ldflags
        append app-config.searches opt spread ext.searches
    ]
    else [
        append dynamic-ext-objlibs ext-objlib

        ; We need to make a new "Project" abstraction to represent the DLL

        ext-proj: make rebmake.dynamic-library-class [
            name: join either platform-config.os-base = 'Windows ["r3-"]["libr3-"]
                lowercase to text! ext.name
            output: to file! name
            depends: compose [
                (ext-objlib)  ; !!! Pulls in in all of extensions deps?
                ;
                ; (app) all dynamic extensions depend on r3, but app not ready
                ; so the dependency is added at a later phase below
                ;
                (opt spread app-config.libraries)
                (opt spread ext-objlib.libraries)
            ]
            post-build-commands: all [
                off? cfg-symbols
                reduce [
                    make rebmake.cmd-strip-class [
                        file: join output opt rebmake.target-platform.dll-suffix
                    ]
                ]
            ]

            ldflags: compose [
                (opt spread ext.ldflags)
                (opt spread app-config.ldflags)

                ; GCC has this but Clang does not, and currently Clang is
                ; being called through a gcc alias.  Review.
                ;
                ;<gcc:-Wl,--as-needed>  ; Switch ignores linking unused libs
            ]
        ]

        ; !!! It's not clear if this is really needed (?)
        ;
        add-project-flags // [
            ext-proj
            I: app-config.includes
            D: compose [
                "LIBREBOL_USES_API_TABLE=1"
                (spread app-config.definitions)
            ]
            c: app-config.cflags
            O: app-config.optimization
            g: app-config.debug
        ]

        append dynamic-libs ext-proj  ; need to add app as a dependency later
    ]

    ; For all the obj files needed by this extension, ensure the output dir
    ; is known to neeed to be created
    ;
    add-new-obj-folders (reduce [ext-objlib]) folders
]


=== "RUN THE MAKE (INVOKE COMPILER WITH CALL -OR- GENERATE MAKEFILE" ===

; Here we run MAKE which will not do the actual compilation if you ask the
; TARGET to be a makefile.  It only runs the compilation if you are using
; Rebmake to run the compiler via CALL.

reb-tool: ~  ; used in code below :-/

vars: reduce [
    reb-tool: make rebmake.var-class [
        name: "REBOL_TOOL"
        any [
            'file = exists? value: system.options.boot
            all [
                user-config.rebol-tool
                'file = exists? value: join repo-dir user-config.rebol-tool
            ]
            'file = exists? value: join repo-dir unspaced [
                "r3-make"
                rebmake.target-platform.exe-suffix
            ]
        ] else [
            panic "^/^/!! Cannot find a valid REBOL_TOOL !!^/"
        ]

        ; Originally this didn't transform to a local file path (e.g. with
        ; backslashes instead of slashes on Windows).  There was some reason it
        ; worked in visual studio, but not with nmake.
        ;
        value: file-to-local value
    ]
    make rebmake.var-class [
        name: "REBOL"
        value: "$(REBOL_TOOL) -q"
    ]
    make rebmake.var-class [
        name: "T"
        value: join src-dir %tools/
    ]
    make rebmake.var-class [
        name: "GIT_COMMIT"
        default: any [user-config.git-commit "unknown"]
    ]
]

prep: make rebmake.entry-class [
    target: 'prep ; phony target

    commands: collect [
        /keep: adapt keep/ [
            if block? value [
                value: spaced value  ; old append semantics
            ] else [
                value: quote spaced unquote value  ; new append ^META semantics
            ]
        ]

        keep ["$(REBOL)" join tools-dir %make-types.r]
        keep ["$(REBOL)" join tools-dir %make-natives.r]
        keep ["$(REBOL)" join tools-dir %make-headers.r]
        keep [
            "$(REBOL)" join tools-dir %make-boot.r
            unspaced ["OS_ID=" mold platform-config.id]
            "GIT_COMMIT=$(GIT_COMMIT)"
        ]
        keep [
            "$(REBOL)" join tools-dir %make-librebol.r
            unspaced ["OS_ID=" mold platform-config.id]
        ]

        for-each 'ext extensions [
            let name
            let molded-sources: mold collect [
                parse3 ext.sources [some [
                    name: [tuple! | path! | file!] opt block! (
                        keep to file! name
                    )
                ]]
            ]
            replace molded-sources newline space

            keep [
                "$(REBOL)" join tools-dir %prep-extension.r
                unspaced ["MODULE=" ext.name]
                unspaced ["DIRECTORY=" ext.directory]
                unspaced [-[SOURCES="]- molded-sources -["]-]  ; BLOCK of FILE
                unspaced ["OS_ID=" mold platform-config.id]
                unspaced ["USE_LIBREBOL=" ext.use-librebol]
            ]

            if ext.hook [
                ;
                ; This puts a "per-extension" script into the commands to
                ; run on prep.  It runs after the core prep, so that it can
                ; assume things like %rebol.h are available.  (That is
                ; necessary for things like the TCC extension being able to
                ; compile in const data for the header, and tables of API
                ; functions to make available with `tcc_add_symbol()`)
                ;
                hook-script: file-to-local:full (
                    join repo-dir spread reduce [
                        "extensions/" (ext.directory) (ext.hook)
                    ]
                )
                keep [
                    "$(REBOL)" hook-script
                    unspaced ["OS_ID=" mold platform-config.id]
                ]
            ]
        ]

        keep [
            "$(REBOL)" join tools-dir %make-extensions-table.r
            unspaced [
                "EXTENSIONS=" delimit ":" map-each 'ext extensions [
                    when ext.mode = <builtin> [as text! ext.name]
                ]
            ]
        ]

        keep ["$(REBOL)" join src-dir %main/prep-main.r]
    ]
    depends: reduce [
        reb-tool
    ]
]

app: make rebmake.application-class [
    name: 'r3-exe
    output: %r3  ; no suffix
    depends: compose [
        (libr3-core)
        (spread builtin-ext-objlibs)
        (spread app-config.libraries)
        (main)
    ]
    post-build-commands: either on? cfg-symbols [
        null
    ][
        reduce [
            make rebmake.cmd-strip-class [
                file: join output opt rebmake.target-platform.exe-suffix
            ]
        ]
    ]

    searches: app-config.searches
    ldflags: app-config.ldflags
    cflags: app-config.cflags
    optimization: app-config.optimization
    debug: app-config.debug
    includes: app-config.includes
    definitions: app-config.definitions
]

; Now that app is created, make it a dependency of all the dynamic libs
; See `accept` method handling of #application for pulling in import lib
;
for-each 'proj dynamic-libs [
    append proj.depends app
]

library: make rebmake.dynamic-library-class [
    name: 'libr3
    output: %libr3  ; no suffix
    depends: compose [
        (libr3-core)
        (spread builtin-ext-objlibs)
        (spread app-config.libraries)
    ]
    searches: app-config.searches
    ldflags: app-config.ldflags
    cflags: app-config.cflags
    optimization: app-config.optimization
    debug: app-config.debug
    includes: app-config.includes
    definitions: app-config.definitions
]

top: make rebmake.entry-class [
    target: 'top  ; phony target

    depends: flatten reduce
        either tmp: select user-config 'top
        [either block? tmp [tmp] [reduce [tmp]]]
        [[ app dynamic-libs ]]

    commands: []
]

t-folders: make rebmake.entry-class [
    target: 'folders  ; phony target

    ; Sort it so that the parent folder gets created first
    ;
    commands: map-each 'dir sort folders [
        make rebmake.cmd-create-class compose [
            file: (dir)
        ]
    ]
]

clean: make rebmake.entry-class [
    target: 'clean  ; phony target

    commands: reduce [
        make rebmake.cmd-delete-class [file: %objs/]
        make rebmake.cmd-delete-class [file: %prep/]
        make rebmake.cmd-delete-class [
            file: join %r3 rebmake.target-platform.exe-suffix
        ]
        make rebmake.cmd-delete-class [file: %libr3.*]
    ]
]

check: make rebmake.entry-class [
    target: 'check  ; phony target

    depends: append (copy dynamic-libs) app

    commands: collect [
        keep make rebmake.cmd-strip-class [
            file: join app.output opt rebmake.target-platform.exe-suffix
        ]
        for-each 's dynamic-libs [
            keep make rebmake.cmd-strip-class [
                file: join s.output opt rebmake.target-platform.dll-suffix
            ]
        ]
    ]
]

solution: make rebmake.solution-class [
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

    debug: app-config.debug
]

target: user-config.target
if not block? target [target: reduce [target]]
for-each 't target [
    switch t targets else [
        panic [
            newline
            newline
            "UNSUPPORTED TARGET" user-config.target newline
            "TRY --HELP TARGETS" newline
        ]
    ]
]
