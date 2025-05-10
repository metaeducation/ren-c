Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Boot Base: Function Constructors"
    rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    license: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    notes: {
        This code is evaluated just after actions, natives, sysobj, and other lower
        levels definitions. This file intializes a minimal working environment
        that is used for the rest of the boot.
    }
]

?: opt: optional/

assert: func [
    {Ensure conditions are conditionally true if hooked by debugging}

    return: [~]
    conditions [block!]
        {Block of conditions to evaluate and test for logical truth}
][
    ; ASSERT has no default implementation, but can be HIJACKed by a debug
    ; mode with a custom validation or output routine.
    ;
    ; !!! R3-Alpha and Rebol2 did not distinguish ASSERT and VERIFY, since
    ; there was no idea of a "debug mode"
]

raise: func [error [error!]] [  ; poor man's definitional error
    return error
]

so: infix func [
    {Postfix assertion which won't keep running if left expression is false}

    return: [~]
    condition "Condition to test (voids are treated as false)"
        [any-value!]
][
    any [condition null] else [
        panic/blame ["Postfix 'SO assertion' failed"] 'condition
    ]
]

assert [null = binding of :return] ;-- it's archetypal, nowhere to return to
return: ~  ; so don't let the archetype be visible

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

    new-body: null
    statics: null
    defaulters: null
    var: <dummy> ;-- want to enter PARSE with truthy state (gets overwritten)

    ;; dump [spec]

    ; Gather the SET-WORD!s in the body, excluding the collected ANY-WORD!s
    ; that should not be considered.  Note that COLLECT is not defined by
    ; this point in the bootstrap.
    ;
    ; !!! REVIEW: ignore self too if binding object?
    ;
    parse spec [opt some [
        the <void> (append new-spec <void>)
    |
        when (var) [
            var: any-word! (
                append exclusions var ;-- exclude args/refines
                append new-spec var
            )
        |
            other: [block! | text!] (
                append/only new-spec other ;-- spec notes or data type blocks
            )
        ]
    |
        when (not var) [
            var: set-word! ( ;-- locals legal anywhere
                append exclusions var
                append new-spec var
                var: null
            )
        ]
    |
        other: <here>
        group! (
            if not var [
                panic [
                    ; <where> spec
                    ; <near> other
                    "Default value not paired with argument:" (mold other/1)
                ]
            ]
            defaulters: default [copy []]
            append defaulters compose/deep [
                (as set-word! var) default [(meta eval other/1)]
            ]
        )
    |
        (var: null)  ; everything below this line resets var
        bypass  ; rolling over to next alternate
    |
        the <local>
        opt some [var: word! other: try group! (
            append new-spec as set-word! var
            append exclusions var
            if other [
                defaulters: default [copy []]
                append defaulters compose/deep [ ;-- always sets
                    (as set-word! var) (meta eval other)
                ]
            ]
        )]
        (var: null) ;-- don't consider further GROUP!s or variables
    |
        the <in> (
            new-body: default [
                append exclusions 'self
                copy/deep body
            ]
        )
        opt some [
            other: [object! | word! | path!] (
                if not object? other [other: ensure any-context! get other]
                bind new-body other
                for-each [key val] other [
                    append exclusions key
                ]
            )
        ]
    |
        the <with> opt some [
            other: [word! | path!] (append exclusions other)
        |
            text! ;-- skip over as commentary
        ]
    |
        the <static> (
            statics: default [copy []]
            new-body: default [
                append exclusions 'self
                copy/deep body
            ]
        )
        opt some [
            var: word! other: try group! (
                append exclusions var
                append statics compose [
                    (as set-word! var) (other)
                ]
            )
        ]
        (var: null)
    |
        <end> accept <here>
    |
        other: (
            print mold other/1
            panic [
                ; <where> spec
                ; <near> other
                "Invalid spec item:" (mold other/1)
            ]
        )
    ]]

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

    return func new-spec either defaulters [
        append/only defaulters as group! any [new-body body]
    ][
        any [new-body body]
    ]
]


