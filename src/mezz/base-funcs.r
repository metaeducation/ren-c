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
    {Ensure conditions are TRUE? if hooked by debugging (see also: VERIFY)}

    return: [<opt> logic! block!]
        {Always returns void unless /QUIET is used to return failing blocks}
    conditions [logic! block!]
        {Single logic value or block of conditions to evaluate and test TRUE?}
    /quiet
        {Return failing condition as a BLOCK!, or BLANK! if success}
][
    ; ASSERT has no default implementation, but can be HIJACKed by a debug
    ; build with a custom validation or output routine.  (Currently there is
    ; a default hijacking to set it to be equivalent to VERIFY, late in boot.)
]

set/lookback quote enfix: proc [
    "Convenience version of SET/LOOKBACK, e.g `+: enfix :add`"
    :target [set-word! set-path!]
    action [function!]
][
    set/lookback target :action

    ; return value can't currently be given back as enfix, since it is a
    ; property of words and not values.  Should it be given back at all?
]

default: enfix func [
    "Set word or path to a default value if it is not set yet or blank."

    return: [any-value!]
    :target [set-word! set-path!]
        "The word"
    value [any-value!] ; not <opt> on purpose
        "Value to set (blocks and 0-arity functions evaluated)"
    <local>
        gotten
][
    ; A lookback quoting function that quotes a SET-WORD! on its left is
    ; responsible for setting the value if it wants it to change since the
    ; SET-WORD! is not actually active.  But if something *looks* like an
    ; assignment, it's good practice to evaluate the whole expression to
    ; the result the SET-WORD! was set to, so `x: y: op z` makes `x = y`.
    ;
    if all [any-value? gotten: get/opt target | not blank? :gotten] [
        :gotten
    ]
    else [
        set/opt target if true :value ;-- executed if block or function
    ]
]


does: func [
    {A shortcut to define a function that has no arguments or locals.}
    return: [function!]
    body [block!]
        {The body block of the function}
][
    func [] body ;-- no spec documentation permits any return value
]


