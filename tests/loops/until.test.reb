; %loops/until.test.reb

(
    num: 0
    until [num: num + 1 num > 9]
    num = 10
)
; Test body-block return values
(1 = until [1])
; Test break
(null? until [break true])
; Test continue
(
    success: true
    cycle?: true
    until [if cycle? [cycle?: false, continue, success: false] true]
    success
)
; Test that return stops the loop
(
    f1: func [return: [integer!]] [until [return 1]]
    1 = f1
)
; Test that errors do not stop the loop
(1 = until [trap [1 / 0] 1])
; Recursion check
(
    num1: 0
    num3: 0
    until [
        num2: 0
        until [
            num3: num3 + 1
            1 < (num2: num2 + 1)
        ]
        4 < (num1: num1 + 1)
    ]
    10 = num3
)


; === PREDICATES ===

(
    x: [2 4 6 8 7 9 11 30]
    did all [
        7 = until/predicate [take x] chain [:even?, :not]
        x = [9 11 30]
    ]
)(
    x: [1 "hi" <foo> _ <bar> "baz" 2]
    did all [
        blank? until/predicate [take x] z -> [blank? z]
        x = [<bar> "baz" 2]
    ]
)

; UNTIL truth tests the results, which means unstable isotopes have to be
; decayed to run that test.
[
    (1 = until [pack [1 2]])
    ('~['1 '2]~ = until [meta pack [1 2]])
]

; At one time, UNTIL errored upon receiving "isotopes", so that cases like
; `until [match [logic?] false]` would raise an error on a ~false~ isotopic
; word--as opposed to a plain #[false] value.  This protection no longer made
; sense once ~false~ the isotopic word *was* the representation of falseness.
; So there's no stopping you shooting yourself in the foot with MATCH here,
; you need to use something like DID.
[
    (
        true = until [did match logic?! false]
    )
]

(
    n: 1
    9 = until/predicate [n: n + 2] lambda [x] [x > 7]
)
