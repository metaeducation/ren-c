REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Rebol datatypes and their related attributes"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2022 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        This table is used to make C defines and intialization tables.

            name            "description"
            ~antiform~      [typesets]  ; makes TS_XXX
                            (CELL_FLAG_XXX | CELL_FLAG_XXX)
                            [class       make    mold]

        name        - name of datatype (generates words)
        description - short statement of type's purpose (used by HELP)
        antiform    - if present, the name of the antiform of the type
        typesets    - what typesets the type belongs to
        class       - how "generic" actions are dispatched (T_type)
        make        - it can be made with #[datatype] method
        mold        - code implementing both MOLD and FORM (hook gets a flag)

        What is in the table can be `+` to mean the method exists and has the
        same name as the type (e.g. MF_Blank() if type is BLANK!)

        If it is `?` then the method is loaded dynamically by an extension,
        and unavailable otherwise and uses e.g. T_Unhooked()

        If it is `-` then it is not available at all, and will substitute with
        an implementation that fails, e.g. CT_Fail()

        If it is 0 then it really should not happen, ever--so just null is
        used to generate an exception (for now).

        Note that if there is `somename` in the class column, that means you
        will find the ACTION! dispatch for that type in `REBTYPE(Somename)`.
    }
    Notes: {
      * Code should avoid dependence on exact values of the heart bytes.
        Any relative ordering dependencies not captured in this type table
        should be captured as macros/functions in %sys-ordered.h

      * ANY-SCALAR! is weird at this point, because TUPLE! may or may not be
        fully numeric (1.2.3 vs alpha.beta.gamma).  What the typeset was for
        was defining input types things like ADD would accept, and now that's
        kind of gotten fuzzy.
    }
]


; VOID is not a "datatype" (type of void is NULL) but it uses the REB_VOID
; heart value of 0.  This is also the heart byte of its antiform (called
; "trash", the contents of unset variables).  attempts at memset() to 0
; optimization are made for creating unset variables quickly.

void        "absence of value, used by many operations to opt-out"
~trash~     (CELL_MASK_NO_NODES)
            []
            [-       -       -]


; ============================================================================
; BEGIN TYPES THAT ARE EVALUATOR INERT (Any_Inert() fails on VOID + ANTIFORM)
; ============================================================================

blank       "placeholder unit type"
            (CELL_MASK_NO_NODES)
            [any-unit!]  ; allow as `branch`?
            [blank       -       +]

decimal     "64bit floating point number (IEEE standard)"
            (CELL_MASK_NO_NODES)
            [any-number! any-scalar!]
            [decimal     *       +]

percent     "special form of decimals (used mainly for layout)"
            (CELL_MASK_NO_NODES)
            [any-number! any-scalar!]
            [decimal     *       +]

money       "high precision decimals with denomination (opt)"
            (CELL_MASK_NO_NODES)
            [any-scalar!]
            [money       +       +]

time        "time of day or duration"
            (CELL_MASK_NO_NODES)
            [any-scalar!]
            [time        +       +]

date        "day, month, year, time of day, and timezone"
            (CELL_MASK_NO_NODES)
            []
            [date        +       +]

integer     "64 bit integer"
            (CELL_MASK_NO_NODES)  ; would change with bignum ints
            [any-number! any-scalar!]
            [integer     +       +]

parameter         "function parameter description"
~unspecialized~   (CELL_FLAG_FIRST_IS_NODE | CELL_FLAG_SECOND_IS_NODE)
                  []
                  [parameter   +       +]

bitset      "set of bit flags"
            (CELL_FLAG_FIRST_IS_NODE)
            []
            [bitset      +       +]

map         "name-value pairs (hash associative)"
            (CELL_FLAG_FIRST_IS_NODE)
            []
            [map         +       +]

handle      "arbitrary internal object or value"
            ()
            []
            [handle      -       +]


; URL! has a HEART-BYTE! that is a string, but is not itself in the ANY-STRING!
; category, due to not allowing index positions that would pass the URN.
;
url         "uniform resource locator or identifier"
            (CELL_FLAG_FIRST_IS_NODE)
            [any-utf8!]
            [url         string  string]


binary      "series of bytes"
            (CELL_FLAG_FIRST_IS_NODE)
            [any-series!]  ; not an ANY-STRING!
            [binary      *       +]


