REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Base: Function Constructors"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Note: {
        This code is evaluated just after actions, natives, sysobj, and other lower
        levels definitions. This file intializes a minimal working environment
        that is used for the rest of the boot.
    }
]

assert: func [
    {Ensure conditions are conditionally true if hooked by debugging}

    return: []
    conditions [block!]
        {Block of conditions to evaluate and test for logical truth}
][
    ; ASSERT has no default implementation, but can be HIJACKed by a debug
    ; mode with a custom validation or output routine.
    ;
    ; !!! R3-Alpha and Rebol2 did not distinguish ASSERT and VERIFY, since
    ; there was no idea of a "debug mode"
]


maybe: enfix func [
    "Set word or path to a default value if that value is a value"

    return: [<opt> any-value!]
    'target [set-word! set-path!]
        "The word to which might be set"
    optional [<opt> any-value!]
        "Value to assign only if it is not null"

    <local> gotten
][
    if semiquoted? 'optional [
        ;
        ; While DEFAULT requires a BLOCK!, MAYBE does not.  Catch mistakes
        ; such as `x: maybe [...]`
        ;
        fail 'optional [
            "Literal" type of :optional "used w/MAYBE, use () if intentional"
        ]
    ]

    case [
        set-word? target [
            if null? :optional [return get target]
            set target :optional
        ]

        set-path? target [
            ; If a SET-PATH!, it may contain a GROUP!.  SET/GET don't accept
            ; that due to potential side-effects, so use REDUCE.  See also:
            ;
            ; https://github.com/rebol/rebol-issues/issues/2275
            ;
            if null? :optional [return do compose [(as get-path! target)]]
            do compose [(target) lit ((:optional))]
        ]
    ]
]


