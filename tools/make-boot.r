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

if trap [:import/into] [  ; See %import-shim.r
    do load append copy system/script/path %import-shim.r
]

import <bootstrap-shim.r>

import <common.r>
import <common-emitter.r>

import <systems.r>

change-dir join repo-dir %src/boot/

args: parse-args system/script/args  ; either from command line or DO/ARGS
config: config-system get 'args/OS_ID

first-rebol-commit: "19d4f969b4f5c1536f24b023991ec11ee6d5adfb"

if args/GIT_COMMIT = "unknown" [
    git-commit: null
] else [
    git-commit: args/GIT_COMMIT
    if (length of git-commit) != (length of first-rebol-commit) [
        fail [
            "GIT_COMMIT should be a full hash, e.g." first-rebol-commit newline
            "Invalid hash was:" git-commit
        ]
    ]
]

=== {SETUP PATHS AND MAKE DIRECTORIES (IF NEEDED)} ===

prep-dir: join system/options/path %prep/

mkdir/deep join prep-dir %include/
mkdir/deep join prep-dir %boot/
mkdir/deep join prep-dir %core/

Title: {
    REBOL
    Copyright 2012 REBOL Technologies
    Copyright 2012-2019 Ren-C Open Source Contributors
    REBOL is a trademark of REBOL Technologies
    Licensed under the Apache License, Version 2.0
}


=== {PROCESS COMMAND LINE ARGUMENTS} ===

; !!! Heed /script/args so you could say e.g. `do/args %make-boot.r [0.3.1]`
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

