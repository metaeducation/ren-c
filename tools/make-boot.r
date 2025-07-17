Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "Make primary boot files"
    file: %make-boot.r ;-- used by EMIT-HEADER to indicate emitting script
    rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    license: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    version: 2.100.0
    needs: 2.100.100
    purpose: {
        A lot of the REBOL system is built by REBOL, and this program
        does most of the serious work. It generates most of the C include
        files required to compile REBOL.
    }
]

print "--- Make Boot : System Embedded Script ---"

; **SENSITIVE MAGIC LINE OF VOODOO** - see "Usage" in %bootstrap-shim.r
(change-dir do join copy system/script/path %bootstrap-shim.r)

do <common.r>
do <common-emitter.r>

do <platforms.r>

change-dir repo-dir
change-dir %src/boot/

args: parse-args system/options/args
config: config-system degrade ((get 'args/OS_ID) else [reify null])

first-rebol-commit: "19d4f969b4f5c1536f24b023991ec11ee6d5adfb"

if args/GIT_COMMIT = "unknown" [
    git-commit: null
] else [
    git-commit: args/GIT_COMMIT
    if (length of git-commit) != (length of first-rebol-commit) [
        panic [
            "GIT_COMMIT should be a full hash, e.g." first-rebol-commit newline
            "Invalid hash was:" git-commit
        ]
    ]
]

;-- SETUP --------------------------------------------------------------

;dir: %../core/temp/  ; temporary definition
output-dir: join system/options/path %prep/
inc: join output-dir %include/
core: join output-dir %core/
boot: join output-dir %boot/
mkdir/deep probe inc
mkdir/deep probe boot
mkdir/deep probe core

version: load %version.r
version/4: config/id/2
version/5: config/id/3

;-- Title string put into boot.h file checksum:
Title:
{REBOL
Copyright 2012 REBOL Technologies
REBOL is a trademark of REBOL Technologies
Licensed under the Apache License, Version 2.0
}

sections: [
    boot-types
    boot-words
    boot-generics
    boot-natives
    boot-typespecs
    boot-errors
    boot-sysobj
    boot-base
    boot-sys
    boot-mezz
;   boot-script
]

; Args passed: platform, product
;
; !!! Heed /script/args so you could say e.g. `do/args %make-boot.r [0.3.01]`
; Note however that current leaning is that scripts called by the invoked
; process will not have access to the "outer" args, hence there will be only
; one "args" to be looked at in the long run.  This is an attempt to still
; be able to bootstrap under the conditions of the A111 rebol.com R3-Alpha
; as well as function either from the command line or the REPL.
;
args: any [
    either text? :system/script/args [
        either block? load system/script/args [
            load system/script/args
        ][
            reduce [load system/script/args]
        ]
    ][
        get 'system/script/args
    ]

    ; This is the only piece that should be necessary if not dealing w/legacy
    system/options/args
] else [
    panic "No platform specified."
]

product: to-word any [get 'args/PRODUCT  "core"]

platform-data: context [type: 'windows]
build: context [features: [help-strings]]

;-- Fetch platform specifications:
;init-build-objects/platform platform
;platform-data: platforms/:platform
;build: platform-data/builds/:product


;----------------------------------------------------------------------------
;
; %tmp-symbols.h - Symbol Numbers
;
;----------------------------------------------------------------------------

e-symbols: make-emitter "Symbol Numbers" (join inc %tmp-symbols.h)

syms: copy []
sym-n: 1

boot-words: copy []
add-sym: function [
    {Add SYM_XXX to enumeration}
    return: [~null~ integer!]
    word [word! text!]  ; BAR! is just synonym for WORD! in R3C
    /exists "return ID of existing SYM_XXX constant if already exists"
    <with> sym-n
][
    if pos: find boot-words word [
        if exists [return index of pos]
        panic ["Duplicate word specified" word]
    ]

    append syms cscape [sym-n word {/* $<Word> */ SYM_${WORD} = $<sym-n>}]
    sym-n: sym-n + 1

    if text? word [
        append boot-words switch word [
            "|" ['|]
            "~" ['~]
            panic "Only | and ~ are strings in words.r"
        ]
    ] else [
        append boot-words word
    ]
    return null
]

; Several different sections add to the symbol constants, types are first...


type-table: load %types.r

e-dispatch: make-emitter "Dispatchers" (join core %tmp-dispatchers.c)

generic-hooks: collect [
    for-each-record t type-table [
        switch t/class [
            '* [
                keep cscape [t {T_Unhooked /* $<T/Name> */}]
            ]
            default [
                keep cscape [t
                    {T_$<Propercase-Of t/class> /* $<T/Name> */}
                ]
            ]
        ]
    ]
]

path-hooks: collect [
    for-each-record t type-table [
        switch t/path [
            '- [keep cscape [t {PD_Panic /* $<T/Name> */}]]
            '+ [
                proper: propercase-of t/class
                keep cscape [proper t {PD_$<Proper> /* $<T/Name> */}]
            ]
            '* [keep cscape [t {PD_Unhooked /* $<T/Name> */}]]
            default [
                ; !!! Today's PORT! path dispatches through context although
                ; that isn't its technical "class" for responding to generics.
                ;
                proper: propercase-of t/path
                keep cscape [proper t {PD_$<Proper> /* $<T/Name> */}]
            ]
        ]
    ]
]

make-hooks: collect [
    for-each-record t type-table [
        switch t/make [
            '- [keep cscape [t {/* $<T/Name> */ MAKE_Panic}]]
            '+ [
                proper: propercase-of t/class
                keep cscape [proper t {/* $<T/Name> */ MAKE_$<Proper>}]
            ]
            '* [keep cscape [t {/* $<T/Name> */ MAKE_Unhooked}]]

            panic "MAKE in %types.r should be, -, +, or *"
        ]
    ]
]

to-hooks: collect [
    for-each-record t type-table [
        switch t/make [
            '- [keep cscape [t {/* $<T/Name> */ TO_Panic}]]
            '+ [
                proper: propercase-of T/Class
                keep cscape [t {TO_$<Proper> /* $<T/Name> */}]
            ]
            '* [keep cscape [t {TO_Unhooked /* $T/Name> */}]]

            panic "TO in %types.r should be -, +, or *"
        ]
    ]
]

mold-hooks: collect [
    for-each-record t type-table [
        switch t/mold [
            '- [keep cscape [t {/* $<T/Name> */ MF_Panic"}]]
            '+ [
                proper: propercase-of t/class
                keep cscape [proper t {/* $<T/Name> */ MF_$<Proper>}]
            ]
            '* [keep cscape [t {/* $<T/Name> */ MF_Unhooked}]]
            default [
                ;
                ; ERROR! may be a context, but it has its own special forming
                ; beyond the class (falls through to ANY-CONTEXT! for mold),
                ; and BINARY! has a different handler than strings
                ;
                proper: propercase-of t/mold
                keep cscape [proper t {MF_$<Proper> /* $<T/Name> */}]
            ]
        ]
    ]
]

compare-hooks: collect [
    for-each-record t type-table [
        either t/class = '* [
            keep cscape [t {CT_Unhooked /* $<T/Class> */}]
        ][
            proper: propercase-of T/Class
            keep cscape [proper t {CT_$<Proper> /* $<T/Class> */}]
        ]
    ]
]

