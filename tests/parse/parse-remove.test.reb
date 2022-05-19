; %parse-remove.test.reb
;
; Mutating operations in UPARSE raise some large questions; they were removed
; from Topaz entirely.  For the moment they are being considered.

(did all [
    '~removed~ == meta uparse text: "a ^/ " [
        some [newline remove [to <end>] | "a" [remove [to newline]] | skip]
    ]
    text = "a^/"
])


; BLOCK! remove tests from %parse-test.red
[
    (error? trap [uparse [] [remove]])
    (didn't uparse [] [remove <any>])
    (
        blk: [a]
        did all [
            '~removed~ == meta uparse blk [remove <any>]
            blk = []
        ]
    )
    (
        blk: [a b a]
        did all [
            'a == uparse blk [some ['a | remove 'b]]
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
    (error? trap [uparse "" [remove]])
    (didn't uparse "" [remove <any>])
    (
        str: "a"
        did all [
            '~removed~ == meta uparse str [remove <any>]
            str = ""
        ]
    )
    (
        str: "aba"
        did all [
            #a == uparse str [some [#a | remove #b]]
            str = "aa"
        ]
    )
    (
        str: "hello world"
        did all [
            "world" == uparse str [remove thru ws "world"]
            str = "world"
        ]
    )
    (
        str: "hello world"
        did all [
            "world" == uparse str [remove "hello" <any> "world"]
            str = " world"
        ]
    )
    (did all [
        '~removed~ == meta uparse s: " t e s t " [some [remove ws | <any>]]
        s = "test"
    ])
    (did all [
        '~removed~ == meta uparse s: " t e s t " [some [remove ws | <any>]]
        s = "test"
    ])
    (
        str: "hello 123 world"
        digit: charset "0123456789"
        did all [
            #d == uparse str [some [remove [some digit #" "] | <any>]]
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
    (error? trap [uparse #{} [remove]])
    (didn't uparse #{} [remove <any>])
    (
        bin: #{0A}
        did all [
            '~removed~ == meta uparse bin [remove <any>]
            bin = #{}
        ]
    )
    (
        bin: #{0A0B0A}
        did all [
            #{0A} == uparse bin [some [#{0A} | remove #{0B}]]
            bin = #{0A0A}
        ]
    )
    (
        ws: make bitset! [" ^- ^/^M" #]
        bin: #{DEAD00BEEF}
        did all [
            #{BEEF} == uparse bin [remove thru ws #{BEEF}]
            bin = #{BEEF}
        ]
    )
    (
        bin: #{DEAD00BEEF}
        did all [
            #{BEEF} == uparse bin [remove #{DEAD} <any> #{BEEF}]
            bin = #{00BEEF}
        ]
    )
    (did all [
        ws: make bitset! [" ^- ^/^M" #]
        '~removed~ == meta uparse s: #{00DE00AD00} [some [remove ws | <any>]]
        s = #{DEAD}
    ])
    (did all [
        ws: make bitset! [" ^- ^/^M" #]
        '~removed~ == meta uparse s: #{00DE00AD00} [some [remove ws | <any>]]
        s = #{DEAD}
    ])
    (
        bin: #{DEAD0001020300BEEF}
        digit: charset [1 - 9]
        did all [
            239 == uparse bin [some [remove [some digit #] | <any>]]
            bin = #{DEAD00BEEF}
        ]
    )
]


[https://github.com/red/red/issues/748
    (
        txt: "Hello world"
        #d == uparse txt [opt some further some [remove "l" | <any>]]
        did all [
            txt = "Heo word"
            8 = length? txt
        ]
    )
]

[#1251
    (did all [
        '~inserted~ == meta uparse e: "a" [remove skip insert ("xxx")]
        e = "xxx"
    ])
    (did all [
        '~inserted~ == meta uparse e: "a" [[remove skip] insert ("xxx")]
        e = "xxx"
    ])
]

[#1244
    (did all [
        didn't uparse a: "12" [remove v: across skip]
        a = "2"
        v = "1"
    ])
    (did all [
        didn't uparse a: "12" [remove [v: across skip]]
        a = "2"
        v = "1"
    ])
]
