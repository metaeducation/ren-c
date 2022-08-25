; %parse-to.test.reb

; Edge case of matching END with TO or THRU
;
("" == parse "" [to ["a" | <end>]])
([] == parse [] [to ["a" | <end>]])

[
    ([] == parse [] [to <end>])
    ([] == parse [a] [to <end>])
    (didn't parse [a] [to 'a])
    (didn't parse [a] [to 'a <end>])
    ('a == parse [a] [to 'a <any>])
    ('b == parse [a b] [to 'b <any>])
    ('b == parse [a a a b] [to 'b <any>])
    ('a == parse [a a b a] [<any> to 'b repeat 2 <any>])
    (didn't parse [a] [to ['a]])
    (didn't parse [a] [to ['a] <end>])
    ('a == parse [a] [to ['a] <any>])
    ('b == parse [a b] [to ['b] <any>])
    ('b == parse [a a a b] [to ['b] <any>])
    ('a == parse [a a b a] [<any> to ['b] repeat 2 <any>])
    ('c == parse [z z a b c] [to ['c | 'b | 'a] repeat 3 <any>])
    ('c == parse [z z a b c] [to ['a | 'b | 'c] repeat 3 <any>])
    (didn't parse [] [to 'a])
    (didn't parse [] [to ['a]])
]

[
    ("" == parse "" [to <end>])
    ("" == parse "a" [to <end>])
    (didn't parse "a" [to #a])
    (didn't parse "a" [to #a <end>])
    (#b == parse "ab" [to #a repeat 2 <any>])
    (#a == parse "a" [to #a <any>])
    ("" == parse "aaab" [to #a to <end>])
    (didn't parse "a" [to [#a]])
    (didn't parse "a" [to [#a] <end>])
    (#a == parse "a" [to [#a] <any>])
    ("" == parse "aaab" [to [#a] to <end>])
    (#b == parse "ab" [to [#a] repeat 2 <any>])
    (#c == parse "zzabc" [to [#c | #b | #a] repeat 3 <any>])
    (#c == parse "zzabc" [to [#a | #b | #c] repeat 3 <any>])
    (didn't parse "" [to "a"])
    (didn't parse "" [to #a])
    (didn't parse "" [to ["a"]])
    (didn't parse "" [to [#a]])
]

; TO and THRU would be too costly to be implicitly value bearing by making
; copies; you need to use ACROSS.
[(
    "" = parse "abc" [to <end>]
)(
    '~void~ = ^ parse "abc" [elide to <end>]
)(
    "b" = parse "aaabbb" [thru "b" elide to <end>]
)(
    "b" = parse "aaabbb" [to "b" elide to <end>]
)]

[#1959
    ("d" == parse "abcd" [to "d" elide <any>])
    ('d == parse [a b c d] [to 'd <any>])
]

[#1457
    (#a == parse "ba" compose [to (charset "a") <any>])
    (didn't parse "ba" compose [to (charset "a") "ba"])
]

[https://github.com/red/red/issues/2515
    ("is" == parse "this one is" ["this" to "is" "is"])
]

[https://github.com/red/red/issues/2818
    (#c == parse "abc" [to [s: <here> "bc"] repeat 2 <any>])
    (#c == parse "abc" [to [s: <here> () "bc"] repeat 2 <any>])
    (#c == parse "abc" [to [s: <here> (123) "bc"] repeat 2 <any>])
]

[
    (
        bin: #{0BAD00CAFE00BABE00DEADBEEF00}
        wa: [#{0A}]
        true
    )
    (#{} == parse #{} [to <end>])
    (#{} == parse #{0A} [to <end>])
    (didn't parse #{0A} [to #{0A}])
    (didn't parse #{0A} [to #{0A} <end>])
    (10 == parse #{0A} [to #{0A} <any>])
    (11 == parse #{0A0B} [to #{0A} repeat 2 <any>])
    (#{} == parse #{0A0A0A0B} [to #{0A} to <end>])
    (didn't parse #{0A} [to [#{0A}]])
    (didn't parse #{0A} [to [#{0A}] <end>])
    (10 == parse #{0A} [to [#{0A}] <any>])
    (11 == parse #{0A0B} [to [#{0A}] repeat 2 <any>])
    (12 == parse #{99990A0B0C} [to [#"^L" | #{0B} | #{0A}] repeat 3 <any>])
    (12 == parse #{99990A0B0C} [to [#{0A} | #{0B} | #"^L"] repeat 3 <any>])
    (#{} == parse #{0A0A0A0B} [to [#{0A}] to <end>])
    (didn't parse #{} [to #{0A}])
    (didn't parse #{} [to #"^/"])
    (didn't parse #{} [to [#{0A}]])
    (didn't parse #{} [to [#"^/"]])
]

[https://github.com/red/red/issues/3427
    (%"" == parse/part %234 ["23" to [<end>]] 3)
    (%"" == parse/part %234 ["23" to <end>] 3)
    (
        count-up i 4 [
            assert ["" == parse/part "12" ["1" to [<end>]] i]
        ]
        true
    )
]

; TO and AHEAD are "broken" w.r.t. FURTHEST at the moment
; https://forum.rebol.info/t/semantics-of-parses-furthest/1868
(
    [value furthest]: parse "abcd" [to "c"]
    did all [
        value = null
        furthest = "d"
    ]
)
