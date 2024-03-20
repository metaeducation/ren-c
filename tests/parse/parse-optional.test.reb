; %parse-optional.test.reb
;
; OPTIONAL (shorthand OPT) passes through the synthesized product of the parser
; it calls on success, but on failure it synthesizes NULL and continues the
; parse rules from the current position.

[
    (null == parse [] [optional blank])
    (null == parse [] [optional 'a])
    ('a == parse [a] [opt 'a])
    ('a == parse [a] [opt 'b 'a])
    ('a == parse [a] [opt ['a]])
    (
        wa: ['a]
        'a == parse [a] [optional wa]
    )
    ('a == parse [a] [optional <any>])
    ('c == parse [a b c] [<any> opt 'b <any>])
]

[
    (null == parse "" [optional blank])
    (null == parse "" [optional #a])
    (#a == parse "a" [opt #a])
    (#a == parse "a" [opt #b #a])
    (#a == parse "a" [opt [#a]])
    (
        wa: [#a]
        #a == parse "a" [optional wa]
    )
    (#a == parse "a" [opt <any>])
    (#c == parse "abc" [<any> opt #b <any>])
]

[
    (null == parse #{} [optional blank])
    (null == parse #{} [optional #{0A}])
    (#{0A} == parse #{0A} [opt #{0A}])
    (#{0A} == parse #{0A} [opt #{0B} #{0A}])
    (#{0A} == parse #{0A} [opt [#{0A}]])
    (
        wa: [#{0A}]
        #{0A} == parse #{0A} [optional wa]
    )
    (10 == parse #{0A} [optional <any>])
    (12 == parse #{0A0B0C} [<any> opt #{0B} <any>])
]

[https://gitter.im/red/bugs?at=638e27b34cb5585f9666500d (
    not ok? parse [1] [optional (x: true)]
    x = true
)]
