; %for-parallel.loop.test.reb
;
; Function requested by @gchiu, serves as another test of loop composition.

[
    (for-parallel: lambda [
        vars [block!]
        blk1 [<maybe> block!]
        blk2 [<maybe> block!]
        body [block!]
    ][
        while [(not empty? blk1) or (not empty? blk2)] [  ; null and [] EMPTY?
            (vars): pack [(first maybe blk1) (first maybe blk2)]

            do body  ; BREAK from body break the outer while, it returns NULL

            ; Now ELIDE the increment, so body evaluation above is result
            ;
            elide blk1: next maybe blk1
            elide blk2: next maybe blk2
        ]
    ], true)

    ([[a 1] [b 2]] = collect [
        assert [
            20 = for-parallel [x y] [a b] [1 2] [
                keep :[x y]
                y * 10
            ]
        ]
    ])

    ([[a 1] [b 2] [c _]] = collect [
        assert [
            <exhausted> = for-parallel [x y] [a b c] [1 2] [
                keep :[x reify y]
                if y [y * 10] else [<exhausted>]
            ]
        ]
    ])

    ([[a 1] [b 2] [_ 3]] = collect [
        assert [
            30 = for-parallel [x y] [a b] [1 2 3] [
                keep :[reify x y]
                y * 10
            ]
        ]
    ])
]
