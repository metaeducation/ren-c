; %parse-let.test.reb
;
; Analog to LET in normal code.
; Until variadic combinators exist, only the SET-WORD! version exists.
; Multi-return is also TBD.

; Check usages inside GROUP!
(
    x: 10
    did all [
        #a = parse "a" [let x: <any> (x)]
        x = 10
    ]
)
(
    x: 10
    did all [
        #a = parse "a" [let x: <any> [[(x)]]]
        x = 10
    ]
)

; Check usages in rules
(
    rule: "golden"
    did all [
        'b = parse [['a 'b] a b] [let rule: <any> rule]
        rule = "golden"
    ]
)
(
    rule: "golden"
    did all [
        'b = parse [['a 'b] a b] [let rule: <any> [[rule]]]
        rule = "golden"
    ]
)
