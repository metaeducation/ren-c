Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "Make primary boot files"
    file: %make-boot.r  ; used by EMIT-HEADER to indicate emitting script
    rights: --[
        Copyright 2012 REBOL Technologies
        Copyright 2012-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    version: 2.100.0
    needs: 2.100.100
    purpose: --[
        A lot of the REBOL system is built by REBOL, and this program
        does most of the serious work. It generates most of the C include
        files required to compile REBOL.
    ]--
]

if not find (words of import/) 'into [  ; See %import-shim.r
    do <import-shim.r>
]

print "--- Make Boot : System Embedded Script ---"

import <bootstrap-shim.r>

import <common.r>
import <common-emitter.r>

import <platforms.r>

change-dir join repo-dir %src/specs/

args: parse-args system.script.args  ; either from command line or DO:ARGS
platform-config: configure-platform select args 'OS_ID

first-rebol-commit: "19d4f969b4f5c1536f24b023991ec11ee6d5adfb"

git-commit: select args 'GIT_COMMIT
if git-commit = "unknown" [
    git-commit: null
] else [
    if (length of git-commit) != (length of first-rebol-commit) [
        panic [
            "GIT_COMMIT should be a full hash, e.g." first-rebol-commit newline
            "Invalid hash was:" git-commit
        ]
    ]
]

=== "SETUP PATHS AND MAKE DIRECTORIES (IF NEEDED)" ===

prep-dir: join system.options.path %prep/

; trust that %make-types.r created prep/core, prep/include, prep/boot...


=== "MAKE VERSION INFORMATION AVAILABLE TO CORE C CODE" ===

e-version: make-emitter "Version Information" (
    join prep-dir %include/tmp-version.h
)

version: transcode:one read %version.r
version: join tuple! [
    version.1 version.2 version.3 platform-config.id.2 platform-config.id.3
]

e-version/emit [version --[
    /*
     * VERSION INFORMATION
     *
     * !!! While using 5 byte-sized integers to denote a Rebol version might
     * not be ideal, it's a standard that's been around a long time.
     *
     * (Using years would make more sense, e.g. version 2025)
     */
    #define REBOL_VER $<version.1>
    #define REBOL_REV $<version.2>
    #define REBOL_UPD $<version.3>
    #define REBOL_SYS $<version.4>
    #define REBOL_VAR $<version.5>
]--]

e-version/write-emitted


=== "SET UP COLLECTION OF SYMBOL NUMBERS" ===

sym-table: copy []

; Symbols are added to the list without corresponding to any specific number.
; It will try to preserve the symbols in the order you add them, but once
; you add the <reorderable> placeholder all symbols after that point are
; subject to reordering.
;
add-sym: func [
    "Add SYM_XXX or <PLACEHOLDER> signal to enumeration (order may adjust)"
    return: "position of an already existing symbol if found"
        [null? block!]
    item "If TAG!, then the / in </FOO> means mark *before* last added symbol"
        [word! text! tag! block!]
    :relax "tolerate an already-added symbol"
    :placeholder "add marker, filtered out and made a #define at end"
][
    if placeholder [
        case [
            tag! [if find item "-" [
                panic [
                    "Use _ not - in placeholders so it's easier to search"
                    "for references matching the generated C macros"
                ]
            ]]
            block! [noop]
            panic "ADD-SYM:PLACHOLDER must be TAG! or BLOCK!"
        ]
        append sym-table item
        return null
    ]

    let name: case [
        text? item [item]
        word? item [
            to text! item  ; force word to text
        ]
    ] else [
        panic ["ADD-SYM without :PLACEHOLDER requires WORD! or TEXT!"]
    ]

    let pos: find sym-table name
    if pos: find sym-table name [
        if relax [return pos]
        probe sym-table
        panic ["Duplicate symbol string specified:" name]
    ]

    ; The OF native interprets things like LENGTH OF by looking up LENGTH-OF
    ; and dispatching it.  Help it do the symbol mapping from XXX to XXX-OF
    ; for all the built-in XXX-OF natives by placing the XXX-OF symbol
    ; directly after XXX.  This may mean putting it after an already added
    ; symbol, or the additional symbol will be added here.
    ;
    ; 1. We can't do this for some things, e.g. SYM_SIGIL needs its position
    ;    to be fixed in the datatypes.  So SIGIL-OF is handled specially.
    ;
    let base
    all [
        parse3:match name [base: across to "-of" "-of"]
        base <> "sigil"  ; can't do optimization [1]
        base <> "file"  ; also needs an exception
    ] then [
        if pos: find sym-table base [
            if find pos <MAX_SYM_LIB_PREMADE> [
                panic ["Reorder would disrupt symbol ordering for:" name]
            ]
            insert (next pos) name  ; put it after existing entry
            return null
        ]
        append sym-table base  ; put it in before adding the -OF variant
    ]

    append sym-table name
    return null
]

