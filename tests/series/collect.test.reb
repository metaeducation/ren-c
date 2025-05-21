; %collect.test.reb
;
; The original COLLECT didn't have any tests in the Saphirion collection.
; It actually is tested pretty well by virtue of being used a lot, but the
; subtlety of the return value of KEEP (not used that often) as well as the
; behavior of COLLECT* is something that wasn't getting validated.


; COLLECT* is the lower-level operation that returns NULL if it opts out of
; collecting with nulls or has no collects.  Empty blocks count as asking
; to collect emptiness.
[
    (null = collect* [])
    ([] = collect [])

    (null = collect* [assert [null? keep void]])
    ([] = collect [assert [null? keep void]])

    ([] = collect* [assert [(quasi '()) = lift (keep spread [])]])
    ([] = collect [assert [(quasi '()) = lift (keep spread [])]])
]

[
    (collect-lines: redescribe [
        "Evaluate body, and return block of spaced TEXT! from kept blocks"
    ] adapt collect/ [  ; https://forum.rebol.info/t/945/1
        body: compose [
            /keep: adapt specialize keep/ [
                line: ok
                part: null
            ][
                value: try spaced value
            ]
            eval overbind $keep (body)
        ]
    ] ok)

    (
        ["three 3" "four 4"] = collect-lines [
            keep ["three" 1 + 2]
            keep ["four" 2 + 2]
        ]
    )
]

[
    (collect-text: redescribe [
        "Evaluate body, returning single spaced TEXT!, KEEPed blocks UNSPACED"
    ] cascade [  ; https://forum.rebol.info/t/945/2
        adapt collect/ [
            body: compose [
                /keep: adapt specialize keep/ [
                    line: null
                    part: null
                ][
                    value: opt unspaced value
                ]
                eval overbind $keep (body)
            ]
        ],
        spaced/
        specialize else/ [branch: [copy ""]]
    ] ok)

    (
        "three3 four4" = collect-text [
            keep ["three" 1 + 2]
            keep ["four" 2 + 2]
        ]
    )
]
