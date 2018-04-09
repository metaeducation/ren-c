; functions/convert/load.r
[#20
    (block? load/all "1")
]
[#22 ; a
    (error? try [load "':a"])
]
[#22 ; b
    (error? try [load "':a:"])
]
[#858 (
    a: [ < ]
    a = load mold a
)]
(error? try [load "1xyz#"])

; LOAD/NEXT removed, see #1703
;
(error? try [load/next "1"])
([1 #{}] = transcode/next to binary! "1")

[#1122 (
    any [
        error? try [load "9999999999999999999"]
        greater? load "9999999999999999999" load "9223372036854775807"
    ]
)]
; R2 bug
(
     x: 1
     error? try [x: load/header ""]
     not error? x
)

[#1421 (
    did all [
        error? try [load "[a<]"]
        error? try [load "[a>]"]
        error? try [load "[a+<]"]
        error? try [load "[1<]"]
        error? try [load "[+<]"]
    ]
)]
