Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Boot Base: Function Constructors"
    rights: --[
        Copyright 2012 REBOL Technologies
        Copyright 2012-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    notes: --[
        This code is evaluated just after actions, natives, sysobj, and other
        lower-level definitions.  It intializes a minimal working environment
        that is used for the rest of the boot.
    ]--
]

assert: ghostable func [
    "Ensure conditions are branch triggers if hooked by debugging"

    return: [ghost!]
    conditions "Block of conditions to evaluate and test for logical truth"
        [block!]
    :handler "Optional code to run if the assertion fails, receives condition"
        [<unrun> block! frame!]
][
    ; ASSERT has no default implementation, but can be HIJACKed by a debug
    ; mode with a custom validation or output routine.
    ;
    return ()
]

steal: lambda [
    "Return a variable's value prior to an assignment, then do the assignment"

    evaluation "Takes assigned value (variadic enables STEAL X: DEFAULT [...])"
        [any-stable? <variadic>]
    @look [set-word? set-tuple? <variadic>]
][
    get first look  ; returned value
    elide take evaluation
]

assert [null = coupling of return/]  ; it's archetypal, nowhere to return to

return: ~#[RETURN used when no function generator is providing it]#~

continue: ~#[CONTINUE used when no loop is providing it]#~

continue: ~#[AGAIN used when no loop is providing it]#~

break: ~#[BREAK used when no loop is providing it]#~

stop: ~#[STOP used when no loop is providing it]#~

throw: ~#[THROW used when no catch is providing it]#~

quit: ~#[QUIT used when no (DO IMPORT CONSOLE) is providing it]#~

yield: ~#[YIELD used when no generator or yielder is providing it]#~

