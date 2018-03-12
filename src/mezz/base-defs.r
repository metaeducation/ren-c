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

; Start with basic debugging

c-break-debug: :c-debug-break ;-- easy to mix up

probe: func [
    {Debug print a molded value and returns that same value.}
    return: [<opt> any-value!]
        {Same as the input value.}
    value [<opt> any-value!]
        {Value to display.}
][
    either set? 'value [
        write-stdout mold :value
    ][
        write-stdout "!!! PROBE of void !!!!" ;-- MOLD won't take voids
    ]
    write-stdout newline
    :value
]


; Words for BLANK! and BAR! and void, for those who don't like symbols

blank: _
bar: '|

void: func [
    "Function returning no result (alternative for `()`)"
    return: [<opt>]
][
] ;-- No body runs just as fast as a no-op native, see Noop_Dispatcher()


; Convenience helper for making enfixed functions

set/enfix quote enfix: proc [ ;-- `x: y: enfix :z` wouldn't enfix x
    "Convenience version of SET/ENFIX, e.g `+: enfix :add`"
    :target [set-word! set-path!]
    action [function!]
][
    set/enfix target :action
]


; Common "Invisibles"

comment: enfix func [
    {Ignores the argument value, but does no evaluation (see also ELIDE).}

    return: []
        {The evaluator will skip over the result (not seen, not even void)}
    #returned [<opt> <end> any-value!]
        {The returned value.} ;-- by protocol of enfixed `return: []`
    :discarded [block! any-string! binary! any-scalar!]
        "Literal value to be ignored." ;-- `comment print "hi"` disallowed
][
]

elide: func [
    {Argument is evaluative, but discarded (see also COMMENT).}

    return: []
        {The evaluator will skip over the result (not seen, not even void)}
    discarded [<opt> any-value!]
        {Evaluated value to be ignored.}
][
]

end: func [
    {Inertly consumes all subsequent data, evaluating to previous result.}

    return: []
    :omit [<opt> any-value! <...>]
][
    while-not [tail? omit] [take omit]
]


; Some find UNLESS confusing, so IF-NOT is a synonym.  NOT binds with the IF.
; So `if-not x and (y)` => `if not (x and (y))` vs `if (not x) and (y)`
;
if-not: :unless


; Despite being very "noun-like", HEAD and TAIL have classically been "verbs"
; in Rebol.  Ren-C builds on the concept of REFLECT, so that REFLECT STR 'HEAD
; will get the head of a string.  An enfix left-soft-quoting operation is
; introduced called OF, so that you can write HEAD OF STR and get the same
; ultimate effect.
;
of: enfix func [
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

an: func [
    {Prepends the correct "a" or "an" to a string, based on leading character}
    value <local> s
][
    head of insert (s: form value) either (find "aeiou" s/1) ["an "] ["a "]
]


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
    {Make fast type testing functions (variadic to quote "top-level" words)}
    'set-word... [set-word! <...>]
    <local>
        set-word type-name tester meta
][
    while [value? set-word: take* set-word...] [
        type-name: append (head of clear find (spelling-of set-word) {?}) "!"
        tester: typechecker (get bind (to word! type-name) set-word)
        set set-word :tester

        set-meta :tester construct system/standard/function-meta [
            description: spaced [{Returns TRUE if the value is} an type-name]
            return-type: [logic!]
        ]
    ]
]
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

    ; Typesets predefined during bootstrap.

    any-string?:
    any-word?:
    any-path?:
    any-context?:
    any-number?:
    any-series?:
    any-scalar?:
    any-array?:
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

    write-stdout identity case [
        not block? value [
            form :value
        ]

        eval_PRINT or (semiquoted? 'value) [
            spaced value
        ]
    ] else [
        fail/where
            "PRINT called on non-literal block without /EVAL switch"
            'value
    ]

    write-stdout newline
]

print-newline: specialize 'write-stdout [value: newline]


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
