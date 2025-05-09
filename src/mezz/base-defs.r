Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Boot Base: Other Definitions"
    rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    license: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    description: {
        This code is evaluated just after actions, natives, sysobj, and
        other lower level definitions. This file intializes a minimal working
        environment that is used for the rest of the boot.
    }
    notes: {
        Any exported SET-WORD!s must be themselves "top level". This hampers
        procedural code here that would like to use tables to avoid repeating
        itself.  This means variadic approaches have to be used that quote
        SET-WORD!s living at the top level, inline after the function call.
    }
]


unset: func [return: [~] word [any-word!]] [set/any word ~]
eval: :evaluate

; Start with basic debugging

c-break-debug: :c-debug-break ;-- easy to mix up

??: ;; shorthand form to use in debug sessions, not intended to be committed
probe: func [
    {Debug print a molded value and returns that same value.}

    return: "Same as the input value"
        [any-atom!]
    value [any-atom!]
][
    case [
        trash? :value [write-stdout "~  ; anti"]
        void? :value [write-stdout "~void~  ; anti"]
        null? :value [write-stdout "~null~  ; anti"]
        okay? :value [write-stdout "~okay~  ; anti"]
        <else> [write-stdout mold :value]
    ]
    write-stdout newline
    return :value
]


; Common "Invisibles"
;
; The mechanic by which invisibility was implemented in this bootstrap EXE
; initially was very complex, and had no bearing on invisibility as it is
; implemented in the new executable.  Given that the bootstrap EXE is only
; used for building the modern EXE, simplicity and fewer bugs is better.
; This just returns VOID, which is good enough to opt out of ANY and ALL.

comment: func [
    {Ignores the argument value, but does no evaluation (see also ELIDE).}

    return: [~void~]
        {The evaluator will skip over the result (not seen, not even void)}
    :discarded [block! any-string! binary! any-scalar!]
        "Literal value to be ignored." ;-- `comment print "hi"` disallowed
][
    return void
]

elide: func [
    {Argument is evaluative, but discarded (see also COMMENT).}

    return: [~void~]
        {The evaluator will skip over the result (not seen, not even void)}
    discarded [any-value! trash!]
        {Evaluated value to be ignored.}
][
    return void
]


; !!! NEXT and BACK seem somewhat "noun-like" and desirable to use as variable
; names, but are very entrenched in Rebol history.  Also, since they are
; specializations they don't fit easily into the NEXT OF SERIES model--this
; is a problem which hasn't been addressed.
;
next: specialize 'skip [
    offset: 1
    only: okay  ; don't clip (return null if already at head of series)
]
back: specialize 'skip [
    offset: -1
    only: okay  ; don't clip (return null if already at tail of series)
]

bound?: cascade [
    specialize 'reflect [property: 'binding]
    :element?
]

unspaced: specialize 'delimit [delimiter: null]
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
            fail/blame "NEWLINED on TEXT! semantics being debated" 'line
        ]
    ]
    func [t [~null~ text!]] [
        if null? t [return null]
        append t newline ;; Terminal newline is POSIX standard, more useful
        return t
    ]
]

an: func [
    {Prepends the correct "a" or "an" to a string, based on leading character}
    return: [text!]
    value <local> s
][
    return head of insert (s: form value) either (find "aeiou" s/1) ["an "] ["a "]
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
    series [<undo-opt> any-series! object! port! bitset! map! blank!]
][
    return any [
        not series
        blank? series
        tail? series
    ]
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
        tester: typechecker (get bind (as word! type-name) binding of set-word)
        set set-word :tester

        set-meta :tester make system/standard/action-meta [
            description: spaced [{Returns TRUE if the value is} an type-name]
            return-type: [logic!]
        ]
    ]
]
    trash?:
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


trashified?: :trash?  ; helps make tests clearer for trashification


print: func [
    {Textually output spaced line (evaluating elements if a block)}

    return: "NULL if blank input or effectively empty block, otherwise trash"
        [~null~ trash!]
    line "Line of text or block, blank or [] has NO output, newline allowed"
        [<opt-out> char! text! block!]
][
    if char? line [
        if not equal? line newline [
            fail "PRINT only allows CHAR! of newline (see WRITE-STDOUT)"
        ]
        return write-stdout line
    ]

    return ((write-stdout maybe spaced line) then [write-stdout newline])
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
    return: [<undo-opt>]
    label [text!]
    'terminal [word!]
][
    assert [equal? terminal '===]
    return void
]


decode-url: ~ ; set in sys init

internal!: make typeset! [
    handle!
]

immediate!: make typeset! [
    ; Does not include internal datatypes
    blank! logic! any-scalar! date! any-word! datatype! typeset! event!
]

; Convenient alternatives for readability
;
neither?: :nand?
both?: :and?
