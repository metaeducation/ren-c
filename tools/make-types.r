REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Calculate type and typeset constants"
    File: %make-types.r  ; used by EMIT-HEADER to indicate emitting script
    Rights: --{
        Copyright 2012 REBOL Technologies
        Copyright 2012-2025 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }--
    License: --{
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }--
    Version: 2.100.0
    Needs: 2.100.100
    Purpose: --{
       The %types.r table defines many of the properties of Rebol's
       datatypes in a declarative way.  It is parsed and used to make
       tables used by %make-boot.r, as well as C source files that are
       included by the build.
    }--
]


if not find (words of import/) 'into [  ; See %import-shim.r
    do <import-shim.r>
]

print "--- Make Types ---"

import <bootstrap-shim.r>

import <common.r>
import <common-emitter.r>

change-dir join repo-dir %src/boot/


=== "SETUP PATHS AND MAKE DIRECTORIES (IF NEEDED)" ===

prep-dir: join system.options.path %prep/

mkdir:deep join prep-dir %include/
mkdir:deep join prep-dir %boot/
mkdir:deep join prep-dir %core/


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
    completed* running* is-unstable* decorated pos
    obj
][
    obj: construct compose1 [(setify var) ~]  ; make variable
    body: overbind obj body  ; make variable visible to body
    var: has obj var

    heart*: 1  ; 0 is reserved
    parse3:match type-table [some [
        [opt some tag! <end> accept (okay)]
        |
        opt some tag!  ; <TYPE!> or </TYPE!> used by FOR-EACH-TYPERANGE
        name*: word!
        description*: text!
        [antiname*: quasiform! | (antiname*: null)]  ; quasiform is word in boot
        [
            ahead group! into [
                ['CELL_MASK_NO_NODES <end>]
                    (cellmask*: the (CELL_MASK_NO_NODES))
                | ['node1 <end>]
                    (cellmask*: the (CELL_FLAG_DONT_MARK_NODE2))
                | [the :node1 <end>]
                    (cellmask*: null)  ; don't define a CELL_MASK_XXX
                | [the :node1 the :node2]
                    (cellmask*: null)  ; don't define a CELL_MASK_XXX
                | ['node1 'node2]
                    (cellmask*: the (0))
                | ['node2 <end>]
                    (cellmask*: the (CELL_FLAG_DONT_MARK_NODE1))
            ]
            | pos: <here> (
                fail ["Bad node1/node2 spec for" name* "in %types.r"]
            )
        ]
        [is-unstable*: issue! | (is-unstable*: null)]
        [typesets*: block!]
        (
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
                antiname: either antiname* [to text! unquasi antiname*] [null]
                unstable: switch is-unstable* [
                    null ['no]
                    #unstable ['yes]
                    fail "unstable annotation must be #unstable"
                ]
            ]
            repeat 1 body else [return null]  ; give body BREAK/CONTINUE
        )
        (heart*: heart* + 1)
    ]] else [
        fail "Couldn't fully parse %types.r"
    ]
]

; 1. Type ranges are inclusive, so the end is included in the range.  This is
;    done so that checks of a single type look more sensible when done with
;    a range (e.g. start and end are the same)
;
/for-each-typerange: func [
    "Iterate type table and create object for each <TYPE!>...</TYPE!> range"

    var "Word to set each time to the typerange as an object record"
        [word!]
    body "Block to evaluate each time"
        [block!]
    <local> name* heart* any-name!* stack types* starting
    obj
][
    obj: construct compose1 [(setify var) ~]  ; make variable
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
                        end: heart* - 1  ; compare end using <=, not <, see [1]
                        types: types*
                    ]
                    types*: _
                    eval body  ; no support for BREAK/CONTINUE in bootstrap
                ]
            )]
            [<end> | [
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
                (heart*: heart* + 1)
            ]]
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
        any [
            t.name = "quasiform"
            t.name = "quoted"
            t.name = "antiform"
        ] else [
            keep cscape [t --{REB_${T.NAME} = $<T.HEART>}--]
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
            keep cscape [t --{KIND_${T.NAME} = $<T.HEART>}--]
        ]
    ]
]