=== "LOAD TYPESET BYTE MAPPING" ===

; At one time, processing of the %types.r table was done in this file.  Now
; that is handled in %make-types.r and we merely read the product of that
; process, e.g. a table of TypesetByte, like:
;
;    integer 1
;    decimal 2
;    ...
;    any-list 89
;    any-bindable 90
;    any-element 91
;
; We use this table to make symbols, e.g. SYM_INTEGER or SYM_INTEGER_X for
; `integer!` or SYM_INTEGER_Q for `integer?`

name-to-typeset-byte: load3 (join prep-dir %specs/tmp-typeset-bytes.r)


=== "SYMBOLS FOR BUILTIN TYPESETS" ===

add-sym:placeholder <MIN_SYM_TYPESETS>

for-each [name byte] name-to-typeset-byte [
    if name = '~ [
        name: unspaced ["antiform-" byte]
    ]
    add-sym unspaced [name "?"]
]

add-sym:placeholder </MAX_SYM_TYPESETS>


=== "SYMBOLS FOR LIB-WORDS.R" ===

; Add SYM_XXX constants for the words in %lib-words.r - these are words that
; reserve a spot in the lib context.  They can be accessed quickly, without
; going through a hash table.
;
; Since the relative order of these words is honored, that means they must
; establish their slots first.  Any natives or types which have the same
; name will have to use the slot position established for these words.

for-each 'term load3 %lib-words.r [
    case [
        rune? term [
            term: as word! term
            if not add-sym:relax term [  ; returns POS if already present
                panic ["Expected symbol for" term "from [native type]"]
            ]
        ]
        tag? term [  ; want to mark things like where parse keywords are
            add-sym:placeholder term
        ]
        <default> [
            add-sym term
        ]
    ]
]


=== "ESTABLISH SYM_XXX VALUES FOR EACH NATIVE" ===

; It's desirable for the core to be able to get the Value* for a native
; quickly just by indexing into a table.  An aspect of optimizations related
; to that is that the SYM_XXX values for the names of the natives index into
; a fixed block.  We put them after the ordered words in lib.

add-sym:placeholder <MIN_SYM_NATIVE>

native-names: copy []

boot-natives: stripload:gather (
    join prep-dir %specs/tmp-natives.r
) $native-names

insert boot-natives "["
append boot-natives "]"
for-each 'name native-names [
    let pos: add-sym:relax name
    if not pos [  ; not a duplicate
        continue
    ]
    if not find pos <MIN_SYM_NATIVE> [
        panic ["Native name collision found:" name]
    ]
]

add-sym:placeholder </MAX_SYM_LIB_PREMADE>


=== "SYMBOLS FOR NATIVE PARAMETERS AND REFINEMENTS" ===

; This lets us talk about the parameters to natives via CANON(PARAM_NAME).
; These don't necessarily have corresponding variables in LIB (though they
; might, so we use ADD-SYM:RELAX).

native-param-symbols: load3 join prep-dir %specs/tmp-param-symbols.r
for-each 'param native-param-symbols [
    add-sym:relax param
]
native-param-symbols: ~#[native-param-symbols accounted for]#~


=== "SYMBOLS FOR DATATYPES (INTEGER! or BLOCK!, with the !)" ===

