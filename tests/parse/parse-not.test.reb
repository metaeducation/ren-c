; %parse-not.test.reb
;
; NOT is actually NOT-AHEAD, but simplified to just NOT

; NOT NOT should be equivalent to AHEAD
; Red at time of writing has trouble with this
; As does Haskell Parsec, e.g. (notFollowedBy . notFollowedBy != lookAhead)
; https://github.com/haskell/parsec/issues/8
[
    ("a" = match-parse "a" [[ahead "a"] "a"])
    ("a" = match-parse "a" [[not not "a"] "a"])
]

[#1246
    ("1" == parse "1" [not not "1" "1"])
    ("1" == parse "1" [not [not "1"] "1"])
    (raised? parse "" [not repeat 0 "a"])
    (raised? parse "" [not [repeat 0 "a"]])
]

[#1240
    ('~not~ == meta parse "" [not "a"])
    ('~not~ == meta parse "" [not <any>])
    ('~not~ == meta parse "" [not false])
]

[
    (raised? parse [] [not <end>])
    ('a == parse [a] [not 'b 'a])
    (raised? parse [a] [not <any>])
    (raised? parse [a] [not <any> <any>])
    ('a == parse [a] [not ['b] 'a])
    (
        wb: ['b]
        'a == parse [a] [not wb 'a]
    )
    (raised? parse [a a] [not ['a 'a] to <end>])
    (~not~ == parse [a a] [not [some 'b] to <end>])
]

[
    (raised? parse "" [not <end>])
    (#a == parse "a" [not #b #a])
    (raised? parse "a" [not <any>])
    (raised? parse "a" [not <any> <any>])
    (#a == parse "a" [not [#b] #a])
    (
        wb: [#b]
        #a == parse "a" [not wb #a]
    )
    (raised? parse "aa" [not [#a #a] to <end>])
    (~not~ == parse "aa" [not [some #b] to <end>])
]

[
    (raised? parse #{} [not <end>])
    (#{0A} == parse #{0A} [not #{0B} #{0A}])
    (raised? parse #{0A} [not <any>])
    (raised? parse #{0A} [not <any> <any>])
    (#{0A} == parse #{0A} [not [#{0B}] #{0A}])
    (
        wb: [#b]
        #{0A} == parse #{0A} [not wb #{0A}]
    )
    (raised? parse #{0A0A} [not [#{0A} #{0A}] to <end>])
    (~not~ == parse #{0A0A} [not [some #{0B}] to <end>])
]
