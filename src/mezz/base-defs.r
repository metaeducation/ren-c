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

probe: func* [
    {Debug print a molded value and returns that same value.}

    return: "Same as the input value"
        [<opt> any-value!]
    ^value' [<opt> any-value!]
][
    ; Remember this is early in the boot so many things not defined
    write-stdout switch type of value' [
        null ["; null"]
        bad-word! [unspaced [value' space space "; isotope"]]
    ] else [
        let value: unquote value'
        switch type of :value [
            null ["; heavy null"]
            bad-word! [mold value]  ; friendly
        ] else [
            mold :value
        ]
    ]

    write-stdout newline

    ; We need to wait until the last minute and unquote the original value
    ; because ~null~ isotopes decay if assigned to a variable.
    ;
    return/isotope unmeta value'
]

??: :probe  ; shorthand to use in debug sessions, not intended to be committed


; Give special operations their special properties
;
; !!! There may be a function spec property for these, but it's not currently
; known what would be best for them.  They aren't parameter conventions, they
; apply to the whole action.
;
tweak :else 'defer on
tweak :then 'defer on
tweak :also 'defer on


; The pattern `foo: enfix function [...] [...]` is probably more common than
; enfixing an existing function, e.g. `foo: enfix :add`.  Hence making a
; COPY of the ACTION! identity is probably a waste.  It may be better to go
; with mutability-by-default, so `foo: enfix copy :add` would avoid the
; mutation.  However, it could also be that the function spec dialect gets
; a means to specify enfixedness.  See:
;
; https://forum.rebol.info/t/moving-enfixedness-back-into-the-action/1156
;
enfixed: chain* reduce [:copy, :enfix]


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

; "Official" forms of the comparison operators.  This is what we would use
; if starting from scratch, and didn't have to deal with expectations people
; have coming from other languages: https://forum.rebol.info/t/349/
;
>=: enfixed :greater-or-equal?
=<: enfixed :equal-or-lesser?

; Compatibility Compromise: sacrifice what looks like left and right arrows
; for usage as comparison, even though the perfectly good `=<` winds up
; being unused as a result.  Compromise `=>` just to reinforce what is lost
; by not retraining: https://forum.rebol.info/t/349/11
;
equal-or-greater?: :greater-or-equal?
lesser-or-equal?: :equal-or-lesser?
=>: enfixed :equal-or-greater?
<=: enfixed :lesser-or-equal?

!=: enfixed :not-equal?  ; http://www.rebol.net/r3blogs/0017.html
==: enfixed :strict-equal?  ; !!! https://forum.rebol.info/t/349
!==: enfixed :strict-not-equal?  ; !!! bad pairing, most would think !=

=?: enfixed :same?


; Common "Invisibles"

comment: enfixed func* [
    {Ignores the argument value, but does no evaluation (see also ELIDE)}

    return: "Evaluator will skip over the result (not seen)"
        <void>
    ^returned "The returned value"  ; by protocol of enfixed `return: <void>`
        [<opt> <end> any-value!]
    :discarded "Literal value to be ignored."  ; `comment print "x"` disallowed
        [block! any-string! binary! any-scalar!]
][
]

elide: func* [
    {Argument is evaluative, but discarded (see also COMMENT).}

    return: "The evaluator will skip over the result (not seen)"
        <void>
    ^discarded "Evaluated value to be ignored"
        [<opt> any-value!]
][
]

void: enfixed func* [  ; 0-arg so enfix doesn't matter, but tests issue below
    {Arity-0 COMMENT (use to replace an arity-0 function with side effects)}
    return: <void> {Evaluator will skip result}
][
    ; https://github.com/metaeducation/ren-c/issues/581#issuecomment-562875470
]

; Simple "divider-style" thing for remarks.  At a certain verbosity level,
; it could dump those remarks out...perhaps based on how many == there are.
; (This is a good reason for retaking ==, as that looks like a divider.)
;
===: func* [
    return: <void>
    'remarks [any-value! <variadic>]
] [
    until [
        equal? '=== take remarks
    ]
    return  ; return no value (invisible)
]

; COMMA! is the new expression barrier.  But `||` is included as a redefine of
; the old `|`, so that the barrier-making properties of a usermode entity can
; stay tested.  But outside of testing, use `,` instead.
;
||: func* [
    "Expression barrier - invisible so it vanishes, but blocks evaluation"
    return: <void>
][
    ; Note: actually *faster* than a native, due to Commenter_Dispatcher()
]

tweak :|| 'barrier on

|||: func* [
    {Inertly consumes all subsequent data, evaluating to previous result.}

    return: <void>
    :omit [any-value! <variadic>]
][
    until [null? take omit]
]


; MATCH isn't always used with ELSE and THEN.  So its result can trigger false
; negatives on matches when CASE clauses and `IF MATCH` when the matched value
; returned is falsey.  By default, make falsey matches give ~falsey~ isotope.
;
match: :match*/safe

; EACH will ultimately be a generator, but for now it acts as QUOTE so it can
; be used with `map x each [a b c] [...]` and give you x as a, then b, then c.
;
each: :quote

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
        fail @block ["Unused argument data at end of POINTFREE block"]
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
;
; (A usermode INHERIT-META existed, but it was very slow.  It was made native.)
;
; https://forum.rebol.info/t/performance-of-inherit-meta/1619
;

enclose: enclose* :enclose* func* [f] [  ; uses low-level ENCLOSE* to make
    let inner: get 'f/inner
    inherit-meta do f 'inner
]
inherit-meta :enclose 'enclose*  ; needed since we used ENCLOSE*

specialize: enclose :specialize* func* [f] [  ; now we have high-level ENCLOSE
    let action: get 'f/action
    inherit-meta do f 'action
]

adapt: enclose :adapt* func* [f] [
    let action: get 'f/action
    inherit-meta do f 'action
]

chain: enclose :chain* func* [f] [
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

    let pipeline1: pick (f/pipeline: reduce :f/pipeline) 1
    inherit-meta do f 'pipeline1
]

augment: enclose :augment* func* [f] [
    let action: get 'f/action
    let spec: :f/spec
    inherit-meta/augment do f 'action spec
]

reframer: enclose :reframer* func* [f] [
    let shim: get 'f/shim
    inherit-meta do f 'shim
]

reorder: enclose :reorder* func* [f] [
    let action: get 'f/action
    inherit-meta do f 'action
]

; !!! The native R3-Alpha parse functionality doesn't have parity with UPARSE's
; ability to synthesize results, but it will once it is re-engineered to match
; UPARSE's design when it hardens.  For now these routines provide some amount
; of interface parity with UPARSE.
;
parse: :parse*/fully  ; could be more complex definition (UPARSE is!)
parse?: chain [:parse*/fully | :then?]
match-parse: enclose :parse func* [f] [
    let input: f.input
    do f then [input]
]

; The lower-level pointfree function separates out the action it takes, but
; the higher level one uses a block.  Specialize out the action, and then
; overwrite it in the enclosure with an action taken out of the block.
;
pointfree: specialize* (enclose :pointfree* func* [f] [
    let action: f/action: (match action! any [
        if match [word! path!] :f/block/1 [get compose f/block/1]
        :f/block/1
    ]) else [
        fail "POINTFREE requires ACTION! argument at head of block"
    ]

    ; rest of block is invocation by example
    f/block: skip f/block 1  ; Note: NEXT not defined yet

    inherit-meta do f 'action  ; no :action name cache
])[
    action: :panic/value  ; gets overwritten, best to make it something mean
]


; REQUOTE is helpful when functions do not accept QUOTED! values.
;
requote: reframer func* [
    {Remove Quoting Levels From First Argument and Re-Apply to Result}
    f [frame!]
    <local> p num-quotes result
][
    if not p: first parameters of action of f [
        fail ["REQUOTE must have an argument to process"]
    ]

    num-quotes: quotes of f/(p)

    f/(p): noquote f/(p)

    do f then result -> [
        quote/depth get/any 'result num-quotes
    ]
]


->: enfixed :lambda

<-: enfixed func* [
    {Declare action by example instantiation, missing args left unspecialized}

    return: [action!]
    :left "Enforces nothing to the left of the pointfree expression"
        [<end>]
    :expression "POINTFREE expression, BLANK!s are unspecialized arg slots"
        [any-value! <variadic>]
][
    return pointfree make block! expression  ; !!! vararg param for efficiency?
]


; !!! NEXT and BACK seem somewhat "noun-like" and desirable to use as variable
; names, but are very entrenched in Rebol history.  Also, since they are
; specializations they don't fit easily into the NEXT OF SERIES model--this
; is a problem which hasn't been addressed.
;
next: specialize :skip [offset: 1]
back: specialize :skip [offset: -1]

bound?: chain [specialize :reflect [property: 'binding] | :value?]

unspaced: specialize :delimit [delimiter: null]
unspaced-text: chain [:unspaced | specialize :else [branch: [copy ""]]]

spaced: specialize :delimit [delimiter: space]
spaced-text: chain [:spaced | specialize :else [branch: [copy ""]]]

newlined: chain [
    adapt specialize :delimit [delimiter: newline] [
        if text? :line [
            fail @line "NEWLINED on TEXT! semantics being debated"
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

head?: specialize :reflect [property: 'head?]
tail?: specialize :reflect [property: 'tail?]
past?: specialize :reflect [property: 'past?]
open?: specialize :reflect [property: 'open?]


empty?: func* [
    {TRUE if empty or BLANK!, or if series is at or beyond its tail.}
    return: [logic!]
    series [any-series! object! port! bitset! map! blank!]
][
    return did any [blank? series, tail? series]
]


reeval func* [
    {Make fast type testing functions (variadic to quote "top-level" words)}
    return: <none>
    'set-words [<variadic> set-word! tag!]
    <local>
        set-word type-name tester meta
][
    loop [<end> != set-word: take set-words] [
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
    bad-word?:
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
    meta-word?:
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
    meta-path?:
    block?:
    set-block?:
    get-block?:
    meta-block?:
    group?:
    get-group?:
    set-group?:
    meta-group?:
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
    any-branch?:
    any-the-type?:
    any-plain-type?:
    any-get-type?:
    any-set-type?:
    any-meta-type?:
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
    quote to word! noquote :value
]
lit-path?: func* [value [<opt> any-value!]] [
    did all [
        quoted? :value
        path! = type of unquote value
    ]
]
to-lit-path: func* [value [any-value!]] [
    quote to path! noquote :value
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

    return: "NULL if blank input or effectively empty block, else ~none~"
        [<opt> bad-word!]
    line "Line of text or block, blank or [] has NO output, newline allowed"
        [<blank> char! text! block! quoted!]
][
    if char? line [
        if line <> newline [
            fail "PRINT only allows CHAR! of newline (see WRITE-STDOUT)"
        ]
        return write-stdout line
    ]

    if quoted? line [  ; Feature: treats a quote mark as a mold request
        line: mold unquote line
    ]

    return write-stdout (try spaced line) then [write-stdout newline]
]


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
