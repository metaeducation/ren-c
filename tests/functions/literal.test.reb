; %literal.test.reb
;
; Literal arguments allow callees the ability to distinguish the
; NULL, ~null~ isotope, and END states.

[
    (did detector: func [^x [<opt> <end> any-value!]] [get/any 'x])

    ((the '10) = detector 10)
    (null = detector null)
    ('~null~ = detector if true [null])

    (_ = detector (comment "hi"))
    ('~end~ = detector)

    (did left-detector: enfixed :detector)

    ((the '1) = (1 left-detector))
    ('~end~ = left-detector)
    ('~end~ = (left-detector))
]

(
    x: false
    ^(void) then [x: true]
    x
)
