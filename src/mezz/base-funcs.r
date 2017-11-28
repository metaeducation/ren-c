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


set/enfix quote enfix: proc [
    "Convenience version of SET/ENFIX, e.g `+: enfix :add`"
    :target [set-word! set-path!]
    action [function!]
][
    set/enfix target :action

    ; return value can't currently be given back as enfix, since it is a
    ; property of words and not values.  So it isn't given back at all (as
    ; this is a PROC).  Is this sensible?
]


default: enfix func [
    "Set word or path to a default value if it is not set yet or blank."

    return: [any-value!]
    'target [set-word! set-path!]
        "The word to which might be set"
    value [any-value!] ; not <opt> on purpose
        "Value to set (blocks and 0-arity functions evaluated)"
    /only
        "Consider target being BLANK! to be a value not to overwrite"

    <local> gotten
][
    ; Protect against `var: 10 | x: default var`, only allow vars if blocks
    ; (same general behavior as IF)
    ;
    unless any [only | maybe [function! block!] :value] [
        unless semiquoted? 'value [
            fail ["Evaluated non-block/function used with DEFAULT" mold value]
        ]
    ]

    ; A lookback quoting function that quotes a SET-WORD! on its left is
    ; responsible for setting the value if it wants it to change since the
    ; SET-WORD! is not actually active.  But if something *looks* like an
    ; assignment, it's good practice to evaluate the whole expression to
    ; the result the SET-WORD! was set to, so `x: y: op z` makes `x = y`.
    ;
    ; Note: This overwrites the variable with SET* even if it's setting it
    ; back to its old value.  That's potentially wasteful, but also might
    ; bother a data breakpoint.  Though you might see it either way, if a
    ; variable -could- be modified by a line, you may want to know that...
    ; perhaps the debugger could monitor for *changes*.  Either way, this
    ; should be a native, so it's not worth worrying about.
    ;
    set* target either-test/only
        (only ?? :any-value? !! :something?) ;-- test function
        get* target ;-- value to test, and to return if passes test
        :value ;-- branch to use if test fails
]

update: enfix func [
    "Set word or path to a default value if that value is set and not blank."

    return: [any-value!]
    'target [set-word! set-path!]
        "The word to which might be set"
    value [<opt> any-value!]
        "Value to set (blocks and 0-arity functions evaluated)"
    /only
        "Consider value being BLANK! to be a value to use for overwriting"

    <local> gotten
][
    ; See notes on DEFAULT above, which works the same way.  This should also
    ; ultimately be a native, once a name is invented for the prefix form.

    unless any [only | maybe [function! block!] :value] [
        unless semiquoted? 'value [
            fail ["Evaluated non-block/function used with UPDATE" mold value]
        ]
    ]

    set* target either-test/only
        (only ?? :any-value? !! :something?) ;-- test function
        if* true :value ;-- use IF to dispatch branch for value being tested
        [get* target] ;-- branch if test fails, in block to avoid execution
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
        fail ;-- failing here means rolling over to next rule
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
                other: really any-context! get other
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
        <static> (
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
does: specialize 'func [spec: []]


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

            not notes: get 'meta/parameter-notes [
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

                        actual: first find words of :value param
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

redescribe [
    {A shortcut to define a function that has no arguments or locals.}
] :does


default*: enfix redescribe [
    {Would be the same as DEFAULT/ONLY if paths could dispatch infix}
](
    specialize 'default [only: true]
)

update*: enfix redescribe [
    {Would be the same as UPDATE/ONLY if paths could dispatch infix}
](
    specialize 'update [only: true]
)


; Though this name is questionable, it's nice to be easier to call
;
semiquote: specialize 'identity [quote: true]


get*: redescribe [
    {Variation of GET which returns void if the source is not set}
](
    specialize 'get [only: true]
)

get-value: redescribe [
    {Variation of GET which fails if the value is not set (vs. void or blank)}
](
    chain [
        :get*
            |
        specialize 'either-test-value [
            branch: [
                fail "GET-VALUE requires source variable to be set"
            ]
        ]
    ]
)

set*: redescribe [
    {Variation of SET where voids are tolerated for unsetting variables.}
](
    specialize 'set [only: true]
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
    {Same as IF/ONLY (void, not blank, if branch evaluates to void)}
](
    specialize 'if [only: true]
)

unless?: redescribe [
    {Variation of UNLESS which returns TRUE if the branch runs, FALSE if not}
](
    chain [:unless | :any-value?]
)

unless*: redescribe [
    {Same as UNLESS/ONLY (void, not blank, if branch evaluates to void)}
](
    specialize 'unless [only: true]
)

either*: redescribe [
    {Same as EITHER/ONLY (void, not blank, if branch evaluates to void)}
](
    specialize 'either [only: true]
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
    {Same as CASE/ONLY (void, not blank, if branch evaluates to void)}
](
    specialize 'case [only: true]
)

