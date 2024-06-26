; %parse-to.test.reb

; Edge case of matching END with TO or THRU
;
(void? parse "" [to ["a" | <end>]])
(void? parse [] [to ["a" | <end>]])

[
    (void? parse [] [to <end>])
    (void? parse [a] [to <end>])
    ~parse-incomplete~ !! (parse [a] [to 'a])
    ~parse-mismatch~ !! (parse [a] [to 'a <end>])
    ('a == parse [a] [to 'a <any>])
    ('b == parse [a b] [to 'b <any>])
    ('b == parse [a a a b] [to 'b <any>])
    ('a == parse [a a b a] [<any> to 'b repeat 2 <any>])
    ~parse-incomplete~ !! (parse [a] [to ['a]])
    ~parse-mismatch~ !! (parse [a] [to ['a] <end>])
    ('a == parse [a] [to ['a] <any>])
    ('b == parse [a b] [to ['b] <any>])
    ('b == parse [a a a b] [to ['b] <any>])
    ('a == parse [a a b a] [<any> to ['b] repeat 2 <any>])
    ('c == parse [z z a b c] [to ['c | 'b | 'a] repeat 3 <any>])
    ('c == parse [z z a b c] [to ['a | 'b | 'c] repeat 3 <any>])
    ~parse-mismatch~ !! (parse [] [to 'a])
    ~parse-mismatch~ !! (parse [] [to ['a]])
]

[
    (void? parse "" [to <end>])
    (void? parse "a" [to <end>])
    ~parse-incomplete~ !! (parse "a" [to #a])
    ~parse-mismatch~ !! (parse "a" [to #a <end>])
    (#b == parse "ab" [to #a repeat 2 <any>])
    (#a == parse "a" [to #a <any>])
    (#a == parse "aaab" [to #a to <end>])
    ~parse-incomplete~ !! (parse "a" [to [#a]])
    ~parse-mismatch~ !! (parse "a" [to [#a] <end>])
    (#a == parse "a" [to [#a] <any>])
    (#a == parse "aaab" [to [#a] to <end>])
    (#b == parse "ab" [to [#a] repeat 2 <any>])
    (#c == parse "zzabc" [to [#c | #b | #a] repeat 3 <any>])
    (#c == parse "zzabc" [to [#a | #b | #c] repeat 3 <any>])
    ~parse-mismatch~ !! (parse "" [to "a"])
    ~parse-mismatch~ !! (parse "" [to #a])
    ~parse-mismatch~ !! (parse "" [to ["a"]])
    ~parse-mismatch~ !! (parse "" [to [#a]])
]

; TO and THRU would be too costly to be implicitly value bearing by making
; copies; you need to use ACROSS.
[(
    void? parse "abc" [to <end>]
)(
    void? parse "abc" [elide to <end>]
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
    ~parse-mismatch~ !! (parse "ba" compose [to (charset "a") "ba"])
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
    (void? parse #{} [to <end>])
    (void? parse #{0A} [to <end>])
    ~parse-incomplete~ !! (parse #{0A} [to #{0A}])
    ~parse-mismatch~ !! (parse #{0A} [to #{0A} <end>])
    (10 == parse #{0A} [to #{0A} <any>])
    (11 == parse #{0A0B} [to #{0A} repeat 2 <any>])
    (#{0A} == parse #{0A0A0A0B} [to #{0A} to <end>])
    ~parse-incomplete~ !! (parse #{0A} [to [#{0A}]])
    ~parse-mismatch~ !! (parse #{0A} [to [#{0A}] <end>])
    (10 == parse #{0A} [to [#{0A}] <any>])
    (11 == parse #{0A0B} [to [#{0A}] repeat 2 <any>])
    (12 == parse #{99990A0B0C} [to [#"^L" | #{0B} | #{0A}] repeat 3 <any>])
    (12 == parse #{99990A0B0C} [to [#{0A} | #{0B} | #"^L"] repeat 3 <any>])
    (#{0A} == parse #{0A0A0A0B} [to [#{0A}] to <end>])
    ~parse-mismatch~ !! (parse #{} [to #{0A}])
    ~parse-mismatch~ !! (parse #{} [to #"^/"])
    ~parse-mismatch~ !! (parse #{} [to [#{0A}]])
    ~parse-mismatch~ !! (parse #{} [to [#"^/"]])
]

[https://github.com/red/red/issues/3427
    ("23" == parse/part %234 ["23" to [<end>]] 3)
    ("23" == parse/part %234 ["23" to <end>] 3)
    (
        count-up i 4 [
            assert ["1" == parse/part "12" ["1" to [<end>]] i]
        ]
        true
    )
]
