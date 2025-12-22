; %ugly-parse.parse.test.r
;
; Usermode implementation a maligned proposal to always splice BLOCK!
; evaluations in parse as retriggered rules, unless a keyword was
; used to suppress them.
;
; Overall a turkey of an idea...but interesting to demo as something you
; could do by tweaking UPARSE if you wanted it.
;
; https://forum.rebol.info/t/1374/12

[(
    ugly-combinators: copy default-combinators

    ugly-combinators.(group!): combinator [
        return: [any-stable? pack!]
        input [any-series?]
        :pending [hole? block!]   ; we retrigger combinator; it may KEEP, etc.
        value [group?]
        {r comb}
    ][
        ^r: eval value except e -> [panic e]  ; can't `return fail` for this

        if ghostly? ^r [  ; like [inline (comment "hi")]
            return ()
        ]

        if antiform? ^r [
            panic "Misguided GROUP! cannot make non-ghostly antiforms"
        ]

        pending: hole

        r: ^r  ; only needed as ^META to check for ghosts

        if word? r [
            r: reduce [r]  ; enable 0-arity combinators
        ]

        if not comb: select state.combinators type of r [
            panic ["Unhandled type in GROUP! combinator:" to word! type of r]
        ]

        return [{_} input pending]: run comb state input r
    ]

    ; DISCARD is different from ELIDE when GROUP! acts like INLINE because we
    ; want to suppress the triggering of the generated rule.
    ;
    ugly-combinators.discard: vanishable combinator [
        return: [void!]
        input [any-series?]
        @group [group!]
    ][
        eval group
        return ()
    ]

    ugly-parse: specialize parse/ [combinators: ugly-combinators]

    ok
)

(
    "b" = ugly-parse "aaabbb" [
        (if ok '[some "a"]), some "b", discard (if ok '[some "c"])
    ]
)
]
