; %for-parallel.loop.test.reb
;
; Function requested by @gchiu, serves as another test of loop composition.

[
    (for-parallel: func [
        return: [any-atom?]
        vars [block!]
        ^blk1 [~[]~ any-list?]
        ^blk2 [~[]~ any-list?]
        body [block!]
        <local> context
    ][
        blk1: any [^blk1 null]  ; turn voids to null or unlift
        blk2: any [^blk2 null]  ; "

        [vars context]: wrap:set compose vars
        body: overbind context body
        return while [(not empty? opt blk1) or (not empty? opt blk2)] [
            (vars): pack [(try first opt blk1) (try first opt blk2)]

            repeat 1 body else [  ; !!! REVIEW: invent ONCE for REPEAT 1
                return null  ; if pure NULL it was a BREAK
            ]

            ; They either did a CONTINUE the REPEAT caught, or the body reached
            ; the end.  ELIDE the increment, so body evaluation is result.
            ;
            elide blk1: next opt blk1
            elide blk2: next opt blk2
        ]
    ], ok)

    (void = for-parallel [x y] [] [] [panic])
    ([1 2] = collect [for-parallel [x y] [] [1 2] [keep opt x, keep y]])
    ([a b] = collect [for-parallel [x y] [a b] [] [keep x, keep opt y]])

    (void = for-parallel [x y] void void [panic])
    ([1 2] = collect [for-parallel [x y] void [1 2] [keep opt x, keep y]])
    ([a b] = collect [for-parallel [x y] [a b] void [keep x, keep opt y]])

    ((lift null) = lift for-parallel [x y] [a b] [1 2] [if x = 'b [break]])
    ('~[~null~]~ = lift for-parallel [x y] [a b] [1 2] [null])

    ('z = for-parallel [x y] [a b] [1 2] [if x = 'b [continue:with 'z]])
    ([a b 2] = collect [
        for-parallel [x y] [a b] [1 2] [keep x if y = '1 [continue] keep y]
    ])

    ([[a 1] [b 2]] = collect [
        assert [
            20 = for-parallel [x y] [a b] [1 2] [
                keep :[x y]
                y * 10
            ]
        ]
    ])

    ([[a 1] [b 2] [c ~null~]] = collect [
        assert [
            <exhausted> = for-parallel [x y] [a b c] [1 2] [
                keep :[x reify y]
                if y [y * 10] else [<exhausted>]
            ]
        ]
    ])

    ([[a 1] [b 2] [~null~ 3]] = collect [
        assert [
            30 = for-parallel [x y] [a b] [1 2 3] [
                keep :[reify x y]
                y * 10
            ]
        ]
    ])
]
