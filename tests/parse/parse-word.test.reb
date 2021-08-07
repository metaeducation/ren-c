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
    (uparse? [a] [wa])
    (not uparse? [a] [wb])
    (uparse? [a b] [wa wb])
    (uparse? [a #b] [wa wcb])
    (uparse? [a] [wra])
    (uparse? [a b] [wra 'b])
    (uparse? [a b] ['a wrb])
    (uparse? [a b] [wra wrb])
    (uparse? ["hello"] [wh])
    (uparse? [#a] [wcb | wca])
    (not uparse? [a b] [wb | wa])
    (uparse? [#a] [[wcb | wca]])
    (not uparse? [a b] [wrba])
    (uparse? [a b] [wrab wrba])
    (uparse? [a 123] [wa integer!])
    (not uparse? [a 123] [wa char!])
    (uparse? [a 123] [wra [integer!]])
    (not uparse? [a 123] [wa [char!]])
    (
        res: ~
        did all [
            uparse? [a] [wa (res: 1)]
            res = 1
        ]
    )
    (
        res: '~before~
        did all [
            not uparse? [a] [wb (res: 1)]
            res = '~before~
        ]
    )
    (
        res: ~
        wres: [(res: 1)]
        did all [
            uparse? [] [wres]
            res = 1
        ]
    )
    (
        res: ~
        wres: ['a (res: 1)]
        did all [
            uparse? [a] [wres]
            res = 1
        ]
    )
    (
        res: '~before~
        wres: ['b (res: 1)]
        did all [
            not uparse? [a] [wres]
            res = '~before~
        ]
    )
    (
        res: ~
        wres: [char! (res: 2) | integer! (res: 3)]
        did all [
            uparse? [a 123] [wa (res: 1) wres]
            res = 3
        ]
    )
    (
        res: ~
        wres: [char! (res: 2) | text! (res: 3)]
        did all [
            not uparse? [a 123] [wa (res: 1) wres]
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
    (uparse? "a" [wa])
    (not uparse? "a" [wb])
    (uparse? "ab" [wa wb])
    (uparse? "a" [wra])
    (uparse? "ab" [wra #b])
    (uparse? "ab" [#a wrb])
    (uparse? "ab" [wra wrb])
    (uparse? "hello" [wh])
    (uparse? "a" [wcb | wca])
    (not uparse? "ab" [wb | wa])
    (uparse? "a" [[wcb | wca]])
    (not uparse? "ab" [wrba])
    (uparse? "ab" [wrab wrba])
    (
        res: ~
        did all [
            uparse? "a" [wa (res: 1)]
            res = 1
        ]
    )
    (
        res: '~before~
        did all [
            not uparse? "a" [wb (res: 1)]
            res = '~before~
        ]
    )
    (
        res: ~
        wres: [(res: 1)]
        did all [
            uparse? "" [wres]
            res = 1
        ]
    )
    (
        res: ~
        wres: [#a (res: 1)]
        did all [
            uparse? "a" [wres]
            res = 1
        ]
    )
    (
        res: '~before~
        wres: [#b (res: 1)]
        did all [
            not uparse? "a" [wres]
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
    (uparse? #{0A} [wa])
    (not uparse? #{0A} [wb])
    (uparse? #{0A0B} [wa wb])
    (uparse? #{0A} [wra])
    (uparse? #{0A0B} [wra #{0B}])
    (uparse? #{0A0B} [#{0A} wrb])
    (uparse? #{0A0B} [wra wrb])
    (uparse? #{88031100} [wh])
    (uparse? #{0A} [wcb | wca])
    (not uparse? #{0A0B} [wb | wa])
    (uparse? #{0A} [[wcb | wca]])
    (not uparse? #{0A0B} [wrba])
    (uparse? #{0A0B} [wrab wrba])
    (
        res: ~
        did all [
            uparse? #{0A} [wa (res: 1)]
            res = 1
        ]
    )
    (
        res: '~before~
        did all [
            not uparse? #{0A} [wb (res: 1)]
            res = '~before~
        ]
    )
    (
        res: ~
        wres: [(res: 1)]
        did all [
            uparse? #{} [wres]
            res = 1
        ]
    )
    (
        res: ~
        wres: [#{0A} (res: 1)]
        did all [
            uparse? #{0A} [wres]
            res = 1
        ]
    )
    (
        res: '~before~
        wres: [#{0B} (res: 1)]
        did all [
            not uparse? #{0A} [wres]
            res = '~before~
        ]
    )
]
