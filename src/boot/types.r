REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Rebol datatypes and their related attributes"
    Rights: --{
        Copyright 2012 REBOL Technologies
        Copyright 2012-2024 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }--
    License: --{
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }--
    Purpose: --{
        This table is used to make C defines and intialization tables.

            name            "description"
            ~antiform~      (CELL_FLAG_XXX | CELL_FLAG_XXX)
                            [constraints]  ; makes `g_typeset_memberships`
                            [class       make    mold]

        name        - name of datatype (generates words)
        description - short statement of type's purpose (used by HELP)
        antiform    - if present, the name of the antiform of the type
        constraints - type constraints this type will answer true to
        class       - how "generic" actions are dispatched (T_type)
        make        - it can be made with #[datatype] method
        mold        - code implementing both MOLD and FORM (hook gets a flag)

        What is in the table can be `+` to mean the method exists and has the
        same name as the type (e.g. MF_Blank() if type is BLANK?)

        If it is `?` then the method is loaded dynamically by an extension,
        and unavailable otherwise and uses e.g. T_Unhooked()

        If it is `-` then it is not available at all, and will substitute with
        an implementation that fails, e.g. CT_Fail()

        If it is 0 then it really should not happen, ever--so just null is
        used to generate an exception (for now).

        Note that if there is `somename` in the class column, that means you
        will find the ACTION? dispatch for that type in `DECLARE_GENERICS(Somename)`.
    }--
    Notes: --{
      * Code should avoid dependence on exact values of the heart bytes.
        Any relative ordering dependencies not captured in this type table
        should be captured as macros/functions in %sys-ordered.h

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
    }--
]

; ============== BEGIN "ELEMENTS" THAT CAN BE PUT IN BLOCKS ==================
<ANY-ELEMENT?>
; ============================================================================

blank       "placeholder unit type"
~nothing~   (CELL_MASK_NO_NODES)
            [any-unit? any-inert?]  ; allow as `branch`?
            [blank       -       +]

comma       "separator between full evaluations (that is otherwise invisible)"
~barrier~   (CELL_MASK_NO_NODES)
#unstable   [any-unit?]  ; NOT inert
            [comma       -       +]

decimal     "64bit floating point number (IEEE standard)"
            (CELL_MASK_NO_NODES)
            [any-number? any-scalar? any-inert?]
            [decimal     *       +]

percent     "special form of decimals (used mainly for layout)"
            (CELL_MASK_NO_NODES)
            [any-number? any-scalar? any-inert?]
            [decimal     *       +]

money       "high precision decimals with denomination (opt)"
            (CELL_MASK_NO_NODES)
            [any-scalar? any-inert?]
            [money       +       +]

time        "time of day or duration"
            (CELL_MASK_NO_NODES)
            [any-scalar? any-inert?]
            [time        +       +]

date        "day, month, year, time of day, and timezone"
            (CELL_MASK_NO_NODES)
            [any-inert?]
            [date        +       +]

integer     "64 bit integer"
            (CELL_MASK_NO_NODES)  ; would change with bignum ints
            [any-number? any-scalar? any-inert? any-sequencable?]
            [integer     +       +]

parameter   "function parameter description"
~hole~      (node1 node2)
            [any-inert?]
            [parameter   +       +]

bitset      "set of bit flags"
            (node1)
            [any-inert?]
            [bitset      +       +]

map         "name-value pairs (hash associative)"
            (node1)
            [any-inert?]
            [map         +       +]

handle      "arbitrary internal object or value"
            (:node1)  ; managed handles use a node to get a shared instance
            [any-inert?]
            [handle      -       +]

blob        "series of bytes"
            (node1)
            [any-series? any-inert?]  ; note: not an ANY-STRING?
            [blob        *       +]

<ANY-STRING?>  ; (order does not currently matter)

    text        "text string series of characters"
                (node1)
                [any-series? any-utf8? any-inert? any-sequencable?]
                [string      *       *]

    file        "file name or path"
                (node1)
                [any-series? any-utf8? any-inert?]
                [string      *       *]

    tag         "markup string (HTML or XML)"
    ~tripwire~  (node1)
                [any-series? any-utf8? any-inert? any-sequencable?]
                [string      *       *]

</ANY-STRING?>

url         "uniform resource locator or identifier"
            (:node1)  ; may or may not embed data in issue vs. use node
            [any-utf8? any-inert?]
            [utf8        *       +]

email       "email address"
            (:node1)  ; may or may not embed data in issue vs. use node
            [any-utf8? any-inert?]
            [utf8        *       +]

