; %evaluate.parse.test.reb
;
; Reimagination of R3-Alpha's DO, which let you treat the input block as code
; and advance through a step of evaluation.
;
; https://forum.rebol.info/t/replacing-r3-parses-do-rule-w-parses-evaluate/1478

[
    (parse-evaluate: combinator [
        "Run the evaluator one step to advance input, and produce a result"
        return: "Result of one evaluation step"
            [any-value?]
    ][
        return [remainder {#}]: evaluate:step input else [
            fail "PARSE-EVALUATE attempted at series tail"
        ]
    ]
    ok)

(
    keeper-saver: func [
        return: "Saved as secondary result"
            [~[block! block!]~]
        input [block!]
        <local> mode value saved
    ][
        saved: copy []

        mode: #save
        return pack [parse input [collect [
            some [
                mode: ['<K> (#keep) | '<S> (#save) | tag! (panic "BAD MODE")]
                |
                [',]  ; skip over commas
                |
                [value: parse-evaluate] [
                    when (mode = #keep) keep (value)
                    |
                    when (mode = #save) (if did value [append saved value])
                ]
            ]
        ]], saved]
    ]
    ok
)

    (all wrap [
        [35 13 23] = [k s]: keeper-saver [
            1 + 2
            <K> (3 + 4) * 5 if ok [6 + 7]
            <S> 7 + 8, if null [9 + 10] else ["save me!"] <K>, 11 + 12
        ]
        k = [35 13 23]
        s = [3 15 "save me!"]
    ])
]
