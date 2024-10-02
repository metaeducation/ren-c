REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Base: Function Constructors"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2019 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Note: {
        This code is evaluated just after actions, natives, sysobj, and other
        lower-level definitions.  It intializes a minimal working environment
        that is used for the rest of the boot.
    }
]

assert: func* [
    {Ensure conditions are branch triggers if hooked by debugging}

    return: [~[]~]
    conditions "Block of conditions to evaluate and test for logical truth"
        [block!]
    /handler "Optional code to run if the assertion fails, receives condition"
        [<unrun> block! frame!]
][
    ; ASSERT has no default implementation, but can be HIJACKed by a debug
    ; mode with a custom validation or output routine.
    ;
    return ~[]~
]

steal: lambda [
    {Return a variable's value prior to an assignment, then do the assignment}

    evaluation "Takes assigned value (variadic enables STEAL X: DEFAULT [...])"
        [any-value? <variadic>]
    @look [set-word? set-tuple? <variadic>]
][
    get first look  ; returned value
    elide take evaluation
]

assert [null = coupling of return/]  ; it's archetypal, nowhere to return to
return: func* [] [
    fail "RETURN archetype called when no generator is providing it"
]

continue: func* [] [
    fail "CONTINUE archetype called when no loop is providing it"
]

break: func* [] [
    fail "BREAK archetype called when no loop is providing it"
]

stop: func* [] [
    fail "STOP archetype called when no loop is providing it"
]

throw: func* [] [
    fail "THROW archetype called when no catch is providing it"
]

quit: func* [] [
    fail "QUIT archetype called when none no DO/IMPORT/console is providing it"
]

catch: specialize catch*/ [name: 'throw]

func: func* [
    {Augment action with <static>, <in>, <with> features}

    return: [action?]
    spec "Help string (opt) followed by arg words (and opt type and string)"
        [block!]
    body "The body block of the function"
        [<const> block!]
    <local>
        new-spec var loc other
        new-body defaulters statics with-return
][
    ; R3-Alpha offered features on FUNCTION (a complex usermode construct)
    ; that the simpler/faster FUNC did not have.  Ren-C seeks to make FUNC and
    ; FUNCTION synonyms:
    ;
    ; https://forum.rebol.info/t/abbreviations-as-synonyms/1211
    ;
    ; To get a little ways along this path, there needs to be a way for FUNC
    ; to get features like <static> which are easier to write in usermode.
    ; So the lower-level FUNC* is implemented as a native, and this wrapper
    ; does a fast shortcut to check to see if the spec has no tags...and if
    ; not, it quickly falls through to that fast implementation.
    ;
    ; Note: Long term, FUNC could be a native which does this check in raw
    ; C and then calls out to usermode if there are tags.  That would be even
    ; faster than this usermode prelude.
    ;
    all [
        not find spec matches tag!
        return func* spec body
    ]

    ; Rather than MAKE BLOCK! LENGTH OF SPEC here, we copy the spec and clear
    ; it.  This costs slightly more, but it means we inherit the file and line
    ; number of the original spec...so when we pass NEW-SPEC to FUNC or PROC
    ; it uses that to give the FILE OF and LINE OF the function itself.
    ;
    ; !!! General API control to set the file and line on blocks is another
    ; possibility, but since it's so new, we'd rather get experience first.
    ;
    new-spec: clear copy spec  ; also inherits binding

    new-body: null
    statics: null
    defaulters: null
    var: #dummy  ; enter PARSE with truthy state (gets overwritten)
    loc: null
    with-return: null

    parse3 spec [opt some [
        :(if var '[  ; so long as we haven't reached any <local> or <with> etc.
            var: [
                &set-word? | &get-word? | &any-word? | &refinement? | quoted!
            ] (
                append new-spec var
            )
            |
            other: block! (
                append new-spec other  ; data type blocks
            )
            |
            other: across some text! (
                append new-spec spaced other  ; spec notes
            )
        ] else [
            'bypass
        ])
    |
        other: group! (
            if not var [
                fail [
                    ; <where> spec
                    ; <near> other
                    "Default value not paired with argument:" (mold other)
                ]
            ]
            defaulters: default [inside body copy '[]]
            append defaulters spread compose [
                (var): default (meta eval inside spec other)
            ]
        )
    |
        (var: null)  ; everything below this line resets var
        bypass  ; failing here means rolling over to next rule
    |
        '<local> (append new-spec <local>)
        opt some [var: word! other: opt group! (
            append new-spec var
            if other [
                defaulters: default [inside body copy '[]]
                append defaulters spread compose [  ; always sets
                    (var): (meta eval inside spec other)
                ]
            ]
        )]
        (var: null)  ; don't consider further GROUP!s or variables
    |
        '<in> (
            new-body: default [
                copy/deep body
            ]
        )
        opt some [
            other: [object! | word! | tuple!] (
                if not object? other [
                    other: ensure [any-context?] get inside spec other
                ]
                bind new-body other
            )
        ]
    |
        '<with> opt some [
            other: [word! | path!] (
                ;
                ; Definitional returns need to be signaled even if FUNC, so
                ; the FUNC* doesn't automatically generate one.
                ;
                if other = 'return [with-return: '[<with> return]]
            )
        |
            text!  ; skip over as commentary
        ]
    |
        ; For static variables to see each other, the GROUP!s can't have an
        ; hardened context.  We ignore their binding here for now.
        ;
        ; https://forum.rebol.info/t/2132
        ;
        '<static> (
            statics: default [copy inside spec '[]]
            new-body: default [
                copy/deep body
            ]
        )
        opt some [
            var: word!, other: opt group! (
                append statics setify var
                append statics any [
                    bindable maybe other  ; !!! ignore binding on group
                    '~
                ]
            )
        ]
        (var: null)
    |
        <end> accept (~)
    |
        other: <here> (
            fail [
                ; <where> spec
                ; <near> other
                "Invalid spec item:" @(other.1)
                "in spec" @spec
            ]
        )
    ]]

    if statics [
        statics: make object! statics
        bind new-body statics
    ]

    append new-spec maybe with-return  ; if FUNC* suppresses return generation

    ; The constness of the body parameter influences whether FUNC* will allow
    ; mutations of the created function body or not.  It's disallowed by
    ; default, but TWEAK can be used to create variations e.g. a compatible
    ; implementation with Rebol2's FUNC.
    ;
    if const? body [new-body: const new-body]

    return func* new-spec either defaulters [
        append defaulters as group! bindable any [new-body body]
    ][
        any [new-body body]
    ]
]


