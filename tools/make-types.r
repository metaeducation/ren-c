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
][
    let obj: construct compose [(setify var) ~]  ; make variable
    body: overbind obj body  ; make variable visible to body
    var: has obj var

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
    return
]

; 1. Type ranges are inclusive, so the end is included in the range.  This is
;    done so that checks of a single type look more sensible when done with
;    a range (e.g. start and end are the same)
;
for-each-typerange: proc [
    "Iterate type table and create object for each <TYPE!>...</TYPE!> range"

    var "Word to set each time to the typerange as an object record"
        [word!]
    body "Block to evaluate each time"
        [block!]
][
    let obj: construct compose [(setify var) ~]  ; make variable
    body: overbind obj body  ; make variable visible to body
    var: has obj var

    let stack: copy []
    let types*: _  ; will be put in a block, can't be null

    let [name* any-name!* starting]

    let heart*: 4  ; 0 is reserved, 1-3 are Sigils
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
     * Modern Ren-C carves out special values of the LIFT_BYTE for unlifted
     * values--trading off some abilities to have higher quoting levels to
     * get a very fast answer to Type_Of().  However, it's still necessary
     * to collapse the many quoted states of the lift byte to a single one
     * to get TYPE_QUOTED.
     *
     * But if we're only interested in whether something has a fundamental
     * non-quoted type or not, we don't need to do that collapse...and can
     * just compare the LIFT_BYTE() directly, which Has_Type() does.
     *
     *     #define Is_Text(v)  Has_Type((v), TYPE_TEXT)
     */
]--]

for-each-datatype 't [
    if t.cellmask [
        e-types/emit [t --[
            #define CELL_MASK_${T.NAME} \
                (FLAG_HEART(TYPE_${T.NAME}) | FLAG_LIFT_BYTE(As_Lift(TYPE_${T.NAME})) | $<MOLD T.CELLMASK>)
        ]--]
    ]

    e-types/emit [propercase-of t --[
        #define Is_${propercase-of T.name}(v)  Has_Type((v), TYPE_$<T.NAME>)

        #define Is_Possibly_Unstable_Value_${propercase-of T.name}(v) \
            Possibly_Unstable_Has_Type((v), TYPE_$<T.NAME>)
    ]--]
]

sparse-typesets: copy []

for-each-typerange 'tr [  ; typeranges first (e.g. ANY-STRING? < ANY-UTF8?)
    let proper-name: propercase-of tr.name

    e-types/emit [tr --[
        INLINE bool Any_${Proper-Name}_Type(Option(Type) t)
          { return ii_cast(TypeByte, (t)) >= $<TR.START> and ii_cast(TypeByte, (t)) <= $<TR.END>; }

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
        #define Any_${propercase-of Ts-Name}_Type(t) \
            (logical (Sparse_Memberships(t) & TYPESET_FLAG_${TS-NAME}))

        #define Any_${propercase-of Ts-Name}(cell) \
            Any_${propercase-of Ts-Name}_Type(Type_Of(cell))
    ]--]
]