singlehearts: collect [  ; !!! Omit invalid singleheart values?
    for-each-datatype 't [
        keep cscape [t
            --{SINGLEHEART_TAIL_BLANK_${T.NAME} = $<T.HEART * 256>}--
        ]
        keep cscape [t
            --{SINGLEHEART_HEAD_BLANK_${T.NAME} = $<(T.HEART * 256) + 1>}--
        ]
    ]
]

e-hearts/emit [rebs --{
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
    #if NO_RUNTIME_CHECKS || NO_CPLUSPLUS_11  || defined(__clang__)
        enum HeartKindEnum {
            REB_0 = 0,  /* reserved */
            $[Rebs],
            REB_MAX_HEART,
            REB_QUASIFORM = REB_MAX_HEART,
            REB_QUOTED,
            REB_ANTIFORM,
            REB_MAX
        };
    #else
        enum HeartEnum {
            REB_0 = 0,  /* reserved falsey case for Option(Heart) */
            $[Rebs],
            REB_MAX_HEART,  /* one past valid types */
        };

        enum KindEnum {
            KIND_0 = 0,  /* reserved falsey case for Option(Kind) */
            $[Kinds],
            REB_QUASIFORM,
            REB_QUOTED,
            REB_ANTIFORM,
            REB_MAX
        };
    #endif

    STATIC_ASSERT(u_cast(int, REB_QUASIFORM) == u_cast(int, REB_MAX_HEART));
    STATIC_ASSERT(REB_MAX < 256);  /* Stored in bytes */

    /*
     * SINGLEHEART OPTIMIZED SEQUENCE DETECTION
     *
     * We want to use SingleHeart in switch() statements, but don't want them
     * to be type-compatible with Heart or Kind types due to the extra flag of
     * information they multiplex in.  Making a specialized enum type is
     * kind of the only way to get that type checking, since implicit casts
     * to integer to facilitate switching would let you use Heart or Kind.
     */
    enum SingleHeartEnum {
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
         * (e.g. there is no such thing as SINGLEHEART_TRAILING_BLANK_BLANK)
         *
         * It's just easier to make this enum and have the math work out by
         * filling it with all the combinatorics...
         */

        $(Singlehearts),
    };
}--]
e-hearts/emit newline

e-hearts/write-emitted


=== "MACROS LIKE Is_Block(), OTHER DATATYPE DEFINITIONS" ===

e-types: make-emitter "Datatype Definitions" (
    join prep-dir %include/tmp-typesets.h
)

e-types/emit [--{
    /* Tables generated from %types.r for builtin typesets */
    extern TypesetFlags const g_typesets[];  // up to 255 allowed
    extern uint_fast32_t const g_sparse_memberships[];
}--]
e-types/emit newline

e-types/emit --{
    /*
     * SINGLE TYPE CHECK MACROS, e.g. Is_Block() or Is_Tag()
     *
     * Originally these macros looked like:
     *
     *     #define Is_Text(cell) \
     *         (VAL_TYPE(cell) == REB_TEXT)
     *
     * So you'd calculate REB_QUOTED, REB_QUASI, or REB_ANTIFORM from the
     * QUOTE_BYTE(), and those would be filtered out and not match.
     *
     * This was changed to instead mask out the heart byte and quote byte
     * from the header, and compare to the precise mask of NOQUOTE_1 with
     * the specific heart byte:
     *
     *     #define Is_Text(cell) \
     *         ((Ensure_Readable(cell)->header.bits & CELL_HEART_QUOTE_MASK) \
     *           == (FLAG_HEART_BYTE(REB_TEXT) | FLAG_QUOTE_BYTE(NOQUOTE_1)))
     *
     * This avoids the branching in VAL_TYPE(), so it's a slight bit faster.
     *
     * Note that Ensure_Readable() is a no-op in the release build.
     */

    #define CELL_HEART_QUOTE_MASK \
        (FLAG_HEART_BYTE(255) | FLAG_QUOTE_BYTE(255))
}--
e-types/emit newline

