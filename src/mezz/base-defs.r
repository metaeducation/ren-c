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


unset: func [word [any-word!]] [set/any word ~]


; Start with basic debugging

c-break-debug: :c-debug-break ;-- easy to mix up

??: ;; shorthand form to use in debug sessions, not intended to be committed
probe: func [
    {Debug print a molded value and returns that same value.}

    return: "Same as the input value"
        [~null~ any-value!]
    value [~null~ any-value!]
][
    case [
        nothing? :value [write-stdout "~  ; anti"]
        void? :value [write-stdout "~void~  ; anti"]
        null? :value [write-stdout "~null~  ; anti"]
        true [write-stdout mold :value]
    ]
    write-stdout newline
    :value
]


; Convenience helper for making enfixed functions

set/enfix the enfix: func [
    "Convenience version of SET/ENFIX, e.g `+: enfix :add`"
    return: [~] "`x: y: enfix :z` wouldn't enfix x, so returns void"
    :target [set-word! set-path!]
    action [action!]
][
    set/enfix target :action
]


; Common "Invisibles"

comment: enfix func [
    {Ignores the argument value, but does no evaluation (see also ELIDE).}

    return: []
        {The evaluator will skip over the result (not seen, not even void)}
    #returned [~null~ <end> any-value!]
        {The returned value.} ;-- by protocol of enfixed `return: []`
    :discarded [block! any-string! binary! any-scalar!]
        "Literal value to be ignored." ;-- `comment print "hi"` disallowed
][
]

elide: func [
    {Argument is evaluative, but discarded (see also COMMENT).}

    return: []
        {The evaluator will skip over the result (not seen, not even void)}
    discarded [~null~ any-value!]
        {Evaluated value to be ignored.}
][
]

nihil: enfix func [
    {Arity-0 form of COMMENT}
    return: [] {Evaluator will skip result}
][
]

end: func [
    {Inertly consumes all subsequent data, evaluating to previous result.}

    return: []
    :omit [any-value! <...>]
][
    until [null? take omit]
]

; Note: UNEVAL is done far more elegantly as the new QUOTE in a generalized
; way in mainline.  This exists in R3C prior to arbitrary quoting.
;
uneval: func [
    {Make expression that when evaluated, will produce the input}

    return: {`(null)` if null, or `(the ...)` where ... is passed-in cell}
        [group!]
    optional [~null~ any-value!]
][
    case [
        void? :optional [the (void)]
        null? :optional [the (null)]
        nothing? :optional [the (~)]
        true [as group! reduce ['the :optional]]
    ]
]


; !!! NEXT and BACK seem somewhat "noun-like" and desirable to use as variable
; names, but are very entrenched in Rebol history.  Also, since they are
; specializations they don't fit easily into the NEXT OF SERIES model--this
; is a problem which hasn't been addressed.
;
next: specialize 'skip [
    offset: 1
    only: true ;-- don't clip (return null if already at head of series)
]
back: specialize 'skip [
    offset: -1
    only: true ;-- don't clip (return null if already at tail of series)
]

bound?: cascade [
    specialize 'reflect [property: 'binding]
    :value?
]

unspaced: specialize 'delimit [delimiter: void]
unspaced-text: cascade [
    :unspaced
    specialize 'else [branch: [copy ""]]
]

spaced: specialize 'delimit [delimiter: space]
spaced-text: cascade [
    :spaced
    specialize 'else [branch: [copy ""]]
]

newlined: cascade [
    adapt specialize 'delimit [delimiter: newline] [
        if text? :line [
            fail/where "NEWLINED on TEXT! semantics being debated" 'line
        ]
    ]
    func [t [~null~ text!]] [
        if null? t [return null]
        append t newline ;; Terminal newline is POSIX standard, more useful
    ]
]

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
; series [any-series! port!]
;
; tail?
; {Returns TRUE if series is at or past its end; or empty for other types.}
; series [any-series! object! port! bitset! map! blank! varargs!]
;
; past?
; {Returns TRUE if series is past its end.}
; series [any-series! port!]
;
; open?
; {Returns TRUE if port is open.}
; port [port!]

head?: specialize 'reflect [property: 'head?]
tail?: specialize 'reflect [property: 'tail?]
past?: specialize 'reflect [property: 'past?]
open?: specialize 'reflect [property: 'open?]


