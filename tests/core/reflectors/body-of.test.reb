; functions/reflectors/body-of.r
; bug#49
[
    f: func [] []
    not same? body-of :f body-of :f
]
