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
    purpose: --[
        This table is used to make C defines and intialization tables.

            name            "description"
            ~antiform~      "antinotes"    ; ~antiform~:U means unstable
                            (node flags)
                            [constraints]  ; makes `g_sparse_memberships`

        name        - name of datatype (generates words)
        description - short statement of type's purpose (used by HELP)
        antiform    - (optional) the name of the antiform of the type
        antinotes   - (optional) if antiform, statement of antiform purpose
        node flags  - indication of if cell payload slot 1 or 2 hold nodes
        constraints - sparse type constraints this type will answer true to
    ]--
    notes: --[
      * Code should avoid dependence on exact values of the heart bytes.
        Any relative ordering dependencies not captured in this type table
        should be captured as macros/functions in %enum-typesets.h

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


blank       "placeholder unit type"
~trash~     "state held by unset variables, can't be passed as normal argument"
            (CELL_MASK_NO_NODES)
            [any-unit? any-inert?]  ; allow as `branch`?

integer     "64 bit integer"
            (CELL_MASK_NO_NODES)  ; would change with bignum ints
            [any-number? any-scalar? any-inert? any-sequencable?]

<ANY-FLOAT?>

    decimal     "64bit floating point number (IEEE standard)"
                (CELL_MASK_NO_NODES)
                [any-number? any-scalar? any-inert?]

    percent     "special form of decimals (used mainly for layout)"
                (CELL_MASK_NO_NODES)
                [any-number? any-scalar? any-inert?]

</ANY-FLOAT?>

pair        "two dimensional point or size"
            (node1)
            [any-scalar? any-inert?]

time        "time of day or duration"
            (CELL_MASK_NO_NODES)
            [any-scalar? any-inert?]

date        "day, month, year, time of day, and timezone"
            (CELL_MASK_NO_NODES)
            [any-inert?]

parameter   "function parameter description"
            (node1 node2)
            [any-inert?]

bitset      "set of bit flags"
            (node1)
            [any-inert?]

map         "name-value pairs (hash associative)"
            (node1)
            [any-inert?]

handle      "arbitrary internal object or value"
            (:node1)  ; managed handles use a node to get a shared instance
            [any-inert?]

blob        "series of bytes"
            (node1)
            [any-series? any-inert?]  ; note: not an ANY-STRING?

<ANY-STRING?>  ; (order does not currently matter)

    text        "text string series of characters"
                (node1)
                [any-series? any-utf8? any-inert? any-sequencable?]

    file        "file name or path"
                (node1)
                [any-series? any-utf8? any-inert?]

    tag         "markup string (HTML or XML)"
    ~tripwire~  "unset variable state with informative message"
                (node1)
                [any-series? any-utf8? any-inert? any-sequencable?]

</ANY-STRING?>

url         "uniform resource locator or identifier"
            (:node1)  ; may or may not embed data in issue vs. use node
            [any-utf8? any-inert?]

email       "email address"
            (:node1)  ; may or may not embed data in issue vs. use node
            [any-utf8? any-inert?]

issue       "immutable codepoint or codepoint sequence"
            (:node1)  ; may or may not embed data in issue vs. use node
            [any-utf8? any-inert? any-sequencable?]

money       "digits and decimal points as a string, preserved precisely"
            (CELL_MASK_NO_NODES)
            [any-utf8? any-inert? any-sequencable?]

sigil       "Decorators like $ ^ & @ ' ~~"
            (CELL_MASK_NO_NODES)
            [any-utf8? any-sequencable?]  ; NOT inert


; ============================================================================
; ABOVE THIS LINE, CELL's "Extra" IS RAW BITS: Cell_Extra_Needs_Mark() = false
; ============================================================================

; With CELL_FLAG_DONT_MARK_NODE1 and CELL_FLAG_DONT_MARK_NODE2, each cell
; can say whether the GC needs to consider marking the first or second slots
; in the payload.  But rather than sacrifice another bit for whether the
; EXTRA slot is a node, the types are in order so that all the ones that need
; it marked come after this point.

; ============================================================================
; BELOW THIS LINE, CELL's "Extra" USES A NODE: Cell_Extra_Needs_Mark() = true
; ============================================================================


varargs     "evaluator position for variable numbers of arguments"
            (node2)
            [any-inert?]

<ANY-CONTEXT?>

    object      "context of names with values"
    ~lazy~:U    "unstable antiform object for lazy evaluation (WIP)"
                (node1 node2)
                [any-inert?]

    module      "loadable context of code and data"
                (node1)
                [any-inert?]

    error       "error context with id, arguments, and stack origin"
    ~raised~:U  "trappable error cooperatively returned from a function call"
                (node1 node2)
                [any-inert?]

    port        "external series, an I/O channel"
                (node1 node2)
                [any-inert?]

    frame       "arguments and locals of a function state"
    ~action~    "will trigger function execution from words"
                (node1 node2)
                [any-branch?]

</ANY-CONTEXT?>


; ============================================================================
; BEGIN BINDABLE TYPES - SEE Is_Bindable() - Cell's "extra" USED FOR BINDING
; ============================================================================

<ANY-BINDABLE?>

<ANY-WORD?>  ; (order matters, see Sigilize_Any_Plain_Heart())

    word        "evaluates a variable or action"
    ~keyword~   "special constant values (e.g. ~null~, ~void~)"
                (node1)
                [any-utf8? any-plain-value? any-sequencable?]

    meta-word   "word that quotes product or turns quasiforms to antiforms"
                (node1)
                [any-utf8? any-meta-value? any-sequencable?]

    wild-word   "inert form of word"
                (node1)
                [any-utf8? any-wild-value? any-sequencable?]

    the-word    "evaluates to the bound version of the word as a word!"
                (node1)
                [any-utf8? any-the-value? any-sequencable?]

    var-word    "evaluates to the bound version of the word as a the-word!"
                (node1)
                [any-utf8? any-var-value? any-sequencable?]

</ANY-WORD?>


<ANY-SEQUENCE?>

  <ANY-TUPLE?>  ; (order matters, see Sigilize_Any_Plain_Heart())

    tuple       "member selection with inert bias"
                (:node1)
                [any-scalar? any-plain-value?]

    meta-tuple  "tuple that quotes product or turns quasiforms to antiforms"
                (:node1)
                [any-meta-value?]

    wild-tuple  "inert form of tuple"
                (:node1)
                [any-scalar? any-wild-value?]

    the-tuple   "evaluates to the bound version of the tuple as a tuple!"
                (:node1)
                [any-scalar? any-the-value?]

    var-tuple   "evaluates to the bound version of the tuple as a the-tuple!"
                (:node1)
                [any-scalar? any-var-value?]

  </ANY-TUPLE?>

  <ANY-CHAIN?>  ; (order matters, see Sigilize_Any_Plain_Heart())

    chain       "refinement and function call dialect"
                (:node1)
                [any-scalar? any-plain-value?]

    meta-chain  "chain that quotes product or turns quasiforms to antiforms"
                (:node1)
                [any-meta-value?]

    wild-chain  "inert form of chain"
                (:node1)
                [any-wild-value?]

    the-chain   "evaluates to the bound version of the chain as a the-chain!"
                (:node1)
                [any-the-value?]

    var-chain   "evaluates to the bound version of the chain as a chain!"
                (:node1)
                [any-var-value?]

  </ANY-CHAIN?>

  <ANY-PATH?>  ; (order matters, see Sigilize_Any_Plain_Heart())

    path        "member or refinement selection with execution bias"
                (:node1)
                [any-plain-value?]

    meta-path   "path that quotes product or turns quasiforms to antiforms"
                (:node1)
                [any-meta-value?]

    wild-path   "inert form of path"
                (:node1)
                [any-wild-value?]

    the-path    "evaluates to the bound version of the path as a the-path!"
                (:node1)
                [any-the-value?]

    var-path    "evaluates to the bound version of the path as a path!"
                (:node1)
                [any-var-value?]

  </ANY-PATH?>

</ANY-SEQUENCE?>


<ANY-LIST?>

  <ANY-BLOCK?>  ; (order matters, see Sigilize_Any_Plain_Heart())

    block       "list of elements that blocks evaluation unless EVAL is used"
    ~pack~:U    "multi-return that can be unpacked or decays to first item"
                (node1)
                [any-series? any-branch? any-plain-value? any-sequencable?]

    meta-block  "block that evaluates to produce a quoted block"
                (node1)
                [any-series? any-branch? any-meta-value? any-sequencable?]

    wild-block  "inert form of block"
                (node1)
                [any-series? any-branch? any-wild-value? any-sequencable?]

    the-block   "evaluates to the bound version of the block as a the-block!"
                (node1)
                [any-series? any-branch? any-the-value? any-sequencable?]

    var-block   "evaluates to the bound version of the block as a block!"
                (node1)
                [any-series? any-branch? any-var-value? any-sequencable?]

  </ANY-BLOCK?>


  <ANY-FENCE?>  ; (order matters, see Sigilize_Any_Plain_Heart())

    fence       "list of elements that are used in construction via MAKE"
    ~datatype~  "the type of a value expressed as an antiform"
                (node1)
                [any-series? any-branch? any-plain-value? any-sequencable?]

    meta-fence  "fence that we don't know what it does yet"
                (node1)
                [any-series? any-branch? any-meta-value? any-sequencable?]

    wild-fence  "inert form of fence"
                (node1)
                [any-series? any-branch? any-wild-value? any-sequencable?]

    the-fence   "evaluates to the bound version of the fence as a the-fence!"
                (node1)
                [any-series? any-branch? any-the-value? any-sequencable?]

    var-fence   "evaluates to the bound version of the fence as a fence!"
                (node1)
                [any-series? any-branch? any-var-value? any-sequencable?]

  </ANY-FENCE?>


  <ANY-GROUP?>  ; (order matters, see Sigilize_Any_Plain_Heart())

    group       "list that evaluates expressions as an isolated group"
    ~splice~    "fragment of multiple values without a surrounding block"
                (node1)
                [any-series? any-plain-value? any-sequencable?]

    meta-group  "group that quotes product or turns antiforms to quasiforms"
                (node1)
                [any-series? any-meta-value? any-sequencable?]

    wild-group  "inert form of group"
                (node1)
                [any-series? any-wild-value? any-sequencable?]

    the-group   "evaluates to the bound version of the group as a the-group!"
                (node1)
                [any-series? any-the-value? any-branch? any-sequencable?]

    var-group   "evaluates to the bound version of the group as a group!"
                (node1)
                [any-series? any-var-value? any-sequencable?]

  </ANY-GROUP?>

</ANY-LIST?>

; COMMA! is weirdly bindable, due to its application in variadic feeds.
; It's an implementation detail which would require inventing another datatype
; that was FEED-specific.  Better ideas welcome.

comma         "separator between full evaluations (otherwise invisible)"
~barrier~:U   "elision state that is discarded by the evaluator"
              (CELL_MASK_NO_NODES)
              [any-unit?]  ; NOT inert

</ANY-BINDABLE?>

; ======= END "FUNDAMENTALS" THAT AREN'T QUOTED, QUASI, OR ANTIFORM ==========

; Note: We don't use <ANY-FUNDAMENTAL></ANY-FUNDAMENTAL> in this file because
; fundamentals must include TYPE_0 for all extension types.  So that typerange
; is made explicitly in %make-types.r

; ============================================================================
; ABOVE THESE ARE QUOTED, QUASIFORM, and ANTIFORM "PSEUDOTYPES"
; ============================================================================

; The TYPE_QUOTED, TYPE_QUASIFORM, and all the antiform types (TYPE_SPLICE,
; TYPE_TRASH, etc.) enum values never appear in the HEART_BYTE() of a cell.
; These are synthesized datatypes when the QUOTE_BYTE() contains values other
; than one (NOQUOTE_1).
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
; (Just one example: ANY-VALUE? is currently defined in such a way that it
; tolerates the state of an unset variable, since it's supposed to model
; anything that can be stored in a variable (the `Value` typedef can hold
; antiform blank).  But since the ANY-VALUE? function must thus take its
; argument as ^META to receive the state, we don't automatically produce the
; ANY-VALUE? function by means of this table, it has to be a native.)
