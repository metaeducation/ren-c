REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Make primary boot files"
    File: %make-boot.r  ; used by EMIT-HEADER to indicate emitting script
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2019 Ren-C Open Source Contributors
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

if not find words of :import [product] [  ; See %import-shim.r
    do load append copy system/script/path %import-shim.r
]

import <bootstrap-shim.r>
import <common.r>
import <common-emitter.r>

import <systems.r>

change-dir join repo-dir %src/boot/

args: parse-args system/script/args  ; either from command line or DO/ARGS
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

=== SETUP PATHS AND MAKE DIRECTORIES (IF NEEDED) ===

prep-dir: make-file [(system/options/path) prep /]

mkdir/deep make-file [(prep-dir) include /]
mkdir/deep make-file [(prep-dir) boot /]
mkdir/deep make-file [(prep-dir) core /]

Title: {
    REBOL
    Copyright 2012 REBOL Technologies
    Copyright 2012-2019 Ren-C Open Source Contributors
    REBOL is a trademark of REBOL Technologies
    Licensed under the Apache License, Version 2.0
}


=== PROCESS COMMAND LINE ARGUMENTS ===

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
    fail "No platform specified."
]

product: to-word any [
    try get 'args/PRODUCT
    "core"
]

platform-data: context [type: 'windows]
build: context [features: [help-strings]]

; !!! "Fetch platform specifications" (was commented out)
;
comment [
    init-build-objects/platform platform
    platform-data: platforms/:platform
    build: platform-data/builds/:product
]

=== MAKE VERSION INFORMATION AVAILABLE TO CORE C CODE ===

(e-version: make-emitter "Version Information"
    make-file [(prep-dir) include/tmp-version.h])

version: load-value %version.r
version: to tuple! reduce [
    version/1 version/2 version/3 config/id/2 config/id/3
 ]

e-version/emit 'version {
    /*
     * VERSION INFORMATION
     *
     * !!! While using 5 byte-sized integers to denote a Rebol version might
     * not be ideal, it's a standard that's been around a long time.
     */

    #define REBOL_VER $<version/1>
    #define REBOL_REV $<version/2>
    #define REBOL_UPD $<version/3>
    #define REBOL_SYS $<version/4>
    #define REBOL_VAR $<version/5>
}
e-version/emit newline
e-version/write-emitted


=== SET UP COLLECTION OF SYMBOL NUMBERS ===

; !!! The symbol strategy in Ren-C is expected to move to using a fixed table
; of words that commit to their identity, as opposed to picking on each build.
; Concept would be to fit every common word that would be used in Rebol to
; the low 65535 indices, while allowing numbers beyond that to be claimed
; over time...so they could still be used in C switch() statements (but might
; have to be stored and managed in a less efficient way)
;
; For now, the symbols are gathered from the various phases, and can change
; as things are added or removed.  Hence C code using SYM_XXX must be
; recompiled with changes to the core.  These symbols aren't in libRebol,
; however, so it only affects clients of the core API for now.

(e-symbols: make-emitter "Symbol ID (SYMID) Enumeration Type and Values"
    make-file [(prep-dir) include/tmp-symid.h])

syms-words: copy []
syms-cscape: copy []

