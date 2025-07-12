Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "Rebol datatypes and their related attributes"

    rights: --[
        Copyright 2012 REBOL Technologies
        Copyright 2012-2025 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--

    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--

    description: --[
        This table is used to make C defines and intialization tables for
        the "Heart" enumeration, which tags a Cell with how its contents
        (Payload, Extra, and Flags) will be interpreted.

        Most code should avoid dependence on exact values of the Heart bytes,
        and this should never be exposed via the user-visible API.  The
        values are chosen and tweaked in order to make certain tests fast.
        Where possible, those orderings are captured in this table by tag
        ranges like <TYPERANGE-BEGIN?> ... </TYPERANGE-END?>.

        Any relative ordering dependencies not captured in this type table
        should be captured as macros/functions in %enum-typesets.h.  As an
        example, see Is_Cell_Bindable().
    ]--

    usage: --[
      Entries look like:

            name!           "description"
            ~antiform!~     "antinotes"    ; ~antiform!~:U means unstable
                            (node flags)
                            [constraints]  ; makes `g_sparse_memberships`

        name        - name of datatype (generates words)
        description - short statement of type's purpose (used by HELP)
        antiform    - (optional) the name of the antiform of the type
        antinotes   - (optional) if antiform, statement of antiform purpose
        mark flags  - indication of if cell payload slot 1 or 2 mark Base*
        constraints - sparse type constraints this type will answer true to
    ]--

    notes: --[
      * ANY-SCALAR? is weird at this point, because TUPLE? may or may not be
        fully numeric (1.2.3 vs alpha.beta.gamma).  What the this was for
        was defining input types things like ADD would accept, and now that's
        kind of gotten fuzzy.

      * Historical Redbol has TYPESET! as a datatype which is a bitset with
        a bit for each of 64 datatypes, that fits in a cell payload.  This
        approach was too limited for Ren-C, so it uses type constraint
        functions.  But bitsets are still employed in the implementation of
        built-in type constraints, and the term "typeset" now refers to
        these implementation bits.

      * Historical Rebol had INTERNAL! and IMMEDIATE! "typesets".  Important
        to have a parallel in Ren-C type constraints?
    ]--
]


integer!    "64 bit integer"
            (CELL_MASK_NO_MARKING)  ; would change with bignum ints
            [any-number? any-scalar? any-inert? any-sequencable?]

<ANY-FLOAT?>

    decimal!    "64bit floating point number (IEEE standard)"
                (CELL_MASK_NO_MARKING)
                [any-number? any-scalar? any-inert?]

    percent!    "special form of decimals (used mainly for layout)"
                (CELL_MASK_NO_MARKING)
                [any-number? any-scalar? any-inert?]

</ANY-FLOAT?>

pair!       "two dimensional point or size"
            (payload1)
            [any-scalar? any-inert?]

time!       "time of day or duration"
            (CELL_MASK_NO_MARKING)
            [any-scalar? any-inert?]

date!       "day, month, year, time of day, and timezone"
            (CELL_MASK_NO_MARKING)
            [any-inert?]

bitset!     "set of bit flags"
            (payload1)
            [any-inert?]

map!        "name-value pairs (hash associative)"
            (payload1)
            [any-inert?]

handle!     "arbitrary internal object or value"
            (:payload1)  ; managed handles use a Stub to get a shared instance
            [any-inert?]

opaque!     "extension cell which uses its type information slot for data"
            (:payload1 :payload2)
            [any-inert?]

blob!       "series of bytes"
            (payload1)
            [any-series? any-inert?]  ; note: not an ANY-STRING?

<ANY-STRING?>  ; (order does not currently matter)

    text!       "text string series of characters"
                (payload1)
                [any-series? any-utf8? any-inert? any-sequencable?]

    file!       "file name or path"
                (payload1)
                [any-series? any-utf8? any-inert?]

    tag!        "markup string (HTML or XML)"
                (payload1)
                [any-series? any-utf8? any-inert? any-sequencable?]

</ANY-STRING?>

url!        "uniform resource locator or identifier"
            (:payload1)  ; may or may not embed data in url vs. use node
            [any-utf8? any-inert?]

email!      "email address"
            (:payload1)  ; may or may not embed data in email vs. use node
            [any-utf8? any-inert?]

rune!       "immutable codepoint or codepoint sequence"
~trash!~    "state held by unset variables, can't be passed as normal argument"
            (:payload1)  ; may or may not embed data in rune vs. use node
            [any-utf8? any-inert? any-sequencable?]

money!      "digits and decimal points as a string, preserved precisely"
            (CELL_MASK_NO_MARKING)
            [any-utf8? any-inert? any-sequencable?]


