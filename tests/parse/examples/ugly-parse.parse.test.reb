; %ugly-parse.parse.test.reb
;
; Usermode implementation via tweak of a maligned proposal to always splice
; BLOCK! evaluations in parse as retriggered rules, unless a keyword was
; used to suppress them
;
; Overall a turkey of an idea...but interesting to demo as something you
; could do by tweaking UPARSE if you wanted it.
;
; https://forum.rebol.info/t/1374/12

[(
    ugly-combinators: copy default-combinators
    ugly-combinators.(group!): :default-combinators.(get-group!)
    ugly-combinators.(get-group!): null

    ugly-parse: specialize :uparse [combinators: ugly-combinators]

    ugly-combinators.discard: combinator [
        return: "Don't return anything" [<invisible>]
        'group [group!]
    ][
        do group
        set remainder input
        return
    ]
    true
)

(
    data: "aaabbb"

    did ugly-parse data [(if true '[some "a"]) some "b" discard (if true '[some "c"])]
)
]
