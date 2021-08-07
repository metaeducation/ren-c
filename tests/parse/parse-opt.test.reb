; %parse-opt.test.reb
;
; OPT is a rare case of an "abbreviation" being used, but it's a common
; enough word (opt out).  It returns a ~null~ isotope on success.

[
    (uparse? [] [opt blank])
    (uparse? [] [opt 'a])
    (uparse? [a] [opt 'a])
    (uparse? [a] [opt 'b 'a])
    (uparse? [a] [opt ['a]])
    (
        wa: ['a]
        uparse? [a] [opt wa]
    )
    (uparse? [a] [opt <any>])
    (uparse? [a b c] [<any> opt 'b <any>])
]

[
    (uparse? "" [opt blank])
    (uparse? "" [opt #a])
    (uparse? "a" [opt #a])
    (uparse? "a" [opt #b #a])
    (uparse? "a" [opt [#a]])
    (
        wa: [#a]
        uparse? "a" [opt wa]
    )
    (uparse? "a" [opt <any>])
    (uparse? "abc" [<any> opt #b <any>])
]

[
    (uparse? #{} [opt blank])
    (uparse? #{} [opt #{0A}])
    (uparse? #{0A} [opt #{0A}])
    (uparse? #{0A} [opt #{0B} #{0A}])
    (uparse? #{0A} [opt [#{0A}]])
    (
        wa: [#{0A}]
        uparse? #{0A} [opt wa]
    )
    (uparse? #{0A} [opt <any>])
    (uparse? #{0A0B0C} [<any> opt #{0B} <any>])
]
