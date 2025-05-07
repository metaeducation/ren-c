; %parse-optional.test.reb
;
; OPTIONAL (shorthand OPT) passes through the synthesized product of the parser
; it calls on success, but on failure it synthesizes NULL and continues the
; parse rules from the current position.

[
    (void = parse [] [optional blank])
    (void = parse [] [optional 'a])
    ('a = parse [a] [opt 'a])
    ('a = parse [a] [opt 'b 'a])
    ('a = parse [a] [opt ['a]])
    (
        wa: ['a]
        'a = parse [a] [optional wa]
    )
    ('a = parse [a] [optional one])
    ('c = parse [a b c] [one opt 'b one])
]

[
    (void = parse "" [optional blank])
    (void = parse "" [optional #a])
    (#a = parse "a" [opt #a])
    (#a = parse "a" [opt #b #a])
    (#a = parse "a" [opt [#a]])
    (
        wa: [#a]
        #a = parse "a" [optional wa]
    )
    (#a = parse "a" [opt one])
    (#c = parse "abc" [<next> opt #b one])
]

[
    (void = parse #{} [optional blank])
    (void = parse #{} [optional #{0A}])
    (#{0A} = parse #{0A} [opt #{0A}])
    (#{0A} = parse #{0A} [opt #{0B} #{0A}])
    (#{0A} = parse #{0A} [opt [#{0A}]])
    (
        wa: [#{0A}]
        #{0A} = parse #{0A} [optional wa]
    )
    (10 = parse #{0A} [optional one])
    (12 = parse #{0A0B0C} [one opt #{0B} one])
]

[https://gitter.im/red/bugs?at=638e27b34cb5585f9666500d (
    x: ~
    raised? parse [1] [optional (x: 'true)]
    x = 'true
)]
