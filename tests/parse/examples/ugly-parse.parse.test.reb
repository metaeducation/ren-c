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
    ugly-combinators.(group!): default-combinators.(get-group!)
    ugly-combinators.(get-group!): void

    ugly-parse: specialize parse/ [combinators: ugly-combinators]

    ; DISCARD is different from ELIDE when GROUP! acts like a GET-GROUP!,
    ; because we want to suppress the triggering of the generated rule
    ;
    ugly-combinators.discard: combinator [
        return: "Don't return anything" [~[]~]
        @group [group!]
    ][
        eval group
        remainder: input
        return ~[]~
    ]
    ok
)

(
    "b" = ugly-parse "aaabbb" [
        (if ok '[some "a"]), some "b", discard (if ok '[some "c"])
    ]
)
]
