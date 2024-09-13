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

    ([] = collect* [assert [(quasi '()) = ^(keep spread [])]])
    ([] = collect [assert [(quasi '()) = ^(keep spread [])]])
]

[
    (collect-lines: redescribe [
        {Evaluate body, and return block of values collected via KEEP function.
        KEEPed blocks become spaced TEXT!.}
    ] adapt get $collect [  ; https://forum.rebol.info/t/945/1
        body: compose [
            keep: adapt specialize get $keep [
                line: ok
                part: null
            ][
                value: try spaced :value
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
        {Evaluate body, and return block of values collected via KEEP function.
        Returns all values as a single spaced TEXT!, individual KEEPed blocks get UNSPACED.}
    ] cascade [  ; https://forum.rebol.info/t/945/2
        adapt get $collect [
            body: compose [
                keep: adapt specialize get $keep [
                    line: null
                    part: null
                ][
                    value: maybe unspaced :value
                ]
                eval overbind $keep (body)
            ]
        ],
        get $spaced,
        specialize get $else [branch: [copy ""]]
    ] ok)

    (
        "three3 four4" = collect-text [
            keep ["three" 1 + 2]
            keep ["four" 2 + 2]
        ]
    )
]