for-each-datatype 't [
    ;
    ; Pseudotypes don't make macros or cell masks.
    ;
    if find ["quoted" "quasiform" "antiform"] ensure text! t.name  [
        continue
    ]

    if t.cellmask [
        e-types/emit [t --{
            #define CELL_MASK_${T.NAME} \
                (FLAG_HEART_BYTE(REB_${T.NAME}) | $<MOLD T.CELLMASK>)
        }--]
        e-types/emit newline
    ]

    e-types/emit [propercase-of t --{
        #define Is_${propercase-of T.name}(cell)  /* $<T.HEART> */ \
            ((Ensure_Readable(cell)->header.bits & CELL_HEART_QUOTE_MASK) \
              == (FLAG_HEART_BYTE(REB_${T.NAME}) | FLAG_QUOTE_BYTE(NOQUOTE_1)))
    }--]
    e-types/emit newline
]

sparse-typesets: copy []

for-each-typerange 'tr [  ; typeranges first (e.g. ANY-STRING? < ANY-UTF8?)
    let proper-name: propercase-of tr.name

    e-types/emit newline
    e-types/emit [tr --{
        INLINE bool Any_${Proper-Name}_Kind(Byte k)
          { return k >= $<TR.START> and k <= $<TR.END>; }

        #define Any_${Proper-Name}(v) \
            Any_${Proper-Name}_Kind(VAL_TYPE(v))
    }--]
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

        e-types/emit newline
        e-types/emit [propercase-of ts-name --{
            #define Any_${propercase-of Ts-Name}_Kind(k) \
               (did (g_sparse_memberships[k] & TYPESET_FLAG_${TS-NAME}))

            #define Any_${propercase-of Ts-Name}(v) \
                Any_${propercase-of Ts-Name}_Kind(VAL_TYPE(v))
        }--]
    ]
]


=== "GENERATE TYPESET_FLAG_XXX" ===

; Non-range typesets are handled by checking a flag in a static array which
; for each kind has a bitset of typeset flags for each set the kind is in.

e-types/emit -{
    /* Furthest left bit is used to convey when a typeset table entry is based
     * on a range of heart bytes as opposed to being a typeset bit.
     */
    #define TYPESET_FLAG_0_RANGE  FLAG_LEFT_BIT(0)
}-

shift-by: 1  ; start at 1, since FLAG_LEFT_BIT(0) indicates a ranged typeset

for-each [ts-name types] sparse-typesets [
    e-types/emit [ts-name --{
        #define TYPESET_FLAG_${TS-NAME}  FLAG_LEFT_BIT($<shift-by>)
    }--]
    shift-by: me + 1

    if shift-by > 32 [
        fail [
            "Current design only allows for 31 sparse typesets." newline
            "64-bit integers would need to be used for more."
        ]
    ]
]

e-types/emit newline


for-each-datatype 't [
    if not t.antiname [continue]  ; no special name for antiform form

    let need: either yes? t.unstable ["Atom"] ["Value"]

    let proper-name: propercase-of t.antiname

    ; Note: Ensure_Readable() not defined yet at this point, so defined as
    ; a macro vs. an inline function.  Revisit.
    ;
    e-types/emit [t proper-name --{
        INLINE bool Is_$<Proper-Name>_Core(Need(const $<Need>*) v) { \
            return ((v->header.bits & (FLAG_QUOTE_BYTE(255) | FLAG_HEART_BYTE(255))) \
                == (FLAG_QUOTE_BYTE_ANTIFORM_0 | FLAG_HEART_BYTE(REB_$<T.NAME>))); \
        }

        #define Is_$<Proper-Name>(v) \
            Is_$<Proper-Name>_Core(Ensure_Readable(v))

        #define Is_Meta_Of_$<Proper-Name>(v) \
        ((Ensure_Readable(v)->header.bits & (FLAG_QUOTE_BYTE(255) | FLAG_HEART_BYTE(255))) \
            == (FLAG_QUOTE_BYTE_QUASIFORM_2 | FLAG_HEART_BYTE(REB_$<T.NAME>)))

        #define Is_Quasi_$<Propercase-Of T.Name>(v) \
            Is_Meta_Of_$<Proper-Name>(v)  /* alternative */
    }--]
    e-types/emit newline
]

e-types/write-emitted


=== "SYMBOL-TO-TYPESET-BITS MAPPING TABLE" ===

; The typesets for things like ANY-BLOCK? etc. are specified in the %types.r
; table, and turned into 64-bit bitsets.