issue       "immutable codepoint or codepoint sequence"
            (:node1)  ; may or may not embed data in issue vs. use node
            [any-utf8? any-inert? any-sequencable?]
            [utf8       *        +]

sigil       "Decorators like $ ^ & @ ' ~~"
            (CELL_MASK_NO_NODES)
            [any-utf8? any-sequencable?]  ; NOT inert
            [utf8        *       +]  ; UTF-8 content in cell, like ISSUE?



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
            [varargs     +       +]

pair        "two dimensional point or size"
            (node1)
            [any-scalar? any-inert?]
            [pair        +       +]

<ANY-CONTEXT?>

    object      "context of names with values"
    ~lazy~      (node1 node2)
    #unstable   [any-inert?]
                [context     *       *]

    module      "loadable context of code and data"
                (node1 node2)
                [any-inert?]
                [context     *       *]

    error       "error context with id, arguments, and stack origin"
    ~raised~    (node1 node2)
    #unstable   [any-inert?]
                [context     +       +]

    port        "external series, an I/O channel"
                (node1 node2)
                [any-inert?]
                [port        +       context]

    frame       "arguments and locals of a function state"
    ~action~    (node1 node2)
                [any-branch?]
                [frame       +       *]

</ANY-CONTEXT?>


; ============================================================================
; BEGIN BINDABLE TYPES - SEE Is_Bindable() - Cell's "extra" USED FOR BINDING
; ============================================================================

<BINDABLE?>

<ANY-WORD?>  ; (order matters, see Sigilize_Any_Plain_Kind())

    word        "evaluates a variable or action"
    ~keyword~   (node1)
                [any-utf8? any-plain-value? any-sequencable?]
                [word        *       *]

    meta-word   "word that quotes product or turns quasiforms to antiforms"
                (node1)
                [any-utf8? any-meta-value? any-sequencable?]
                [word        *       *]

    type-word   "inert form of word"
                (node1)
                [any-utf8? any-type-value? any-sequencable?]
                [word        *       *]

    the-word    "inert form of word"
                (node1)
                [any-utf8? any-the-value? any-sequencable?]
                [word        *       *]

    var-word    "word that evaluates to the bound version of the word"
                (node1)
                [any-utf8? any-var-value? any-sequencable?]
                [word        *       *]

</ANY-WORD?>


<ANY-SEQUENCE?>

  <ANY-TUPLE?>  ; (order matters, see Sigilize_Any_Plain_Kind())

    tuple       "member selection with inert bias"
                (:node1)
                [any-scalar? any-plain-value?]
                [sequence    *       *]

    meta-tuple  "tuple that quotes product or turns quasiforms to antiforms"
                (:node1)
                [any-meta-value?]
                [sequence    *       *]

    type-tuple  "inert form of tuple"
                (:node1)
                [any-scalar? any-type-value?]
                [sequence    *       *]

    the-tuple   "inert form of tuple"
                (:node1)
                [any-scalar? any-the-value?]
                [sequence    *       *]

    var-tuple   "tuple that evaluates to the bound form of the tuple"
                (:node1)
                [any-scalar? any-var-value?]
                [sequence    *       *]

  </ANY-TUPLE?>

  <ANY-CHAIN?>  ; (order matters, see Sigilize_Any_Plain_Kind())

    chain       "refinement and function call dialect"
                (:node1)
                [any-scalar? any-plain-value?]
                [sequence    *       *]

    meta-chain  "chain that quotes product or turns quasiforms to antiforms"
                (:node1)
                [any-meta-value?]
                [sequence    *       *]

    type-chain  "inert form of chain"
                (:node1)
                [any-type-value?]
                [sequence    *       *]

    the-chain   "inert form of chain"
                (:node1)
                [any-the-value?]
                [sequence    *       *]

    var-chain   "chain that evaluates to the bound form of the chain"
                (:node1)
                [any-var-value?]
                [sequence    *       *]

  </ANY-CHAIN?>

  <ANY-PATH?>  ; (order matters, see Sigilize_Any_Plain_Kind())

    path        "member or refinement selection with execution bias"
                (:node1)
                [any-plain-value?]
                [sequence    *       *]

    meta-path   "path that quotes product or turns quasiforms to antiforms"
                (:node1)
                [any-meta-value?]
                [sequence    *       *]

    type-path   "inert form of path"
                (:node1)
                [any-type-value?]
                [sequence    *       *]

    the-path    "inert form of path"
                (:node1)
                [any-the-value?]
                [sequence    *       *]

    var-path    "path that evaluates to the bound version of the path"
                (:node1)
                [any-var-value?]
                [sequence    *       *]

  </ANY-PATH?>