; INTEGER? and BLOCK? are type constraint functions in SYS.CONTEXTS.LIB, but
; actual datatype instances live in SYS.CONTEXTS.DATATYPES - which is also
; where extension types are put.  Hence it's not necessary to allocate a
; variable for INTEGER! or BLOCK! in LIB.  These are past MAX_SYM_LIB_PREMADE,
; but still included in MAX_SYM_BUILTIN (the Symbol Stubs are preallocated)

add-sym:placeholder <MIN_SYM_BUILTIN_TYPES>

for-each [name byte] name-to-typeset-byte [
    case [
        name = '~ [
            name: unspaced ["antiform-" byte]
        ]
        find as text! name "any" [
            break  ; done with just the datatypes
        ]
    ]
    add-sym unspaced [name "!"]  ; integer! holds ~{integer}~
]

add-sym:placeholder </MAX_SYM_BUILTIN_TYPES>


=== "SYMBOLS FOR SYMBOLS.R" ===

; The %symbols.r file are terms that get SYM_XXX constants and an entry in
; the table for turning those constants into a symbol pointer.  But they do
; not have priority on establishing declarations in lib.  Hence a native or
; type might come along and use one of these terms...meaning they have to
; yield to that position.  That's why there's no guarantee of order.

for-each 'item load3 %symbols.r [
    switch type of item [
        word! [add-sym item]
        rune! [
            if not find sym-table as text! item [
                panic ["Expected symbol for" item "from [native type]"]
            ]
        ]
        panic ["bad %symbols.r item:" mold item]
    ]
]


=== "SYSTEM OBJECT SELECTORS" ===

e-sysobj: make-emitter "System Object" (
    join prep-dir %include/tmp-sysobj.h
)

at-value: func [field] [return next find boot-sysobj setify field]

boot-sysobj: load3 %sysobj.r
change (at-value 'version) version
change (at-value 'commit) opt git-commit  ; no-op if no git-commit
change (at-value 'build) now:utc
change (at-value 'product) (quote to word! "core")  ; want it to be quoted

change at-value 'platform reduce [
    any [platform-config.name "Unknown"]
    any [platform-config.build-label ""]
]

c-debug-break: ~#[C-DEBUG-BREAK applies in non-bootstrap %sysobj.r only]#~

ob: make object! boot-sysobj

c-debug-break: get $lib.c-debug-break

make-obj-defs: proc [
    "Given a Rebol OBJECT!, write C structs that can access its raw variables"

    e "The emitter to write definitions to"
        [object!]
    obj
    prefix
    depth
][
    let n: 1

    let items: collect [
        for-each 'field words-of obj [
            keep cscape [prefix field n "${PREFIX}_${FIELD} = $<n>"]
            n: n + 1
        ]
    ]

    e/emit [prefix items n --[
        enum ${PREFIX}_object {
            $(Items),
        };
        #define MAX_${PREFIX}  $<n - 1>
    ]--]

    if depth > 1 [
        for-each 'field words-of obj [
            if all [
                field != 'standard
                object? opt get has obj field
            ][
                let extended-prefix: uppercase unspaced [prefix "_" field]
                make-obj-defs e obj.(field) extended-prefix (depth - 1)
            ]
        ]
    ]
]

make-obj-defs e-sysobj ob "SYS" 1
make-obj-defs e-sysobj ob.catalog "CAT" 4
make-obj-defs e-sysobj ob.contexts "CTX" 4
make-obj-defs e-sysobj ob.standard "STD" 4
make-obj-defs e-sysobj ob.state "STATE" 4
;make-obj-defs e-sysobj ob.network "NET" 4
make-obj-defs e-sysobj ob.ports "PORTS" 4
make-obj-defs e-sysobj ob.options "OPTIONS" 4
;make-obj-defs e-sysobj ob.intrinsic "INTRINSIC" 4
make-obj-defs e-sysobj ob.locale "LOCALE" 4

e-sysobj/write-emitted


=== "ERROR STRUCTURE AND CONSTANTS" ===

e-errfuncs: make-emitter "Error structure and functions" (
    join prep-dir %include/tmp-error-funcs.h
)

fields: collect [
    for-each 'word words-of ob.standard.error [
        either word = 'near [
            keep --[/* near & far are old C keywords */ Slot nearest]--
        ][
            keep cscape [word "Slot ${word}"]
        ]
    ]
]

