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

if not find (words of import/) 'into [  ; See %import-shim.r
    do <import-shim.r>
]

print "--- Make Boot : System Embedded Script ---"

import <bootstrap-shim.r>

import <common.r>
import <common-emitter.r>

import <platforms.r>

change-dir join repo-dir %src/boot/

args: parse-args system.script.args  ; either from command line or DO:ARGS
platform-config: configure-platform args.OS_ID

first-rebol-commit: "19d4f969b4f5c1536f24b023991ec11ee6d5adfb"

if args.GIT_COMMIT = "unknown" [
    git-commit: null
] else [
    git-commit: args.GIT_COMMIT
    if (length of git-commit) != (length of first-rebol-commit) [
        fail [
            "GIT_COMMIT should be a full hash, e.g." first-rebol-commit newline
            "Invalid hash was:" git-commit
        ]
    ]
]

=== "SETUP PATHS AND MAKE DIRECTORIES (IF NEEDED)" ===

prep-dir: join system.options.path %prep/

mkdir:deep join prep-dir %include/
mkdir:deep join prep-dir %boot/
mkdir:deep join prep-dir %core/


=== "MAKE VERSION INFORMATION AVAILABLE TO CORE C CODE" ===

e-version: make-emitter "Version Information" (
    join prep-dir %include/tmp-version.h
)

version: transcode:one read %version.r
version: to tuple! reduce [
    version.1 version.2 version.3 platform-config.id.2 platform-config.id.3
]

e-version/emit [version {
    /*
     * VERSION INFORMATION
     *
     * !!! While using 5 byte-sized integers to denote a Rebol version might
     * not be ideal, it's a standard that's been around a long time.
     */

    #define REBOL_VER $<version.1>
    #define REBOL_REV $<version.2>
    #define REBOL_UPD $<version.3>
    #define REBOL_SYS $<version.4>
    #define REBOL_VAR $<version.5>
}]
e-version/emit newline
e-version/write-emitted


=== "SET UP COLLECTION OF SYMBOL NUMBERS" ===

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

; SYM_0 is reserved for symbols that do not have baked-in ID numbers.
;
sym-n: 1  ; counts up as symbols are added

/add-sym: func [
    "Add SYM_XXX to enumeration"
    return: [~null~ integer!]
    word [word! text!]
    :exists "return ID of existing SYM_XXX constant if already exists"
    <with> sym-n
][
    if let pos: find syms-words as text! word [
        if exists [return index of pos]
        fail ["Duplicate word specified" word]
    ]

    append syms-words as text! word
    append syms-cscape cscape [
        sym-n word
        {/* $<Word> */ SYM_${FORM WORD} = $<sym-n>}
    ]
    sym-n: sym-n + 1

    return null
]


=== "DATATYPE DEFINITIONS" ===

type-table: load %types.r

