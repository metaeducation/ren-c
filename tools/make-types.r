Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "Calculate type and typeset constants"
    file: %make-types.r  ; used by EMIT-HEADER to indicate emitting script
    rights: --[
        Copyright 2012 REBOL Technologies
        Copyright 2012-2025 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    version: 2.100.0
    needs: 2.100.100
    purpose: --[
       The %types.r table defines many of the properties of Rebol's
       datatypes in a declarative way.  It is parsed and used to make
       tables used by %make-boot.r, as well as C source files that are
       included by the build.
    ]--
    notes: --[
        This file has evolved over time and could be simplified drastically
        if some time and effort were put into it... it's very repetitive.
    ]--
]


if not find (words of import/) 'into [  ; See %import-shim.r
    do <import-shim.r>
]

print "--- Make Types ---"

import <bootstrap-shim.r>

import <common.r>
import <common-emitter.r>

change-dir join repo-dir %src/specs/


=== "SETUP PATHS AND MAKE DIRECTORIES (IF NEEDED)" ===

prep-dir: join system.options.path %prep/

mkdir:deep join prep-dir %include/
mkdir:deep join prep-dir %specs/
mkdir:deep join prep-dir %core/


=== "DATATYPE DEFINITIONS" ===

type-table: load3 %types.r

for-each-datatype: func [
    "Iterate type table by creating an object for each row"

    var "Word to set each time to the row made into an object record"
        [word!]
    body "Block to evaluate each time"
        [block!]
    <local>
    name* description*
    antiname* antidescription*
    typesets* heart* cellmask*
    completed* running* unstable* decorated pos
    obj
][
    obj: construct compose [(setify var) ~]  ; make variable
    body: overbind obj body  ; make variable visible to body
    var: has obj var

    heart*: 1  ; 0 is reserved
    parse3:match type-table [some [
        [opt some tag! <end> accept (okay)]
        |
        opt some tag!  ; <TYPE!> or </TYPE!> used by FOR-EACH-TYPERANGE
        name*: word!
        description*: text!
        [
            ; quasiform is word in boot
            antiname*: quasiform!, antidescription*: text!
            (
                unstable*: 'no
                antiname*: to text! unquasi antiname*
                assert [#"!" = take:last antiname*]
            )
            |
            ; unstable is conveyed by ~antiform!~:U (path in bootstrap)
            ahead chain! into [
                antiname*: quasiform!
                ['U | (panic "need U!")]
                (
                    antiname*: to text! unquasi antiname*
                    assert [#"!" = take:last antiname*]
                )
            ]
            antidescription*: text!
            (unstable*: 'yes)
            |
            (antiname*: null, antidescription*: null, unstable*: null)
        ][
            ahead group! into [
                ['CELL_MASK_NO_MARKING <end>]
                    (cellmask*: 'CELL_MASK_NO_MARKING)
                | ['payload1 <end>]
                    (cellmask*: 'CELL_FLAG_DONT_MARK_PAYLOAD_2)
                | [the :payload1 <end>]
                    (cellmask*: null)  ; don't define a CELL_MASK_XXX
                | [the :payload1 the :payload2]
                    (cellmask*: null)  ; don't define a CELL_MASK_XXX
                | ['payload1 'payload2]
                    (cellmask*: the (0))
                | ['payload1 the :payload2]
                    (cellmask*: null)  ; don't define a CELL_MASK_XXX
                | ['payload2 <end>]
                    (cellmask*: 'CELL_FLAG_DONT_MARK_PAYLOAD_1)
            ]
            | pos: <here> (
                panic ["Bad payload1/payload2 spec for" name* "in %types.r"]
            )
        ]
        [typesets*: block!]
        (
            name*: to text! name*
            assert [#"!" = take:last name*]
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
                antiname: antiname*
                antidescription: antidescription*
                unstable: unstable*
            ]
            repeat 1 body else [return null]  ; give body BREAK/CONTINUE
        )
        (heart*: heart* + 1)
    ]] else [
        panic "Couldn't fully parse %types.r"
    ]
]

; 1. Type ranges are inclusive, so the end is included in the range.  This is
;    done so that checks of a single type look more sensible when done with
;    a range (e.g. start and end are the same)
;
for-each-typerange: func [
    "Iterate type table and create object for each <TYPE!>...</TYPE!> range"

    var "Word to set each time to the typerange as an object record"
        [word!]
    body "Block to evaluate each time"
        [block!]
    <local> name* heart* any-name!* stack types* starting
    obj
][
    obj: construct compose [(setify var) ~]  ; make variable
    body: overbind obj body  ; make variable visible to body
    var: has obj var

    stack: copy []
    types*: _  ; will be put in a block, can't be null

    heart*: 1  ; 0 is reserved
    cycle [  ; need to be in loop for BREAK to work
        parse3:match type-table [some [not <end>
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
                    panic "Bad type category name"
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
                        end: heart* - 1  ; compare end using <=, not <, see [1]
                        types: types*
                    ]
                    types*: _
                    eval body  ; no support for BREAK/CONTINUE in bootstrap
                ]
            )]
            [<end> | [
                [name*: word! (if not space? types* [
                    name*: to text! name*
                    assert [#"?" <> last name*]
                    append types* to text! name*
                ])]
                text!
                opt [[quasiform! | chain!] text!]
                group!
                block!
                (heart*: heart* + 1)
            ]]
        ]] else [
            panic "Couldn't fully parse %types.r"
        ]
        assert [empty? stack]
        break  ; doesn't return last value (bootstrap)
    ]
]


=== "MACROS LIKE Is_Block(), OTHER DATATYPE DEFINITIONS" ===

e-types: make-emitter "Datatype Definitions" (
    join prep-dir %include/tmp-typesets.h
)

e-types/emit [--[
    /* Tables generated from %types.r for builtin typesets */
    extern TypesetFlags const g_typesets[];  // up to 255 allowed
    extern uint_fast32_t const g_sparse_memberships[MAX_TYPE_BYTE_ELEMENT + 1];
]--]

e-types/emit [--[
    /*
     * SINGLE TYPE CHECK MACROS, e.g. Is_Block() or Is_Tag()
     *
     * Originally these macros looked like:
     *
     *     #define Is_Text(cell) \
     *         (Type_Of(cell) == TYPE_TEXT)
     *
     * So you'd calculate TYPE_QUOTED, TYPE_QUASI, or TYPE_ANTIFORM from the
     * LIFT_BYTE(), and those would be filtered out and not match.
     *
     * This was changed to instead mask out the heart byte and lift byte
     * from the header, and compare to the precise mask of NOQUOTE_2 with
     * the specific heart byte:
     *
     *     #define Is_Text(cell) \
     *         Cell_Has_Lift_Heart_No_Sigil(NOQUOTE_2, TYPE_TEXT, (cell))
     *
     * This avoids the branching in Type_Of(), so it's a slight bit faster.
     */
]--]

for-each-datatype 't [
    if t.cellmask [
        e-types/emit [t --[
            #define CELL_MASK_${T.NAME} \
                (FLAG_HEART(TYPE_${T.NAME}) | $<MOLD T.CELLMASK>)
        ]--]
    ]

    e-types/emit [propercase-of t --[
        #define Is_${propercase-of T.name}(value) \
            Cell_Has_Lift_Heart_No_Sigil(NOQUOTE_2, TYPE_$<T.NAME>, \
                ensure(Value*, (value)))
    ]--]
]

sparse-typesets: copy []

for-each-typerange 'tr [  ; typeranges first (e.g. ANY-STRING? < ANY-UTF8?)
    let proper-name: propercase-of tr.name

    e-types/emit [tr --[
        INLINE bool Any_${Proper-Name}_Type(Option(Type) t)
          { return cast(Byte, (t)) >= $<TR.START> and cast(Byte, (t)) <= $<TR.END>; }

        #define Any_${Proper-Name}(cell) \
            Any_${Proper-Name}_Type(Type_Of(cell))
    ]--]
]

for-each-datatype 't [
    if t.antiname [  ; if there was a ~antiname~ in types.r for this type
        append t.typesets "isotopic"  ; add to the Any_Isotopic() typeset
    ]

    for-each 'ts-name t.typesets [
        let spot
        if spot: select sparse-typesets ts-name [
            append spot t.name  ; not the first time we've seen this typeset
            continue
        ]

        append sparse-typesets ts-name
        append sparse-typesets reduce [t.name]
    ]
]


=== "GENERATE TYPESET_FLAG_XXX" ===

; Non-range typesets are handled by checking a flag in a static array which
; for each kind has a bitset of typeset flags for each set the kind is in.

e-types/emit -[
    /* Furthest left bit is used to convey when a typeset table entry is based
     * on a range of heart bytes as opposed to being a typeset bit.
     */
    #define TYPESET_FLAG_0_RANGE  FLAG_LEFT_BIT(0)
]-

shift-by: 1  ; start at 1, since FLAG_LEFT_BIT(0) indicates a ranged typeset

for-each [ts-name types] sparse-typesets [
    e-types/emit cscape [ts-name --[
        #define TYPESET_FLAG_${TS-NAME}  FLAG_LEFT_BIT($<shift-by>)
    ]--]
    shift-by: me + 1

    if shift-by > 32 [
        panic [
            "Current design only allows for 31 sparse typesets." newline
            "64-bit integers would need to be used for more."
        ]
    ]
]

e-types/emit newline

for-each [ts-name types] sparse-typesets [
    e-types/emit [propercase-of ts-name --[
        INLINE bool Any_${propercase-of Ts-Name}_Type(Option(Type) t) {
            return did (g_sparse_memberships[cast(Byte, (t))] & TYPESET_FLAG_${TS-NAME});
        }

        #define Any_${propercase-of Ts-Name}(cell) \
            Any_${propercase-of Ts-Name}_Type(Type_Of(cell))
    ]--]
]

for-each-datatype 't [
    if not t.antiname [continue]  ; no special name for antiform form

    let need: either yes? t.unstable ["Atom"] ["Value"]

    let proper-name: propercase-of t.antiname

    ; Note: Ensure_Readable() not defined yet at this point, so defined as
    ; a macro vs. an inline function.  Revisit.
    ;
    e-types/emit [t proper-name --[
        #define Is_$<Proper-Name>(cell) \
            Cell_Has_Lift_Heart_No_Sigil(ANTIFORM_1, TYPE_$<T.NAME>, \
                ensure($<Need>*, (cell)))


        #define Is_Lifted_$<Proper-Name>(cell) \
            Cell_Has_Lift_Heart_No_Sigil(QUASIFORM_3, TYPE_$<T.NAME>, \
                ensure(Value*, (cell)))

        #define Is_Quasi_$<Propercase-Of T.Name>(cell) \
            Is_Lifted_$<Proper-Name>(cell)  /* alternative */
    ]--]

    if yes? t.unstable [continue]

    ; Usually we don't want people testing Atom* directly to see if it's any
    ; stable type (antiform or otherwise) because it might be a PACK! or an
    ; ERROR! and need to decay.  But occasionally code is checking return
    ; types and has special reasons to see if an Atom is an ACTION! or a
    ; TRASH!, etc.  Allow the test, but throw in a speedbump by naming them
    ; Is_Possibly_Unstable_Atom_Action() instead of just Is_Action(), to
    ; provoke some thought from testing casually.
    ;
    e-types/emit [t proper-name --[
        #define Is_Possibly_Unstable_Atom_$<Proper-Name>(atom) \
            Cell_Has_Lift_Heart_No_Sigil(ANTIFORM_1, TYPE_$<T.NAME>, \
                ensure(Atom*, (atom)))
    ]--]
]

e-types/write-emitted


=== "CALCULATE SPARSE TYPESET MEMBERSHIPS" ===

; Some typesets in %types.r are in tag ranges like <ANY-XXX?>...</ANY-XXX?>.
; These can be checked by range.  Others are in a block with the type, like
; [any-unit? any-inert?].  These sparse typesets get TYPESET_FLAG_XXX that
; are collected into a `g_sparse_memberships[]` C array.

memberships: copy []

for-each-datatype 't [
    let flagits: collect [
        for-each [ts-name types] sparse-typesets [
            if not find types t.name [continue]

            keep cscape [ts-name "TYPESET_FLAG_${TS-NAME}"]
        ]
    ]
    if empty? flagits [
        append memberships cscape [t -[/* $<t.name> - $<t.heart> */  0]-]
    ] else [
        append memberships cscape [flagits t
            --[/* $<t.name> - $<t.heart> */  ($<Delimit " | " Flagits>)]--
        ]
    ]
]


=== "SYMBOL-TO-TYPESET-BITS MAPPING TABLE" ===

e-typesets: make-emitter "Built-in Typesets (Ranged and Sparse)" (
    join prep-dir %core/tmp-typesets.c
)
e-typesets/emit [--[
    #include "sys-core.h"
]--]

e-typeset-bytes: make-emitter "Typeset Byte Mapping" (
    join prep-dir %specs/tmp-typeset-bytes.r
)

e-typespecs: make-emitter "Type Help Descriptions" (
    join prep-dir %specs/tmp-typespecs.r
)

plains: make block! 64  ; "TYPE_XXX = (num)" for all KIND_BYTE
pseudotype-hearts: make block! 128  ; "PSEUDO_XXX = (num)" for all KIND_BYTE
pseudotypes: make block! 128  ; "TYPE_XXX = (num)" for all pseudotypes
types: make block! 128
typedefines: make block! 128

singlehearts: make block! 256  ; tricky thing, see description in comments

typeset-flags: copy []

index: 1

max-heart: ~

for-each-datatype 't [  ; plains
    append plains cscape [t
        --[ENUM_TYPE_${T.NAME} = $<index>]--
    ]

    append pseudotype-hearts cscape [t
        --[ENUM_PSEUDO_${T.NAME} = $<index>]--
    ]

    append types cscape [t
        --[TYPE_${T.NAME} = $<index>]--
    ]
    max-heart: cscape [t --[TYPE_${T.NAME}]--]  ; overwritten until max found

    append typedefines cscape [t
        --[#define TYPE_${T.NAME}  HEART_ENUM(${T.NAME})]--
    ]

    append singlehearts cscape [t
        --[SINGLEHEART_TAIL_SPACE_${T.NAME} = $<index * 256>]--
    ]
    append singlehearts cscape [t
        --[SINGLEHEART_HEAD_SPACE_${T.NAME} = $<(index * 256) + 1>]--
    ]

    e-typeset-bytes/emit [t -[
        $<t.name> $<index>
    ]-]

    e-typespecs/emit [t -[
        $<t.name> $<mold t.description>
    ]-]

    append typeset-flags cscape [t --[
        /* $<index> - $<t.name> */
        TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE($<index>) | FLAG_FOURTH_BYTE($<index>)
    ]--]

    index: me + 1
]

for-each [pseudo spec] [
    metaform "marker to read unlifted and write lifted representations"
    tied "mark to bind in the evaluator in current context, and drop mark"
    pinned "mark to bind in the evaluator in current context, and keep mark"

    ; ^-- "fundamentals" (though not "plain") should be first

    quasiform "value which evaluates to an antiform"
    quoted "container for arbitrary levels of quoting"
][
    append memberships cscape [pseudo
        --[/* $<pseudo> - $<index> */  (TYPESET_FLAG_BRANCH)]--
    ]

    append pseudotypes cscape [pseudo
        --[ENUM_TYPE_$<PSEUDO> = $<index>]--
    ]

    append types cscape [pseudo
        --[TYPE_$<PSEUDO> = $<index>]--
    ]

    append typedefines cscape [pseudo
        --[#define TYPE_$<PSEUDO>  TYPE_ENUM($<PSEUDO>)]--
    ]

    e-typeset-bytes/emit [pseudo --[
        $<pseudo> $<index>
    ]--]

    e-typespecs/emit [pseudo spec --[
        $<pseudo> $<mold spec>
    ]--]

    append typeset-flags cscape [pseudo --[
        /* $<index> - $<pseudo> */
        TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE($<index>) | FLAG_FOURTH_BYTE($<index>)
    ]--]

    index: me + 1
]

first-antiform-index: index

max-type: ~

for-each-datatype 't [  ; now generate bytes for antiforms
    if t.antiname [
        append pseudotypes cscape [t
            --[ENUM_TYPE_${T.ANTINAME} = $<index>]--
        ]

        append types cscape [t
            --[TYPE_${T.ANTINAME} = $<index>]--
        ]
        max-type: cscape [t
            --[TYPE_${T.ANTINAME}]--  ; overwritten until max found
        ]

        append typedefines cscape [t
            --[#define TYPE_${T.ANTINAME}  TYPE_ENUM(${T.ANTINAME})]--
        ]

        e-typeset-bytes/emit [t -[
            ${t.antiname} $<index>
        ]-]

        e-typespecs/emit [t -[
            $<t.antiname> $<mold t.antidescription>
        ]-]

        append typeset-flags cscape [t --[
            /* $<index> - ${t.antiname} */
            TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE($<index>) | FLAG_FOURTH_BYTE($<index>)
        ]--]
    ] else [
        append pseudotypes cscape [t --[ENUM_TYPE_$<index> = $<index>]--]

        append typedefines cscape [t
            --[/* no #define for TYPE_$<index> */]--
        ]

        append types cscape [t --[TYPE_$<index> = $<index>]--]
        max-type: cscape [t --[TYPE_$<index>]--]

        ; don't need a #define for these

        e-typeset-bytes/emit [t -[
            ~ $<index>
        ]-]

        e-typespecs/emit [t -[
            ~ ~
        ]-]

        append typeset-flags cscape [t --[
            /* $<index> - <unused> */  0
        ]--]
    ]
    index: me + 1
]


; antiform range check (core uses Is_Antiform() which just checks heart byte)
(
    e-typeset-bytes/emit [no-tildes -[
        any-antiform $<index>
    ]-]

    append typeset-flags cscape [t --[
        /* $<index> - any-antiform */
        TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE($<first-antiform-index>) | FLAG_FOURTH_BYTE($<index - 1>)
    ]--]
    index: me + 1
)

for-each-typerange 'tr [  ; range, typeset is a start and end
    e-typeset-bytes/emit [tr -[
        any-$<tr.name> $<index>
    ]-]

    append typeset-flags cscape [tr --[
        /* $<index> - any-$<tr.name> */
        TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE($<TR.START>) | FLAG_FOURTH_BYTE($<TR.END>)
    ]--]
    index: index + 1
]

for-each [ts-name types] sparse-typesets [  ; sparse, typeset is a single flag
    e-typeset-bytes/emit [ts-name -[
        any-$<ts-name> $<index>
    ]-]

    append typeset-flags cscape [ts-name --[
        /* $<index> - any-$<ts-name> */
        TYPESET_FLAG_${TS-NAME}
    ]--]
    index: index + 1
]


; Add ANY-PLAIN? to be anything that's not meta/tied/pinned/quoted/quasi.
(
    e-typeset-bytes/emit [ts-name -[
        any-plain $<index>
    ]-]

    append typeset-flags cscape [tr --[
        /* $<index> - any-plain */
        TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE(0) | FLAG_FOURTH_BYTE(cast(Byte, MAX_HEART))
    ]--]
    index: index + 1
)


; Add ANY-FUNDAMENTAL? to go right up to the max heart byte (don't include
; quoted or quasi).  Include TYPE_0 for ExtraHeart types.
(
    e-typeset-bytes/emit [ts-name -[
        any-fundamental $<index>
    ]-]

    append typeset-flags cscape [tr --[
        /* $<index> - any-fundamental */
        TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE(0) | FLAG_FOURTH_BYTE(cast(Byte, MAX_TYPE_FUNDAMENTAL))
    ]--]
    index: index + 1
)


; Add ANY-ELEMENT? to the absolute end of the list, so it hooks last.  Include
; TYPE_QUOTED and TYPE_QUASI, and TYPE_0 for ExtraHeart extension types.
(
    e-typeset-bytes/emit [ts-name -[
        any-element $<index>
    ]-]

    append typeset-flags cscape [tr --[
        /* $<index> - any-element */
        TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE(0) | FLAG_FOURTH_BYTE(MAX_TYPE_ELEMENT)
    ]--]
    index: index + 1
)


e-typesets/emit [--[
    /*
     * Builtin "typesets" use either ranges or sparse bits to answer whether
     * a type matches a typeset.  If the top bit TYPESET_FLAG_0_RANGE is not
     * set, then the entry holds a single typeset flag which can be tested
     * for in that datatype's g_sparse_memberships[] entry.  But if the top
     * bit is set, then the bottom two bytes represent a range of heart
     * bytes that you test to see if the Type byte is between.
     */
    TypesetFlags const g_typesets[] = {
        /* 0 - <ExtraHeart> */  0,
        $(Typeset-Flags),
    };

    /*
     * For each non-antiform TYPE_XXX, this is the OR'd together flags of all
     * the sparse typesets that datatype is a member of.  There can be up
     * to 31 of those TYPESET_FLAG_XXX flags in this model (avoids dependency
     * on 64-bit integers, which we are attempting to excise from the system).
     */
    uint_fast32_t const g_sparse_memberships[MAX_TYPE_BYTE_ELEMENT + 1] = {
        /* 0 - <ExtraHeart> */  0,
        $(Memberships),
    };
]--]

e-typesets/write-emitted

e-typeset-bytes/write-emitted

e-typespecs/write-emitted


=== "EMIT THE HEARTS" ===

e-hearts: make-emitter "Cell Hearts Enum" (
    join prep-dir %include/tmp-hearts.h
)

e-hearts/emit [rebs --[
    /*
     * INTERNAL CELL HEART ENUM, e.g. TYPE_BLOCK or TYPE_TAG
     *
     * GENERATED FROM %TYPES.R
     *
     * Do not export these values via libRebol, as the numbers can change.
     * Their ordering is for supporting tricks--like being able to quickly
     * check if a type Is_Bindable_Heart().  So when types are added or
     * removed, the numbers must shuffle around to preserve invariants.
     *
     * NOTE ABOUT C++11 ENUM TYPING: It is best not to specify an "underlying
     * type" because that prohibits certain optimizations, which the compiler
     * can make based on knowing a value is only in the range of the enum.
     */
    #if (! DEBUG_EXTRA_HEART_CHECKS)
        typedef enum {
            TYPE_0_internal = 0,  /* use TYPE_0 or TYPE_0_constexpr */
            $(Types),
        } TypeEnum;

        #define HEART_ENUM(name)  TYPE_##name
    #else
        /*
         * The "Extra Heart Byte Checks" are designed to make sure you don't
         * pass Type where Heart is expected, or write things like TYPE_QUOTED
         * or TYPE_SPLICE into the KIND_BYTE().
         *
         * Accomplishing this rigorously requires using C++ enum classes.
         * An enum class has to be used so that the values can be in a
         * switch statement, and so that type checking can differentiate
         * between them in order to overload functions differently for
         * values in the range of hearts vs. in the range of hearts and types.
         *
         * (This would be much easier if C++ defined enum inheritance, but
         * it does not, and as far as I can tell this is the only way you
         * can pull it off.)
         */

      #if DEBUG_TYPE_ENUMS_USE_ENUM_CLASS  /* needs msvc for lax comparison */
        enum class HeartEnum {
      #else
        enum HeartEnum {
      #endif
            ENUM_TYPE_0_internal = 0,  /* use TYPE_0 or TYPE_0_constexpr */
            $(Plains),
        };

      #if DEBUG_TYPE_ENUMS_USE_ENUM_CLASS  /* needs msvc for lax comparison */
        enum class TypeEnum {
      #else
        enum TypeEnum {
      #endif
            /* PSEUDO_XXX placeholders for values in range of heart byte */
            ENUM_PSEUDO_0_internal = 0,
            $[Pseudotype-Hearts],

            /* TYPE_XXX pseudotypes for values out of range of heart byte */
            $(Pseudotypes),
        };

    #if DEBUG_TYPE_ENUMS_USE_ENUM_CLASS  /* enum class needs qualifiers */
      #define HEART_ENUM(name)  HeartEnum::ENUM_TYPE_##name
      #define TYPE_ENUM(name)   TypeEnum::ENUM_TYPE_##name
    #else
      #define HEART_ENUM(name)  ENUM_TYPE_##name
      #define TYPE_ENUM(name)   ENUM_TYPE_##name
    #endif

        $[Typedefines]
    #endif

    #define MAX_HEART  $<MAX-HEART>
    STATIC_ASSERT(cast(Byte, MAX_HEART) < 64);

    STATIC_ASSERT(cast(int, TYPE_METAFORM) == cast(int, MAX_HEART) + 1);
    STATIC_ASSERT(cast(int, TYPE_TIED) == cast(int, MAX_HEART) + 2);
    STATIC_ASSERT(cast(int, TYPE_PINNED) == cast(int, MAX_HEART) + 3);
    STATIC_ASSERT(cast(int, TYPE_QUASIFORM) == cast(int, MAX_HEART) + 4);
    STATIC_ASSERT(cast(int, TYPE_QUOTED) == cast(int, MAX_HEART) + 5);

    #define MAX_TYPE_FUNDAMENTAL  TYPE_PINNED

    #define MAX_TYPE_ELEMENT  TYPE_QUOTED
    #define MAX_TYPE_BYTE_ELEMENT  cast(Byte, TYPE_QUOTED)

    #define MAX_TYPE  $<MAX-TYPE>
    #define MAX_TYPE_BYTE  cast(Byte, $<MAX-TYPE>)

    STATIC_ASSERT(cast(int, $<MAX-TYPE>) <= 256);  /* Stored in bytes */

    /*
     * SINGLEHEART OPTIMIZED SEQUENCE DETECTION
     *
     * Define Singleheart as an enum because we want to use them in switch()
     * but don't want them to be type-compatible with Heart or Type variables
     * due to the extra flag of information they multiplex in.  Making a
     * specialized enum type is kind of the only way to get that type checking,
     * since implicit casts to integer to facilitate switching would let you
     * use Heart or Type.
     */
    typedef enum {
        NOT_SINGLEHEART_0,  /* reserved falsey case for Option(SingleHeart) */

        /*
         * !!! VALUES INTENTIONALLY > 256, OUT OF RANGE OF HEART AND KIND !!!
         *
         * Distinct enum types won't compare directly due to -Wenum-compare.
         * But pushing the values out of each others ranges is the only way to
         * make C switch() give warnings when the wrong enum type is used in
         * a `case` label.
         */

        /*
         * !!! NOTE THAT ALL THESE COMBINATIONS ARE NOT ACTUALLY VALID !!!
         *
         * (you can't put everything in sequences)
         *
         * It's just easier to make this enum and have the math work out by
         * filling it with all the combinatorics...
         */

        $(Singlehearts),
    } SingleHeart;
]--]

e-hearts/write-emitted