e-errfuncs/emit [fields --[
    /*
     * STANDARD ERROR STRUCTURE
     */
    typedef struct REBOL_Error_Vars {
        $[Fields];
    } ERROR_VARS;
]--]

e-errfuncs/emit [--[
    /*
     * The variadic Make_Error_Managed() function must be passed the exact
     * number of fully resolved Value* that the error spec specifies.  This is
     * easy to get wrong in C, since variadics aren't checked.  Also, the
     * category symbol needs to be right for the error ID.
     *
     * These are inline function stubs made for each "raw" error in %errors.r.
     * They shouldn't add overhead in release builds, but help catch mistakes
     * at compile time.
     */
]--]

add-sym:placeholder <MIN_SYM_ERRORS>

boot-errors: load3 %errors.r

for-each [sw-cat list] boot-errors [
    assert [set-word? sw-cat]
    let cat: resolve sw-cat
    ensure block! list

    add-sym cat  ; category might incidentally exist as SYM_XXX

    for-each [sw-id t-message] list [
        if not set-word? sw-id [
            panic ["%errors.r parse error, not SET-WORD!" mold sw-id]
        ]
        let id: resolve sw-id
        let message: t-message

        ; Add a SYM_XXX constant for the error's ID word
        ;
        let pos: add-sym:relax id
        if pos [
            if not find pos <MIN_SYM_ERRORS> [
                panic ["Duplicate error ID found:" id]
            ]
        ]

        let arity: 0
        if block? message [  ; can have N GET-WORD! substitution slots
            ; This uses GET-WORD!...review rationale for why
            parse3 message [opt some [get-word?/ (arity: arity + 1) | one]]
        ] else [
            ensure text! message  ; textual message, no arguments
        ]

        ; Camel Case and make legal for C (e.g. "not-found*" => "Not_Found_P")
        ;
        let f-name: uppercase:part to-c-name id 1
        let w
        parse3 f-name [
            opt some [
                "_" w: <here>
                (uppercase:part w 1)
                |
                one
            ]
        ]

        let params
        let args
        if arity = 0 [
            params: ["void"]  ; In C, f(void) has a distinct meaning from f()
            args: ["rebEND"]
        ] else [
            params: collect [
                ;
                ; Stack values (`unstable`) are allowed as arguments to the
                ; error generator, as they are copied before any evaluator
                ; calls are made.
                ;
                count-up 'i arity [
                    keep cscape [i "SymbolOrValue(const*) arg$<i>"]
                ]
            ]
            args: collect [
                count-up 'i arity [keep cscape [i "Extract_SoV(arg$<i>)"]]
                keep "rebEND"
            ]
        ]

        e-errfuncs/emit [message cat id f-name params args --[
            /* $<Mold Message> */
            INLINE Error* Error_${F-Name}_Raw(
                $(Params),
            ){
                return Make_Error_Managed(
                    SYM_${CAT}, SYM_${ID},
                    $(Args),
                );
            }
        ]--]
    ]
]

e-errfuncs/write-emitted


=== "LOAD BOOT MEZZANINE FUNCTIONS" ===

; The %base-xxx.r and %mezz-xxx.r files are not run through LOAD.  This is
; because the r3.exe being used to bootstrap may be older than the Rebol it
; is building...and if LOAD is used then it means any new changes to the
; scanner couldn't be used without an update to the bootstrap executable.
;
; However, %sys-xxx.r is a library of calls that are made available to Rebol
; by means of static ID numbers.  The way the #define-s for these IDs were
; made involved LOAD-ing the objects.  While we could rewrite that not to do
; a LOAD as well, keep it how it was for the moment.

mezz-files: load3 %../mezz/boot-files.r  ; base, sys, mezz

sys-toplevel: copy []