e-dispatch/emit {
    #include "sys-core.h"

    /*
     * PER-TYPE GENERIC HOOKS: e.g. for `append value x` or `select value y`
     *
     * This is using the term in the sense of "generic functions":
     * https://en.wikipedia.org/wiki/Generic_function
     */
    GENERIC_HOOK Generic_Hooks[TYPE_MAX] = {
        nullptr, /* TYPE_0 */
        $(Generic-Hooks),
    };

    /*
     * PER-TYPE PATH HOOKS: for `a/b`, `:a/b`, `a/b:`, `pick a b`, `poke a b`
     */
    PATH_HOOK Path_Hooks[TYPE_MAX] = {
        nullptr, /* TYPE_0 */
        $(Path-Hooks),
    };

    /*
     * PER-TYPE MAKE HOOKS: for `make datatype def`
     *
     * These functions must return a Value* to the type they are making
     * (either in the output cell given or an API cell)...or they can return
     * BOUNCE_THROWN if they throw.  (e.g. `make object! [return]` can throw)
     */
    MAKE_HOOK Make_Hooks[TYPE_MAX] = {
        nullptr, /* TYPE_0 */
        $(Make-Hooks),
    };

    /*
     * PER-TYPE TO HOOKS: for `to datatype value`
     *
     * These functions must return a Value* to the type they are making
     * (either in the output cell or an API cell).  They are NOT allowed to
     * throw, and are not supposed to make use of any binding information in
     * blocks they are passed...so no evaluations should be performed.
     */
    TO_HOOK To_Hooks[TYPE_MAX] = {
        nullptr, /* TYPE_0 */
        $(To-Hooks),
    };

    /*
     * PER-TYPE MOLD HOOKS: for `mold value` and `form value`
     */
    MOLD_HOOK Mold_Or_Form_Hooks[TYPE_MAX] = {
        nullptr, /* TYPE_0 */
        $(Mold-Hooks),
    };

    /*
     * PER-TYPE COMPARE HOOKS, to support GREATER?, EQUAL?, LESSER?...
     */
    COMPARE_HOOK Compare_Hooks[TYPE_MAX] = {
        nullptr, /* TYPE_0 */
        $(Compare-Hooks),
    };
}