product: to-word any [get 'args/PRODUCT "core"]

platform-data: context [type: 'windows]
build: context [features: [help-strings]]

; !!! "Fetch platform specifications" (was commented out)
;
comment [
    init-build-objects/platform platform
    platform-data: platforms/:platform
    build: platform-data/builds/:product
]

=== {MAKE VERSION INFORMATION AVAILABLE TO CORE C CODE} ===

e-version: make-emitter "Version Information" (
    join prep-dir %include/tmp-version.h
)

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


=== {SET UP COLLECTION OF SYMBOL NUMBERS} ===

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

e-symbols: make-emitter "Symbol ID (SymId) Enumeration Type and Values" (
    join prep-dir %include/tmp-symid.h
)

syms-words: copy []
syms-cscape: copy []

; It's important for clarity and optimization that REB_VOID is 0 (with the
; isotope byte being 0, empty memory is interpreted as isotopic VOID, e.g.
; state of an unset variable, and that's one of many good reasons for it).
;
; But SYM_0 does not line up with that; it is distinctly used for symbols that
; do not have baked-in ID numbers.
;
; There's no datatype for VOID anyway, so having VOID line up with Lib(0) being
; its datatype isn't important.  It's just a random later symbol number.
;
sym-n: 1  ; counts up as symbols are added

add-sym: function [
    {Add SYM_XXX to enumeration}
    return: [<opt> integer!]
    word "Word (but may be in text form to gloss over bootstrap issues)"
        [word! text!]
    /exists "return ID of existing SYM_XXX constant if already exists"
    <with> sym-n
][
    if pos: find syms-words as text! word [
        if exists [return index of pos]
        fail ["Duplicate word specified" word]
    ]

    append syms-words as text! word
    append syms-cscape cscape/with {/* $<Word> */ SYM_${FORM WORD} = $<sym-n>} [
        sym-n word
    ]
    sym-n: sym-n + 1

    return null
]


=== {DATATYPE DEFINITIONS} ===

type-table: load %types.r

for-each-datatype: func [
    {Iterate type table by creating an object for each row}

    'var "Word to set each time to the row made into an object record"
        [word!]
    body "Block to evaluate each time"
        [block!]
    <local>
    name* isoname* description* typesets* class* make* mold* heart* cellmask*
    completed* running*
][
    heart*: 1  ; VOID is 0, and is not in the type table
    parse2 type-table [some [not end
        opt some tag!  ; <TYPE!> or </TYPE!> used by FOR-EACH-TYPERANGE

        set name* word!
        set description* text!
        [set isoname* quasi! | (isoname*: null)]
        [set cellmask* group!]
        [set typesets* block!]
        [and block! into [
            set class* [word! | '- | '? | quote 0]
            set make* [word! | '* | '+ | '- | '? | quote 0]
            set mold* [word! | '+ | '- | '? | quote 0]
        ] (
            name*: to text! name*
            set var make object! [
                name!: all [
                    #"!" = last name*
                    to word! name*
                ]
                name: (
                    if #"!" = last name* [take/last name*]
                    name*
                )
                cellmask: cellmask*
                heart: ensure integer! heart*
                description: ensure text! description*
                typesets: map-each any-name! typesets* [
                    any-name!: to text! any-name!
                    assert [#"!" = take/last any-name!]
                    assert ["any-" = take/part any-name! 4]
                    any-name!
                ]
                class: class*
                isoname: either isoname* [unquasi isoname*] [null]
                make: make*
                mold: mold*
            ]
            completed*: false
            running*: false
            while [true] [  ; must be in loop for BREAK or CONTINUE
                if running* [  ; must have had a CONTINUE
                    completed*: true
                    break
                ]
                running*: true
                do body
                completed*: true
                break
            ]
            if not completed* [return null]  ; must have asked to BREAK
        )]
        (heart*: heart* + 1)
    ]] else [
        fail "Couldn't fully parse %types.r"
    ]
]

for-each-typerange: func [
    {Iterate type table and create object for each <TYPE!>...</TYPE!> range}

    'var "Word to set each time to the typerange as an object record"
        [word!]
    body "Block to evaluate each time"
        [block!]
    <local> name* heart* any-name!* stack types starting
][
    stack: copy []
    types*: null

    heart*: 1  ; VOID is 0, and is not in the type table
    while [true] [  ; need to be in loop for BREAK to work
        parse2 type-table [some [
            opt some [set name* tag! (
                name*: to text! name*
                lowercase name*
                starting: not (#"/" = first name*)
                if not starting [take name*]
                any-name!*: to word! name*

                ; The name ANY-META-VALUE! is used to produce functions like
                ; ANY_META() in the C code.  Extract relevant name part.
                ;
                parse2 name* [
                    remove "any-"
                    to "!"  ; once dropped -VALUE from e.g. ANY-META-VALUE!
                    remove "!"
                ] else [
                    fail "Bad type category name"
                ]

                if starting [
                    append stack spread reduce [heart* any-name!*]
                    types*: copy []
                ] else [
                    assert [any-name!* = take/last stack]
                    set var make object! [
                        name: name*
                        any-name!: any-name!*
                        start: ensure integer! take/last stack
                        end: heart*
                        types: types*
                    ]
                    types*: null
                    do body  ; no support for BREAK/CONTINUE in bootstrap
                ]
            )]
            [set name* word! (if types* [
                name*: to text! name*
                assert [#"!" = take/last name*]
                append types* to text! name*
            ])]
            text!
            opt quasi!
            group!
            block!
            block!
            (heart*: heart* + 1)
        ]] else [
            fail "Couldn't fully parse %types.r"
        ]
        assert [empty? stack]
        break  ; doesn't return last value (bootstrap)
    ]
]

e-types: make-emitter "Datatype Definitions" (
    join prep-dir %include/tmp-kinds.h
)

rebs: collect [
    for-each-datatype t [
        assert [sym-n == t/heart]  ; SYM_XXX should equal REB_XXX value

        if not t/name! [
            add-sym as word! t/name
        ] else [
            add-sym t/name!
        ]

        keep cscape/with {REB_${T/NAME} = $<T/HEART>} [t]
    ]
]

e-types/emit 'rebs {
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

        REB_VOID = 0,  /* system relies on specific 0 heart for VOID */
        $[Rebs],
        REB_MAX,  /* one past valid types */

        REB_NULL,  /* similar--but not conflated with isotopes */
        REB_LOGIC, /* also not conflated with isotopes (at this time) */
        REB_ISOTOPE,  /* not a "type", but can answer VAL_TYPE() */

        /*
        * Invalid type bytes can currently be used for other purposes.  (If
        * bits become scarce, then the HEART_BYTE could be processed % 64
        * to get a couple more states at a slight performance cost)
        */

        REB_T_RETURN_SIGNAL  /* signals throws, etc. */
    };

    /*
     * While the VAL_TYPE() is a full byte, only 64 states can fit in the
     * payload of a TYPESET! at the moment.  Significant rethinking would be
     * necessary if this number exceeds 64.
     */
    STATIC_ASSERT(REB_MAX <= 64);
}
e-types/emit newline

e-types/emit {
    /*
     * SINGLE TYPE CHECK MACROS, e.g. IS_BLOCK() or IS_TAG()
     */
}
e-types/emit newline

boot-types: copy []  ; includes internal types like REB_VOID (but not END)

for-each-datatype t [
    if not empty? t/cellmask [
        e-types/emit 't {
            #define CELL_MASK_${T/NAME} \
                (FLAG_HEART_BYTE(REB_${T/NAME}) | $<MOLD T/CELLMASK>)
        }
        e-types/emit newline
    ]

    if not t/name! [  ; internal type
        append boot-types as word! t/name
        continue
    ]

    append boot-types t/name!

    e-types/emit 't {
        #define IS_${T/NAME}(v) \
            (VAL_TYPE(v) == REB_${T/NAME})  /* $<T/HEART> */
    }
    e-types/emit newline
]

nontypes: collect [
    keep cscape {FLAGIT_KIND(REB_VOID)}
    for-each-datatype t [
        any [not t/name!] then [
            keep cscape/with {FLAGIT_KIND(REB_${AS TEXT! T/NAME})} 't
        ]
    ]
]

value-flagnots: compose [
    "(FLAGIT_KIND(REB_MAX) - 1)"  ; Subtract 1 to get mask for everything
    (spread nontypes)  ; take out all nontypes
]

e-types/emit 'value-flagnots {
    #define TS_VALUE \
        ($<Delimit "&~" Value-Flagnots>)
}

typeset-sets: copy []

add-sym 'any-value!  ; starts the typesets, not mentioned in %types.r

for-each-datatype t [
    for-each ts-name t/typesets [
        if spot: select typeset-sets ts-name [
            append spot t/name  ; not the first time we've seen this typeset
            continue
        ]

        add-sym as word! unspaced ["any-" ts-name "!"]
        append typeset-sets ts-name
        append typeset-sets reduce [t/name]

        e-types/emit newline
        e-types/emit 'ts-name {
            #define ANY_${TS-NAME}_KIND(k) \
               (did (FLAGIT_KIND(k) & TS_${TS-NAME}))

            #define ANY_${TS-NAME}(v) \
                ANY_${TS-NAME}_KIND(VAL_TYPE(v))
        }
    ]
]

