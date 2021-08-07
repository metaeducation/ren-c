; %parse-thru.test.reb
;
; Up to but not including

; Edge case of matching END with THRU
;
(uparse? "" [thru ["a" | <end>]])
(uparse? [] [thru ["a" | <end>]])

[
    (uparse? [] [thru <end>])
    (uparse? [a] [thru 'a])
    (uparse? [a] [thru 'a <end>])
    (not uparse? [a] [thru 'a <any>])
    (uparse? [a b] [thru 'b])
    (uparse? [a] [thru ['a]])
    (uparse? [a] [thru ['a] <end>])
    (not uparse? [a] [thru ['a] <any>])
    (uparse? [a b] [thru ['b]])
]


[#1959
    (uparse? "abcd" [thru "d"])
    (uparse? "<abcd>" [thru '<abcd>])
    (uparse? [a b c d] [thru 'd])
]

[#1457
    (uparse? "a" compose [thru (charset "a")])
    (not uparse? "a" compose [thru (charset "a") skip])
]

[#2141 (
    xset: charset "x"
    uparse? "x" [thru [xset]]
)]

; THRU advances the input position correctly.
(
    i: 0
    uparse "a." [
        while [thru "a" (i: i + 1 j: try if i > 1 [end skip]) j]
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

    (uparse? [z z a b c] [thru ['c | 'b | 'a] 2 <any>])
    (uparse? [z z a b c] [thru ['a | 'b | 'c] 2 <any>])
    (uparse? [b b a a c] [thru 2 'a 'c])
    (uparse? [b b a a c] [thru 2 'a 'c])
    (uparse? [b b a a c] [thru [2 'a] 'c])
    (uparse? [b b a a c] [thru some 'a 'c])
    (uparse? [b b a a c] [thru [some 'a] 'c])
    (uparse? [b b a a c] [thru [some 'x | 2 'a] 'c])
    (uparse? [b b a a c] [thru 2 wa 'c])
    (uparse? [b b a a c] [thru some wa 'c])
    (uparse? [1 "hello"] [thru "hello"])
]

(
    res: ~
    did all [
        uparse? [1 "hello" a 1 2 3 b] [
            thru "hello" <any> res: across to 'b <any>
        ]
        res = [1 2 3]
    ]
)

[
    (uparse? "" [thru <end>])
    (uparse? "a" [thru #a])
    (uparse? "a" [thru #a <end>])
    (not uparse? "a" [thru #a <any>])
    (uparse? "ab" [thru #a <any>])
    (uparse? "aaba" [<any> thru #a 2 <any>])
    (uparse? "a" [thru [#a]])
    (uparse? "a" [thru [#a] <end>])
    (not uparse? "a" [thru [#a] <any>])
    (uparse? "ab" [thru [#a] <any>])
    (uparse? "aaba" [<any> thru [#a] 2 <any>])
    (uparse? "zzabc" [thru [#c | #b | #a] 2 <any>])
    (uparse? "zzabc" [thru [#a | #b | #c] 2 <any>])
    (uparse? "bbaaac" [thru 3 #a #c])
    (uparse? "bbaaac" [thru 3 "a" "c"])
    (
        wa: [#a]
        uparse? "bbaaac" [thru 3 wa #c]
    )
    (uparse? "bbaaac" [thru [3 "a"] "c"])
    (uparse? "bbaaac" [thru some "a" "c"])
    (uparse? "bbaaac" [thru [some #a] "c"])
    (uparse? "bbaaac" [thru [some #x | "aaa"] "c"])
]

[
    (
        bin: #{0BAD00CAFE00BABE00DEADBEEF00}
        wa: [#{0A}]
        true
    )
    (uparse? #{} [thru <end>])
    (uparse? #{0A} [thru #{0A}])
    (uparse? #{0A} [thru #{0A} <end>])
    (not uparse? #{0A} [thru #{0A} <any>])
    (uparse? #{0A0B} [thru #{0A} <any>])
    (uparse? #{0A0A0B0A} [<any> thru #{0A} 2 <any>])
    (uparse? #{0A} [thru [#{0A}]])
    (uparse? #{0A} [thru [#{0A}] <end>])
    (not uparse? #{0A} [thru [#{0A}] <any>])
    (uparse? #{0A0B} [thru [#{0A}] <any>])
    (uparse? #{0A0A0B0A} [<any> thru [#{0A}] 2 <any>])
    (uparse? #{99990A0B0C} [thru [#"^L" | #{0B} | #{0A}] 2 <any>])
    (uparse? #{99990A0B0C} [thru [#{0A} | #{0B} | #"^L"] 2 <any>])
    (uparse? #{0B0B0A0A0A0C} [thru 3 #{0A} #"^L"])
    (uparse? #{0B0B0A0A0A0C} [thru 3 #{0A} #{0C}])
    (uparse? #{0B0B0A0A0A0C} [thru 3 wa #"^L"])
    (uparse? #{0B0B0A0A0A0C} [thru [3 #{0A}] #{0C}])
    (uparse? #{0B0B0A0A0A0C} [thru some #{0A} #{0C}])
    (uparse? #{0B0B0A0A0A0C} [thru [some #{0A}] #{0C}])
    (uparse? #{0B0B0A0A0A0C} [thru [some #x | #{0A0A0A}] #{0C}])
    (uparse? bin [thru #{DEADBEEF} <any>])
    (
        res: ~
        did all [
            uparse? bin [thru #{CAFE} <any> res: across to # to <end>]
            res = #{BABE}
        ]
    )
    (
        res: ~
        did all [
            uparse? bin [thru #{BABE} res: <here> to <end>]
            9 = index? res
        ]
    )
]

[https://github.com/red/red/issues/3427
    (uparse?/part %234 ["23" thru [<end>]] 3)
]
