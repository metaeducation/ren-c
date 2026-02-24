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


=== "LOAD TYPES.R TABLE" ===

; This file is all about digesting the "dialected" %types.r table into
; into actionable information.  We use PARSE to build objects from it, and
; then operate on those objects.

type-table: load3 %types.r


=== "MAKE OBJECT!s FROM DATATYPE DEFINITIONS" ===

datatype-objects: collect [
    let heart*: 4  ; 0 is reserved and 1-3 are sigils

    let [  ; no LET in PARSE3
        name* description*
        antiname* antidescription*
        typesets* cellmask*
        completed* running* unstable* decorated pos
    ]

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
            keep make object! [
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
        )
        (heart*: heart* + 1)
    ]] else [
        panic "Couldn't fully parse %types.r"
    ]
]

=== "MAKE OBJECT!S FROM TYPE RANGES e.g. <ANY-LIST?>...</ANY-LIST>" ===

; 1. Type ranges are inclusive, so the end is included in the range.  This is
;    done so that checks of a single type look more sensible when done with
;    a range (e.g. start and end are the same)

typerange-objects: collect [
    let stack: copy []
    let types*: _  ; will be put in a block, can't be null

    let [name* any-name!* starting]

    let heart*: 4  ; 0 is reserved, 1-3 are Sigils

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
                keep make object! [
                    name: name*
                    any-name!: any-name!*
                    start: ensure integer! take:last stack
                    end: heart* - 1  ; compare end using <=, not <, see [1]
                    types: types*
                ]
                types*: _
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
]


sparse-typesets: copy []

for-each 't datatype-objects [
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


=== "MACROS LIKE Is_Block(), OTHER DATATYPE DEFINITIONS" ===

e-types: make-emitter "Datatype Definitions" (
    join prep-dir %include/tmp-typesets.h
)

e-types/emit [--[
    /*
     * === TABLES GENERATED FROM %types.r FOR BUILTIN TYPESETS ===
     */

    extern TypesetFlags const g_typesets[];  // up to 255 allowed

    /*
     * NOTE: GCC (at least 13.3.0) has in the past gotten some false positives
     * on array bounds warnings with `g_sparse_memberships`.  The code was
     * changed in ways that worked around it, but if it happens again the
     * trick was to export the array under another const pointer where the
     * compiler couldn't see the array bounds:
     *
     *     extern uint_fast32_t const * const g_memberships_hack;
     *
     * AI explains why the spurious warnings are not uncommon:
     *
     * "False positives for -Warray-bounds remain common in high optimization
     * levels (-O2 and -O3) because of Value Range Propagation (VRP). When the
     * compiler unrolls a loop or inlines a function, it may create a "phantom"
     * path where it believes an index could exceed the bounds, even if your
     * logic prevents it. Using a pointer cast is the most common 'silent' fix
     * for this issue."
     */
    extern uint_fast32_t const g_sparse_memberships[MAX_TYPEBYTE + 1];

    #define Sparse_Memberships(t) \
        g_sparse_memberships[ii_cast(Byte, known(Option(Type), (t)))]
]--]

e-types/emit [--[
    /*
     * SINGLE TYPE CHECK MACROS, e.g. Is_Block() or Is_Tag()
     *
     * Modern Ren-C carves out special values of the TYPE_BYTE for unlifted
     * values--trading off some abilities to have higher quoting levels to
     * get a very fast answer to Type_Of().  To make that answer particularly
     * fast, it doesn't canonize the quoted states to a single state...you
     * need to use functions like Is_Quoted_Type(t).  Hence there is no
     * single TYPE_QUOTED.
     *
     * But for non-quoted types, this is really only a single byte check!!
     *
     *     #define Is_Text(v)  (Type_Of(v) == TYPE_TEXT)
     */
]--]

