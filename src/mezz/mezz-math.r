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


; Shorthands for radian forms of trig functions, first introduced by Red.
; http://www.red-lang.org/2014/08/043-floating-point-support.html

cos: :cosine/radians
sin: :sine/radians
tan: :tangent/radians ;; contentious with color "tan" (in CSS and elsewhere)
acos: :arccosine/radians
asin: :arcsine/radians
atan: :arctangent/radians


mod: function [
    "Compute a nonnegative remainder of A divided by B."
    a [any-number! money! time!]
    b [any-number! money! time!]
        "Must be nonzero."
][
    ; This function tries to find the remainder that is "almost non-negative"
    ; Example: 0.15 - 0.05 - 0.1 // 0.1 is negative,
    ; but it is "almost" zero, i.e. "almost non-negative"

    ; Compute the smallest non-negative remainder
    all [negative? r: remainder a b | r: r + b]
    ; Use abs a for comparisons
    a: abs a
    ; If r is "almost" b (i.e. negligible compared to b), the
    ; result will be r - b. Otherwise the result will be r
    either all [(a + r) = (a + b) | positive? (r + r) - b] [r - b] [r]
]


modulo: function [
    {Wrapper for MOD that handles errors like REMAINDER.}
    return:
        {Negligible values (compared to A and B) are rounded to zero}
    a [any-number! money! time!]
    b [any-number! money! time!]
        "Absolute value will be used"
] [
    ; Coerce B to a type compatible with A
    any [any-number? a  b: make a b]
    ; Get the "accurate" MOD value
    r: mod a abs b
    ; If the MOD result is "near zero", w.r.t. A and B,
    ; return 0--the "expected" result, in human terms.
    ; Otherwise, return the result we got from MOD.
    either any [(a - r) = a | (r + b) = b] [make r 0] [r]
]


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

    return: [any-series!] {Position where the extreme value was found}
    series [any-series!] {Series to search}
    comparator [action!] {Comparator to use, e.g. LESSER? for MINIMUM-OF}
    /skip {Treat the series as records of fixed size}
    size [integer!]
    <local> spot
][
    size: default [1]
    if 1 > size [cause-error 'script 'out-of-range size]
    spot: series
    for-skip series size [
        if (comparator first series first spot) [spot: series]
    ]
    spot
]

minimum-of: redescribe [
    {Finds the smallest value in a series}
](
    specialize 'extreme-of [comparator: :lesser?]
)

maximum-of: redescribe [
    {Finds the largest value in a series}
](
    specialize 'extreme-of [comparator: :greater?]
)


; A simple iterative implementation; returns 1 for negative
; numbers. FEEL FREE TO IMPROVE THIS!
;
factorial: func [n [integer!] <local> res] [
    if n < 2 [return 1]
    res: 1
    ; should avoid doing the loop for i = 1...
    repeat i n [res: res * i]
]


; This MATH implementation is from Gabrielle Santilli circa 2001, found
; via http://www.rebol.org/ml-display-thread.r?m=rmlXJHS. It implements the
; much-requested (by new users) idea of * and / running before + and - in
; math expressions. Expanded to include functions.
;
math: function [
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

    slash (to-lit-word first [ / ])

    expr-val (_)

    expr-op (_)

    expression  ([
        term (expr-val: term-val)
        any [
            ['+ (expr-op: 'add) | '- (expr-op: 'subtract)]
            term (expr-val: compose [(expr-op) (expr-val) (term-val)])
        ]
    ])

    term-val (_)

    term-op (_)

    term ([
        pow (term-val: power-val)
        any [
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
            any [
                nested-expression (append prim-val take nested-expr-val)
            ]
        | and group! into nested-expression (prim-val: take nested-expr-val)
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
                insert/only nested-expr-val expr-val
                restore-vars
            )
            ; vars could be changed even it failed, so restore them and fail
            | (restore-vars) fail

    ])
][
    clear nested-expr-val
    res: either parse expr expression [expr-val] [blank]

    either only [res] [
        ret: reduce res
        unless all [
            1 = length of ret
            any-number? ret/1
        ][
            fail [
                unspaced ["Cannot be REDUCED to a number(" mold ret ")"]
                ":" mold res
            ]
        ]
        ret/1
    ]
]
