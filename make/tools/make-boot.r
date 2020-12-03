REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Make primary boot files"
    File: %make-boot.r ;-- used by EMIT-HEADER to indicate emitting script
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Version: 2.100.0
    Needs: 2.100.100
    Purpose: {
        A lot of the REBOL system is built by REBOL, and this program
        does most of the serious work. It generates most of the C include
        files required to compile REBOL.
    }
]

print "--- Make Boot : System Embedded Script ---"

do %bootstrap-shim.r
do %common.r
do %common-emitter.r

do %systems.r

change-dir %../../src/boot/

args: parse-args system/options/args
config: config-system try get 'args/OS_ID

first-rebol-commit: "19d4f969b4f5c1536f24b023991ec11ee6d5adfb"

if args/GIT_COMMIT = "unknown" [
    git-commit: _
] else [
    git-commit: args/GIT_COMMIT
    if (length of git-commit) != (length of first-rebol-commit) [
        print ["GIT_COMMIT should be a full hash, e.g." first-rebol-commit]
        print ["Invalid hash was:" git-commit]
        quit
    ]
]

;-- SETUP --------------------------------------------------------------

;dir: %../core/temp/  ; temporary definition
output-dir: system/options/path/prep
inc: output-dir/include
core: output-dir/core
boot: output-dir/boot
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
] or [
    fail "No platform specified."
]

product: to-word any [try get 'args/PRODUCT | "core"]

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

e-symbols: make-emitter "Symbol Numbers" inc/tmp-symbols.h

syms: copy []
sym-n: 1

boot-words: copy []
add-sym: function [
    {Add SYM_XXX to enumeration}
    return: [<opt> integer!]
    word [word!]
    /exists "return ID of existing SYM_XXX constant if already exists"
    <with> sym-n
][
    if pos: find boot-words word [
        if exists [return index of pos]
        fail ["Duplicate word specified" word]
    ]

    append syms cscape/with {/* $<Word> */ SYM_${WORD} = $<sym-n>} [sym-n word]
    sym-n: sym-n + 1

    append boot-words word
    return null
]

; Several different sections add to the symbol constants, types are first...


type-table: load %types.r

e-dispatch: make-emitter "Dispatchers" core/tmp-dispatchers.c

generic-hooks: collect [
    for-each-record t type-table [
        switch t/class [
            '* [
                keep cscape/with {T_Unhooked /* $<T/Name> */} [t]
            ]
            default [
                keep cscape/with
                    {T_$<Propercase-Of t/class> /* $<T/Name> */} [t]
            ]
        ]
    ]
]

path-hooks: collect [
    for-each-record t type-table [
        switch t/path [
            '- [keep cscape/with {PD_Fail /* $<T/Name> */} [t]]
            '+ [
                proper: propercase-of t/class
                keep cscape/with {PD_$<Proper> /* $<T/Name> */} [proper t]
            ]
            '* [keep cscape/with {PD_Unhooked /* $<T/Name> */} [t]]
            default [
                ; !!! Today's PORT! path dispatches through context although
                ; that isn't its technical "class" for responding to generics.
                ;
                proper: propercase-of t/path
                keep cscape/with {PD_$<Proper> /* $<T/Name> */} [proper t]
            ]
        ]
    ]
]

make-hooks: collect [
    for-each-record t type-table [
        switch t/make [
            '- [keep cscape/with {/* $<T/Name> */ MAKE_Fail} [t]]
            '+ [
                proper: propercase-of t/class
                keep cscape/with {/* $<T/Name> */ MAKE_$<Proper>} [proper t]
            ]
            '* [keep cscape/with {/* $<T/Name> */ MAKE_Unhooked} [t]]

            fail "MAKE in %types.r should be, -, +, or *"
        ]
    ]
]

to-hooks: collect [
    for-each-record t type-table [
        switch t/make [
            '- [keep cscape/with {/* $<T/Name> */ TO_Fail} [t]]
            '+ [
                proper: propercase-of T/Class
                keep cscape/with {TO_$<Proper> /* $<T/Name> */} [proper t]
            ]
            '* [keep cscape/with {TO_Unhooked /* $T/Name> */} [t]]

            fail "TO in %types.r should be -, +, or *"
        ]
    ]
]