<ANY-STRING!>  ; (order does not currently matter)

    text        "text string series of characters"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-series! any-utf8!]
                [string      *       *]

    file        "file name or path"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-series! any-utf8!]
                [string      *       *]

    email       "email address"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-series! any-utf8!]
                [string      *       *]

    tag         "markup string (HTML or XML)"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-series! any-utf8!]
                [string      *       *]

</ANY-STRING!>  ; ISSUE! is "string-like" but not an ANY-STRING!


issue       "immutable codepoint or codepoint sequence"
            ()  ; may embed data in issue
            [any-utf8!]
            [issue       *       *]


; ============================================================================
; BEGIN Cell_Extra_Needs_Mark() Cell's "extra" USES A NODE
; ============================================================================
;
; There's CELL_FLAG_FIRST_IS_NODE and CELL_FLAG_SECOND_IS_NODE so each cell
; can say whether the GC needs to consider marking the first or second slots
; in the payload.  But rather than sacrifice another bit for whether the
; EXTRA slot is a node, the types are in order so that all the ones that need
; it marked come after this point.

varargs     "evaluator position for variable numbers of arguments"
            (CELL_FLAG_SECOND_IS_NODE)
            []
            [varargs     +       +]

pair        "two dimensional point or size"
            (CELL_FLAG_FIRST_IS_NODE)
            [any-scalar!]
            [pair        +       +]

<ANY-CONTEXT!>

    object      "context of names with values"
    ~lazy~      (CELL_FLAG_FIRST_IS_NODE | CELL_FLAG_SECOND_IS_NODE)
    #unstable   []
                [context     *       *]

    module      "loadable context of code and data"
                (CELL_FLAG_FIRST_IS_NODE | CELL_FLAG_SECOND_IS_NODE)
                []
                [context     *       *]

    error       "error context with id, arguments, and stack origin"
    ~raised~    (CELL_FLAG_FIRST_IS_NODE | CELL_FLAG_SECOND_IS_NODE)
    #unstable   []
                [context     +       +]

    port        "external series, an I/O channel"
                (CELL_FLAG_FIRST_IS_NODE | CELL_FLAG_SECOND_IS_NODE)
                []
                [port        +       context]

  ; ==========================================================================
  ; BEGIN EVALUATOR ACTIVE TYPES, SEE Any_Evaluative()
  ; ==========================================================================

    frame       "arguments and locals of a function state"
    ~action~    (CELL_FLAG_FIRST_IS_NODE | CELL_FLAG_SECOND_IS_NODE)
                [any-branch!]
                [frame       +       *]

</ANY-CONTEXT!>


comma       "separator between full evaluations (that is otherwise invisible)"
~barrier~   (CELL_MASK_NO_NODES)
#unstable   [any-unit!]
            [comma       -       +]


; ============================================================================
; BEGIN BINDABLE TYPES - SEE Is_Bindable() - Cell's "extra" USED FOR BINDING
; ============================================================================


<ANY-WORD!>  ; (order matters, e.g. Theify_Any_Plain_Kind())

    word        "evaluates a variable or action"
    ~antiword~   (CELL_FLAG_FIRST_IS_NODE)  ; !!! Better name than antiword?
                [any-utf8! any-plain-value!]
                [word        *       +]

    set-word    "definition of a word's value"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-utf8! any-set-value!]
                [word        *       +]

    get-word    "the value of a word (variable)"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-utf8! any-get-value!]
                [word        *       +]

    meta-word   "word that quotes product or turns quasiforms to antiforms"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-utf8! any-meta-value!]
                [word        *       +]

    type-word   "inert form of word"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-utf8! any-type-value!]
                [word        *       +]

    the-word    "inert form of word"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-utf8! any-the-value!]
                [word        *       +]

</ANY-WORD!>


<ANY-TUPLE!>  ; (order matters, e.g. Theify_Any_Plain_Kind())

    tuple       "member selection with inert bias"
                ()
                [any-sequence! any-scalar! any-plain-value!]
                [sequence    *       *]

    set-tuple   "definition of a tuple's value"
                ()
                [any-sequence! any-set-value!]
                [sequence    *       *]

    get-tuple   "the value of a tuple"
                ()
                [any-sequence! any-get-value!]
                [sequence    *       *]

    meta-tuple  "tuple that quotes product or turns quasiforms to antiforms"
                ()
                [any-sequence! any-meta-value!]
                [sequence    *       *]

    type-tuple  "inert form of tuple"
                ()
                [any-sequence! any-scalar! any-type-value!]
                [sequence    *       *]

    the-tuple   "inert form of tuple"
                ()
                [any-sequence! any-scalar! any-the-value!]
                [sequence    *       *]

