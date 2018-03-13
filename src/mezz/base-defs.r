REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Base: Other Definitions"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        This code is evaluated just after actions, natives, sysobj, and
        other lower level definitions. This file intializes a minimal working
        environment that is used for the rest of the boot.
    }
    Note: {
        Any exported SET-WORD!s must be themselves "top level". This hampers
        procedural code here that would like to use tables to avoid repeating
        itself.  This means variadic approaches have to be used that quote
        SET-WORD!s living at the top level, inline after the function call.
    }
]

; Easy to mistype, so alias it to avoid an even more painful debug session (!)
;
c-break-debug: :c-debug-break


; Words for BLANK! and BAR!, for those who don't like symbols

blank: _
bar: '|


; Some find UNLESS confusing, so IF-NOT is a synonym.  NOT binds with the IF.
; So `if-not x and (y)` => `if not (x and (y))` vs `if (not x) and (y)`
;
if-not: :unless
while-not: :until


; COMMENT has a relatively complex-looking definition because it seeks to be
; "truly invisible".  This means that it can't disrupt the flow of evaluation,
; it must run eagerly--as if it were an enfix tight operation that pipes its
; left argument directly to the output.  (Even if it's an <end>, which is not
; technically possible for normal operations, but the `return []` plus being
; enfix cues the evaluator to do this.)
;
; Internal optimizations make it so COMMENT doesn't need to be a native...the
; empty body triggers an "eliding noop dispatcher".  You can only comment out
; inert types (e.g. no `comment print "hi"`) to avoid looking deceptive.
;
set/enfix quote comment: func [
    {Ignores the argument value, but does no evaluation (see also ELIDE).}

    return: []
        {The evaluator will skip over the result (not seen, not even void)}
    #returned [<opt> <end> any-value!]
        {By protocol of `return: []`, this is the return value when enfixed}
    :discarded [block! any-string! binary! any-scalar!]
        "Literal value to be ignored."
][
    ; no body
]

elide: func [
    {Argument is evaluative, but discarded (see also COMMENT).}

    return: []
        {The evaluator will skip over the result (not seen, not even void)}
    discarded [<opt> any-value!]
        {Evaluated value to be ignored.}
][
    ; no body
]

end: func [
    {Inertly consumes all subsequent data, evaluating to previous result.}

    return: []
    :omit [<opt> any-value! <...>]
][
    until [tail? omit] [take omit]
]


; Despite being very "noun-like", HEAD and TAIL have classically been "verbs"
; in Rebol.  Ren-C builds on the concept of REFLECT, so that REFLECT STR 'HEAD
; will get the head of a string.  An enfix left-soft-quoting operation is
; introduced called OF, so that you can write HEAD OF STR and get the same
; ultimate effect.
;
set/enfix quote of: func [ ;-- NOTE can't be (quote of:), OF: top-level...
    'property [word!]
    value [<opt> any-value!] ;-- TYPE OF () needs to be BLANK!, so <opt> okay
][
    reflect :value property
]

; !!! NEXT and BACK seem somewhat "noun-like" and desirable to use as variable
; names, but are very entrenched in Rebol history.  Also, since they are
; specializations they don't fit easily into the NEXT OF SERIES model--this
; is a problem which hasn't been addressed.
;
next: specialize 'skip [
    offset: 1
    only: true ;-- don't clip (return BLANK! if already at head of series)
]
back: specialize 'skip [
    offset: -1
    only: true ;-- don't clip (return BLANK! if already at tail of series)
]

unspaced: specialize 'delimit [delimiter: blank]
spaced: specialize 'delimit [delimiter: space]


; !!! REDESCRIBE not defined yet
;
; head?
; {Returns TRUE if a series is at its beginning.}
; series [any-series! gob! port!]
;
; tail?
; {Returns TRUE if series is at or past its end; or empty for other types.}
; series [any-series! object! gob! port! bitset! map! blank! varargs!]
;
; past?
; {Returns TRUE if series is past its end.}
; series [any-series! gob! port!]
;
; open?
; {Returns TRUE if port is open.}
; port [port!]

head?: specialize 'reflect [property: 'head?]
tail?: specialize 'reflect [property: 'tail?]
past?: specialize 'reflect [property: 'past?]
open?: specialize 'reflect [property: 'open?]


