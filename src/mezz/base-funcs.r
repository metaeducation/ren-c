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

assert: proc [
    {Ensure conditions are conditionally true if hooked by debugging}

    conditions [block!]
        {Block of conditions to evaluate and test for logical truth}
][
    ; ASSERT has no default implementation, but can be HIJACKed by a debug
    ; mode with a custom validation or output routine.
    ;
    ; !!! R3-Alpha and Rebol2 did not distinguish ASSERT and VERIFY, since
    ; there was no idea of a "debug mode"
]


default: enfix func [
    {Set word or path to a default value if it is not set yet or blank.}

    return: [any-value!]
    'target "The word or path which might be set"
        [set-word! set-path!]
    branch "If target is not set already, this is evaluated and stored there"
        [block! action!]
    /only "Consider target being BLANK! to be a value not to overwrite"
    <local> gotten
][
    either all [
        value? gotten: get target
        any [only | not blank? :gotten]
    ][
        :gotten ;; so that `x: y: default z` leads to `x = y`
    ][
        set target do branch
    ]
]

maybe: enfix func [
    "Set word or path to a default value if that value is set and not blank."

    return: [<opt> any-value!]
    'target [set-word! set-path!]
        "The word to which might be set"
    value [<opt> any-value!]
        "Value to assign only if it is nothing not nothing"
    /only
        "Consider value being BLANK! to be 'something' to use for overwriting"

    <local> gotten
][
    ; While DEFAULT requires a BLOCK!, MAYBE does not.  Catch mistakes such
    ; as `x: maybe [...]`
    ;
    if semiquoted? 'value [
        fail/where [
            "Literal" type of :value "used w/MAYBE, use () if intentional"
        ] 'value
    ]

    set target either-test/opt
        (only ?? :value? !! :something?) ;-- test function
        :value ;-- value being tested
        [get target] ;-- branch to evaluate and return if test fails
]


