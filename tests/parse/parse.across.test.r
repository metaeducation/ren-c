; %parse-across.test.r
;
; ACROSS is UPARSE's version of historical PARSE COPY.
;
; !!! It is likely that this will change back, due to making peace with the
; ambiguity that COPY represents as a term, given all the other ambiguities.
; However, ACROSS is distinct and hence can be mixed in for compatibility
; while hybrid syntax is still around.


(all wrap [
    x: y: ~
    "bbb" = parse "aaabbb" [x: across some "a", y: across [some "b"]]
    x = "aaa"
    y = "bbb"
])

[https://github.com/red/red/issues/1093
   (
        se53-copied: copy ""
        s: ~
        all [
            "abcde" = parse "abcde" [
                "xyz" | s: across to <end> (se53-copied: :s)
            ]
            "abcde" = se53-copied
        ]
    )
    (
        se53-copied: copy #{}
        s: ~
        all [
            #{0102030405} = parse #{0102030405} [
                #{AABBCC} | s: across to <end> (se53-copied: :s)
            ]
            #{0102030405} = se53-copied
        ]
    )
]

; BLOCK! copying tests from %parse-test.red
[
    (
        res: ~
        all [
            [a] = parse [a] [res: across one]
            res = [a]
        ]
    )
    (
        res: ~
        all [
            [a] = parse [a] [res: across 'a]
            res = [a]
        ]
    )
    (
        res: ~
        all [
            [a] = parse [a] [res: across word!]
            res = [a]
        ]
    )
    (
        res: ~
        res2: ~
        all [
            [a] = parse [a] [res: across res2: across 'a]
            res = [a]
            res2 = [a]
        ]
    )
    (
        res: ~
        all [
            [a a] = parse [a a] [res: across repeat 2 'a]
            res = [a a]
        ]
    )
    (
        res: '~before~
        all [
            error? parse [a a] [res: across repeat 3 'a]
            res = '~before~
        ]
    )
    (
        res: ~
        all [
            [a] = parse [a] [res: across ['a]]
            res = [a]
        ]
    )
    (
        res: ~
        all [
            'b = parse [a a b] [<next> res: across 'a one]
            res = [a]
        ]
    )
    (
        res: ~
        all [
            'b = parse [a a b] [<next> res: across ['a | 'b] one]
            res = [a]
        ]
    )
    (
        res: '~before~
        all [
            error? parse [a] [res: across ['c | 'b]]
            res = '~before~
        ]
    )
    (
        wa: ['a]
        res: ~
        all [
            [a] = parse [a] [res: across wa]
            res = [a]
        ]
    )
    (
        wa: ['a]
        res: ~
        all [
            [a a] = parse [a a] [res: across repeat 2 wa]
            res = [a a]
        ]
    )
]


; ACROSS tests with :PART from %parse-test.red
; !!! At time of writing the :PART feature in UPARSE is fake
[
    (
        input: [h 5 #l "l" o]
        input2: [a a a b b]
        ok
    )
    (
        v: '~before~
        all [
            error? parse:part input [v: across repeat 3 one] 2
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            [h 5 #l] = parse:part input [v: across repeat 3 one] 3
            v = [h 5 #l]
        ]
    )
    (
        v: ~
        all [
            error? parse:part input [v: across repeat 3 one] 4
            v = [h 5 #l]
        ]
    )
    (
        v: ~
        all [
            "l" = parse:part input [v: across repeat 3 one one] 4
            v = [h 5 #l]
        ]
    )
    (
        v: ~
        all [
            [5 #l "l"] = parse:part next input [v: across repeat 3 one] 3
            v = [5 #l "l"]
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input [v: across to 'o one] 3
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            'o = parse:part input [v: across to 'o one] 5
            v = [h 5 #l "l"]
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input2 [v: across repeat 3 'a] 2
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            [a a a] = parse:part input2 [v: across repeat 3 'a] 3
            v = [a a a]
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input [v: across repeat 3 one] skip input 2
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            [h 5 #l] = parse:part input [v: across repeat 3 one] skip input 3
            v = [h 5 #l]
        ]
    )
    (
        v: ~
        all [
            error? parse:part input [v: across repeat 3 one] skip input 4
            v = [h 5 #l]
        ]
    )
    (
        v: ~
        all [
            "l" = parse:part input [
                v: across repeat 3 one one
            ] skip input 4
            v = [h 5 #l]
        ]
    )
    (
        v: ~
        all [
            [5 #l "l"] = parse:part next input [
                v: across repeat 3 one
            ] skip input 4
            v = [5 #l "l"]
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input [v: across to 'o one] skip input 3
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            'o = parse:part input [v: across to 'o one] skip input 5
            v = [h 5 #l "l"]
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input2 [v: across repeat 3 'a] skip input2 2
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            [a a a] = parse:part input2 [v: across repeat 3 'a] skip input2 3
            v = [a a a]
        ]
    )
]


; TEXT! copying tests from %parse-test.red
[
    (
        res: ~
        all [
            "a" = parse "a" [res: across one]
            res = "a"
        ]
    )
    (
        res: ~
        all [
            "a" = parse "a" [res: across #a]
            res = "a"
        ]
    )
    (
        res: ~
        res2: ~
        all [
            "a" = parse "a" [res: across res2: across #a]
            res = "a"
            res2 = "a"
        ]
    )
    (
        res: ~
        all [
            "aa" = parse "aa" [res: across repeat 2 #a]
            res = "aa"
        ]
    )
    (
        res: '~before~
        all [
            error? parse "aa" [res: across repeat 3 #a]
            res = '~before~
        ]
    )
    (
        res: ~
        all [
            "a" = parse "a" [res: across [#a]]
            res = "a"
        ]
    )
    (
        wa: [#a]
        res: ~
        all [
            "a" = parse "a" [res: across wa]
            res = "a"
        ]
    )
    (
        wa: [#a]
        res: ~
        all [
            "aa" = parse "aa" [res: across repeat 2 wa]
            res = "aa"
        ]
    )
    (
        res: ~
        all [
            #b = parse "aab" [<next> res: across #a one]
            res = "a"
        ]
    )
    (
        res: ~
        all [
            #b = parse "aab" [<next> res: across [#a | #b] one]
            res = "a"
        ]
    )
    (
        res: '~before~
        all [
            error? parse "a" [res: across [#c | #b]]
            res = '~before~
        ]
    )
]

; Testing ACROSS with :PART on Strings from %parse-test.red
; !!! At time of writing, the :PART feature in UPARSE is faked.
[
    (
        input: "hello"
        input2: "aaabb"
        letters: charset [#a - #o]
        ok
    )
    (
        v: '~before~
        all [
            error? parse:part input [v: across repeat 3 one] 2
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            "hel" = parse:part input [v: across repeat 3 one] 3
            v = "hel"
        ]
    )
    (
        v: ~
        all [
            error? parse:part input [v: across repeat 3 one] 4
            v = "hel"
        ]
    )
    (
        v: ~
        all [
            #l = parse:part input [v: across repeat 3 one one] 4
            v = "hel"
        ]
    )
    (
        v: ~
        all [
            "ell" = parse:part next input [v: across repeat 3 one] 3
            v = "ell"
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input [v: across to #o one] 3
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            #o = parse:part input [v: across to #o one] 5
            v = "hell"
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input [v: across repeat 3 letters] 2
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            "hel" = parse:part input [v: across repeat 3 letters] 3
            v = "hel"
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input2 [v: across repeat 3 #a] 2
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            "aaa" = parse:part input2 [v: across repeat 3 #a] 3
            v = "aaa"
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input [v: across repeat 3 one] skip input 2
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            "hel" = parse:part input [v: across repeat 3 one] skip input 3
            v = "hel"
        ]
    )
    (
        v: ~
        all [
            error? parse:part input [v: across repeat 3 one] skip input 4
            v = "hel"
        ]
    )
    (
        v: ~
        all [
            #l = parse:part input [v: across skip 3, one] skip input 4
            v = "hel"
        ]
    )
    (
        v: ~
        all [
            "ell" = parse:part next input [v: across skip 3] skip input 4
            v = "ell"
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input [v: across to #o one] skip input 3
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            #o = parse:part input [v: across to #o one] skip input 5
            v = "hell"
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input [v: across repeat 3 letters] skip input 2
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            "hel" = parse:part input [v: across repeat 3 letters] skip input 3
            v = "hel"
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input2 [v: across repeat 3 #a] skip input2 2
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            "aaa" = parse:part input2 [v: across repeat 3 #a] skip input2 3
            v = "aaa"
        ]
    )
]


; BLOB! copying tests from %parse-test.red
[
    (
        res: ~
        all [
            #{0A} = parse #{0A} [res: across one]
            res = #{0A}
        ]
    )
    (
        res: ~
        all [
            #{0A} = parse #{0A} [res: across #{0A}]
            res = #{0A}
        ]
    )
    (
        res: ~
        res2: ~
        all [
            #{0A} = parse #{0A} [res: across res2: across #{0A}]
            res = #{0A}
            res2 = #{0A}
        ]
    )
    (
        res: ~
        all [
            #{0A0A} = parse #{0A0A} [res: across repeat 2 #{0A}]
            res = #{0A0A}
        ]
    )
    (
        res: '~before~
        all [
            error? parse #{0A0A} [res: across repeat 3 #{0A}]
            res = '~before~
        ]
    )
    (
        res: ~
        all [
            #{0A} = parse #{0A} [res: across [#{0A}]]
            res = #{0A}
        ]
    )
    (
        res: ~
        all [
            11 = parse #{0A0A0B} [<next> res: across #{0A} one]
            res = #{0A}
        ]
    )
    (
        res: ~
        all [
            11 = parse #{0A0A0B} [<next> res: across [#{0A} | #{0B}] one]
            res = #{0A}
        ]
    )
    (
        res: '~before~
        all [
            error? parse #{0A} [res: across [#"^L" | #{0B}]]
            res = '~before~
        ]
    )
    (
        wa: [#{0A}]
        res: ~
        all [
            #{0A} = parse #{0A} [res: across wa]
            res = #{0A}
        ]
    )
    (
        wa: [#{0A}]
        res: ~
        all [
            #{0A0A} = parse #{0A0A} [res: across repeat 2 wa]
            res = #{0A0A}
        ]
    )
]

; Testing ACROSS with :PART on BLOB! from %parse-test.red
; !!! At time of writing, the :PART feature in UPARSE is fake
[
    (
        input: #{DEADBEEF}
        input2: #{0A0A0A0B0B}
        letters: charset [#­ - #Þ]
        ok
    )
    (
        v: '~before~
        all [
            error? parse:part input [v: across repeat 3 one] 2
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            #{DEADBE} = parse:part input [v: across skip 3] 3
            v = #{DEADBE}
        ]
    )
    (
        v: ~
        all [
            error? parse:part input [v: across repeat 3 one] 4
            v = #{DEADBE}
        ]
    )
    (
        v: ~
        all [
            239 = parse:part input [v: across skip 3, one] 4
            v = #{DEADBE}
        ]
    )
    (
        v: ~
        all [
            #{ADBEEF} = parse:part next input [v: across skip 3] 3
            v = #{ADBEEF}
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input [v: across to #o one] 3
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            239 = parse:part input [v: across to #{EF} one] 5
            v = #{DEADBE}
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input [v: across repeat 3 letters] 2
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            #{DEADBE} = parse:part input [v: across repeat 3 letters] 3
            v = #{DEADBE}
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input2 [v: across repeat 3 #{0A}] 2
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            #{0A0A0A} = parse:part input2 [v: across repeat 3 #{0A}] 3
            v = #{0A0A0A}
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input [v: across skip 3] skip input 2
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            #{DEADBE} = parse:part input [v: across skip 3] skip input 3
            v = #{DEADBE}
        ]
    )
    (
        v: ~
        all [
            error? parse:part input [v: across repeat 3 one] skip input 4
            v = #{DEADBE}
        ]
    )
    (
        v: ~
        all [
            239 = parse:part input [v: across skip 3, one] skip input 4
            v = #{DEADBE}
        ]
    )
    (
        v: ~
        all [
            #{ADBEEF} = parse:part next input [v: across skip 3] skip input 4
            v = #{ADBEEF}
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input [v: across to #o one] skip input 3
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            239 = parse:part input [v: across to #{EF} one] skip input 5
            v = #{DEADBE}
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input [v: across repeat 3 letters] skip input 2
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            #{DEADBE} = parse:part input [
                v: across repeat 3 letters
            ] skip input 3
            v = #{DEADBE}
        ]
    )
    (
        v: '~before~
        all [
            error? parse:part input2 [v: across repeat 3 #{0A}] skip input2 2
            v = '~before~
        ]
    )
    (
        v: ~
        all [
            #{0A0A0A} = parse:part input2 [
                v: across repeat 3 #{0A}
            ] skip input2 3
            v = #{0A0A0A}
        ]
    )
]

; Parsing URL!s and ANY-SEQUENCE? is read-only
[(
    all [
        let name
        "example" = parse http://example.com [
            "http:" some "/" name: between <here> ".com"
        ]
        name = "example"
    ]
)(
    all [
        let tags
        'jkl = parse 'abc.<def>.<ghi>.jkl [word! tags: across some tag! word!]
        tags = [<def> <ghi>]
    ]
)]