e-dispatch/write-emitted



;----------------------------------------------------------------------------
;
; %reb-types.h - Datatype Definitions
;
;----------------------------------------------------------------------------

e-types: make-emitter "Datatype Definitions" (join inc %tmp-kinds.h)

n: 1
rebs: collect [
    for-each-record t type-table [
        ensure word! t/name
        ensure word! t/class

        assert [sym-n = n] ;-- SYM_XXX should equal TYPE_XXX value
        add-sym to-word unspaced [t/name "!"]
        keep cscape [n t {TYPE_${T/NAME} = $<n>}]
        n: n + 1
    ]
]

e-types/emit [{
    /*
     * INTERNAL DATATYPE CONSTANTS, e.g. TYPE_BLOCK or TYPE_TAG
     *
     * Do not export these values via libRebol, as the numbers can change.
     * Their ordering is for supporting certain optimizations, such as being
     * able to quickly check if a type IS_BINDABLE().  When types are added,
     * or removed, the numbers must shuffle around to preserve invariants.
     *
     * TYPE_MAX and beyond should not be used to index into arrays of types,
     * as there are no corresponding DATATYPE!s for them.  But the values are
     * used for out-of-band purposes, which should be kept in consideration.
     */
  #if CPLUSPLUS_11
    enum TypeEnum : uint_fast8_t {
  #else
    enum TypeEnum {
  #endif
        TYPE_0 = 0, /* reserved for internal purposes */
        TYPE_0_END = TYPE_0, /* ...most commonly array termination cells... */
        $[Rebs],
        TYPE_MAX,

        TYPE_MAX_PLUS_ONE,
        TYPE_TS_VARIADIC = TYPE_MAX_PLUS_ONE,
        TYPE_R_THROWN = TYPE_MAX_PLUS_ONE,

        TYPE_MAX_PLUS_TWO,
        TYPE_TS_SKIPPABLE = TYPE_MAX_PLUS_TWO,
        TYPE_R_INVISIBLE = TYPE_MAX_PLUS_TWO,

        TYPE_MAX_PLUS_THREE,
        TYPE_TS_HIDDEN = TYPE_MAX_PLUS_THREE,
        TYPE_R_REDO = TYPE_MAX_PLUS_THREE,

        TYPE_MAX_PLUS_FOUR,
        TYPE_TS_UNBINDABLE = TYPE_MAX_PLUS_FOUR,
        TYPE_R_REFERENCE = TYPE_MAX_PLUS_FOUR,

        TYPE_MAX_PLUS_FIVE,
        TYPE_TS_NOOP_IF_VOID = TYPE_MAX_PLUS_FIVE,
        TYPE_R_IMMEDIATE = TYPE_MAX_PLUS_FIVE,

        TYPE_MAX_PLUS_SIX,
        TYPE_TS_NULL_IF_VOID = TYPE_MAX_PLUS_SIX,

        TYPE_MAX_PLUS_MAX
  #if CPLUSPLUS_11
    };
  #else
    };
  #endif

    typedef enum TypeEnum Type;

    /*
     * Not really a type, but a state cells can be in that's valid but not
     * intended to be read.
     */
    #define TYPE_255_UNREADABLE  255
}]  ;-- weird close brace thing needed to pair braces inside string literal

