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
    ('a == uparse [a] [wa])
    (didn't uparse [a] [wb])
    ('b == uparse [a b] [wa wb])
    (#b == uparse [a #b] [wa wcb])
    ('a == uparse [a] [wra])
    ('b == uparse [a b] [wra 'b])
    ('b == uparse [a b] ['a wrb])
    ('b == uparse [a b] [wra wrb])
    ("hello" == uparse ["hello"] [wh])
    (#a == uparse [#a] [wcb | wca])
    (didn't uparse [a b] [wb | wa])
    (#a == uparse [#a] [[wcb | wca]])
    (didn't uparse [a b] [wrba])
    ('b == uparse [a b] [wrab wrba])
    (123 == uparse [a 123] [wa integer!])
    (didn't uparse [a 123] [wa char!])
    (123 == uparse [a 123] [wra [integer!]])
    (didn't uparse [a 123] [wa [char!]])
    (
        res: ~
        did all [
            1 == uparse [a] [wa (res: 1)]
            res = 1
        ]
    )
    (
        res: '~before~
        did all [
            didn't uparse [a] [wb (res: 1)]
            res = '~before~
        ]
    )
    (
        res: ~
        wres: [(res: 1)]
        did all [
            1 == uparse [] [wres]
            res = 1
        ]
    )
    (
        res: ~
        wres: ['a (res: 1)]
        did all [
            1 == uparse [a] [wres]
            res = 1
        ]
    )
    (
        res: '~before~
        wres: ['b (res: 1)]
        did all [
            didn't uparse [a] [wres]
            res = '~before~
        ]
    )
    (
        res: ~
        wres: [char! (res: 2) | integer! (res: 3)]
        did all [
            3 == uparse [a 123] [wa (res: 1) wres]
            res = 3
        ]
    )
    (
        res: ~
        wres: [char! (res: 2) | text! (res: 3)]
        did all [
            didn't uparse [a 123] [wa (res: 1) wres]
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
    (#a == uparse "a" [wa])
    (didn't uparse "a" [wb])
    (#b == uparse "ab" [wa wb])
    (#a == uparse "a" [wra])
    (#b == uparse "ab" [wra #b])
    (#b == uparse "ab" [#a wrb])
    (#b == uparse "ab" [wra wrb])
    ("hello" == uparse "hello" [wh])
    (#a == uparse "a" [wcb | wca])
    (didn't uparse "ab" [wb | wa])
    (#a == uparse "a" [[wcb | wca]])
    (didn't uparse "ab" [wrba])
    (#b == uparse "ab" [wrab wrba])
    (
        res: ~
        did all [
            1 == uparse "a" [wa (res: 1)]
            res = 1
        ]
    )
    (
        res: '~before~
        did all [
            didn't uparse "a" [wb (res: 1)]
            res = '~before~
        ]
    )
    (
        res: ~
        wres: [(res: 1)]
        did all [
            1 == uparse "" [wres]
            res = 1
        ]
    )
    (
        res: ~
        wres: [#a (res: 1)]
        did all [
            1 == uparse "a" [wres]
            res = 1
        ]
    )
    (
        res: '~before~
        wres: [#b (res: 1)]
        did all [
            didn't uparse "a" [wres]
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
    (#{0A} == uparse #{0A} [wa])
    (didn't uparse #{0A} [wb])
    (#{0B} == uparse #{0A0B} [wa wb])
    (#{0A} == uparse #{0A} [wra])
    (#{0B} == uparse #{0A0B} [wra #{0B}])
    (#{0B} == uparse #{0A0B} [#{0A} wrb])
    (#{0B} == uparse #{0A0B} [wra wrb])
    (#{88031100} == uparse #{88031100} [wh])
    (#{0A} == uparse #{0A} [wcb | wca])
    (didn't uparse #{0A0B} [wb | wa])
    (#{0A} == uparse #{0A} [[wcb | wca]])
    (didn't uparse #{0A0B} [wrba])
    (#{0B} == uparse #{0A0B} [wrab wrba])
    (
        res: ~
        did all [
            1 == uparse #{0A} [wa (res: 1)]
            res = 1
        ]
    )
    (
        res: '~before~
        did all [
            didn't uparse #{0A} [wb (res: 1)]
            res = '~before~
        ]
    )
    (
        res: ~
        wres: [(res: 1)]
        did all [
            1 == uparse #{} [wres]
            res = 1
        ]
    )
    (
        res: ~
        wres: [#{0A} (res: 1)]
        did all [
            1 == uparse #{0A} [wres]
            res = 1
        ]
    )
    (
        res: '~before~
        wres: [#{0B} (res: 1)]
        did all [
            didn't uparse #{0A} [wres]
            res = '~before~
        ]
    )
]