</ANY-SEQUENCE?>


<ANY-LIST?>

  <ANY-BLOCK?>  ; (order matters, see Sigilize_Any_Plain_Kind())

    block       "list of elements that blocks evaluation unless EVAL is used"
    ~pack~      (node1)
    #unstable   [any-series? any-branch? any-plain-value? any-sequencable?]
                [list        *       *]

    meta-block  "block that evaluates to produce a quoted block"
                (node1)
                [any-series? any-branch? any-meta-value? any-sequencable?]
                [list        *       *]

    type-block  "alternative inert form of block"
                (node1)
                [any-series? any-branch? any-type-value? any-sequencable?]
                [list        *       *]

    the-block   "alternative inert form of block"
                (node1)
                [any-series? any-branch? any-the-value? any-sequencable?]
                [list        *       *]

    var-block   "block that evaluates to the bound version of the block"
                (node1)
                [any-series? any-branch? any-var-value? any-sequencable?]
                [list        *       *]

  </ANY-BLOCK?>


  <ANY-FENCE?>  ; (order matters, see Sigilize_Any_Plain_Kind())

    fence       "list of elements that are used in construction via MAKE"
                (node1)
                [any-series? any-branch? any-plain-value? any-sequencable?]
                [list        *       *]

    meta-fence  "fence that we don't know what it does yet"
                (node1)
                [any-series? any-branch? any-meta-value? any-sequencable?]
                [list        *       *]

    type-fence  "alternative inert form of fence"
                (node1)
                [any-series? any-branch? any-type-value? any-sequencable?]
                [list        *       *]

    the-fence   "alternative inert form of fence"
                (node1)
                [any-series? any-branch? any-the-value? any-sequencable?]
                [list        *       *]

    var-fence   "fence that evaluates to the bound version of the fence"
                (node1)
                [any-series? any-branch? any-var-value? any-sequencable?]
                [list        *       *]

  </ANY-FENCE?>


  <ANY-GROUP?>  ; (order matters, see Sigilize_Any_Plain_Kind())

    group       "list that evaluates expressions as an isolated group"
    ~splice~    (node1)
                [any-series? any-plain-value? any-sequencable?]
                [list        *       *]

    meta-group  "group that quotes product or turns antiforms to quasiforms"
                (node1)
                [any-series? any-meta-value? any-sequencable?]
                [list        *       *]

    type-group  "inert form of group"
                (node1)
                [any-series? any-type-value? any-sequencable?]
                [list        *       *]

    the-group   "inert form of group"
                (node1)
                [any-series? any-the-value? any-branch? any-sequencable?]
                [list        *       *]

    var-group   "group that evaluates to the bound version of the group"
                (node1)
                [any-series? any-var-value? any-sequencable?]
                [list        *       *]

  </ANY-GROUP?>

</ANY-LIST?>

</BINDABLE?>


; ============================================================================
; QUOTED, QUASIFORM, and ANTIFORM "PSEUDOTYPES"
; ============================================================================

; The REB_QUOTED, REB_QUASIFORM, and REB_ANTIFORM enum values never appear in
; the HEART_BYTE() of a cell.  These are synthesized datatypes when the
; QUOTE_BYTE() contains values other than one (NOQUOTE_1).
;
; They're not inert... QUASIFORM? becomes ANTIFORM? when evaluated, and QUOTED?
; removes one level of quoting.  (ANTIFORM? should never be seen by the
; evaluator, only produced by it.)
;
; NOTE: These are in the %types.r table because that drives the creation of
; things like "type spec strings" as well as the QUOTED? and QUOTED? usermode
; functions, which are in specific places expected for a "datatype".  The
; macros for testing Is_Quoted()/Is_Quasiform()/Is_Antiform() are exempted and
; written by hand.

quasiform   "value which evaluates to an antiform"
            (:node1 :node2)
            []
            [-          -       - ]

quoted      "container for arbitrary levels of quoting"
            (:node1 :node2)
            [any-branch?]
            [-           -       -]

; =============== END "ELEMENTS" THAT CAN BE PUT IN BLOCKS ===================
</ANY-ELEMENT?>
; ============================================================================

antiform    "special states that cannot be stored in blocks"
            (:node1 :node2)
            []
            [-           -       -]


; This is the end of the value cell enumerations (after REB_QUOTED is REB_MAX)
; and no valid cell should have bits between REB_QUOTED and REB_MAX.
