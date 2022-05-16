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
    ("1" == uparse "1" [not not "1" "1"])
    ("1" == uparse "1" [not [not "1"] "1"])
    (didn't uparse "" [not 0 "a"])
    (didn't uparse "" [not [0 "a"]])
]

[#1240
    ('~not~ == meta uparse "" [not "a"])
    ('~not~ == meta uparse "" [not skip])
    ('~not~ == meta uparse "" [not false])
]

[
    (didn't uparse [] [not <end>])
    ('a == uparse [a] [not 'b 'a])
    (didn't uparse [a] [not <any>])
    (didn't uparse [a] [not <any> <any>])
    ('a == uparse [a] [not ['b] 'a])
    (
        wb: ['b]
        'a == uparse [a] [not wb 'a]
    )
    (didn't uparse [a a] [not ['a 'a] to <end>])
    ([] == uparse [a a] [not [some 'b] to <end>])
]

[
    (didn't uparse "" [not <end>])
    (#a == uparse "a" [not #b #a])
    (didn't uparse "a" [not <any>])
    (didn't uparse "a" [not <any> <any>])
    (#a == uparse "a" [not [#b] #a])
    (
        wb: [#b]
        #a == uparse "a" [not wb #a]
    )
    (didn't uparse "aa" [not [#a #a] to <end>])
    ("" == uparse "aa" [not [some #b] to <end>])
]

[
    (didn't uparse #{} [not <end>])
    (#{0A} == uparse #{0A} [not #{0B} #{0A}])
    (didn't uparse #{0A} [not <any>])
    (didn't uparse #{0A} [not <any> <any>])
    (#{0A} == uparse #{0A} [not [#{0B}] #{0A}])
    (
        wb: [#b]
        #{0A} == uparse #{0A} [not wb #{0A}]
    )
    (didn't uparse #{0A0A} [not [#{0A} #{0A}] to <end>])
    (#{} == uparse #{0A0A} [not [some #{0B}] to <end>])
]
