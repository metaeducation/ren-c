; %parse-not.test.reb
;
; NOT is actually NOT-AHEAD, but simplified to just NOT

; NOT NOT should be equivalent to AHEAD
; Red at time of writing has trouble with this
; As does Haskell Parsec, e.g. (notFollowedBy . notFollowedBy != lookAhead)
; https://github.com/haskell/parsec/issues/8
[
    ("a" = parse "a" [[ahead "a"] "a"])
    ("a" = parse "a" [[not not "a"] "a"])
]

[#1246
    ("1" == parse "1" [not not "1" "1"])
    ("1" == parse "1" [not [not "1"] "1"])

    ~parse-mismatch~ !! (parse "" [not repeat 0 "a"])
    ~parse-mismatch~ !! (parse "" [not [repeat 0 "a"]])
]

[#1240
    ('~not~ == meta parse "" [not "a"])
    ('~not~ == meta parse "" [not <next>])
    ('~not~ == meta parse "" [not false])
]

[
    ~parse-mismatch~ !! (parse [] [not <end>])
    ~parse-mismatch~ !! (parse [a] [not <next>])
    ~parse-mismatch~ !! (parse [a] [not one one])

    ('a == parse [a] [not 'b 'a])
    ('a == parse [a] [not ['b] 'a])
    (
        wb: ['b]
        'a == parse [a] [not wb 'a]
    )
    (~not~ == parse [a a] [not [some 'b] to <end>])

    ~parse-mismatch~ !! (parse [a a] [not ['a 'a] to <end>])
]

[
    ~parse-mismatch~ !! (parse "" [not <end>])
    ~parse-mismatch~ !! (parse "a" [not one])
    ~parse-mismatch~ !! (parse "a" [not <next> <next>])

    (#a == parse "a" [not #b #a])
    (#a == parse "a" [not [#b] #a])
    (
        wb: [#b]
        #a == parse "a" [not wb #a]
    )
    (~not~ == parse "aa" [not [some #b] to <end>])

    ~parse-mismatch~ !! (parse "aa" [not [#a #a] to <end>])
]

[
    ~parse-mismatch~ !! (parse #{} [not <end>])
    ~parse-mismatch~ !! (parse #{0A} [not one])
    ~parse-mismatch~ !! (parse #{0A} [not <next> one])

    (#{0A} == parse #{0A} [not #{0B} #{0A}])
    (#{0A} == parse #{0A} [not [#{0B}] #{0A}])
    (
        wb: [#b]
        #{0A} == parse #{0A} [not wb #{0A}]
    )
    (~not~ == parse #{0A0A} [not [some #{0B}] to <end>])

    ~parse-mismatch~ !! (parse #{0A0A} [not [#{0A} #{0A}] to <end>])
]
