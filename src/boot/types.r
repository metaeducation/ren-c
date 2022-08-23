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

        !!! REVIEW IMPACT ON %sys-ordered.h ANY TIME YOU CHANGE THE ORDER !!!
        Special ordering is used to make tests on ranges of types particularly
        fast in common cases.  Pay close attention.

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
;           [class       make    mold]

; The special 0 state of the REB_XXX enumeration was once used as a marker to
; indicate array termination (parallel to how '\0' terminates strings).  But
; to save on memory usage, the concept of a full cell terminator was dropped.
; Instead, arrays are enumerated to their tail positions:
;
; https://forum.rebol.info/t/1445
;
; But REB_0 had another use, as a "less than null" signal.  This was as the
; the true internal representation of an evaluation that produced nothing.
; Today we call that use REB_0_VOID.
;
; It's also the cell type uninitialized cells wind up with.  It's a consequence
; of C-isms where memory initialization of zeros can be lower cost (globals
; always initialized to 0, calloc() and memset may be faster with 0.)  The
; alias REB_0_FREE is given to use when this is the intent for REB_0.

void        "!!! `VOID!` and `FREE!` aren't datatypes, not exposed to the user"
            []
            [0           0       0]

; REB_NULL takes value 1, but it being 1 is less intrinsic.  It is also not
; a "type"...but it is falsey, hence it has to be before LOGIC! in the table.
; In the API, a cell isn't used but it is transformed into the language NULL.
; To help distinguish it from C's NULL in places where it is undecorated,
; functions are given names like `Init_Nulled` or `Is_Nulled()`, but the
; type itself is simply called REB_NULL...which is distinct enough.

null        "!!! `NULL!` isn't a datatype, `null` can't be stored in blocks"
            []
            [0           0       +]

blank!      "placeholder unit type which acts as conditionally false"
            [any-unit!]  ; allow as `branch`?
            [blank       -       +]

logic!      "boolean true or false"
            []
            [logic       +       +]

; ============================================================================
; BEGIN TYPES THAT ARE ALWAYS "TRUTHY" - Is_Truthy()/IS_CONDITIONALLY_TRUE()
; ============================================================================

bytes       "!!! `BYTES!` isn't a datatype, `heart` type  for optimizations"
            []
            [0           0       0]

decimal!    "64bit floating point number (IEEE standard)"
            [any-number! any-scalar!]
            [decimal     *       +]

percent!    "special form of decimals (used mainly for layout)"
            [any-number! any-scalar!]
            [decimal     *       +]

money!      "high precision decimals with denomination (opt)"
            [any-scalar!]
            [money       +       +]

time!       "time of day or duration"
            [any-scalar!]
            [time        +       +]

date!       "day, month, year, time of day, and timezone"
            []
            [date        +       +]

integer!    "64 bit integer"
            [any-number! any-scalar!]
            [integer     +       +]

pair!       "two dimensional point or size"
            [any-scalar!]
            [pair        +       +]

datatype!   "type of datatype"
            []
            [datatype    +       +]

typeset!    "set of datatypes"
            []
            [typeset     +       +]

bitset!     "set of bit flags"
            []
            [bitset      +       +]

map!        "name-value pairs (hash associative)"
            []
            [map         +       +]

handle!     "arbitrary internal object or value"
            []
            [handle      -       +]


; This table of fundamental types is intended to be limited (less than
; 64 entries).  Yet there can be an arbitrary number of extension types.
; Those types use the REB_CUSTOM cell class, and give up their ->extra field
; of their cell instances to point to their specific datatype.
;
; Exceptions may be permitted.  As an example, EVENT! is implemented in an
; extension (because not all builds need it).  But it reserves a type byte
; and fills in its entry in this table when it is loaded (hence `?`)

custom!     "instance of an extension-defined type"
            []
            [-           -       -]

event!      "user interface event"  ; %extensions/event/README.md
            []
            [?           ?       ?]


; URL! has a HEART-BYTE! that is a string, but is not itself in the ANY-STRING!
; category.
;
url!        "uniform resource locator or identifier"
            []
            [url         string  string]



binary!     "series of bytes"
            [any-series!]  ; not an ANY-STRING!
            [binary      *       +]


<ANY-STRING!>  ; (order does not currently matter)

    text!       "text string series of characters"
                [any-series!]
                [string      *       *]

    file!       "file name or path"
                [any-series!]
                [string      *       *]

    email!      "email address"
                [any-series!]
                [string      *       *]

    tag!        "markup string (HTML or XML)"
                [any-series!]
                [string      *       *]

</ANY-STRING!>  ; ISSUE! is "string-like" but not an ANY-STRING!


issue!      "immutable codepoint or codepoint sequence"
            []  ; !!! sequence of INTEGER?
            [issue       *       *]


; ============================================================================
; BEGIN BINDABLE TYPES - SEE Is_Bindable() - Reb_Value.extra USED FOR BINDING
; ============================================================================


<ANY-CONTEXT!>

    object!     "context of names with values"
                []
                [context     *       *]

    module!     "loadable context of code and data"
                []
                [context     *       *]

    error!      "error context with id, arguments, and stack origin"
                []
                [context     +       +]

    frame!      "arguments and locals of a specific action invocation"
                []
                [frame       +       *]

    port!       "external series, an I/O channel"
                []
                [port        +       context]

