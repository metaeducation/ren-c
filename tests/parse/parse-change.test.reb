; %parse-change.test.reb
;
; CHANGE is rethought in UPARSE to work with value-bearing rules.  The rule
; gets the same input that the first argument did.

(
    str: "aaa"
    did all [
        '~changed~ == meta uparse str [
            change [some "a"] (if true ["literally"])
        ]
        str = "literally"
    ]
)

(
    str: "(aba)"
    did all [
        ")" == uparse str [
            "("
            change [to ")"] [
                collect [
                    some ["a" keep ("A") | <any>]
                ]
            ]
            ")"
        ]
        str = "(AA)"
    ]
)

(
    s: {a}
    did all [
        null = uparse s [opt change "b" ("x")]
        s = {a}
    ]
)

(
    s: {aba}
    did all [
        '~changed~ = ^ uparse s [while [
            opt change "b" ("x")
            skip
        ]]
        s = {axa}
    ]
)


; BLOCK! change tests from %parse-test.red
[
    (did all [
        '~changed~ == meta uparse blk: [1] [change integer! ^(the a)]
        blk = [a]
    ])
    (did all [
        '~changed~ == meta uparse blk: [1 2 3] [change [some integer!] ^(the a)]
        blk = [a]
    ])
    (did all [
        3 == uparse blk: [1 a 2 b 3] [some [change word! (#.) | integer!]]
        blk = [1 #. 2 #. 3]
    ])
    (did all [
        '~changed~ == meta uparse blk: [1 2 3] [change [some integer!] (99)]
        blk = [99]
    ])
    (did all [
        '~changed~ == meta uparse blk: [1 2 3] [change [some integer!] ^([a])]
        blk = [[a]]
    ])
    (did all [
        '~changed~ == meta uparse blk: [1 2 3] [
            change [some integer!] ^(reduce [1 + 2])
        ]
        blk = [[3]]
    ])
    (
        b: ["long long long string" "long long long string" [1]]
        '~changed~ == meta uparse copy "." [change <any> (b)]
    )
]


; TEXT! change tests from %parse-test.red
[
    (did all [
        '~changed~ == meta uparse str: "1" [change <any> (#a)]
        str = "a"
    ])
    (did all [
        '~changed~ == meta uparse str: "123" [change [3 <any>] (#a)]
        str = "a"
    ])
    (
        alpha: charset [#a - #z]
        did all [
            #3 == uparse str: "1a2b3" [
                some [change alpha (#.) | <any>]
            ]
            str = "1.2.3"
        ]
    )
    (did all [
        '~changed~ == meta uparse str: "123" [change 3 <any> (99)]
        str = "99"
    ])
    (did all [
        '~changed~ == meta uparse str: "test" [some [change #t (#o) | <any>]]
        str = "oeso"
    ])
    (did all [
        #4 == uparse str: "12abc34" [
            some [to alpha change [some alpha] ("zzzz")] 2 <any>
        ]
        str = "12zzzz34"
    ])
]


; BINARY! change tests from %parse-test.red
[
    (did all [
        '~changed~ == meta uparse bin: #{01} [change <any> (#{0A})]
        bin = #{0A}
    ])
    (did all [
        '~changed~ == meta uparse bin: #{010203} [change [3 <any>] (#{0A})]
        bin = #{0A}
    ])
    (
        digit: charset [1 - 9]
        did all [
            '~changed~ == meta uparse bin: #{010A020B03} [
                some [change digit (#{00}) | <any>]
            ]
            bin = #{000A000B00}
        ]
    )
    (did all [
        '~changed~ == meta uparse bin: #{010203} [change 3 <any> (99)]
        bin = #{63}
    ])
    (did all [
        239 == uparse bin: #{BEADBEEF} [
            some [change #{BE} (#{DE}) | <any>]
        ]
        bin = #{DEADDEEF}
    ])
    (did all [
        14 == uparse bin: #{0A0B0C03040D0E} [
            some [to digit change [some digit] (#{BEEF})] 2 <any>
        ]
        bin = #{0A0B0CBEEF0D0E}
    ])
]

[#1245
    (did all [
        '~changed~ == meta uparse s: "(1)" [change "(1)" ("()")]
        s = "()"
    ])
]

; https://github.com/metaeducation/rebol-issues/issues/1279
(
    s: ~
    did all [
        '~changed~ == meta uparse s: [1] [change n: integer! (n * 10)]
        s = [10]
    ]
)
