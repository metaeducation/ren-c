; %parse-remove.test.reb
;
; Mutating operations in UPARSE raise some large questions; they were removed
; from Topaz entirely.  For the moment they are being considered.

(did all [
    uparse? text: "a ^/ " [
        while [newline remove [to <end>] | "a" [remove [to newline]] | skip]
    ]
    text = "a^/"
])


; BLOCK! remove tests from %parse-test.red
[
    (error? trap [uparse? [] [remove]])
    (not uparse? [] [remove <any>])
    (
        blk: [a]
        did all [
            uparse? blk [remove <any>]
            blk = []
        ]
    )
    (
        blk: [a b a]
        did all [
            uparse? blk [some ['a | remove 'b]]
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
    (error? trap [uparse? "" [remove]])
    (not uparse? "" [remove <any>])
    (
        str: "a"
        did all [
            uparse? str [remove <any>]
            str = ""
        ]
    )
    (
        str: "aba"
        did all [
            uparse? str [some [#a | remove #b]]
            str = "aa"
        ]
    )
    (
        str: "hello world"
        did all [
            uparse? str [remove thru ws "world"]
            str = "world"
        ]
    )
    (
        str: "hello world"
        did all [
            uparse? str [remove "hello" <any> "world"]
            str = " world"
        ]
    )
    (did all [
        uparse? s: " t e s t " [while [remove ws | <any>]]
        s = "test"
    ])
    (did all [
        uparse? s: " t e s t " [while [remove ws | <any>]]
        s = "test"
    ])
    (
        str: "hello 123 world"
        digit: charset "0123456789"
        did all [
            uparse? str [while [remove [some digit #" "] | <any>]]
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
    (error? trap [uparse? #{} [remove]])
    (not uparse? #{} [remove <any>])
    (
        bin: #{0A}
        did all [
            uparse? bin [remove <any>]
            bin = #{}
        ]
    )
    (
        bin: #{0A0B0A}
        did all [
            uparse? bin [some [#{0A} | remove #{0B}]]
            bin = #{0A0A}
        ]
    )
    (
        ws: make bitset! [" ^- ^/^M" #]
        bin: #{DEAD00BEEF}
        did all [
            uparse? bin [remove thru ws #{BEEF}]
            bin = #{BEEF}
        ]
    )
    (
        bin: #{DEAD00BEEF}
        did all [
            uparse? bin [remove #{DEAD} <any> #{BEEF}]
            bin = #{00BEEF}
        ]
    )
    (did all [
        ws: make bitset! [" ^- ^/^M" #]
        uparse? s: #{00DE00AD00} [while [remove ws | <any>]]
        s = #{DEAD}
    ])
    (did all [
        ws: make bitset! [" ^- ^/^M" #]
        uparse? s: #{00DE00AD00} [while [remove ws | <any>]]
        s = #{DEAD}
    ])
    (
        bin: #{DEAD0001020300BEEF}
        digit: charset [1 - 9]
        did all [
            uparse? bin [while [remove [some digit #] | <any>]]
            bin = #{DEAD00BEEF}
        ]
    )
]


[https://github.com/red/red/issues/748
    (
        txt: "Hello world"
        uparse? txt [while [while [remove "l" | <any>] false]]
        did all [
            txt = "Heo word"
            8 = length? txt
        ]
    )
]
