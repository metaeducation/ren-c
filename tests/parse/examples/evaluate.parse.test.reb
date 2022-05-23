; %evaluate.parse.test.reb
;
; Reimagination of R3-Alpha's DO, which let you treat the input block as code
; and advance through a step of evaluation.
;
; https://forum.rebol.info/t/replacing-r3-parses-do-rule-w-uparses-evaluate/1478

[
    (uparse-evaluate: combinator [
        {Run the evaluator one step to advance input, and produce a result}
        return: "Result of one evaluation step"
            [<opt> any-value!]
    ][
        if tail? input [return null]

        return [# (remainder)]: evaluate input
    ]
    true)

(
    keeper-saver: func [
        return: [block!]
        saved: [block!]

        input [block!]
        <local> mode value
    ][
        set saved copy []

        mode: #save
        uparse input [collect [
            some [
                mode: ['<K> (#keep) | '<S> (#save) | tag! (fail "BAD MODE")]
                |
                [',]  ; skip over commas
                |
                [value: uparse-evaluate] [
                    :(mode = #keep) keep ^ (value)
                    |
                    :(mode = #save) (if did value [append get saved ^value])
                ]
            ]
        ]]
    ]
)

    (did all [
        [35 13 23] = [k s]: keeper-saver [
            1 + 2
            <K> (3 + 4) * 5 if true [6 + 7]
            <S> 7 + 8, if false [9 + 10] else ["save me!"] <K>, 11 + 12
        ]
        k = [35 13 23]
        s = [3 15 "save me!"]
    ])
]