/for-each-datatype: func [
    "Iterate type table by creating an object for each row"

    var "Word to set each time to the row made into an object record"
        [word!]
    body "Block to evaluate each time"
        [block!]
    <local>
    name* antiname* description* typesets* class* make* mold* heart* cellmask*
    completed* running* is-unstable* decorated
][
    obj: make object! compose [(to-set-word var) ~]  ; make variable
    body: overbind obj body  ; make variable visible to body
    var: has obj var

    heart*: 1  ; 0 is reserved
    parse3:match type-table [some [not <end>
        opt some tag!  ; <TYPE!> or </TYPE!> used by FOR-EACH-TYPERANGE

        name*: word!
        description*: text!
        [antiname*: quasiform! | (antiname*: null)]
        [cellmask*: group!]
        [is-unstable*: issue! | (is-unstable*: null)]
        [typesets*: block!]
        [ahead block! into [
            class*: [word! | '- | '? | the 0]
            make*: [word! | '* | '+ | '- | '? | the 0]
            mold*: [word! | '+ | '- | '? | the 0]
        ] (
            name*: to text! name*
            set var make object! [
                name: name*
                cellmask: cellmask*
                heart: ensure integer! heart*
                description: ensure text! description*
                typesets: map-each 'any-name! typesets* [
                    decorated: to text! any-name!
                    assert [#"?" = take:last decorated]
                    assert ["any-" = take:part decorated 4]
                    decorated  ; has now been undecorated
                ]
                class: class*
                antiname: either antiname* [to text! unquasi antiname*] [null]
                unstable: switch is-unstable* [
                    null ['no]
                    #unstable ['yes]
                    fail "unstable annotation must be #unstable"
                ]
                make: make*
                mold: mold*
            ]
            repeat 1 body else [return null]  ; give body BREAK/CONTINUE
        )]
        (heart*: heart* + 1)
    ]] else [
        fail "Couldn't fully parse %types.r"
    ]
]

/for-each-typerange: func [
    "Iterate type table and create object for each <TYPE!>...</TYPE!> range"

    var "Word to set each time to the typerange as an object record"
        [word!]
    body "Block to evaluate each time"
        [block!]
    <local> name* heart* any-name!* stack types* starting
][
    obj: make object! compose [(to-set-word var) ~]  ; make variable
    body: overbind obj body  ; make variable visible to body
    var: has obj var

    stack: copy []
    types*: _  ; will be put in a block, can't be null

    heart*: 1  ; 0 is reserved
    cycle [  ; need to be in loop for BREAK to work
        parse3:match type-table [some [
            opt some [name*: tag! (
                name*: to text! name*
                lowercase name*
                starting: not (#"/" = first name*)
                if not starting [take name*]
                any-name!*: to word! name*

                ; The name ANY-META-VALUE! is used to produce functions like
                ; ANY_META() in the C code.  Extract relevant name part.
                ;
                parse3:match name* [
                    opt remove "any-"
                    to "?"  ; once dropped -VALUE from e.g. ANY-META-VALUE?
                    remove "?"
                ] else [
                    fail "Bad type category name"
                ]

                if starting [
                    append stack spread reduce [heart* any-name!*]
                    types*: copy []
                ] else [
                    assert [any-name!* = take:last stack]
                    set var make object! [
                        name: name*
                        any-name!: any-name!*
                        start: ensure integer! take:last stack
                        end: heart*
                        types: types*
                    ]
                    types*: _
                    eval body  ; no support for BREAK/CONTINUE in bootstrap
                ]
            )]
            [name*: word! (if not blank? types* [
                name*: to text! name*
                assert [#"?" <> last name*]
                append types* to text! name*
            ])]
            text!
            opt quasiform!
            group!
            opt issue!
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


=== "HEARTS ENUM FILE" ===

e-hearts: make-emitter "Cell Hearts Enum" (
    join prep-dir %include/tmp-hearts.h
)

rebs: collect [
    for-each-datatype 't [
        assert [sym-n == t.heart]  ; SYM_XXX should equal REB_XXX value
        add-sym unspaced t.name

        any [
            t.name = "quasiform"
            t.name = "quoted"
            t.name = "antiform"
        ] else [
            keep cscape [t {REB_${T.NAME} = $<T.HEART>}]
        ]
    ]
]

kinds: collect [
    for-each-datatype 't [
        any [
            t.name = "quasiform"
            t.name = "quoted"
            t.name = "antiform"
        ] else [
            keep cscape [t {KIND_${T.NAME} = $<T.HEART>}]
        ]
    ]
]

e-hearts/emit [rebs {
    /*
     * INTERNAL CELL HEART ENUM, e.g. REB_BLOCK or REB_TAG
     *
     * GENERATED FROM %TYPES.R
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
    #if (! CPLUSPLUS_11 || ! DEBUG  || defined(__clang__))
        enum HeartKindEnum {
            REB_0 = 0,  /* reserved */
            $[Rebs],
            REB_MAX_HEART,
            REB_QUASIFORM = REB_MAX_HEART,
            REB_QUOTED,
            REB_ANTIFORM,
            REB_MAX,

            REB_T_RETURN_SIGNAL  /* signals throws, etc. */
        };
    #else
        enum HeartEnum {
            REB_0 = 0,  /* reserved */
            $[Rebs],
            REB_MAX_HEART,  /* one past valid types */
        };

        enum KindEnum {
            KIND_0 = 0,  /* reserved */
            $[Kinds],
            REB_QUASIFORM,
            REB_QUOTED,
            REB_ANTIFORM,
            REB_MAX,

            REB_T_RETURN_SIGNAL  /* signals throws, etc. */
        };
    #endif

    STATIC_ASSERT(u_cast(int, REB_QUASIFORM) == u_cast(int, REB_MAX_HEART));
    STATIC_ASSERT(REB_MAX < 256);  /* Stored in bytes */
}]
e-hearts/emit newline

e-hearts/write-emitted


=== "MACROS LIKE Is_Block(), OTHER DATATYPE DEFINITIONS" ===

e-types: make-emitter "Datatype Definitions" (
    join prep-dir %include/tmp-typesets.h
)

e-types/emit [{
    /* Tables generated from %types.r for builtin typesets */
    extern Decider* const g_type_deciders[];
    extern uint_fast32_t const g_typeset_memberships[];
}]
e-types/emit newline

e-types/emit {
    /*
     * SINGLE TYPE CHECK MACROS, e.g. Is_Block() or Is_Tag()
     */
}
e-types/emit newline

for-each-datatype 't [
    ;
    ; Pseudotypes don't make macros or cell masks.
    ;
    if find ["quoted" "quasiform" "antiform"] ensure text! t.name  [
        continue
    ]

    if not empty? t.cellmask [
        e-types/emit [t {
            #define CELL_MASK_${T.NAME} \
                (FLAG_HEART_BYTE(REB_${T.NAME}) | $<MOLD T.CELLMASK>)
        }]
        e-types/emit newline
    ]

    e-types/emit [propercase-of t {
        #define Is_${propercase-of T.name}(v) \
            (VAL_TYPE(v) == REB_${T.NAME})  /* $<T.HEART> */
    }]
    e-types/emit newline
]

; Type constraints: integer! is &(integer?) and distinct from &integer

for-each-datatype 't [
    add-sym unspaced [t.name "?"]
    add-sym unspaced [t.name "!"]
]

typeset-sets: copy []

for-each-datatype 't [
    if t.antiname [  ; if there was a ~antiname~ in types.r for this type
        append t.typesets "isotopic"  ; add to the Any_Isotopic() typeset
    ]

    for-each ts-name t.typesets [
        if spot: select typeset-sets ts-name [
            append spot t.name  ; not the first time we've seen this typeset
            continue
        ]

        add-sym unspaced ["any-" ts-name "?"]

        append typeset-sets ts-name
        append typeset-sets reduce [t.name]

        e-types/emit newline
        e-types/emit [propercase-of ts-name {
            #define Any_${propercase-of Ts-Name}_Kind(k) \
               (did (g_typeset_memberships[k] & TYPESET_FLAG_${TS-NAME}))

            #define Any_${propercase-of Ts-Name}(v) \
                Any_${propercase-of Ts-Name}_Kind(VAL_TYPE(v))
        }]
    ]
]

for-each-typerange 'tr [
    add-sym replace to text! tr.any-name! "!" "?"

    append typeset-sets spread reduce [tr.name tr.types]

    name: copy tr.name

    e-types/emit newline
    e-types/emit [propercase-of tr {
        INLINE bool Any_${propercase-of Tr.Name}_Kind(Byte k)
          { return k >= $<TR.START> and k < $<TR.END>; }

        #define Any_${propercase-of Tr.Name}(v) \
            Any_${propercase-of Tr.Name}_Kind(VAL_TYPE(v))
    }]
]

=== "GENERATE TYPESET_FLAG_XXX" ===

; Non-range typesets are handled by checking a flag in a static array which
; for each kind has a bitset of typeset flags for each set the kind is in.

ts-index: 0

for-each [ts-name types] typeset-sets [
    if blank? types [continue]  ; done with ranges, no TS_XXX

    e-types/emit [ts-name {
        #define TYPESET_FLAG_${TS-NAME} FLAG_LEFT_BIT($<ts-index>)
    }]
    ts-index: ts-index + 1
]
assert [ts-index < 32]  ; typesets use uint_fast32_t

e-types/emit newline


add-sym 'datatypes  ; signal where the datatypes stop

for-each-datatype 't [
    if not t.antiname [continue]  ; no special name for antiform form

    need: either yes? t.unstable ["Atom"] ["Value"]

    ; Note: Ensure_Readable() not defined yet at this point, so defined as
    ; a macro vs. an inline function.  Revisit.
    ;
    e-types/emit [t {
        INLINE bool Is_$<Propercase T.Antiname>_Core(Need(const $<Need>*) v) { \
            return ((v->header.bits & (FLAG_QUOTE_BYTE(255) | FLAG_HEART_BYTE(255))) \
                == (FLAG_QUOTE_BYTE_ANTIFORM_0 | FLAG_HEART_BYTE(REB_$<T.NAME>))); \
        }

        #define Is_$<Propercase T.Antiname>(v) \
            Is_$<Propercase T.Antiname>_Core(Ensure_Readable(v))

        #define Is_Meta_Of_$<Propercase T.Antiname>(v) \
        ((Ensure_Readable(v)->header.bits & (FLAG_QUOTE_BYTE(255) | FLAG_HEART_BYTE(255))) \
            == (FLAG_QUOTE_BYTE_QUASIFORM_2 | FLAG_HEART_BYTE(REB_$<T.NAME>)))

        #define Is_Quasi_$<Propercase T.Name>(v) \
            Is_Meta_Of_$<Propercase T.Antiname>(v)  /* alternative */
    }]
    e-types/emit newline
]

