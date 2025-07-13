; %parse-binary.test.r
;
; This is generally intended for when the parse rule is a BLOB!, but other
; misc tests related to binary input can go here too.

; General BLOB! matching
[
    (void? parse #{} [])

    (#{0A} = parse #{0A} [#{0A}])
    (#"^/" = parse #{0A} [#"^/"])

    ~parse-mismatch~ !! (parse #{0A} [#{0B}])

    (#{0B} = parse #{0A0B} [#{0A} #{0B}])
    (#{0A0B} = parse #{0A0B} [#{0A0B}])
    (#{0A} = parse #{0A} [[#{0A}]])
    (#{0B} = parse #{0A0B} [[#{0A}] #{0B}])
    (#{0B} = parse #{0A0B} [#{0A} [#{0B}]])
    (#{0B} = parse #{0A0B} [[#{0A}] [#{0B}]])
    (#{0A} = parse #{0A} [#{0B} | #{0A}])

    ~parse-incomplete~ !! (parse #{0A0B} [#{0B} | #{0A}])

    (#{0A} = parse #{0A} [[#{0B} | #{0A}]])

    ~parse-incomplete~ !! (parse #{0A0B} [[#{0B} | #{0A}]])

    (#{0B} = parse #{0A0B} [[#{0A} | #{0B}] [#{0B} | #{0A}]])
    (
        res: 0
        all [
            1 = parse #{} [(res: 1)]
            res = 1
        ]
    )
    (
        res: 0
        all [
            1 = parse #{0A} [#{0A} (res: 1)]
            res = 1
        ]
    )
    (
        res: 0
        all [
            error? parse #{0A} [#{0B} (res: 1)]
            res = 0
        ]
    )
    (
        res: 0
        all [
            1 = parse #{} [[(res: 1)]]
            res = 1
        ]
    )
    (
        res: 0
        all [
            1 = parse #{0A} [[#{0A} (res: 1)]]
            res = 1
        ]
    )
    (
        res: 0
        all [
            error? parse #{0A} [[#{0B} (res: 1)]]
            res = 0
        ]
    )
    (
        res: 0
        all [
            3 = parse #{0A0B} [
                #{0A} (res: 1) [#"^L" (res: 2) | #{0B} (res: 3)]
            ]
            res = 3
        ]
    )
    (
        res: 0
        all [
            error? parse #{0A0B} [#{0A} (res: 1) [#{0C} (res: 2) | #{0D} (res: 3)]]
            res = 1
        ]
    )

    ~parse-incomplete~ !! (parse #{0A0A} [repeat 1 [#{0A}]])

    (#{0A} = parse #{0A0A} [repeat 2 [#{0A}]])

    ~parse-mismatch~ !! (parse #{0A0A} [repeat 3 [#{0A}]])
    ~parse-incomplete~ !! (parse #{0A0A} [repeat ([1 1]) [#{0A}]])

    (#{0A} = parse #{0A0A} [repeat ([1 2]) [#{0A}]])
    (#{0A} = parse #{0A0A} [repeat ([2 2]) [#{0A}]])
    (#{0A} = parse #{0A0A} [repeat ([2 3]) [#{0A}]])

    ~parse-mismatch~ !! (parse #{0A0A} [repeat ([3 4]) [#{0A}]])
    ~parse-incomplete~ !! (parse #{0A0A} [repeat ([1 1]) #{0A}])

    (#{0A} = parse #{0A0A} [repeat ([1 2]) #{0A}])
    (#{0A} = parse #{0A0A} [repeat ([2 2]) #{0A}])
    (#{0A} = parse #{0A0A} [repeat ([2 3]) #{0A}])

    ~parse-mismatch~ !! (parse #{0A0A} [repeat ([3 4]) #{0A}])
    ~parse-incomplete~ !! (parse #{0A0A} [repeat ([1 1]) one])

    (10 = parse #{0A0A} [repeat ([1 2]) one])
    (10 = parse #{0A0A} [repeat ([2 2]) one])
    (10 = parse #{0A0A} [repeat ([2 3]) one])

    ~parse-mismatch~ !! (parse #{0A0A} [repeat ([3 4]) one])
    ~parse-incomplete~ !! (parse #{0A0A} [repeat 1 #{0A}])

    (#{0A} = parse #{0A0A} [repeat 2 #{0A}])

    ~parse-mismatch~ !! (parse #{0A0A} [repeat 3 #{0A}])
    ~parse-incomplete~ !! (parse #{0A0A} [repeat 1 one])

    (10 = parse #{0A0A} [repeat 2 one])

    ~parse-mismatch~ !! (parse #{0A0A} [repeat 3 one])

    (10 = parse #{0A} [one])
    (11 = parse #{0A0B} [one one])
    (11 = parse #{0A0B} [one [one]])
    (11 = parse #{0A0B} [[one] [one]])
    (#{0A} = parse #{0A0A} [some [#{0A}]])

    ~parse-mismatch~ !! (parse #{0A0A} [some [#{0A}] #{0B}])

    (10 = parse #{0A0A0B0A0B0B0B0A} [some [one]])
    (#{0A} = parse #{0A0A0B0A0B0B0B0A} [some [#{0A} | #{0B}]])

    ~parse-incomplete~ !! (parse #{0A0A0B0A0B0B0B0A} [some [#{0A} | #"^L"]])

    (#{0A} = parse #{0A0A} [some [#{0A}]])
    (null = parse #{0A0A} [some [#{0A}] try some [#{0B}]])
    (#{0B} = parse #{0A0A0B0B} [repeat 2 #{0A} repeat 2 #{0B}])

    ~parse-mismatch~ !! (parse #{0A0A0B0B} [repeat 2 #{0A} repeat 3 #{0B}])

    (#{0B} = parse #{0A0A0B0B} [some #{0A} some #{0B}])

    ~parse-mismatch~ !! (parse #{0A0A0B0B} [some #{0A} some #"^L"])

    (#"^L" = parse #{0B0A0A0A0C} [<next> some [#{0A}] #"^L"])
]


[#394 (
    #{22} = parse #{001122} [#{00} #{11} #{22}]
)]

; "BLOB! extraction"
[
    (
        res: ~
        all [
            10 = parse #{0A} [res: one]
            res = 10
        ]
    )
    (
        res: ~
        all [
            #{0A} = parse #{0A} [res: #{0A}]
            res = #{0A}
        ]
    )
    (
        res: ~
        res2: ~
        all [
            #{0A} = parse #{0A} [res: res2: #{0A}]
            res = #{0A}
            res2 = #{0A}
        ]
    )
    (
        res: ~
        all [
            #{0A} = parse #{0A0A} [res: repeat 2 #{0A}]
            res = #{0A}
        ]
    )
    (
        res: '~before~
        all [
            error? parse #{0A0A} [res: repeat 3 #{0A}]
            res = '~before~
        ]
    )
    (
        res: ~
        all [
            #{0A} = parse #{0A} [res: [#{0A}]]
            res = #{0A}
        ]
    )
    (
        wa: [#{0A}]
        res: ~
        all [
            #{0A} = parse #{0A} [res: wa]
            res = #{0A}
        ]
    )
    (
        wa: [#{0A}]
        res: ~
        all [
            #{0A} = parse #{0A0A} [res: repeat 2 wa]
            res = #{0A}
        ]
    )
    (
        wa: [#{0A}]
        res: ~
        all [
            11 = parse #{0A0A0B} [<next> res: #{0A} one]
            res = #{0A}
        ]
    )
    (
        res: ~
        all [
            11 = parse #{0A0A0B} [<next> res: [#{0A} | #{0B}] one]
            res = #{0A}
        ]
    )
    (
        res: '~before~
        all [
            error? parse #{0A} [res: [#"^L" | #{0B}]]
            res = '~before~
        ]
    )
    (
        res: ~
        all [
            #"^L" = parse #{0B0A0A0A0C} [<next> res: some #{0A} #"^L"]
            res = #{0A}
        ]
    )
    (
        wa: [#{0A}]
        res: ~
        all [
            #"^L" = parse #{0B0A0A0A0C} [<next> res: some wa #"^L"]
            res = #{0A}
        ]
    )
]