steal: func [
    {Return a variable's value prior to an assignment, then do the assignment}

    return: [<opt> any-value!]
        {Value of the following SET-WORD! or SET-PATH! before assignment}
    evaluation [<opt> any-value! <...>]
        {Used to take the assigned value}
    :look [set-word! set-path! <...>]
][
    get first look  ; returned value

    elide take evaluation
]

assert [null = binding of :return]  ; it's archetypal, nowhere to return to
unset 'return  ; so don't let the archetype be visible

function: func [
    {Make action with set-words as locals, <static>, <in>, <with>, <local>}
    return: [action!]
    spec [block!]
        {Help string (opt) followed by arg words (and opt type and string)}
    body [block!]
        {The body block of the function}
    <local>
        new-spec var other
        new-body exclusions locals defaulters statics
][
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

    new-body: _
    statics: _
    defaulters: _
    var: <dummy> ;-- want to enter PARSE with truthy state (gets overwritten)

    ;; dump [spec]

    ; Gather the SET-WORD!s in the body, excluding the collected ANY-WORD!s
    ; that should not be considered.  Note that COLLECT is not defined by
    ; this point in the bootstrap.
    ;
    ; !!! REVIEW: ignore self too if binding object?
    ;
    parse spec [any [
        <void> (append new-spec <void>)
    |
        ((either var [[
            set var: [
                match [any-word! 'word!]
                | ahead path! into [blank! word!]
            ](
                append new-spec var

                ;-- exclude args/refines
                append exclusions either path? var [var/2] [var]
            )
            |
            set other: block! (
                append/only new-spec other ;-- data type blocks
            )
            |
            copy other some text! (
                append/only new-spec spaced other ;-- spec notes
            )
        ]][[
            set var: set-word! ( ;-- locals legal anywhere
                append exclusions var
                append new-spec var
                var: _
            )
        ]]))
    |
        other:
        group! (
            if not var [
                fail [
                    ; <where> spec
                    ; <near> other
                    "Default value not paired with argument:" (mold other/1)
                ]
            ]
            defaulters: default [copy []]
            append defaulters compose/deep [
                (as set-word! var) default [(uneval do other/1)]
            ]
        )
    |
        (var: _) ;-- everything below this line resets var
        fail ;-- failing here means rolling over to next rule
    |
        <local>
        any [set var: word! (other: _) opt set other: group! (
            append new-spec as set-word! var
            append exclusions var
            if other [
                defaulters: default [copy []]
                append defaulters compose/deep [ ;-- always sets
                    (as set-word! var) (uneval do other)
                ]
            ]
        )]
        (var: _) ;-- don't consider further GROUP!s or variables
    |
        <in> (
            new-body: default [
                append exclusions 'self
                copy/deep body
            ]
        )
        any [
            set other: [object! | word! | path!] (
                if not object? other [other: ensure any-context! get other]
                bind new-body other
                for-each [key val] other [
                    append exclusions key
                ]
            )
        ]
    |
        <with> any [
            set other: [word! | path!] (append exclusions other)
        |
            text! ;-- skip over as commentary
        ]
    |
        <static> (
            statics: default [copy []]
            new-body: default [
                append exclusions 'self
                copy/deep body
            ]
        )
        any [
            set var: word! (other: _) opt set other: group! (
                append exclusions var
                append statics compose [
                    (as set-word! var) ((other))
                ]
            )
        ]
        (var: _)
    |
        end accept
    |
        other: (
            print mold other/1
            fail [
                ; <where> spec
                ; <near> other
                "Invalid spec item:" (mold other/1)
            ]
        )
    ] end]

    locals: collect-words/deep/set/ignore body exclusions

    ;; dump [{before} statics new-spec exclusions]

    if statics [
        statics: make object! statics
        bind new-body statics
    ]

    ; !!! The words that come back from COLLECT-WORDS are all WORD!, but we
    ; need SET-WORD! to specify pure locals to the generators.  Review the
    ; COLLECT-WORDS interface to efficiently give this result, as well as
    ; a possible COLLECT-WORDS/INTO
    ;
    for-each loc locals [
        append new-spec to set-word! loc
    ]

    ;; dump [{after} new-spec defaulters]

    func new-spec either defaulters [
        append/only defaulters as group! any [new-body body]
    ][
        any [new-body body]
    ]
]


; Actions can be chained, adapted, and specialized--repeatedly.  The meta
; information from which HELP is determined can be inherited through links
; in that meta information.  Though in order to mutate the information for
; the purposes of distinguishing a derived action, it must be copied.
;
dig-action-meta-fields: function [value [action!]] [
    meta: meta-of :value else [
        return make system/standard/action-meta [
            description: _
            return-type: _
            return-note: _
            parameter-types: make frame! :value
            parameter-notes: make frame! :value
        ]
    ]

    underlying: try ensure [<opt> action!] any [
        select meta 'specializee
        select meta 'adaptee
        first try match block! select meta 'chainees
    ]

    fields: try all [:underlying | dig-action-meta-fields :underlying]

    inherit-frame: function [parent [<blank> frame!]] [
        child: make frame! :value
        for-each param words of child [ ;-- `for-each param child` locks child
            child/(param): maybe select parent param
        ]
        return child
    ]

    return make system/standard/action-meta [
        description: try ensure [<opt> text!] any [
            select meta 'description
            copy try select fields 'description
        ]
        return-type: try ensure [<opt> block!] any [
            select meta 'return-type
            copy try select fields 'return-type
        ]
        return-note: try ensure [<opt> text!] any [
            select meta 'return-note
            copy try select fields 'return-note
        ]
        parameter-types: try ensure [<opt> frame!] any [
            select meta 'parameter-types
            inherit-frame try select fields 'parameter-types
        ]
        parameter-notes: try ensure [<opt> frame!] any [
            select meta 'parameter-notes
            inherit-frame try select fields 'parameter-notes
        ]
    ]
]


redescribe: function [
    {Mutate action description with new title and/or new argument notes.}

    return: [action!]
        {The input action, with its description now updated.}
    spec [block!]
        {Either a string description, or a spec block (without types).}
    value [action!]
        {(modified) Action whose description is to be updated.}
][
    meta: meta-of :value
    notes: _

    ; For efficiency, objects are only created on demand by hitting the
    ; required point in the PARSE.  Hence `redescribe [] :foo` will not tamper
    ; with the meta information at all, while `redescribe [{stuff}] :foo` will
    ; only manipulate the description.

    on-demand-meta: does [
        meta: default [set-meta :value copy system/standard/action-meta]

        if not find meta 'description [
            fail [{archetype META-OF doesn't have DESCRIPTION slot} meta]
        ]

        if notes: try select meta 'parameter-notes [
            if not frame? notes [
                fail [{PARAMETER-NOTES in META-OF is not a FRAME!} notes]
            ]

          ;; !!! Getting error on equality test from expired frame...review
          comment [
            if not equal? :value (action of notes) [
                fail [{PARAMETER-NOTES in META-OF frame mismatch} notes]
            ]
          ]
        ]
    ]

    ; !!! SPECIALIZEE and SPECIALIZEE-NAME will be lost if a REDESCRIBE is
    ; done of a specialized function that needs to change more than just the
    ; main description.  Same with ADAPTEE and ADAPTEE-NAME in adaptations.
    ;
    ; (This is for efficiency to not generate new keylists on each describe
    ; but to reuse archetypal ones.  Also to limit the total number of
    ; variations that clients like HELP have to reason about.)
    ;
    on-demand-notes: does [ ;-- was a DOES CATCH, removed during DOES tweaking
        on-demand-meta

        if find meta 'parameter-notes [
            fields: dig-action-meta-fields :value

            meta: _ ;-- need to get a parameter-notes field in the OBJECT!
            on-demand-meta ;-- ...so this loses SPECIALIZEE, etc.

            description: meta/description: fields/description
            notes: meta/parameter-notes: fields/parameter-notes
            types: meta/parameter-types: fields/parameter-types
        ]
    ]
    parse spec [
        opt [
            copy description any text! (
                description: spaced description
                either all [equal? description {} | not meta] [
                    ; No action needed (no meta to delete old description in)
                ][
                    on-demand-meta
                    meta/description: if equal? description {} [
                        _
                    ] else [
                        description
                    ]
                ]
            )
        ]
        any [
            set param: [
                word! | get-word! | lit-word! | set-word!
                | ahead path! into [word! blank!]
            ](
                if path? param [param: param/1]
            )

            ; It's legal for the redescribe to name a parameter just to
            ; show it's there for descriptive purposes without adding notes.
            ; But if {} is given as the notes, that's seen as a request
            ; to delete a note.
            ;
            opt [[copy note some text!] (
                note: spaced note
                on-demand-meta
                either equal? param (lit return:) [
                    meta/return-note: all [
                        not equal? note {}
                        copy note
                    ]
                ][
                    if notes or [not equal? note {}] [
                        on-demand-notes

                        if not find notes as word! param [
                            fail [param "not found in frame to describe"]
                        ]

                        actual: first find parameters of :value param
                        if not strict-equal? param actual [
                            fail [param {doesn't match word type of} actual]
                        ]

                        notes/(as word! param): if not equal? note {} [note]
                    ]
                ]
            )]
        ]
        end
    ] else [
        fail [{REDESCRIBE specs should be STRING! and ANY-WORD! only:} spec]
    ]

    ; If you kill all the notes then they will be cleaned up.  The meta
    ; object will be left behind, however.
    ;
    if notes and [every [param note] notes [unset? 'note]] [
        meta/parameter-notes: _
    ]

    :value ;-- should have updated the meta
]


redescribe [
    {Create an ACTION, implicity gathering SET-WORD!s as <local> by default}
] :function


so: enfix func [
    {Postfix assertion which won't keep running if left expression is false}

    return: [<opt> any-value!]
    condition "Condition to test, must resolve to a LOGIC! (use DID, NOT)"
        [logic!]
    feed [<opt> <end> any-value! <...>]
][
    if not condition [
        fail 'condition make error! [
            type: 'Script
            id: 'assertion-failure
            arg1: compose [((:condition)) so]
        ]
    ]
    if tail? feed [return void]
    set* lit feed: take feed
    if (block? :feed) and [semiquoted? 'feed] [
        fail "Don't use literal block as SO right hand side, use ([...])"
    ]
    return :feed
]
tweak :so #postpone on


matched: enfix redescribe [
    "Assert that the left hand side--when fully evaluated--MATCHES the right"
](
    enclose :matches function [f [frame!]] [
        test: :f/test  ; save for reporting, note DO F makes F/XXX unavailable
        value: :f/value  ; returned value

        if not do f [
            fail 'f make error! [
                type: 'Script
                id: 'assertion-failure
                arg1: compose [(:value) matches (:test)]
            ]
        ]
        :value
    ]
)
tweak :matched #postpone on

; Rare case where a `?` variant is useful, to avoid VOID! on falsey matches
match?: chain [:match | :value?]


was: enfix redescribe [
    "Assert that the left hand side--when fully evaluated--IS the right"
](
    function [left [<opt> any-value!] right [<opt> any-value!]] [
        if :left != :right [
            fail 'return make error! [
                type: 'Script
                id: 'assertion-failure
                arg1: compose [(:left) is (:right)]
            ]
        ]
        :left  ; choose left in case binding or case matters somehow
    ]
)
tweak :was #postpone on


zdeflate: redescribe [
    {Deflates data with zlib envelope: https://en.wikipedia.org/wiki/ZLIB}
](
    specialize 'deflate/envelope [format: 'zlib]
)

zinflate: redescribe [
    {Inflates data with zlib envelope: https://en.wikipedia.org/wiki/ZLIB}
](
    specialize 'inflate/envelope [format: 'zlib]
)

gzip: redescribe [
    {Deflates data with gzip envelope: https://en.wikipedia.org/wiki/Gzip}
](
    specialize 'deflate/envelope [format: 'gzip]
)

gunzip: redescribe [
    {Inflates data with gzip envelope: https://en.wikipedia.org/wiki/Gzip}
](
    specialize 'inflate/envelope [format: 'gzip] ;-- What about GZIP-BADSIZE?
)


default*: enfix redescribe [
    {Would be the same as DEFAULT/ONLY if paths could dispatch infix}
](
    specialize 'default [only: true]
)


skip*: redescribe [
    {Variant of SKIP that returns NULL instead of clipping to series bounds}
](
    specialize 'skip [only: true]
)

set*: redescribe [
    {Variant of SET that allows a null to actually unset the variable}
](
    specialize 'set [opt: true]
)

ensure: redescribe [
    {Pass through value if it matches test, otherwise trigger a FAIL}
](
    specialize 'either-match [
        branch: func [arg [<opt> any-value!]] [
            ;
            ; !!! Can't use FAIL/WHERE until there is a good way to SPECIALIZE
            ; a conditional with a branch referring to invocation parameters:
            ;
            ; https://github.com/metaeducation/ren-c/issues/587
            ;
            fail [
                "ENSURE failed with argument of type"
                    type of :arg else ["NULL"]
            ]
        ]
    ]
)

really: func [
    {FAIL if value is null, otherwise pass it through}

    return: [any-value!]
    value [any-value!] ;-- always checked for null, since no <opt>
][
    ; While DEFAULT requires a BLOCK!, REALLY does not.  Catch mistakes such
    ; as `x: really [...]`
    ;
    if semiquoted? 'value [
        fail 'value [
            "Literal" type of :value "used w/REALLY, use () if intentional"
        ]
    ]

    :value
]