; 1. The boot process makes sure that the evaluation produces a QUASI-WORD!
;    with the symbol "END".  (~end~ won't make a valid antiform)
;
boot-constants: boot-base: boot-system-util: boot-mezz: ~  ; !!! better answer?
for-each 'section [boot-constants boot-base boot-system-util boot-mezz] [
    let s: make text! 20000
    append:line s "["
    for-each 'file first mezz-files [  ; doesn't use LOAD to strip
        let text: stripload:gather (
            join %../mezz/ file
        ) either section = 'boot-system-util [$sys-toplevel] [null]
        append:line s text
    ]
    append:line s "'~end~"  ; sanity check [1]
    append:line s "]"

    set (inside [] section) s

    mezz-files: next mezz-files
]

; We heuristically gather top level declarations in the system context, vs.
; trying to use DO and look at actual OBJECT! keys.  Previously this produced
; index numbers, but modules are no longer index-based so we make sure there
; are SYMIDs instead, so the SYM_XXX numbers can quickly produce canons that
; lead to the function definitions.

for-each 'item sys-toplevel [
    add-sym:relax as word! item
]


=== "EXTENSION SYMBOLS (NOT BUILT-IN, BUT USABLE IN SWITCH() STATEMENTS)" ===

; The extension words are reserved IDs which are intended to (someday) be set
; in stone.  The core executable doesn't ship with the strings embedded or
; pay for the cost of pre-creating them (so this list could get large... up
; to 65535 at present), and not be a problem.

add-sym:placeholder </MAX_SYM_BUILTIN>

add-sym:placeholder <MIN_SYM_EXTENDED>

for-each 'item load3 %ext-words.r [
    switch type of item [
        word! [add-sym item]
        block! [  ; will someday define reserved groups
            add-sym:placeholder item
        ]
        rune! [
            if not find sym-table as text! item [
                panic ["Expected symbol for" item "from [native type]"]
            ]
        ]
        panic ["bad %symbols.r item:" mold item]
    ]
]

add-sym:placeholder </MAX_SYM_EXTENDED>


=== "EMIT SYMBOLS AND PRUNE SPECIAL SIGNALS FROM sym-table" ===

symid: 1  ; SYM_0 is reserved for symbols that do not have baked-in ID numbers

lib-syms-max: ~

sym-enum-items: copy []
placeholder-define-items: copy []

emitting-extension-syms: null

e-ext-symids: make-emitter "Extension SymId Commitment Table" (
    join prep-dir %specs/tmp-ext-symid.r
)

for-next 'pos sym-table [
    while [tag? opt try pos.1] [  ; remove placeholders, add defines
        let definition: as text! pos.1
        take pos

        let delta
        let name
        if definition.1 = #"/" [  ; inclusive maximum
            definition: next definition
            delta: -1
            name: first back pos
        ] else [
            delta: 0
            name: first pos
        ]

        append placeholder-define-items cscape [
            -[#define $<DEFINITION>  $<symid + delta>]-
        ]
    ]

    if tail? pos [
        break
    ]

    if block? pos.1 [
        if emitting-extension-syms [
            e-ext-symids/emit newline
        ] else [
            emitting-extension-syms: okay
        ]
        e-ext-symids/emit spaced [";" form pos.1 newline]
        take pos
        pos: back pos  ; so for-next gets us back to this position
        continue
    ]

    if not text? pos.1 [
        panic ["Unknown item in sym-table table:" pos.1]
    ]

    let name: pos.1
    if emitting-extension-syms [
        ;
        ; While the EXT_SYM_XXX values aren't in the symbol table, we need
        ; their states to be there or compiler warnings will give an
        ; "illegal switch() case".  Put them in by number (%prep-extension.r
        ; makes #defines for them on an extension-by-extension basis)
        ;
        append sym-enum-items cscape [symid name
            --[/* $<Name> */  EXT_SYM_$<symid> = $<symid>]--
        ]
        e-ext-symids/emit [symid name
            --[$<name> $<symid>]--
        ]
        take pos  ; remove so it's not put in the builtin compressed strings
        pos: back pos  ; so for-next gets us back to this position
    ]
    else [
        append sym-enum-items cscape [symid name
            --[/* $<Name> */  SYM_${FORM NAME} = $<symid>]--
        ]
    ]

    symid: symid + 1
]