sym-n: 1  ; skip SYM_0 (null added as #1)

add-sym: function compose/deep [
    {Add SYM_XXX to enumeration}
    return: [<opt> integer!]
    word [word! ((if set? 'bar! [bar!]))]  ; bootstrap needs | accepted for BAR!
    /exists "return ID of existing SYM_XXX constant if already exists"
    <with> sym-n
][
    if pos: find/only syms-words word [
        if exists [return index of pos]
        fail ["Duplicate word specified" word]
    ]

    append syms-words ^(word)
    append syms-cscape cscape/with {/* $<Word> */ SYM_${FORM WORD} = $<sym-n>} [
        sym-n word
    ]
    sym-n: sym-n + 1

    return null
]


=== DATATYPE DEFINITIONS ===

type-table: load %types.r

(e-types: make-emitter "Datatype Definitions"
    make-file [(prep-dir) include/tmp-kinds.h])

n: 0

rebs: collect [
    for-each-record t type-table [
        if is-real-type: word? t/name [
            ensure word! t/class
        ] else [
            ensure issue! t/name
            assert [t/class = 0]  ; e.g. REB_NULL
            t/name: as text! t/name  ; TO TEXT! of ISSUE! has # in bootstrap
        ]

        if n <> 0 [
            assert [sym-n == n]  ; SYM_XXX should equal REB_XXX value
            add-sym to-word unspaced [t/name (if is-real-type ["!"])]
        ]

        keep cscape/with {REB_${T/NAME} = $<n>} [n t]

        n: n + 1
    ]
]

e-types/emit 'rebs {
    /*
     * Current hard limit, higher types used for QUOTED!.  In code which
     * is using the 64 split to implement quoting, use REB_64 instead of just
     * the constant value 64 to make places dependent on that trick findable.
     */
    #define REB_64 64

    /*
     * INTERNAL DATATYPE CONSTANTS, e.g. REB_BLOCK or REB_TAG
     *
     * Do not export these values via libRebol, as the numbers can change.
     * Their ordering is for supporting tricks--like being able to quickly
     * check if a type IS_BINDABLE().  So when types are added or removed, the
     * numbers must shuffle around to preserve invariants.
     *
     * NOTE ABOUT C++11 ENUM TYPING: It is best not to specify an "underlying
     * type" because that prohibits certain optimizations, which the compiler
     * can make based on knowing a value is only in the range of the enum.
     */
    enum Reb_Kind {

        /*** TYPES AND INTERNALS GENERATED FROM %TYPES.R ***/

        $[Rebs],
        REB_MAX,  /* one past valid types */

        /*
        * Aliases for REB_0 to clarify which purpose it is used for.
        */
        REB_0_END = REB_0,
        REB_0_FREE = REB_0,

        /*
        * There are no "quoted ends" and there are no "quoted REB_QUOTEDs".
        * So these six distinct states are available for other uses.
        */

        REB_T_RETURN_SIGNAL = (REB_0 + REB_64),  /* signals throws, etc. */

      #if DEBUG_POISON_CELLS
        REB_T_POISON = (REB_0 + 2 * REB_64),  /* simulate lack of GC safety */
      #endif

        REB_PSEUDOTYPE_THREE = (REB_0 + 3 * REB_64),

        REB_PSEUDOTYPE_FOUR = (REB_QUOTED + REB_64),

        REB_PSEUDOTYPE_FIVE = (REB_QUOTED + 2 * REB_64),

        REB_PSEUDOTYPE_SIX = (REB_QUOTED + 3 * REB_64)
    };


    /*
     * While the VAL_TYPE() is a full byte, only 64 states can fit in the
     * payload of a TYPESET! at the moment.  Significant rethinking would be
     * necessary if this number exceeds 64, especially since the optimization
     * of allowing up to three levels of quoting cheaply relies on using the
     * gap between 64 and 256 in the KIND3Q_BYTE().
     */
    STATIC_ASSERT(REB_MAX <= REB_64);
}
e-types/emit newline

e-types/emit {
    /*
     * SINGLE TYPE CHECK MACROS, e.g. IS_BLOCK() or IS_TAG()
     *
     * These routines are based on KIND3Q_BYTE(), which is cheaper than
     * VAL_TYPE() but still does a number of checks in the debug build for cell
     * validity.  In some commonly called routines where performance in the
     * debug build would suffer too much, it may be better to do comparisons
     * like (REB_INTEGER == KIND3Q_BYTE_UNCHECKED()) instead of IS_INTEGER().
     *
     * Note that due to a raw type encoding trick, IS_QUOTED() is unusual.
     * `KIND3Q_BYTE(v) == REB_QUOTED` isn't `VAL_TYPE(v) == REB_QUOTED`,
     * they mean different things.  This is because raw types > REB_64 are
     * used to encode literals whose escaping level is low enough that it
     * can use the same cell bits as the escaped value.
     */
}
e-types/emit newline

boot-types: copy []  ; includes internal types like REB_NULL (but not END)
n: 0

for-each-record t type-table [
    if n != 0 [
        append boot-types either issue? t/name [
            ^(to-word t/name)
        ][
            ^(to-word unspaced [form t/name "!"])
        ]
    ]

    all [
        not issue? t/name  ; internal type
        t/name != 'quoted  ; see IS_QUOTED(), handled specially
    ] then [
        e-types/emit 't {
            #define IS_${T/NAME}(v) \
                (KIND3Q_BYTE(v) == REB_${T/NAME})  /* $<n> */
        }
        e-types/emit newline
    ]

    n: n + 1
]

nontypes: collect [
    for-each-record t type-table [
        if issue? t/name [
            nontype: mold t/name
            keep cscape/with {FLAGIT_KIND(REB_${AS TEXT! T/NAME})} 't
        ]
    ]
]

value-flagnots: compose [
    "(FLAGIT_KIND(REB_MAX) - 1)"  ; Subtract 1 to get mask for everything
    ((nontypes))  ; take out all nontypes
]

e-types/emit 'value-flagnots {
    /*
     * TYPESET DEFINITIONS (e.g. TS_ARRAY or TS_STRING)
     */

    /*
     * Typeset for ANY-VALUE!
     */
    #define TS_VALUE \
        ($<Delimit "&~" Value-Flagnots>)

    /*
     * Typeset for [<OPT> ANY-VALUE!] (similar to TS_VALUE but accept NULL)
     */
    #define TS_OPT_VALUE \
        (TS_VALUE | FLAGIT_KIND(REB_NULL))

}

typeset-sets: copy []

for-each-record t type-table [
    for-each ts t/typesets [
        spot: any [
            select typeset-sets ts
            first back insert tail typeset-sets reduce [ts copy []]
        ]
        append spot ^(t/name)
    ]
]

add-sym 'any-value!  ; starts the typesets, not mentioned in %types.r

for-each [ts types] typeset-sets [
    add-sym to-word unspaced ["any-" ts "!"]

    flagits: collect [
        for-each t types [
            keep cscape/with {FLAGIT_KIND(REB_${T})} 't
        ]
    ]
    e-types/emit [flagits ts] {
        #define TS_${TS} ($<Delimit "|" Flagits>)
    }  ; !!! TS_ANY_XXX is wordy, considering TS_XXX denotes a typeset
]

add-sym 'datatypes  ; signal where the datatypes stop


e-types/emit {
    /* !!! R3-Alpha made frequent use of these predefined typesets.  In Ren-C
     * they have been called into question, as to exactly how copying
     * mechanics should work.
     */

    #define TS_NOT_COPIED \
        (FLAGIT_KIND(REB_CUSTOM) \
        | FLAGIT_KIND(REB_PORT))

    #define TS_STD_SERIES \
        (TS_SERIES & ~TS_NOT_COPIED)

    #define TS_SERIES_OBJ \
        ((TS_SERIES | TS_CONTEXT | TS_SEQUENCE) & ~TS_NOT_COPIED)

    #define TS_ARRAYS_OBJ \
        ((TS_ARRAY | TS_CONTEXT | TS_SEQUENCE) & ~TS_NOT_COPIED)

    #define TS_CLONE \
        (TS_SERIES & ~TS_NOT_COPIED) // currently same as TS_NOT_COPIED
}

e-types/write-emitted


=== BUILT-IN TYPE HOOKS TABLE ===

(e-hooks: make-emitter "Built-in Type Hooks"
    make-file [(prep-dir) core/tmp-type-hooks.c])

hookname: enfixed func [
    return: [text!]
    'prefix [text!] "quoted prefix, e.g. T_ for T_Action"
    t [object!] "type record (e.g. a row out of %types.r)"
    column [word!] "which column we are deriving the hook's name based on"
][
    if t/(column) = 0 [return "nullptr"]

    ; The CSCAPE mechanics lowercase all strings.  Uppercase it back.
    ;
    prefix: uppercase copy prefix

    return unspaced [prefix propercase-of (switch ensure word! t/(column) [
        '+ [as text! t/name]  ; type has its own unique hook
        '* [t/class]        ; type uses common hook for class
        '? ['unhooked]      ; datatype provided by extension
        '- ['fail]          ; service unavailable for type
    ] else [
        t/(column)      ; override with word in column
    ])]
]

