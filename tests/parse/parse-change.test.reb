; %parse-change.test.reb
;
; CHANGE is rethought in UPARSE to work with value-bearing rules.  The rule
; gets the same input that the first argument did.

(
    str: "aaa"
    did all [
        uparse? str [change [some "a"] (if true ["literally"])]
        str = "literally"
    ]
)

(
    str: "(aba)"
    did all [
        uparse? str [
            "("
            change [to ")"] [
                collect [
                    some ["a" keep ("A") | <any>]
                ]
            ]
            ")"
        ]
        str = "(AA)"
    ]
)

(
    s: {a}
    did all [
        null = uparse s [opt change "b" ("x")]
        s = {a}
    ]
)

(
    s: {aba}
    did all [
        '~changed~ = ^ uparse s [while [
            opt change "b" ("x")
            skip
        ]]
        s = {axa}
    ]
)

