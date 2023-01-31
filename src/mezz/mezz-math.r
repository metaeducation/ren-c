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

cos: runs :cosine/radians
sin: runs :sine/radians
tan: runs :tangent/radians  ; contentious with color "tan" (CSS and elsewhere)
acos: runs :arccosine/radians
asin: runs :arcsine/radians
atan: runs :arctangent/radians

modulo: func [
    "Compute a remainder of A divided by B with the sign of B."
    return: [any-number! money! time!]
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
    any [
        a + r = a, b + r = b  ; 'almost zero'
        all [  ; 'almost b'
            (a + r) = (a + b)
            positive? (r + r) - b
        ]
    ] then [
        return 0.0
    ]
    return r
]

mod: enfixed :modulo
pow: enfixed :power

sign-of: func [
    "Returns sign of number as 1, 0, or -1 (to use as multiplier)."
    number [any-number! money! time!]
][
    return case [
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
        if (comparator/ [first series first spot]) [spot: series]
    ]
    return spot
]

minimum-of: redescribe [
    {Finds the smallest value in a series}
](
    specialize :extreme-of [comparator: unrun :lesser?]
)

maximum-of: redescribe [
    {Finds the largest value in a series}
](
    specialize :extreme-of [comparator: unrun :greater?]
)


; A simple iterative implementation; returns 1 for negative
; numbers. FEEL FREE TO IMPROVE THIS!
;
factorial: func [n [integer!]] [
    if n < 2 [return 1]
    let res: 1
    ; should avoid doing the loop for i = 1...
    return count-up i n [res: res * i]
]