was: func [
    {Return a variable's value prior to an assignment, then do the assignment}

    return: [<opt> any-value!]
        {Value of the following SET-WORD! or SET-PATH! before assignment}
    evaluation [<opt> any-value! <...>]
        {Used to take the assigned value}
    :look [set-word! set-path! <...>]
][
    get first look ;-- returned value

    elide take evaluation
]


make-action: func [
    {Internal generator used by FUNCTION and PROCEDURE specializations.}
    return: [action!]
    generator [action!]
        {Arity-2 "lower"-level function generator to use (e.g. FUNC or PROC)}
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
    var: _

    ;; dump [spec]

    ; Gather the SET-WORD!s in the body, excluding the collected ANY-WORD!s
    ; that should not be considered.  Note that COLLECT is not defined by
    ; this point in the bootstrap.
    ;
    ; !!! REVIEW: ignore self too if binding object?
    ;
    parse spec [any [
        if (set? 'var) [
            set var: any-word! (
                append exclusions var ;-- exclude args/refines
                append new-spec var
            )
        |
            set other: [block! | text!] (
                append/only new-spec other ;-- spec notes or data type blocks
            )
        ]
    |
        other:
        [group!] (
            if not var [
                fail [
                    ; <where> spec
                    ; <near> other
                    "Default value not paired with argument:" (mold other/1)
                ]
            ]
            defaulters: default [copy []]
            append defaulters compose/deep [
                (to set-word! var) default [(uneval do other/1)]
            ]
        )
    |
        (var: null) ;-- everything below this line clears var
        fail ;-- failing here means rolling over to next rule
    |
        <local>
        any [set var: word! (other: _) opt set other: group! (
            append new-spec to set-word! var
            append exclusions var
            if other [
                defaulters: default [copy []]
                append defaulters compose/deep [ ;-- always sets
                    (to set-word! var) (uneval do other)
                ]
            ]
        )]
        (var: null) ;-- don't consider further GROUP!s or variables
    |
        <in> (
            new-body: default [
                append exclusions 'self
                copy/deep body
            ]
        )
        any [
            set other: [word! | path!] (
                other: ensure any-context! get other
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
            set var: word! (other: quote ()) opt set other: group! (
                append exclusions var
                append statics compose/only [
                    (to set-word! var) (other)
                ]
            )
        ]
        (var: null)
    |
        end accept
    |
        other: (
            fail [
                ; <where> spec
                ; <near> other
                "Invalid spec item:" (mold other/1)
            ]
        )
    ]]

    locals: collect-words/deep/set/ignore body exclusions

    ;; dump [{before} statics new-spec exclusions]

    if statics [
        statics: has statics
        bind new-body statics
    ]

    ; !!! The words that come back from COLLECT-WORDS are all WORD!, but we
    ; need SET-WORD! to specify pure locals to the generators.  Review the
    ; COLLECT-WORDS interface to efficiently give this result, as well as
    ; a possible COLLECT-WORDS/INTO
    ;
    for-skip locals 1 [ ;-- FOR-NEXT not specialized yet
        append new-spec to set-word! locals/1
    ]

    ;; dump [{after} new-spec defaulters]

    generator new-spec either defaulters [
        append/only defaulters as group! any [new-body body]
    ][
        any [new-body body]
    ]
]

;-- These are "redescribed" after REDESCRIBE is created
;
function: specialize :make-action [generator: :func]
procedure: specialize :make-action [generator: :proc]


; Actions can be chained, adapted, and specialized--repeatedly.  The meta
; information from which HELP is determined can be inherited through links
; in that meta information.  Though in order to mutate the information for
; the purposes of distinguishing a derived action, it must be copied.
;
dig-action-meta-fields: function [value [action!]] [
    meta: meta-of :value or [
        return construct system/standard/action-meta [
            description: _
            return-type: _
            return-note: _
            parameter-types: make frame! :value
            parameter-notes: make frame! :value
        ]
    ]

    underlying: ensure [action! blank!] any [
        try get 'meta/specializee
        try get 'meta/adaptee
        all [block? :meta/chainees | try first meta/chainees]
    ]

    fields: all [:underlying | dig-action-meta-fields :underlying]

    inherit-frame: function [parent [blank! frame!]] [
        if blank? parent [return blank]

        child: make frame! :value
        for-each param child [
            ;
            ; !!! GROUP! cannot be used with MAYBE because it left quotes and
            ; might-or-might-not run the GROUP!.  Review.
            ;
            child/:param: maybe select parent param
        ]
        return child
    ]

    return construct system/standard/action-meta [
        description: ensure* text! opt any [
            try select meta 'description
            all [fields | copy fields/description]
        ]
        return-type: (
            ;
            ; !!! The optimized native signals the difference between
            ; "undocumented argument" and "no argument at all" with the
            ; void vs BLANK! distinction.  This routine needs an overhaul and
            ; wasn't really written to anticipate the subtlety.  But be
            ; sensitive to it here.
            ;
            temp: select meta 'return-type
            if all [unset? 'temp | fields | did select fields 'return-type] [
                temp: copy fields/return-type
            ]
            :temp
        )
        return-note: ensure* text! opt any [
            try select meta 'return-note
            all [try get 'fields/return-note | copy fields/return-note]
        ]
        parameter-types: ensure* frame! opt any [
            try select meta 'parameter-types
            all [
                try get 'fields/parameter-types
                inherit-frame :fields/parameter-types
            ]
        ]
        parameter-notes: ensure* frame! opt any [
            try select meta 'parameter-notes
            all [
                try get 'fields/parameter-notes
                inherit-frame :fields/parameter-notes
            ]
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

        if notes: try get 'meta/parameter-notes [
            if not frame? notes [
                fail [{PARAMETER-NOTES in META-OF is not a FRAME!} notes]
            ]

            if :value <> action-of notes [
                fail [{PARAMETER-NOTES in META-OF frame mismatch} notes]
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
    on-demand-notes: does catch [
        on-demand-meta

        if find meta 'parameter-notes [throw ()]

        fields: dig-action-meta-fields :value

        meta: _ ;-- need to get a parameter-notes field in the OBJECT!
        on-demand-meta ;-- ...so this loses SPECIALIZEE, etc.

        description: meta/description: fields/description
        notes: meta/parameter-notes: fields/parameter-notes
        types: meta/parameter-types: fields/parameter-types
    ]

    if not parse spec [
        opt [
            set description: text! (
                either all [equal? description {} | not meta] [
                    ; No action needed (no meta to delete old description in)
                ][
                    on-demand-meta
                    meta/description: if not equal? description {} [
                        description
                    ]
                ]
            )
        ]
        any [
            set param: [word! | get-word! | lit-word! | refinement! | set-word!]

            ; It's legal for the redescribe to name a parameter just to
            ; show it's there for descriptive purposes without adding notes.
            ; But if {} is given as the notes, that's seen as a request
            ; to delete a note.
            ;
            opt [[set note: text!] (
                on-demand-meta
                either equal? param (quote return:) [
                    meta/return-note: all [
                        not equal? note {}
                        copy note
                    ]
                ][
                    if any [notes | not equal? note {}] [
                        on-demand-notes

                        if not find notes to word! param [
                            fail [param "not found in frame to describe"]
                        ]

                        actual: first find words of :value param
                        if not strict-equal? param actual [
                            fail [param {doesn't match word type of} actual]
                        ]

                        notes/(to word! param): if not equal? note {} [note]
                    ]
                ]
            )]
        ]
    ][ ;-- note: OR not defined yet!
        fail [{REDESCRIBE specs should be STRING! and ANY-WORD! only:} spec]
    ]

    ; If you kill all the notes then they will be cleaned up.  The meta
    ; object will be left behind, however.
    ;
    if all [notes | every [param note] notes [unset? 'note]] [
        meta/parameter-notes: ()
    ]

    :value ;-- should have updated the meta
]


redescribe [
    {Define an action with set-words as locals, that returns a value.}
] :function

redescribe [
    {Define an action with set-words as locals, that doesn't return a value.}
] :procedure

redescribe [
    {Evaluates only the GROUP!s in an array of expressions.}
] :compose


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

maybe*: enfix redescribe [
    {Would be the same as MAYBE/ONLY if paths could dispatch infix}
](
    specialize 'maybe [only: true]
)


; Though this name is questionable, it's nice to be easier to call
;
semiquote: specialize 'identity [quote: true]


if*: redescribe [
    {Same as IF/OPT (null, not blank, if branch evaluates to null)}
](
    specialize 'if [opt: true]
)

if-not*: redescribe [
    {Same as IF-NOT/OPT (null, not blank, if branch evaluates to null)}
](
    specialize 'if-not [opt: true]
)

either*: redescribe [
    {Same as EITHER/OPT (null, not blank, if branch evaluates to null)}
](
    specialize 'either [opt: true]
)


unless: enfix func [
    {Returns the left hand side, unless the right hand side is something}

    return: [any-value!]
    left [any-value!]
    right [<opt> any-value!]
    /try {Consider right hand side being BLANK! a value to override the left}
][
    either-test (try ?? :value? !! :something?) :right [:left]
]

unless*: enfix redescribe [
    {Same as UNLESS/TRY (right hand side being BLANK! overrides the left)}
](
    specialize 'unless [try: true]
)


case*: redescribe [
    {Same as CASE/ONLY (void, not blank, if branch evaluates to void)}
](
    specialize 'case [only: true]
)

switch*: redescribe [
    {Same as SWITCH/ONLY (void, not blank, if branch evaluates to void)}
](
    specialize 'switch [opt: true]
)

skip*: redescribe [
    {Variant of SKIP that returns BLANK! instead of clipping to series bounds}
](
    specialize 'skip [only: true]
)

any*: redescribe [
    {Variant of ANY that ignores voids, and returns void if all opt out}
](
    specialize 'any [only: true]
)

all*: redescribe [
    {Variant of ALL that ignores voids, and returns void if all opt out}
](
    specialize 'all [only: true]
)

ensure: redescribe [
    {Pass through value if it matches test, otherwise trigger a FAIL}
](
    specialize 'either-test [
        branch: func [arg [<opt> any-value!]] [
            ;
            ; !!! Can't use FAIL/WHERE until there is a good way to SPECIALIZE
            ; a conditional with a branch referring to invocation parameters:
            ;
            ; https://github.com/metaeducation/ren-c/issues/587
            ;
            if null? :arg [
                fail "ENSURE doesn't allow null arguments, see ENSURE*"
            ] else [
                fail [
                    "ENSURE failed with argument of type" type of :arg
                ]
            ]
        ]
        opt: false ;-- Doesn't matter (it fails) just hide the refinement
    ]
)

ensure*: redescribe [
    {Pass through value if it matches test -or- if it is null}
](
    specialize 'either-test [
        branch: func [
            return: [<opt>]
            arg [<opt> any-value!]
        ][
            ; !!! Can't use FAIL/WHERE until there is a good way to SPECIALIZE
            ; a conditional with a branch referring to invocation parameters:
            ;
            ; https://github.com/metaeducation/ren-c/issues/587
            ;
            if value? :arg [
                fail [
                    "ENSURE* failed with argument of type" type of :arg
                ]
            ]
        ]
        opt: true ;-- Doesn't matter (it fails) just hide the refinement
    ]
)

really: func [
    {FAIL if value is void or blank, otherwise pass it through}

    return: [any-value!]
    value [any-value!] ;-- always checked for void, since no <opt>
    /opt
        {Just make sure value isn't void, pass through BLANK! (see REALLY*)}
][
    ; While DEFAULT requires a BLOCK!, REALLY does not.  Catch mistakes such
    ; as `x: really [...]`
    ;
    if semiquoted? 'value [
        fail/where [
            "Literal" type of :value "used w/REALLY, use () if intentional"
        ] 'value
    ]

    opt ?? :value else [
        either-test :something? :value [
            fail/where
                ["REALLY received BLANK! (use /OPT or REALLY* if intended)"]
                'value
        ]
    ]
]

really*: redescribe [
    {FAIL if value is void, otherwise pass it through}
](
    specialize 'really [opt: true]
)


take: redescribe [
    {Variant of TAKE* that will give an error if it can't take, vs. void}
](
    chain [
        :take*
            |
        specialize 'either-test-value [
            branch: [
                fail "Can't TAKE from series end (see TAKE* to get void)"
            ]
        ]
    ]
)

attempt: redescribe [
    {Tries to evaluate a block and returns result or NONE on error.}
](
    specialize 'trap [with: true | handler: [_]]
)

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


lock-of: redescribe [
    "If value is already locked, return it...otherwise CLONE it and LOCK it."
](
    specialize 'lock [clone: true]
)


; To help for discoverability, there is SET-INFIX and INFIX?.  However, the
; term can be a misnomer if the function is more advanced, and using the
; "lookback" capabilities in another way.  Hence these return descriptive
; errors when people are "outside the bounds" of assurance RE:infixedness.

arity-of: function [
    "Get the number of fixed parameters (not refinements or refinement args)"
    value [any-word! any-path! action!]
][
    if path? :value [fail "arity-of for paths is not yet implemented."]

    if not action? :value [
        value: get value
        if not action? :value [return 0]
    ]

    if variadic? :value [
        fail "arity-of cannot give reliable answer for variadic actions"
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
    arity
]

nfix?: function [
    n [integer!]
    name [text!]
    source [any-word! any-path!]
][
    case [
        not enfixed? source [false]
        equal? n arity: arity-of source [true]
        n < arity [
            ; If the queried arity is lower than the arity of the function,
            ; assume it's ok...e.g. PREFIX? callers know INFIX? exists (but
            ; we don't assume INFIX? callers know PREFIX?/ENDFIX? exist)
            false
        ]
    ] else [
        fail [
            name "used on enfixed function with arity" arity
                |
            "Use ENFIXED? for generalized (tricky) testing"
        ]
    ]
]

postfix?: redescribe [
    {TRUE if an arity 1 function is SET/ENFIX to act as postfix.}
](
    specialize :nfix? [n: 1 | name: "POSTFIX?"]
)

infix?: redescribe [
    {TRUE if an arity 2 function is SET/ENFIX to act as infix.}
](
    specialize :nfix? [n: 2 | name: "INFIX?"]
)


;-- => cannot be loaded by R3-Alpha, or even earlier Ren-C
;
lambda: function [
    {Convenience variadic wrapper for MAKE ACTION!}

    return: [action!]
    :args [<end> word! path! block!]
        {Block of argument words, or a single word (passed via LIT-WORD!)}
    :body [any-value! <...>]
        {Block that serves as the body or variadic elements for the body}
][
    make action! compose/deep [
        [(:args also [to block! args])]
        [(
            if block? first body [
                take body
            ] else [
                make block! body
            ]
        )]
    ]
]


invisible-eval-all: func [
    {Evaluate any number of expressions, but completely elide the results.}

    return: []
        {Returns nothing, not even void ("invisible function", like COMMENT)}
    expressions [<opt> any-value! <...>]
        {Any number of expressions on the right.}
][
    do expressions
]

right-bar: func [
    {Evaluates to first expression on right, discarding ensuing expressions.}

    return: [<opt> any-value!]
        {Evaluative result of first of the following expressions.}
    expressions [<opt> any-value! <...>]
        {Any number of expression.}
][
    if* not tail? expressions [take* expressions] ;-- return result

    elide do expressions
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
    ] or [
        fail/where [
            "|| expected single expression, found residual of" :look
        ] 'right
    ]
]


; Shorthand helper for CONSTRUCT (similar to DOES for FUNCTION).
;
has: func [
    "Defines an object with just a body...no spec and no parent."
    body [block!] ;-- !!! name checked as `body` vs `vars` by r2r3-future.r
        "Object words and values (bindings modified)"
    /only
        "Values are kept as-is"
][
    construct/(all [only 'only]) [] body
]


module: func [
    {Creates a new module.}

    spec [block! object!]
        "The header block of the module (modified)"
    body [block!]
        "The body block of the module (modified)"
    /mixin
        "Mix in words from other modules"
    mixins [object!]
        "Words collected into an object"

    <local> hidden w mod
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
        spec: attempt [construct/only system/standard/header :spec]
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
        do compose/only [ensure (types) (var)] ;-- names to show if fails
    ]

    ; In Ren-C, MAKE MODULE! acts just like MAKE OBJECT! due to the generic
    ; facility for SET-META.

    mod: make module! 7 ; arbitrary starting size

    if did find spec/options 'extension [
        append mod 'lib-base ; specific runtime values MUST BE FIRST
    ]

    if not spec/type [spec/type: 'module] ; in case not set earlier

    ; Collect 'export keyword exports, removing the keywords
    if find body 'export [
        if not block? select spec 'exports [
            join spec ['exports make block! 10]
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

    either did find spec/options 'isolate [
        ;
        ; All words of the module body are module variables:
        ;
        bind/new body mod

        ; The module keeps its own variables (not shared with system):
        ;
        if object? mixins [resolve mod mixins]

        comment [resolve mod sys] ; no longer done -Carl

        resolve mod lib
    ][
        ; Only top level defined words are module variables.
        ;
        bind/only/set body mod

        ; The module shares system exported variables:
        ;
        bind body lib

        comment [bind body sys] ; no longer done -Carl

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
    ; Make sure it's a block:
    args: compose [(:args)]

    ; Filter out functional values:
    for-next args [
        if action? first args [
            change/only args meta-of first args
        ]
    ]

    fail make error! [
        type: err-type
        id: err-id
        arg1: first args
        arg2: second args
        arg3: third args
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

    reason "ERROR! value, message text, or failure spec"
        [error! text! block!]
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
    ;
    error: switch type of reason [
        error! [reason]
        text! [make error! reason]
        block! [make error! spaced reason]
    ]

    all [
        error? reason
        try pick reason 'where
    ] then [
        ;
        ; If no specific location specified, and error doesn't already have a
        ; location, make it appear to originate from the frame calling FAIL.
        ;
        location: default [context of 'reason]

        ; !!! Does SET-LOCATION-OF-ERROR need to be a native?
        ;
        set-location-of-error error location
    ]

    ; Raise error to the nearest TRAP up the stack (if any)
    ;
    do ensure error! error
]