mold-hooks: collect [
    for-each-record t type-table [
        switch t/mold [
            '- [keep cscape/with {/* $<T/Name> */ MF_Fail"} [t]]
            '+ [
                proper: propercase-of t/class
                keep cscape/with {/* $<T/Name> */ MF_$<Proper>} [proper t]
            ]
            '* [keep cscape/with {/* $<T/Name> */ MF_Unhooked} [t]]
            default [
                ;
                ; ERROR! may be a context, but it has its own special forming
                ; beyond the class (falls through to ANY-CONTEXT! for mold),
                ; and BINARY! has a different handler than strings
                ;
                proper: propercase-of t/mold
                keep cscape/with {MF_$<Proper> /* $<T/Name> */} [proper t]
            ]
        ]
    ]
]

compare-hooks: collect [
    for-each-record t type-table [
        either t/class = '* [
            keep cscape/with {CT_Unhooked /* $<T/Class> */} [t]
        ][
            proper: Propercase-Of T/Class
            keep cscape/with {CT_$<Proper> /* $<T/Class> */} [proper t]
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
    GENERIC_HOOK Generic_Hooks[REB_MAX] = {
        nullptr, /* REB_0 */
        $(Generic-Hooks),
    };

    /*
     * PER-TYPE PATH HOOKS: for `a/b`, `:a/b`, `a/b:`, `pick a b`, `poke a b`
     */
    PATH_HOOK Path_Hooks[REB_MAX] = {
        nullptr, /* REB_0 */
        $(Path-Hooks),
    };

    /*
     * PER-TYPE MAKE HOOKS: for `make datatype def`
     *
     * These functions must return a REBVAL* to the type they are making
     * (either in the output cell given or an API cell)...or they can return
     * R_THROWN if they throw.  (e.g. `make object! [return]` can throw)
     */
    MAKE_HOOK Make_Hooks[REB_MAX] = {
        nullptr, /* REB_0 */
        $(Make-Hooks),
    };

    /*
     * PER-TYPE TO HOOKS: for `to datatype value`
     *
     * These functions must return a REBVAL* to the type they are making
     * (either in the output cell or an API cell).  They are NOT allowed to
     * throw, and are not supposed to make use of any binding information in
     * blocks they are passed...so no evaluations should be performed.
     */
    TO_HOOK To_Hooks[REB_MAX] = {
        nullptr, /* REB_0 */
        $(To-Hooks),
    };

    /*
     * PER-TYPE MOLD HOOKS: for `mold value` and `form value`
     */
    MOLD_HOOK Mold_Or_Form_Hooks[REB_MAX] = {
        nullptr, /* REB_0 */
        $(Mold-Hooks),
    };

    /*
     * PER-TYPE COMPARE HOOKS, to support GREATER?, EQUAL?, LESSER?...
     */
    COMPARE_HOOK Compare_Hooks[REB_MAX] = {
        nullptr, /* REB_0 */
        $(Compare-Hooks),
    };
}

e-dispatch/write-emitted



;----------------------------------------------------------------------------
;
; %reb-types.h - Datatype Definitions
;
;----------------------------------------------------------------------------

e-types: make-emitter "Datatype Definitions" inc/tmp-kinds.h

n: 1
rebs: collect [
    for-each-record t type-table [
        ensure word! t/name
        ensure word! t/class

        assert [sym-n == n] ;-- SYM_XXX should equal REB_XXX value
        add-sym to-word unspaced [ensure word! t/name "!"]
        keep cscape/with {REB_${T/NAME} = $<n>} [n t]
        n: n + 1
    ]
]

for-each-record t type-table [
    
]

e-types/emit {
    /*
     * INTERNAL DATATYPE CONSTANTS, e.g. REB_BLOCK or REB_TAG
     *
     * Do not export these values via libRebol, as the numbers can change.
     * Their ordering is for supporting certain optimizations, such as being
     * able to quickly check if a type IS_BINDABLE().  When types are added,
     * or removed, the numbers must shuffle around to preserve invariants.
     *
     * REB_MAX and beyond should not be used to index into arrays of types,
     * as there are no corresponding DATATYPE!s for them.  But the values are
     * used for out-of-band purposes, which should be kept in consideration.
     */
  #ifdef CPLUSPLUS_11
    enum Reb_Kind : int_fast8_t {
  #else
    enum Reb_Kind {
  #endif
        REB_0 = 0, /* reserved for internal purposes */
        REB_0_END = REB_0, /* ...most commonly array termination cells... */
        $[Rebs],
        REB_MAX, /* one past valid types, does double duty as NULL signal */
        REB_MAX_NULLED = REB_MAX,

        REB_MAX_PLUS_ONE, /* used for internal markings and algorithms */
        REB_R_THROWN = REB_MAX_PLUS_ONE,

        REB_MAX_PLUS_TWO, /* used to indicate trash in the debug build */
        REB_R_INVISIBLE = REB_MAX_PLUS_TWO,

        REB_MAX_PLUS_THREE, /* used for experimental typeset flag */
        REB_R_REDO = REB_MAX_PLUS_THREE,

        REB_MAX_PLUS_FOUR, /* also used for experimental typeset flag */
        REB_R_REFERENCE = REB_MAX_PLUS_FOUR,

        REB_MAX_PLUS_FIVE,
        REB_R_IMMEDIATE = REB_MAX_PLUS_FIVE,

        REB_MAX_PLUS_MAX
  #ifdef CPLUSPLUS_11
    };
  #else
    };
  #endif
} ;-- weird close brace thing needed to pair braces inside string literal
e-types/emit newline

e-types/emit {
    /*
     * SINGLE TYPE CHECK MACROS, e.g. IS_BLOCK() or IS_TAG()
     *
     * These routines are based on VAL_TYPE(), which does much more checking
     * than VAL_TYPE_RAW() in the debug build.  In some commonly called
     * routines, it may be worth it to use the less checked version.
     */
}
e-types/emit newline

boot-types: copy []
n: 1
for-each-record t type-table [
    e-types/emit 't {
        #define IS_${T/NAME}(v) \
            (VAL_TYPE(v) == REB_${T/NAME}) /* $<n> */
    }
    e-types/emit newline

    append boot-types to-word adjoin form t/name "!"
    n: n + 1
]

