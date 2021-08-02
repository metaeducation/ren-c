REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Rebol datatypes and their related attributes"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        This table is used to make C defines and intialization tables.

        !!! REVIEW IMPACT ON %sys-ordered.h ANY TIME YOU CHANGE THE ORDER !!!

        name        - name of datatype (generates words)
        description - short statement of type's purpose (used by HELP)
        class       - how "generic" actions are dispatched (T_type)
        path        - it supports various path forms
        make        - it can be made with #[datatype] method
        mold        - code implementing both MOLD and FORM (hook gets a flag)
        typesets    - what typesets the type belongs to

        What is in the table can be `+` to mean the method exists and has the
        same name as the type (e.g. MF_Blank() if type is BLANK!)

        If it is `*` then the method uses a common dispatcher for the type,
        so (e.g. PD_Array()) even if the type is BLOCK!)

        If it is `?` then the method is loaded dynamically by an extension,
        and unavailable otherwise and uses e.g. T_Unhooked()

        If it is `-` then it is not available at all, and will substitute with
        an implementation that fails, e.g. CT_Fail()

        If it is 0 then it really should not happen, ever--so just null is
        used to generate an exception (for now).

        Note that if there is `somename` in the class column, that means you
        will find the ACTION! dispatch for that type in `REBTYPE(Somename)`.
    }
]


[name       description
            class       path    make    mold    typesets]  ; makes TS_XXX

; REB_0_END is an array terminator, and not a "type".  It has the 0 as part
; of the name to indicate its zero-ness and C falsey-ness is intrinsic to the
; design--huge parts of the system would not work if it were not zero.  Plus,
; making it a leading digit means it's an invalid word, catching if anything
; leaks it to being an actual word (it can't have a symbol, due to SYM_0
; having other purposes in the system).

#0-end      "!!! `END!` isn't a datatype, this isn't exposed to the user"
            0           0       0       0       []

; REB_NULL takes value 1, but it being 1 is less intrinsic.  It is also not
; a "type"...but it is falsey, hence it has to be before LOGIC! in the table.
; In the API, a cell isn't used but it is transformed into the language NULL.
; To help distinguish it from C's NULL in places where it is undecorated,
; functions are given names like `Init_Nulled` or `IS_NULLED()`, but the
; type itself is simply called REB_NULL...which is distinct enough.

#null       "!!! `NULL!` isn't a datatype, `null` can't be stored in blocks"
            0           0       0       +       []

blank       "placeholder unit type which acts as conditionally false"
            blank       +       -       +       [unit branch]

; <ANY-SCALAR>

logic       "boolean true or false"
            logic       -       +       +       []

; ============================================================================
; BEGIN TYPES THAT ARE ALWAYS "TRUTHY" - IS_TRUTHY()/IS_CONDITIONALLY_TRUE()
; ============================================================================

#bytes      "!!! `BYTES!` isn't a datatype, `heart` type  for optimizations"
            0           0       0       0       []

decimal     "64bit floating point number (IEEE standard)"
            decimal     -       *       +       [number scalar]

percent     "special form of decimals (used mainly for layout)"
            decimal     -       *       +       [number scalar]

money       "high precision decimals with denomination (opt)"
            money       -       +       +       [scalar]

time        "time of day or duration"
            time        +       +       +       [scalar]

date        "day, month, year, time of day, and timezone"
            date        +       +       +       []

integer     "64 bit integer"
            integer     -       +       +       [number scalar]


; ============================================================================
; BEGIN TYPES THAT NEED TO BE GC-MARKED
; ============================================================================
;
; !!! Note that INTEGER! may become arbitrary precision, and thus could have
; a node in it to mark in some cases.

pair        "two dimensional point or size"
            pair        +       +       +       [scalar]

; </ANY_SCALAR>

datatype    "type of datatype"
            datatype    -       +       +       []

typeset     "set of datatypes"
            typeset     -       +       +       []

bitset      "set of bit flags"
            bitset      +       +       +       []

map         "name-value pairs (hash associative)"
            map         +       +       +       []

handle      "arbitrary internal object or value"
            handle      -       -       +       []


; This table of fundamental types is intended to be limited (less than
; 64 entries).  Yet there can be an arbitrary number of extension types.
; Those types use the REB_CUSTOM cell class, and give up their ->extra field
; of their cell instances to point to their specific datatype.
;
; Exceptions may be permitted.  As an example, EVENT! is implemented in an
; extension (because not all builds need it).  But it reserves a type byte
; and fills in its entry in this table when it is loaded (hence `?`)

custom      "instance of an extension-defined type"
            -           -       -       -       []

event       "user interface event"  ; %extensions/event/README.md
            ?           ?       ?       ?       []


