; %parse-insert.test.reb
;
; Mutating operations are not necessarily high priority for UPARSE (and
; were removed from Topaz entirely) but they are being given a shot.

[https://github.com/metaeducation/ren-c/issues/1032 (
    s: {abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ}
    t: {----------------------------------------------------}
    cfor n 2 50 1 [
        sub: copy/part s n
        uparse sub [some [
            remove <any>
            insert ("-")
        ]]
        if sub != copy/part t n [fail "Incorrect Replacement"]
    ]
    true
)]

[https://github.com/red/red/issues/3357
    (did all [
        '~inserted~ == meta uparse x3357: [] [insert ^('foo)]
        x3357 = [foo]
    ])
    (did all [
        '~inserted~ == meta uparse x3357b: [] [insert ^(the foo)]
        x3357b = [foo]
    ])
]

; Block insertion tests from %parse-test.red
[
    (did all [
        '~inserted~ == meta uparse blk: [] [insert (1)]
        blk = [1]
    ])
    (did all [
        'a == uparse blk: [a a] [<any> insert ^(the b) <any>]
        blk = [a b a]
    ])
    (did all [
        '~removed~ == meta uparse blk: [] [
            p: <here> insert ^(the a) seek (p) remove 'a
        ]
        blk = []
    ])
    (did all [
        '~inserted~ == meta uparse blk: [] [insert ([a b])]
        blk = [a b]
    ])
    (did all [
        '~inserted~ == meta uparse blk: [] [insert ^([a b])]
        blk = [[a b]]
    ])
    (
        series: [a b c]
        letter: 'x
        did all [
            'c == uparse series [insert ^(letter) 'a 'b 'c]
            series == [x a b c]
        ]
    )
    (
        series: [a b c]
        letters: [x y z]
        did all [
            'c == uparse series ['a 'b insert (letters) insert ^(letters) 'c]
            series == [a b x y z [x y z] c]
        ]
    )
    (
        series: [b]
        digit: 2
        did all [
            'b == uparse series [insert (digit) 'b]
            series == [2 b]
        ]
    )
]


; TEXT! insertion tests from %parse-test.red
[
    (did all [
        '~inserted~ == meta uparse str: "" [insert (#1)]
        str = "1"
    ])
    (did all [
        #a == uparse str: "aa" [<any> insert (#b) <any>]
        str = "aba"
    ])
    (did all [
        '~removed~ == meta uparse str: "" [
            p: <here> insert (#a) seek (p) remove #a
        ]
        str = ""
    ])
    (did all [
        '~removed~ == meta uparse str: "test" [
            some [<any> p: <here> insert (#_)] seek (p) remove <any>
        ]
        str = "t_e_s_t"
    ])
]

; BINARY! insertion tests from %parse-test.red
[
   (did all [
        '~inserted~ == meta uparse bin: #{} [insert (#"^A")]
        bin = #{01}
    ])
    (did all [
        10 == uparse bin: #{0A0A} [<any> insert (#{0B}) <any>]
        bin = #{0A0B0A}
    ])
    (did all [
        '~removed~ == meta uparse bin: #{} [
            p: <here> insert (#{0A}) seek (p) remove #{0A}
        ]
        bin = #{}
    ])
    (did all [
        '~removed~ == meta uparse bin: #{DEADBEEF} [
            some [<any> p: <here> insert (#)] seek (p) remove <any>
        ]
        bin = #{DE00AD00BE00EF}
    ])
]

; !!! Red has an arity-2 form of INSERT which allows you to specify a series
; position in the series you are processing as the first argument.  That
; becomes where to insert...and it will adjust your parse position accordingly.
; This is a complex feature that points to the concerns of trying to hold
; onto positions when mutability is in play, and should be thought about...
; but making INSERT variable arity does not seem the right solution.  These
; tests simulate the heavior of the original tests manually.
[
    (
        series: [a b c]
        letters: [x y z]
        did all [
            'c == uparse series [
                mark: <here>
                'a

                ; Try equivalent of Red's `insert mark letters`
                pos: <here>
                seek (mark)
                insert (letters)
                after: <here>
                seek (skip pos (index? after) - (index? mark))

                ; Try equivalent of Red's `insert only mark letters`
                pos: <here>
                seek (mark)
                insert ^(letters)
                after: <here>
                seek (skip pos (index? after) - (index? mark))

                'b 'c
            ]
            series == [[x y z] x y z a b c]
        ]
    )
    (
        series: [a b c]
        letter: 'x
        did all [
            'c == uparse series [
                mark: <here> insert ^(letter) 'a 'b

                ; Try equivalent of Red's `insert only mark letter`
                pos: <here>
                seek (mark)
                insert ^(letter)
                after: <here>
                seek (skip pos (index? after) - (index? mark))

                'c
            ]
            series == [x x a b c]
        ]
    )
    (
        series: [a b c]
        letters: [x y z]
        did all [
            [x y z] == uparse series [
                to <end> mark: <here> [false]
                |
                ; Try equivalent of Red's `insert only mark letters`
                pos: <here>
                seek (mark)
                insert ^(letters)
                after: <here>
                ; don't adjust pos, it is before insert point
                seek (pos)

                ; Try equivalent of Red's `insert mark letters`
                pos: <here>
                seek (mark)
                insert (letters)
                after: <here>
                ; don't adjust pos, it is before insert point
                seek (pos)

                'a 'b 'c 'x 'y 'z block!
            ]
            series == [a b c x y z [x y z]]
        ]
    )
]