</ANY-TUPLE!>


<ANY-PATH!>  ; (order matters, e.g. Theify_Any_Plain_Kind())

    path        "member or refinement selection with execution bias"
                ()
                [any-sequence! any-plain-value!]
                [sequence    *       *]

    set-path    "definition of a path's value"
                ()
                [any-sequence! any-set-value!]
                [sequence    *       *]

    get-path    "the value of a path"
                ()
                [any-sequence! any-get-value!]
                [sequence    *       *]

    meta-path   "path that quotes product or turns quasiforms to antiforms"
                ()
                [any-sequence! any-meta-value!]
                [sequence    *       *]

    type-path   "inert form of path"
                ()
                [any-sequence! any-type-value!]
                [sequence    *       *]

    the-path    "inert form of path"
                ()
                [any-sequence! any-the-value!]
                [sequence    *       *]

</ANY-PATH!>


<ANY-BLOCK!>  ; (order matters, e.g. Theify_Any_Plain_Kind())

    block       "array of values that blocks evaluation unless DO is used"
    ~pack~      (CELL_FLAG_FIRST_IS_NODE)
    #unstable   [any-array! any-series! any-branch! any-plain-value!]
                [array       *       *]

    set-block   "array of values that will element-wise SET if evaluated"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-array! any-series! any-set-value!]
                [array       *       *]

    get-block   "array of values that is reduced if evaluated"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-array! any-series! any-branch! any-get-value!]
                [array       *       *]

    meta-block  "block that evaluates to produce a quoted block"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-array! any-series! any-branch! any-meta-value!]
                [array       *       *]

    type-block  "alternative inert form of block"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-array! any-series! any-branch! any-type-value!]
                [array       *       *]

    the-block   "alternative inert form of block"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-array! any-series! any-branch! any-the-value!]
                [array       *       *]

</ANY-BLOCK!>


<ANY-GROUP!>  ; (order matters, e.g. Theify_Any_Plain_Kind())

    group       "array that evaluates expressions as an isolated group"
    ~splice~    (CELL_FLAG_FIRST_IS_NODE)
                [any-array! any-series! any-branch! any-plain-value!]
                [array       *       *]

    set-group   "array that evaluates and runs SET on the resulting word/path"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-array! any-series! any-set-value!]
                [array       *       *]

    get-group   "array that evaluates and runs GET on the resulting word/path"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-array! any-series! any-get-value!]
                [array       *       *]

    meta-group  "group that quotes product or turns quasiforms to antiforms"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-array! any-series! any-meta-value!]
                [array       *       *]

    type-group  "inert form of group"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-array! any-series! any-type-value!]
                [array       *       *]

    the-group   "inert form of group"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-array! any-series! any-the-value!]
                [array       *       *]

</ANY-GROUP!>



; ============================================================================
; QUOTED, QUASIFORM, and ANTIFORM "PSEUDOTYPES"
; ============================================================================

; The REB_QUOTED, REB_QUASIFORM, and REB_ANTIFORM enum values never appear in
; the HEART_BYTE() of a cell.  These are synthesized datatypes when the
; QUOTE_BYTE() contains values other than one (NOQUOTE_1).
;
; They're not inert... QUASIFORM! becomes ANTIFORM! when evaluated, and QUOTED!
; removes one level of quoting.  (ANTIFORM! should never be seen by the
; evaluator, only produced by it.)
;
; NOTE: These are in the %types.r table because that drives the creation of
; things like "type spec strings" as well as the QUOTED! and QUOTED? usermode
; functions, which are in specific places expected for a "datatype".  The
; macros for testing Is_Quoted()/Is_Quasiform()/Is_Antiform() are exempted and
; written by hand.

quasiform   "value which evaluates to an antiform"
            ()
            []
            [quasiform    +       -]

quoted      "container for arbitrary levels of quoting"
            ()
            [any-branch!]
            [quoted       +       -]

antiform    "special states that cannot be stored in blocks"
            ()
            []
            [quoted       +       -]


; This is the end of the value cell enumerations (after REB_QUOTED is REB_MAX)
; and no valid cell should have bits between REB_QUOTED and REB_MAX.
