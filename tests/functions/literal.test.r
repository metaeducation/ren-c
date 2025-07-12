; %literal.test.r
;
; ^META arguments can tell the difference between the end condition, a null,
; and a nihil`

[
    (did detector: lambda [^x [<end> any-stable? pack! ghost!]] [
        tweak $ '^x null
    ])

    ((the '10) = detector 10)
    ((lift null) = detector null)
    ('~[~null~]~ = detector if ok [null])

    ('~,~ = detector (comment "hi"))
    (tripwire? detector)

    (did left-detector: infix detector/)

    ((the '1) = (1 left-detector))
    (tripwire? left-detector)
    (tripwire? (left-detector))
]

(
    x: 'false
    ^(lift void) then [x: 'true]
    true? x
)