e-ext-symids/write-emitted

e-symids: make-emitter "Symbol ID (SymId) Enumeration Type and Values" (
    join prep-dir %include/tmp-symid.h
)

e-symids/emit [syms-cscape --[
    /*
     * CONSTANTS FOR BUILT-IN SYMBOLS: e.g. SYM_THRU or SYM_INTEGER_X
     *
     * ANY-WORD? uses internings of UTF-8 character strings.  An arbitrary
     * number of these are created at runtime, and can be garbage collected
     * when no longer in use.  But a pre-determined set of internings are
     * assigned 16-bit integer "SYM" compile-time-constants, to be used in
     * switch() for efficiency in the core.
     *
     *     switch (maybe Word_Id(color)) {
     *       case SYM_BLUE: ...
     *       case SYM_RED: ...
     *     }
     *
     * (Note that since the built-in symbols are collected from scanning
     * the source, the IDs will change if the source changes.  This means
     * any change affecting symbols will require recompiling everything.)
     *
     * Built-in symbols are quickly allocated in a batch at boot time, loaded
     * from a set of compressed strings in the boot block.  The symbols are
     * never garbage collected, because they need to stick around to hold on
     * to their embedded ID so they are recognized by the switches.
     *
     * There's also a feature of reserving numbers for symbols that aren't
     * built in, but that any extension which uses them agrees to use the
     * same number and provide it at registration time.  See Register_Symbol().
     *
     * Datatypes are given symbol numbers at the start of the list, so that
     * their SYM_XXX values will be identical to their TYPE_XXX values.
     *
     * The file %words.r contains a list of spellings that are given ID
     * numbers recognized by the core.
     *
     * Errors used in the core are identified by the symbol number of their
     * ID (there are no fixed-integer values for these errors as R3-Alpha
     * tried to do with RE_XXX numbers, which fluctuated and were of dubious
     * benefit when symbol comparison is available).
     *
     * Note: Any interning that *does not have* a compile-time constant
     * assigned to it will have a symbol ID of 0.  See Option(SymId) for how
     * potential bugs like `Word_Id(a) == Word_Id(b)` are mitigated
     * by preventing such comparisons.
     */
    typedef enum {
        SYM_0_constexpr,  /* SYM_0 defined as Option(SymId) for safety */

        $(Sym-Enum-Items),
    } SymId;

    /*
     * These definitions are derived from markers added during the symbol
     * table creation process via ADD-SYM:PLACEHOLDER (and are much better
     * than hardcoding symbol IDs in source the way R3-Alpha did it!)
     *
     * Note that the naming convention is MAX_SYM_XXX and not SYM_MAX_XXX,
     * to avoid thinking the symbol is literally for the letters "max-xxx"
     */
    $[Placeholder-Define-Items]

    /*
     * Builtin DATATYPE!s preallocate Patches in SYS.CONTEXTS.DATATYPES
     */
    #define NUM_BUILTIN_TYPES \
        (1 + MAX_SYM_BUILTIN_TYPES - MIN_SYM_BUILTIN_TYPES)
]--]

print [symid "words + natives + errors"]

e-symids/write-emitted

add-sym: ~#[Symbol table finalized, can't ADD-SYM at this point]#~


=== "MAKE BOOT BLOCK!" ===

; Create the aggregated Rebol file of all the Rebol-formatted data that is
; used in bootstrap.  This includes everything from a list of WORD!s that
; are built-in as symbols, to the sys and mezzanine functions.
;
; %tmp-boot-block.c is just a C file containing a literal constant of the
; compressed representation of %tmp-boot-block.r

e-bootblock: make-emitter "Natives and Bootstrap" (
    join prep-dir %core/tmp-boot-block.c
)

e-bootblock/emit [--[
    #include "sys-core.h"
]--]

sections: [
    :boot-natives
    boot-typespecs
    :boot-constants
    boot-errors
    boot-sysobj
    :boot-base
    :boot-system-util
    :boot-mezz
]