oneshot: specialize 'n-shot [n: 1]
upshot: specialize 'n-shot [n: -1]

take: redescribe [
    {Variant of TAKE* that will give an error if it can't take, vs. null}
](
    chain [
        :take*
            |
        specialize 'else [
            branch: [
                fail "Can't TAKE from series end (see TAKE* to get null)"
            ]
        ]
    ]
)

attempt: func [
    {Tries to evaluate a block and returns result or NULL on error.}

    return: "null on error, if code runs and produces null it becomes void"
        [<opt> any-value!]
    code [block! action!]
][
    trap [
        return do code ;; VOIDIFY of null avoids conflation, but is overkill
    ]
    null ;; don't look at trapped error value, just return null
]

for-next: redescribe [
    "Evaluates a block for each position until the end, using NEXT to skip"
](
    specialize 'for-skip [skip: 1]
)

for-back: redescribe [
    "Evaluates a block for each position until the start, using BACK to skip"
](
    specialize 'for-skip [skip: -1]
)

iterate-skip: redescribe [
    "Variant of FOR-SKIP that directly modifies a series variable in a word"
](
    specialize enclose 'for-skip function [f] [
        if blank? word: f/word [return null]
        f/word: uneval to word! word ;-- do not create new virtual binding
        saved: f/series: get word

        ; !!! https://github.com/rebol/rebol-issues/issues/2331
        comment [
            trap [set* lit result: do f] then (lambda e [
                set* word saved
                fail e
            ])
            set* word saved
            :result
        ]

        do f
        elide set* word saved
    ][
        series: <overwritten>
    ]
)

