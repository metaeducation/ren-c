; functions/convert/load.r
[#20
    (block? load/all "1")
]
[#22 ; a
    (error? trap [load "':a"])
]
[#22 ; b
    (error? trap [load "':a:"])
]
[#858 (
    a: [ < ]
    a == load mold a
)]
(error? trap [load "1xyz#"])

; LOAD/NEXT removed, see #1703
;
(error? trap [load/next "1"])
([1 #{}] == transcode/next to binary! "1")

[#1122 (
    any [
        error? trap [load "9999999999999999999"]
        greater? load "9999999999999999999" load "9223372036854775807"
    ]
)]
; R2 bug
(
     x: 1
     error? trap [x: load/header ""]
     not error? x
)

[#1421 (
    did all [
        error? trap [load "[a<]"]
        error? trap [load "[a>]"]
        error? trap [load "[a+<]"]
        error? trap [load "[1<]"]
        error? trap [load "[+<]"]
    ]
)]
