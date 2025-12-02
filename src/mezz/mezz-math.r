Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Mezzanine: Math"
    rights: --[
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
]

pi: 3.14159265358979323846

odd?: redescribe ["Returns OKAY if the number is odd"] (
    cascade [even?/ not/]
)


; Shorthands for radian forms of trig functions, first introduced by Red.
; http://www.red-lang.org/2014/08/043-floating-point-support.html

cos: cosine:radians/
sin: sine:radians/
tan: tangent:radians/  ; contentious with color "tan" (CSS and elsewhere)
acos: arccosine:radians/
asin: arcsine:radians/
atan: arctangent:radians/

modulo: func [
    "Compute a remainder of A divided by B with the sign of B."
    return: [any-number? money! time!]
    a [any-number? money! time!]
    b [any-number? money! time!] "Must be nonzero."
    :adjusted "Set 'almost zero' and 'almost B' to zero"
][
    ; This function tries to find the remainder that is "almost non-negative"
    ; Example: 0.15 - 0.05 - 0.1 // 0.1 is negative,
    ; but it is "almost" zero, i.e. "almost non-negative"

    ; Compute the smallest remainder with the same sign as b
    let r: remainder a b
    if (sign of r) = negate sign of b [r: r + b]
    if not adjusted [return r]
    if (sign of a) = negate sign of b [a: negate a]
    ; If r is "almost" b (i.e. negligible compared to b), the
    ; result will be 0. Otherwise the result will be r
    any [
        a + r ?= a, b + r ?= b  ; 'almost zero'
        all [  ; 'almost b'
            (a + r) ?= (a + b)
            positive? (r + r) - b
        ]
    ] then [
        return 0.0
    ]
    return r
]

mod: infix modulo/
pow: infix power/

sign-of: func [
    "Returns sign of number as 1, 0, or -1 (to use as multiplier)."
    value [any-number? money! time!]
][
    return case [
        positive? value [1]
        negative? value [-1]
    ] else [0]
]

extreme-of: func [
    "Finds position of value with most extreme property in a series"

    return: [any-series?]
    series [any-series?]
    comparator "Comparator to use, e.g. LESSER? for MINIMUM-OF"
        [<unrun> frame!]
    :skip "Treat the series as records of fixed size"
        [integer!]
][
    skip: default [1]
    if 1 > skip [cause-error 'script 'out-of-range skip]
    let spot: series
    iterate-skip @series skip [
        if (comparator // [first series first spot]) [spot: series]
    ]
    return spot
]

minimum-of: redescribe [
    "Finds the smallest value in a series"
](
    specialize extreme-of/ [comparator: lesser?/]
)

maximum-of: redescribe [
    "Finds the largest value in a series"
](
    specialize extreme-of/ [comparator: greater?/]
)


; A simple iterative implementation; returns 1 for negative
; numbers. FEEL FREE TO IMPROVE THIS!
;
factorial: func [n [integer!]] [
    if n < 2 [return 1]
    let res: 1
    ; should avoid doing the loop for i = 1...
    return count-up 'i n [res: res * i]
]
