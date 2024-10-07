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

    ugly-combinators.(group!): combinator [
        return: [any-value? pack?]
        :pending [blank! block!]   ; we retrigger combinator; it may KEEP, etc.

        value [group?]
        <local> r comb
    ][
        r: meta eval:undecayed value except e -> [fail e]  ; can't raise

        if r = ^null [  ; like [:(1 = 0)]
            return raise "GET-GROUP! evaluated to NULL"  ; means no match
        ]

        pending: _
        remainder: input

        any [
            r = ^okay  ; like [:(1 = 1)]
            r = '~[]~  ; like [:(comment "hi")]
        ] then [
            return ~[]~  ; invisible
        ]

        if r = ^void [  ; like [:(if 1 = 0 [...])]
            return void  ; couldn't produce void at all if vaporized
        ]

        r: unmeta r  ; only needed as ^META to check for NIHIL

        if word? r [
            r: :[r]  ; enable 0-arity combinators
        ]

        if not comb: select state.combinators type of r [
            fail ["Unhandled type in GROUP! combinator:" mold type of r]
        ]

        return [@ remainder pending]: run comb state input r
    ]

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

    /ugly-parse: specialize parse/ [combinators: ugly-combinators]

    ok
)

(
    "b" = ugly-parse "aaabbb" [
        (if ok '[some "a"]), some "b", discard (if ok '[some "c"])
    ]
)
]