iterate: iterate-next: redescribe [
    "Variant of FOR-NEXT that directly modifies a series variable in a word"
](
    specialize 'iterate-skip [skip: 1]
)

iterate-back: redescribe [
    "Variant of FOR-BACK that directly modifies a series variable in a word"
](
    specialize 'iterate-skip [skip: -1]
)


count-up: redescribe [
    "Loop the body, setting a word from 1 up to the end value given"
](
    specialize 'for [start: 1 | bump: 1]
)

count-down: redescribe [
    "Loop the body, setting a word from the end value given down to 1"
](
    specialize adapt 'for [
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
    specialize 'lock [clone: true]
)


;-- => cannot be loaded by R3-Alpha, or even earlier Ren-C
;
lambda: function [
    {Convenience variadic wrapper for MAKE ACTION!}

    return: [action!]
    :args [<end> word! block!]
        {Block of argument words, or a single word (if only one argument)}
    :body [any-value! <...>]
        {Block that serves as the body or variadic elements for the body}
][
    make action! compose [
        (blockify :args)
        (const if block? first body [take body] else [make block! body])
    ]
]


eval-all: func [
    {Evaluate any number of expressions and discard them}

    return: []
    expressions [<opt> any-value! <...>]
        {Any number of expressions on the right.}
][
    do expressions
]


