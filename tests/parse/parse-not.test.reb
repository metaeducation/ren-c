; %parse-not.test.reb
;
; NOT is actually NOT-AHEAD, but simplified to just NOT

; NOT NOT should be equivalent to AHEAD
; Red at time of writing has trouble with this
; As does Haskell Parsec, e.g. (notFollowedBy . notFollowedBy != lookAhead)
; https://github.com/haskell/parsec/issues/8
[
    ("a" = match-uparse "a" [[ahead "a"] "a"])
    ("a" = match-uparse "a" [[not not "a"] "a"])
]

[#1246
    (uparse? "1" [not not "1" "1"])
    (uparse? "1" [not [not "1"] "1"])
    (not uparse? "" [not 0 "a"])
    (not uparse? "" [not [0 "a"]])
]

[#1240
    (uparse? "" [not "a"])
    (uparse? "" [not skip])
    (uparse? "" [not false])
]

[
    (not uparse? [] [not <end>])
    (uparse? [a] [not 'b 'a])
    (not uparse? [a] [not <any>])
    (not uparse? [a] [not <any> <any>])
    (uparse? [a] [not ['b] 'a])
    (
        wb: ['b]
        uparse? [a] [not wb 'a]
    )
    (not uparse? [a a] [not ['a 'a] to <end>])
    (uparse? [a a] [not [some 'b] to <end>])
]

[
    (not uparse? "" [not <end>])
    (uparse? "a" [not #b #a])
    (not uparse? "a" [not <any>])
    (not uparse? "a" [not <any> <any>])
    (uparse? "a" [not [#b] #a])
    (
        wb: [#b]
        uparse? "a" [not wb #a]
    )
    (not uparse? "aa" [not [#a #a] to <end>])
    (uparse? "aa" [not [some #b] to <end>])
]

[
    (not uparse? #{} [not <end>])
    (uparse? #{0A} [not #{0B} #{0A}])
    (not uparse? #{0A} [not <any>])
    (not uparse? #{0A} [not <any> <any>])
    (uparse? #{0A} [not [#{0B}] #{0A}])
    (
        wb: [#b]
        uparse? #{0A} [not wb #{0A}]
    )
    (not uparse? #{0A0A} [not [#{0A} #{0A}] to <end>])
    (uparse? #{0A0A} [not [some #{0B}] to <end>])
]