for-each-datatype 't [
    if not t.antiname [continue]  ; no special name for antiform form

    let want: either yes? t.unstable ["Possibly_Unstable"] ["Known_Stable"]

    let proper-name: propercase-of t.antiname

    ; Note: Readable_Cell() not defined yet at this point, so defined as
    ; a macro vs. an inline function.  Revisit.
    ;
    e-types/emit [t proper-name --[
        #define Is_$<Proper-Name>(v) \
            (LIFT_BYTE(Readable_Cell($<Want>(v))) == LIFTBYTE_$<T.ANTINAME>)

        #define Is_Lifted_$<Proper-Name>(v) \
            Cell_Has_Lift_Heart_No_Sigil(Known_Stable(v), \
                QUASIFORM_64, TYPE_$<T.NAME>)

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
            (LIFT_BYTE(Readable_Cell(Possibly_Unstable(v))) == LIFTBYTE_$<T.ANTINAME>)
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

plains: make block! 64  ; "TYPE_XXX = (num)" for all KIND_BYTE
pseudotype-hearts: make block! 128  ; "PSEUDO_XXX = (num)" for all KIND_BYTE
pseudotypes: make block! 128  ; "TYPE_XXX = (num)" for all pseudotypes
types: make block! 128
typedefines: make block! 128

singlehearts: make block! 256  ; tricky thing, see description in comments

typeset-flags: copy []  ; from ranges in %types.r, <ANY-XXX?>...</ANY-XXX?>
sparse-memberships: copy []  ; non-ranged in %types.r, [any-unit? any-inert?]

index: 1

do-appends: proc [
    name [word! text! integer!]
    description [<opt> text!]
    sparse [block!]  ; possibly empty
    :ranged [block!]
    :heart
][
    description: default [
        assert [integer? name]
        <- "reserved for future use"
    ]

    ranged: default [
        reduce [
            "TYPESET_FLAG_0_RANGE"
            unspaced ["FLAG_THIRD_BYTE(" index ")"]
            unspaced ["FLAG_FOURTH_BYTE(" index ")"]
        ]
    ]

    append types cscape [name  ; for the non-distinguishing heart/type builds
        --[TYPE_${NAME} = $<index>]--
    ]

    if heart [  ; still needs slot in TYPE enum if heart
        append pseudotype-hearts cscape [name
            --[ENUM_PSEUDO_${NAME} = $<index>]--
        ]

        append typedefines cscape [name
            --[#define TYPE_${NAME}  HEART_ENUM(${NAME})]--
        ]
    ]
    else [
        append pseudotype-hearts cscape [name
            --[ENUM_TYPE_${NAME} = $<index>]--
        ]

        append typedefines cscape [name
            --[#define TYPE_${NAME}  TYPE_ENUM(${NAME})]--
        ]
    ]

    append singlehearts cscape [name
        --[SINGLEHEART_TAIL_SPACE_${NAME} = $<index * 256>]--
    ]
    append singlehearts cscape [name
        --[SINGLEHEART_HEAD_SPACE_${NAME} = $<(index * 256) + 1>]--
    ]

    e-typeset-bytes/emit [name -[
        $<name> $<index>
    ]-]

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
; KIND_BYTE() so are derived types when present in the LIFT_BYTE.

for-each [name description] [
    metaform "marker to read unlifted and write lifted representations"
    pinned "mark to bind in the evaluator in current context, and keep mark"
    tied "mark to bind in the evaluator in current context, and drop mark"
][
    do-appends name description ["TYPESET_FLAG_BRANCH"]
]

=== "HEART TYPES" ===

; "Heart" types refer to the actual pattern of layout of a Cell.

min-heart: ~
max-heart: ~

for-each-datatype 't [  ; plains
    append plains cscape [t
        --[ENUM_TYPE_${T.NAME} = $<index>]--
    ]

    min-heart: default [cscape [t.name --[TYPE_${T.NAME}]--]]
    max-heart: cscape [t.name --[TYPE_${T.NAME}]--]  ; overwritten until max

    let sparse: collect [
        for-each [ts-name types] sparse-typesets [
            if not find types t.name [continue]

            keep cscape [ts-name "TYPESET_FLAG_${TS-NAME}"]
        ]
    ]

    do-appends:heart t.name t.description sparse
]

=== "FILL SPACE UNTIL QUASIFORM (64)" ===

; These are fundamental types that are reserved for future use.  (Modern Ren-C
; gets away with fewer types, because things like SET-WORD! aren't fundamental
; because that's a CHAIN! containing a WORD!, and also extension types are
; available so long as you're willing to give up a pointer per cell for it.)

assert [index <= 64]  ; need 64 to be quasiform

while [index < 64] [
    do-appends // [
        unspaced ["reserved_" index]
        "<reserved>"
        sparse: []
    ]
]

do-appends // [
    'quasiform
    "value which evaluates to an antiform"
    ["TYPESET_FLAG_BRANCH"]
]

=== "CREATE THE QUOTING PSEUDOTYPES (UP THRU 192)" ===

; Up to 66 levels of quoting are implemented by reserving 65-192 in the
; type byte (half the states encode that the quoted thing is a quasiform too).
;
; If we stopped at 195 leaving 64 full states above, that would mean there'd
; be a state for "quoted 66 levels and quasi" but no state for "quoted 66
; levels and nonquasi".  So we round up, leaving 63 states for antiforms,
; bedrock states, and whatever else.  (This could be compromised to have
; more or fewer quote states; 64 might be a better choice just because it's
; likely easier to emulate in other models, review the decision.)

do-appends // [
    "quoted"
    "container for up to 64 levels of quoting"
    ["TYPESET_FLAG_BRANCH"]
]

num-quotes: 1
is-quasi: okay

while [index <= 192] [
    do-appends // [
        unspaced [
            "quoted_" num-quotes "_time" (if num-quotes > 1 ["s"])
                "_" if not is-quasi ["non"] "quasi"
        ]
        "<internal>"
        ["TYPESET_FLAG_BRANCH"]
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
    name [<opt> text! word!]
    antiname [<opt> text! word!]
    antidescription [<opt> text!]
][
    if antiname [
        append antiheart-aliases cscape [name antiname
            --[#define HEART_${NAME}_SIGNIFYING_${ANTINAME}  HEART_ENUM(${NAME})]--
        ]

        append sparse-memberships cscape [antiname
            --[/* $<index> - $<antiname> */  (0)]--
        ]

        append pseudotypes cscape [antiname
            --[ENUM_TYPE_${ANTINAME} = $<index>]--
        ]

        append types cscape [antiname
            --[TYPE_${ANTINAME} = $<index>]--
        ]
        max-type: cscape [antiname
            --[TYPE_${ANTINAME}]--  ; overwritten until max found
        ]

        append typedefines cscape [antiname
            --[#define TYPE_${ANTINAME}  TYPE_ENUM(${ANTINAME})]--
        ]

        e-typeset-bytes/emit [antiname -[
            ${antiname} $<index>
        ]-]

        e-typespecs/emit [antiname antidescription -[
            $<antiname> $<mold antidescription>
        ]-]

        append typeset-flags cscape [antiname --[
            /* $<index> - ${antiname} */
            TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE($<index>) | FLAG_FOURTH_BYTE($<index>)
        ]--]
    ] else [
        append sparse-memberships cscape [
            --[/* $<index> */  (0)]--
        ]

        append pseudotypes cscape [ --[ENUM_TYPE_$<index> = $<index>]--]

        append typedefines cscape [
            --[/* no #define for TYPE_$<index> */]--
        ]

        append types cscape [ --[TYPE_$<index> = $<index>]--]
        max-type: cscape [ --[TYPE_$<index>]--]

        ; don't need a #define for these

        e-typeset-bytes/emit [ -[
            ~ $<index>
        ]-]

        e-typespecs/emit [ -[
            ~ ~
        ]-]

        append typeset-flags cscape [ --[
            /* $<index> - <unused> */  0
        ]--]
    ]
    index: me + 1
]

for-each sigil [metaform pinned tied] [  ; space out by 3 ?
    do-anti-appends () () ()
]

for-each-datatype 't [  ; now generate bytes for antiforms
    do-anti-appends (opt t.name) (opt t.antiname) (opt t.antidescription)
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
        TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE(0) | FLAG_FOURTH_BYTE(MAX_HEARTBYTE)
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
        TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE(0) | FLAG_FOURTH_BYTE(MAX_TYPEBYTE_FUNDAMENTAL)
    ]--]
    index: index + 1
)


; Add ANY-ELEMENT? to the absolute end of the list, so it hooks last.  Include
; TYPE_QUOTED_X_TIMES_Y and TYPE_QUASIFORM, and TYPE_0 for ExtraHeart
; extension types.
(
    e-typeset-bytes/emit [ts-name -[
        any-element $<index>
    ]-]

    append typeset-flags cscape [tr --[
        /* $<index> - any-element */
        TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE(0) | FLAG_FOURTH_BYTE(MAX_TYPEBYTE_ELEMENT)
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

    #define MAX_TYPEBYTE_FUNDAMENTAL  63  /* should be UNLIFTED, more clear */
    #define MAX_TYPEBYTE_ELEMENT  192

    STATIC_ASSERT(i_cast(int, $<MAX-TYPE>) <= 256);  /* Stored in bytes */

    STATIC_ASSERT(i_cast(Byte, TYPE_METAFORM) == 1);
    STATIC_ASSERT(i_cast(Byte, TYPE_PINNED) == 2);
    STATIC_ASSERT(i_cast(Byte, TYPE_TIED) == 3);

    #define MIN_HEARTBYTE  i_cast(Byte, $<MIN-HEART>)
    #define MAX_HEARTBYTE  i_cast(Byte, $<MAX-HEART>)
    STATIC_ASSERT(MIN_HEARTBYTE == 4);
    STATIC_ASSERT(MAX_HEARTBYTE <= MAX_TYPEBYTE_FUNDAMENTAL);

    STATIC_ASSERT(i_cast(Byte, TYPE_QUASIFORM) == 64);

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