for-each 't datatype-objects [
    if t.cellmask [
        ;
        ; NOTE: FLAG_HEART_AND_LIFT() is an inline function (repeats macro
        ; argument) so it's slower than FLAG_HEART() and FLAG_LIFT(); since
        ; this is auto-generated do it the faster way.
        ;
        e-types/emit [t --[
            #define CELL_MASK_${T.NAME} \
                (FLAG_HEART(HEART_${T.NAME}) | FLAG_LIFT(TYPE_${T.NAME}) | $<MOLD T.CELLMASK>)
        ]--]
    ]

    e-types/emit [propercase-of t --[
        #define Is_${propercase-of T.name}(v)  (Type_Of(v) == TYPE_$<T.NAME>)

        #define Is_Possibly_Unstable_Value_${propercase-of T.name}(v) \
            (Type_Of_Possibly_Unstable(v) == TYPE_$<T.NAME>)
    ]--]
]

for-each 'tr typerange-objects [  ; (e.g. ANY-STRING? < ANY-UTF8?)
    let proper-name: propercase-of tr.name

    e-types/emit [tr --[
        INLINE bool Any_${Proper-Name}_Type(Option(Type) t)
          { return ii_cast(TypeByte, (t)) >= $<TR.START> and ii_cast(TypeByte, (t)) <= $<TR.END>; }

        #define Any_${Proper-Name}(cell) \
            Any_${Proper-Name}_Type(Type_Of(cell))
    ]--]
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
        #define Any_${propercase-of Ts-Name}_Type(t) \
            (logical (Sparse_Memberships(t) & TYPESET_FLAG_${TS-NAME}))

        #define Any_${propercase-of Ts-Name}(cell) \
            Any_${propercase-of Ts-Name}_Type(Type_Of(cell))
    ]--]
]

for-each 't datatype-objects [
    if not t.antiname [continue]  ; no special name for antiform form

    let qualifier: all [yes? t.unstable, "_Possibly_Unstable"]

    let proper-name: propercase-of t.antiname

    ; Note: Readable_Cell() not defined yet at this point, so defined as
    ; a macro vs. an inline function.  Revisit.
    ;
    e-types/emit [t proper-name qualifier --[
        #define Is_$<Proper-Name>(v) \
            (Type_Of$<Opt Qualifier>(v) == TYPE_$<T.ANTINAME>)

        #define Is_Lifted_$<Proper-Name>(v) \
            Cell_Has_Lift_Heart_No_Sigil(Known_Stable(v), \
                TYPE_QUASIFORM, HEART_$<T.NAME>)

        #define Is_Quasi_$<Propercase-Of T.Name>(v) \
            Is_Lifted_$<Proper-Name>(v)  /* alternative */
    ]--]

    if yes? t.unstable [continue]

    ; Usually we don't want people testing Value* directly to see if it's any
    ; stable type (antiform or otherwise) because it might be a PACK! or a
    ; FAILURE! and need to decay.  But occasionally code is checking return
    ; types and has special reasons to see if an Value is an ACTION! or a
    ; TRASH!, etc.  Allow the test, but throw in a speedbump by naming them
    ; Is_Possibly_Unstable_Value_Action() instead of just Is_Action(), to
    ; provoke some thought from testing casually.
    ;
    e-types/emit [t proper-name --[
        #define Is_Possibly_Unstable_Value_$<Proper-Name>(v) \
            (Type_Of_Possibly_Unstable(v) == TYPE_$<T.ANTINAME>)
    ]--]
]

e-types/write-emitted


=== "SYMBOL-TO-TYPESET-BITS MAPPING TABLES" ===

; See comments where the files are written out (would be redundant to write
; them here, so better to have them show up in the .h and .c files)

e-typesets: make-emitter "Built-in Typesets (Ranged and Sparse)" (
    join prep-dir %core/tmp-typesets.c
)

e-typeset-bytes: make-emitter "Typeset Byte Mapping" (
    join prep-dir %specs/tmp-typeset-bytes.r
)

