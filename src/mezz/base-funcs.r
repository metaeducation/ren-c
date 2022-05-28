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
    {Ensure conditions are conditionally true if hooked by debugging}

    return: <void>
    conditions [block!]
        {Block of conditions to evaluate and test for logical truth}
][
    ; ASSERT has no default implementation, but can be HIJACKed by a debug
    ; mode with a custom validation or output routine.
    ;
    ; !!! R3-Alpha and Rebol2 did not distinguish ASSERT and VERIFY, since
    ; there was no idea of a "debug mode"
]

steal: func* [
    {Return a variable's value prior to an assignment, then do the assignment}

    return: [<opt> any-value!]
        {Value of the following SET-WORD! or SET-PATH! before assignment}
    evaluation [<opt> any-value! <variadic>]
        {Used to take the assigned value}
    'look [set-word! set-path! <variadic>]
][
    return (
        get first look  ; returned value
        elide take evaluation
    )
]

assert [null = binding of :return]  ; it's archetypal, nowhere to return to
return: func* [] [
    fail "RETURN archetype called when no generator is providing it"
]

func: func* [
    {Make action with set-words as locals, <static>, <in>, <with>, <local>}

    return: [action!]
    spec "Help string (opt) followed by arg words (and opt type and string)"
        [block!]
    body "The body block of the function"
        [<const> block!]
    /gather "Gather SET-WORD! as local variables (preferably, please use LET)"
    <local>
        new-spec var loc other
        new-body exclusions locals defaulters statics with-return
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
        not gather
        not find spec tag!
        return func* spec body
    ]

    exclusions: copy []

    ; Rather than MAKE BLOCK! LENGTH OF SPEC here, we copy the spec and clear
    ; it.  This costs slightly more, but it means we inherit the file and line
    ; number of the original spec...so when we pass NEW-SPEC to FUNC or PROC
    ; it uses that to give the FILE OF and LINE OF the function itself.
    ;
    ; !!! General API control to set the file and line on blocks is another
    ; possibility, but since it's so new, we'd rather get experience first.
    ;
    new-spec: clear copy spec

    new-body: '
    statics: '
    defaulters: '
    var: #dummy  ; enter PARSE with truthy state (gets overwritten)
    loc: '
    with-return: '

    parse3 spec [opt some [
        '<none> (append new-spec <none>)
    |
        '<void> (append new-spec <void>)
    |
        :(if var '[  ; so long as we haven't reached any <local> or <with> etc.
            set var: [any-word! | any-path! | quoted!] (
                append new-spec ^var  ; need quote level as-is in new spec

                var: noquote var
                case [
                    match [get-path! path!] var [
                        if (length of var != 2) or (_ <> var.1) [
                            fail ["Bad path in spec:" ^spec]
                        ]
                        append exclusions ^var.2  ; exclude args/refines
                    ]

                    any-word? var [
                        append exclusions ^var  ; exclude args/refines
                    ]

                    true [  ; QUOTED! could have been anything
                        fail ["Bad spec item" ^var]
                    ]
                ]
            )
            |
            set other: block! (
                append new-spec ^other  ; data type blocks
            )
            |
            copy other some text! (
                append new-spec spaced other  ; spec notes
            )
        ] else [
            [false]
        ])
    |
        other: <here>
        group! (
            if not var [
                fail [
                    ; <where> spec
                    ; <near> other
                    "Default value not paired with argument:" (mold other.1)
                ]
            ]
            defaulters: default [copy []]
            append defaulters compose [
                (var): default '(do other.1)
            ]
        )
    |
        (var: _)  ; everything below this line resets var
        false  ; failing here means rolling over to next rule
    |
        '<local> (append new-spec <local>)
        opt some [set var: word! set other: opt group! (
            append new-spec ^var
            append exclusions ^var
            if other [
                defaulters: default [copy []]
                append defaulters compose [  ; always sets
                    (var): '(do other)
                ]
            ]
        )]
        (var: _)  ; don't consider further GROUP!s or variables
    |
        '<in> (
            new-body: default [
                copy/deep body
            ]
        )
        opt some [
            set other: [object! | word! | tuple!] (
                if not object? other [other: ensure any-context! get other]
                bind new-body other
                for-each [key val] other [
                    append exclusions ^key
                ]
            )
        ]
    |
        '<with> opt some [
            set other: [word! | path!] (
                append exclusions ^other

                ; Definitional returns need to be signaled even if FUNC, so
                ; the FUNC* doesn't automatically generate one.
                ;
                if other = 'return [with-return: [<with> return]]
            )
        |
            text!  ; skip over as commentary
        ]
    |
        '<static> (
            statics: default [copy []]
            new-body: default [
                copy/deep body
            ]
        )
        maybe some [
            set var: word! (other: _) maybe set other: group! (
                append exclusions ^var
                append statics ^(as set-word! var)
                append statics ^(other else [the ~])
            )
        ]
        (var: _)
    |
        <end> accept
    |
        other: <here> (
            fail [
                ; <where> spec
                ; <near> other
                "Invalid spec item:" (mold ^other.1)
                "in spec" ^spec
            ]
        )
    ]]

    ; Gather the SET-WORD!s in the body, excluding the collected ANY-WORD!s
    ; that should not be considered.  Note that COLLECT is not defined by
    ; this point in the bootstrap.
    ;
    locals: if gather [
        collect-words/deep/set/ignore body exclusions
    ] else [null]

    if statics [
        statics: make object! statics
        bind new-body statics
    ]

    ; The <local> designation may be redundant with a <local> in the original
    ; spec.  Historical implementations of things like FUNCT in R3-Alpha had
    ; to dodge this to keep from providing redundant refinements.  It's legal
    ; in Ren-C, but really this generator should be making a FRAME! and not
    ; a spec block.  This is a work in progress.
    ;
    if locals [
        append new-spec [<local>]
        for-each loc locals [
            append new-spec ^loc
        ]
    ]

    append new-spec try with-return  ; if FUNC* suppresses return generation

    ; The constness of the body parameter influences whether FUNC* will allow
    ; mutations of the created function body or not.  It's disallowed by
    ; default, but TWEAK can be used to create variations e.g. a compatible
    ; implementation with Rebol2's FUNC.
    ;
    if const? body [new-body: const new-body]

    return func* new-spec either defaulters [
        append defaulters ^ as group! any [new-body body]
    ][
        any [new-body body]
    ]
]