e-types/emit [{
    /*
     * SINGLE TYPE CHECK MACROS, e.g. Is_Block() or Is_Tag()
     *
     * These routines are based on Type_Of(), which does much more checking
     * than Unchecked_Type_Of() in the debug build.  In some commonly called
     * routines, it may be worth it to use the less checked version.
     */
}]

boot-types: copy []
n: 1
for-each-record t type-table [
    e-types/emit [t {
        #define Is_${Propercase-Of T/Name}(v) \
            (Type_Of(v) == TYPE_${T/NAME}) /* $<n> */
    }]

    append boot-types to-word append form t/name "!"
    n: n + 1
]

types-header: first load/header %types.r
e-types/emit trim/auto copy ensure text! types-header/macros


e-types/emit {
    /*
    ** TYPESET DEFINITIONS (e.g. TS_LIST or TS_STRING)
    **
    ** Note: User-facing typesets, such as ANY-VALUE!, do not include null
    ** (absence of a value), nor do they include the internal "TYPE_0" type.
    */

    /*
     * Subtract 1 to get mask for everything (including TYPE_0 for END)
     * Subtract signal for end and  (signal for "endability")
     * Includes TYPE_VOID.
     */
    #define TS_VALUE \
        ((FLAG_TYPE(TYPE_MAX) - 1) - FLAG_TYPE(TYPE_0_END))

    /*
     * TS_VALUE minus VOID
     */
    #define TS_STABLE \
        (TS_VALUE - FLAG_TYPE(TYPE_VOID))

    /*
     * TS_VALUE minus TRASH
     *
     * (you can compare things to VOID, e.g. (xxx = while [...] [...])
     * and have it not die just because the while never ran a loop iteration.)
     *
     * Note tripwires are elements in the bootstrap shim, else we'd need
     * quasi-tripwires to represent them.  Mainline EXE does not let you do
     * comparisons on tripwires, because they're true antiforms.
     */
    #define TS_EQUATABLE \
        (TS_VALUE - FLAG_TYPE(TYPE_TRASH))

    /*
     * TS_STABLE minus NULL, VOID, and TRASH
     */
    #define TS_ELEMENT \
        (TS_STABLE - FLAG_TYPE(TYPE_TRASH) - FLAG_TYPE(TYPE_NULLED) \
            - FLAG_TYPE(TYPE_OKAY))

    #define TS_LOGIC \
        (FLAG_TYPE(TYPE_NULLED) | FLAG_TYPE(TYPE_OKAY))

    #define TS_LIFTED \
        (FLAG_TYPE(TYPE_WORD) | FLAG_TYPE(TYPE_GROUP) \
            | FLAG_TYPE(TYPE_LIT_WORD) | FLAG_TYPE(TYPE_LIT_PATH))
}
typeset-sets: copy []

for-each-record t type-table [
    for-each ts compose [(t/typesets)] [
        spot: any [
            select typeset-sets ts
            first back insert tail-of typeset-sets reduce [ts copy []]
        ]
        append spot t/name
    ]
]
remove/part typeset-sets 2 ; the - markers

for-each [ts types] typeset-sets [
    flagits: collect [
        for-each t types [
            keep cscape [t {FLAG_TYPE(TYPE_${T})}]
        ]
    ]
    e-types/emit [flagits ts {
        #define TS_${TS} ($<Delimit "|" Flagits>)
    }]  ;-- !!! TS_ANY_XXX is wordy, considering TS_XXX denotes a typeset
]

e-types/write-emitted


;----------------------------------------------------------------------------
;
; %tmp-version.h - Version Information
;
;----------------------------------------------------------------------------

e-version: make-emitter "Version Information" (join inc %tmp-version.h)


e-version/emit [{
    /*
    ** VERSION INFORMATION
    **
    ** !!! While using 5 byte-sized integers to denote a Rebol version might
    ** not be ideal, it's a standard that's been around a long time.
    */

    #define REBOL_VER $<version/1>
    #define REBOL_REV $<version/2>
    #define REBOL_UPD $<version/3>
    #define REBOL_SYS $<version/4>
    #define REBOL_VAR $<version/5>
}]
e-version/write-emitted