e-typespecs: make-emitter "Type Help Descriptions" (
    join prep-dir %specs/tmp-typespecs.r
)

; lists are all of the form "XXX = (num)", prefix (e.g. "TYPE_") added later
sigils: make block! 3
hearts: make block! 60  ; 64 minus sigils minus TYPE_0
quasiform: make block! 1  ; just QUASIFORM! (in a list to fit pattern)
quoteds: make block! 128  ; 128 states for 64 quote levels
antiforms: make block! 64  ; lots of blank space atm in this span

heartdefines: make block! 60
typedefines: make block! 128

singlehearts: make block! 256  ; tricky thing, see description in comments

typeset-flags: copy []  ; from ranges in %types.r, <ANY-XXX?>...</ANY-XXX?>
sparse-memberships: copy []  ; non-ranged in %types.r, [any-unit? any-inert?]

index: 1

do-appends: proc [
    which [word!]
    name [word! text! integer!]
    description [<opt> text!]
    sparse [block!]  ; possibly empty
    :ranged [block!]
][
    let enumlist: ensure block! get which

    append enumlist cscape [name
        --[${NAME} = $<index>]--
    ]

    description: default [
        assert [integer? name]
        <- "reserved for future use"
    ]

    append typedefines cscape [name
        --[#define TYPE_${NAME}  TypeEnum::ENUM_${NAME}]--
    ]

    ranged: default [
        reduce [
            "TYPESET_FLAG_0_RANGE"
            unspaced ["FLAG_THIRD_BYTE(" index ")"]
            unspaced ["FLAG_FOURTH_BYTE(" index ")"]
        ]
    ]

    e-typespecs/emit [name description -[
        $<name> $<mold description>
    ]-]

    append typeset-flags cscape [name ranged
        --[/* $<index> - $<name> */  ($<Delimit " | " Ranged>)]--
    ]

    if empty? sparse [
        append sparse-memberships cscape [name -[/* $<index> - $<name> */  0]-]
    ] else [
        append sparse-memberships cscape [name sparse
            --[/* $<index> - $<name> */  ($<Delimit " | " Sparse>)]--
        ]
    ]

    index: me + 1
]

=== "PSEUDOTYPES FOR SIGILIZED TYPES" ===

; TYPE_METAFORM, TYPE_PINNED, and TYPE_TIED come from 2-bit encoding in the
; KIND_BYTE() so are derived types when present in the TYPE_BYTE.

for-each [name description] [
    metaform "marker to read unlifted and write lifted representations"
    pinned "mark to bind in the evaluator in current context, and keep mark"
    tied "mark to bind in the evaluator in current context, and drop mark"
][
    e-typeset-bytes/emit [name "$<name> $<index>"]

    do-appends $sigils name description ["TYPESET_FLAG_BRANCH"]
]

=== "HEART TYPES" ===

; "Heart" types refer to the actual pattern of layout of a Cell.

min-heart: ~
max-heart: ~

for-each 't datatype-objects [
    e-typeset-bytes/emit [t "$<t.name> $<index>"]

    min-heart: default [t.name]
    max-heart: t.name  ; overwritten until max

    let sparse: collect [
        for-each [ts-name types] sparse-typesets [
            if not find types t.name [continue]

            keep cscape [ts-name "TYPESET_FLAG_${TS-NAME}"]
        ]
    ]

    append singlehearts cscape [t
        --[SINGLEHEART_TAIL_SPACE_${T.NAME} = $<index * 256>]--
    ]
    append singlehearts cscape [t
        --[SINGLEHEART_HEAD_SPACE_${T.NAME} = $<(index * 256) + 1>]--
    ]

    append heartdefines cscape [t
        --[#define HEART_${T.NAME}  TYPE_${T.NAME}]--
    ]

    do-appends $hearts t.name t.description sparse
]

=== "FILL SPACE UNTIL QUASIFORM (64)" ===

; These are fundamental types that are reserved for future use.  (Modern Ren-C
; gets away with fewer types, because things like SET-WORD! aren't fundamental
; because that's a CHAIN! containing a WORD!, and also extension types are
; available so long as you're willing to give up a pointer per cell for it.)

assert [index <= 64]  ; need 64 to be quasiform

while [index < 64] [
    e-typeset-bytes/emit [name "~ $<index>"]

    do-appends // [
        $hearts
        unspaced ["reserved_" index]
        "<reserved>"
        sparse: []
    ]
]

e-typeset-bytes/emit [name "quasiform $<index>"]

do-appends // [
    $quasiform
    'quasiform
    "value which evaluates to an antiform"
    ["TYPESET_FLAG_BRANCH"]
]


