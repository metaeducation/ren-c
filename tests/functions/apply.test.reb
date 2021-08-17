; %apply.test.reb
;
; Ren-C's APPLY is a hybrid of positional and non-positional application.

(-1 = apply :negate [1])
([a b c d e] = apply :append [[a b c] [d e]])

; Refinements can be provided in any order.  Commas can be used interstitially
[
    ([a b c d e d e] = apply :append [[a b c] [d e] /dup 2])
    ([a b c d e d e] = apply :append [/dup 2 [a b c] [d e]])
    ([a b c d e d e] = apply :append [[a b c] /dup 2 [d e]])

    ([a b c d d] = apply :append [/dup 2 [a b c] [d e] /part 1])
    ([a b c d d] = apply :append [[a b c] [d e] /part 1 /dup 2])
]

; Not only can refinements be used by name, any parameter can.
; Once a parameter has been supplied by name, it is no longer considered for
; consuming positionally.
[
    ([a b c d e] = apply :append [/series [a b c] /value [d e]])
    ([a b c d e] = apply :append [/value [d e] /series [a b c]])

    ([a b c d e] = apply :append [/series [a b c] [d e]])
    ([a b c d e] = apply :append [/value [d e] [a b c]])
]

; Giving too many arguments is an error
[
    (did all [
        e: trap [apply :append [[a b c] [d e] [f g]]]
        e.id = 'apply-too-many
    ])
    (did all [
        e: trap [apply :append [/value [d e] [a b c] [f g]]]
        e.id = 'apply-too-many
    ])
]

; You can use commas so long as they are at interstitial positions
[
    ([a b c d e d e] = apply :append [[a b c], [d e], /dup 2])
    ([a b c d e d e] = apply :append [/dup 2, [a b c] [d e]])

    (did all [
        e: trap [
            [a b c d e d e] = apply :append [/dup, 2 [a b c] [d e]]
            e.arg1 = 'need-non-end
            e.arg2 = '/dup
        ]
    ])
]

; If you specify a refinement, there has to be a value after it
[
    (did all [
        e: trap [
            [a b c d e d e] = apply :append [/dup /part 1 [a b c] [d e]]
            e.arg1 = 'need-non-end
            e.arg2 = '/dup
        ]
    ])
    (did all [
        e: trap [
            [a b c d e d e] = apply :append [[a b c] [d e] /dup]
            e.arg1 = 'need-non-end
            e.arg2 = '/dup
        ]
    ])
]

; ^META functions will receive the meta form of the argument, so isotopes will
; be converted to BAD-WORD! and other values quoted.  This is a service given
; by APPLY, because the fundamental frame mechanics do not intervene.
[
    (
        non-detector: func [arg] [arg]
        did all [
            e: trap [apply :non-detector [~baddie~]]
            e.id = 'isotope-arg
            e.arg1 = 'non-detector
            e.arg2 = 'arg
            e.arg3 = '~baddie~
        ]
    )
    (
        detector: func [^arg] [arg]
        '~baddie~ = apply :detector [~baddie~]
    )
]

; Refinements that take no argument are allowed to be not just # or NULL (which
; is what the core frame mechanics demand), but also LOGIC!.  APPLY will
; convert true to # and false to NULL.
[
    (
        testme: func [/refine] [refine]
        true
    )

    (# = apply :testme [/refine #])
    (null = apply :testme [/refine null])
    (# = apply :testme [/refine true])
    (null = apply :testme [/refine false])

    (did all [
        e: trap [apply :testme [/refine #garbage]]
        e.id = 'bad-argless-refine
        e.arg1 = '/refine
    ])
]

; Argument fulfillment needs to handle throws.
[
    (catch [apply :append [[a b c] throw true]])
    (catch [apply :append [[a b c] [d e f] /dup throw true]])
]


; APPLIQUE is a form of APPLY that blends together MAKE FRAME!, binding code
; into that frame, running the code, and then running the frame.
;
; Theoretically it could also take care of META parameterization, it does
; not currently.
;
(
    s: applique :append [
        series: [a b c]
        value: [d e]
        dup: 2
    ]
    s = [a b c d e d e]
)