e-typesets: make-emitter "Built-in Typesets" (
    join prep-dir %core/tmp-typesets.c
)


e-typesets/emit --{
    #include "sys-core.h"
}--
e-typesets/emit newline

memberships: copy []
typeset-flags: copy []

index: 1

for-each-datatype 't [
    let proper-name: propercase-of t.name

    append typeset-flags cscape [t
        --{/* $<index> - $<t.name> */  TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE($<index>) | FLAG_FOURTH_BYTE($<index>)}--
    ]

    let flagits: collect [
        for-each [ts-name types] sparse-typesets [
            if not find types t.name [continue]

            keep cscape [ts-name "TYPESET_FLAG_${TS-NAME}"]
        ]
    ]
    if empty? flagits [
        append memberships cscape [t -{/* $<index> - $<t.name> */  0}-]
    ] else [
        append memberships cscape [flagits t
            --{/* $<index> - $<t.name> */  ($<Delimit " | " Flagits>)}--
        ]
    ]

    index: me + 1
]

for-each-typerange 'tr [  ; range, typeset is a start and end
    append typeset-flags cscape [tr
        --{/* $<index> - any-$<tr.name> */  TYPESET_FLAG_0_RANGE | FLAG_THIRD_BYTE($<TR.START>) | FLAG_FOURTH_BYTE($<TR.END>)}--
    ]
    index: index + 1
]

for-each [ts-name types] sparse-typesets [  ; sparse, typeset is a single flag
    append typeset-flags cscape [ts-name
        --{/* $<index> - any-$<ts-name> */  TYPESET_FLAG_${TS-NAME}}--
    ]
    index: index + 1
]

e-typesets/emit [--{
    /*
     * Builtin "typesets" use either ranges or sparse bits to answer whether
     * a type matches a typeset.  If the top bit TYPESET_FLAG_0_RANGE is not
     * set, then the entry holds a single typeset flag which can be tested
     * for in that datatype's g_sparse_memberships[] entry.  But if the top
     * bit is set, then the bottom two bytes represent a range of heart
     * bytes that you test to see if the Kind byte is between.
     */
    TypesetFlags const g_typesets[] = {
        /* 0 - <reserved> */  0,
        $(Typeset-Flags),
    };

    /*
     * For each fundamental datatype, this is the OR'd together flags of all
     * the sparse typesets that datatype is a member of.  There can be up
     * to 31 of those TYPESET_FLAG_XXX flags in this model (avoids dependency
     * on 64-bit integers, which we are attempting to excise from the system).
     */
    uint_fast32_t const g_sparse_memberships[REB_MAX] = {
        /* 0 - <reserved> */  0,
        $(Memberships),
    };
}--]

e-typesets/write-emitted


=== "WRITE TYPESET MAPPING FOR GENERICS TO USE" ===

; The generic table needs to know the integer values of types and typesets, so
; that if you say IMPLEMENT_GENERIC(append, any-list) it knows the TypesetByte
; ANY-LIST? corresponds to, and can put it after any more specific generic
; handlers.  We just write the typeset indices out to a file.

e-typeset-bytes: make-emitter "Typeset Byte Mapping" (
    join prep-dir %boot/tmp-typeset-bytes.r
)

typeset-byte: 1

for-each-datatype 't [
    e-typeset-bytes/emit [t -{
        $<t.name> $<typeset-byte>
    }-]
    typeset-byte: me + 1
]

for-each-typerange 'tr [
    e-typeset-bytes/emit [tr -{
        any-$<tr.name> $<typeset-byte>
    }-]
    typeset-byte: me + 1
]

for-each [ts-name types] sparse-typesets [
    e-typeset-bytes/emit [ts-name -{
        any-$<ts-name> $<typeset-byte>
    }-]
    typeset-byte: me + 1
]

e-typeset-bytes/write-emitted


=== "WRITE TYPESPECS TABLE (USED BY HELP)" ===

; When you say `help integer!` there is a table of help strings which are
; generated from %types.r

e-typespecs: make-emitter "Type Specs Mapping" (
    join prep-dir %boot/tmp-typespecs.r
)

for-each-datatype 't [
    e-typespecs/emit [t -{
        $<t.name> $<mold t.description>
    }-]
]

e-typespecs/write-emitted