=== "CREATE THE QUOTING PSEUDOTYPES (UP THRU 192)" ===

; Up to 64 levels of quoting are implemented by reserving 65-192 in the
; type byte (half the states encode that the quoted thing is a quasiform too).

num-quotes: 1
is-quasi: null

max-element: ~

while [index <= 192] [
    if (num-quotes = 1) and (is-quasi = null) [
        e-typeset-bytes/emit ["quoted $<index>"]
    ] else [
        e-typeset-bytes/emit ["~ $<index>"]
    ]

    let name: unspaced [
        "quoted_" num-quotes "_time" (if num-quotes > 1 ["s"])
            "_" if not is-quasi ["non"] "quasi"
    ]
    max-element: name

    do-appends // [
        $quoteds
        name
        "<internal>"
        sparse: ["TYPESET_FLAG_BRANCH"]
        ranged: reduce [
            "TYPESET_FLAG_0_RANGE"
            unspaced ["FLAG_THIRD_BYTE(TYPE_QUOTED_1_TIME_NONQUASI)"]
            unspaced ["FLAG_FOURTH_BYTE(TYPE_QUOTED_64_TIMES_QUASI)"]
        ]
    ]
    if is-quasi [
        num-quotes: me + 1
        is-quasi: null
    ] else [
        is-quasi: okay
    ]
]

=== "CREATE THE ANTIFORMS" ===

first-antiform-index: index

antiheart-aliases: copy []

max-type: ~

do-anti-appends: proc [
    name [text! word!]
    antiname [text! word!]
    antidescription [text!]
][
    e-typeset-bytes/emit [antiname "${antiname} $<index>"]

    append typedefines cscape [antiname
        --[#define TYPE_${ANTINAME}  TypeEnum::ENUM_${ANTINAME}]--
    ]

    append antiforms cscape [antiname
        --[${ANTINAME} = $<index>]--
    ]
    max-type: cscape [antiname
        --[TYPE_${ANTINAME}]--  ; overwritten until max found
    ]

    append antiheart-aliases cscape [name antiname
        --[#define HEART_${NAME}_SIGNIFYING_${ANTINAME}  HEART_${NAME}]--
    ]

    append sparse-memberships cscape [antiname
        --[/* $<index> - $<antiname> */  (0)]--
    ]

    e-typespecs/emit [antiname antidescription -[
        $<antiname> $<mold antidescription>
    ]-]

    append typeset-flags cscape [antiname --[
        /* $<index> - ${antiname} */
        TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE($<index>) | FLAG_FOURTH_BYTE($<index>)
    ]--]

    index: me + 1
]


=== "ANTIFORMS (STABLE FIRST, THEN UNSTABLE)" ===

; We want the stable antiforms to come first, this way we can byte check for
; instability by just asking if the value is greater than the minimum unstable

min-antiform: ~
max-stable: ~
max-antiform: ~  ; overwritten each time

for-each 't datatype-objects [
    if not t.antiname [continue]
    if t.unstable = 'yes [continue]  ; do stable antiforms first
    min-antiform: default [t.antiname]
    max-stable: t.antiname
    do-anti-appends t.name t.antiname t.antidescription
]

for-each 't datatype-objects [
    if not t.antiname [continue]
    if t.unstable = 'no [continue]  ; now unstable ones
    max-antiform: t.antiname
    do-anti-appends t.name t.antiname t.antidescription
]

