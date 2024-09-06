; %for-parallel.loop.test.reb
;
; Function requested by @gchiu, serves as another test of loop composition.

[
    (for-parallel: func [
        return: [any-atom?]
        vars [block!]
        blk1 [~void~ any-list?]
        blk2 [~void~ any-list?]
        body [block!]
    ][
        return while [(not empty? maybe blk1) or (not empty? maybe blk2)] [
            (vars): pack [(try first maybe blk1) (try first maybe blk2)]

            repeat 1 body else [  ; if pure NULL it was a BREAK
                return null
            ]

            ; They either did a CONTINUE the REPEAT caught, or the body reached
            ; the end.  ELIDE the increment, so body evaluation is result.
            ;
            elide blk1: next maybe blk1
            elide blk2: next maybe blk2
        ]
    ], ok)

    (void = for-parallel [x y] [] [] [fail])
    ([1 2] = collect [for-parallel [x y] [] [1 2] [keep maybe x, keep y]])
    ([a b] = collect [for-parallel [x y] [a b] [] [keep x, keep maybe y]])

    (void = for-parallel [x y] void void [fail])
    ([1 2] = collect [for-parallel [x y] void [1 2] [keep maybe x, keep y]])
    ([a b] = collect [for-parallel [x y] [a b] void [keep x, keep maybe y]])

    (null' = ^ for-parallel [x y] [a b] [1 2] [if x = 'b [break]])
    ('~[~null~]~ = ^ for-parallel [x y] [a b] [1 2] [null])

    ('z = for-parallel [x y] [a b] [1 2] [if x = 'b [continue/with 'z]])
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
