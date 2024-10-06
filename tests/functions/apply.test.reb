; %apply.test.reb
;
; Ren-C's APPLY is a hybrid of positional and non-positional application.

(-1 = apply negate/ [1])
([a b c d e] = apply append/ [[a b c] spread [d e]])

; Refinements can be provided in any order.  Commas can be used interstitially
[
    ([a b c d e d e] = apply append/ [[a b c] spread [d e] /dup 2])
    ([a b c d e d e] = apply append/ [/dup 2 [a b c] spread [d e]])
    ([a b c d e d e] = apply append/ [[a b c] /dup 2 spread [d e]])

    ([a b c d d] = apply append/ [/dup 2 [a b c] spread [d e] /part 1])
    ([a b c d d] = apply append/ [[a b c] spread [d e] /part 1 /dup 2])
]

; Not only can refinements be used by name, any parameter can.
; Once a parameter has been supplied by name, it is no longer considered for
; consuming positionally.
[
    ([a b c d e] = apply append/ [:series [a b c] :value spread [d e]])
    ([a b c d e] = apply append/ [:value spread [d e] :series [a b c]])

    ([a b c d e] = apply append/ [:series [a b c] spread [d e]])
    ([a b c d e] = apply append/ [:value spread [d e] [a b c]])
]

; Giving too many arguments is an error, unless you use :RELAX
[
    ~apply-too-many~ !! (
        apply append/ [[a b c] spread [d e] [f g]]
    )
    ~apply-too-many~ !! (
        apply append/ [:value spread [d e] [a b c] [f g]]
    )

    ([a b c d e] = apply:relax append/ [[a b c] spread [d e] [f g]])
    ([a b c d e] = apply:relax append/ [:value spread [d e] [a b c] [f g]])
]

; You can use commas so long as they are at interstitial positions
[
    ([a b c d e d e] = apply append/ [[a b c], spread [d e], :dup 2])
    ([a b c d e d e] = apply append/ [:dup 2, [a b c] spread [d e]])

    (all [
        e: sys.util/rescue [
            [a b c d e d e] = apply append/ [:dup, 2 [a b c] spread [d e]]
        ]
        e.arg1 = 'need-non-end
        e.arg2 = '/dup

    ])
]

; If you specify a refinement, there has to be a value after it
[
    (all [
        e: sys.util/rescue [
            [a b c d e d e] = apply append/ [
                :dup :part 1 [a b c] spread [d e]
            ]
        ]
        e.arg1 = 'need-non-end
        e.arg2 = ':dup
    ])
    (all [
        e: sys.util/rescue [
            [a b c d e d e] = apply append/ [[a b c] spread [d e] :dup]
        ]
        e.arg1 = 'need-non-end
        e.arg2 = ':dup
    ])
]

; ^META functions will receive the meta form of the argument, antiforms will
; be converted to quasiforms and other values quoted.  This is a service given
; by APPLY, because the fundamental frame mechanics do not intervene.
[
    (
        non-detector: lambda [arg] [arg]
        'a = apply non-detector/ [~['a 'b]~]
    )
    (
        detector: lambda [^arg] [arg]
        '~['a 'b]~ = apply detector/ [~['a 'b]~]
    )
]

; Refinements that take no argument can only be OKAY or NULL
[
    (
        testme: func [/refine] [return refine]
        ok
    )

    (ok = apply testme/ [:refine ok])
    (null = apply testme/ [:refine null])
    ~expect-arg~ !! (apply testme/ [:refine 'true])
    ~expect-arg~ !! (apply testme/ [:refine 'false])

    (all [
        e: sys.util/rescue [apply testme/ [:refine #garbage]]
        e.id = 'bad-argless-refine
        e.arg1 = ':refine
    ])
]

; Argument fulfillment needs to handle throws.
[
    (true? catch [apply append/ [[a b c] throw 'true]])
    (true? catch [apply append/ [[a b c] [d e f] :dup throw 'true]])
]


; APPLIQUE is a form of APPLY that blends together MAKE FRAME!, binding code
; into that frame, running the code, and then running the frame.
;
; Theoretically it could also take care of META parameterization, it does
; not currently.
(
    s: applique append/ [
        series: copy [a b c]
        value: spread [d e]
        dup: 2
    ]
    s = [a b c d e d e]
)

; APPLY is called by the evaluator when it sees a // using whatever is on the
; left as the action.
[
    ([a b c [d e] [d e]] = append // [[a b c] [d e] :dup 2])
    ([a b c e f e f] = append // [:dup 2 [a b c] spread [e f]])

    ; !!! More complex forms not implemented yet, just WORD!, but will be
    ;
    ; ([a b c [e f] [e f]] = append:dup // [[a b c] [e f] 2])

    ~???~ !! (// [a b c])  ; must be used as infix operator

    ~expect-arg~ !! (mold // 1)
]
