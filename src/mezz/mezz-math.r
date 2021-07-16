REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Mezzanine: Math"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
]

pi: 3.14159265358979323846
tau: 6.28318530717958647693

; Shorthands for radian forms of trig functions, first introduced by Red.
; http://www.red-lang.org/2014/08/043-floating-point-support.html

cos: :cosine/radians
sin: :sine/radians
tan: :tangent/radians  ; contentious with color "tan" (in CSS and elsewhere)
acos: :arccosine/radians
asin: :arcsine/radians
atan: :arctangent/radians

modulo: func [
    "Compute a remainder of A divided by B with the sign of B."
    a [any-number! money! time!]
    b [any-number! money! time!] "Must be nonzero."
    /adjusted "Set 'almost zero' and 'almost B' to zero"
][
    ; This function tries to find the remainder that is "almost non-negative"
    ; Example: 0.15 - 0.05 - 0.1 // 0.1 is negative,
    ; but it is "almost" zero, i.e. "almost non-negative"

    ; Compute the smallest remainder with the same sign as b
    let r: remainder a b
    if (sign? r) = negate sign? b [r: r + b]
    if not adjusted [return r]
    if (sign? a) = negate sign? b [a: negate a]
    ; If r is "almost" b (i.e. negligible compared to b), the
    ; result will be 0. Otherwise the result will be r
    if any [
        a + r = a, b + r = b ; 'almost zero'
        all [ ; 'almost b'
            (a + r) = (a + b)
            positive? (r + r) - b
        ]
    ] [return 0.0]
    r
]

mod: enfixed :modulo
pow: enfixed :power

sign-of: func [
    "Returns sign of number as 1, 0, or -1 (to use as multiplier)."
    number [any-number! money! time!]
][
    case [
        positive? number [1]
        negative? number [-1]
    ] else [0]
]

extreme-of: func [
    {Finds the value with a property in a series that is the most "extreme"}

    return: "Position where the extreme value was found"
        [any-series!]
    series [any-series!]
    comparator "Comparator to use, e.g. LESSER? for MINIMUM-OF"
        [action!]
    /skip "Treat the series as records of fixed size"
        [integer!]
][
    skip: default [1]
    if 1 > skip [cause-error 'script 'out-of-range skip]
    let spot: series
    iterate-skip series skip [
        if (comparator first series first spot) [spot: series]
    ]
    spot
]

minimum-of: redescribe [
    {Finds the smallest value in a series}
](
    specialize :extreme-of [comparator: :lesser?]
)

maximum-of: redescribe [
    {Finds the largest value in a series}
](
    specialize :extreme-of [comparator: :greater?]
)


; A simple iterative implementation; returns 1 for negative
; numbers. FEEL FREE TO IMPROVE THIS!
;
factorial: func [n [integer!]] [
    if n < 2 [return 1]
    let res: 1
    ; should avoid doing the loop for i = 1...
    count-up i n [res: res * i]
]


; This MATH implementation is from Gabrielle Santilli circa 2001, found
; via http://www.rebol.org/ml-display-thread.r?m=rmlXJHS. It implements the
; much-requested (by new users) idea of * and / running before + and - in
; math expressions. Expanded to include functions.
;
math: func [
    {Process expression taking "usual" operator precedence into account.}

    expr [block!]
        {Block to evaluate}
    /only
        {Translate operators to their prefix calls, but don't execute}

    ; !!! This creation of static rules helps avoid creating those rules
    ; every time, but has the problem that the references to what should
    ; be locals are bound to statics as well (e.g. everything below which
    ; is assigned with BLANK! really should be relatively bound to the
    ; function, so that it will refer to the specific call.)  It's not
    ; technically obvious how to do that, not the least of the problem is
    ; that statics are currently a usermode feature...and injecting relative
    ; binding information into something that's not the function body itself
    ; isn't implemented.

    <static>

    slash (the /)

    expr-val (_)

    expr-op (_)

    expression  ([
        term (expr-val: term-val)
        while [
            ['+ (expr-op: 'add) | '- (expr-op: 'subtract)]
            term (expr-val: compose [(expr-op) (expr-val) (term-val)])
        ]
        end
    ])

    term-val (_)

    term-op (_)

    term ([
        pow (term-val: power-val)
        while [
            ['* (term-op: 'multiply) | slash (term-op: 'divide)]
            pow (term-val: compose [(term-op) (term-val) (power-val)])
        ]
    ])

    power-val (_)

    pow ([
        unary (power-val: unary-val)
        opt ['** unary (power-val: compose [power (power-val) (unary-val)])]
    ])

    unary-val (_)

    pre-uop (_)

    post-uop (_)

    unary ([
        (post-uop: pre-uop: [])
        opt ['- (pre-uop: 'negate)]
        primary
        opt ['! (post-uop: 'factorial)]
        (unary-val: compose [(post-uop) (pre-uop) (prim-val)])
    ])

    prim-val (_)

    primary ([
        set prim-val any-number!
        | set prim-val [word! | path!] (prim-val: reduce [prim-val])
            ; might be a funtion call, looking for arguments
            while [
                nested-expression (append prim-val take nested-expr-val)
            ]
        | ahead group! into nested-expression (prim-val: take nested-expr-val)
    ])

    p-recursion (_)

    nested-expr-val ([])

    save-vars (func [][
            p-recursion: reduce [
                :p-recursion :expr-val :expr-op :term-val :term-op :power-val :unary-val
                :pre-uop :post-uop :prim-val
            ]
        ])

    restore-vars (func [][
            set [
                p-recursion expr-val expr-op term-val term-op power-val unary-val
                pre-uop post-uop prim-val
            ] p-recursion
        ])

    nested-expression ([
            ;all of the static variables have to be saved
            (save-vars)
            expression
            (
                ; This rule can be recursively called as well,
                ; so result has to be passed via a stack
                insert nested-expr-val ^expr-val
                restore-vars
            )
            ; vars could be changed even it failed, so restore them and fail
            | (restore-vars) fail

    ])
][
    clear nested-expr-val
    let res: either parse expr expression [expr-val] [blank]

    either only [
        res
    ][
        ret: reduce res
        all [
            1 = length of ret
            any-number? ret/1
        ] else [
            fail [
                unspaced ["Cannot be REDUCED to a number (" mold ret ")"]
                ":" mold res
            ]
        ]
        ret/1
    ]
]
