; %literal.test.reb
;
; Literal arguments allow callees the ability to distinguish the
; NULL, ~_~ isotope, and END states.

[
    (did detector: lambda [^x [<opt> <end> <void> any-value!]] [x])

    ((the '10) = detector 10)
    (null' = detector null)
    ('~_~ = detector if true [null])

    (void' = detector (comment "hi"))
    (void' = detector)

    (did left-detector: enfixed :detector)

    ((the '1) = (1 left-detector))
    (void' = left-detector)
    (void' = (left-detector))
]

(
    x: false
    ^(void) then [x: true]
    x
)
