; %fence.test.reb
;
; While FENCE! defaults to running CONSTRUCT, you can override it in
; local environments to do something else, by defining fence!-EVAL:

(
    fence!-EVAL: lambda [rule [fence!]] [
        rule: as block! rule
        parse data [accept [rule, elide data: <here>]]
    ]

    designer: {some word!, one}
    occurrence: {text!, elide 'Over}
    other: {collect [
        keep one, keep ('Intellectually) keep spread across to <end>
    ]}

    all [
        designer = @Fork
        occurrence = "Quantum Leaped"
        other = [The Intellectually Lazy @Red]
    ] 
)