</ANY-CONTEXT!>

varargs!    "evaluator position for variable numbers of arguments"
            []
            [varargs     +       +]


<ANY-THE-VALUE!>  ; (order matters, e.g. UNTHEIFY_ANY_XXX_KIND())

    ; Review: Should these be ANY-BRANCH! types?

    the-block!  "alternative inert form of block"
                [any-block! any-array! any-series! any-branch!]
                [array       *       *]

    the-group!  "inert form of group"
                [any-group! any-array! any-series!]
                [array       *       *]

    the-path!   "inert form of path"
                [any-path! any-sequence!]
                [sequence    *       *]

    the-tuple!  "inert form of tuple"
                [any-tuple! any-sequence! any-scalar!]
                [sequence    *       *]

    the-word!   "inert form of word"
                [any-word!]
                [word        *       +]

</ANY-THE-VALUE!>


<ANY-PLAIN-VALUE!>  ; (order matters, e.g. SETIFY_ANY_PLAIN_KIND())

    block!      "array of values that blocks evaluation unless DO is used"
                [any-block! any-array! any-series! any-branch!]
                [array       *       *]

  ; ==========================================================================
  ; BEGIN EVALUATOR ACTIVE TYPES, SEE ANY_EVALUATIVE()
  ; ==========================================================================

    group!      "array that evaluates expressions as an isolated group"
                [any-group! any-array! any-series! any-branch!]
                [array       *       *]

    path!       "member or refinement selection with execution bias"
                [any-path! any-sequence!]
                [sequence    *       *]

    tuple!      "member selection with inert bias"
                [any-tuple! any-sequence! any-scalar!]  ; scalar e.g. ADD 0.0.1
                [sequence    *       *]

    word!       "evaluates a variable or action"
                [any-word!]
                [word        *       +]

</ANY-PLAIN-VALUE!>  ; contiguous with ANY-SET below matters


<ANY-SET-VALUE!>  ; (order matters, e.g. UNSETIFY_ANY_XXX_KIND())

    set-block!  "array of values that will element-wise SET if evaluated"
                [any-block! any-array! any-series!]
                [array       *       *]

    set-group!  "array that evaluates and runs SET on the resulting word/path"
                [any-group! any-array! any-series!]
                [array       *       *]

    set-path!   "definition of a path's value"
                [any-path! any-sequence!]
                [sequence    *       *]

    set-tuple!  "definition of a tuple's value"
                [any-tuple! any-sequence!]
                [sequence    *       *]

    set-word!   "definition of a word's value"
                [any-word!]
                [word        *       +]

</ANY-SET-VALUE!>  ; (contiguous with ANY-GET below matters)


<ANY-GET-VALUE!>  ; (order matters, e.g. UNGETIFY_ANY_XXX_KIND())

    get-block!  "array of values that is reduced if evaluated"
                [any-block! any-array! any-series! any-branch!]
                [array       *       *]

    get-group!  "array that evaluates and runs GET on the resulting word/path"
                [any-group! any-array! any-series!]
                [array       *       *]

    get-path!   "the value of a path"
                [any-path! any-sequence!]
                [sequence    *       *]

    get-tuple!  "the value of a tuple"
                [any-tuple! any-sequence!]
                [sequence    *       *]

    get-word!   "the value of a word (variable)"
                [any-word!]
                [word        *       +]

</ANY-GET-VALUE!>  ; (contiguous with ANY-META below matters)


<ANY-META-VALUE!>  ; (order matters, e.g. UNMETAFY_ANY_XXX_KIND())

    meta-block! "block that evaluates to produce a quoted block"
                [any-block! any-array! any-series! any-branch!]
                [array       *       *]

    meta-group! "group that quotes its product or removes isotope status"
                [any-group! any-array! any-series!]
                [array       *       *]

    meta-path!  "path that quotes its product or removes isotope status"
                [any-path! any-sequence!]
                [sequence    *       *]

    meta-tuple! "tuple that quotes its product or removes isotope status"
                [any-tuple! any-sequence!]
                [sequence    *       *]

    meta-word!  "word that quotes its product or removes isotope status"
                [any-word!]
                [word        *       +]

</ANY-META-VALUE!>


; COMMA! has a high number with bindable types it's evaluative, and the
; desire is to make the ANY_INERT() test fast with a single comparison.

comma!      "separator between full evaluations (that is otherwise invisible)"
            [any-unit!]
            [comma       -       +]


; ACTION! is the "OneFunction" type in Ren-C https://forum.rebol.info/t/596

action!     "an invokable Rebol subroutine"
            [any-branch!]
            [action      +       +]


; ============================================================================
; QUOTED and QUASI "PSEUDOTYPES"
; ============================================================================

; The REB_QUOTED and REB_QUASI enum values never appear in the HEART_BYTE() of
; a cell.  These are synthesized datatypes when the QUOTE_BYTE() contains
; nonzero values.
;
; Neither are inert...QUASI! becomes isotopic when evaluated, and QUOTED!
; removes one level of quoting.

quasi!      "value which evaluates to a form that triggers errors on access"
            []
            [quasi       +       -]

quoted!     "container for arbitrary levels of quoting"
            [any-branch!]
            [quoted       +       -]


; This is the end of the value cell enumerations (after REB_QUOTED is REB_MAX)
; and no valid cell should have bits between REB_QUOTED and REB_MAX.
