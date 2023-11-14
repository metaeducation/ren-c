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

        name        - name of datatype (generates words)
        description - short statement of type's purpose (used by HELP)
        class       - how "generic" actions are dispatched (T_type)
        make        - it can be made with #[datatype] method
        mold        - code implementing both MOLD and FORM (hook gets a flag)
        typesets    - what typesets the type belongs to

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
      * NULL is not a datatype, and is not included here.  It uses the special
        heart byte of 0, which is also the heart byte of its isotopic form
        (which is called VOID).

      * Code shouldn't be dependent on the specific values of the other heart
        bytes.  Though there are some issues related to relative ordering;
        all such dependencies should be in %sys-ordered.h

      * There's no particularly fast test for ANY_ARRAY(), ANY_PATH(), or
        ANY_WORD(), as they're less common than testing for ANY_INERT().

      * ANY-SCALAR! is weird at this point, because TUPLE! may or may not be
        fully numeric (1.2.3 vs alpha.beta.gamma).  What the typeset was for
        was defining input types things like ADD would accept, and now that's
        kind of gotten fuzzy.
    }
]


; name      "description"
;           [typesets]  ; makes TS_XXX
;           (CELL_FLAG_XXX | CELL_FLAG_XXX)  ; makes CELL_MASK_XXX
;           [class       make    mold]


; ============================================================================
; BEGIN TYPES THAT ARE EVALUATOR INERT
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

pair        "two dimensional point or size"
            (CELL_FLAG_FIRST_IS_NODE)
            [any-scalar!]
            [pair        +       +]

parameter   "function parameter description"
            (CELL_FLAG_FIRST_IS_NODE)
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
; category.
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
; BEGIN BINDABLE TYPES - SEE Is_Bindable() - ValueStruct.extra USED FOR BINDING
; ============================================================================


<ANY-CONTEXT!>

    object      "context of names with values"
    ~lazy~      (CELL_FLAG_FIRST_IS_NODE | CELL_FLAG_SECOND_IS_NODE)
                []
                [context     *       *]

    module      "loadable context of code and data"
                (CELL_FLAG_FIRST_IS_NODE | CELL_FLAG_SECOND_IS_NODE)
                []
                [context     *       *]

    error       "error context with id, arguments, and stack origin"
                (CELL_FLAG_FIRST_IS_NODE | CELL_FLAG_SECOND_IS_NODE)
                []
                [context     +       +]

    frame       "arguments and locals of a specific action invocation"
    ~activation~ (CELL_FLAG_FIRST_IS_NODE | CELL_FLAG_SECOND_IS_NODE)
                [any-branch!]
                [frame       +       *]

    port        "external series, an I/O channel"
                (CELL_FLAG_FIRST_IS_NODE | CELL_FLAG_SECOND_IS_NODE)
                []
                [port        +       context]

</ANY-CONTEXT!>

varargs     "evaluator position for variable numbers of arguments"
            (CELL_FLAG_SECOND_IS_NODE)
            []
            [varargs     +       +]


<ANY-THE-VALUE!>  ; (order matters, e.g. UNTHEIFY_ANY_XXX_KIND())

    ; Review: Should these be ANY-BRANCH! types?

    the-block   "alternative inert form of block"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-block! any-array! any-series! any-branch!]
                [array       *       *]

    the-group   "inert form of group"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-group! any-array! any-series!]
                [array       *       *]

    the-path    "inert form of path"
                ()
                [any-path! any-sequence!]
                [sequence    *       *]

    the-tuple   "inert form of tuple"
                ()
                [any-tuple! any-sequence! any-scalar!]
                [sequence    *       *]

    the-word    "inert form of word"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-word! any-utf8!]
                [word        *       +]

</ANY-THE-VALUE!>


<ANY-TYPE-VALUE!>  ; (order matters, e.g. UNTYPEIFY_ANY_XXX_KIND())

    ; Review: Should these be ANY-BRANCH! types?

    type-block  "alternative inert form of block"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-block! any-array! any-series! any-branch!]
                [array       *       *]

    type-group  "inert form of group"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-group! any-array! any-series!]
                [array       *       *]

    type-path   "inert form of path"
                ()
                [any-path! any-sequence!]
                [sequence    *       *]

    type-tuple  "inert form of tuple"
                ()
                [any-tuple! any-sequence! any-scalar!]
                [sequence    *       *]

    type-word   "inert form of word"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-word! any-utf8!]
                [word        *       +]

</ANY-TYPE-VALUE!>