; Simple "divider-style" thing for remarks.  At a certain verbosity level,
; it could dump those remarks out...perhaps based on how many == there are.
; (This is a good reason for retaking ==, as that looks like a divider.)
;
===: func [
    return: [~[]~]
    'remarks [element? <variadic>]
    /visibility [onoff?]
    <static> showing ('no)
][
    if visibility [showing: visibility, return ~[]~]

    if yes? showing [
        print form collect [
            keep [===]
            until [equal? '=== keep take remarks]  ; prints tail `===`
        ]
    ] else [
        until [equal? '=== take remarks]
    ]
    return ~[]~
]

what-dir: func [  ; This can be HIJACK'd by a "smarter" version
    {Returns the current directory path}
    return: [~null~ file! url!]
][
    return system.options.current-path
]

change-dir: func [  ; This can be HIJACK'd by a "smarter" version
    {Changes the current path (where scripts with relative paths will be run).}
    return: [file! url!]
    path [file! url!]
][
    return system.options.current-path: path
]


redescribe: func [
    {Mutate action description with new title and/or new argument notes.}

    return: [action?]
    spec "Either a string description, or a spec block"
        [block!]
    action "(modified) Action whose description is to be updated"
        [<unrun> frame!]
][
    ; !!! This needs to be completely rethought
    return runs action
]


unset: redescribe [
    {Clear the value of a word to the unset state (in its current context)}
](
    specialize set/ [value: meta ~]  ; SET's value is a ^META parameter
)

unset?: func [
    {Determine if a variable looks up to a `~` antiform}
    return: [logic?]
    var [word! path! tuple!]
][
    return nothing? get/any var
]

vacant?: func [
    "Determine if a variable is nothing, antiform tag, or antiform parameter"
    return: [logic?]
    var [word! path! tuple!]
][
    return vacancy? get/any var
]

set?: func [
    {Determine if a variable does not look up to  `~` antiform}
    return: [logic?]
    var [word! path! tuple!]
][
    return something? get/any var
]

defined?: func [
    {Determine if a variable is both "attached", and not unset}
    return: [logic?]
    var [word! path! tuple!]
][
    return not trap [get var]
]

undefined?: func [
    {Determine if a variable is "unattached" or unset}
    return: [logic?]
    var [word! path! tuple!]
][
    return did trap [get var]
]

unspecialized?: func [
    "Determine if a variable looks up to a parameter antiform"
    return: [logic?]
    var [word! tuple!]
][
    return hole? get/any var
]

specialized?: func [
    "Determine if a variable doesn't look up to a parameter antiform"
    return: [logic?]
    var [word! tuple!]
][
    return not hole? get/any var
]


curtail: reframer func [
    {Voids an expression if it raises any NEED-NON-NULL failures}
    return: [any-value?]
    frame [frame!]
][
    return eval frame except e -> [
        if e.id == 'need-non-null [return void]
        fail e
    ]
]


; ->- is the SHOVE operator.  It uses the item immediately to its left for
; the first argument to whatever operation is on its right hand side.  While
; most useful for calling enfix functions from PATH! (which can't be done
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
; since literal arguments out-prioritize evaluative right enfix, these
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
->-: enfix shove/


; The -- and ++ operators were deemed too "C-like", so ME was created to allow
; `some-var: me + 1` or `some-var: me / 2` in a generic way.  Once they shared
; code with SHOVE, but are currently done using macro due to unfinished binding
; semantics in SHOVE pertaining to fetched values.

me: enfix redescribe [
    {Update variable using it as the left hand argument to an enfix operator}
](
    macro [@left [set-word! set-tuple!] @right [word! tuple! path!]] [
        :[left, plain left, right]
    ]
)

my: enfix redescribe [
    {Update variable using it as the first argument to a prefix operator}
](
    macro [@left [set-word! set-tuple!] @right [word! tuple! path!]] [
        :[left, right, plain left]
    ]
)

so: enfix func [
    {Postfix assertion which stops running if left expression is inhibitor}

    return: [any-value?]
    condition "Condition to test, must resolve to logic (use DID, NOT)"
        [logic?]
    feed "Needs value to return as result e.g. (x: even? 4 so 10 + 20)"
        [<end> any-value? <variadic>]
][
    if not get/any $condition [
        fail/blame make error! [
            type: 'Script
            id: 'assertion-failure
            arg1: compose [~null~ so]
        ] $condition
    ]
    if tail? feed [return ~]
    return take feed
]
tweak so/ 'postpone 'on


was: enfix redescribe [
    "Assert that the left hand side--when fully evaluated--IS the right"
](
    lambda [left [any-value?] right [any-value?]] [
        if :left != :right [
            fail/blame make error! [
                type: 'Script
                id: 'assertion-failure
                arg1: compose [(:left) is (:right)]
            ] $return
        ]
        :left  ; choose left in case binding or case matters somehow
    ]
)
tweak was/ 'postpone 'on


zdeflate: redescribe [
    {Deflates data with zlib envelope: https://en.wikipedia.org/wiki/ZLIB}
](
    specialize deflate/ [envelope: 'zlib]
)

zinflate: redescribe [
    {Inflates data with zlib envelope: https://en.wikipedia.org/wiki/ZLIB}
](
    specialize inflate/ [envelope: 'zlib]
)

gzip: redescribe [
    {Deflates data with gzip envelope: https://en.wikipedia.org/wiki/Gzip}
](
    specialize deflate/ [envelope: 'gzip]
)

gunzip: redescribe [
    {Inflates data with gzip envelope: https://en.wikipedia.org/wiki/Gzip}
](
    specialize inflate/ [envelope: 'gzip]  ; What about GZIP-BADSIZE?
)

ensure: redescribe [
    "Pass through value if it matches test, otherwise trigger a FAIL"
](
    enclose get $match/meta lambda [f] [
        let value: :f.value  ; EVAL makes frame arguments unavailable
        eval f else [  ; /META allows any value, must UNMETA
            ; !!! Can't use FAIL/WHERE until we can implicate the callsite.
            ;
            ; https://github.com/metaeducation/ren-c/issues/587
            ;
            fail [
                "ENSURE failed with argument of type"
                    (mold reify try type of :value) else ["VOID"]
            ]
        ]
        :value
    ]
)

; NULL is sticky in NON, as with MATCH.  If you say `non integer! null` then
; if you give back NULL ("it passed, it wasn't an integer") this conflates
; with the failure signal.  You need to use
;
non: redescribe [
    "Pass through value if it *doesn't* match test, else null (MATCH/NOT)"
](
    enclose match/ func [f] [
        let value: :f.value  ; EVAL makes frame arguments unavailable
        if f.meta [value: ^value]
        eval f then [return null] else [return :value]
    ]
)

prohibit: redescribe [
    {Pass through value if it *doesn't* match test, else fail (ENSURE/NOT)}
](
    enclose get $match/meta lambda [f] [
        let value: :f.value  ; EVAL makes frame arguments unavailable
        eval f then [
            ; !!! Can't use FAIL/WHERE until we can implicate the callsite.
            ;
            ; https://github.com/metaeducation/ren-c/issues/587
            ;
            fail [
                "PROHIBIT failed with argument of type"
                    (mold reify try type of :value) else ["NULL"]
            ]
        ]
        :value
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
    {Variant of FIND that uses a /SKIP of -1}
](
    specialize find/ [skip: -1]
)

find-last: redescribe [
    {Variant of FIND that uses a /SKIP of -1 and seeks the TAIL of a series}
](
    adapt find-reverse/ [
        if not any-series? series [
            fail/blame "Can only use FIND-LAST on ANY-SERIES?" $series
        ]

        series: tail of series  ; can't use plain TAIL due to /TAIL refinement
    ]
)

attempt: func [
    {Evaluate a block and returns result or NULL if an expression fails}

    return: "Returns NULL on failure (-or- if last evaluative result is NULL)"
        [any-value?]
    code [block!]
    <local> temp
][
    if error? temp: entrap code [return null]
    return unmeta temp
]

trap: func [
    {If evaluation raises an error, return it, otherwise NULL}

    return: [~null~ error!]
    code [block!]
][
    return match error! entrap code
]

trap+: func [
    {Experimental variation of TRAP using THENable mechanics}

    return: [pack?]
    code [block!]
    <local> result
][
    ; If you return a pure NULL with the desire of triggering ELSE, that does
    ; not allow you to return more values.  This uses a lazy object that will
    ; run a THEN branch on error, but then an ELSE branch with the returned
    ; value on non-error...or reify to a pack with NULL for the error and
    ; the result.
    ;
    if error? result: entrap code [
        return pack [result null]
    ]

    return anti make object! [
        else: branch -> [(heavy unmeta :result) then (:branch)]
        decay: [pack [null unmeta result]]
    ]
]

reduce*: redescribe [
    "REDUCE a block but vaporize NULL Expressions"
](
    specialize reduce/ [predicate: maybe/]
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
    specialize enclose for-skip/ func [f] [
        if blank? let word: f.word [return null]
        assert [the-word? f.word]
        let saved: f.series: get word

        ; !!! https://github.com/rebol/rebol-issues/issues/2331
        comment [
            let result
            trap [result: eval f] then e -> [
                set word saved
                fail e
            ]
            set word saved
            :result
        ]

        return (eval f, elide set word saved)
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
    {Loop the body, setting a word from 1 up to the end value given}
    return: [any-value?]
    var [word!]
    limit [<maybe> integer! issue!]
    body [block!]
    <local> start end result'
][
    ; REPEAT in UPARSE wanted to try out some cutting-edge ideas about
    ; "opting in" to counting loops, e.g. `count-up 'i _` opts out and doesn't
    ; loop at all.  But what if `count-up 'i #` meant loop forever?  This
    ; clunky layer on top of cfor is a good test of loop abstraction, and
    ; is good enough to let UPARSE do its experiment without changing any
    ; native code.

    start: 1
    end: if issue? limit [
        if limit <> # [fail]
        100  ; not forever...don't use max int to help test "pseudoforever"
    ] else [
        limit
    ]
    return cycle [
        result': ^ cfor (var) start end 1 body except e -> [
            return raise e
        ]
        if result' = ^null [return null]  ; a BREAK was encountered
        if result' = ^void [
            assert [start = end]  ; should only happen if body never runs
            return ^void
        ]
        if limit <> # [  ; Note: /WITH not ^META, decays PACK! etc
            stop/with heavy unmeta result'  ; the limit was actually reached
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
    {Evaluate any number of expressions and discard them}

    return: [~[]~]
    expressions "Any number of expressions on the right"
        [any-value? <variadic>]
][
    eval expressions
    return ~[]~
]


; These constructs used to be enfix to complete their left hand side.  Yet
; that form of completion was only one expression's worth, when they wanted
; to allow longer runs of evaluation.  "Invisible functions" (those which
; `return: [~[]~]`) permit a more flexible version of the mechanic.

<|: runs tweak copy unrun eval-all/ 'postpone 'on


; Currently, METH is just a synonym for FUNC as a way of annotating that you
; are using the `.field` notation and as such depend on the function invocation
; being coupled with an object.  This coupling can be done explicitly by the
; user with COUPLE or an implicit coupling will be supplied when a function
; is invoked from a TUPLE! where an object is on the left hand side.
;
meth: func/


; It's a bit odd that `foo: accessor does [...]` will evaluate to nothing.
; But the other option would be to call the accessor to synthesize its value
; (or to provide the prior value of the variable?)  It seems wasteful to
; call the accessor when 99 times out of 100 the value would be discarded.
;
accessor: enfix func [
    return: [~]
    var [set-word?]
    action [action?]
][
    set-accessor var action/
]


cause-error: func [
    "Causes an immediate error throw with the provided information."
    err-type [word!]
    err-id [word!]
    args
][
    args: blockify args  ; make sure it's a block

    fail make error! [
        type: err-type
        id: err-id
        arg1: try first args
        arg2: try second args
        arg3: try third args
    ]
]


; Note that in addition to this definition of FAIL, there is an early-boot
; definition which runs if a FAIL happens before this point, which panics and
; gives more debug information.
;
; Though HIJACK would have to be aware of it and preserve the rule.
;
raise: func [
    {Interrupts execution by reporting an error (a TRAP can intercept it).}

    return: []
    reason "ERROR! value, ID, URL, message text, or failure spec"
        [
            <end>  ; non-specific failure
            error!  ; already constructed error
            the-word!  ; invalid-arg error with variable name/value
            text!  ; textual error message
            tripwire?  ; same as text (but more attention grabbing at callsite)
            block!  ; mixture of object error spec and message
            word! path! url!  ; increasing specificity of error ID
        ]
    /blame "Point to variable or parameter to blame"
        [word! frame!]
][
    if tripwire? get/any $reason [
        reason: as text! unquasi ^reason  ; antiform tag! ~<unreachable>~
    ]
    all [error? reason, not blame] then [
        return raise* reason  ; fast shortcut
    ]

    ; Ultimately we might like FAIL to use some clever error-creating dialect
    ; when passed a block, maybe something like:
    ;
    ;     fail [<invalid-key> {The key} key-name: key {is invalid}]
    ;
    ; That could provide an error ID, the format message, and the values to
    ; plug into the slots to make the message...which could be extracted from
    ; the error if captured (e.g. error.id and `error.key-name`.  Another
    ; option would be something like:
    ;
    ;     fail/with [{The key} :key-name {is invalid}] [key-name: key]

    let error: switch/type :reason [
        error! [reason]
        the-word! [
            blame: default [to word! reason]
            make error! [
                id: 'invalid-arg
                arg1: label of binding of reason
                arg2: to word! reason
                arg3: get reason
            ]
        ]
        text! [make error! reason]
        word! [
            make error! [  ; no Type, so no message
                id: reason
            ]
        ]
        path! [
            assert [  ; limited idea for now
                2 = length of reason
                'core = first reason
            ]
            make error! [  ; will look up message in core error table
                type: 'Script
                id: last reason
            ]
        ]
        url! [make error! to text! reason]  ; should use URL! as ID
        block! [
            make error! (spaced reason else '[
                type: 'Script
                id: 'unknown-error
            ])
        ]
    ] else [
        null? reason so make error! [
            type: 'Script
            id: 'unknown-error
        ]
    ]

    ; !!! PATH! doesn't do BINDING OF, and in the general case it couldn't
    ; tell you where it resolved to without evaluating, just do WORD! for now.
    ;
    let frame: match frame! maybe binding of maybe match word! maybe blame

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
    return raise* ensure error! error
]

; Immediately fail on a raised error--do not allow ^META interception/etc.
;
; Note: we use CHAIN into NULL? as an arbitrary intrinsic which won't take
; a raised error, because the CHAIN doesn't add a stack level that obscures
; generation of the NEAR and WHERE fields.  If we tried to ENCLOSE and DO
; the error it would add more overhead and confuse those matters.
;
fail: cascade [raise/ null?/]
