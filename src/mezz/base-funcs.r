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

    return: <elide>
    conditions [block!]
        {Block of conditions to evaluate and test for logical truth}
][
    ; ASSERT has no default implementation, but can be HIJACKed by a debug
    ; mode with a custom validation or output routine.
    ;
    ; !!! R3-Alpha and Rebol2 did not distinguish ASSERT and VERIFY, since
    ; there was no idea of a "debug mode"
]


maybe: enfixed func* [
    "Set word or path to a default value if that value is a value"

    return: [<opt> any-value!]
    'target [set-word! set-path!]
        "The word to which might be set"
    optional [<opt> any-value!]
        "Value to assign only if it is not null"
][
    if semiquoted? 'optional [
        ;
        ; While DEFAULT requires a BLOCK!, MAYBE does not.  Catch mistakes
        ; such as `x: maybe [...]`
        ;
        fail @optional [
            "Literal" type of :optional "used w/MAYBE, use () if intentional"
        ]
    ]

    ; SET and GET require you to pre-compose paths if they have GROUP! in
    ; them (then use /HARD).  Note also that right evaluates before left here:
    ;
    ; https://github.com/rebol/rebol-issues/issues/2275
    ;
    if null? :optional [return get/hard/any compose target]
    set/hard compose target :optional
]


