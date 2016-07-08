; functions/string/decloak.r
; bug#48
[
    a: compress "a"
    b: encloak a "a"
    equal? a decloak b "a"
]
