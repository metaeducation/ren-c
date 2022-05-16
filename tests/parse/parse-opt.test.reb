; %parse-opt.test.reb
;
; OPT is a rare case of an "abbreviation" being used, but it's a common
; enough word (opt out).  It returns a ~null~ isotope on success.

[
    ('~null~ == meta uparse [] [opt blank])
    ('~null~ == meta uparse [] [opt 'a])
    ('a == uparse [a] [opt 'a])
    ('a == uparse [a] [opt 'b 'a])
    ('a == uparse [a] [opt ['a]])
    (
        wa: ['a]
        'a == uparse [a] [opt wa]
    )
    ('a == uparse [a] [opt <any>])
    ('c == uparse [a b c] [<any> opt 'b <any>])
]

[
    ('~null~ == meta uparse "" [opt blank])
    ('~null~ == meta uparse "" [opt #a])
    (#a == uparse "a" [opt #a])
    (#a == uparse "a" [opt #b #a])
    (#a == uparse "a" [opt [#a]])
    (
        wa: [#a]
        #a == uparse "a" [opt wa]
    )
    (#a == uparse "a" [opt <any>])
    (#c == uparse "abc" [<any> opt #b <any>])
]

[
    ('~null~ == meta uparse #{} [opt blank])
    ('~null~ == meta uparse #{} [opt #{0A}])
    (#{0A} == uparse #{0A} [opt #{0A}])
    (#{0A} == uparse #{0A} [opt #{0B} #{0A}])
    (#{0A} == uparse #{0A} [opt [#{0A}]])
    (
        wa: [#{0A}]
        #{0A} == uparse #{0A} [opt wa]
    )
    (10 == uparse #{0A} [opt <any>])
    (12 == uparse #{0A0B0C} [<any> opt #{0B} <any>])
]
