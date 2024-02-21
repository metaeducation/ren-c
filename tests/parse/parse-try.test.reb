; %parse-try.test.reb
;
; TRY passes through the synthesized product of the parser it calls on success,
; but on failure it synthesizes NULL and continues the parse rules from the
; current position.
;
; Historically this was called OPT in Rebol2/R3-Alpha/Red PARSE.  However, OPT
; being an abbreviation was somewhat unpleasant-looking, and TRY lines up
; better with the meaning in regular code as well as matching how Haskell uses
; the word in Parsec.

[
    (null == parse [] [try blank])
    (null == parse [] [try 'a])
    ('a == parse [a] [try 'a])
    ('a == parse [a] [try 'b 'a])
    ('a == parse [a] [try ['a]])
    (
        wa: ['a]
        'a == parse [a] [try wa]
    )
    ('a == parse [a] [try <any>])
    ('c == parse [a b c] [<any> try 'b <any>])
]

[
    (null == parse "" [try blank])
    (null == parse "" [try #a])
    (#a == parse "a" [try #a])
    (#a == parse "a" [try #b #a])
    (#a == parse "a" [try [#a]])
    (
        wa: [#a]
        #a == parse "a" [try wa]
    )
    (#a == parse "a" [try <any>])
    (#c == parse "abc" [<any> try #b <any>])
]

[
    (null == parse #{} [try blank])
    (null == parse #{} [try #{0A}])
    (#{0A} == parse #{0A} [try #{0A}])
    (#{0A} == parse #{0A} [try #{0B} #{0A}])
    (#{0A} == parse #{0A} [try [#{0A}]])
    (
        wa: [#{0A}]
        #{0A} == parse #{0A} [try wa]
    )
    (10 == parse #{0A} [try <any>])
    (12 == parse #{0A0B0C} [<any> try #{0B} <any>])
]

[https://gitter.im/red/bugs?at=638e27b34cb5585f9666500d (
    not ok? parse [1] [try (x: true)]
    x = true
)]