types-header: first load/header %types.r
e-types/emit trim/auto copy ensure text! types-header/macros


e-types/emit {
    /*
    ** TYPESET DEFINITIONS (e.g. TS_ARRAY or TS_STRING)
    **
    ** Note: User-facing typesets, such as ANY-VALUE!, do not include null
    ** (absence of a value), nor do they include the internal "REB_0" type.
    */

    /*
     * Subtract 1 to get mask for everything but REB_MAX_NULLED
     * Subtract 1 again to take out REB_0 for END (signal for "endability")
     */
    #define TS_VALUE \
        ((FLAGIT_KIND(REB_MAX) - 1) - 1)

    /*
     * Similar to TS_VALUE but accept NULL (as REB_MAX)
     */
    #define TS_OPT_VALUE \
        (((FLAGIT_KIND(REB_MAX_NULLED + 1) - 1) - 1))
}
typeset-sets: copy []

for-each-record t type-table [
    for-each ts compose [(t/typesets)] [
        spot: any [
            try select typeset-sets ts
            first back insert tail-of typeset-sets reduce [ts copy []]
        ]
        append spot t/name
    ]
]
remove/part typeset-sets 2 ; the - markers

for-each [ts types] typeset-sets [
    flagits: collect [
        for-each t types [
            keep cscape/with {FLAGIT_KIND(REB_${T})} 't
        ]
    ]
    e-types/emit [flagits ts] {
        #define TS_${TS} ($<Delimit "|" Flagits>)
    } ;-- !!! TS_ANY_XXX is wordy, considering TS_XXX denotes a typeset
]

e-types/write-emitted


;----------------------------------------------------------------------------
;
; %tmp-version.h - Version Information
;
;----------------------------------------------------------------------------