switch?: redescribe [
    {Variation of SWITCH which returns TRUE if any cases run, FALSE if not}
](
    chain [:switch | :any-value?]
)

switch*: redescribe [
    {Same as SWITCH/ONLY (void, not blank, if branch evaluates to void)}
](
    specialize 'switch [only: true]
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

maybe: redescribe [
   {Check value using tests (match types, TRUE? or FALSE?, filter function)}
](
    adapt specialize 'either-test [
        ;
        ; return blank on test failure (can't be plain _ due to "evaluative
        ; bit" rules...should this be changed so exemplars clear the bit?)
        ;
        branch: []
        only: false ;-- no /ONLY, hence void branch returns BLANK!
    ][
        if void? :value [ ; !!! TBD: filter this via REDESCRIBE when possible
            fail "Cannot use MAYBE on void values (try using EITHER-TEST)"
        ]

        ; !!! Since a BLANK! result means test failure, an input of blank
        ; can't discern a success or failure.  Yet prohibiting blanks as
        ; input seems bad.  A previous iteration of MAYBE would get past this
        ; by returning blank on failure, but void on success...to help cue
        ; a problem to conditionals.  That is not easy to do with a
        ; specialization in this style, so just let people deal with it for
        ; now...e.g. `maybe [function! block!] blank` will be blank, but so
        ; will be `maybe [blank!] blank`.
    ]
)

maybe?: redescribe [
    {Check value using tests (match types, TRUE? or FALSE?, filter function)}
    ; return: [logic!] ;-- blocks for type changes not supported yet
    ;    {TRUE if match, FALSE if no match (use MAYBE to pass through value)}
](
    adapt chain [
        specialize 'either-test [
            branch: [] ; return void on test failure
            only: true
        ]
            |
        :any-value?
    ][
        if void? :value [ ; !!! TBD: filter this via REDESCRIBE when possible
            fail "Cannot use MAYBE? on void values (try using EITHER-TEST)"
        ]
    ]
)

really: redescribe [
    {Pass through value if it matches test, otherwise trigger a FAIL}
](
    specialize 'either-test [
        branch: func [value [<opt> any-value!]] [
            fail [
                "REALLY did not expect argument of type" type of :value
            ]

            ; !!! There is currently no good way to SPECIALIZE a conditional
            ; which takes a branch, with a branch that refers to parameters
            ; of the running specialization.  Hence, there's no way to say
            ; something like /WHERE 'TEST to indicate a parameter from the
            ; callsite, until a solution is found for that. :-(
        ]
        only: false ;-- Doesn't matter (it fails) just hide the refinement
    ]
)

ensure: func [
    {FAIL if value is void (or blank if not /ONLY), otherwise pass it through}

    value [any-value!] ;-- always checked for void, since no <opt>
    /only
        {Just make sure the value isn't void, pass through BLANK!}
][
    only ?? :value else [
        either-test :something? :value [
            fail/where
                ["ENSURE received a BLANK! (use ENSURE* if this is ok)"]
                'value
        ] 
    ]
]

ensure*: specialize 'ensure [only: true]


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
        specialize 'either-test-value [
            branch: [
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
        specialize 'either-test [
            test: :logic?
            branch: [
                fail [
                    "Rules passed to PARSE? returned non-LOGIC!:" (mold :x)
                ]
            ]
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
        :args then [to block! args] !! []
    )(
        if block? first body [
            take body
        ] else [
            make block! body
        ]
    )
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
    if* not tail? expressions [take* expressions] also-do [do expressions]
]


once-bar: func [
    {Expression barrier that's willing to only run one expression after it}

    return: [<opt> any-value!]
    right [<opt> any-value! <...>]
    :lookahead [any-value! <...>]
    look:
][
    take right also-do [
        unless any [
            tail? right
                |
            '|| = look: take lookahead ;-- hack...recognize selfs
        ][
            fail [
                "|| expected single expression, found residual of" :look
            ]
        ]
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
        do compose/only [really (types) (var)] ;-- names to show if fails
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


; In order to be able to call usermode FAIL from the rebFail() API directly,
; it is a native that can be hijacked with something more sophisticated.
; A similar strategy is used in PANIC, so see also rebPanic()
;
;    reason [error! string! block!]
;        "ERROR! value, message string, or failure spec"
;    /where
;        "Specify an originating location other than the FAIL itself"
;    location [frame! any-word!]
;        "Frame or parameter at which to indicate the error originated"
;
hijack 'fail copy adapt 'fail [
    ; By default, make the originating frame the FAIL's frame
    ;
    unless where [location: context of 'reason]

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

    ; Should not fall through to the default FAIL which just panics
]