; Historical FUNCTION is intended to one day be a synonym for FUNC, once there
; are solutions such that LET can take the place of what SET-WORD! gathering
; was able to do.  This will be an ongoing process.
;
; https://forum.rebol.info/t/rethinking-auto-gathered-set-word-locals/1150
;
function: specialize :func [gather: #]


; Simple "divider-style" thing for remarks.  At a certain verbosity level,
; it could dump those remarks out...perhaps based on how many == there are.
; (This is a good reason for retaking ==, as that looks like a divider.)
;
===: func [
    return: <void>
    'remarks [any-value! <variadic>]
    /visibility [logic!]
    <static> showing (false)
][
    if not null? visibility [showing: visibility, return]

    if showing [
        print form collect [
            keep [===]
            until [equal? ''=== keep ^(take remarks)]  ; prints tail `===`
        ]
    ] else [
        until [equal? '=== take remarks]
    ]
    return  ; return no value (invisible)
]


what-dir: func [  ; This can be HIJACK'd by a "smarter" version
    {Returns the current directory path}
    return: [<opt> file! url!]
][
    return opt system.options.current-path
]

change-dir: func [  ; This can be HIJACK'd by a "smarter" version
    {Changes the current path (where scripts with relative paths will be run).}
    return: [file! url!]
    path [file! url!]
][
    system.options.current-path: path
]


redescribe: func [
    {Mutate action description with new title and/or new argument notes.}

    return: [action!]
        {The input action, with its description now updated.}
    spec [block!]
        {Either a string description, or a spec block (without types).}
    value [action!]
        {(modified) Action whose description is to be updated.}
][
    let meta: meta-of :value
    let notes: _
    let description: _

    ; For efficiency, objects are only created on demand by hitting the
    ; required point in the PARSE.  Hence `redescribe [] :foo` will not tamper
    ; with the meta information at all, while `redescribe [{stuff}] :foo` will
    ; only manipulate the description.

    let on-demand-meta: does [
        meta: default [set-meta :value copy system.standard.action-meta]

        if not find meta 'description [
            fail [{archetype META-OF doesn't have DESCRIPTION slot} meta]
        ]

        if notes: try select meta 'parameter-notes [
            if not any-context? notes [  ; !!! Sometimes OBJECT!, else FRAME!
                fail [{PARAMETER-NOTES in META-OF is not a FRAME!} notes]
            ]

            all [frame? notes, :value <> (action of notes)] then [
                fail [{PARAMETER-NOTES in META-OF frame mismatch} notes]
            ]
        ]
    ]

    let on-demand-notes: does [  ; was DOES CATCH, removed during DOES tweak
        on-demand-meta

        if find meta 'parameter-notes [
            meta: _  ; need to get a parameter-notes field in the OBJECT!
            on-demand-meta  ; ...so this loses SPECIALIZEE, etc.

            description: meta.description: fields.description
            notes: meta.parameter-notes: fields.parameter-notes
            types: meta.parameter-types: fields.parameter-types
        ]
    ]

    let param: _
    let note: _
    parse3 spec [
        opt [
            copy description some text! (
                description: spaced description
                all [
                    description = {}
                    not meta
                ] then [
                    ; No action needed (no meta to delete old description in)
                ] else [
                    on-demand-meta
                    meta.description: if description = {} [
                        _
                    ] else [
                        description
                    ]
                ]
            )
        ]
        opt some [
            set param: [
                word! | get-word! | lit-word! | set-word!
                | ahead path! into [word! blank!]
            ](
                if path? param [param: param.1]
            )

            ; It's legal for the redescribe to name a parameter just to
            ; show it's there for descriptive purposes without adding notes.
            ; But if {} is given as the notes, that's seen as a request
            ; to delete a note.
            ;
            opt [[copy note some text!] (
                note: spaced note
                on-demand-meta
                if param = 'return: [
                    meta.return-note: all [
                        note <> {}
                        copy note
                    ]
                ] else [
                    any [
                        notes
                        note <> {}
                    ] then [
                        on-demand-notes

                        if not find notes as word! param [
                            fail [param "not found in frame to describe"]
                        ]

                        let actual: first find parameters of :value param
                        if param !== actual [
                            fail [param {doesn't match word type of} actual]
                        ]

                        notes/(as word! param): if note = {} [null] else [note]
                    ]
                ]
            )]
        ]
        <end>
    ] else [
        fail [{REDESCRIBE specs should be STRING! and ANY-WORD! only:} spec]
    ]

    ; If you kill all the notes then they will be cleaned up.  The meta
    ; object will be left behind, however.
    ;
    all [
        notes
        every [param note] notes [null? :note]
    ] then [
        meta.parameter-notes: null
    ]

    return :value  ; should have updated the meta
]


redescribe [
    {Create an ACTION, implicity gathering SET-WORD!s as <local> by default}
] :function


unset: redescribe [
    {Clear the value of a word to an unset isotope (in its current context)}
](
    specialize :set [value: ~]  ; tricky case, but supported!
)

unset?: func [
    {Determine if a variable looks up to a `~` isotope}
    return: [logic!]
    var [word! path! tuple!]
][
    return '~ = ^ get/any var
]

set?: func [
    {Determine if a variable does not look up to  `~` isotope}
    return: [logic!]
    var [word! path! tuple!]
][
    return '~ <> ^ get/any var
]


; >- is the SHOVE operator.  It uses the item immediately to its left for
; the first argument to whatever operation is on its right hand side.
; Parameter conventions of that first argument apply when processing the
; value, e.g. quoted arguments will act quoted.
;
; By default, the evaluation rules proceed according to the enfix mode of
; the operation being shoved into:
;
;    >> 10 >- lib.= 5 + 5  ; as if you wrote `10 = 5 + 5`
;    ** Script Error: + does not allow logic! for its value1 argument
;
;    >> 10 >- equal? 5 + 5  ; as if you wrote `equal? 10 5 + 5`
;    == #[true]
;
; You can force processing to be enfix using `->-` (an infix-looking "icon"):
;
;    >> 1 ->- lib.add 2 * 3  ; as if you wrote `1 + 2 * 3`
;    == 9
;
; Or force prefix processing using `>--` (multi-arg prefix "icon"):
;
;    >> 10 >-- lib.+ 2 * 3  ; as if you wrote `add 1 2 * 3`
;    == 7
;
>-: enfixed :shove
>--: enfixed specialize :>- [prefix: true]
->-: enfixed specialize :>- [prefix: false]


; The -- and ++ operators were deemed too "C-like", so ME was created to allow
; `some-var: me + 1` or `some-var: me / 2` in a generic way.  They share code
; with SHOVE, so it's folded into the implementation of that.

me: enfixed redescribe [
    {Update variable using it as the left hand argument to an enfix operator}
](
    ; /ENFIX so `x: 1, x: me + 1 * 10` is 20, not 11
    ;
    specialize :shove/set [prefix: false]
)

my: enfixed redescribe [
    {Update variable using it as the first argument to a prefix operator}
](
    specialize :shove/set [prefix: true]
)

so: enfixed func [
    {Postfix assertion which won't keep running if left expression is false}

    return: [<opt> any-value!]
    condition "Condition to test, must resolve to a LOGIC! (use DID, NOT)"
        [logic!]
    feed [<opt> <end> any-value! <variadic>]
][
    if not condition [
        fail @condition make error! [
            type: 'Script
            id: 'assertion-failure
            arg1: compose [((:condition)) so]
        ]
    ]
    if tail? feed [return none]
    feed: take feed
    all [block? :feed, semiquoted? 'feed] then [
        fail "Don't use literal block as SO right hand side, use ([...])"
    ]
    return :feed
]
tweak :so 'postpone on


; Rare case where a `?` variant is useful, to avoid BAD-WORD! on falsey matches
match?: chain [:match | :value?]


was: enfixed redescribe [
    "Assert that the left hand side--when fully evaluated--IS the right"
](
    func [left [<opt> any-value!] right [<opt> any-value!]] [
        if :left != :right [
            fail @return make error! [
                type: 'Script
                id: 'assertion-failure
                arg1: compose [(:left) is (:right)]
            ]
        ]
        :left  ; choose left in case binding or case matters somehow
    ]
)
tweak :was 'postpone on


zdeflate: redescribe [
    {Deflates data with zlib envelope: https://en.wikipedia.org/wiki/ZLIB}
](
    specialize :deflate [envelope: 'zlib]
)

zinflate: redescribe [
    {Inflates data with zlib envelope: https://en.wikipedia.org/wiki/ZLIB}
](
    specialize :inflate [envelope: 'zlib]
)

gzip: redescribe [
    {Deflates data with gzip envelope: https://en.wikipedia.org/wiki/Gzip}
](
    specialize :deflate [envelope: 'gzip]
)

gunzip: redescribe [
    {Inflates data with gzip envelope: https://en.wikipedia.org/wiki/Gzip}
](
    specialize :inflate [envelope: 'gzip]  ; What about GZIP-BADSIZE?
)

ensure: redescribe [
    {Pass through value if it matches test, otherwise trigger a FAIL}
](
    ; MATCH returns isotopes for BLANK!, #[false], and if the input is a
    ; NULL but matches the test via <opt>.  We take advantage of the unique
    ; meaning of NULL coming back from MATCH to know if the test failed...
    ; and then use DECAY to turn the isotopes back to their meanings.
    ;
    enclose :match func [f] [
        let value: :f.value  ; DO makes frame arguments unavailable
        decay (do f else [
            ; !!! Can't use FAIL/WHERE until we can implicate the callsite.
            ;
            ; https://github.com/metaeducation/ren-c/issues/587
            ;
            fail [
                "ENSURE failed with argument of type"
                    type of :value else ["NULL"]
            ]
        ])
    ]
)

non: redescribe [
    {Pass through value if it *doesn't* match test (e.g. MATCH/NOT)}
](
    enclose :match func [f] [
        let value: :f.value  ; DO makes frame arguments unavailable
        do f then [
            ; !!! Can't use FAIL/WHERE until we can implicate the callsite.
            ;
            ; https://github.com/metaeducation/ren-c/issues/587
            ;
            fail [
                "NON failed with argument of type"
                    type of :value else ["NULL"]
            ]
        ]
        :value
    ]
)


; !!! For a time this was THE, until it took its unevaluating role.  It is
; now called ENSURE-VALUE until a better name is thought of.
;
ensure-value: func [
    {FAIL if value is null, otherwise pass it through}

    return: [any-value!]
    value [any-value!]  ; not <opt>!
][
    ; Note: Doesn't check explicitly for NULL, as parameter is not marked <opt>
    :value
]

oneshot: specialize :n-shot [n: 1]
upshot: specialize :n-shot [n: -1]

;
; !!! The /REVERSE and /LAST refinements of FIND and SELECT caused a lot of
; bugs.  This recasts those refinements in userspace, in the hopes to reduce
; the combinatorics in the C code.  If needed, they could be made for SELECT.
;

find-reverse: redescribe [
    {Variant of FIND that uses a /SKIP of -1}
](
    specialize :find [skip: -1]
)

find-last: redescribe [
    {Variant of FIND that uses a /SKIP of -1 and seeks the TAIL of a series}
](
    adapt :find-reverse [
        if not any-series? series [
            fail @series "Can only use FIND-LAST on ANY-SERIES!"
        ]

        series: tail of series  ; can't use plain TAIL due to /TAIL refinement
    ]
)

attempt: func [
    {Tries to evaluate a block and returns result or NULL on error.}

    return: "NULL on error"
        [<opt> any-value!]
    code [block! action!]
][
    trap [
        return/no-decay (do code else [null])  ; want ~null~ isotope if was NULL
    ]
    return null
]

entrap: func [
    {If evaluation raises an error, return ERROR!...otherwise the ^META result}

    return: [<opt> any-value!]
    code [block! action!]
    <local> result
][
    any [trap/result code 'result, ^result]
]

reduce*: redescribe [
    "REDUCE a block but vaporize NULL Expressions"
](
    specialize :reduce [predicate: :null-to-void]
)

for-next: redescribe [
    "Evaluates a block for each position until the end, using NEXT to skip"
](
    specialize :for-skip [skip: 1]
)

for-back: redescribe [
    "Evaluates a block for each position until the start, using BACK to skip"
](
    specialize :for-skip [skip: -1]
)

iterate-skip: redescribe [
    "Variant of FOR-SKIP that directly modifies a series variable in a word"
](
    specialize enclose :for-skip func [f] [
        if blank? let word: f.word [return null]
        f.word: quote to word! word  ; do not create new virtual binding
        let saved: f.series: get word

        ; !!! https://github.com/rebol/rebol-issues/issues/2331
        comment [
            let result
            trap [result: do f] then e -> [
                set word saved
                fail e
            ]
            set word saved
            :result
        ]

        do f
        elide set word saved
    ][
        series: <overwritten>
    ]
)

iterate: iterate-next: redescribe [
    "Variant of FOR-NEXT that directly modifies a series variable in a word"
](
    specialize :iterate-skip [skip: 1]
)

iterate-back: redescribe [
    "Variant of FOR-BACK that directly modifies a series variable in a word"
](
    specialize :iterate-skip [skip: -1]
)


count-up: func [
    "Loop the body, setting a word from 1 up to the end value given"
    return: [<opt> any-value!]
    'var [word!]
    limit [<blank> integer! issue!]
    body [block!]
    <local> start end result'
][
    ; REPEAT in UPARSE wanted to try out some cutting-edge ideas about
    ; "opting in" to counting loops, e.g. `count-up i _` opts out and doesn't
    ; loop at all.  But what if `count-up i #` meant loop forever?  This
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
    return/no-decay (
        cycle [
            result': (^ cfor :var start end 1 body) else [
                return null  ; a BREAK was encountered
            ]
            if limit <> # [
                stop unmeta result'  ; the limit was actually reached
            ]
            ; otherwise keep going...
            end: end + 100
            start: start + 100
        ]
    )
]

count-down: redescribe [
    "Loop the body, setting a word from the end value given down to 1"
](
    specialize adapt :cfor [
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
    chain [specialize :copy [deep: #] | :freeze]
)

eval-all: func [
    {Evaluate any number of expressions and discard them}

    return: <void>
    expressions [<opt> any-value! <variadic>]
        {Any number of expressions on the right.}
][
    do expressions
]


; These constructs used to be enfix to complete their left hand side.  Yet
; that form of completion was only one expression's worth, when they wanted
; to allow longer runs of evaluation.  "Invisible functions" (those which
; `return: <void>`) permit a more flexible version of the mechanic.

<|: tweak copy :eval-all 'postpone on
|>: tweak enfixed :shove 'postpone on


meth: enfixed func [
    {FUNC variant that creates an ACTION! implicitly bound in a context}

    return: [action!]
    :member [set-word! set-path!]
    spec [block!]
    body [block!]
    /gather "Temporary compatibility tweak for METHOD (until synonymous)"
][
    let context: binding of member else [
        fail [member "must be bound to an ANY-CONTEXT! to use METHOD"]
    ]
    return set member bind (
        apply :func [
            compose [((spec)) <in> (context)]
            body
            /gather gather
        ]
    ) context
]


cause-error: func [
    "Causes an immediate error throw with the provided information."
    err-type [word!]
    err-id [word!]
    args
][
    args: blockify :args  ; make sure it's a block

    ; Filter out functional values:
    iterate args [
        if action? first args [
            change args ^(meta-of first args)
        ]
    ]

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
; !!! Should there be a special bit or dispatcher used on the FAIL to ensure
; it does not continue running?  `return: <divergent>`?
;
; Though HIJACK would have to be aware of it and preserve the rule.
;
fail: func [
    {Interrupts execution by reporting an error (a TRAP can intercept it).}

    return: []  ; !!! notation for divergent function?
    'blame "Point to variable or parameter to blame"
        [<skip> the-word! the-path!]
    reason "ERROR! value, ID, URL, message text, or failure spec"
        [<end> error! word! path! url! text! block!]
    /where "Frame or parameter at which to indicate the error originated"
        [frame! any-word!]
][
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

    ; !!! PATH! doesn't do BINDING OF, and in the general case it couldn't
    ; tell you where it resolved to without evaluating, just do WORD! for now.
    ;
    let frame: try match frame! binding of try match the-word! :blame

    let error: switch type of :reason [
        error! [reason]
        text! [make error! reason]
        word! [
            make error! [
                type: 'User
                id: reason
                message: to text! reason
            ]
        ]
        path! [
            if word? last reason [
                make error! [
                    type: 'User
                    id: last reason
                    message: to text! reason
                ]
            ] else [
                make error! to text! reason
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
        null? reason so make error! compose [
            type: 'Script
            ((case [
                frame and (blame) '[
                    id: 'invalid-arg
                    arg1: label of frame
                    arg2: as word! uppercase make text! blame
                    arg3: get blame
                ]
                frame and (not blame) '[
                    id: 'no-arg
                    arg1: label of frame
                    arg2: as word! uppercase make text! blame
                ]
                blame and (get blame) '[
                    id: 'bad-value
                    arg1: get blame
                ]
            ] else '[
                id: 'unknown-error
            ]))
        ]
    ]

    if not pick error 'where [
        ;
        ; If no specific location specified, and error doesn't already have a
        ; location, make it appear to originate from the frame calling FAIL.
        ;
        where: default [any [frame, binding of 'return]]

        set-location-of-error error where  ; !!! why is this native?
    ]

    do ensure error! error  ; raise to nearest TRAP up the stack (if any)
]


generate: func [ "Make a generator."
    init [block!] "Init code"
    condition [block! blank!] "While condition"
    iteration [block!] "Step code"
][
    let words: make block! 2
    for-each x reduce [init condition iteration] [
        if not block? x [continue]
        let w: collect-words/deep/set x
        if not empty? intersect w [count result] [ fail [
            "count: and result: set-words aren't allowed in" mold x
        ]]
        append words w
    ]
    let spec: compose [/reset [block!] <static> ((unique words)) count]
    let body: compose/deep [
        if reset [count: reset return]
        if block? count [
            let result: bind count 'count
            count: 1
            return do result
        ]
        count: me + 1
        let result: (to group! iteration)
        ((either empty? condition
            [[ return result ]]
            [compose [ return if (to group! condition) [result] ]]
        ))
    ]
    let f: function spec body
    f/reset init
    return :f
]

read-lines: func [
    {Makes a generator that yields lines from a file or port.}
    src [port! file! blank!]
    /delimiter [binary! char! text! bitset!]
    /keep "Don't remove delimiter"
    /binary "Return BINARY instead of TEXT"
][
    if blank? src [src: system.ports.input]
    if file? src [src: open src]

    let pos
    let rule: compose/deep either delimiter [
        either keep
        [ [thru (delimiter) pos: <here>] ]
        [ [to (delimiter) remove (delimiter) pos: <here>] ]
    ][
        [
            to crlf opt some [
                ["^M" and not "^/"]
                to crlf
            ] (if not keep ['remove]) ["^/" | "^M^/"] pos: <here>
        ]
    ]

    let f: function compose [
        <static> buffer (to group! [make binary! 4096])
        <static> port (groupify src)
    ] compose/deep [
        let crlf: charset "^/^M"
        let data: _
        let eof: false
        cycle [
            pos: _
            parse3 buffer (rule)
            if pos [break]
            ((if same? src system.ports.input
                '[data: read port]
                else
                '[data: read/part port 4096]
            ))
            if empty? data [
                eof: true
                pos: tail of buffer
                break
            ]
            append buffer data
        ]
        if all [eof empty? buffer] [return null]
        ((if not binary '[to text!])) take/part buffer pos
    ]
]

input-lines: redescribe [
    {Makes a generator that yields lines from system.ports.input.}
](
    specialize :read-lines [src: _]
)