e-version: make-emitter "Version Information" inc/tmp-version.h


e-version/emit {
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
}
e-version/emit newline
e-version/write-emitted



;-- Add SYM_XXX constants for the words in %words.r

wordlist: load %words.r
replace wordlist '*port-modes* load %modes.r
for-each word wordlist [add-sym word]


;-- Add SYM_XXX constants for generics (e.g. SYM_APPEND, etc.)
;-- This allows C switch() statements to process them efficiently

first-generic-sym: sym-n

boot-generics: load boot/tmp-generics.r
for-each item boot-generics [
    if set-word? :item [
        if first-generic-sym < (add-sym/exists to-word item else [0]) [
            fail ["Duplicate generic found:" item]
        ]
    ]
]



;----------------------------------------------------------------------------
;
; Sysobj.h - System Object Selectors
;
;----------------------------------------------------------------------------

e-sysobj: make-emitter "System Object" inc/tmp-sysobj.h

at-value: func ['field] [next find boot-sysobj to-set-word field]

boot-sysobj: load %sysobj.r
change at-value version version
change at-value commit git-commit
change at-value build now/utc
change at-value product to lit-word! product

change/only at-value platform reduce [
    any [config/platform-name | "Unknown"]
    any [config/build-label | ""]
]

ob: has boot-sysobj

make-obj-defs: function [
    {Given a Rebol OBJECT!, write C structs that can access its raw variables}

    return: <void>
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
            keep cscape/with {${PREFIX}_SELF = 1} [prefix]
            n: 2
        ]

        for-each field words-of obj [
            keep cscape/with {${PREFIX}_${FIELD} = $<n>} [prefix field n]
            n: n + 1
        ]

        keep cscape/with {${PREFIX}_MAX} [prefix]
    ]

    e/emit [prefix items] {
        enum ${PREFIX}_object {
            $(Items),
        };
    }

    if depth > 1 [
        for-each field words-of obj [
            if all [
                field != 'standard
                object? get in obj field
            ][
                extended-prefix: uppercase unspaced [prefix "_" field]
                make-obj-defs e obj/:field extended-prefix (depth - 1)
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
make-obj-defs e-sysobj ob/view "VIEW" 4

e-sysobj/write-emitted


;----------------------------------------------------------------------------
;
; Event Types
;
;----------------------------------------------------------------------------

e-event: make-emitter "Event Types" inc/reb-evtypes.h

evts: collect [
    for-each field ob/view/event-types [
        keep cscape/with {EVT_${FIELD}} 'field
    ]
]

evks: collect [
    for-each field ob/view/event-keys [
        keep cscape/with {EVK_${FIELD}} 'field
    ]
]

e-event/emit {
    enum event_types {
        $[Evts],
        EVT_MAX
    };

    enum event_keys {
        $[Evks],
        EVK_MAX
    };
}

e-event/write-emitted


;----------------------------------------------------------------------------
;
; Error Constants
;
;----------------------------------------------------------------------------

;-- Error Structure ----------------------------------------------------------

e-errfuncs: make-emitter "Error structure and functions" inc/tmp-error-funcs.h

fields: collect [
    keep {RELVAL self}
    for-each word words-of ob/standard/error [
        either word = 'near [
            keep {/* near/far are old C keywords */ RELVAL nearest}
        ][
            keep cscape/with {RELVAL ${word}} 'word
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
     * fully resolved REBVAL* that the error spec specifies.  This is easy
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
            fail ["Duplicate error ID found:" id]
        ]

        arity: 0
        if block? message [ ;-- can have N GET-WORD! substitution slots
            parse message [any [get-word! (arity: arity + 1) | skip] end]
        ] else [
            ensure text! message ;-- textual message, no arguments
        ]

        ; Camel Case and make legal for C (e.g. "not-found*" => "Not_Found_P")
        ;
        f-name: uppercase/part to-c-name id 1
        parse f-name [
            any ["_" w: (uppercase/part w 1) | skip] end
        ]

        if arity = 0 [
            params: ["void"] ;-- In C, f(void) has a distinct meaning from f()
            args: ["rebEND"]
        ] else [
            params: collect [
                count-up i arity [keep unspaced ["const REBVAL *arg" i]]
            ]
            args: collect [
                count-up i arity [keep unspaced ["arg" i]]
                keep "rebEND"
            ]
        ]

        e-errfuncs/emit [message cat id f-name params args] {
            /* $<Mold Message> */
            static inline REBCTX *Error_${F-Name}_Raw($<Delimit ", " Params>) {
                return Error(SYM_${CAT}, SYM_${ID}, $<Delimit ", " Args>);
            }
        }
        e-errfuncs/emit newline
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
        append get section load join-of %../mezz/ file
    ]

    ; Make section evaluation return a BLANK! (something like <section-done>
    ; may be better, but calling code is C and that complicates checking).
    ;
    append get section _

    mezz-files: next mezz-files
]

e-sysctx: make-emitter "Sys Context" inc/tmp-sysctx.h

; We don't actually want to create the object in the R3-MAKE Rebol, because
; the constructs are intended to run in the Rebol being built.  But the list
; of top-level SET-WORD!s is needed.  R3-Alpha used a non-evaluating CONSTRUCT
; to do this, but Ren-C's non-evaluating construct expects direct alternation
; of SET-WORD! and unevaluated value (even another SET-WORD!).  So we just
; gather the top-level set-words manually.

sctx: has collect [
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
; to the userspace choice of self-vs-no-self (as with func's `<with> return`)
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

e-bootblock: make-emitter "Natives and Bootstrap" core/tmp-boot-block.c

boot-natives: load boot/tmp-natives.r

nats: collect [
    for-each val boot-natives [
        if set-word? val [
            keep cscape/with {N_${to word! val}} 'val
        ]
    ]
]

print [length of nats "natives"]

e-bootblock/emit {
    #include "sys-core.h"

    #define NUM_NATIVES $<length of nats>
    const REBCNT Num_Natives = NUM_NATIVES;
    REBVAL Natives[NUM_NATIVES];

    const REBNAT Native_C_Funcs[NUM_NATIVES] = {
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

write-if-changed boot/tmp-boot-block.r mold reduce sections
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
    const REBCNT Nat_Compressed_Size = $<length of compressed>;
    const REBYTE Native_Specs[$<length of compressed>] = {
        $<Binary-To-C Compressed>
    };
}

e-bootblock/write-emitted


;----------------------------------------------------------------------------
;
; Boot.h - Boot header file
;
;----------------------------------------------------------------------------

e-boot: make-emitter "Bootstrap Structure and Root Module" inc/tmp-boot.h

nat-index: 0
nids: collect [
    for-each val boot-natives [
        if set-word? val [
            keep cscape/with
                {N_${to word! val}_ID = $<nat-index>} [nat-index val]
            nat-index: nat-index + 1
        ]
    ]
]

fields: collect [
    for-each word sections [
        word: form word
        remove/part word 5 ; boot_
        keep cscape/with {RELVAL ${word}} 'word
    ]
]

e-boot/emit {
    /*
     * Compressed data of the native specifications, uncompressed during boot.
     */
    EXTERN_C const REBCNT Nat_Compressed_Size;
    EXTERN_C const REBYTE Native_Specs[];

    /*
     * Raw C function pointers for natives, take REBFRM* and return REBVAL*.
     */
    EXTERN_C const REBCNT Num_Natives;
    EXTERN_C const REBNAT Native_C_Funcs[];

    /*
     * A canon ACTION! REBVAL of the native, accessible by native's index #
     */
    EXTERN_C REBVAL Natives[]; /* size is Num_Natives */

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
     * their SYM_XXX values will be identical to their REB_XXX values.
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
     * constant assigned to it.  Since VAL_WORD_SYM() will return SYM_0 for
     * all user (and extension) defined words, don't try to check equality
     * with `VAL_WORD_SYM(word1) == VAL_WORD_SYM(word2)`.
     */
    enum Reb_Symbol {
        SYM_0 = 0,
        $(Syms),
    };
}

print [n "words + generics + errors"]

e-symbols/write-emitted
