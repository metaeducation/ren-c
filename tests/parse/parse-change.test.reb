; %parse-change.test.reb
;
; CHANGE is rethought in UPARSE to work with value-bearing rules.  The rule
; gets the same input that the first argument did.

(
    str: "aaa"
    all [
        '~change~ == meta parse str [
            change [some "a"] (if true ["literally"])
        ]
        str = "literally"
    ]
)

(
    str: "(aba)"
    all [
        ")" == parse str [
            "("
            change [to ")"] spread collect [
                some ["a" keep ("A") | <any>]
            ]
            ")"
        ]
        str = "(AA)"
    ]
)

(
    s: {a}
    all [
        raised? parse s [try change "b" ("x")]
        s = {a}
    ]
)

(
    s: {aba}
    all [
        '~change~ = ^ parse s [some [
            try change "b" ("x")
            elide <any>
        ]]
        s = {axa}
    ]
)


; BLOCK! change tests from %parse-test.red
[
    (all [
        '~change~ == meta parse blk: [1] [change integer! (the a)]
        blk = [a]
    ])
    (all [
        '~change~ == meta parse blk: [1 2 3] [change [some integer!] (the a)]
        blk = [a]
    ])
    (all [
        3 == parse blk: [1 a 2 b 3] [some [change word! (#.) | integer!]]
        blk = [1 #. 2 #. 3]
    ])
    (all [
        '~change~ == meta parse blk: [1 2 3] [change [some integer!] (99)]
        blk = [99]
    ])
    (all [
        '~change~ == meta parse blk: [1 2 3] [change [some integer!] ([a])]
        blk = [[a]]
    ])
    (all [
        '~change~ == meta parse blk: [1 2 3] [
            change [some integer!] (reduce [1 + 2])
        ]
        blk = [[3]]
    ])
    (
        b: ["long long long string" "long long long string" 1]
        '~change~ == meta parse copy "." [change <any> (spread b)]
    )
]


; TEXT! change tests from %parse-test.red
[
    (all [
        '~change~ == meta parse str: "1" [change <any> (#a)]
        str = "a"
    ])
    (all [
        '~change~ == meta parse str: "123" [change [repeat 3 <any>] (#a)]
        str = "a"
    ])
    (
        alpha: charset [#a - #z]
        all [
            #3 == parse str: "1a2b3" [
                some [change alpha (#.) | <any>]
            ]
            str = "1.2.3"
        ]
    )
    (all [
        '~change~ == meta parse str: "123" [change skip 3 (99)]
        str = "99"
    ])
    (all [
        '~change~ == meta parse str: "test" [some [change #t (#o) | <any>]]
        str = "oeso"
    ])
    (all [
        #4 == parse str: "12abc34" [
            some [to alpha change [some alpha] ("zzzz")] repeat 2 <any>
        ]
        str = "12zzzz34"
    ])
]


; BINARY! change tests from %parse-test.red
[
    (all [
        '~change~ == meta parse bin: #{01} [change <any> (#{0A})]
        bin = #{0A}
    ])
    (all [
        '~change~ == meta parse bin: #{010203} [change [skip 3] (#{0A})]
        bin = #{0A}
    ])
    (
        digit: charset [1 - 9]
        all [
            '~change~ == meta parse bin: #{010A020B03} [
                some [change digit (#{00}) | <any>]
            ]
            bin = #{000A000B00}
        ]
    )
    (all [
        '~change~ == meta parse bin: #{010203} [change skip 3 (99)]
        bin = #{63}
    ])
    (all [
        239 == parse bin: #{BEADBEEF} [
            some [change #{BE} (#{DE}) | <any>]
        ]
        bin = #{DEADDEEF}
    ])
    (all [
        14 == parse bin: #{0A0B0C03040D0E} [
            some [to digit change [some digit] (#{BEEF})] repeat 2 <any>
        ]
        bin = #{0A0B0CBEEF0D0E}
    ])
]

[#1245
    (all [
        '~change~ == meta parse s: "(1)" [change "(1)" ("()")]
        s = "()"
    ])
]

; https://github.com/metaeducation/rebol-issues/issues/1279
(
    s: ~
    all [
        '~change~ == meta parse s: [1] [change n: integer! (n * 10)]
        s = [10]
    ]
)

[
    (
        parse s: ">" [change '> ("greater")]  ; > is WORD!
        s = "greater"
    )
    (
        parse s: "&" [change '& ("ampersand")]  ; & is SIGIL!
        s = "ampersand"
    )
]
