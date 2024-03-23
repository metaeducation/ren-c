; %parse-let.test.reb
;
; Analog to LET in normal code.
; Until variadic combinators exist, only the SET-WORD! version exists.
; Multi-return is also TBD.

; Check usages inside GROUP!
(
    x: 10
    all [
        #a = parse "a" [let x: one (x)]
        x = 10
    ]
)
(
    x: 10
    all [
        #a = parse "a" [let x: one [[(x)]]]
        x = 10
    ]
)

; Check usages in rules
(
    rule: "golden"
    all [
        'b = parse [['a 'b] a b] [let rule: one, rule]
        rule = "golden"
    ]
)
(
    rule: "golden"
    all [
        'b = parse [['a 'b] a b] [let rule: one [[rule]]]
        rule = "golden"
    ]
)

; https://forum.rebol.info/t/question-about-binding-in-parse/461
(
    [hello -hi-there-] = collect [
        function-rule: [
           subparse group! [
               let code: word!
               opt some [integer! | function-rule]
               (keep code)
           ]
        ]

        parse x: copy/deep [(-hi-there- 1 2 3 (hello 2 3 4))] function-rule
    ]
)
