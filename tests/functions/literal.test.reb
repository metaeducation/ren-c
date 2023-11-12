; %literal.test.reb
;
; ^META arguments can tell the difference between the end condition, a null,
; and a nihil`

[
    (did detector: lambda [^x [<opt> <end> <void> pack? any-value!]] [x])

    ((the '10) = detector 10)
    (null' = detector null)
    ('~[~null~]~ = detector if true [null])

    (nihil' = detector (comment "hi"))
    (null = detector)

    (did left-detector: enfix :detector)

    ((the '1) = (1 left-detector))
    (null = left-detector)
    (null = (left-detector))
]

(
    x: false
    ^(void) then [x: true]
    x
)
