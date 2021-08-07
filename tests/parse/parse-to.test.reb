; %parse-to.test.reb

; Edge case of matching END with TO or THRU
;
(uparse? "" [to ["a" | <end>]])
(uparse? [] [to ["a" | <end>]])

[
    (uparse? [] [to <end>])
    (uparse? [a] [to <end>])
    (not uparse? [a] [to 'a])
    (not uparse? [a] [to 'a <end>])
    (uparse? [a] [to 'a <any>])
    (uparse? [a b] [to 'b <any>])
    (uparse? [a a a b] [to 'b <any>])
    (uparse? [a a b a] [<any> to 'b 2 <any>])
    (not uparse? [a] [to ['a]])
    (not uparse? [a] [to ['a] <end>])
    (uparse? [a] [to ['a] <any>])
    (uparse? [a b] [to ['b] <any>])
    (uparse? [a a a b] [to ['b] <any>])
    (uparse? [a a b a] [<any> to ['b] 2 <any>])
    (uparse? [z z a b c] [to ['c | 'b | 'a] 3 <any>])
    (uparse? [z z a b c] [to ['a | 'b | 'c] 3 <any>])
    (not uparse? [] [to 'a])
    (not uparse? [] [to ['a]])
]

[
    (uparse? "" [to <end>])
    (uparse? "a" [to <end>])
    (not uparse? "a" [to #a])
    (not uparse? "a" [to #a <end>])
    (uparse? "ab" [to #a 2 <any>])
    (uparse? "a" [to #a <any>])
    (uparse? "aaab" [to #a to <end>])
    (not uparse? "a" [to [#a]])
    (not uparse? "a" [to [#a] <end>])
    (uparse? "a" [to [#a] <any>])
    (uparse? "aaab" [to [#a] to <end>])
    (uparse? "ab" [to [#a] 2 <any>])
    (uparse? "zzabc" [to [#c | #b | #a] 3 <any>])
    (uparse? "zzabc" [to [#a | #b | #c] 3 <any>])
    (not uparse? "" [to "a"])
    (not uparse? "" [to #a])
    (not uparse? "" [to ["a"]])
    (not uparse? "" [to [#a]])
]

; TO and THRU would be too costly to be implicitly value bearing by making
; copies; you need to use ACROSS.
[(
    "" = uparse "abc" [to <end>]
)(
    '~void~ = ^(uparse "abc" [elide to <end>])  ; ornery void
)(
    "b" = uparse "aaabbb" [thru "b" elide to <end>]
)(
    "b" = uparse "aaabbb" [to "b" elide to <end>]
)]

[#1959
    (uparse? "abcd" [to "d" skip])
    (uparse? [a b c d] [to 'd skip])
]

[#1457
    (uparse? "ba" compose [to (charset "a") skip])
    (not uparse? "ba" compose [to (charset "a") "ba"])
]

[https://github.com/red/red/issues/2515
    (uparse? "this one is" ["this" to "is" "is"])
]

[https://github.com/red/red/issues/2818
    (uparse? "abc" [to [s: <here> "bc"] 2 <any>])
    (uparse? "abc" [to [s: <here> () "bc"] 2 <any>])
    (uparse? "abc" [to [s: <here> (123) "bc"] 2 <any>])
]

[
    (
        bin: #{0BAD00CAFE00BABE00DEADBEEF00}
        wa: [#{0A}]
        true
    )
    (uparse? #{} [to <end>])
    (uparse? #{0A} [to <end>])
    (not uparse? #{0A} [to #{0A}])
    (not uparse? #{0A} [to #{0A} <end>])
    (uparse? #{0A} [to #{0A} <any>])
    (uparse? #{0A0B} [to #{0A} 2 <any>])
    (uparse? #{0A0A0A0B} [to #{0A} to <end>])
    (not uparse? #{0A} [to [#{0A}]])
    (not uparse? #{0A} [to [#{0A}] <end>])
    (uparse? #{0A} [to [#{0A}] <any>])
    (uparse? #{0A0B} [to [#{0A}] 2 <any>])
    (uparse? #{99990A0B0C} [to [#"^L" | #{0B} | #{0A}] 3 <any>])
    (uparse? #{99990A0B0C} [to [#{0A} | #{0B} | #"^L"] 3 <any>])
    (uparse? #{0A0A0A0B} [to [#{0A}] to <end>])
    (not uparse? #{} [to #{0A}])
    (not uparse? #{} [to #"^/"])
    (not uparse? #{} [to [#{0A}]])
    (not uparse? #{} [to [#"^/"]])
]

[https://github.com/red/red/issues/3427
    (uparse?/part %234 ["23" to [<end>]] 3)
    (uparse?/part %234 ["23" to <end>] 3)
    (
        count-up i 4 [
            assert [uparse?/part "12" ["1" to [<end>]] i]
        ]
        true
    )
]
