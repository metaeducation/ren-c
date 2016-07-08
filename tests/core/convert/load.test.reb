; functions/convert/load.r
; bug#20
[block? load/all "1"]
; bug#22a
[error? try [load "':a"]]
; bug#22b
[error? try [load "':a:"]]
; bug#858
[
    a: [ < ]
    a = load mold a
]
[error? try [load "1xyz#"]]
; load/next
[error? try [load/next "1"]]
; bug#1122
[
    any [
        error? try [load "9999999999999999999"]
        greater? load "9999999999999999999" load "9223372036854775807"
    ]
]
; R2 bug
[
     x: 1
     error? try [x: load/header ""]
     not error? x
]
