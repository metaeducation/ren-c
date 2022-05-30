; %parse-binary.test.reb
;
; This is generally intended for when the parse rule is a BINARY!, but other
; misc tests related to binary input can go here too.

; General BINARY! matching
[
    ('~ = ^ uparse #{} [])  ; can't be ~void~ isotope, would run ELSE!

    (#{0A} == uparse #{0A} [#{0A}])
    (#"^/" == uparse #{0A} [#"^/"])
    (didn't uparse #{0A} [#{0B}])
    (#{0B} == uparse #{0A0B} [#{0A} #{0B}])
    (#{0A0B} == uparse #{0A0B} [#{0A0B}])
    (#{0A} == uparse #{0A} [[#{0A}]])
    (#{0B} == uparse #{0A0B} [[#{0A}] #{0B}])
    (#{0B} == uparse #{0A0B} [#{0A} [#{0B}]])
    (#{0B} == uparse #{0A0B} [[#{0A}] [#{0B}]])
    (#{0A} == uparse #{0A} [#{0B} | #{0A}])
    (didn't uparse #{0A0B} [#{0B} | #{0A}])
    (#{0A} == uparse #{0A} [[#{0B} | #{0A}]])
    (didn't uparse #{0A0B} [[#{0B} | #{0A}]])
    (#{0B} == uparse #{0A0B} [[#{0A} | #{0B}] [#{0B} | #{0A}]])
    (
        res: 0
        did all [
            1 == uparse #{} [(res: 1)]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            1 == uparse #{0A} [#{0A} (res: 1)]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            didn't uparse #{0A} [#{0B} (res: 1)]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            1 == uparse #{} [[(res: 1)]]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            1 == uparse #{0A} [[#{0A} (res: 1)]]
            res = 1
        ]
    )
    (
        res: 0
        did all [
            didn't uparse #{0A} [[#{0B} (res: 1)]]
            res = 0
        ]
    )
    (
        res: 0
        did all [
            3 == uparse #{0A0B} [
                #{0A} (res: 1) [#"^L" (res: 2) | #{0B} (res: 3)]
            ]
            res = 3
        ]
    )
    (
        res: 0
        did all [
            didn't uparse #{0A0B} [#{0A} (res: 1) [#{0C} (res: 2) | #{0D} (res: 3)]]
            res = 1
        ]
    )
    (didn't uparse #{0A0A} [1 [#{0A}]])
    (#{0A} == uparse #{0A0A} [2 [#{0A}]])
    (didn't uparse #{0A0A} [3 [#{0A}]])

    (didn't uparse #{0A0A} [repeat ([1 1]) [#{0A}]])
    (#{0A} == uparse #{0A0A} [repeat ([1 2]) [#{0A}]])
    (#{0A} == uparse #{0A0A} [repeat ([2 2]) [#{0A}]])
    (#{0A} == uparse #{0A0A} [repeat ([2 3]) [#{0A}]])
    (didn't uparse #{0A0A} [repeat ([3 4]) [#{0A}]])
    (didn't uparse #{0A0A} [repeat ([1 1]) #{0A}])
    (#{0A} == uparse #{0A0A} [repeat ([1 2]) #{0A}])
    (#{0A} == uparse #{0A0A} [repeat ([2 2]) #{0A}])
    (#{0A} == uparse #{0A0A} [repeat ([2 3]) #{0A}])
    (didn't uparse #{0A0A} [repeat ([3 4]) #{0A}])
    (didn't uparse #{0A0A} [repeat ([1 1]) <any>])
    (10 == uparse #{0A0A} [repeat ([1 2]) <any>])
    (10 == uparse #{0A0A} [repeat ([2 2]) <any>])
    (10 == uparse #{0A0A} [repeat ([2 3]) <any>])
    (didn't uparse #{0A0A} [repeat ([3 4]) <any>])

    (didn't uparse #{0A0A} [1 #{0A}])
    (#{0A} == uparse #{0A0A} [2 #{0A}])
    (didn't uparse #{0A0A} [3 #{0A}])
    (didn't uparse #{0A0A} [1 <any>])
    (10 == uparse #{0A0A} [2 <any>])
    (didn't uparse #{0A0A} [3 <any>])
    (10 == uparse #{0A} [<any>])
    (11 == uparse #{0A0B} [<any> <any>])
    (11 == uparse #{0A0B} [<any> [<any>]])
    (11 == uparse #{0A0B} [[<any>] [<any>]])
    (#{0A} == uparse #{0A0A} [some [#{0A}]])
    (didn't uparse #{0A0A} [some [#{0A}] #{0B}])
    (10 == uparse #{0A0A0B0A0B0B0B0A} [some [<any>]])
    (#{0A} == uparse #{0A0A0B0A0B0B0B0A} [some [#{0A} | #{0B}]])
    (didn't uparse #{0A0A0B0A0B0B0B0A} [some [#{0A} | #"^L"]])
    (#{0A} == uparse #{0A0A} [some [#{0A}]])
    ('~null~ = ^ uparse #{0A0A} [some [#{0A}] opt some [#{0B}]])
    (#{0B} == uparse #{0A0A0B0B} [2 #{0A} 2 #{0B}])
    (didn't uparse #{0A0A0B0B} [2 #{0A} 3 #{0B}])
    (#{0B} == uparse #{0A0A0B0B} [some #{0A} some #{0B}])
    (didn't uparse #{0A0A0B0B} [some #{0A} some #"^L"])
    (#"^L" == uparse #{0B0A0A0A0C} [<any> some [#{0A}] #"^L"])
]


[#394 (
    #{22} == uparse #{001122} [#{00} #{11} #{22}]
)]

; "BINARY! extraction"
[
    (
        res: ~
        did all [
            10 == uparse #{0A} [res: <any>]
            res = 10
        ]
    )
    (
        res: ~
        did all [
            #{0A} == uparse #{0A} [res: #{0A}]
            res = #{0A}
        ]
    )
    (
        res: ~
        res2: ~
        did all [
            #{0A} == uparse #{0A} [res: res2: #{0A}]
            res = #{0A}
            res2 = #{0A}
        ]
    )
    (
        res: ~
        did all [
            #{0A} == uparse #{0A0A} [res: 2 #{0A}]
            res = #{0A}
        ]
    )
    (
        res: '~before~
        did all [
            didn't uparse #{0A0A} [res: 3 #{0A}]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            #{0A} == uparse #{0A} [res: [#{0A}]]
            res = #{0A}
        ]
    )
    (
        wa: [#{0A}]
        res: ~
        did all [
            #{0A} == uparse #{0A} [res: wa]
            res = #{0A}
        ]
    )
    (
        wa: [#{0A}]
        res: ~
        did all [
            #{0A} == uparse #{0A0A} [res: 2 wa]
            res = #{0A}
        ]
    )
    (
        wa: [#{0A}]
        res: ~
        did all [
            11 == uparse #{0A0A0B} [<any> res: #{0A} <any>]
            res = #{0A}
        ]
    )
    (
        res: ~
        did all [
            11 == uparse #{0A0A0B} [<any> res: [#{0A} | #{0B}] <any>]
            res = #{0A}
        ]
    )
    (
        res: '~before~
        did all [
            didn't uparse #{0A} [res: [#"^L" | #{0B}]]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            #"^L" == uparse #{0B0A0A0A0C} [<any> res: some #{0A} #"^L"]
            res = #{0A}
        ]
    )
    (
        wa: [#{0A}]
        res: ~
        did all [
            #"^L" == uparse #{0B0A0A0A0C} [<any> res: some wa #"^L"]
            res = #{0A}
        ]
    )
]