eval proc [
    {Make type testing functions (variadic to quote "top-level" words)}
    'set-word... [set-word! <...>]
    <local>
        set-word type-name tester meta
][
    while [any-value? set-word: take* set-word...] [
        type-name: append (head of clear find (spelling-of set-word) {?}) "!"
        tester: typechecker (get bind (to word! type-name) set-word)
        set set-word :tester

        ; The TYPECHECKER generator doesn't have make meta information by
        ; default, so it leaves it up to the user code.  Note REDESCRIBE is
        ; not defined yet, so this just makes the meta object directly.
        ;
        meta: copy system/standard/function-meta
        meta/description: form reduce [
            {Returns TRUE if the value is a} type-name
        ]
        meta/return-type: [logic!]
        set-meta :tester meta
    ]
]
    ; This list consumed by the variadic evaluation, up to the | barrier
    ; Each makes a specialization, `XXX: TYPECHECKER XXX!`.  A special
    ; generator is used vs. something like a specialization of a HAS-TYPE?
    ; function...because the generated dispatcher can be more optimized...
    ; and type checking is quite common.
    ;
    blank?:
    bar?:
    lit-bar?:
    logic?:
    integer?:
    decimal?:
    percent?:
    money?:
    char?:
    pair?:
    tuple?:
    time?:
    date?:
    word?:
    set-word?:
    get-word?:
    lit-word?:
    refinement?:
    issue?:
    binary?:
    string?:
    file?:
    email?:
    url?:
    tag?:
    bitset?:
    image?:
    vector?:
    block?:
    group?:
    path?:
    set-path?:
    get-path?:
    lit-path?:
    map?:
    datatype?:
    typeset?:
    function?:
    varargs?:
    object?:
    frame?:
    module?:
    error?:
    port?:
    gob?:
    event?:
    handle?:
    struct?:
    library?:

    ; These typesets are predefined during bootstrap.  REDESCRIBE is not
    ; defined yet, so decide if it's worth it to add descriptions later
    ; e.g. [{Return TRUE if value is } summary {.}]

    any-string?: ;-- "any type of string"
    any-word?: ;-- "any type of word"
    any-path?: ;-- "any type of path"
    any-context?: ;-- "any type of context"
    any-number?: ;-- "a number (integer or decimal)"
    any-series?: ;-- "any type of series"
    any-scalar?: ;-- "any type of scalar"
    any-array?: ;-- "a series of Rebol values"
|


print: proc [
    "Textually output value (evaluating elements if a block), adds newline"

     value [any-value!]
          "Value or BLOCK! literal (BLANK! means print nothing)"
     /eval
          "Allow value to be a block and evaluated (even if not literal)"
    <local> eval_PRINT ;quote_PRINT
][
    eval_PRINT: eval
    eval: :lib/eval

    if blank? :value [leave]

    write-stdout case [
        not block? value [
            form :value
        ]

        eval_PRINT or (semiquoted? 'value) [
            spaced value
        ]
    ] else [
        fail "PRINT called on non-literal block without /EVAL switch"
    ]

    write-stdout newline
]

print-newline: specialize 'write-stdout [value: newline]


; PROBE is a good early function to have handy for debugging all the rest (!)
;
probe: func [
    {Debug print a molded value and returns that same value.}
    return: [<opt> any-value!]
        {Same as the input value.}
    value [<opt> any-value!]
        {Value to display.}
][
    if set? 'value [
        print mold :value
    ] else [
        print "!!! PROBE of void !!!!" ;-- MOLD won't take voids
    ]
    :value
]


decode-url: _ ; set in sys init

r3-legacy*: _ ; set in %mezz-legacy.r

; used only by Ren-C++ as a test of how to patch the lib context prior to
; boot at the higher levels.
test-rencpp-low-level-hook: _

internal!: make typeset! [
    handle!
]

immediate!: make typeset! [
    ; Does not include internal datatypes
    blank! logic! any-scalar! date! any-word! datatype! typeset! event!
]

ok?: func [
    "Returns TRUE on all values that are not ERROR!"
    value [<opt> any-value!]
][
    not error? :value
]

; Convenient alternatives for readability
;
neither?: :nand?
both?: :and?
