REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Base: Other Definitions"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2019 Ren-C Open Source Contributors
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

c-break-debug: :c-debug-break  ; easy to mix up

lit: :literal  ; because it's shorter

??:  ; shorthand form to use in debug sessions, not intended to be committed
probe: func* [
    {Debug print a molded value and returns that same value.}

    return: "Same as the input value"
        [<opt> any-value!]
    value [<opt> any-value!]
][
    either set? 'value [
        write-stdout mold :value
    ][
        write-stdout "; null"  ; MOLD won't take nulls
    ]
    write-stdout newline
    :value
]


; Give special operations their special properties
;
; !!! There may be a function spec property for these, but it's not currently
; known what would be best for them.  They aren't parameter conventions, they
; apply to the whole action.
;
tweak :else 'defer on
tweak :then 'defer on
tweak :also 'defer on


; ARITHMETIC OPERATORS
;
; Note that `/` is actually path containing two elements, both of which are
; BLANK! so they do not render.  A special mechanism through a word binding
; hidden in the cell allows it to dispatch.  See Startup_Sequence_1_Symbol()

+: enfixed :add
-: enfixed :subtract
*: enfixed :multiply
-slash-1-: enfixed :divide  ; TBD: make `/: enfixed :divide` act equivalently


; SET OPERATORS

not+: :bitwise-not
and+: enfixed :bitwise-and
or+: enfixed :bitwise-or
xor+: enfixed :bitwise-xor
and-not+: enfixed :bitwise-and-not


; COMPARISON OPERATORS
;
; !!! See discussion about the future of comparison operators:
; https://forum.rebol.info/t/349

=: enfixed :equal?
<>: enfixed :not-equal?
<: enfixed :lesser?
>: enfixed :greater?

>=: enfixed :greater-or-equal?
=<: <=: enfixed :equal-or-lesser?  ; https://forum.rebol.info/t/349/11
!=: enfixed :not-equal?  ; http://www.rebol.net/r3blogs/0017.html
==: enfixed :strict-equal?  ; !!! https://forum.rebol.info/t/349
!==: enfixed :strict-not-equal?  ; !!! bad pairing, most would think !=

=?: enfixed :same?


; Common "Invisibles"

comment: enfixed func* [
    {Ignores the argument value, but does no evaluation (see also ELIDE).}

    return: <elide>
        {The evaluator will skip over the result (not seen, not even void)}
    returned [<opt> <end> any-value!]
        {The returned value.}  ; by protocol of enfixed `return: <invisible>`
    :discarded [block! any-string! binary! any-scalar!]
        "Literal value to be ignored."  ; `comment print "hi"` disallowed
][
]

elide: func* [
    {Argument is evaluative, but discarded (see also COMMENT).}

    return: <elide>
        {The evaluator will skip over the result (not seen, not even void)}
    discarded [<opt> any-value!]
        {Evaluated value to be ignored.}
][
]

nihil: enfixed func* [  ; 0-arg so enfix doesn't matter, but tests issue below
    {Arity-0 COMMENT (use to replace an arity-0 function with side effects)}
    return: <elide> {Evaluator will skip result}
][
    ; https://github.com/metaeducation/ren-c/issues/581#issuecomment-562875470
]

; Simple "divider-style" thing for remarks.  At a certain verbosity level,
; it could dump those remarks out...perhaps based on how many == there are.
; (This is a good reason for retaking ==, as that looks like a divider.)
;
===: func* [:remarks [any-value! <variadic>]] [
    until [
        equal? '=== take remarks
    ]
]

; COMMA! is the new expression barrier.  But `||` is included as a redefine of
; the old `|`, so that the barrier-making properties of a usermode entity can
; stay tested.  But outside of testing, use `,` instead.
;
||: func* [
    "Expression barrier - invisible so it vanishes, but blocks evaluation"
    return: <elide>
][
    ; Note: actually *faster* than a native, due to Commenter_Dispatcher()
]

