; %parse-remove.test.reb
;
; Mutating operations in UPARSE raise some large questions; they were removed
; from Topaz entirely.  For the moment they are being considered.

(all [
    '~<remove>~ == meta parse text: "a ^/ " [
        some [newline remove [to <end>] | "a" [remove [to newline]] | <next>]
    ]
    text = "a^/"
])


; BLOCK! remove tests from %parse-test.red
[
    ~???~ !! (parse [] [remove])
    ~parse-mismatch~ !! (parse [] [remove one])
    (
        blk: [a]
        all [
            '~<remove>~ == meta parse blk [remove one]
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
        ok
    )
    ~???~ !! (parse "" [remove])
    ~parse-mismatch~ !! (parse "" [remove one])
    (
        str: "a"
        all [
            '~<remove>~ == meta parse str [remove one]
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
            "world" == parse str [remove "hello" <next> "world"]
            str = " world"
        ]
    )
    (all [
        '~<remove>~ == meta parse s: " t e s t " [some [remove ws | <next>]]
        s = "test"
    ])
    (all [
        '~<remove>~ == meta parse s: " t e s t " [some [remove ws | one]]
        s = "test"
    ])
    (
        str: "hello 123 world"
        digit: charset "0123456789"
        all [
            #d == parse str [some [remove [some digit #" "] | one]]
            str = "hello world"
        ]
    )
]


; BINARY! remove tests from %parse-test.red
[
    (
        ws: charset " ^- ^/^M"
        not-ws: complement ws
        ok
    )
    ~???~ !! (parse #{} [remove])
    ~parse-mismatch~ !! (parse #{} [remove one])
    (
        bin: #{0A}
        all [
            '~<remove>~ == meta parse bin [remove one]
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
        ws: make bitset! [" ^- ^/^M" #""]
        bin: #{DEAD00BEEF}
        all [
            #{BEEF} == parse bin [remove thru ws #{BEEF}]
            bin = #{BEEF}
        ]
    )
    (
        bin: #{DEAD00BEEF}
        all [
            #{BEEF} == parse bin [remove #{DEAD} <next> #{BEEF}]
            bin = #{00BEEF}
        ]
    )
    (all [
        ws: make bitset! [" ^- ^/^M" #""]
        '~<remove>~ == meta parse s: #{00DE00AD00} [some [remove ws | <next>]]
        s = #{DEAD}
    ])
    (all [
        ws: make bitset! [" ^- ^/^M" #""]
        '~<remove>~ == meta parse s: #{00DE00AD00} [some [remove ws | one]]
        s = #{DEAD}
    ])
    (
        bin: #{DEAD0001020300BEEF}
        digit: charset [1 - 9]
        all [
            239 == parse bin [some [remove [some digit NUL] | one]]
            bin = #{DEAD00BEEF}
        ]
    )
]


[https://github.com/red/red/issues/748
    (
        txt: "Hello world"
        #d == parse txt [opt some further some [remove "l" | one]]
        all [
            txt = "Heo word"
            8 = length? txt
        ]
    )
]

[#1251
    (all [
        '~<insert>~ == meta parse e: "a" [remove one insert ("xxx")]
        e = "xxx"
    ])
    (all [
        '~<insert>~ == meta parse e: "a" [[remove one] insert ("xxx")]
        e = "xxx"
    ])
]

[#1244
    (all [
        raised? parse a: "12" [remove v: across one]
        a = "2"
        v = "1"
    ])
    (all [
        raised? parse a: "12" [remove [v: across one]]
        a = "2"
        v = "1"
    ])
]
