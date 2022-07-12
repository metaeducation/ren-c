; %parse-thru.test.reb
;
; Up to but not including

; Edge case of matching END with THRU
;
("" == parse "" [thru ["a" | <end>]])
([] == parse [] [thru ["a" | <end>]])

[
    ([] == parse [] [thru <end>])
    ('a == parse [a] [thru 'a])
    ([] == parse [a] [thru 'a <end>])
    (didn't parse [a] [thru 'a <any>])
    ('b == parse [a b] [thru 'b])
    ('a == parse [a] [thru ['a]])
    ([] == parse [a] [thru ['a] <end>])
    (didn't parse [a] [thru ['a] <any>])
    ('b == parse [a b] [thru ['b]])
]

[#1282
    ('a == parse [1 2 a] [thru word!])
]

[#2129 (
    rule: [x: across thru some "0"]
    did all [
        "000000" == parse "000000" rule
        x = "000000"
    ]
)]

[#295 (
    f: ~
    did all [
        "xyz" == parse "xyz" [f: across thru to <end>]
        f = "xyz"
    ]
)(
    f: ~
    did all [
        "xyz" == parse "xyz" [f: across thru thru <end>]
        f = "xyz"
    ]
)]

[#1959
    ("d" == parse "abcd" [thru "d"])
    ("<abcd>" == parse "<abcd>" [thru '<abcd>])
    ('d == parse [a b c d] [thru 'd])
]

[#1457
    (#a == parse "a" compose [thru (charset "a")])
    (didn't parse "a" compose [thru (charset "a") <any>])
]

[#2141 (
    xset: charset "x"
    #x == parse "x" [thru [xset]]
)]

; THRU advances the input position correctly.
(
    i: 0
    parse "a." [
        some [thru "a" (i: i + 1 j: try if i > 1 [<end> <any>]) j]
    ]
    i == 1
)

; To THRU, the rule is a black box...so it has to run that black box on each
; position it visits.  So `thru [X | Y]` <> `[thru X | thru Y]`.
[
    (
        wa: ['a]
        true
    )

    ('c == parse [z z a b c] [thru ['c | 'b | 'a] repeat 2 <any>])
    ('c == parse [z z a b c] [thru ['a | 'b | 'c] repeat 2 <any>])
    ('c == parse [b b a a c] [thru repeat 2 'a 'c])
    ('c == parse [b b a a c] [thru repeat 2 'a 'c])
    ('c == parse [b b a a c] [thru [repeat 2 'a] 'c])
    ('c == parse [b b a a c] [thru some 'a 'c])
    ('c == parse [b b a a c] [thru [some 'a] 'c])
    ('c == parse [b b a a c] [thru [some 'x | repeat 2 'a] 'c])
    ('c == parse [b b a a c] [thru repeat 2 wa 'c])
    ('c == parse [b b a a c] [thru some wa 'c])
    ("hello" == parse [1 "hello"] [thru "hello"])
]

(
    res: ~
    did all [
        'b == parse [1 "hello" a 1 2 3 b] [
            thru "hello" <any> res: across to 'b <any>
        ]
        res = [1 2 3]
    ]
)

[
    ("" == parse "" [thru <end>])
    (#a == parse "a" [thru #a])
    ("" == parse "a" [thru #a <end>])
    (didn't parse "a" [thru #a <any>])
    (#b == parse "ab" [thru #a <any>])
    (#a == parse "aaba" [<any> thru #a repeat 2 <any>])
    (#a == parse "a" [thru [#a]])
    ("" == parse "a" [thru [#a] <end>])
    (didn't parse "a" [thru [#a] <any>])
    (#b == parse "ab" [thru [#a] <any>])
    (#a == parse "aaba" [<any> thru [#a] repeat 2 <any>])
    (#c == parse "zzabc" [thru [#c | #b | #a] repeat 2 <any>])
    (#c == parse "zzabc" [thru [#a | #b | #c] repeat 2 <any>])
    (#c == parse "bbaaac" [thru repeat 3 #a #c])
    ("c" == parse "bbaaac" [thru repeat 3 "a" "c"])
    (
        wa: [#a]
        #c == parse "bbaaac" [thru repeat 3 wa #c]
    )
    ("c" == parse "bbaaac" [thru [repeat 3 "a"] "c"])
    ("c" == parse "bbaaac" [thru some "a" "c"])
    ("c" == parse "bbaaac" [thru [some #a] "c"])
    ("c" == parse "bbaaac" [thru [some #x | "aaa"] "c"])
]

[
    (
        bin: #{0BAD00CAFE00BABE00DEADBEEF00}
        wa: [#{0A}]
        true
    )
    (#{} == parse #{} [thru <end>])
    (#{0A} == parse #{0A} [thru #{0A}])
    (#{} == parse #{0A} [thru #{0A} <end>])
    (didn't parse #{0A} [thru #{0A} <any>])
    (11 == parse #{0A0B} [thru #{0A} <any>])
    (10 == parse #{0A0A0B0A} [<any> thru #{0A} repeat 2 <any>])
    (#{0A} == parse #{0A} [thru [#{0A}]])
    (#{} == parse #{0A} [thru [#{0A}] <end>])
    (didn't parse #{0A} [thru [#{0A}] <any>])
    (11 == parse #{0A0B} [thru [#{0A}] <any>])
    (10 == parse #{0A0A0B0A} [<any> thru [#{0A}] repeat 2 <any>])
    (12 == parse #{99990A0B0C} [thru [#"^L" | #{0B} | #{0A}] repeat 2 <any>])
    (12 == parse #{99990A0B0C} [thru [#{0A} | #{0B} | #"^L"] repeat 2 <any>])
    (#"^L" == parse #{0B0B0A0A0A0C} [thru repeat 3 #{0A} #"^L"])
    (#{0C} == parse #{0B0B0A0A0A0C} [thru repeat 3 #{0A} #{0C}])
    (#"^L" == parse #{0B0B0A0A0A0C} [thru repeat 3 wa #"^L"])
    (#{0C} == parse #{0B0B0A0A0A0C} [thru [repeat 3 #{0A}] #{0C}])
    (#{0C} == parse #{0B0B0A0A0A0C} [thru some #{0A} #{0C}])
    (#{0C} == parse #{0B0B0A0A0A0C} [thru [some #{0A}] #{0C}])
    (#{0C} == parse #{0B0B0A0A0A0C} [thru [some #x | #{0A0A0A}] #{0C}])
    (0 == parse bin [thru #{DEADBEEF} <any>])
    (
        res: ~
        did all [
            #{} == parse bin [thru #{CAFE} <any> res: across to # to <end>]
            res = #{BABE}
        ]
    )
    (
        res: ~
        did all [
            #{} == parse bin [thru #{BABE} res: <here> to <end>]
            9 = index? res
        ]
    )
]

[https://github.com/red/red/issues/3427
    (%"" == parse/part %234 ["23" thru [<end>]] 3)
]