; ============================================================================
; ABOVE THIS LINE, Don't Mark "Extra": (not Heart_Implies_Extra_Needs_Mark())
; ============================================================================

; With CELL_FLAG_DONT_MARK_PAYLOAD_1 and CELL_FLAG_DONT_MARK_PAYLOAD_2, each
; Cell can say whether the GC needs to consider marking the first or second
; slots in the payload.  But rather than sacrifice another bit for whether the
; EXTRA slot needs marking, the types are in order so that all the ones that
; need to mark Extra come after this point.

; ============================================================================
; BELOW THIS LINE, Mark "Extra": Heart_Implies_Extra_Needs_Mark() is true
; ============================================================================


varargs!    "evaluator position for variable numbers of arguments"
            (payload2)
            [any-inert?]

parameter!  "function parameter description"
            (payload1 :payload2)
            [any-inert?]


<ANY-CONTEXT?>

    object!     "context of names with values"
                (payload1 payload2)
                [any-inert?]

    module!     "loadable context of code and data"
                (payload1)
                [any-inert?]

    warning!    "context with id, arguments, and stack origin"
    ~error!~:U  "error state that is escalated to a panic if not triaged"
                (payload1 payload2)
                [any-inert?]

    port!       "external series, an I/O channel"
                (payload1 payload2)
                [any-inert?]

    frame!      "arguments and locals of a function state"
    ~action!~   "will trigger function execution from words"
                (:payload1)
                [any-branch?]

    let!        "context containing a single variable"
                (payload1)
                [any-inert?]

</ANY-CONTEXT?>


; ============================================================================
; BELOW THIS LINE ALL TYPES USE "Extra" FOR BINDING: SEE Is_Bindable_Heart()
; ============================================================================


; COMMA! is weirdly bindable, due to its application in variadic feeds.
; It's an implementation detail which would require inventing another
; datatype that was FEED-specific.  Better ideas welcome.

comma!        "separator between full evaluations"
~ghost!~:U    "elision state that is discarded by the evaluator"
              (CELL_MASK_NO_MARKING)
              [any-unit?]  ; NOT inert


<ANY-SEQUENCE?>

    tuple!      "member selection with inert bias"
                (:payload1)
                [any-scalar?]  ; !!! 1.2.3 maybe, but not all are scalars...

    chain!      "refinement and function call dialect"
                (:payload1)
                []

    path!       "member or refinement selection with execution bias"
                (:payload1)
                []

</ANY-SEQUENCE?>


<ANY-LIST?>

    block!      "list of elements that blocks evaluation unless EVAL is used"
    ~pack!~:U   "multi-return that can be unpacked or decays to first item"
                (payload1)
                [any-series? any-branch? any-sequencable?]

    fence!      "list of elements that are used in construction via MAKE"
    ~datatype!~ "the type of a value expressed as an antiform"
                (payload1)
                [any-series? any-branch? any-sequencable?]

    group!      "list that evaluates expressions as an isolated group"
    ~splice!~   "fragment of multiple values without a surrounding block"
                (payload1)
                [any-series? any-sequencable?]

</ANY-LIST?>


word!       "evaluates a variable or action"
~keyword!~  "special constant values (e.g. ~null~, ~okay~)"
            (payload1)
            [any-utf8? any-sequencable?]



; ======= END "FUNDAMENTALS" THAT AREN'T QUOTED, QUASI, OR ANTIFORM ==========

; Note: We don't use <ANY-FUNDAMENTAL></ANY-FUNDAMENTAL> in this file because
; fundamentals must include TYPE_0 for all extension types.  So that typerange
; is made explicitly in %make-types.r

; ============================================================================
; ABOVE THESE ARE QUOTED, QUASIFORM, and ANTIFORM "PSEUDOTYPES"
; ============================================================================

; The TYPE_QUOTED, TYPE_QUASIFORM, and all the antiform types (TYPE_SPLICE,
; TYPE_TRASH, etc.) enum values never appear in the KIND_BYTE() of a cell.
; These are synthesized datatypes when the LIFT_BYTE() contains values other
; than one (NOQUOTE_2).
;
; They're not inert... QUASIFORM? becomes ANTIFORM? when evaluated, and QUOTED?
; removes one level of quoting.  (ANTIFORM? should never be seen by the
; evaluator, only produced by it.)
;
; NOTE: These were once in the %types.r table, due to that driving the
; creation of help strings and usermode functions like QUOTED? and QUASIFORM?.
; However the sheer number of edge cases involved made it better to clean
; up the %make-types.r process to do the pseudotype generation itself.
;
; !!! Could ANY-STABLE? be generated in a more automatic fashion, now that
; a ^META parameter is not required to receive TRASH! ?