tweak :|| 'barrier on

|||: func* [
    {Inertly consumes all subsequent data, evaluating to previous result.}

    return: <elide>
    :omit [any-value! <variadic>]
][
    until [null? take omit]
]


; !!! While POINTFREE is being experimented with in its design, it is being
; designed in usermode.  It would be turned into an optimized native when it
; is finalized (and when it is comfortably believed a user could have written
; it themselves and had it work properly.)
;
pointfree*: func* [
    {Specialize by example: https://en.wikipedia.org/wiki/Tacit_programming}

    return: [action!]
    action [action!]  ; lower level version takes action AND a block
    block [block!]
    <local> params frame var
][
    ; If we did a GET of a PATH! it will come back as a partially specialized
    ; function, where the refinements are reported as normal args at the
    ; right spot in the evaluation order.  (e.g. GET 'APPEND/DUP returns a
    ; function where DUP is a plain WORD! parameter in the third spot).
    ;
    ; We prune out any unused refinements for convenience.
    ;
    params: map-each w parameters of :action [
        match [word! lit-word! get-word!] w  ; !!! what about skippable params?
    ]

    frame: make frame! get 'action  ; use GET to avoid :action name cache

    ; Step through the block we are given--first looking to see if there is
    ; a BLANK! in the slot where a parameter was accepted.  If it is blank,
    ; then leave the parameter null in the frame.  Otherwise take one step
    ; of evaluation or literal (as appropriate) and put the parameter in the
    ; frame slot.
    ;
    for-skip p params 1 [
        case [
            ; !!! Have to use STRICT-EQUAL?, else '_ says type equal to blank
            blank! == type of :block/1 [block: skip block 1]

            match word! p/1 [
                until [not quoted? block: try evaluate/result block 'var]
                if not block [
                    break  ; ran out of args, assume remaining unspecialized
                ]
                frame/(p/1): :var
            ]

            all [
                match lit-word! p/1
                match [group! get-word! get-path!] :block/1
            ][
                frame/(p/1): reeval :block/1
                block: skip block 1  ; NEXT not defined yet
            ]

            ; Note: DEFAULT not defined yet
            true [  ; hard literal argument or non-escaped soft literal
                frame/(p/1): :block/1
                block: skip block 1  ; NEXT not defined yet
            ]
        ]
    ]

    if :block/1 [
        fail 'block ["Unused argument data at end of POINTFREE block"]
    ]

    ; We now create an action out of the frame.  NULL parameters are taken as
    ; being unspecialized and gathered at the callsite.
    ;
    return make action! :frame
]


; Function derivations have core implementations (SPECIALIZE*, ADAPT*, etc.)
; that don't create META information for the HELP.  Those can be used in
; performance-sensitive code.
;
; These higher-level variations without the * (SPECIALIZE, ADAPT, etc.) do the
; inheritance for you.  This makes them a little slower, and the generated
; functions will be bigger due to having their own objects describing the
; HELP information.  That's not such a big deal for functions that are made
; only one time, but something like a KEEP inside a COLLECT might be better
; off being defined with ENCLOSE* instead of ENCLOSE and foregoing HELP.
;
; Once HELP has been made for a derived function, it can be customized via
; REDESCRIBE.
;
; https://forum.rebol.info/t/1222
;
; Note: ENCLOSE is the first wrapped version here; so that the other wrappers
; can use it, thus inheriting HELP from their core (*-having) implementations.

inherit-meta: func* [
    return: "Same as derived (assists in efficient chaining)"
        [action!]
    derived [action!]
    original [action! word! path!]
    /augment "Additional spec information to scan"
        [block!]
][
    if action! <> reflect :original 'type [original: get original]
    if let m1: meta-of :original [
        set-meta :derived let m2: copy :m1  ; shallow copy
        if in m1 'parameter-notes [  ; shallow copy, but make frame match
            m2/parameter-notes: make frame! :derived
            for-each [key value] :m1/parameter-notes [
                if in m2/parameter-notes key [
                    m2/parameter-notes/(key): :value
                ]
            ]
        ]
        if in m2 'parameter-types [  ; shallow copy, but make frame match
            m2/parameter-types: make frame! :derived
            for-each [key value] :m1/parameter-types [
                if in m2/parameter-types key [
                    m2/parameter-types/(key): :value
                ]
            ]
        ]
    ]
    return get 'derived  ; no :derived name cache
]