; Actions can be cascaded, adapted, and specialized--repeatedly.  The meta
; information from which HELP is determined can be inherited through links
; in that meta information.  Though in order to mutate the information for
; the purposes of distinguishing a derived action, it must be copied.
;
dig-action-meta-fields: function [
    return: [object!]
    value [action!]
][
    meta: meta-of :value else [
        return make system/standard/action-meta [
            description: null
            return-type: null
            return-note: null
            parameter-types: make frame! :value
            parameter-notes: make frame! :value
        ]
    ]

    underlying: ensure [~null~ action!] any [
        get 'meta/specializee
        get 'meta/adaptee
        all [
            block? :meta/pipeline
            first meta/pipeline
        ]
    ]

    fields: all [
        :underlying
        dig-action-meta-fields :underlying
    ]

    inherit-frame: function [
        return: [frame!]
        parent [<opt-out> frame!]
    ][
        child: make frame! :value
        for-each param child [
            child/(param): any [
                select parent param
                :child/param
            ]
        ]
        return child
    ]

    return make system/standard/action-meta [
        description: ensure [~null~ text!] any [
            select meta 'description
            copy opt select opt fields 'description
        ]
        return-type: ensure [~null~ block!] any [
            select meta 'return-type
            copy opt select opt fields 'return-type
        ]
        return-note: ensure [~null~ text!] any [
            select meta 'return-note
            copy opt select opt fields 'return-note
        ]
        parameter-types: ensure [~null~ frame!] any [
            select meta 'parameter-types
            inherit-frame opt get opt 'parameter-types
        ]
        parameter-notes: ensure [~null~ frame!] any [
            select meta 'parameter-notes
            inherit-frame opt get opt 'parameter-notes
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
    notes: null

    ; For efficiency, objects are only created on demand by hitting the
    ; required point in the PARSE.  Hence `redescribe [] :foo` will not tamper
    ; with the meta information at all, while `redescribe [{stuff}] :foo` will
    ; only manipulate the description.

    on-demand-meta: does [
        meta: default [set-meta :value copy system/standard/action-meta]

        if not find meta 'description [
            panic [{archetype META-OF doesn't have DESCRIPTION slot} meta]
        ]

        if notes: get 'meta/parameter-notes [
            if not frame? notes [
                panic [{PARAMETER-NOTES in META-OF is not a FRAME!} notes]
            ]

          ; Getting error on equality test from expired frame...review
          comment [
            if not equal? :value (action of notes) [
                panic [{PARAMETER-NOTES in META-OF frame mismatch} notes]
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

            meta: null ;-- need to get a parameter-notes field in the OBJECT!
            on-demand-meta ;-- ...so this loses SPECIALIZEE, etc.

            description: meta/description: fields/description
            notes: meta/parameter-notes: fields/parameter-notes
            types: meta/parameter-types: fields/parameter-types
        ]
    ]

    parse spec [
        opt [
            description: text! (
                either all [
                    equal? description {}
                    not meta
                ][
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
        opt some [
            param: [word! | get-word! | lit-word! | refinement! | set-word!]

            ; It's legal for the redescribe to name a parameter just to
            ; show it's there for descriptive purposes without adding notes.
            ; But if {} is given as the notes, that's seen as a request
            ; to delete a note.
            ;
            opt [note: text! (
                on-demand-meta
                either equal? param (the return:) [
                    meta/return-note: all [
                        not equal? note {}
                        copy note
                    ]
                ][
                    if notes or (not equal? note {}) [
                        on-demand-notes

                        if not find notes as word! param [
                            panic [param "not found in frame to describe"]
                        ]

                        actual: first find words of :value param
                        if not equal? param actual [
                            panic [param {doesn't match word type of} actual]
                        ]

                        notes/(as word! param): if not equal? note {} [note]
                    ]
                ]
            )]
        ]
    ] else [
        panic [{REDESCRIBE specs should be STRING! and ANY-WORD! only:} spec]
    ]

    ; If you kill all the notes then they will be cleaned up.  The meta
    ; object will be left behind, however.
    ;
    if notes and (every [param note] notes [(unset? 'note) or (null? note)]) [
        meta/parameter-notes: null
    ]

    return :value ;-- should have updated the meta
]


redescribe [
    {Create an ACTION, implicity gathering SET-WORD!s as <local> by default}
] :function


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


default*: infix redescribe [
    {Would be the same as DEFAULT/ONLY if paths could dispatch infix}
](
    specialize 'default [only: okay]
)


skip*: redescribe [
    {Variant of SKIP that returns NULL instead of clipping to series bounds}
](
    specialize 'skip [only: okay]
)

ensure: redescribe [
    {Pass through value if it matches test, otherwise trigger a PANIC}
](
    specialize 'either-test [
        branch: func [arg [any-value!]] [
            ;
            ; !!! Can't use PANIC/BLAME until there is a good way to SPECIALIZE
            ; a conditional with a branch referring to invocation parameters:
            ;
            ; https://github.com/metaeducation/ren-c/issues/587
            ;
            panic [
                "ENSURE failed with argument of type" any [
                    type of :arg
                    "null"
                ]
            ]
        ]
    ]
)

really: func [
    {PANIC if value is null, otherwise pass it through}

    return: [any-value!]
    value [any-value!]
][
    if null? :value [panic "REALLY requires non-NULL"]
    return :value
]

oneshot: specialize 'n-shot [n: 1]
upshot: specialize 'n-shot [n: -1]

attempt: ~<Use SYS.UTIL/RESCUE instead of ATTEMPT>~

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
        f/word: to issue! word ;-- do not create new virtual binding
        saved: f/series: get word

        ; !!! https://github.com/rebol/rebol-issues/issues/2331
        comment [
            sys/util/rescue [set the result: eval f] then arrow e [
                set word saved
                panic e
            ]
            set word saved
            :result
        ]

        result: meta eval f
        set word saved
        return unmeta result
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
    specialize 'for [
        start: 1
        bump: 1
    ]
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
    specialize 'lock [clone: okay]
)


arity-of: function [
    "Get the number of fixed parameters (not refinements or refinement args)"
    return: [integer!]
    value [any-word! any-path! action!]
][
    if path? :value [panic "arity-of for paths is not yet implemented."]

    if not action? :value [
        value: get value
        if not action? :value [return 0]
    ]

    if variadic? :value [
        panic "arity-of cannot give reliable answer for variadic actions"
    ]

    ; !!! Should willingness to take endability cause a similar error?
    ; Arguably the answer tells you an arity that at least it *will* accept,
    ; so it's not completely false.

    arity: 0
    for-each param reflect :value 'words [
        if refinement? :param [
            return arity
        ]
        arity: arity + 1
    ]
    return arity
]


arrow: lambda [
    {Convenience variadic wrapper for LAMBDA (infix'd as ->)}

    :args [<end> word! block!]
        {Block of argument words, or a single word (if only one argument)}
    :body [any-element! <...>]
        {Block that serves as the body or variadic elements for the body}
][
    lambda (either :args [to block! args] [[]]) (
        if block? first body [
            take body
        ] else [
            make block! body
        ]
    )
]

find-reverse: specialize :find [
    reverse: okay

    ; !!! Specialize out /SKIP because it was not compatible--R3-Alpha
    ; and Red both say `find/skip tail "abcd" "bc" -1` is none.
    ;
    skip: null
]

find-last: specialize :find [
    ;
    ; New builds do FIND-LAST in terms of /SKIP of -1 and starting from the
    ; tail.  But in this bootstrap build (find/reverse tail "abcd" "bc") is
    ; null, and fixing that code is not worth it.  So we define FIND-LAST
    ; as simply still using the /LAST refinement.
    ;
    last: okay
]

reify: func [
    return: [any-element!]
    value [~null~ ~void~ trash! any-value!]
][
    case [
        void? :value [return '~void~]
        trash? :value [return '~]
        null? :value [return '~null~]
        okay? :value [return '~okay~]
    ]
    return :value
]

degrade: func [
    return: [~void~ any-value!]
    value [any-value!]
][
    case [
        '~void~ = :value [return void]
        '~ = :value [return ~]
        '~null~ = :value [return null]
        '~okay~ = :value [return okay]
    ]
    return :value
]

method: infix func [
    {FUNCTION variant that creates an ACTION! implicitly bound in a context}

    return: [action!]
    :member [set-word! set-path!]
    spec [block!]
    body [block!]
    <local> context
][
    context: binding of member else [
        panic [member "must be bound to an ANY-CONTEXT! to use METHOD"]
    ]
    return set member bind (function compose [(spec) <in> (context)] body) context
]

meth: infix func [
    {FUNC variant that creates an ACTION! implicitly bound in a context}

    return: [action!]
    :member [set-word! set-path!]
    spec [block!]
    body [block!]
    <local> context
][
    context: binding of member else [
        panic [target "must be bound to an ANY-CONTEXT! to use METH"]
    ]

    ; !!! This is somewhat inefficient because <in> is currently implemented
    ; in usermode...so the body will get copied twice.  The contention is
    ; between not wanting to alter the bindings in the caller's body variable
    ; and wanting to update them for the purposes of the FUNC.  Review.
    ;
    return set member bind (func spec bind copy/deep body context) context
]


module: func [
    {Creates a new module.}

    return: [module!]
    spec "The header block of the module (modified)"
        [block! object!]
    body "The body block of the module (modified)"
        [block!]
    /into "Add data to existing MODULE! context (vs making a new one)"
    mod [module!]

    <local> hidden w
][
    ; !!! Is it a good idea to mess with the given spec and body bindings?
    ; This was done by MODULE but not seemingly automatically by MAKE MODULE!
    ;
    unbind/deep body

    ; Convert header block to standard header object:
    ;
    if block? :spec [
        unbind/deep spec
        sys/util/rescue [
            spec: construct/with/only spec system/standard/header
        ] then [
            spec: null
        ]
    ]

    ; Historically, the Name: and Type: fields would tolerate either LIT-WORD!
    ; or WORD! equally well.  This is because it used R3-Alpha's CONSTRUCT,
    ; (which was non-evaluative by default, unlike Ren-C's construct) but
    ; without the /ONLY switch.  In that mode, it decayed LIT-WORD! to WORD!.
    ; To try and standardize the variance, Ren-C does not accept LIT-WORD!
    ; in these slots.
    ;
    if lit-word? spec/name [
        spec/name: as word! spec/name
        panic ["Ren-C module name:" (spec/name) "must be WORD!, not LIT-WORD!"]
    ]
    if lit-word? spec/type [
        spec/type: as word! spec/type
        panic ["Ren-C module type:" (spec/type) "must be WORD!, not LIT-WORD!"]
    ]

    ; Validate the important fields of header:
    ;
    ; !!! This should be an informative error instead of asserts!
    ;
    for-each [var types] [
        spec object!
        body block!
        spec/name [~null~ word!]
        spec/type [~null~ word!]
        spec/version [~null~ tuple!]
        spec/options [~null~ block!]
    ][
        eval compose [ensure ((types)) (var)] ;-- names to show if fails
    ]

    spec/options: default [copy []]

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
            append spec reduce ['exports make block! 10]
        ]

        ; Note: 'export overrides 'hidden, silently for now
        parse body [opt some [
            to 'export remove one opt remove 'hidden opt
            [
                w: any-word! (
                    if not find spec/exports w: to word! w [
                        append spec/exports w
                    ]
                )
                w: block! (
                    append spec/exports collect-words/ignore w spec/exports
                )
            ]
        ] to <end>]
    ]

    ; Collect 'hidden keyword words, removing the keywords. Ignore exports.
    hidden: null
    if find body 'hidden [
        hidden: make block! 10
        ; Note: Exports are not hidden, silently for now
        parse body [opt some [
            to 'hidden remove one opt
            [
                w: any-word! (
                    if not find select spec 'exports w: to word! w [
                        append hidden w
                    ]
                )
                w: block! (
                    append hidden collect-words/ignore w select spec 'exports
                )
            ]
        ] to <end>]
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

        resolve mod lib
    ][
        ; Only top level defined words are module variables.
        ;
        bind/only/set body mod

        ; The module shares system exported variables:
        ;
        bind body lib
    ]

    bind body mod ;-- redundant?
    eval body

    ;print ["Module created" spec/name spec/version]
    return mod
]


cause-error: func [
    "Causes an immediate error throw with the provided information."
    return: []
    err-type [word!]
    err-id [word!]
    args
][
    ; Make sure it's a block:
    args: compose [(:args)]

    ; Filter out functional values:
    iterate args [
        if action? first args [
            change/only args meta-of first args
        ]
    ]

    panic make error! [
        type: err-type
        id: err-id
        arg1: first args
        arg2: second args
        arg3: third args
    ]
]

panic: function [
    {Interrupts execution by reporting an error (a TRAP can intercept it).}

    return: []
    reason "ERROR! value, message text, or failure spec"
        [<end> error! text! block!]
    /blame "Specify an originating location other than the PANIC itself"
    location "Frame or parameter at which to indicate the error originated"
        [frame! any-word!]
][
    ; Ultimately we might like PANIC to use some clever error-creating dialect
    ; when passed a block, maybe something like:
    ;
    ;     panic [<invalid-key> {The key} key-name: key {is invalid}]
    ;
    ; That could provide an error ID, the format message, and the values to
    ; plug into the slots to make the message...which could be extracted from
    ; the error if captured (e.g. error/id and `error/key-name`).  Another
    ; option would be something like:
    ;
    ;     panic/with [{The key} :key-name {is invalid}] [key-name: key]
    ;
    error: switch type of :reason [
        error! [reason]

        null [make error! "(no message)"]
        text! [make error! reason]
        block! [make error! spaced reason]
    ]

    if (not error? :reason) or (not pick reason 'where) [
        ;
        ; If no specific location specified, and error doesn't already have a
        ; location, make it appear to originate from the frame calling PANIC.
        ;
        location: default [binding of 'reason]

        ; !!! Does SET-LOCATION-OF-ERROR need to be a native?
        ;
        set-location-of-error error location
    ]

    ; Raise error to the nearest TRAP up the stack (if any)
    ;
    eval ensure error! error
]