once-bar: func [
    {Expression barrier that's willing to only run one expression after it}

    return: [<opt> any-value!]
    right [<opt> <end> any-value! <...>]
    :lookahead [any-value! <...>]
    look:
][
    take* right ;-- returned value

    elide any [
        tail? right
            |
        '|| = look: take lookahead ;-- hack...recognize selfs
    ] else [
        fail 'right [
            "|| expected single expression, found residual of" :look
        ]
    ]
]


method: enfix func [
    {FUNCTION variant that creates an ACTION! implicitly bound in a context}

    return: [action!]
    :member [set-word! set-path!]
    spec [block!]
    body [block!]
    <local> context
][
    context: binding of member else [
        fail [member "must be bound to an ANY-CONTEXT! to use METHOD"]
    ]
    set member bind (function compose [((spec)) <in> ((context))] body) context
]

meth: enfix func [
    {FUNC variant that creates an ACTION! implicitly bound in a context}

    return: [action!]
    :member [set-word! set-path!]
    spec [block!]
    body [block!]
    <local> context
][
    context: binding of member else [
        fail [target "must be bound to an ANY-CONTEXT! to use METH"]
    ]

    ; !!! This is somewhat inefficient because <in> is currently implemented
    ; in usermode...so the body will get copied twice.  The contention is
    ; between not wanting to alter the bindings in the caller's body variable
    ; and wanting to update them for the purposes of the FUNC.  Review.
    ;
    set member bind (func spec bind copy/deep body context) context
]