<ANY-PLAIN-VALUE!>  ; (order matters, e.g. SETIFY_ANY_PLAIN_KIND())

    block       "array of values that blocks evaluation unless DO is used"
    ~pack~      (CELL_FLAG_FIRST_IS_NODE)
                [any-block! any-array! any-series! any-branch!]
                [array       *       *]

  ; ==========================================================================
  ; BEGIN EVALUATOR ACTIVE TYPES, SEE ANY_EVALUATIVE()
  ; ==========================================================================

    group       "array that evaluates expressions as an isolated group"
    ~splice~    (CELL_FLAG_FIRST_IS_NODE)
                [any-group! any-array! any-series! any-branch!]
                [array       *       *]

    path        "member or refinement selection with execution bias"
                ()
                [any-path! any-sequence!]
                [sequence    *       *]

    tuple       "member selection with inert bias"
                ()
                [any-tuple! any-sequence! any-scalar!]  ; scalar e.g. ADD 0.0.1
                [sequence    *       *]

    word        "evaluates a variable or action"
    ~isoword~   (CELL_FLAG_FIRST_IS_NODE)  ; !!! Better name than isoword?
                [any-word! any-utf8!]
                [word        *       +]

</ANY-PLAIN-VALUE!>  ; contiguous with ANY-SET below matters


<ANY-SET-VALUE!>  ; (order matters, e.g. UNSETIFY_ANY_XXX_KIND())

    set-block   "array of values that will element-wise SET if evaluated"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-block! any-array! any-series!]
                [array       *       *]

    set-group   "array that evaluates and runs SET on the resulting word/path"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-group! any-array! any-series!]
                [array       *       *]

    set-path    "definition of a path's value"
                ()
                [any-path! any-sequence!]
                [sequence    *       *]

    set-tuple   "definition of a tuple's value"
                ()
                [any-tuple! any-sequence!]
                [sequence    *       *]

    set-word    "definition of a word's value"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-word! any-utf8!]
                [word        *       +]

</ANY-SET-VALUE!>  ; (contiguous with ANY-GET below matters)


<ANY-GET-VALUE!>  ; (order matters, e.g. UNGETIFY_ANY_XXX_KIND())

    get-block   "array of values that is reduced if evaluated"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-block! any-array! any-series! any-branch!]
                [array       *       *]

    get-group   "array that evaluates and runs GET on the resulting word/path"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-group! any-array! any-series!]
                [array       *       *]

    get-path    "the value of a path"
                ()
                [any-path! any-sequence!]
                [sequence    *       *]

    get-tuple   "the value of a tuple"
                ()
                [any-tuple! any-sequence!]
                [sequence    *       *]

    get-word    "the value of a word (variable)"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-word! any-utf8!]
                [word        *       +]

</ANY-GET-VALUE!>  ; (contiguous with ANY-META below matters)


<ANY-META-VALUE!>  ; (order matters, e.g. UNMETAFY_ANY_XXX_KIND())

    meta-block  "block that evaluates to produce a quoted block"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-block! any-array! any-series! any-branch!]
                [array       *       *]

    meta-group  "group that quotes its product or removes isotope status"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-group! any-array! any-series!]
                [array       *       *]

    meta-path   "path that quotes its product or removes isotope status"
                ()
                [any-path! any-sequence!]
                [sequence    *       *]

    meta-tuple  "tuple that quotes its product or removes isotope status"
                ()
                [any-tuple! any-sequence!]
                [sequence    *       *]

    meta-word   "word that quotes its product or removes isotope status"
                (CELL_FLAG_FIRST_IS_NODE)
                [any-word! any-utf8!]
                [word        *       +]

</ANY-META-VALUE!>


; COMMA! needs to be evaluative, so it is past the non-bindable types.  We
; want the ANY_INERT() test to be fast with a single comparison, so it has
; to null out the binding field in order to avoid crashing the processing
; since it reports Is_Bindable()

comma       "separator between full evaluations (that is otherwise invisible)"
~barrier~   (CELL_MASK_NO_NODES)
            [any-unit!]
            [comma       -       +]


; ============================================================================
; QUOTED and QUASI "PSEUDOTYPES"
; ============================================================================

; The REB_QUOTED and REB_QUASI enum values never appear in the HEART_BYTE() of
; a cell.  These are synthesized datatypes when the QUOTE_BYTE() contains
; nonzero values.
;
; Neither are inert...QUASI! becomes isotopic when evaluated, and QUOTED!
; removes one level of quoting.

quasi       "value which evaluates to a form that triggers errors on access"
            ()
            []
            [quasi       +       -]

quoted      "container for arbitrary levels of quoting"
            ()
            [any-branch!]
            [quoted       +       -]

isotope     "special values that cannot be stored in blocks"
            ()
            []
            [quoted       +       -]


; This is the end of the value cell enumerations (after REB_QUOTED is REB_MAX)
; and no valid cell should have bits between REB_QUOTED and REB_MAX.