enclose: enclose* 'enclose* func* [f] [  ; uses low-level ENCLOSE* to make
    let inner: f/inner: compose :f/inner
    inherit-meta do f get 'inner  ; no :inner name cache
]
inherit-meta :enclose 'enclose*  ; needed since we used ENCLOSE*

specialize: enclose 'specialize* func* [f] [  ; now we have high-level ENCLOSE
    let specializee: f/specializee: compose :f/specializee
    inherit-meta do f get 'specializee  ; no :specializee name cache
]

adapt: enclose 'adapt* func* [f] [
    let adaptee: f/adaptee: compose :f/adaptee
    inherit-meta do f get 'adaptee  ; no :adaptee name cache
]

chain: enclose 'chain* func* [f] [
    ;
    ; !!! Historically CHAIN supported | for "pipe" but it was really just an
    ; expression barrier.  Review this idea, but for now let it work in a
    ; dialect way by replacing with commas.
    ;
    f/pipeline: map-each x f/pipeline [
        either :x = '| [
            ',
        ][
            :x
        ]
    ]

    let pipeline: f/pipeline: reduce :f/pipeline
    inherit-meta do f pick pipeline 1
]

augment: enclose 'augment* func* [f] [
    let augmentee: f/augmentee: compose :f/augmentee
    let spec: :f/spec
    inherit-meta/augment do f get 'augmentee spec  ; no :augmentee name cache
]

; The lower-level pointfree function separates out the action it takes, but
; the higher level one uses a block.  Specialize out the action, and then
; overwrite it in the enclosure with an action taken out of the block.
;
pointfree: enclose (specialize* 'pointfree* [
    action: :panic-value  ; gets overwritten, best to make it something mean
]) func* [f] [
    let action: f/action: (match action! any [
        if match [word! path!] :f/block/1 [get compose f/block/1]
        :f/block/1
    ]) else [
        fail "POINTFREE requires ACTION! argument at head of block"
    ]

    ; rest of block is invocation by example
    f/block: skip f/block 1  ; Note: NEXT not defined yet

    inherit-meta do f get 'action  ; no :action name cache
]


->: enfixed lambda: func* [
    {Convenience wrapper for MAKE ACTION! (Note: does not provide RETURN)}

    return: [action!]
    'args "Block of argument words, or a single word (if one argument)"
        [<end> word! block!]
    body "Block that serves as the body"
        [block!]
][
    make action! reduce [  ; use MAKE ACTION! for no RETURN handling
        blockify dequote args  ; note NULL for <end> produces `[]`
        body
    ]
]

<-: enfixed func* [
    {Declare action by example instantiation, missing args left unspecialized}

    return: [action!]
    :left "Enforces nothing to the left of the pointfree expression"
        [<end>]
    :expression "POINTFREE expression, BLANK!s are unspecialized arg slots"
        [any-value! <...>]
][
    pointfree make block! expression  ; !!! Allow Vararg param for efficiency?
]


; !!! NEXT and BACK seem somewhat "noun-like" and desirable to use as variable
; names, but are very entrenched in Rebol history.  Also, since they are
; specializations they don't fit easily into the NEXT OF SERIES model--this
; is a problem which hasn't been addressed.
;
next: specialize 'skip [offset: 1]
back: specialize 'skip [offset: -1]

