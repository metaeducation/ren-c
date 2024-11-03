; datatypes/tuple.r

(tuple? 1.2.3)
(not tuple? 1)
(tuple! = type of 1.2.3)

; Test that scanner compacted forms match forms built from lists
;
(1.2.3 = to tuple! [1 2 3])
;(1x2 = to tuple! [1 2])  ; !!! TBD when unified with pairs

(error? sys.util/rescue [load "1."])  ; !!! Reserved
(error? sys.util/rescue [load ".1"])  ; !!! Reserved
;(.1 = to tuple! [_ 1])  ; No representation due to reservation
;(1. = to tuple! [1 _])  ; No representation due to reservation

("1.2.3" = mold 1.2.3)

; minimum
(tuple? make tuple! [])

; there is no longer a maximum (if it won't fit in a cell, it will allocate
; a series)

(tuple? 255.255.255.255.255.255.255)
(
    tuple: transcode:one "1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1.1"
    all [
        30 = length of tuple  ; too big to fit in cell on 32-bit -or- 64-bit
        (cfor 'i 1 30 1 [
            assert [tuple.(i) = 1]
        ] ok)
    ]
)


; TO Conversion tests
(
    tests: [
        "a.b.c" [a b c]
        "a b c" [a b c]
        "1.2.3" [1 2 3]
        "1 2 3" [1 2 3]
    ]

    for-each [text structure] tests [
        let tuple: ensure tuple! to tuple! text
        assert [(length of tuple) = (length of structure)]
        cfor 'i 1 (length of tuple) 1 [
            assert [tuple.(i) = structure.(i)]
        ]
    ]
    ok
)

; No implicit to blob! from tuple!
(
    a-value: 0.0.0.0
    not equal? to blob! a-value a-value
)

(
    a-value: 0.0.0.0
    equal? equal? to blob! a-value a-value equal? a-value to blob! a-value
)

(equal? 0.0.0 0.0.0)
(not equal? 0.0.1 0.0.0)


; These tests were for padding in R3-Alpha of TUPLE! which is not supported
; by the generalized tuple mechanics.
;
(comment [
    ; tuple! right-pads with 0
    (equal? 1.0.0 1.0.0.0.0.0.0)
    ; tuple! right-pads with 0
    (equal? 1.0.0.0.0.0.0 1.0.0)
] ok)

[
    ~bad-pick~ !! (pick 'a/b 1000)
    (null = try pick 'a/b 1000)

    ~bad-pick~ !! (pick 'a/b 0)
    (null = try pick 'a/b 0)
]

[
    (3 = length of 'a.b.c)
    (not empty? 'a.b.c)
    ~type-has-no-index~ !! (index of 'a.b.c)
    (null = try index of 'a.b.c)
]

(
    e: trap [to tuple! [_ _]]
    all [
        e.id = 'conflated-sequence
        e.arg1 = word!
        e.arg2 = '.
        word? e.arg2
    ]
)
(
    e: trap [to tuple! [~ ~]]
    all [
        e.id = 'conflated-sequence
        e.arg1 = quasiform!
        e.arg2 = '~.~
        quasiform? e.arg2
        '. = unquasi e.arg2
    ]
)
(
    e: trap [to tuple! [a _ b]]
    all [
        e.id = 'bad-sequence-blank
    ]
)

; Historical math on all-integer tuples; preserved for now.
;
(3.3.3 = (1.1.1 + 2.2.2))
(3.3.3.1 = (1.1.1.1 + 2.2.2))
(3.3.3.2 = (1.1.1 + 2.2.2.2))