catch: specialize catch*/ [name: 'throw]


; Simple "divider-style" thing for remarks.  At a certain verbosity level,
; it could dump those remarks out...perhaps based on how many == there are.
; (This is a good reason for retaking ==, as that looks like a divider.)
;
===: func [
    return: [ghost!]
    'remarks [element? <variadic>]
    :visibility [onoff?]
]
bind construct [
    logging: 'off
][
    if visibility [logging: visibility, return ()]

    if on? logging [
        print form collect [
            keep [===]
            insist [equal? '=== keep take remarks]  ; prints tail `===`
        ]
    ] else [
        insist [equal? '=== take remarks]
    ]
    return ()
]

what-dir: func [  ; This can be HIJACK'd by a "smarter" version
    "Returns the current directory path"
    return: [<null> file! url!]
][
    return system.options.current-path
]

change-dir: func [  ; This can be HIJACK'd by a "smarter" version
    "Changes the current path (where scripts with relative paths will be run)"
    return: [file! url!]
    path [file! url!]
][
    return system.options.current-path: path
]


redescribe: func [
    "Mutate action description with new title and/or new argument notes"

    return: [~[action!]~ frame!]
    spec "Either a string description, or a spec block"
        [block!]
    action "(modified) Action whose description is to be updated"
        [<unrun> frame!]  ; should be active or inactive based on input
][
    ; !!! This needs to be completely rethought
    return runs action
]


unset: redescribe [
    "Put variable into a dual state that prohibits any form of GET on it"
](
    specialize set/ [value: ()]
)

unset?: redescribe [
    "Determine if a variable holds a GHOST! antiform"
](
    cascade [meta/ get/ ghost?/]
)

set?: redescribe [
    "Determine if a variable holds something other than a GHOST! antiform"
](
    cascade [unset?/ not/]
)

vacant?: redescribe [
    "Determine if a variable would be considered empty for purposes of DEFAULT"
](
    cascade [meta/ get/ vacancy?/]
)

undefined?: cascade [defined?/ not/]

unspecialized?: lambda [
    "Determine if a variable looks up to a PARAMETER!"
    []: [logic?]
    var [word! tuple!]
][
    try parameter? get meta var
]

specialized?: lambda [
    "Determine if a variable doesn't look up to a PARAMETER!"
    []: [logic?]
    var [word! tuple!]
][
    not try parameter? get meta var
]


; ->- is the SHOVE operator.  It uses the item immediately to its left for
; the first argument to whatever operation is on its right hand side.  While
; most useful for calling infix functions from PATH! (which can't be done
; otherwise), it can also be used with ordinary functions...and precedence
; will be handled accordingly:
;
;    >> 10 ->- lib/= 5 + 5  ; as if you wrote `10 = 5 + 5`
;    ** Script Error: + does not allow logic? for its value1 argument
;
;    >> 10 ->- equal? 5 + 5  ; as if you wrote `equal? 10 5 + 5`
;    == ~okay~  ; anti
;
; SHOVE's left hand side is taken as one literal unit, and then pre-processed
; for the parameter conventions of the right hand side's first argument.  But
; since literal arguments out-prioritize evaluative right infix, these
; "simulated evaluative left parameters" will act differently than if SHOVE
; were not being used:
;
;     >> 1 + 2 * 3
;     == 9  ; e.g. (1 + 2) * 3
;
;     >> 1 + 2 ->- lib/* 3
;     == 7  ; e.g. 1 + (2 * 3)
;
; Offering ways to work around this with additional operators was deemed not
; worth the cost, when you can just put the left hand side in parentheses
; if this isn't what you want.
;
->-: infix shove/


; The -- and ++ operators were deemed too "C-like", so ME was created to allow
; `some-var: me + 1` or `some-var: me / 2` in a generic way.  Once they shared
; code with SHOVE, but currently done using inliner due to unfinished binding
; semantics in SHOVE pertaining to fetched values.

me: infix redescribe [
    "Update variable using it as the left hand argument to an infix operator"
](
    inliner [@left [set-word? set-tuple?] @right [word! path! chain!]] [
        spread reduce [left, unchain left, right]
    ]
)

my: infix redescribe [
    "Update variable using it as the first argument to a prefix operator"
](
    inliner [@left [set-word? set-tuple?] @right [word! path! chain!]] [
        spread reduce [left, right, unchain left]
    ]
)

so: infix:postpone func [
    "Postfix assertion which stops running if left expression is inhibitor"

    return: [any-stable?]
    condition "Condition to test, must resolve to logic (use DID, NOT)"
        [logic?]
    feed "Needs value to return as result e.g. (x: even? 4 so 10 + 20)"
        [<end> any-stable? <variadic>]
][
    if not condition [
        panic:blame make warning! [
            type: 'script
            id: 'assertion-failure
            arg1: [~null~ so]
        ] $condition
    ]
    if tail? feed [return ~]
    return take feed
]


was: infix:postpone redescribe [
    "Assert that the left hand side--when fully evaluated--IS the right"
](
    lambda [left [any-stable?] right [any-stable?]] [
        if ^left != ^right [
            panic:blame make warning! [
                type: 'script
                id: 'assertion-failure
                arg1: compose [(:left) is (:right)]
            ] $return
        ]
        ^left  ; choose left in case binding or case matters somehow
    ]
)


zdeflate: redescribe [
    "Deflates data with zlib envelope: https://en.wikipedia.org/wiki/ZLIB"
](
    specialize deflate/ [envelope: 'zlib]
)

zinflate: redescribe [
    "Inflates data with zlib envelope: https://en.wikipedia.org/wiki/ZLIB"
](
    specialize inflate/ [envelope: 'zlib]
)

gzip: redescribe [
    "Deflates data with gzip envelope: https://en.wikipedia.org/wiki/Gzip"
](
    specialize deflate/ [envelope: 'gzip]
)

gunzip: redescribe [
    "Inflates data with gzip envelope: https://en.wikipedia.org/wiki/Gzip"
](
    specialize inflate/ [envelope: 'gzip]  ; What about GZIP-BADSIZE?
)

ensure: redescribe [
    "Pass through value if it matches test, otherwise trigger a FAIL"
](
    enclose typecheck/ lambda [f] [
        eval f else [
            ; !!! Can't use FAIL:BLAME until we can implicate the callsite.
            ;
            ; https://github.com/metaeducation/ren-c/issues/587
            ;
            panic [
                "ENSURE failed with argument of type"
                    (mold reify try type of f.value) else ["VOID"]
            ]
        ]
        ^f.value
    ]
)

; NULL is sticky in NON, as with MATCH.  If you say `non integer! null` then
; if you give back NULL ("it passed, it wasn't an integer") this conflates
; with the failure signal.  You need to use
;
non: redescribe [
    "Pass through value if it *doesn't* match test, else null"
](
    enclose match/ func [f] [
        eval f then [return null]
        return f.value
    ]
)

prohibit: redescribe [
    "Pass through value if it *doesn't* match test, else panic"
](
    enclose typecheck/ lambda [f] [
        eval f then [
            ; !!! Can't use FAIL:BLAME until we can implicate the callsite.
            ;
            ; https://github.com/metaeducation/ren-c/issues/587
            ;
            panic [
                "PROHIBIT failed with argument of type"
                    (mold reify try type of f.value) else ["NULL"]
            ]
        ]
        f.value
    ]
)


oneshot: specialize n-shot/ [n: 1]
upshot: specialize n-shot/ [n: -1]

;
; !!! The /REVERSE and /LAST refinements of FIND and SELECT caused a lot of
; bugs.  This recasts those refinements in userspace, in the hopes to reduce
; the combinatorics in the C code.  If needed, they could be made for SELECT.
;

find-reverse: redescribe [
    "Variant of FIND that uses a /SKIP of -1"
](
    specialize find/ [skip: -1]
)

find-last: redescribe [
    "Variant of FIND that uses a /SKIP of -1 and seeks the TAIL of a series"
](
    adapt find-reverse/ [
        if not any-series? series [
            panic:blame "Can only use FIND-LAST on ANY-SERIES?" $series
        ]

        series: tail of series  ; can't use plain TAIL due to /TAIL refinement
    ]
)

; New-attempt, run body as a "loop of 1" with BREAK and CONTINUE, and able
; to integrate with THEN and ELSE.
;
attempt: specialize repeat/ [count: 1]

rescue: func [
    "If evaluation returns an antiform error, return as warning, else NULL"

    return: [<null> warning!]
    code [block!]
][
    return match warning! enrescue code
]

reduce*: redescribe [
    "REDUCE a block but vaporize NULL Expressions"
](
    specialize reduce/ [predicate: opt/]
)

for-next: redescribe [
    "Evaluates a block for each position until the end, using NEXT to skip"
](
    specialize for-skip/ [skip: 1]
)

for-back: redescribe [
    "Evaluates a block for each position until the start, using BACK to skip"
](
    specialize for-skip/ [skip: -1]
)

iterate-skip: redescribe [
    "Variant of FOR-SKIP that directly modifies a series variable in a word"
](
    specialize enclose for-skip/ func [f {word}] [
        if space? f.word [return null]
        assert [match [@word!] f.word]
        f.series: get word: unpin f.word

        ; !!! https://github.com/rebol/rebol-issues/issues/2331
        comment [
            let result
            rescue [^result: eval f] then e -> [
                set word f.series
                panic e
            ]
            set word f.series
            ^result
        ]

        return (eval f, elide set word f.series)
    ][
        series: <overwritten>
    ]
)

iterate: iterate-next: redescribe [
    "Variant of FOR-NEXT that directly modifies a series variable in a word"
](
    specialize iterate-skip/ [skip: 1]
)

iterate-back: redescribe [
    "Variant of FOR-BACK that directly modifies a series variable in a word"
](
    specialize iterate-skip/ [skip: -1]
)


count-up: func [
    "Loop the body, setting a word from 1 up to the end value given"
    return: [any-value?]
    var [word!]
    limit [<opt-out> integer! rune!]
    body [block!]
    {start end ^result}
][
    ; REPEAT in UPARSE wanted to try out some cutting-edge ideas about
    ; "opting in" to counting loops, e.g. `count-up 'i _` opts out and doesn't
    ; loop at all.  But what if `count-up 'i #` meant loop forever?  This
    ; clunky layer on top of cfor is a good test of loop abstraction, and
    ; is good enough to let UPARSE do its experiment without changing any
    ; native code.

    start: 1
    end: if rune? limit [
        if limit <> # [panic]
        100  ; not forever...don't use max int to help test "pseudoforever"
    ] else [
        limit
    ]
    return cycle [
        ^result: cfor var start end 1 body else [
            return null  ; a BREAK was encountered
        ]
        if limit <> # [  ; Note: STOP:WITH not ^META, decays PACK! etc
            return heavy ^result  ; the limit was actually reached
        ]
        ; otherwise keep going...
        end: end + 100
        start: start + 100
    ]
]

count-down: redescribe [
    "Loop the body, setting a word from the end value given down to 1"
](
    specialize adapt cfor/ [
        start: end
        end: 1
    ][
        start: <overwritten-with-end>
        bump: -1
    ]
)


lock-of: redescribe [
    "If value is already locked, return it...otherwise CLONE it and LOCK it."
](
    cascade [specialize copy/ [deep: ok], freeze/]
)

eval-all: func [
    "Evaluate any number of expressions and discard them"

    return: [ghost!]
    expressions "Any number of expressions on the right"
        [any-stable? <variadic>]
][
    eval expressions
    return ()
]


; These constructs used to be infix to complete their left hand side.  Yet
; that form of completion was only one expression's worth, when they wanted
; to allow longer runs of evaluation.  "Invisible functions" (those which
; `return: [ghost!]`) permit a more flexible version of the mechanic.

<|: infix:postpone eval-all/


; !!! This is an interim implementation of GETTER that returns nothing.  The
; other concept of it would be to return a PACK! with "sub-band" content
; which represented the TWEAK* representation of a getter.
;
; (Note it would be wasteful to call the getter, when 99 times out of 100
; the value would be discarded--so whatever is done here should avoid that.)
;
getter: infix proc [
    @var [set-word? set-run-word?]
    action [block! action!]
][
    if block? ^action [action: does action]
    tweak var unrun action/
]


cause-error: func [
    "Causes an immediate error throw with the provided information"
    err-type [word!]
    err-id [word!]
    args
][
    args: blockify args  ; make sure it's a block

    panic make warning! [
        type: err-type
        id: err-id
        arg1: try first args
        arg2: try second args
        arg3: try third args
    ]
]


fail: func [
    "Return unstable ERROR! antiform state (RESCUE, EXCEPT, TRY can intercept)"

    return: [error!]
    reason "ERROR! value, ID, URL, message text, or failure spec"
        [
            <end>  ; non-specific failure
            warning!  ; already constructed error
            @word!  ; invalid-arg error with variable name/value
            text!  ; textual error message
            trash!  ; same as text (but more attention grabbing at callsite)
            block!  ; mixture of object error spec and message
            word! path! url!  ; increasing specificity of error ID
        ]
    :blame "Point to variable or parameter to blame"
        [word! frame!]
][
    if trash? ^reason [
        reason: as text! unanti reason  ; antiform tag! ~#unreachable~
    ]
    all [warning? reason, not blame] then [
        return fail* reason  ; fast shortcut
    ]

    ; Ultimately we might like FAIL to use some clever error-creating dialect
    ; when passed a block, maybe something like:
    ;
    ;     panic [<invalid-key> "The key" key-name: key "is invalid"]
    ;
    ; That could provide an error ID, the format message, and the values to
    ; plug into the slots to make the message...which could be extracted from
    ; the error if captured (e.g. error.id and `error.key-name`.  Another
    ; option would be something like:
    ;
    ;     panic:with ["The key" :key-name "is invalid"] [key-name: key]

    let error: switch:type reason [
        warning! [reason]
        word?:pinned/ [
            blame: default [to word! reason]
            make warning! [
                id: 'invalid-arg
                arg1: label of binding of reason
                arg2: to word! reason
                arg3: get reason
            ]
        ]
        text! [make warning! reason]
        word! [
            make warning! [  ; no Type, so no message
                id: reason
            ]
        ]
        path! [
            assert [  ; limited idea for now
                2 = length of reason
                'core = first reason
            ]
            make warning! [  ; will look up message in core error table
                type: 'script
                id: last reason
            ]
        ]
        url! [make warning! to text! reason]  ; should use URL! as ID
        block! [
            make warning! (spaced reason else '[
                type: 'script
                id: 'unknown-error
            ])
        ]
    ] else [
        null? reason so make warning! [
            type: 'script
            id: 'unknown-error
        ]
    ]

    ; !!! PATH! doesn't do BINDING OF, and in the general case it couldn't
    ; tell you where it resolved to without evaluating, just do WORD! for now.
    ;
    let frame: match frame! opt binding of opt match word! opt blame

    if not pick error 'where [
        ;
        ; If no specific location specified, and error doesn't already have a
        ; location, make it appear to originate from the frame calling FAIL.
        ;
        let where: default [any [frame, binding of $return]]

        set-location-of-error error where  ; !!! why is this native?
    ]

    ; Initially this would call EVAL to force an exception to the nearest
    ; trap up the stack (if any).  However, Ren-C rethought errors as being
    ; "definitional", which means you would say RETURN RAISE and it would be
    ; a special kind of "error antiform" that was a unique return result.  Then
    ; you typically can only trap/catch errors that come from a function you
    ; directly called.
    ;
    return fail* ensure warning! error
]

; Immediately panic on an error--do not allow ^META interception/etc.
;
; Note that in addition to this definition of PANIC, there is an early-boot
; definition which runs if a PANIC happens before this point, which crashes
; and gives more debug information.
;
; Note: we use CHAIN into NULL? as an arbitrary intrinsic which won't take
; an error, because the CHAIN doesn't add a stack level that obscures
; generation of the NEAR and WHERE fields.  If we tried to ENCLOSE and EVAL
; the error it would add more overhead and confuse those matters.
;
panic: cascade [fail/ null?/]
