; %shove.test.r
;
; The SHOVE operator (->-) lets you push a left hand side argument
; into something on the right.  This is particularly useful for
; calling infix functions that need to be accessed via PATH!, as
; the evaluator will not do path evaluations speculatively (due
; to impacts on performance and potentially bad semantics).
;
;     >> 1 ->- lib/+ 2 * 3
;     == 9  ; as if you'd written `1 + 2 * 3`

; NORMAL parameter
[
    (left-normal: infix lambda [left right] [
        reduce [left right]
    ]
    right-normal: infix lambda [one two] [
        reduce [one two]
    ]
    value: [whatever]
    ok)

    ([[whatever] 1020] = (value ->- left-normal 1020))
    ([[whatever] 304] = (value ->- right-normal 304))
    ~x~ !! (first ((panic 'x) ->- left-normal 1020))
    ~x~ !! (first ((panic 'x) ->- right-normal 304))
]

; ^META parameter
[
    (left-meta: infix lambda [^left right] [
        reduce [left right]
    ]
    right-meta: infix lambda [^one two] [
        reduce [one two]
    ]
    ok)

    ([~null~ 1020] = (null ->- left-meta 1020))
    ([~null~ 304] = (null ->- right-meta 304))
    (warning? noquasi first ((panic 'x) ->- left-meta 1020))
    (warning? noquasi first ((panic 'x) ->- right-meta 304))
]

; @LITERAL bound parameter
[
    (left-lit-bound: infix lambda [@left right] [
        reduce [left right]
    ]
    right-lit-bound: infix lambda [@one two] [
        reduce [one two]
    ]
    ok)

    (all wrap [
        [null 1020] = block: (null ->- left-lit-bound 1020)
        null = get first block
    ])
    (all wrap [
        [null 304] = block: (null ->- right-lit-bound 304)
        null = get first block
    ])
    (all wrap [
        [(panic 'x) 1020] = block: ((panic 'x) ->- left-lit-bound 1020)
        :panic = get inside block.1 block.1.1
    ])
    (all wrap [
        [(panic 'x) 304] = block: ((panic 'x) ->- right-lit-bound 304)
        :panic = get inside block.1 block.1.1
    ])
]

; 'LITERAL as-is parameter
[
    (left-lit-as-is: infix lambda ['left right] [
        reduce [left right]
    ]
    right-lit-as-is: infix lambda ['one two] [
        reduce [one two]
    ]
    value: <whatever>
    ok)

    (all wrap [
        [value 1020] = block: (value ->- left-lit-as-is 1020)
        null = binding of first block
    ])
    (all wrap [
        [value 304] = block: (value ->- right-lit-as-is 304)
        null = binding of first block
    ])
    (all wrap [
        [(panic 'x) 1020] = block: ((panic 'x) ->- left-lit-as-is 1020)
        null = binding of inside block.1 block.1.1
    ])
    (all wrap [
        [(panic 'x) 304] = block: ((panic 'x) ->- right-lit-as-is 304)
        null = binding of inside block.1 block.1.1
    ])
]

; @(LITERAL) as-is soft parameter
;
; 1. There was only one convention for what needs to be two: @(LITERAL) and
;    '(LITERAL).
[
    (left-soft-as-is: infix lambda [@(left) right] [
        reduce [left right]
    ]
    right-soft-as-is: infix lambda [@(one) two] [
        reduce [one two]
    ]
    value: <whatever>
    ok)

    (all wrap [
        [value 1020] = block: (value ->- left-soft-as-is 1020)
        <whatever> = get first block  ; should not be bound [1]
    ])
    (all wrap [
        [value 304] = block: (value ->- right-soft-as-is 304)
        <whatever> = get first block  ; should not be bound [1]
    ])
    (all wrap [
        [value 1020] = block: (('value) ->- left-soft-as-is 1020)
        null = binding of first block
    ])
    (all wrap [
        [value 304] = block: (('value) ->- right-soft-as-is 304)
        null = binding of first block
    ])
    ~x~ !! (all wrap [
        block: ((panic 'x) ->- left-soft-as-is 1020)
    ])
    ~x~ !! (all wrap [
        block: ((panic 'x) ->- right-soft-as-is 304)
    ])
]

(
    x: null
    x: ->- default [10 + 20]
    x: ->- default [1000000]
    x = 30
)


; SHOVE should be able to handle refinements and contexts.
[
    (did obj: make object! [
        magic: infix lambda [a b :minus] [
            either minus [a - b] [a + b]
        ]
    ])

    ~???~ !! (1 obj/magic 2)  ; must use shove

    (3 = (1 ->- obj/magic 2))
    (-1 = (1 ->- obj/magic:minus 2))
]

; PATH! cannot be directly quoted left, must use ->-
[
    (
        left-the: infix :the
        o: make object! [i: 10 f: does [20]]
        ok
    )

    ('o.i = o.i left-the)
    (o.i ->- left-the = 'o.i)

    ~literal-left-path~ !! (o/f left-the)
    (o/f ->- left-the = 'o/f)
]


((the ->-) = first [->-])
((the ->- the) = 'the)
('x = (x ->- the))
(1 = (1 ->- the))

(7 = (1 + 2 ->- multiply 3))
(9 = ((1 + 2) ->- multiply 3))

(7 = (add 1 2 * 3))
(7 = (add 1 2 ->- lib/* 3))

(7 = (add 1 2 ->- multiply 3))
(7 = (add 1 2 ->- (:multiply) 3))

~expect-arg~ !! (10 ->- lib/= 5 + 5)
(10 ->- = (5 + 5))

~no-arg~ !! (
    add 1 + 2 ->- multiply 3
)
(
    x: add 1 + 2 3 + 4 ->- multiply 5
    x = 26
)

~no-arg~ !! (
    divide negate x: add 1 + 2 3 + 4 ->- multiply 5
)
(-1 = eval wrap [
    divide negate x: add 1 + 2 3 + 4 2 ->- multiply 5
])
