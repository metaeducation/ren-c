REBOL [
    Title: "Rebol2 and R3-Alpha Future Bridge to Ren/C"
    Rights: {
        Rebol is Copyright 1997-2015 REBOL Technologies
        REBOL is a trademark of REBOL Technologies

        Ren/C is Copyright 2015 MetaEducation
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Author: "@HostileFork"
    Purpose: {
        These routines can be run from a Rebol2 or R3-Alpha
        to make them act more like Ren/C (which aims to
        implement a finalized Rebol3 standard).

        !!! Rebol2 support intended but not yet implemented.

        !!! This file is a placeholder for a good design, at time of writing
        it has repeated patterns that are just for expedience.  It is
        awaiting someone who has a vested interest in legacy code to become
        a "maintenance czar" for the concept.
    }
]

; Older versions of Rebol had a different concept of what FUNCTION meant
; (an arity-3 variation of FUNC).  Eventually the arity-2 construct that
; did locals-gathering by default named FUNCT overtook it, with the name
; FUNCT deprecated.
;
unless (copy/part words-of :function 2) = [spec body] [
    function: :funct
]

unless value? 'length [length: :length? unset 'length?]
unless value? 'index-of [index-of: :index? unset 'index?]
unless value? 'offset-of [offset-of: :offset? unset 'offset?]
unless value? 'type-of [type-of: :type? unset 'type?]

unless value? 'opt [
    opt: func [
        {NONEs become unset, all other value types pass through. (See: TO-VALUE)}
        value [any-type!]
    ][
        either none? get/any 'value [()][
            get/any 'value
        ]
    ]
]

unless value? 'to-value [
    to-value: func [
        {Turns unset to NONE, with ANY-VALUE! passing through. (See: OPT)}
        value [any-type!]
    ] [
        either unset? get/any 'value [none][:value]
    ]
]

unless value? 'something? [
    something?: func [value [any-type!]] [
        not any [
            unset? :value
            none? :value
        ]
    ]
]

unless value? 'for-each [
    for-each: :foreach
    ;unset 'foreach ;-- tolerate it (for now, maybe indefinitely?)

    ; Note: EVERY cannot be written in R3-Alpha because there is no way
    ; to write loop wrappers, given lack of definitionally scoped return
    ; or <transparent>
]

; *all* typesets now are ANY-XXX to help distinguish them from concrete types
; https://trello.com/c/d0Nw87kp
;
unless value? 'any-scalar? [any-scalar?: :scalar? any-scalar!: scalar!]
unless value? 'any-series? [any-series?: :series? any-series!: series!]
unless value? 'any-number? [any-number?: :number? any-number!: number!]
unless value? 'any-value? [
    any-value?: func [item [any-type!]] [not unset? :item]
    any-value!: any-type!
]

; It is not possible to make a version of eval that does something other
; than everything DO does in an older Rebol.  Which points to why exactly
; it's important to have only one function like eval in existence.
unless value? 'eval [
    eval: :do
]

unless value? 'fail [
    fail: func [
        {Interrupts execution by reporting an error (a TRAP can intercept it).}
        reason [error! string! block!] "ERROR! value, message string, or failure spec"
    ][
        case [
            error? reason [do error]
            string? reason [do make error! reason]
            block? reason [
                for-each item reason [
                    unless any [
                        any-scalar? :item
                        string? :item
                        paren? :item
                        all [
                            word? :item
                            not any-function? get :item
                        ]
                    ][
                        do make error! (
                            "FAIL requires complex expressions to be in a PAREN!"
                        )
                    ]
                ]
                do make error! form reduce reason
            ]
        ]
    ]
]

; R3-Alpha and Rebol2 did not allow you to make custom infix operators.
; There is no way to get a conditional infix AND using those binaries.
; In some cases, the bitwise and will be good enough for logic purposes...
;
unless value? 'and* [
    and*: :and
    and?: func [a b] [and* true? :a true? :b]
]
unless value? 'or+ [
    or+: :or
    or?: func [a b] [or+ true? :a true? :b]
]
unless value? 'xor- [
    xor-: :xor
    xor?: func [a b] [xor- true? :a true? :b]
]