make-action: func [
    {Internal generator used by FUNCTION and PROCEDURE specializations.}
    return: [function!]
    generator [function!]
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
    new-spec: make block! length-of spec
    new-body: _
    statics: _
    defaulters: _
    var: _

    ;; dump [spec]

    ; Insert <durable> into the spec.  This is based on the belief that
    ; indefinite duration is a fair user expectation without having to ask.
    ; Consider the legitimacy of:
    ;
    ;    foo: function [x] [y: x * 2 | return func [z] [x + y + z]
    ;
    append new-spec <durable>

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
            set other: [block! | string!] (
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
            unless defaulters [
                defaulters: copy []
            ]
            append defaulters compose/deep [
                (to set-word! var) default [(reduce other/1)]
            ]
        )
    |
        (var: void) ;-- everything below this line clears var
        fail ;-- failing here means rolling over to next rule (<durable>)
    |
        <durable> ;-- don't add to new-spec as we already added it
    |
        <local>
        any [set var: word! (other: _) opt set other: group! (
            append new-spec to set-word! var
            append exclusions var
            if other [
                unless defaulters [
                    defaulters: copy []
                ]
                append defaulters compose/deep [
                    (to set-word! var) default [(reduce other)]
                ]
            ]
        )]
        (var: void) ;-- don't consider further GROUP!s or variables
    |
        <in> (
            unless new-body [
                append exclusions 'self
                new-body: copy/deep body
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
            string! ;-- skip over as commentary
        ]
    |
        ; While <static> is a well-known computer science term, it is an
        ; un-intuitive word.  <has> is Ren-C's preference in mezzanine or
        ; official code, relating it to the HAS object constructor.
        ;
        [<has> | <static>] (
            unless statics [
                statics: copy []
            ]
            unless new-body [
                append exclusions 'self
                new-body: copy/deep body
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
        (var: void)
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


; Functions can be chained, adapted, and specialized--repeatedly.  The meta
; information from which HELP is determined can be inherited through links
; in that meta information.  Though in order to mutate the information for
; the purposes of distinguishing a derived function, it must be copied.
;
dig-function-meta-fields: function [value [function!]] [
    meta: meta-of :value

    unless meta [
        return construct system/standard/function-meta [
            description: _
            return_type: _
            return_note: _
            parameter-types: make frame! :value
            parameter-notes: make frame! :value
        ]
    ]

    underlying: maybe function! any [
        :meta/specializee
        :meta/adaptee
        all [block? :meta/chainees | first meta/chainees]
    ]

    fields: all [:underlying | dig-function-meta-fields :underlying]

    inherit-frame: function [parent [blank! frame!]] [
        if blank? parent [return blank]

        child: make frame! :value
        for-each param child [
            if any-value? select* parent param [
                child/(param): copy parent/(param)
            ]
        ]
        return child
    ]

    return construct system/standard/function-meta [
        description: (
            maybe string! any [
                select meta 'description
                all [fields | copy fields/description]
            ]
        )
        return-type: (
            ;
            ; !!! The optimized native signals the difference between
            ; "undocumented argument" and "no argument at all" with the
            ; void vs BLANK! distinction.  This routine needs an overhaul and
            ; wasn't really written to anticipate the subtlety.  But be
            ; sensitive to it here.
            ;
            temp: select meta 'return-type
            if all [not set? 'temp | fields | select? fields 'return-type] [
                temp: copy fields/return-type
            ]
            :temp
        )
        return-note: (
            maybe string! any [
                select meta 'return-note
                all [fields | copy fields/return-note]
            ]
        )
        parameter-types: (
            maybe frame! any [
                select meta 'parameter-types
                all [fields | inherit-frame :fields/parameter-types]
            ]
        )
        parameter-notes: (
            maybe frame! any [
                select meta 'parameter-notes
                all [fields | inherit-frame :fields/parameter-notes]
            ]
        )
    ]
]

redescribe: function [
    {Mutate function description with new title and/or new argument notes.}

    return: [function!]
        {The input function, with its description now updated.}
    spec [block!]
        {Either a string description, or a spec block (without types).}
    value [function!]
        {(modified) Function whose description is to be updated.}
][
    meta: meta-of :value
    notes: _

    ; For efficiency, objects are only created on demand by hitting the
    ; required point in the PARSE.  Hence `redescribe [] :foo` will not tamper
    ; with the meta information at all, while `redescribe [{stuff}] :foo` will
    ; only manipulate the description.

    on-demand-meta: does [
        case/all [
            not meta [
                meta: copy system/standard/function-meta
                set-meta :value meta
            ]

            not find meta 'description [
                fail [{archetype META-OF doesn't have DESCRIPTION slot} meta]
            ]

            not notes: to-value :meta/parameter-notes [
                return () ; specialized or adapted, HELP uses original notes
            ]

            not frame? notes [
                fail [{PARAMETER-NOTES in META-OF is not a FRAME!} notes]
            ]

            :value != function-of notes [
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
    on-demand-notes: does [
        on-demand-meta

        if find meta 'parameter-notes [return ()]

        fields: dig-function-meta-fields :value

        meta: blank ;-- need to get a parameter-notes field in the OBJECT!
        on-demand-meta ;-- ...so this loses SPECIALIZEE, etc.

        description: meta/description: fields/description
        notes: meta/parameter-notes: fields/parameter-notes
        types: meta/parameter-types: fields/parameter-types
    ]

    unless parse spec [
        opt [
            set description: string! (
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
            opt [[set note: string!] (
                on-demand-meta
                either all [set-word? param | equal? param quote return:] [
                    meta/return-note: either equal? note {} [
                        _
                    ][
                        copy note
                    ]
                ][
                    if (not equal? note {}) or notes [
                        on-demand-notes

                        unless find notes to word! param [
                            fail [param "not found in frame to describe"]
                        ]

                        actual: first find words-of :value param
                        unless strict-equal? param actual [
                            fail [param {doesn't match word type of} actual]
                        ]

                        notes/(to word! param): if not equal? note {} [note]
                    ]
                ]
            )]
        ]
    ][
        fail [{REDESCRIBE specs should be STRING! and ANY-WORD! only:} spec]
    ]

    ; If you kill all the notes then they will be cleaned up.  The meta
    ; object will be left behind, however.
    ;
    if all [notes | every [param note] notes [not set? 'note]] [
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


get*: redescribe [
    {Variation of GET which returns void if the source is not set}
](
    specialize 'get [opt: true]
)

get-value: redescribe [
    {Variation of GET which fails if the value is not set (vs. void or blank)}
](
    chain [
        :get*
            |
        func [x [<opt> any-value!]] [
            unless set? 'x [
                fail "GET-VALUE requires source variable to be set"
            ]
            :x
        ]
    ]
)

set*: redescribe [
    {Variation of SET where voids are tolerated for unsetting variables.}
](
    specialize 'set [opt: true]
)

; LOGIC VERSIONS OF CONTROL STRUCTURES
;
; Control structures evaluate to either void (if no branches taken) or the
; last value of any evaluated blocks.  This applies to everything from IF
; to CASE to WHILE.  The ? versions are tailored to return whether a branch
; was taken at all, and always return either TRUE or FALSE.

if?: redescribe [
    {Variation of IF which returns TRUE if the branch runs, FALSE if not}
](
    chain [:if | :any-value?]
)

if*: redescribe [
    {Same as IF/OPT (return void, not blank, if branch evaluates to void)}
](
    specialize 'if [opt: true]
)

unless?: redescribe [
    {Variation of UNLESS which returns TRUE if the branch runs, FALSE if not}
](
    chain [:unless | :any-value?]
)

unless*: redescribe [
    {Same as UNLESS/OPT (return void, not blank, if branch evaluates to void)}
](
    specialize 'unless [opt: true]
)

either*: redescribe [
    {Same as EITHER/OPT (return void, not blank, if branch evaluates to void)}
](
    specialize 'either [opt: true]
)

while?: redescribe [
    {Variation of WHILE which returns TRUE if the body ever runs, FALSE if not}
](
    chain [:while | :any-value?]
)

case?: redescribe [
    {Variation of CASE which returns TRUE if any cases run, FALSE if not}
](
    chain [:case | :any-value?]
)

case*: redescribe [
    {Same as CASE/OPT (return void, not blank, if branch evaluates to void)}
](
    specialize 'case [opt: true]
)

switch?: redescribe [
    {Variation of SWITCH which returns TRUE if any cases run, FALSE if not}
](
    chain [:switch | :any-value?]
)

switch*: redescribe [
    {Same as SWITCH/OPT (return void, not blank, if branch evaluates to void)}
](
    specialize 'switch [opt: true]
)

trap?: redescribe [
    {Variation of TRAP which returns TRUE if an error traps, FALSE if not}
](
    specialize 'trap [?: true]
)

catch?: redescribe [
    {Variation of CATCH which returns TRUE if a throw is caught, FALSE if not}
](
    specialize 'catch [?: true]
)

any?: redescribe [
    {Shortcut OR, ignores voids. Unlike plain ANY, forces result to LOGIC!}
](
    chain [:any | :to-value | :to-logic]
)

all?: redescribe [
    {Shortcut AND, ignores voids. Unlike plain ALL, forces result to LOGIC!}
](
    chain [:all | :to-value | :to-logic]
)

maybe?: redescribe [
    {Check value using tests (match types, TRUE? or FALSE?, filter function)}
    ; return: [logic!] ;-- blocks for type changes not supported yet
    ;    {TRUE if match, FALSE if no match (use MAYBE to pass through value)}
](
    specialize 'maybe [?: true]
)

find?: redescribe [
    {Variant of FIND that returns TRUE if present and FALSE if not.}
](
    chain [:find | :to-logic]
)

select: redescribe [
    {Variant of SELECT* that returns BLANK when not found, instead of void}
](
    chain [:select* | :to-value]
)

select?: redescribe [
    {Variant of SELECT that returns TRUE if a value was selected, else FALSE.}
](
    chain [:select | :any-value?]
)

pick: redescribe [
    {Variant of PICK* that returns BLANK! when not found, instead of void}
](
    chain [:pick* | :to-value]
)

take: redescribe [
    {Variant of TAKE* that will give an error if it can't take, vs. void}
](
    chain [
        :take*
            |
        func [
            return: [any-value!]
            took [<opt> any-value!]
        ][
            either set? 'took [
                :took
            ][
                fail "Can't TAKE from series end (see TAKE* to get void)"
            ]
        ]
    ]
)

parse?: redescribe [
    {Variant of PARSE that enforces a TRUE or FALSE result from the rules.}
](
    chain [
        :parse
            |
        func [x][
            unless logic? :x [
                fail [
                    "Rules passed to PARSE? returned non-LOGIC!:" (mold :x)
                ]
            ]
            x
        ]
    ]
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
    value [any-word! any-path! function!]
][
    if path? :value [fail "arity-of for paths is not yet implemented."]

    unless function? :value [
        value: get value
        unless function? :value [return 0]
    ]

    if variadic? :value [
        fail "arity-of cannot give reliable answer for variadic functions"
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
    name [string!]
    source [any-word! any-path!]
][
    case [
        not lookback? source [false]
        equal? n arity: arity-of source [true]
        n < arity [
            ; If the queried arity is lower than the arity of the function,
            ; assume it's ok...e.g. PREFIX? callers know INFIX? exists (but
            ; we don't assume INFIX? callers know PREFIX?/ENDFIX? exist)
            false
        ]
    ] else [
        fail [
            name "used on lookback function with arity" arity
                |
            "Use LOOKBACK? for generalized (tricky) testing"
        ]
    ]


]

endfix?: redescribe [
    {TRUE if a no-argument function is SET/LOOKBACK to not allow right infix.}
](
    specialize :nfix? [n: 0 | name: "ENDFIX?"]
)

postfix?: redescribe [
    {TRUE if an arity 1 function is SET/LOOKBACK to act as postfix.}
](
    specialize :nfix? [n: 1 | name: "POSTFIX?"]
)

infix?: redescribe [
    {TRUE if an arity 2 function is SET/LOOKBACK to act as infix.}
](
    specialize :nfix? [n: 2 | name: "INFIX?"]
)


set-nfix: function [
    return: [function!]
    n [integer!]
    name [string!]
    target [any-word! any-path!]
    value [function!]
][
    unless equal? n arity-of :value [
        fail [name "requires arity" n "functions, see SET/LOOKAHEAD"]
    ]
    set/lookback target :value
]

set-endfix: redescribe [
    {Convenience wrapper for SET/LOOKBACK that ensures function is arity 0.}
](
    specialize :set-nfix [n: 0 | name: "SET-ENDFIX"]
)

set-postfix: redescribe [
    {Convenience wrapper for SET/LOOKBACK that ensures a function is arity 1.}
](
    specialize :set-nfix [n: 1 | name: "SET-POSTFIX"]
)

set-infix: redescribe [
    {Convenience wrapper for SET/LOOKBACK that ensures a function is arity 2.}
](
    specialize :set-nfix [n: 2 | name: "SET-INFIX"]
)


lambda: function [
    {Convenience variadic wrapper for FUNC and FUNCTION constructors}

    return: [function!]
    :args [<end> word! path! block!]
        {Block of argument words, or a single word (passed via LIT-WORD!)}
    :body [any-value! <...>]
        {Block that serves as the body or variadic elements for the body}
    /only
        {Use FUNC and do not run locals-gathering on the body}
][
    f: either only [:func] [:function]

    f (
        :args then [to block! args] else [[]]
    )(
        if block? first body [
            take body
        ] else [
            make block! body
        ]
    )
]

left-bar: func [
    {Expression barrier that evaluates to left side but executes right.}
    return: [<opt> any-value!]
        {Evaluative result of `left`.}
    left [<opt> <end> any-value!]
        {A single complete expression on the left.}
    right [<opt> any-value! <...>]
        {Any number of expressions on the right.}
    :look [any-value! <...>]
][
    ; !!! Should this fail if left is END?  How would it tell the difference
    ; between left being void or end, is that exposed with SEMIQUOTED?

    loop-until [
        while [bar? first look] [take look] ;-- want to feed past BAR!s
        take* right ;-- a void eval or an end both give back void here
        tail? look
    ]
    :left
]

right-bar: func [
    {Expression barrier that evaluates to first expression on right.}
    return: [<opt> any-value!]
        {Evaluative result of first of the right expressions.}
    left [<opt> <end> any-value!]
        {A single complete expression on the left.}
    right [<opt> any-value! <...>]
        {Any number of expressions on the right.}
    :look [any-value! <...>]
][
    ; !!! This could fail if `tail? right`, but should it?  Might make
    ; COMPOSE situations less useful, e.g. `compose [thing |> (may-be-void)]`

    also (
        ; We want to make sure `1 |> | 2 3 4` is void, not BAR!
        ;
        either* bar? first look [void] [take* right]
    )(
        loop-until [
            while [bar? first look] [take look]
            take* right ;-- a void eval or an end both give back void here
            tail? look
        ]
    )
]


once-bar: func [
    {Expression barrier that's willing to only run one expression after it}
    return: [<opt> any-value!]
    left [<opt> <end> any-value!]
    right [<opt> any-value! <...>]
    :lookahead [any-value! <...>]
    look:
][
    also take right (
        unless any [
            tail? right
                |
            '|| = look: take lookahead ;-- hack...recognize selfs
        ][
            fail [
                "|| expected single expression, found residual of" :look
            ]
        ]
    )
]


use: func [
    {Defines words local to a block.}
    return: [<opt> any-value!]
    vars [block! word!] {Local word(s) to the block}
    body [block!] {Block to evaluate}
][
    ; We are building a FUNC out of the body that was passed to us, and that
    ; body may have RETURN words with bindings in them already that we do
    ; not want to disturb with the definitional bindings in the new code.
    ; So that means either using MAKE FUNCTION! (which wouldn't disrupt
    ; RETURN bindings) or using the more friendly FUNC and `<with> return`
    ; (they do the same thing, just FUNC is arity-2)
    ;
    ; <durable> is used so that the data for the locals will still be
    ; available if any of the words leak out and are accessed after the
    ; execution is finished.
    ;
    eval func compose [<durable> <local> (vars) <with> return] body
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
    "Creates a new module."
    spec [block! object!] "The header block of the module (modified)"
    body [block!] "The body block of the module (modified)"
    /mixin "Mix in words from other modules"
    mixins [object!] "Words collected into an object"
    /local hidden w mod
][
    mixins: to-value :mixins

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

    if find spec/options 'extension [
        append mod 'lib-base ; specific runtime values MUST BE FIRST
    ]

    unless spec/type [spec/type: 'module] ; in case not set earlier

    ; Collect 'export keyword exports, removing the keywords
    if find body 'export [
        unless block? select spec 'exports [
            join spec ['exports make block! 10]
        ]

        ; Note: 'export overrides 'hidden, silently for now
        parse body [while [
            to 'export remove skip opt remove 'hidden opt
            [
                set w any-word! (
                    unless find spec/exports w: to word! w [
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
                    unless find select spec 'exports w: to word! w [
                        append hidden w]
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
        if function? first args [
            change/only args meta-of first args
        ]
    ]

    ; Build and raise the error:
    do make error! [
        type: err-type
        id:   err-id
        arg1: first args
        arg2: second args
        arg3: third args
    ]
]


fail: function [
    {Interrupts execution by reporting an error (a TRAP can intercept it).}

    reason [error! string! block!]
        "ERROR! value, message string, or failure spec"
    /where
        "Specify an originating location other than the FAIL itself"
    location [frame! any-word!]
        "Frame or parameter at which to indicate the error originated"
][
    ; By default, make the originating frame the FAIL's frame
    ;
    unless where [location: context-of 'reason]

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
    case [
        error? reason [
            error: reason
        ]
        string? reason [
            error: make error! reason
        ]
        block? reason [
            error: make error! spaced reason
        ]
    ]

    ; !!! Does SET-LOCATION-OF-ERROR need to be a native?
    ;
    set-location-of-error error location

    ; Raise error to the nearest TRAP up the stack (if any)
    ;
    do error
]


ensure: function [
    {Pass through a value only if it matches types (or TRUE?/FALSE? state)}
    return: [<opt> any-value!]
    test [function! datatype! typeset! block! logic!]
    arg [<opt> any-value!]
][
    case* [
        void? temp: maybe test :arg [
            assert [any [void? :arg | not :arg]]

            ; The test passed but we want to avoid an accidental usage like
            ; `if ensure [logic!] some-false-thing [...]` where the test
            ; passed but the passthru gets used conditionally.  So FALSE?
            ; things are converted to void.
            ;
            ()
        ]
        blank? :temp [
            fail/where [
                "ENSURE expected arg to match" (test)
            ] 'arg
        ]
        true [
            assert [all [:temp | :arg = :temp]]
            :temp
        ]
    ]
]