steal: func* [
    {Return a variable's value prior to an assignment, then do the assignment}

    return: [<opt> any-value!]
        {Value of the following SET-WORD! or SET-PATH! before assignment}
    evaluation [<opt> any-value! <variadic>]
        {Used to take the assigned value}
    'look [set-word! set-path! <variadic>]
][
    get first look  ; returned value

    elide take evaluation
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
        new-spec var other
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

    new-body: _
    statics: _
    defaulters: _
    var: <dummy>  ; enter PARSE with truthy state (gets overwritten)
    with-return: _

    parse spec [any [
        <void> (append new-spec <void>)
    |
        <elide> (append new-spec <elide>)
    |
        :(either var '[
            set var: [any-word! | any-path! | quoted!] (
                append new-spec var  ; need quote level as-is in new spec

                var: dequote var
                case [
                    match [get-path! path!] var [
                        if (length of var != 2) or (_ <> first var) [
                            fail ["Bad path in spec:" var]
                        ]
                        append exclusions var/2  ; exclude args/refines
                    ]

                    any-word? var [
                        append exclusions var  ; exclude args/refines
                    ]

                    true [  ; QUOTED! could have been anything
                        fail ["Bad spec item" var]
                    ]
                ]
            )
            |
            set other: block! (
                append/only new-spec other  ; data type blocks
            )
            |
            copy other some text! (
                append/only new-spec spaced other  ; spec notes
            )
        ] '[
            set var: tuple! (  ; locals legal anywhere
                append exclusions var
                append new-spec var
                var: _
            )
        ])
    |
        other: here
        group! (
            if not var [
                fail [
                    ; <where> spec
                    ; <near> other
                    "Default value not paired with argument:" (mold other/1)
                ]
            ]
            defaulters: default [copy []]
            append defaulters compose [
                (var): default '(do other/1)
            ]
        )
    |
        (var: _)  ; everything below this line resets var
        false  ; failing here means rolling over to next rule
    |
        <local>
        any [set var: word! set other: opt group! (
            append new-spec to tuple! var
            append exclusions var
            if other [
                defaulters: default [copy []]
                append defaulters compose [  ; always sets
                    (var): '(do other)
                ]
            ]
        )]
        (var: _)  ; don't consider further GROUP!s or variables
    |
        <in> (
            new-body: default [
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
            set other: [word! | path!] (
                append exclusions other

                ; Definitional returns need to be signaled even if FUNC, so
                ; the FUNC* doesn't automatically generate one.
                ;
                if other = 'return [with-return: [<with> return]]
            )
        |
            text!  ; skip over as commentary
        ]
    |
        <static> (
            statics: default [copy []]
            new-body: default [
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

    ; Gather the SET-WORD!s in the body, excluding the collected ANY-WORD!s
    ; that should not be considered.  Note that COLLECT is not defined by
    ; this point in the bootstrap.
    ;
    locals: try if gather [collect-words/deep/set/ignore body exclusions]

    if statics [
        statics: make object! statics
        bind new-body statics
    ]

    ; !!! The words that come back from COLLECT-WORDS are all WORD!, but we
    ; need blank-headed-TUPLE! for pure locals to the generators.  Review the
    ; COLLECT-WORDS interface to efficiently give this result.
    ;
    for-each loc locals [
        append new-spec to tuple! loc
    ]

    append new-spec opt with-return  ; if FUNC* suppresses return generation

    ; The constness of the body parameter influences whether FUNC* will allow
    ; mutations of the created function body or not.  It's disallowed by
    ; default, but TWEAK can be used to create variations e.g. a compatible
    ; implementation with Rebol2's FUNC.
    ;
    if const? body [new-body: const new-body]

    func* new-spec either defaulters [
        append/only defaulters as group! any [new-body body]
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
function: :func/gather


what-dir: func [  ; This can be HIJACK'd by a "smarter" version
    {Returns the current directory path}
    return: [<opt> file! url!]
][
    return opt system/options/current-path
]

change-dir: func [  ; This can be HIJACK'd by a "smarter" version
    {Changes the current path (where scripts with relative paths will be run).}
    return: [file! url!]
    path [file! url!]
][
    system/options/current-path: path
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
        meta: default [set-meta :value copy system/standard/action-meta]

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

            description: meta/description: fields/description
            notes: meta/parameter-notes: fields/parameter-notes
            types: meta/parameter-types: fields/parameter-types
        ]
    ]

    let param: _
    let note: _
    parse spec [
        opt [
            copy description any text! (
                description: spaced description
                all [
                    description = {}
                    not meta
                ] then [
                    ; No action needed (no meta to delete old description in)
                ] else [
                    on-demand-meta
                    meta/description: if description = {} [
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
                if param = 'return: [
                    meta/return-note: all [
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

                        notes/(as word! param): if note <> {} [note]
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
    all [
        notes
        every [param note] notes [null? :note]
    ] then [
        meta/parameter-notes: _
    ]

    return :value  ; should have updated the meta
]


redescribe [
    {Create an ACTION, implicity gathering SET-WORD!s as <local> by default}
] :function

unset: redescribe [
    {Clear the value of a word to null (in its current context.)}
](
    specialize :set [value: null]
)


; >- is the SHOVE operator.  It uses the item immediately to its left for
; the first argument to whatever operation is on its right hand side.
; Parameter conventions of that first argument apply when processing the
; value, e.g. quoted arguments will act quoted.
;
; By default, the evaluation rules proceed according to the enfix mode of
; the operation being shoved into:
;
;    >> 10 >- lib/= 5 + 5  ; as if you wrote `10 = 5 + 5`
;    ** Script Error: + does not allow logic! for its value1 argument
;
;    >> 10 >- equal? 5 + 5  ; as if you wrote `equal? 10 5 + 5`
;    == #[true]
;
; You can force processing to be enfix using `->-` (an infix-looking "icon"):
;
;    >> 1 ->- lib/add 2 * 3  ; as if you wrote `1 + 2 * 3`
;    == 9
;
; Or force prefix processing using `>--` (multi-arg prefix "icon"):
;
;    >> 10 >-- lib/+ 2 * 3  ; as if you wrote `add 1 2 * 3`
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
    if tail? feed [return]
    feed: take feed
    all [block? :feed, semiquoted? 'feed] then [
        fail "Don't use literal block as SO right hand side, use ([...])"
    ]
    return :feed
]
tweak :so 'postpone on


matched: enfixed redescribe [
    "Assert that the left hand side--when fully evaluated--MATCHES the right"
](
    enclose :matches func [f [frame!]] [
        let test: :f/test  ; note DO F makes F/XXX unavailable
        let value: :f/value  ; returned value

        if not do f [
            fail @f make error! [
                type: 'Script
                id: 'assertion-failure
                arg1: compose [(:value) matches (:test)]
            ]
        ]
        return :value
    ]
)
tweak :matched 'postpone on

; Rare case where a `?` variant is useful, to avoid VOID! on falsey matches
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
        return :left  ; choose left in case binding or case matters somehow
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
    specialize :either-match [
        branch: func [arg [<opt> any-value!]] [
            ;
            ; !!! Can't use FAIL/WHERE until there is a good way to SPECIALIZE
            ; a conditional with a branch referring to invocation parameters:
            ;
            ; https://github.com/metaeducation/ren-c/issues/587
            ;
            fail [
                "ENSURE failed with argument of type"
                    type of get* 'arg else ["NULL"]
            ]
        ]
    ]
)

non: redescribe [
    {Pass through value if it *doesn't* match test, otherwise trigger a FAIL}
](
    specialize :either-match [
        not: #
        branch: func [arg [<opt> any-value!]] [
            ;
            ; !!! Can't use FAIL/WHERE until there is a good way to SPECIALIZE
            ; a conditional with a branch referring to invocation parameters:
            ;
            ; https://github.com/metaeducation/ren-c/issues/587
            ;
            fail [
                "NON failed with argument of type"
                    (type of get* 'arg) else ["NULL"]
            ]
        ]
    ]
)

really: func [
    {FAIL if value is null, otherwise pass it through}

    return: [any-value!]
    value [any-value!]  ; always checked for null, since no <opt>
][
    ; While DEFAULT requires a BLOCK!, REALLY does not.  Catch mistakes such
    ; as `x: really [...]`
    ;
    if semiquoted? 'value [
        fail @value [
            "Literal" type of :value "used w/REALLY, use () if intentional"
        ]
    ]

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
        return @(do code else [null])  ; want NULL-2 if was NULL
    ]
    return null
]

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
        if blank? let word: f/word [return null]
        f/word: quote to word! word  ; do not create new virtual binding
        let saved: f/series: get word

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


count-up: redescribe [
    "Loop the body, setting a word from 1 up to the end value given"
](
    specialize :for [start: 1, bump: 1]
)

count-down: redescribe [
    "Loop the body, setting a word from the end value given down to 1"
](
    specialize adapt :for [
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
    chain [:copy/deep | :freeze]
)

eval-all: func [
    {Evaluate any number of expressions and discard them}

    return: <elide>
    expressions [<opt> any-value! <variadic>]
        {Any number of expressions on the right.}
][
    do expressions
]


; These constructs used to be enfix to complete their left hand side.  Yet
; that form of completion was only one expression's worth, when they wanted
; to allow longer runs of evaluation.  "Invisible functions" (those which
; `return: <elide>`) permit a more flexible version of the mechanic.

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
    set member bind (
        func/(if gather '/gather) compose [((spec)) <in> (context)] body
    ) context
]


module: func [
    {Creates a new module}

    spec "The header block of the module (modified)"
        [block! object!]
    body "The body block of the module (modified)"
        [block!]
    /mixin "Mix in words collected into an object from other modules"
        [object!]
    /into "Add data to existing MODULE! context (vs making a new one)"
        [module!]
][
    ; !!! Is it a good idea to mess with the given spec and body bindings?
    ; This was done by MODULE but not seemingly automatically by MAKE MODULE!
    ;
    unbind/deep body

    ; Convert header block to standard header object:
    ;
    if block? spec [
        unbind/deep spec
        spec: try attempt [construct/with/only spec system/standard/header]
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
        spec/name: as word! dequote spec/name
        ;fail ["Ren-C module Name:" (spec/name) "must be WORD!, not LIT-WORD!"]
    ]
    if lit-word? spec/type [
        spec/type: as word! dequote spec/type
        ;fail ["Ren-C module Type:" (spec/type) "must be WORD!, not LIT-WORD!"]
    ]

    ; Validate the important fields of header:
    ;
    ; !!! This should be an informative error instead of asserts!
    ;
    for-each [var types] [
        spec object!
        body block!
        mixin [<opt> object!]
        spec/name [word! blank!]
        spec/type [word! blank!]
        spec/version [tuple! blank!]
        spec/options [block! blank!]
    ][
        do compose [ensure (types) (var)]  ; names to show if fails
    ]

    ; In Ren-C, MAKE MODULE! acts just like MAKE OBJECT! due to the generic
    ; facility for SET-META.

    into: default [
        make module! 7 ; arbitrary starting size
    ]
    let mod: into

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
        let w
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
    let hidden: _
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

    if find spec/options 'isolate [
        ;
        ; All words of the module body are module variables:
        ;
        bind/new body mod

        ; The module keeps its own variables (not shared with system):
        ;
        if object? mixin [resolve mod mixin]

        resolve mod lib
    ]
    else [
        ; Only top level defined words are module variables.
        ;
        bind/only/set body mod

        ; The module shares system exported variables:
        ;
        bind body lib

        if object? mixin [bind body mixin]
    ]

    bind body mod  ; !!! "Redundant?" (said the comment...)
    do body

    return mod
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
; it does not continue running?  `return: []` has been freed up as it no
; longer means invisible...
;
; Though HIJACK would have to be aware of it and preserve the rule.
;
fail: func [
    {Interrupts execution by reporting an error (a TRAP can intercept it).}

    'blame "Point to variable or parameter to blame"
        [<skip> sym-word! sym-path!]
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
    ; the error if captured (e.g. error/id and `error/key-name`.  Another
    ; option would be something like:
    ;
    ;     fail/with [{The key} :key-name {is invalid}] [key-name: key]

    ; !!! PATH! doesn't do BINDING OF, and in the general case it couldn't
    ; tell you where it resolved to without evaluating, just do WORD! for now.
    ;
    let frame: try match frame! binding of try match sym-word! :blame

    let error: switch type of :reason [
        error! [reason]
        text! [make error! reason]
        word! [
            make error! [
                Type: 'User
                id: reason
                message: to text! reason
            ]
        ]
        path! [
            if word? last reason [
                make error! [
                    Type: 'User
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
                Type: 'Script
                id: 'unknown-error
            ])
        ]
    ] else [
        null? reason so make error! compose [
            Type: 'Script
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

unreachable: specialize :fail [reason: "Unreachable code"]


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
    if blank? src [src: system/ports/input]
    if file? src [src: open src]

    let pos
    let rule: compose/deep/only either delimiter [
        either keep
        [ [thru (delimiter) pos:] ]
        [ [to (delimiter) remove (delimiter) pos:] ]
    ][
        [
            to crlf any [
                ["^M" and not "^/"]
                to crlf
            ] (if not keep ['remove]) ["^/" | "^M^/"] pos:
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
            let pos: _
            parse buffer (rule)
            if pos [break]
            ((if same? src system/ports/input
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
    {Makes a generator that yields lines from system/ports/input.}
](
    specialize :read-lines [src: _]
)