module: func [
    {Creates a new module.}

    spec "The header block of the module (modified)"
        [block! object!]
    body "The body block of the module (modified)"
        [block!]
    /mixin "Mix in words from other modules"
    mixins "Words collected into an object"
        [object!]
    /into "Add data to existing MODULE! context (vs making a new one)"
    mod [module!]

    <local> hidden w
][
    mixins: default [_]

    ; !!! Is it a good idea to mess with the given spec and body bindings?
    ; This was done by MODULE but not seemingly automatically by MAKE MODULE!
    ;
    unbind/deep body

    ; Convert header block to standard header object:
    ;
    if block? :spec [
        unbind/deep spec
        spec: try attempt [construct/with/only :spec system/standard/header]
    ]

    ; Historically, the Name: and Type: fields would tolerate either LIT-WORD!
    ; or WORD! equally well.  This is because it used R3-Alpha's CONSTRUCT,
    ; (which was non-evaluative by default, unlike Ren-C's construct) but
    ; without the /ONLY switch.  In that mode, it decayed LIT-WORD! to WORD!.
    ; To try and standardize the variance, Ren-C does not accept LIT-WORD!
    ; in these slots.
    ;
    ; !!! Although this is a goal, it creates some friction.  Backing off of
    ; it temporarily.
    ;
    if lit-word? spec/name [
        spec/name: as word! spec/name
        ;fail ["Ren-C module Name:" (spec/name) "must be WORD!, not LIT-WORD!"]
    ]
    if lit-word? spec/type [
        spec/type: as word! spec/type
        ;fail ["Ren-C module Type:" (spec/type) "must be WORD!, not LIT-WORD!"]
    ]

    ; Validate the important fields of header:
    ;
    ; !!! This should be an informative error instead of asserts!
    ;
    for-each [var types] [
        spec object!
        body block!
        mixins [object! blank!]
        spec/name [word! blank!]
        spec/type [word! blank!]
        spec/version [tuple! blank!]
        spec/options [block! blank!]
    ][
        do compose [ensure (types) (var)]  ; names to show if fails
    ]

    ; In Ren-C, MAKE MODULE! acts just like MAKE OBJECT! due to the generic
    ; facility for SET-META.

    mod: default [
        make module! 7 ; arbitrary starting size
    ]

    if find spec/options 'extension [
        append mod 'lib-base ; specific runtime values MUST BE FIRST
    ]

    if not spec/type [spec/type: 'module] ; in case not set earlier

    ; Collect 'export keyword exports, removing the keywords
    if find body 'export [
        if not block? select spec 'exports [
            append spec compose [exports (make block! 10)]
        ]

        ; Note: 'export overrides 'hidden, silently for now
        parse body [while [
            to 'export remove skip opt remove 'hidden opt
            [
                set w any-word! (
                    if not find spec/exports w: to word! w [
                        append spec/exports w
                    ]
                )
            |
                set w block! (
                    append spec/exports collect-words/ignore w spec/exports
                )
            ]
        ] to end]
    ]

    ; Collect 'hidden keyword words, removing the keywords. Ignore exports.
    hidden: _
    if find body 'hidden [
        hidden: make block! 10
        ; Note: Exports are not hidden, silently for now
        parse body [while [
            to 'hidden remove skip opt
            [
                set w any-word! (
                    if not find select spec 'exports w: to word! w [
                        append hidden w
                    ]
                )
            |
                set w block! (
                    append hidden collect-words/ignore w select spec 'exports
                )
            ]
        ] to end]
    ]

    ; Add hidden words next to the context (performance):
    if block? hidden [bind/new hidden mod]

    if block? hidden [protect/hide/words hidden]

    set-meta mod spec

    ; Add exported words at top of context (performance):
    if block? select spec 'exports [bind/new spec/exports mod]

    either find spec/options 'isolate [
        ;
        ; All words of the module body are module variables:
        ;
        bind/new body mod

        ; The module keeps its own variables (not shared with system):
        ;
        if object? mixins [resolve mod mixins]

        resolve mod lib
    ][
        ; Only top level defined words are module variables.
        ;
        bind/only/set body mod

        ; The module shares system exported variables:
        ;
        bind body lib

        if object? mixins [bind body mixins]
    ]

    bind body mod ;-- redundant?
    do body

    ;print ["Module created" spec/name spec/version]
    mod
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
            change/only args meta-of first args
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


