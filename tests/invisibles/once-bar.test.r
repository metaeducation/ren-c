; %once-bar.test.r
;
; ONCE-BAR was an experiment created to see if it could be done, and was
; thought about putting in the box.  Notationally it was || to correspond as
; a stronger version of |.  Not only was it not used, but since COMMA! has
; overtaken the | it no longer makes sense.
;
; Keeping as a test of the variadic feature it exercised.

[
    (
        weird: [|1|]
        all [
            word? first weird
            "|1|" = as text! first weird
            "[|1|]" = mold weird
        ]
    )

    (|1|: lambda [
        "Barrier that's willing to only run one expression after it"

        right [any-stable? <variadic>]
        'lookahead [element? <variadic>]
        <local> look
    ][
        take right  ; returned value

        elide any [
            tail? right,
            '|1| = look: take lookahead  ; hack...recognize selfs
        ] else [
            panic:blame [
                "|1| expected single expression, found residual of" @look
            ] $right
        ]
    ]
    ok)

    (7 = (1 + 2 |1| 3 + 4))
    ~???~ !! (1 + 2 |1| 3 + 4 5 + 6)
]
