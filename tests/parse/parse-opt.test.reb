; %parse-opt.test.reb
;
; OPT is a rare case of an "abbreviation" being used, but it's a common
; enough word (opt out).  It passes through the synthesized product of the
; parser it calls on success, but on failure it synthesizes NULL and continues
; the parse rules from the current position.

[
    ('~[]~ == meta parse [] [opt blank])
    ('~[]~ == meta parse [] [opt 'a])
    ('a == parse [a] [opt 'a])
    ('a == parse [a] [opt 'b 'a])
    ('a == parse [a] [opt ['a]])
    (
        wa: ['a]
        'a == parse [a] [opt wa]
    )
    ('a == parse [a] [opt <any>])
    ('c == parse [a b c] [<any> opt 'b <any>])
]

[
    ('~[]~ == meta parse "" [opt blank])
    ('~[]~ == meta parse "" [opt #a])
    (#a == parse "a" [opt #a])
    (#a == parse "a" [opt #b #a])
    (#a == parse "a" [opt [#a]])
    (
        wa: [#a]
        #a == parse "a" [opt wa]
    )
    (#a == parse "a" [opt <any>])
    (#c == parse "abc" [<any> opt #b <any>])
]

[
    ('~[]~ == meta parse #{} [opt blank])
    ('~[]~ == meta parse #{} [opt #{0A}])
    (#{0A} == parse #{0A} [opt #{0A}])
    (#{0A} == parse #{0A} [opt #{0B} #{0A}])
    (#{0A} == parse #{0A} [opt [#{0A}]])
    (
        wa: [#{0A}]
        #{0A} == parse #{0A} [opt wa]
    )
    (10 == parse #{0A} [opt <any>])
    (12 == parse #{0A0B0C} [<any> opt #{0B} <any>])
]
