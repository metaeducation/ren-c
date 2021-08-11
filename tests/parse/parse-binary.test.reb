; %parse-binary.test.reb
;
; This is generally intended for when the parse rule is a BINARY!, but other
; misc tests related to binary input can go here too.

; General BINARY! matching
[
    (uparse? #{} [])
    (uparse? #{0A} [#{0A}])
    (uparse? #{0A} [#"^/"])
    (not uparse? #{0A} [#{0B}])
    (uparse? #{0A0B} [#{0A} #{0B}])
    (uparse? #{0A0B} [#{0A0B}])
    (uparse? #{0A} [[#{0A}]])
    (uparse? #{0A0B} [[#{0A}] #{0B}])
    (uparse? #{0A0B} [#{0A} [#{0B}]])
    (uparse? #{0A0B} [[#{0A}] [#{0B}]])
    (uparse? #{0A} [#{0B} | #{0A}])
    (not uparse? #{0A0B} [#{0B} | #{0A}])
    (uparse? #{0A} [[#{0B} | #{0A}]])
    (not uparse? #{0A0B} [[#{0B} | #{0A}]])
    (uparse? #{0A0B} [[#{0A} | #{0B}] [#{0B} | #{0A}]])
    (
        res: 0
        did all [
            uparse? #{} [(res: 1)]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A} [#{0A} (res: 1)]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            not uparse? #{0A} [#{0B} (res: 1)]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? #{} [[(res: 1)]]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A} [[#{0A} (res: 1)]]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            not uparse? #{0A} [[#{0B} (res: 1)]]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            uparse? #{0A0B} [#{0A} (res: 1) [#"^L" (res: 2) | #{0B} (res: 3)]]
            res = 3
        ]
    )
    (
        res: 0
        did all [
            not uparse? #{0A0B} [#{0A} (res: 1) [#{0C} (res: 2) | #{0D} (res: 3)]]
            res = 1
        ]
    )
    (not uparse? #{0A0A} [1 [#{0A}]])
    (uparse? #{0A0A} [2 [#{0A}]])
    (not uparse? #{0A0A} [3 [#{0A}]])

    ; (not uparse? #{0A0A} [1 1 [#{0A}]])
    ; (uparse? #{0A0A} [1 2 [#{0A}]])
    ; (uparse? #{0A0A} [2 2 [#{0A}]])
    ; (uparse? #{0A0A} [2 3 [#{0A}]])
    ; (not uparse? #{0A0A} [3 4 [#{0A}]])
    ; (not uparse? #{0A0A} [1 1 #{0A}])
    ; (uparse? #{0A0A} [1 2 #{0A}])
    ; (uparse? #{0A0A} [2 2 #{0A}])
    ; (uparse? #{0A0A} [2 3 #{0A}])
    ; (not uparse? #{0A0A} [3 4 #{0A}])
    ; (not uparse? #{0A0A} [1 1 <any>])
    ; (uparse? #{0A0A} [1 2 <any>])
    ; (uparse? #{0A0A} [2 2 <any>])
    ; (uparse? #{0A0A} [2 3 <any>])
    ; (not uparse? #{0A0A} [3 4 <any>])

    (not uparse? #{0A0A} [1 #{0A}])
    (uparse? #{0A0A} [2 #{0A}])
    (not uparse? #{0A0A} [3 #{0A}])
    (not uparse? #{0A0A} [1 <any>])
    (uparse? #{0A0A} [2 <any>])
    (not uparse? #{0A0A} [3 <any>])
    (uparse? #{0A} [<any>])
    (uparse? #{0A0B} [<any> <any>])
    (uparse? #{0A0B} [<any> [<any>]])
    (uparse? #{0A0B} [[<any>] [<any>]])
    (uparse? #{0A0A} [some [#{0A}]])
    (not uparse? #{0A0A} [some [#{0A}] #{0B}])
    (uparse? #{0A0A0B0A0B0B0B0A} [some [<any>]])
    (uparse? #{0A0A0B0A0B0B0B0A} [some [#{0A} | #{0B}]])
    (not uparse? #{0A0A0B0A0B0B0B0A} [some [#{0A} | #"^L"]])
    (uparse? #{0A0A} [while [#{0A}]])
    (uparse? #{0A0A} [some [#{0A}] while [#{0B}]])
    (uparse? #{0A0A0B0B} [2 #{0A} 2 #{0B}])
    (not uparse? #{0A0A0B0B} [2 #{0A} 3 #{0B}])
    (uparse? #{0A0A0B0B} [some #{0A} some #{0B}])
    (not uparse? #{0A0A0B0B} [some #{0A} some #"^L"])
    (uparse? #{0B0A0A0A0C} [<any> some [#{0A}] #"^L"])
]


[#394 (
    uparse? #{001122} [#{00} #{11} #{22}]
)]

; "BINARY! extraction"
[
    (
        res: ~
        did all [
            uparse? #{0A} [res: <any>]
            res = 10
        ]
    )
    (
        res: ~
        did all [
            uparse? #{0A} [res: #{0A}]
            res = #{0A}
        ]
    )
    (
        res: ~
        res2: ~
        did all [
            uparse? #{0A} [res: res2: #{0A}]
            res = #{0A}
            res2 = #{0A}
        ]
    )
    (
        res: ~
        did all [
            uparse? #{0A0A} [res: 2 #{0A}]
            res = #{0A}
        ]
    )
    (
        res: '~before~
        did all [
            not uparse? #{0A0A} [res: 3 #{0A}]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            uparse? #{0A} [res: [#{0A}]]
            res = #{0A}
        ]
    )
    (
        wa: [#{0A}]
        res: ~
        did all [
            uparse? #{0A} [res: wa]
            res = #{0A}
        ]
    )
    (
        wa: [#{0A}]
        res: ~
        did all [
            uparse? #{0A0A} [res: 2 wa]
            res = #{0A}
        ]
    )
    (
        wa: [#{0A}]
        res: ~
        did all [
            uparse? #{0A0A0B} [<any> res: #{0A} <any>]
            res = #{0A}
        ]
    )
    (
        res: ~
        did all [
            uparse? #{0A0A0B} [<any> res: [#{0A} | #{0B}] <any>]
            res = #{0A}
        ]
    )
    (
        res: '~before~
        did all [
            not uparse? #{0A} [res: [#"^L" | #{0B}]]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            uparse? #{0B0A0A0A0C} [<any> res: some #{0A} #"^L"]
            res = #{0A}
        ]
    )
    (
        wa: [#{0A}]
        res: ~
        did all [
            uparse? #{0B0A0A0A0C} [<any> res: some wa #"^L"]
            res = #{0A}
        ]
    )
]