for-each-typerange tr [
    add-sym tr/any-name!

    append typeset-sets spread reduce [tr/name tr/types]

    name: copy tr/name

    e-types/emit newline
    e-types/emit 'tr {
        inline static bool ANY_${TR/NAME}_KIND(Byte k)
          { return k >= $<TR/START> and k < $<TR/END>; }

        #define ANY_${TR/NAME}(v) \
            ANY_${TR/NAME}_KIND(VAL_TYPE(v))
    }
]

for-each [ts-name types] typeset-sets [
    flagits: collect [
        for-each t-name types [
            keep cscape/with {FLAGIT_KIND(REB_${T-NAME})} 't-name
        ]
    ]
    e-types/emit [flagits ts-name] {
        #define TS_${TS-NAME} ($<Delimit "|" Flagits>)
    }  ; !!! TS_ANY_XXX is wordy, considering TS_XXX denotes a typeset
]

add-sym 'datatypes  ; signal where the datatypes stop


e-types/emit {
    /* !!! R3-Alpha made frequent use of these predefined typesets.  In Ren-C
     * they have been called into question, as to exactly how copying
     * mechanics should work.
     */

    #define TS_NOT_COPIED \
        FLAGIT_KIND(REB_PORT)

    #define TS_STD_SERIES \
        (TS_SERIES & ~TS_NOT_COPIED)

    #define TS_SERIES_OBJ \
        ((TS_SERIES | TS_CONTEXT | TS_SEQUENCE) & ~TS_NOT_COPIED)

    #define TS_ARRAYS_OBJ \
        ((TS_ARRAY | TS_CONTEXT | TS_SEQUENCE) & ~TS_NOT_COPIED)

    #define TS_CLONE \
        (TS_SERIES & ~TS_NOT_COPIED) // currently same as TS_NOT_COPIED
}
e-types/emit newline

