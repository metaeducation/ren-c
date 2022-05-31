; %parse-to.test.reb

; Edge case of matching END with TO or THRU
;
("" == uparse "" [to ["a" | <end>]])
([] == uparse [] [to ["a" | <end>]])

[
    ([] == uparse [] [to <end>])
    ([] == uparse [a] [to <end>])
    (didn't uparse [a] [to 'a])
    (didn't uparse [a] [to 'a <end>])
    ('a == uparse [a] [to 'a <any>])
    ('b == uparse [a b] [to 'b <any>])
    ('b == uparse [a a a b] [to 'b <any>])
    ('a == uparse [a a b a] [<any> to 'b 2 <any>])
    (didn't uparse [a] [to ['a]])
    (didn't uparse [a] [to ['a] <end>])
    ('a == uparse [a] [to ['a] <any>])
    ('b == uparse [a b] [to ['b] <any>])
    ('b == uparse [a a a b] [to ['b] <any>])
    ('a == uparse [a a b a] [<any> to ['b] 2 <any>])
    ('c == uparse [z z a b c] [to ['c | 'b | 'a] 3 <any>])
    ('c == uparse [z z a b c] [to ['a | 'b | 'c] 3 <any>])
    (didn't uparse [] [to 'a])
    (didn't uparse [] [to ['a]])
]

[
    ("" == uparse "" [to <end>])
    ("" == uparse "a" [to <end>])
    (didn't uparse "a" [to #a])
    (didn't uparse "a" [to #a <end>])
    (#b == uparse "ab" [to #a 2 <any>])
    (#a == uparse "a" [to #a <any>])
    ("" == uparse "aaab" [to #a to <end>])
    (didn't uparse "a" [to [#a]])
    (didn't uparse "a" [to [#a] <end>])
    (#a == uparse "a" [to [#a] <any>])
    ("" == uparse "aaab" [to [#a] to <end>])
    (#b == uparse "ab" [to [#a] 2 <any>])
    (#c == uparse "zzabc" [to [#c | #b | #a] 3 <any>])
    (#c == uparse "zzabc" [to [#a | #b | #c] 3 <any>])
    (didn't uparse "" [to "a"])
    (didn't uparse "" [to #a])
    (didn't uparse "" [to ["a"]])
    (didn't uparse "" [to [#a]])
]

; TO and THRU would be too costly to be implicitly value bearing by making
; copies; you need to use ACROSS.
[(
    "" = uparse "abc" [to <end>]
)(
    none? uparse "abc" [elide to <end>]  ; ornery none
)(
    "b" = uparse "aaabbb" [thru "b" elide to <end>]
)(
    "b" = uparse "aaabbb" [to "b" elide to <end>]
)]

[#1959
    ("d" == uparse "abcd" [to "d" elide <any>])
    ('d == uparse [a b c d] [to 'd <any>])
]

[#1457
    (#a == uparse "ba" compose [to (charset "a") <any>])
    (didn't uparse "ba" compose [to (charset "a") "ba"])
]

[https://github.com/red/red/issues/2515
    ("is" == uparse "this one is" ["this" to "is" "is"])
]

[https://github.com/red/red/issues/2818
    (#c == uparse "abc" [to [s: <here> "bc"] 2 <any>])
    (#c == uparse "abc" [to [s: <here> () "bc"] 2 <any>])
    (#c == uparse "abc" [to [s: <here> (123) "bc"] 2 <any>])
]

[
    (
        bin: #{0BAD00CAFE00BABE00DEADBEEF00}
        wa: [#{0A}]
        true
    )
    (#{} == uparse #{} [to <end>])
    (#{} == uparse #{0A} [to <end>])
    (didn't uparse #{0A} [to #{0A}])
    (didn't uparse #{0A} [to #{0A} <end>])
    (10 == uparse #{0A} [to #{0A} <any>])
    (11 == uparse #{0A0B} [to #{0A} 2 <any>])
    (#{} == uparse #{0A0A0A0B} [to #{0A} to <end>])
    (didn't uparse #{0A} [to [#{0A}]])
    (didn't uparse #{0A} [to [#{0A}] <end>])
    (10 == uparse #{0A} [to [#{0A}] <any>])
    (11 == uparse #{0A0B} [to [#{0A}] 2 <any>])
    (12 == uparse #{99990A0B0C} [to [#"^L" | #{0B} | #{0A}] 3 <any>])
    (12 == uparse #{99990A0B0C} [to [#{0A} | #{0B} | #"^L"] 3 <any>])
    (#{} == uparse #{0A0A0A0B} [to [#{0A}] to <end>])
    (didn't uparse #{} [to #{0A}])
    (didn't uparse #{} [to #"^/"])
    (didn't uparse #{} [to [#{0A}]])
    (didn't uparse #{} [to [#"^/"]])
]

[https://github.com/red/red/issues/3427
    (%"" == uparse/part %234 ["23" to [<end>]] 3)
    (%"" == uparse/part %234 ["23" to <end>] 3)
    (
        count-up i 4 [
            assert ["" == uparse/part "12" ["1" to [<end>]] i]
        ]
        true
    )
]