; !!! Should there be a special bit or dispatcher used on the FAIL to ensure
; it does not continue running?  `return: []` is already taken for the
; "invisible" meaning, but it could be an optimized dispatcher used in
; wrapping, e.g.:
;
;     fail: noreturn func [...] [...]
;
; Though HIJACK would have to be aware of it and preserve the rule.
;
fail: function [
    {Interrupts execution by reporting an error (a TRAP can intercept it).}

    :blame "Point to variable or parameter to blame"
        [<skip> 'word! 'path!]
    reason "ERROR! value, message text, or failure spec"
        [<end> error! text! block!]
    /where "Specify an originating location other than the FAIL itself"
    location "Frame or parameter at which to indicate the error originated"
        [frame! any-word!]
][
    ; Ultimately we might like FAIL to use some clever error-creating dialect
    ; when passed a block, maybe something like:
    ;
    ;     fail [<invalid-key> {The key} key-name: key {is invalid}]
    ;
    ; That could provide an error ID, the format message, and the values to
    ; plug into the slots to make the message...which could be extracted from
    ; the error if captured (e.g. error/id and `error/key-name`.  Another
    ; option would be something like:
    ;
    ;     fail/with [{The key} :key-name {is invalid}] [key-name: key]

    ; !!! PATH! doesn't do BINDING OF, and in the general case it couldn't
    ; tell you where it resolved to without evaluating, just do WORD! for now.
    ;
    frame: try match frame! binding of try match 'word! :blame

    error: switch type of :reason [
        error! [reason]
        text! [make error! reason]
        block! [
            make error! (spaced reason else [
                Type: 'Script
                id: 'unknown-error
            ])
        ]
    ] else [
        not set? 'reason so make error! compose [
            Type: 'Script
            ((case [
                frame and [set? blame] [[
                    id: 'invalid-arg
                    arg1: label of frame
                    arg2: blame
                    arg3: get blame
                ]]
                frame and [unset? blame] [[
                    id: 'no-arg
                    arg1: label of frame
                    arg2: blame
                ]]
                :blame and [set? blame] [[
                    id: 'bad-value
                    arg1: get blame
                ]]
                default [[
                    id: 'unknown-error
                ]]
            ]))
        ]
    ]

    if not pick error 'where [
        ;
        ; If no specific location specified, and error doesn't already have a
        ; location, make it appear to originate from the frame calling FAIL.
        ;
        location: default [frame or [binding of 'reason]]

        set-location-of-error error location  ; !!! must this be a native?
    ]

    do ensure error! error  ; raise to nearest TRAP up the stack (if any)
]

generate: function [ "Make a generator."
    init [block!] "Init code"
    condition [block! blank!] "while condition"
    iteration [block!] "m"
][
    words: make block! 2
    for-each x reduce [init condition iteration] [
        if not block? x [continue]
        w: collect-words/deep/set x
        if not empty? intersect w [count result] [ fail [
            "count: and result: set-words aren't allowed in" mold x
        ]]
        append words w
    ]
    words: unique words
    spec: flatten map-each w words [reduce [<static> w]]
    append spec [<static> count]
    insert spec [/reset init [block!]]
    body: compose/deep [
        if reset [count: init return]
        if block? count [
            result: bind count 'count
            count: 1
            return do result
        ]
        count: me + 1
        result: (to group! iteration)
        ((either empty? condition
            [[ return result ]]
            [compose [ return if (to group! condition) [result] ]]
        ))
    ]
    f: function spec body
    f/reset init
    :f
]

read-lines: function [
    {Makes a generator that yields lines from a file or port.}
    src [port! file! blank!]
    /delimiter eol [binary! char! text! bitset!]
    /keep "Don't remove delimiter"
    /binary "Return BINARY instead of TEXT"
][
    if blank? src [src: system/ports/input]
    if file? src [src: open src]

    crlf: charset "^/^M"
    rule: compose/deep/only either delimiter [
        either keep
        [ [[thru (eol) pos:]] ]
        [ [[to (eol) remove (eol) pos:]] ]
    ][
        [[
            to (crlf) any [
                ["^M" and not "^/"]
                to (crlf)
            ] (if not keep ['remove]) ["^/" | "^M^/"] pos:
        ]]
    ]

    f: function compose [
        <static> buffer (to group! [make binary! 4096])
        <static> port (groupify src)
    ] compose/deep [
        data: _
        cycle [
            pos: _
            parse buffer (rule)
            if pos [break]
            if same? port system/ports/input
            [ data: read port ]
            else
            [ data: read/part port 4096 ]
            if empty? data [
                pos: length of buffer
                break
            ]
            append buffer data
        ]
        if all [empty? data empty? buffer] [
            return null
        ]
        (if not binary [[to text!]]) take/part buffer pos
    ]
]

input-lines: redescribe [
    {Makes a generator that yields lines from system/ports/input.}
](
    specialize :read-lines [src: _]
)
