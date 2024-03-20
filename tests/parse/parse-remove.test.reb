; %parse-remove.test.reb
;
; Mutating operations in UPARSE raise some large questions; they were removed
; from Topaz entirely.  For the moment they are being considered.

(all [
    '~remove~ == meta parse text: "a ^/ " [
        some [newline remove [to <end>] | "a" [remove [to newline]] | <any>]
    ]
    text = "a^/"
])


; BLOCK! remove tests from %parse-test.red
[
    ~???~ !! (parse [] [remove])
    ~parse-mismatch~ !! (parse [] [remove <any>])
    (
        blk: [a]
        all [
            '~remove~ == meta parse blk [remove <any>]
            blk = []
        ]
    )
    (
        blk: [a b a]
        all [
            'a == parse blk [some ['a | remove 'b]]
            blk = [a a]
        ]
    )
]

; TEXT! remove tests from %parse-test.red
[
    (
        ws: charset " ^- ^/^M"
        not-ws: complement ws
        true
    )
    ~???~ !! (parse "" [remove])
    ~parse-mismatch~ !! (parse "" [remove <any>])
    (
        str: "a"
        all [
            '~remove~ == meta parse str [remove <any>]
            str = ""
        ]
    )
    (
        str: "aba"
        all [
            #a == parse str [some [#a | remove #b]]
            str = "aa"
        ]
    )
    (
        str: "hello world"
        all [
            "world" == parse str [remove thru ws "world"]
            str = "world"
        ]
    )
    (
        str: "hello world"
        all [
            "world" == parse str [remove "hello" <any> "world"]
            str = " world"
        ]
    )
    (all [
        '~remove~ == meta parse s: " t e s t " [some [remove ws | <any>]]
        s = "test"
    ])
    (all [
        '~remove~ == meta parse s: " t e s t " [some [remove ws | <any>]]
        s = "test"
    ])
    (
        str: "hello 123 world"
        digit: charset "0123456789"
        all [
            #d == parse str [some [remove [some digit #" "] | <any>]]
            str = "hello world"
        ]
    )
]


; BINARY! remove tests from %parse-test.red
[
    (
        ws: charset " ^- ^/^M"
        not-ws: complement ws
        true
    )
    ~???~ !! (parse #{} [remove])
    ~parse-mismatch~ !! (parse #{} [remove <any>])
    (
        bin: #{0A}
        all [
            '~remove~ == meta parse bin [remove <any>]
            bin = #{}
        ]
    )
    (
        bin: #{0A0B0A}
        all [
            #{0A} == parse bin [some [#{0A} | remove #{0B}]]
            bin = #{0A0A}
        ]
    )
    (
        ws: make bitset! [" ^- ^/^M" #]
        bin: #{DEAD00BEEF}
        all [
            #{BEEF} == parse bin [remove thru ws #{BEEF}]
            bin = #{BEEF}
        ]
    )
    (
        bin: #{DEAD00BEEF}
        all [
            #{BEEF} == parse bin [remove #{DEAD} <any> #{BEEF}]
            bin = #{00BEEF}
        ]
    )
    (all [
        ws: make bitset! [" ^- ^/^M" #]
        '~remove~ == meta parse s: #{00DE00AD00} [some [remove ws | <any>]]
        s = #{DEAD}
    ])
    (all [
        ws: make bitset! [" ^- ^/^M" #]
        '~remove~ == meta parse s: #{00DE00AD00} [some [remove ws | <any>]]
        s = #{DEAD}
    ])
    (
        bin: #{DEAD0001020300BEEF}
        digit: charset [1 - 9]
        all [
            239 == parse bin [some [remove [some digit #] | <any>]]
            bin = #{DEAD00BEEF}
        ]
    )
]


[https://github.com/red/red/issues/748
    (
        txt: "Hello world"
        #d == parse txt [opt some further some [remove "l" | <any>]]
        all [
            txt = "Heo word"
            8 = length? txt
        ]
    )
]

[#1251
    (all [
        '~insert~ == meta parse e: "a" [remove <any> insert ("xxx")]
        e = "xxx"
    ])
    (all [
        '~insert~ == meta parse e: "a" [[remove <any>] insert ("xxx")]
        e = "xxx"
    ])
]

[#1244
    (all [
        raised? parse a: "12" [remove v: across <any>]
        a = "2"
        v = "1"
    ])
    (all [
        raised? parse a: "12" [remove [v: across <any>]]
        a = "2"
        v = "1"
    ])
]
