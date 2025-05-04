; %literal.test.reb
;
; ^META arguments can tell the difference between the end condition, a null,
; and a nihil`

[
    (did detector: lambda [^x [<end> any-value? pack!]] [x])

    ((the '10) = detector 10)
    ((meta null) = detector null)
    ('~[~null~]~ = detector if ok [null])

    ('~,~ = detector (comment "hi"))
    (null = detector)

    (did left-detector: infix detector/)

    ((the '1) = (1 left-detector))
    (null = left-detector)
    (null = (left-detector))
]

(
    x: 'false
    ^(meta void) then [x: 'true]
    true? x
)