;-- Add SYM_XXX constants for the words in %words.r

wordlist: load %words.r
for-each word-or-bar wordlist [  ; bootstrap | is BAR!, but WORD! in R3C
    if word-or-bar = '| [
        add-sym "|"
    ] else [
        add-sym word-or-bar
    ]
]


;-- Add SYM_XXX constants for generics (e.g. SYM_APPEND, etc.)
;-- This allows C switch() statements to process them efficiently

first-generic-sym: sym-n

boot-generics: load join boot %tmp-generics.r
for-each item boot-generics [
    if set-word? :item [
        if first-generic-sym < any [
            add-sym/exists to-word item
            0
        ][
            panic ["Duplicate generic found:" item]
        ]
    ]
]



;----------------------------------------------------------------------------
;
; Sysobj.h - System Object Selectors
;
;----------------------------------------------------------------------------

e-sysobj: make-emitter "System Object" (join inc %tmp-sysobj.h)

at-value: lambda ['field] [next find boot-sysobj to-set-word field]

boot-sysobj: load %sysobj.r
change at-value version version
change at-value commit (any [git-commit 'null])
change at-value build now/utc
change at-value product to lit-word! product

change/only at-value platform reduce [
    any [config/platform-name  "Unknown"]
    any [config/build-label  ""]
]

ob: make object! boot-sysobj

make-obj-defs: function [
    {Given a Rebol OBJECT!, write C structs that can access its raw variables}

    return: [~]
    e [object!]
       {The emitter to write definitions to}
    obj
    prefix
    depth
    /selfless
][
    items: collect [
        either selfless [
            n: 1
        ][
            keep cscape [prefix {${PREFIX}_SELF = 1}]
            n: 2
        ]

        for-each field words-of obj [
            keep cscape [prefix field n {${PREFIX}_${FIELD} = $<n>}]
            n: n + 1
        ]

        keep cscape [prefix {${PREFIX}_MAX}]
    ]

    e/emit [prefix items {
        enum ${PREFIX}_object {
            $(Items),
        };
    }]

    if depth > 1 [
        for-each field words-of obj [
            if all [
                field != 'standard
                object? opt get in obj field
            ][
                extended-prefix: uppercase unspaced [prefix "_" field]
                make-obj-defs e obj/(field) extended-prefix (depth - 1)
            ]
        ]
    ]
]

make-obj-defs e-sysobj ob "SYS" 1
make-obj-defs e-sysobj ob/catalog "CAT" 4
make-obj-defs e-sysobj ob/contexts "CTX" 4
make-obj-defs e-sysobj ob/standard "STD" 4
make-obj-defs e-sysobj ob/state "STATE" 4
;make-obj-defs e-sysobj ob/network "NET" 4
make-obj-defs e-sysobj ob/ports "PORTS" 4
make-obj-defs e-sysobj ob/options "OPTIONS" 4
;make-obj-defs e-sysobj ob/intrinsic "INTRINSIC" 4
make-obj-defs e-sysobj ob/locale "LOCALE" 4

e-sysobj/write-emitted


;----------------------------------------------------------------------------
;
; Error Constants
;
;----------------------------------------------------------------------------

;-- Error Structure ----------------------------------------------------------

e-errfuncs: (
    make-emitter "Error structure and functions" (join inc %tmp-error-funcs.h)
)

fields: collect [
    keep {Cell self}
    for-each word words-of ob/standard/error [
        either word = 'near [
            keep {/* near/far are old C keywords */ Cell nearest}
        ][
            keep cscape [word {Cell ${word}}]
        ]
    ]
]

e-errfuncs/emit {
    /*
     * STANDARD ERROR STRUCTURE
     */
    typedef struct REBOL_Error_Vars {
        $[Fields];
    } ERROR_VARS;
}

e-errfuncs/emit {
    /*
     * The variadic Error() function must be passed the exact right number of
     * fully resolved Value* that the error spec specifies.  This is easy
     * to get wrong in C, since variadics aren't checked.  Also, the category
     * symbol needs to be right for the error ID.
     *
     * These are inline function stubs made for each "raw" error in %errors.r.
     * They shouldn't add overhead in release builds, but help catch mistakes
     * at compile time.
     */
}

first-error-sym: sym-n

boot-errors: load %errors.r

for-each [sw-cat list] boot-errors [
    cat: to word! ensure set-word! sw-cat
    ensure block! list

    add-sym to word! cat ;-- category might incidentally exist as SYM_XXX

    for-each [sw-id t-message] list [
        id: to word! ensure set-word! sw-id
        message: t-message

        ;-- Add a SYM_XXX constant for the error's ID word

        if first-error-sym < (add-sym/exists id else [0]) [
            panic ["Duplicate error ID found:" id]
        ]

        arity: 0
        if block? message [ ;-- can have N GET-WORD! substitution slots
            parse2 message [opt some [get-word! (arity: arity + 1) | skip]]
        ] else [
            ensure text! message ;-- textual message, no arguments
        ]

        ; Camel Case and make legal for C (e.g. "not-found*" => "Not_Found_P")
        ;
        f-name: uppercase/part to-c-name id 1
        parse2 f-name [
            opt some ["_" w: (uppercase/part w 1) | skip]
        ]

        if arity = 0 [
            params: ["void"] ;-- In C, f(void) has a distinct meaning from f()
            args: ["rebEND"]
        ] else [
            params: collect [
                count-up i arity [keep unspaced ["const Value* arg" i]]
            ]
            args: collect [
                count-up i arity [keep unspaced ["arg" i]]
                keep "rebEND"
            ]
        ]

        e-errfuncs/emit [message cat id f-name params args {
            /* $<Mold Message> */
            INLINE Error* Error_${F-Name}_Raw($<Delimit ", " Params>) {
                return Make_Error_Managed(
                    SYM_${CAT}, SYM_${ID}, $<Delimit ", " Args>
                );
            }
        }]
    ]
]

e-errfuncs/write-emitted

;----------------------------------------------------------------------------
;
; Load Boot Mezzanine Functions - Base, Sys, and Plus
;
;----------------------------------------------------------------------------

;-- Add other MEZZ functions:
mezz-files: load %../mezz/boot-files.r ; base lib, sys, mezz

for-each section [boot-base boot-sys boot-mezz] [
    set section make block! 200
    for-each file first mezz-files [
        append get section load join %../mezz/ file
    ]

    ; Make section evaluation return NOTHING! (something like <section-done>
    ; may be better, but calling code is C and that complicates checking).
    ;
    append get section '~

    mezz-files: next mezz-files
]

e-sysctx: make-emitter "Sys Context" (join inc %tmp-sysctx.h)

; We don't actually want to create the object in the R3-MAKE Rebol, because
; the constructs are intended to run in the Rebol being built.  But the list
; of top-level SET-WORD!s is needed.  R3-Alpha used a non-evaluating CONSTRUCT
; to do this, but Ren-C's non-evaluating construct expects direct alternation
; of SET-WORD! and unevaluated value (even another SET-WORD!).  So we just
; gather the top-level set-words manually.

sctx: make object! collect [
    for-each item boot-sys [
        if set-word? :item [
            keep item
            keep "stub proxy for %sys-base.r item"
        ]
    ]
]

; !!! The SYS_CTX has no SELF...it is not produced by the ordinary gathering
; constructor, but uses Alloc_Context() directly.  Rather than try and force
; it to have a SELF, having some objects that don't helps pave the way
; to the userspace choice of self-vs-no-self.
;
make-obj-defs/selfless e-sysctx sctx "SYS_CTX" 1

e-sysctx/write-emitted


;----------------------------------------------------------------------------
;
; TMP-BOOT-BLOCK.R and TMP-BOOT-BLOCK.C
;
; Create the aggregated Rebol file of all the Rebol-formatted data that is
; used in bootstrap.  This includes everything from a list of WORD!s that
; are built-in as symbols, to the sys and mezzanine functions.
;
; %tmp-boot-block.c is just a C file containing a literal constant of the
; compressed representation of %tmp-boot-block.r
;
;----------------------------------------------------------------------------

e-bootblock: make-emitter "Natives and Bootstrap" (join core %tmp-boot-block.c)

boot-natives: load join boot %tmp-natives.r

nats: collect [
    for-each val boot-natives [
        if set-word? val [
            keep cscape [val {N_${TO WORD! VAL}}]
        ]
    ]
]

print [length of nats "natives"]

e-bootblock/emit {
    #include "sys-core.h"

    #define NUM_NATIVES $<length of nats>
    const REBLEN Num_Natives = NUM_NATIVES;
    Value Natives[NUM_NATIVES];

    Dispatcher* const Native_C_Funcs[NUM_NATIVES] = {
        $(Nats),
    };
}


;-- Build typespecs block (in same order as datatypes table):

boot-typespecs: make block! 100
specs: load %typespec.r
for-each-record t type-table [
    if t/name <> 0 [
        append/only boot-typespecs really select specs to-word t/name
    ]
]

;-- Create main code section (compressed):

write-if-changed join boot %tmp-boot-block.r mold reduce sections
data: to-binary mold/flat reduce sections

compressed: gzip data

e-bootblock/emit {
    /*
     * Gzip compression of boot block
     * Originally $<length of data> bytes
     *
     * Size is a constant with storage vs. using a #define, so that relinking
     * is enough to sync up the referencing sites.
     */
    const REBLEN Nat_Compressed_Size = $<length of compressed>;
    const Byte Native_Specs[$<length of compressed>] = {
        $<Binary-To-C Compressed>
    };
}

e-bootblock/write-emitted


;----------------------------------------------------------------------------
;
; Boot.h - Boot header file
;
;----------------------------------------------------------------------------

e-boot: make-emitter "Bootstrap Structure and Root Module" join inc %tmp-boot.h

nat-index: 0
nids: collect [
    for-each val boot-natives [
        if set-word? val [
            keep cscape [nat-index val
                {N_${TO WORD! VAL}_ID = $<nat-index>}
            ]
            nat-index: nat-index + 1
        ]
    ]
]

fields: collect [
    for-each word sections [
        word: form word
        remove/part word 5 ; boot_
        keep cscape [word {Cell ${word}}]
    ]
]

e-boot/emit {
    /*
     * Compressed data of the native specifications, uncompressed during boot.
     */
    EXTERN_C const REBLEN Nat_Compressed_Size;
    EXTERN_C const Byte Native_Specs[];

    /*
     * Raw C function pointers for natives, take Level* and return Value*.
     */
    EXTERN_C const REBLEN Num_Natives;
    EXTERN_C Dispatcher* const Native_C_Funcs[];

    /*
     * A canon ACTION! Value of the native, accessible by native's index #
     */
    EXTERN_C Value Natives[]; /* size is Num_Natives */

    enum Native_Indices {
        $(Nids),
    };

    typedef struct REBOL_Boot_Block {
        $[Fields];
    } BOOT_BLK;
}

;-------------------

e-boot/write-emitted


;-----------------------------------------------------------------------------
; EMIT SYMBOLS
;-----------------------------------------------------------------------------

e-symbols/emit {
    /*
     * CONSTANTS FOR BUILT-IN SYMBOLS: e.g. SYM_THRU or SYM_INTEGER_X
     *
     * ANY-WORD! uses internings of UTF-8 character strings.  An arbitrary
     * number of these are created at runtime, and can be garbage collected
     * when no longer in use.  But a pre-determined set of internings are
     * assigned small integer "SYM" compile-time-constants, to be used in
     * switch() for efficiency in the core.
     *
     * Datatypes are given symbol numbers at the start of the list, so that
     * their SYM_XXX values will be identical to their TYPE_XXX values.
     *
     * The file %words.r contains a list of spellings that are given ID
     * numbers recognized by the core.
     *
     * Errors raised by the core are identified by the symbol number of their
     * ID (there are no fixed-integer values for these errors as R3-Alpha
     * tried to do with RE_XXX numbers, which fluctuated and were of dubious
     * benefit when symbol comparison is available).
     *
     * Note: SYM_0 is not a symbol of the string "0".  It's the "SYM" constant
     * that is returned for any interning that *does not have* a compile-time
     * constant assigned to it.  Since Word_Id() will return SYM_0 for
     * all user (and extension) defined words, don't try to check equality
     * with `Word_Id(word1) == Word_Id(word2)`.
     */
    enum SymIdEnum {
        SYM_0_internal = 0,
        $(Syms),
    };
}

print [n "words + generics + errors"]

e-symbols/write-emitted
