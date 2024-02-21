; %parse-word.test.reb
;
; There is actually a WORD! combinator which is dispatched to when a
; word is not a keyword.  (Dispatching keywords would require combinators
; to be variadic, which raises large technical questions.)
;
; Hooking the WORD! combinator can be used for interesting trace effects.

; BLOCK! tests from %parse-test.red
[
    (
        wa: ['a]
        wb: ['b]
        wca: #a
        wcb: #b
        wra: [wa]
        wrb: [wb]
        wh: "hello"
        wrab: ['a | 'b]
        wrba: ['b | 'a]
        true
    )
    ('a == parse [a] [wa])
    ~parse-mismatch~ !! (parse [a] [wb])
    ('b == parse [a b] [wa wb])
    (#b == parse [a #b] [wa wcb])
    ('a == parse [a] [wra])
    ('b == parse [a b] [wra 'b])
    ('b == parse [a b] ['a wrb])
    ('b == parse [a b] [wra wrb])
    ("hello" == parse ["hello"] [wh])
    (#a == parse [#a] [wcb | wca])
    ~parse-incomplete~ !! (parse [a b] [wb | wa])
    (#a == parse [#a] [[wcb | wca]])
    ~parse-incomplete~ !! (parse [a b] [wrba])
    ('b == parse [a b] [wrab wrba])
    (123 == parse [a 123] [wa integer!])
    ~parse-mismatch~ !! (parse [a 123] [wa char?!])
    (123 == parse [a 123] [wra [integer!]])
    ~parse-mismatch~ !! (parse [a 123] [wa [char?!]])
    (
        res: ~
        all [
            1 == parse [a] [wa (res: 1)]
            res = 1
        ]
    )
    (
        res: '~before~
        all [
            raised? parse [a] [wb (res: 1)]
            res = '~before~
        ]
    )
    (
        res: ~
        wres: [(res: 1)]
        all [
            1 == parse [] [wres]
            res = 1
        ]
    )
    (
        res: ~
        wres: ['a (res: 1)]
        all [
            1 == parse [a] [wres]
            res = 1
        ]
    )
    (
        res: '~before~
        wres: ['b (res: 1)]
        all [
            raised? parse [a] [wres]
            res = '~before~
        ]
    )
    (
        res: ~
        wres: [char?! (res: 2) | integer! (res: 3)]
        all [
            3 == parse [a 123] [wa (res: 1) wres]
            res = 3
        ]
    )
    (
        res: ~
        wres: [char?! (res: 2) | text! (res: 3)]
        all [
            raised? parse [a 123] [wa (res: 1) wres]
            res = 1
        ]
    )
]

; TEXT! tests from %parse-test.red
[
    (
        wa: [#a]
        wb: [#b]
        wca: #a
        wcb: #b
        wra: [wa]
        wrb: [wb]
        wh: "hello"
        wrab: [#a | #b]
        wrba: [#b | #a]
        true
    )
    (#a == parse "a" [wa])
    ~parse-mismatch~ !! (parse "a" [wb])
    (#b == parse "ab" [wa wb])
    (#a == parse "a" [wra])
    (#b == parse "ab" [wra #b])
    (#b == parse "ab" [#a wrb])
    (#b == parse "ab" [wra wrb])
    ("hello" == parse "hello" [wh])
    (#a == parse "a" [wcb | wca])
    ~parse-incomplete~ !! (parse "ab" [wb | wa])
    (#a == parse "a" [[wcb | wca]])
    ~parse-incomplete~ !! (parse "ab" [wrba])
    (#b == parse "ab" [wrab wrba])
    (
        res: ~
        all [
            1 == parse "a" [wa (res: 1)]
            res = 1
        ]
    )
    (
        res: '~before~
        all [
            raised? parse "a" [wb (res: 1)]
            res = '~before~
        ]
    )
    (
        res: ~
        wres: [(res: 1)]
        all [
            1 == parse "" [wres]
            res = 1
        ]
    )
    (
        res: ~
        wres: [#a (res: 1)]
        all [
            1 == parse "a" [wres]
            res = 1
        ]
    )
    (
        res: '~before~
        wres: [#b (res: 1)]
        all [
            raised? parse "a" [wres]
            res = '~before~
        ]
    )
]

; BINARY! tests from %parse-test.red
[
    (
        wa: [#{0A}]
        wb: [#{0B}]
        wca: #{0A}
        wcb: #{0B}
        wra: [wa]
        wrb: [wb]
        wh: #{88031100}
        wrab: [#{0A} | #{0B}]
        wrba: [#{0B} | #{0A}]
        true
    )
    (#{0A} == parse #{0A} [wa])
    ~parse-mismatch~ !! (parse #{0A} [wb])
    (#{0B} == parse #{0A0B} [wa wb])
    (#{0A} == parse #{0A} [wra])
    (#{0B} == parse #{0A0B} [wra #{0B}])
    (#{0B} == parse #{0A0B} [#{0A} wrb])
    (#{0B} == parse #{0A0B} [wra wrb])
    (#{88031100} == parse #{88031100} [wh])
    (#{0A} == parse #{0A} [wcb | wca])
    ~parse-incomplete~ !! (parse #{0A0B} [wb | wa])
    (#{0A} == parse #{0A} [[wcb | wca]])
    ~parse-incomplete~ !! (parse #{0A0B} [wrba])
    (#{0B} == parse #{0A0B} [wrab wrba])
    (
        res: ~
        all [
            1 == parse #{0A} [wa (res: 1)]
            res = 1
        ]
    )
    (
        res: '~before~
        all [
            raised? parse #{0A} [wb (res: 1)]
            res = '~before~
        ]
    )
    (
        res: ~
        wres: [(res: 1)]
        all [
            1 == parse #{} [wres]
            res = 1
        ]
    )
    (
        res: ~
        wres: [#{0A} (res: 1)]
        all [
            1 == parse #{0A} [wres]
            res = 1
        ]
    )
    (
        res: '~before~
        wres: [#{0B} (res: 1)]
        all [
            raised? parse #{0A} [wres]
            res = '~before~
        ]
    )
]