=== "RANGE CHECKS" ===

; These should ideally probably use the optimized type bytes that are for
; the quoted ranges, perhaps saving all the upper 64 states for special
; states and modes.  But review.

; antiform range check (core uses Is_Antiform() which just checks heart byte)
(
    e-typeset-bytes/emit ["any-antiform $<index>"]

    append typeset-flags cscape [t --[
        /* $<index> - any-antiform */
        TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE($<first-antiform-index>) | FLAG_FOURTH_BYTE($<index - 1>)
    ]--]
    index: me + 1
)

for-each 'tr typerange-objects [  ; range, typeset is a start and end
    e-typeset-bytes/emit [tr "any-$<tr.name> $<index>"]

    append typeset-flags cscape [tr --[
        /* $<index> - any-$<tr.name> */
        TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE($<TR.START>) | FLAG_FOURTH_BYTE($<TR.END>)
    ]--]
    index: index + 1
]

for-each [ts-name types] sparse-typesets [  ; sparse, typeset is a single flag
    e-typeset-bytes/emit [ts-name "any-$<ts-name> $<index>"]

    append typeset-flags cscape [ts-name --[
        /* $<index> - any-$<ts-name> */
        TYPESET_FLAG_${TS-NAME}
    ]--]
    index: index + 1
]


; Add ANY-PLAIN? to be anything that's not meta/tied/pinned/quoted/quasi.
(
    e-typeset-bytes/emit [ts-name "any-plain $<index>"]

    append typeset-flags cscape [tr --[
        /* $<index> - any-plain */
        TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE(0) | FLAG_FOURTH_BYTE(MAX_TYPE_HEART)
    ]--]
    index: index + 1
)


; Add ANY-FUNDAMENTAL? to go right up to the max heart byte (don't include
; quoted or quasi).  Include TYPE_0 for ExtraHeart types.
(
    e-typeset-bytes/emit ["any-fundamental $<index>"]

    append typeset-flags cscape [tr --[
        /* $<index> - any-fundamental */
        TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE(0) | FLAG_FOURTH_BYTE(MAX_TYPE_FUNDAMENTAL)
    ]--]
    index: index + 1
)


; Add ANY-ELEMENT? to the absolute end of the list, so it hooks last.S
(
    e-typeset-bytes/emit ["any-element $<index>"]

    append typeset-flags cscape [tr --[
        /* $<index> - any-element */
        TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE(0) | FLAG_FOURTH_BYTE(MAX_TYPE_ELEMENT)
    ]--]
    index: index + 1
)