n: 0
hook-list: collect [
    for-each-record t type-table [
        name: either issue? t/name [as text! t/name] [unspaced [t/name "!"]]

        keep cscape/with {
            {  /* $<NAME> = $<n> */
                cast(CFUNC*, ${"T_" Hookname T 'Class}),  /* generic */
                cast(CFUNC*, ${"CT_" Hookname T 'Class}),  /* compare */
                cast(CFUNC*, ${"PD_" Hookname T 'Path}),  /* path */
                cast(CFUNC*, ${"MAKE_" Hookname T 'Make}),  /* make */
                cast(CFUNC*, ${"TO_" Hookname T 'Make}),  /* to */
                cast(CFUNC*, ${"MF_" Hookname T 'Mold}),  /* mold */
                nullptr
            }} [t]

        n: n + 1
    ]
]

e-hooks/emit 'hook-list {
    #include "sys-core.h"

    /* See comments in %sys-ordered.h */
    CFUNC* Builtin_Type_Hooks[REB_MAX][IDX_HOOKS_MAX] = {
        $(Hook-List),
    };
}

e-hooks/write-emitted


=== SYMBOLS FOR LIB-WORDS.R ===

; Add SYM_XXX constants for the words in %lib-words.r - these are words that
; reserve a spot in the lib context.  They can be accessed quickly, without
; going through a hash table.
;
; Since the relative order of these words is honored, that means they must
; establish their slots first.  Any natives or generics which have the same
; name will have to use the slot position established for these words.

for-each word load %lib-words.r [
    add-sym word  ; Note, may actually be a BAR! w/older boot
]


=== ESTABLISH SYM_XXX VALUES FOR EACH NATIVE ===

; It's desirable for the core to be able to get the REBVAL* for a native
; quickly just by indexing into a table.  An aspect of optimizations related
; to that is that the SYM_XXX values for the names of the natives index into
; a fixed block.  We put them after the ordered words in lib.

first-native-sym: sym-n

boot-natives: load make-file [(prep-dir) boot/tmp-natives.r]
for-each item boot-natives [
    if set-word? :item [
        if first-native-sym < ((add-sym/exists to-word item) else [0]) [
            fail ["Native name collision found:" item]
        ]
    ]
]


=== "VERB" SYMBOLS FOR GENERICS ===

; This adds SYM_XXX constants for generics (e.g. SYM_APPEND, etc.), which
; allows C switch() statements to process them efficiently

first-generic-sym: sym-n

boot-generics: load make-file [(prep-dir) boot/tmp-generics.r]
for-each item boot-generics [
    if set-word? :item [
        if first-generic-sym < ((add-sym/exists to-word item) else [0]) [
            fail ["Generic name collision with Native or Generic found:" item]
        ]
    ]
]

lib-syms-max: sym-n  ; *DON'T* count the symbols in %symbols.r, added below...


=== SYMBOLS FOR SYMBOLS.R ===

; The %symbols.r file are terms that get SYM_XXX constants and an entry in
; the table for turning those constants into a symbol pointer.  But they do
; not have priority on establishing declarations in lib.  Hence a native or
; generic might come along and use one of these terms...meaning they have to
; yield to that position.  That's why there's no guarantee of order.

for-each term load %symbols.r [
    if word? term [
        add-sym term
    ] else [
        assert [issue? term]
        if not find syms-words to word! term [
            fail ["Expected symbol for" term "from native/generic/type"]
        ]
    ]
]


=== SYSTEM OBJECT SELECTORS ===

(e-sysobj: make-emitter "System Object"
    make-file [(prep-dir) include/tmp-sysobj.h])

at-value: func ['field] [next find/only boot-sysobj to-set-word field]

boot-sysobj: load strip-commas-and-null-apostrophes read/string %sysobj.r
change at-value version ^(version)
change at-value commit git-commit
change at-value build now/utc
change at-value product ^(quote to word! product)  ; ^ to keep quote

change at-value platform ^ reduce [
    any [config/platform-name "Unknown"]
    any [config/build-label ""]
]

; If debugging something code in %sysobj.r, the C-DEBUG-BREAK should only
; apply in the non-bootstrap case.
;
c-debug-break: :void

ob: make object! boot-sysobj

c-debug-break: :lib/c-debug-break

make-obj-defs: function [
    {Given a Rebol OBJECT!, write C structs that can access its raw variables}

    return: <none>
    e [object!]
       {The emitter to write definitions to}
    obj
    prefix
    depth
][
    items: collect [
        n: 1

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

e-sysobj/write-emitted


=== ERROR STRUCTURE AND CONSTANTS ===

(e-errfuncs: make-emitter "Error structure and functions"
    make-file [(prep-dir) include/tmp-error-funcs.h])

fields: collect [
    for-each word words-of ob/standard/error [
        either word = 'near [
            keep {/* near/far are old C keywords */ RELVAL nearest}
        ][
            keep cscape/with {RELVAL ${word}} 'word
        ]
    ]
]

e-errfuncs/emit 'fields {
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

    add-sym to word! cat  ; category might incidentally exist as SYM_XXX

    for-each [sw-id t-message] list [
        id: to word! ensure set-word! sw-id
        message: t-message

        ; Add a SYM_XXX constant for the error's ID word
        ;
        if first-error-sym < (add-sym/exists id else [0]) [
            fail ["Duplicate error ID found:" id]
        ]

        arity: 0
        if block? message [  ; can have N GET-WORD! substitution slots
            parse message [while [get-word! (arity: arity + 1) | skip] end]
        ] else [
            ensure text! message  ; textual message, no arguments
        ]

        ; Camel Case and make legal for C (e.g. "not-found*" => "Not_Found_P")
        ;
        f-name: uppercase/part to-c-name id 1
        parse f-name [
            while ["_" w: here (uppercase/part w 1) | skip]
        ]

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
                count-up i arity [
                    keep unspaced ["const RELVAL *arg" i]
                ]
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


=== LOAD BOOT MEZZANINE FUNCTIONS ===

; The %base-xxx.r and %mezz-xxx.r files are not run through LOAD.  This is
; because the r3.exe being used to bootstrap may be older than the Rebol it
; is building...and if LOAD is used then it means any new changes to the
; scanner couldn't be used without an update to the bootstrap executable.
;
; However, %sys-xxx.r is a library of calls that are made available to Rebol
; by means of static ID numbers.  The way the #define-s for these IDs were
; made involved LOAD-ing the objects.  While we could rewrite that not to do
; a LOAD as well, keep it how it was for the moment.

mezz-files: load %../mezz/boot-files.r  ; base, sys, mezz

sys-toplevel: copy []

for-each section [boot-base boot-sys boot-mezz] [
    set section s: make text! 20000
    append/line s "["
    for-each file first mezz-files [  ; doesn't use LOAD to strip
        gather: try if section = 'boot-sys ['sys-toplevel]
        text: stripload/gather join %../mezz/ file opt gather
        append/line s text
    ]
    append/line s "_"  ; !!! would <section-done> be better?
    append/line s "]"

    mezz-files: next mezz-files
]

; We heuristically gather top level declarations in the system context, vs.
; trying to use DO and look at actual OBJECT! keys.  Previously this produced
; index numbers, but modules are no longer index-based so we make sure there
; are SYMIDs instead, so the SYM_XXX numbers can quickly produce canons that
; lead to the function definitions.

for-each item sys-toplevel [
    add-sym/exists as word! item
]


=== MAKE BOOT BLOCK! ===

; Create the aggregated Rebol file of all the Rebol-formatted data that is
; used in bootstrap.  This includes everything from a list of WORD!s that
; are built-in as symbols, to the sys and mezzanine functions.
;
; %tmp-boot-block.c is just a C file containing a literal constant of the
; compressed representation of %tmp-boot-block.r

(e-bootblock: make-emitter "Natives and Bootstrap"
    make-file [(prep-dir) core/tmp-boot-block.c])

e-bootblock/emit {
    #include "sys-core.h"
}

sections: [
    boot-types
    boot-generics
    boot-natives
    boot-typespecs
    boot-errors
    boot-sysobj
    :boot-base
    :boot-sys
    :boot-mezz
]

nats: collect [
    for-each val boot-natives [
        if set-word? val [
            keep cscape/with {N_${to word! val}} 'val
        ]
    ]
]

symbol-strings: join binary! collect [
    for-each word syms-words [
        spelling: to text! word
        keep head change copy #{00} length of spelling
        keep spelling
    ]
]

compressed: gzip symbol-strings

e-bootblock/emit 'compressed {
    /*
     * Gzip compression of symbol strings
     * Originally $<length of symbol-strings> bytes
     *
     * Size is a constant with storage vs. using a #define, so that relinking
     * is enough to sync up the referencing sites.
     */
    const REBLEN Symbol_Strings_Compressed_Size = $<length of compressed>;
    const REBYTE Symbol_Strings_Compressed[$<length of compressed>] = {
        $<Binary-To-C Compressed>
    };
}

print [length of nats "natives"]

e-bootblock/emit 'nats {
    #define NUM_NATIVES $<length of nats>
    const REBLEN Num_Natives = NUM_NATIVES;

    const REBNAT Native_C_Funcs[NUM_NATIVES] = {
        $(Nats),
    };
}

; Build typespecs block (in same order as datatypes table)

boot-typespecs: collect [
    for-each-record t type-table [
        keep ^ reduce [t/description]
    ]
]

; Create main code section (compressed)

boot-molded: copy ""
append/line boot-molded "["
for-each sec sections [
    if get-word? sec [  ; wasn't LOAD-ed (no bootstrap compatibility issues)
        append boot-molded get sec
    ]
    else [  ; was LOAD-ed for easier analysis (makes bootstrap complicated)
        append/line boot-molded mold/flat get sec
    ]
]
append/line boot-molded "]"

write-if-changed make-file [(prep-dir) boot/tmp-boot-block.r] boot-molded
data: as binary! boot-molded

compressed: gzip data

e-bootblock/emit 'compressed {
    /*
     * Gzip compression of boot block
     * Originally $<length of data> bytes
     *
     * Size is a constant with storage vs. using a #define, so that relinking
     * is enough to sync up the referencing sites.
     */
    const REBLEN Boot_Block_Compressed_Size = $<length of compressed>;
    const REBYTE Boot_Block_Compressed[$<length of compressed>] = {
        $<Binary-To-C Compressed>
    };
}

e-bootblock/write-emitted


=== BOOT HEADER FILE ===

(e-boot: make-emitter "Bootstrap Structure and Root Module"
    make-file [(prep-dir) include/tmp-boot.h])

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
        word: form as word! word
        remove/part word 5 ; boot_
        keep cscape/with {RELVAL ${word}} 'word
    ]
]

e-boot/emit 'nids {
    /*
     * Symbols in SYM_XXX order, separated by newline characters, compressed.
     */
    EXTERN_C const REBLEN Symbol_Strings_Compressed_Size;
    EXTERN_C const REBYTE Symbol_Strings_Compressed[];

    /*
     * Compressed data of the native specifications, uncompressed during boot.
     */
    EXTERN_C const REBLEN Boot_Block_Compressed_Size;
    EXTERN_C const REBYTE Boot_Block_Compressed[];

    /*
     * Raw C function pointers for natives, take REBFRM* and return REBVAL*.
     */
    EXTERN_C const REBLEN Num_Natives;
    EXTERN_C const REBNAT Native_C_Funcs[];

    typedef struct REBOL_Boot_Block {
        $[Fields];
    } BOOT_BLK;
}

e-boot/write-emitted


=== EMIT SYMBOLS ===

e-symbols/emit 'syms-cscape {
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
     * constant assigned to it.  Since VAL_WORD_ID() will return SYM_0 for
     * all user (and extension) defined words, don't try to check equality
     * with `VAL_WORD_ID(word1) == VAL_WORD_ID(word2)`.
     */
    enum Reb_Symbol_Id {
        SYM_0 = 0,
        $(Syms-Cscape),
    };

    #define LIB_SYMS_MAX $<lib-syms-max>
    #define ALL_SYMS_MAX $<sym-n>
}

print [n "words + generics + errors"]

e-symbols/write-emitted