nats: collect [
    for-each 'name native-names [
        keep cscape [name "&NATIVE_CFUNC(${NAME})"]
    ]
]

symbol-strings: join blob! collect [
    for-each 'name sym-table [
        let utf-8: as blob! name  ; Note: frees string in bootstrap exe
        keep encode [BE + 1] length of utf-8  ; one byte length max
        keep utf-8
    ]
]

compressed: gzip symbol-strings

e-bootblock/emit [compressed --[
    /*
     * Gzip compression of symbol strings
     * Originally $<length of symbol-strings> bytes
     *
     * Size is a constant with storage vs. using a #define, so that relinking
     * is enough to sync up the referencing sites.
     */
    const Size g_symbol_names_compressed_size = $<length of compressed>;

    const Byte g_symbol_names_compressed[$<length of compressed>] = {
    $<Binary-To-C:Indent Compressed 4>
    };
]--]

print [length of nats "natives"]

e-bootblock/emit [nats --[
    #define NUM_NATIVES $<length of nats>

    /*
     * C Functions for the natives.
     */
    Dispatcher* const g_core_native_dispatchers[NUM_NATIVES] = {
        $(Nats),
    };

    /*
     * NUM_NATIVES macro not visible outside this file, export as variable
     */
    const REBLEN g_num_core_natives = NUM_NATIVES;
]--]


; Build typespecs block (in same order as datatypes table)

types-to-typespec: load3 join prep-dir %specs/tmp-typespecs.r

boot-typespecs: collect [
    for-each [type typespec] types-to-typespec [
        keep reduce [typespec]
    ]
]


; Create main code section (compressed)

boot-molded: copy ""
append:line boot-molded "["
for-each 'sec sections [
    if get-word? sec [  ; wasn't LOAD-ed (no bootstrap compatibility issues)
        append boot-molded (get inside sections sec)
    ]
    else [  ; was LOAD-ed for easier analysis (makes bootstrap complicated)
        append:line boot-molded mold:flat (get inside sections sec)
    ]
]
append:line boot-molded "]"

write-if-changed (join prep-dir %specs/tmp-boot-block.r) boot-molded
data: as blob! boot-molded

compressed: gzip data

e-bootblock/emit [compressed --[
    /*
     * Gzip compression of boot block
     * Originally $<length of data> bytes
     *
     * Size is a constant with storage vs. using a #define, so that relinking
     * is enough to sync up the referencing sites.
     */
    const Size g_boot_block_compressed_size = $<length of compressed>;

    const Byte g_boot_block_compressed[$<length of compressed>] = {
        $<Binary-To-C:Indent Compressed 4>
    };
]--]

e-bootblock/write-emitted


=== "BOOT HEADER FILE" ===

e-boot: make-emitter "Bootstrap Structure and Root Module" (
    join prep-dir %include/tmp-boot.h
)

fields: collect [
    for-each 'word sections [
        let name: form resolve word
        parse3 name [remove "boot-" accept (okay)]
        name: to-c-name name
        keep cscape [name "Element ${name}"]
    ]
]

e-boot/emit [fields --[
    /*
     * Symbols in SYM_XXX order, separated by newline characters, compressed.
     */
    EXTERN_C const Size g_symbol_names_compressed_size;
    EXTERN_C const Byte g_symbol_names_compressed[];

    /*
     * Compressed data of the native specifications, uncompressed during boot.
     */
    EXTERN_C const Size g_boot_block_compressed_size;
    EXTERN_C const Byte g_boot_block_compressed[];

    /*
     * Raw C function pointers for natives, take Level* and return Bounce.
     */
    EXTERN_C const Count g_num_core_natives;
    EXTERN_C Dispatcher* const g_core_native_dispatchers[];

    /*
     * Builtin Extensions
     */
    EXTERN_C const Count g_num_builtin_extensions;
    EXTERN_C ExtensionCollator* const g_builtin_collators[];


    typedef struct REBOL_Boot_Block {
        $[Fields];
    } BOOT_BLK;
]--]

e-boot/write-emitted