e-typesets/emit [--[
    #include "sys-core.h"

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
    uint_fast32_t const g_sparse_memberships[MAX_TYPEBYTE + 1] = {
        /* 0 - <ExtraHeart> */  0,
        $(Sparse-Memberships),
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
     * (Since we know it's an 8 bit type, and the enum isn't used for storage,
     * we could use enum typing in C++ to say this is `uint_fast8_t`.  Should
     * we do that?  Trusting the compiler is the more common choice.)
     */
    #if (! DEBUG_EXTRA_HEART_CHECKS)
        typedef enum {
            TYPE_0_constexpr = 0,  /* prefer TYPE_0 to get Option(Type) */
            TYPE_$[Sigils],
            TYPE_$[Hearts],
            TYPE_$[Quasiform],
            TYPE_$[Quoteds],
            TYPE_$[Antiforms],
            LIFT_255 = 255
        } TypeEnum;

        #define HEART_0_constexpr  TYPE_0_constexpr
        $[HeartDefines]
    #else
        /*
         * The "Extra Heart Byte Checks" are designed to make sure you don't
         * pass Type where Heart is expected, or things like TYPE_QUASIFORM
         * or TYPE_SPLICE into the KIND_BYTE().
         *
         * Doing this with overlapping enums may not seem ideal, but trying
         * to do it with one enum and C++ tricks to filter it wind up costing
         * more at runtime--at least in debug builds, which don't inline
         * constexpr functions.  It's just not worth it for the check--this
         * is simpler, faster, and quite good enough.
         *
         * (This would be much easier if C++ defined enum inheritance.)
         *
         * 1. We get benefit from using a C++ `enum class` for the TypeEnum.
         *    But we have a wrapper class over it to facilitate HeartEnum
         *    being accepted where Types are tested, which lets us gloss
         *    over the lack of boolean convertibility needed by Option(Type).
         *    Heart doesn't have such a class, and we'd hate to make one
         *    just for that...and it really doesn't need the enum class.
         *
         * 2. While C++ enum classes don't require qualification, our names
         *    are things like VOID, and that's a macro in Windows headers.
         *    To reduce collisions we call the enum members ENUM_XXX.
         */

        enum HeartEnum {  /* no enum class, need zero conversion [1] */
            HEART_0_constexpr = 0,  /* prefer HEART_0 to get Option(Heart)! */

            /* placeholders for TYPE_METAFORM, TYPE_PINNED, TYPE_TIED */
            PLACEHOLDER_TYPE_$[Sigils],

            HEART_$(Hearts),
        };

        enum class TypeEnum {  /* ENUM_ prefix; bare words conflict [2] */
            ENUM_0_constexpr = 0,

            ENUM_$[Sigils],

            /* placeholders for values in range of heart byte */
            ENUM_$[Hearts],

            /* just one quasiform type! */
            ENUM_$[Quasiform],

            /* PSEUDOTYPE_QUOTED_<N>_TIMES_<NON>QUASI states */
            ENUM_$[Quoteds],

            ENUM_$[Antiforms],

            ENUM_255 = 255
        };

        #define TYPE_0_constexpr  TypeEnum::ENUM_0_constexpr
        $[TypeDefines]
        #define LIFT_255  TypeEnum::ENUM_255
    #endif

    STATIC_ASSERT(i_cast(int, $<MAX-TYPE>) <= 256);  /* Stored in bytes */

    STATIC_ASSERT(i_cast(Byte, TYPE_QUOTED_64_TIMES_QUASI) == 192);
    #define LIFT_192  TYPE_QUOTED_64_TIMES_QUASI

    #define MAX_TYPE_FUNDAMENTAL  TYPE_$<MAX-HEART>  /* same as max heart */
    #define MAX_TYPE_ELEMENT  TYPE_$<MAX-ELEMENT>

    STATIC_ASSERT(i_cast(Byte, TYPE_METAFORM) == 1);
    STATIC_ASSERT(i_cast(Byte, TYPE_PINNED) == 2);
    STATIC_ASSERT(i_cast(Byte, TYPE_TIED) == 3);

    #define MIN_TYPE_HEART  TYPE_$<MIN-HEART>
    #define MAX_TYPE_HEART  TYPE_$<MAX-HEART>
    STATIC_ASSERT(i_cast(Byte, MIN_TYPE_HEART) == 4);
    STATIC_ASSERT(i_cast(Byte, MAX_TYPE_HEART) < 64);

    STATIC_ASSERT(i_cast(Byte, TYPE_QUASIFORM) == 64);

    #define MIN_TYPE_ANTIFORM  TYPE_$<MIN-ANTIFORM>
    #define MAX_TYPE_STABLE  TYPE_$<MAX-STABLE>
    #define MAX_TYPE_ANTIFORM  TYPE_$<MAX-ANTIFORM>

    #define MAX_TYPEBYTE  i_cast(TypeByte, $<MAX-TYPE>)

    /*
     * ANTIFORM HEART ALIASES
     *
     * Helps avoid needing to annotate why you're using a strange heart value
     * to indicate an antiform in source.
     */
    $[Antiheart-Aliases]

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
