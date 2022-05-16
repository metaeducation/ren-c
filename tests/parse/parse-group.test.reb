; %parse-group.test.reb
;
; GROUP! are value-bearing rules that do not advance the input and get their
; argument literally from a DO evaluation.  They always succeed.

(10 = uparse [aaa] ['aaa (10)])
(null = uparse [aaa] [(10)])

(
    three: 3
    did all [
        3 == uparse "" [x: (three)]
        x = 3
    ]
)
(
    did all [
        3 == uparse "" [x: (1 + 2)]
        x = 3
    ]
)
