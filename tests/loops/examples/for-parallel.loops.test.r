; %for-parallel.loop.test.r
;
; Function requested by @gchiu, serves as another test of loop composition.

[
    (for-parallel: lambda [
        vars [block!]
        blk1 [none? any-list?]
        blk2 [none? any-list?]
        body [block!]
        {context}
    ][
        [vars context]: wrap:set compose vars
        body: overbind context body
        while [(not empty? blk1) or (not empty? blk2)] [
            (vars): pack [(try first blk1) (try first blk2)]

            attempt body else [
                break  ; if pure NULL it was a BREAK
            ]

            ; They either did a CONTINUE the ATTEMPT caught, or body reached
            ; the end.  ELIDE the increment, so body evaluation is result.
            ;
            elide try blk1: next blk1
            elide try blk2: next blk2
        ]
    ], ok)

    (heavy-void? for-parallel [x y] [] [] [panic])
    ([1 2] = collect [for-parallel [x y] [] [1 2] [keep opt x, keep y]])
    ([a b] = collect [for-parallel [x y] [a b] [] [keep x, keep opt y]])

    (heavy-void? for-parallel [x y] none none [panic])
    ([1 2] = collect [for-parallel [x y] none [1 2] [keep opt x, keep y]])
    ([a b] = collect [for-parallel [x y] [a b] none [keep x, keep opt y]])

    ((lift null) = lift for-parallel [x y] [a b] [1 2] [if x = 'b [break]])
    ('~(~null~)~ = lift for-parallel [x y] [a b] [1 2] [null])

    ('z = for-parallel [x y] [a b] [1 2] [if x = 'b [continue:with 'z]])
    ([a b 2] = collect [
        for-parallel [x y] [a b] [1 2] [keep x if y = '1 [continue] keep y]
    ])

    ([[a 1] [b 2]] = collect [
        assert [
            20 = for-parallel [x y] [a b] [1 2] [
                keep reduce [x y]
                y * 10
            ]
        ]
    ])

    ([[a 1] [b 2] [c ~null~]] = collect [
        assert [
            <exhausted> = for-parallel [x y] [a b c] [1 2] [
                keep reduce [x reify y]
                if y [y * 10] else [<exhausted>]
            ]
        ]
    ])

    ([[a 1] [b 2] [~null~ 3]] = collect [
        assert [
            30 = for-parallel [x y] [a b] [1 2 3] [
                keep reduce [reify x y]
                y * 10
            ]
        ]
    ])
]