for-each-datatype t [
    if not t/isoname [continue]  ; no special name for isotopic form

    e-types/emit 't {
        #define Is_$<Propercase To Text! T/Isoname>(v) \
            ((READABLE(v)->header.bits & (FLAG_QUOTE_BYTE(255) | FLAG_HEART_BYTE(255))) \
                == (FLAG_QUOTE_BYTE(ISOTOPE_0) | FLAG_HEART_BYTE(REB_$<T/NAME>)))

        #define Is_Meta_Of_$<Propercase To Text! T/Isoname>(v) \
        ((READABLE(v)->header.bits & (FLAG_QUOTE_BYTE(255) | FLAG_HEART_BYTE(255))) \
            == (FLAG_QUOTE_BYTE(QUASI_2) | FLAG_HEART_BYTE(REB_$<T/NAME>)))
    }
    e-types/emit newline
]

e-types/write-emitted


=== {BUILT-IN TYPE HOOKS TABLE} ===

e-hooks: make-emitter "Built-in Type Hooks" (
    join prep-dir %core/tmp-type-hooks.c
)

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

hook-list: collect [
    keep cscape {
        {  /* VOID = 0 */
            cast(CFUNC*, nullptr),  /* generic */
            cast(CFUNC*, nullptr),  /* compare */
            cast(CFUNC*, nullptr),  /* make */
            cast(CFUNC*, nullptr),  /* to */
            cast(CFUNC*, MF_Void),  /* mold */
            nullptr
        }
    }

    for-each-datatype t [
        keep cscape/with {
            {  /* $<T/NAME> = $<T/HEART> */
                cast(CFUNC*, ${"T_" Hookname T 'Class}),  /* generic */
                cast(CFUNC*, ${"CT_" Hookname T 'Class}),  /* compare */
                cast(CFUNC*, ${"MAKE_" Hookname T 'Make}),  /* make */
                cast(CFUNC*, ${"TO_" Hookname T 'Make}),  /* to */
                cast(CFUNC*, ${"MF_" Hookname T 'Mold}),  /* mold */
                nullptr
            }} [t]
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


=== {SYMBOL-TO-TYPESET-BITS MAPPING TABLE} ===

; The typesets for things like ANY-BLOCK! etc. are specified in the %types.r
; table, and turned into 64-bit bitsets.

e-typesets: make-emitter "Built-in Typesets" (
    join prep-dir %core/tmp-typesets.c
)


e-typesets/emit {
    #include "sys-core.h"
}

e-typesets/emit {
    const REBU64 Typesets[] = ^{
        TS_VALUE,  /* any-value! */
}

for-each [ts-name types] typeset-sets [
    e-typesets/emit 'ts-name {
        TS_${TS-NAME},  /* any-${ts-name}! */
    }
]

e-typesets/emit {
        0
    ^};
}

e-typesets/write-emitted


=== {SYMBOLS FOR LIB-WORDS.R} ===

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


=== {ESTABLISH SYM_XXX VALUES FOR EACH NATIVE} ===

; It's desirable for the core to be able to get the REBVAL* for a native
; quickly just by indexing into a table.  An aspect of optimizations related
; to that is that the SYM_XXX values for the names of the natives index into
; a fixed block.  We put them after the ordered words in lib.

first-native-sym: sym-n

native-names: copy []
boot-natives: stripload/gather (join prep-dir %boot/tmp-natives.r) 'native-names
insert boot-natives "["
append boot-natives "]"
for-each name native-names [
    if first-native-sym < ((add-sym/exists name) else [0]) [
        fail ["Native name collision found:" name]
    ]
]


=== {"VERB" SYMBOLS FOR GENERICS} ===

; This adds SYM_XXX constants for generics (e.g. SYM_APPEND, etc.), which
; allows C switch() statements to process them efficiently

first-generic-sym: sym-n

generic-names: load-value join prep-dir %boot/tmp-generic-names.r
boot-generics: as text! read join prep-dir %boot/tmp-generics-stripped.r

for-each name generic-names [
    assert [word? name]
    if first-generic-sym < ((add-sym/exists name) else [0]) [
        fail ["Generic name collision with Native or Generic found:" name]
    ]
]

lib-syms-max: sym-n  ; *DON'T* count the symbols in %symbols.r, added below...


=== {SYMBOLS FOR SYMBOLS.R} ===

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
        if not find syms-words as text! term [
            fail ["Expected symbol for" term "from native/generic/type"]
        ]
    ]
]


=== {SYSTEM OBJECT SELECTORS} ===

e-sysobj: make-emitter "System Object" (
    join prep-dir %include/tmp-sysobj.h
)

at-value: func ['field] [return next find boot-sysobj to-set-word field]

boot-sysobj: load strip-commas-and-null-apostrophes read/string %sysobj.r
change (at-value version) version
change (at-value commit) maybe git-commit  ; no-op if no git-commit
change (at-value build) now/utc
change (at-value product) (quote to word! product)  ; want it to be quoted

change at-value platform reduce [
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


=== {ERROR STRUCTURE AND CONSTANTS} ===

e-errfuncs: make-emitter "Error structure and functions" (
    join prep-dir %include/tmp-error-funcs.h
)

fields: collect [
    for-each word words-of ob/standard/error [
        either word = 'near [
            keep {/* near/far are old C keywords */ Reb_Cell nearest}
        ][
            keep cscape/with {Reb_Cell ${word}} 'word
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
            parse2 message [opt some [get-word! (arity: arity + 1) | skip] end]
        ] else [
            ensure text! message  ; textual message, no arguments
        ]

        ; Camel Case and make legal for C (e.g. "not-found*" => "Not_Found_P")
        ;
        f-name: uppercase/part to-c-name id 1
        parse2 f-name [
            opt some [
                "_" w:  ; <here>
                (uppercase/part w 1)
                |
                skip
            ]
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
                    keep unspaced ["Cell(const*) arg" i]
                ]
            ]
            args: collect [
                count-up i arity [keep unspaced ["arg" i]]
                keep "rebEND"
            ]
        ]

        e-errfuncs/emit [message cat id f-name params args] {
            /* $<Mold Message> */
            inline static Context(*) Error_${F-Name}_Raw($<Delimit ", " Params>) {
                return Error(SYM_${CAT}, SYM_${ID}, $<Delimit ", " Args>);
            }
        }
        e-errfuncs/emit newline
    ]
]

e-errfuncs/write-emitted


=== {LOAD BOOT MEZZANINE FUNCTIONS} ===

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

for-each section [boot-base boot-system-util boot-mezz] [
    set section s: make text! 20000
    append/line s "["
    for-each file first mezz-files [  ; doesn't use LOAD to strip
        gather:  [null]
        text: stripload/gather (
            join %../mezz/ file
        ) if section = 'boot-system-util ['sys-toplevel]
        append/line s text
    ]
    append/line s "~done~"
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


=== {MAKE BOOT BLOCK!} ===

; Create the aggregated Rebol file of all the Rebol-formatted data that is
; used in bootstrap.  This includes everything from a list of WORD!s that
; are built-in as symbols, to the sys and mezzanine functions.
;
; %tmp-boot-block.c is just a C file containing a literal constant of the
; compressed representation of %tmp-boot-block.r

e-bootblock: make-emitter "Natives and Bootstrap" (
    join prep-dir %core/tmp-boot-block.c
)

e-bootblock/emit {
    #include "sys-core.h"
}

sections: [
    boot-types
    :boot-generics
    :boot-natives
    boot-typespecs
    boot-errors
    boot-sysobj
    :boot-base
    :boot-system-util
    :boot-mezz
]

nats: collect [
    for-each name native-names [
        keep cscape/with {N_${name}} 'name
    ]
]

symbol-strings: to binary! reduce collect [  ; no bootstrap MAKE BINARY!
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
    const Byte Symbol_Strings_Compressed[$<length of compressed>] = {
        $<Binary-To-C Compressed>
    };
}

print [length of nats "natives"]

e-bootblock/emit 'nats {
    #define NUM_NATIVES $<length of nats>
    const REBLEN Num_Natives = NUM_NATIVES;

    Dispatcher* const Native_C_Funcs[NUM_NATIVES] = {
        $(Nats),
    };
}

; Build typespecs block (in same order as datatypes table)

boot-typespecs: collect [
    for-each-datatype t [
        keep reduce [t/description]
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

write-if-changed (join prep-dir %boot/tmp-boot-block.r) boot-molded
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
    const Byte Boot_Block_Compressed[$<length of compressed>] = {
        $<Binary-To-C Compressed>
    };
}

e-bootblock/write-emitted


=== {BOOT HEADER FILE} ===

e-boot: make-emitter "Bootstrap Structure and Root Module" (
    join prep-dir %include/tmp-boot.h
)

fields: collect [
    for-each word sections [
        word: form as word! word
        remove/part word 5  ; 5 leading characters, [boot-]xxx
        word: to-c-name word
        keep cscape/with {Reb_Cell ${word}} 'word
    ]
]

e-boot/emit 'fields {
    /*
     * Symbols in SYM_XXX order, separated by newline characters, compressed.
     */
    EXTERN_C const REBLEN Symbol_Strings_Compressed_Size;
    EXTERN_C const Byte Symbol_Strings_Compressed[];

    /*
     * Compressed data of the native specifications, uncompressed during boot.
     */
    EXTERN_C const REBLEN Boot_Block_Compressed_Size;
    EXTERN_C const Byte Boot_Block_Compressed[];

    /*
     * Raw C function pointers for natives, take Frame(*) and return REBVAL*.
     */
    EXTERN_C const REBLEN Num_Natives;
    EXTERN_C Dispatcher* const Native_C_Funcs[];

    typedef struct REBOL_Boot_Block {
        $[Fields];
    } BOOT_BLK;
}

e-boot/write-emitted


=== {EMIT SYMBOLS} ===

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
     * Note: Any interning that *does not have* a compile-time constant
     * assigned to it will have a symbol ID of 0.  See option(SymId) for how
     * potential bugs like `VAL_WORD_ID(a) == VAL_WORD_ID(b)` are mitigated
     * by preventing such comparisons.
     */
    enum Reb_Symbol_Id {
        $(Syms-Cscape),
    };

    #define LIB_SYMS_MAX $<lib-syms-max>
    #define ALL_SYMS_MAX $<sym-n>
}

print [sym-n "words + generics + errors"]

e-symbols/write-emitted