bound?: chain [specialize 'reflect [property: 'binding] | :value?]

unspaced: adapt specialize 'delimit [delimiter: "null-me"] [delimiter: null]
unspaced-text: chain [:unspaced | specialize 'else [branch: [copy ""]]]

spaced: specialize 'delimit [delimiter: space]
spaced-text: chain [:spaced | specialize 'else [branch: [copy ""]]]

newlined: chain [
    adapt specialize 'delimit [delimiter: newline] [
        if text? :line [
            fail 'line "NEWLINED on TEXT! semantics being debated"
        ]
    ]
        |
    func* [t [<opt> text!]] [
        if null? t [return null]
        append t newline  ; Terminal newline is POSIX standard, more useful
    ]
]

an: func* [
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


empty?: func* [
    {TRUE if empty or BLANK!, or if series is at or beyond its tail.}
    return: [logic!]
    series [any-series! object! port! bitset! map! blank!]
][
    did any [blank? series, tail? series]
]


reeval func* [
    {Make fast type testing functions (variadic to quote "top-level" words)}
    return: <void>
    'set-words [<variadic> set-word! tag!]
    <local>
        set-word type-name tester meta
][
    while [<end> != set-word: take set-words] [
        type-name: copy as text! set-word
        change back tail of type-name "!"  ; change ? at tail to !
        tester: typechecker (get bind (as word! type-name) set-word)
        set set-word :tester

        set-meta :tester make system/standard/action-meta [
            description: spaced [{Returns TRUE if the value is} an type-name]
            return-type: [logic!]
        ]
    ]
]
    void?:
    blank?:
    comma?:
    logic?:
    integer?:
    decimal?:
    percent?:
    money?:
    pair?:
    tuple?:
    time?:
    date?:
    word?:
    set-word?:
    get-word?:
    sym-word?:
    issue?:
    binary?:
    text?:
    file?:
    email?:
    url?:
    tag?:
    bitset?:
    path?:
    set-path?:
    get-path?:
    sym-path?:
    block?:
    set-block?:
    get-block?:
    sym-block?:
    group?:
    get-group?:
    set-group?:
    sym-group?:
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
    any-array?:
    <end>


; Note: `LIT-WORD!: UNEVAL WORD!` and `LIT-PATH!: UNEVAL PATH!` is actually
; set up in %b-init.c.  Also LIT-WORD! and LIT-PATH! are handled specially in
; %words.r for bootstrap compatibility as a parse keyword.

lit-word?: func* [value [<opt> any-value!]] [
    did all [
        quoted? :value
        word! = type of unquote value
    ]
]
to-lit-word: func* [value [any-value!]] [
    quote to word! dequote :value
]
lit-path?: func* [value [<opt> any-value!]] [
    did all [
        quoted? :value
        path! = type of unquote value
    ]
]
to-lit-path: func* [value [any-value!]] [
    quote to path! dequote :value
]

refinement?: func* [value [<opt> any-value!]] [
    did all [
        path? :value
        2 = length of value
        blank? :value/1
        word? :value/2
    ]
]

char?: func* [value [<opt> any-value!]] [
    did all [
        issue? :value
        1 >= length of value
    ]
]

print: func* [
    {Textually output spaced line (evaluating elements if a block)}

    return: "NULL if blank input or effectively empty block, otherwise VOID!"
        [<opt> void!]
    line "Line of text or block, blank or [] has NO output, newline allowed"
        [<blank> char! text! block!]
][
    if char? line [
        if line <> newline [
            fail "PRINT only allows CHAR! of newline (see WRITE-STDOUT)"
        ]
        return write-stdout line
    ]

    (write-stdout try spaced line) then @[write-stdout newline]
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

ok?: func* [
    "Returns TRUE on all values that are not ERROR!"
    value [<opt> any-value!]
][
    not error? :value
]

; Convenient alternatives for readability
;
neither?: :nand?
both?: :and?