empty?: func [
    {TRUE if empty or BLANK!, or if series is at or beyond its tail.}
    return: [logic!]
    series [any-series! object! port! bitset! map! blank! ~void~]
][
    did any [
        void? series
        blank? series
        tail? series
    ]
]


; !!! The bootstrap snapshot was made in the middle of some weird ideas about
; having MAKE OBJECT! be non-evaluative, and CONSTRUCT taking its place as
; an arity-2 object creation function.  It's not a terrible idea, but a part
; of the idea was terrible (namely rendering objects everywhere with a spec
; block, for the purposes of naming fields that didn't have values in the
; body block... a problem solved much better with isotopes/antiforms in
; modern Ren-C).
;
; Because the value hadn't materialized, callsites weren't all converted
; outside the mezzanine to CONSTRUCT...so this little shim for having the
; traditional MAKE OBJECT! stuck around.  It's not worth it to redo the
; MAKE_XXX machinery here in bootstrap to pass in parents (and possibly that's
; actually bad anyway).  So keep the shim.
;
make: enclose 'lib/make func [f] [
    all [
        equal? :f/type object!
        block? :f/def
        not block? f/def/1
    ] then [
        return construct [] f/def
    ]
    if object? :f/type [
        return construct :f/type :f/def
    ]
    do f
]


reeval func [
    {Make fast type testing functions (variadic to quote "top-level" words)}
    return: [~]
    'set-words [tag! set-word! <...>]
    <local>
        set-word type-name tester meta
][
    while [not equal? <end> set-word: take set-words] [
        type-name: copy as text! set-word
        change back tail of type-name "!" ;-- change ? at tail to !
        tester: typechecker (get bind (as word! type-name) set-word)
        set set-word :tester

        set-meta :tester construct system/standard/action-meta [
            description: spaced [{Returns TRUE if the value is} an type-name]
            return-type: [logic!]
        ]
    ]
]
    nothing?:
    void?:
    blank?:
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
    text?:
    file?:
    email?:
    url?:
    tag?:
    bitset?:
    block?:
    group?:
    path?:
    set-path?:
    get-path?:
    lit-path?:
    map?:
    datatype?:
    typeset?:
    action?:
    varargs?:
    object?:
    frame?:
    module?:
    error?:
    port?:
    event?:
    handle?:

    ; Typesets predefined during bootstrap.

    any-string?:
    any-word?:
    any-path?:
    any-context?:
    any-number?:
    any-series?:
    any-scalar?:
    any-list?:
    <end>


; For some reason, the WRITE-STDOUT facility is broken in VSCode terminal for
; longer strings.  It was found that printing in smaller chunks helped.
; Delving into repair of old device model code is not a high priority, since
; it's all ripped out in modern executables.

write-stdout-buggy: :write-stdout
write-stdout: lib/func [value [char! text!] <local> pos] [
    if char? value [
        write-stdout-buggy value
        return ~
    ]
    while [true] [
        write-stdout-buggy copy/part value 256
        if tail? value: skip value 256 [break]
    ]
    return ~
]


print: func [
    {Textually output spaced line (evaluating elements if a block)}

    return: "NULL if blank input or effectively empty block, otherwise trash"
        [~null~ nothing!]
    line "Line of text or block, blank or [] has NO output, newline allowed"
        [<maybe> char! text! block!]
][
    if char? line [
        if not equal? line newline [
            fail "PRINT only allows CHAR! of newline (see WRITE-STDOUT)"
        ]
        return write-stdout line
    ]

    (write-stdout maybe spaced line) then [write-stdout newline]
]


; Simple "divider-style" thing for remarks.  At a certain verbosity level,
; it could dump those remarks out...perhaps based on how many == there are.
; (This is a good reason for retaking ==, as that looks like a divider.)
;
; Only supports strings in bootstrap, because sea of words is not in bootstrap
; executable, so plain words here creates a bunch of variables...could confuse
; the global state more than it already is.
;
===: func [
    ; note: <...> is now a TUPLE!, and : used to be "hard quote" (vs ')
    label [text!]
    'terminal [word!]
][
    assert [equal? terminal '===]
]


decode-url: _ ; set in sys init

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
    value [~null~ any-value!]
][
    not error? :value
]

; Convenient alternatives for readability
;
neither?: :nand?
both?: :and?