e-types/write-emitted


=== "BUILT-IN TYPE HOOKS TABLE" ===

e-hooks: make-emitter "Built-in Type Hooks" (
    join prep-dir %core/tmp-type-hooks.c
)

/hookname: infix func [
    return: [text!]
    prefix [text!] "e.g. T_ for T_Action"
    t [object!] "type record (e.g. a row out of %types.r)"
    column [word!] "which column we are deriving the hook's name based on"
][
    if t.(column) = 0 [return "nullptr"]

    ; The CSCAPE mechanics lowercase all strings.  Uppercase it back.
    ;
    prefix: uppercase copy prefix

    return unspaced [prefix propercase-of (switch ensure word! t.(column) [
        '+ [propercase-of t.name]  ; type has its own unique hook
        '* [t.class]        ; type uses common hook for class
        '? ['unhooked]      ; datatype provided by extension
        '- ['fail]          ; service unavailable for type
    ] else [
        t.(column)      ; override with word in column
    ])]
]

hook-list: collect [
    keep cscape [{
        {  /* REB_0 is reserved */
            cast(CFunction*, nullptr),  /* generic */
            cast(CFunction*, nullptr),  /* compare */
            cast(CFunction*, nullptr),  /* make */
            cast(CFunction*, nullptr),  /* to */
            cast(CFunction*, nullptr),  /* mold */
            nullptr
        }
    }]

    for-each-datatype 't [
        if t.name = "void" [
            keep cscape [{
                {  /* VOID = $<T.HEART> */
                    cast(CFunction*, nullptr),  /* generic */
                    cast(CFunction*, nullptr),  /* compare */
                    cast(CFunction*, nullptr),  /* make */
                    cast(CFunction*, nullptr),  /* to */
                    cast(CFunction*, MF_Void),  /* mold */
                    nullptr
                }
            }]
            continue
        ]

        keep cscape [t {
            {  /* $<T.NAME> = $<T.HEART> */
                cast(CFunction*, ${"T_" Hookname T 'Class}),  /* generic */
                cast(CFunction*, ${"CT_" Hookname T 'Class}),  /* compare */
                cast(CFunction*, ${"MAKE_" Hookname T 'Make}),  /* make */
                cast(CFunction*, ${"TO_" Hookname T 'Make}),  /* to */
                cast(CFunction*, ${"MF_" Hookname T 'Mold}),  /* mold */
                nullptr
            }
        }]
    ]
]

e-hooks/emit [hook-list {
    #include "sys-core.h"

    /* See comments in %sys-ordered.h */
    CFunction* Builtin_Type_Hooks[REB_MAX][IDX_HOOKS_MAX] = {
        $(Hook-List),
    };
}]

e-hooks/write-emitted


=== "SYMBOL-TO-TYPESET-BITS MAPPING TABLE" ===

; The typesets for things like ANY-BLOCK? etc. are specified in the %types.r
; table, and turned into 64-bit bitsets.

e-typesets: make-emitter "Built-in Typesets" (
    join prep-dir %core/tmp-typesets.c
)


e-typesets/emit {
    #include "sys-core.h"
}
e-typesets/emit newline

decider-names: copy []
memberships: copy []

for-each-datatype 't [
    e-typesets/emit [t {
        bool ${Propercase T.Name}_Decider(const Value* arg)
          { return Is_${Propercase T.Name}(arg); }
    }]
    e-typesets/emit newline
    append decider-names cscape [t {${Propercase T.Name}_Decider}]

    flagits: collect [
        for-each [ts-name types] typeset-sets [
            if blank? types [continue]
            if not find types t.name [continue]

            keep cscape [ts-name {TYPESET_FLAG_${TS-NAME}}]
        ]
    ]
    if empty? flagits [
        append memberships "0"
    ] else [
        append memberships cscape [flagits ts-name {($<Delimit "|" Flagits>)}]
    ]
]

for-each [ts-name types] typeset-sets [
    e-typesets/emit [ts-name {
        bool Any_${Propercase Ts-Name}_Decider(const Value* arg)
          { return Any_${Propercase Ts-Name}(arg); }
    }]
    e-typesets/emit newline
    append decider-names cscape [ts-name {Any_${Propercase Ts-Name}_Decider}]
]

e-typesets/emit [{
    Decider* const g_type_deciders[] = {
        nullptr,  /* REB_0 is reserved */
        &$(Decider-Names),
    };
}]

e-typesets/emit [{
    uint_fast32_t const g_typeset_memberships[REB_MAX] = {
        0,  /* REB_0 is reserved */
        $(Memberships),
    };
}]

e-typesets/write-emitted


=== "SYMBOLS FOR LIB-WORDS.R" ===

; Add SYM_XXX constants for the words in %lib-words.r - these are words that
; reserve a spot in the lib context.  They can be accessed quickly, without
; going through a hash table.
;
; Since the relative order of these words is honored, that means they must
; establish their slots first.  Any natives or generics which have the same
; name will have to use the slot position established for these words.

for-each 'term load %lib-words.r [
    if issue? term [
        if not find syms-words as text! term [
            fail ["Expected symbol for" term "from [native generic type]"]
        ]
    ] else [
        add-sym term
    ]
]


=== "ESTABLISH SYM_XXX VALUES FOR EACH NATIVE" ===

; It's desirable for the core to be able to get the Value* for a native
; quickly just by indexing into a table.  An aspect of optimizations related
; to that is that the SYM_XXX values for the names of the natives index into
; a fixed block.  We put them after the ordered words in lib.

first-native-sym: sym-n

native-names: copy []

boot-natives: stripload:gather (
    join prep-dir %boot/tmp-natives.r
) $native-names

insert boot-natives "["
append boot-natives "]"
for-each 'name native-names [
    if first-native-sym < ((add-sym:exists name) else [0]) [
        fail ["Native name collision found:" name]
    ]
]


=== "'VERB' SYMBOLS FOR GENERICS" ===

; This adds SYM_XXX constants for generics (e.g. SYM_APPEND, etc.), which
; allows C switch() statements to process them efficiently

first-generic-sym: sym-n

generic-names: transcode:one read join prep-dir %boot/tmp-generic-names.r
boot-generics: as text! read join prep-dir %boot/tmp-generics-stripped.r

for-each 'name generic-names [
    assert [word? name]
    if first-generic-sym < ((add-sym:exists name) else [0]) [
        fail ["Generic name collision with Native or Generic found:" name]
    ]
]

lib-syms-max: sym-n  ; *DON'T* count the symbols in %symbols.r, added below...


=== "SYMBOLS FOR SYMBOLS.R" ===

; The %symbols.r file are terms that get SYM_XXX constants and an entry in
; the table for turning those constants into a symbol pointer.  But they do
; not have priority on establishing declarations in lib.  Hence a native or
; generic might come along and use one of these terms...meaning they have to
; yield to that position.  That's why there's no guarantee of order.

for-each 'term load %symbols.r [
    if word? term [
        add-sym term
    ] else [
        assert [issue? term]
        if not find syms-words as text! term [
            fail ["Expected symbol for" term "from [native generic type]"]
        ]
    ]
]


=== "SYSTEM OBJECT SELECTORS" ===

e-sysobj: make-emitter "System Object" (
    join prep-dir %include/tmp-sysobj.h
)

/at-value: func [field] [return next find boot-sysobj to-set-word field]

boot-sysobj: load %sysobj.r
change (at-value 'version) version
change (at-value 'commit) maybe git-commit  ; no-op if no git-commit
change (at-value 'build) now:utc
change (at-value 'product) (quote to word! "core")  ; want it to be quoted

change at-value 'platform reduce [
    any [platform-config.name "Unknown"]
    any [platform-config.build-label ""]
]

; If debugging something code in %sysobj.r, the C-DEBUG-BREAK should only
; apply in the non-bootstrap case.
;
c-debug-break: :void

ob: make object! boot-sysobj

/c-debug-break: get $lib/c-debug-break

/make-obj-defs: func [
    "Given a Rebol OBJECT!, write C structs that can access its raw variables"

    return: [~]
    e "The emitter to write definitions to"
        [object!]
    obj
    prefix
    depth
][
    let items: collect [
        let n: 1

        for-each 'field words-of obj [
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
        for-each 'field words-of obj [
            if all [
                field != 'standard
                object? get has obj field
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
            keep {/* near & far are old C keywords */ Value nearest}
        ][
            keep cscape [word {Value ${word}}]
        ]
    ]
]

e-errfuncs/emit [fields {
    /*
     * STANDARD ERROR STRUCTURE
     */
    typedef struct REBOL_Error_Vars {
        $[Fields];
    } ERROR_VARS;
}]

e-errfuncs/emit {
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
}

first-error-sym: sym-n

boot-errors: load %errors.r

for-each [sw-cat list] boot-errors [
    assert [set-word? sw-cat]
    cat: to word! sw-cat
    ensure block! list

    add-sym cat  ; category might incidentally exist as SYM_XXX

    for-each [sw-id t-message] list [
        assert [set-word? sw-id]
        id: to word! sw-id
        message: t-message

        ; Add a SYM_XXX constant for the error's ID word
        ;
        if first-error-sym < (add-sym:exists id else [0]) [
            fail ["Duplicate error ID found:" id]
        ]

        arity: 0
        if block? message [  ; can have N GET-WORD! substitution slots
            parse3 message [opt some [get-word?! (arity: arity + 1) | one]]
        ] else [
            ensure text! message  ; textual message, no arguments
        ]

        ; Camel Case and make legal for C (e.g. "not-found*" => "Not_Found_P")
        ;
        f-name: uppercase:part to-c-name id 1
        parse3 f-name [
            opt some [
                "_" w: <here>
                (uppercase:part w 1)
                |
                one
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
                count-up 'i arity [
                    keep unspaced ["const Cell* arg" i]
                ]
            ]
            args: collect [
                count-up 'i arity [keep unspaced ["arg" i]]
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
        e-errfuncs/emit newline
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

mezz-files: load %../mezz/boot-files.r  ; base, sys, mezz

sys-toplevel: copy []

; 1. The boot process makes sure that the evaluation produces a QUASI-WORD!
;    with the symbol "DONE".  We don't use a keyword here because ~done~
;    doesn't have a legal antiform (at this time), and we don't use a tripwire
;    like ~<done>~ because the bootstrap executable doesn't know how to load
;    tripwires.  The quasi-word is sufficient.
;
for-each 'section [boot-base boot-system-util boot-mezz] [
    set (inside [] section) s: make text! 20000
    append:line s "["
    for-each 'file first mezz-files [  ; doesn't use LOAD to strip
        gather:  [null]
        text: stripload:gather (
            join %../mezz/ file
        ) if section = 'boot-system-util [$sys-toplevel]
        append:line s text
    ]
    append:line s "'~done~"  ; sanity check [1]
    append:line s "]"

    mezz-files: next mezz-files
]

; We heuristically gather top level declarations in the system context, vs.
; trying to use DO and look at actual OBJECT! keys.  Previously this produced
; index numbers, but modules are no longer index-based so we make sure there
; are SYMIDs instead, so the SYM_XXX numbers can quickly produce canons that
; lead to the function definitions.

for-each 'item sys-toplevel [
    add-sym:exists as word! item
]


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

e-bootblock/emit {
    #include "sys-core.h"
}

sections: [
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
    ;
    ; Most C functions backing natives are Dispatchers (take a "Frame" pointer,
    ; return a "Bounce").  But some are intrinsics and are able to be called
    ; without building a frame.  It would be a nuisance to separate these
    ; into distinct tables, so they're all coerced to a CFunction, and then
    ; Make_Native() decides which actual function type to cast them to.
    ;
    for-each 'name native-names [
        keep cscape [name {cast(CFunction*, N_${name})}]
    ]
]

symbol-strings: to binary! reduce collect [  ; no bootstrap MAKE BINARY!
    for-each 'word syms-words [
        spelling: to text! word
        keep head change copy #{00} length of spelling
        keep spelling
    ]
]

compressed: gzip symbol-strings

e-bootblock/emit [compressed {
    /*
     * Gzip compression of symbol strings
     * Originally $<length of symbol-strings> bytes
     *
     * Size is a constant with storage vs. using a #define, so that relinking
     * is enough to sync up the referencing sites.
     */
    const Size Symbol_Strings_Compressed_Size = $<length of compressed>;
    const Byte Symbol_Strings_Compressed[$<length of compressed>] = {
        $<Binary-To-C Compressed>
    };
}]

print [length of nats "natives"]

e-bootblock/emit [nats {
    #define NUM_NATIVES $<length of nats>

    /*
     * Note: These functions may be Dispatcher* or they may be Intrinsic*.
     * Easiest to keep them in the same table, so they're typed as CFunction*.
     */
    CFunction* const g_core_native_cfuncs[NUM_NATIVES] = {
        $(Nats),
    };

    /*
     * NUM_NATIVES macro not visible outside this file, export as variable
     */
    const REBLEN g_num_core_natives = NUM_NATIVES;
}]

; Build typespecs block (in same order as datatypes table)

boot-typespecs: collect [
    for-each-datatype 't [
        keep reduce [t.description]
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

write-if-changed (join prep-dir %boot/tmp-boot-block.r) boot-molded
data: as binary! boot-molded

compressed: gzip data

e-bootblock/emit [compressed {
    /*
     * Gzip compression of boot block
     * Originally $<length of data> bytes
     *
     * Size is a constant with storage vs. using a #define, so that relinking
     * is enough to sync up the referencing sites.
     */
    const Size Boot_Block_Compressed_Size = $<length of compressed>;
    const Byte Boot_Block_Compressed[$<length of compressed>] = {
        $<Binary-To-C Compressed>
    };
}]

e-bootblock/write-emitted


=== "BOOT HEADER FILE" ===

e-boot: make-emitter "Bootstrap Structure and Root Module" (
    join prep-dir %include/tmp-boot.h
)

fields: collect [
    for-each 'word sections [
        word: form as word! word
        remove:part word 5  ; 5 leading characters, [boot-]xxx
        word: to-c-name word
        keep cscape [word {Element ${word}}]
    ]
]

e-boot/emit [fields {
    /*
     * Symbols in SYM_XXX order, separated by newline characters, compressed.
     */
    EXTERN_C const Size Symbol_Strings_Compressed_Size;
    EXTERN_C const Byte Symbol_Strings_Compressed[];

    /*
     * Compressed data of the native specifications, uncompressed during boot.
     */
    EXTERN_C const Size Boot_Block_Compressed_Size;
    EXTERN_C const Byte Boot_Block_Compressed[];

    /*
     * Raw C function pointers for natives, take Level* and return Bounce.
     */
    EXTERN_C const REBLEN g_num_core_natives;
    EXTERN_C CFunction* const g_core_native_cfuncs[];

    /*
     * Builtin Extensions
     */
    EXTERN_C const unsigned int g_num_builtin_extensions;
    EXTERN_C ExtensionCollator* const g_builtin_collators[];


    typedef struct REBOL_Boot_Block {
        $[Fields];
    } BOOT_BLK;
}]

e-boot/write-emitted


=== "EMIT SYMBOLS" ===

e-symbols/emit [syms-cscape {
    /*
     * CONSTANTS FOR BUILT-IN SYMBOLS: e.g. SYM_THRU or SYM_INTEGER_X
     *
     * ANY-WORD? uses internings of UTF-8 character strings.  An arbitrary
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
     * assigned to it will have a symbol ID of 0.  See Option(SymId) for how
     * potential bugs like `Cell_Word_Id(a) == Cell_Word_Id(b)` are mitigated
     * by preventing such comparisons.
     */
    enum SymIdEnum {
        SYM_0 = 0,
        $(Syms-Cscape),
    };

    #define LIB_SYMS_MAX $<lib-syms-max>
    #define ALL_SYMS_MAX $<sym-n>
}]

print [sym-n "words + generics + errors"]

e-symbols/write-emitted