; <BINARY>
;
;     (...we continue along in order with more ANY-SERIES! types...)
;     (...BINARY! is alone, it's not an ANY-STRING!, just an ANY-SERIES!...)

binary      "series of bytes"
            binary      *       *       +       [series]  ; not an ANY-STRING!


; </BINARY> (adjacent to ANY-STRING matters)
;
; <ANY-STRING> (order does not currently matter)

text        "text string series of characters"
            string      *       *       *       [series string]

file        "file name or path"
            string      *       *       *       [series string]

email       "email address"
            string      *       *       *       [series string]

url         "uniform resource locator or identifier"
            string      *       *       *       [series string]

tag         "markup string (HTML or XML)"
            string      *       *       *       [series string]

issue       "immutable codepoint or codepoint sequence"
            issue       *       *       *       []  ; !!! sequence of INTEGER?

; </ANY-STRING>


; ============================================================================
; BEGIN BINDABLE TYPES - SEE Is_Bindable() - Reb_Value.extra USED FOR BINDING
; ============================================================================


; <ANY-CONTEXT>

object      "context of names with values"
            context     *       *       *       [context]

module      "loadable context of code and data"
            context     *       *       *       [context]

error       "error context with id, arguments, and stack origin"
            context     *       +       +       [context]

frame       "arguments and locals of a specific action invocation"
            context     *       +       *       [context]

port        "external series, an I/O channel"
            port        context +       context [context]

; </ANY-CONTEXT>

varargs     "evaluator position for variable numbers of arguments"
            varargs     +       +       +       []


; <ANY-THE> (order matters, see UNTHEIFY_ANY_XXX_KIND())

the-block   "alternative inert form of block"
            array       *       *       *       [block array series branch]

the-group   "inert form of group"
            array       *       *       *       [group array series branch]

the-path    "inert form of path"
            sequence    *       *       *       [path sequence]

the-tuple   "inert form of tuple"
            sequence    *       *       *       [tuple sequence scalar]

the-word    "inert form of word"
            word        -       *       +       [word]

; </ANY-THE>


; <ANY-PLAIN> (order matters, see UNSETIFY_ANY_XXX_KIND())

block       "array of values that blocks evaluation unless DO is used"
            array       *       *       *       [block array series branch]

; ============================================================================
; BEGIN EVALUATOR ACTIVE TYPES, SEE ANY_EVALUATIVE()
; ============================================================================

group       "array that evaluates expressions as an isolated group"
            array       *       *       *       [group array series branch]

path        "member or refinement selection with execution bias"
            sequence    *       *       *       [path sequence]

tuple       "member selection with inert bias"
            sequence    *       *       *       [tuple sequence scalar]

word        "evaluates a variable or action"
            word        -       *       +       [word]

; </ANY-PLAIN>


; <ANY-SET> (order matters, see UNSETIFY_ANY_XXX_KIND())

set-block   "array of values that will element-wise SET if evaluated"
            array       *       *       *       [block array series]

set-group   "array that evaluates and runs SET on the resulting word/path"
            array       *       *       *       [group array series]

set-path    "definition of a path's value"
            sequence    *       *       *       [path sequence]

set-tuple   "definition of a tuple's value"
            sequence    *       *       *       [tuple sequence]

set-word    "definition of a word's value"
            word        -       *       +       [word]

; </ANY-SET> (contiguous with ANY-GET below matters)


; <ANY-GET> (order matters)

get-block   "array of values that is reduced if evaluated"
            array       *       *       *       [block array series]

get-group   "array that evaluates and runs GET on the resulting word/path"
            array       *       *       *       [group array series]

get-path    "the value of a path"
            sequence    *       *       *       [path sequence branch]

get-tuple   "the value of a tuple"
            sequence    *       *       *       [tuple sequence]

get-word    "the value of a word (variable)"
            word        -       *       +       [word branch]

; </ANY-GET> (except for ISSUE!)


; Right now there's no particularly fast test for ANY_ARRAY(), ANY_PATH(),
; ANY_WORD()...due to those being less common than testing for ANY_INERT().
; Review the decision.

; <ANY-META> (order matters, see UNSETIFY_ANY_XXX_KIND())

meta-block  "block that evaluates to produce a quoted block"
            array       *       *       *       [block array series branch]

meta-group  "group that quotes its product or removes isotope status"
            array       *       *       *       [group array series branch]

meta-path   "path that quotes its product or removes isotope status"
            sequence    *       *       *       [path sequence branch]

meta-tuple  "tuple that quotes its product or removes isotope status"
            sequence    *       *       *       [tuple sequence branch]

meta-word   "word that quotes its product or removes isotope status"
            word        -       *       +       [word branch]

; <ANY-META> (order matters, see UNSETIFY_ANY_XXX_KIND())


; META! is just the lone ^ symbol, which acts like QUOTE, but with the ability
; to pick up on the isotope/invisible distinctions.

meta        "quoting operator which distinguishes NULL and BAD-WORD! isotopes"
            meta        -       -       +       [unit]


; THE! is the lone @ symbol, which acts like THE.  It's particularly nice to
; have for use in the API, for writing `rebDid("action? @", var)` instead of
; needing to say `rebDid("action?" rebQ(var))`.

the         "as-is operator which suppresses evaluation on the next value"
            the         -       -       +       [unit]


; COMMA! has a high number with bindable types it's evaluative, and the
; desire is to make the ANY_INERT() test fast with a single comparison.

comma       "separator between full evaluations (that is otherwise invisible)"
            comma       -       -       +       [unit]


; ACTION! is the "OneFunction" type in Ren-C https://forum.rebol.info/t/596

action      "an invokable Rebol subroutine"
            action      +       +       +       [branch]


; BAD-WORD! is not inert, because it needs to become "unfriendly" when it is
; evaluated.
;
; !!! Because it does not have a binding, it is not an actual WORD!.  There
; could be questions about whether it should be more wordlike, or if there
; should be BAD-BLOCK! ~[]~ and it should fit into a bigger set of types :-/

bad-word    "value which evaluates to a form that triggers errors on access"
            bad-word     -       +       +       []


; ============================================================================
; BEGIN QUOTED RANGE
; ============================================================================
;
; QUOTED! claims "bindable", but null binding if containing an unbindable type
; (is last basic type so that >= REB_QUOTED can also catch all in-situ quotes,
; which use the value + REB_64, + REB_64*2, or + REB_64*3)

quoted     "container for arbitrary levels of quoting"
            quoted       +       +       -      [branch]


; This is the end of the value cell enumerations (after REB_QUOTED is REB_MAX)
; and no valid cell should have bits between REB_QUOTED and REB_MAX.
;
; But after the 64 case, the remaining 196 possibilities in the byte are used
; for an optimization that allows up to three levels of quote optimization to
; fit into a single byte.
