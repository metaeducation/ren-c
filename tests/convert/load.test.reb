; functions/convert/load.r
[#20
    (block? load/all "1")
]
[#22 ; a
    (error? sys/util/rescue [load "':a"])
]
[#22 ; b
    (error? sys/util/rescue [load "':a:"])
]
[#858 (
    a: [ < ]
    a = load mold a
)]
(error? sys/util/rescue [load "1xyz#"])

; LOAD/NEXT removed, see #1703
;
(error? sys/util/rescue [load/next "1"])
(all [
    #{} = transcode/next3 to binary! "1" 'value
    value = 1
])

[#1122 (
    any [
        error? sys/util/rescue [load "9999999999999999999"]
        greater? load "9999999999999999999" load "9223372036854775807"
    ]
)]
; R2 bug
(
     x: 1
     error? sys/util/rescue [x: load/header ""]
     not error? x
)

[#1421 (
    all [
        error? sys/util/rescue [load "[a<]"]
        error? sys/util/rescue [load "[a>]"]
        error? sys/util/rescue [load "[a+<]"]
        error? sys/util/rescue [load "[1<]"]
        error? sys/util/rescue [load "[+<]"]
    ]
)]

;=== BOOTSTRAP SCANNER MODIFICATIONS ===

; Stripping out commas
[
    ([[]] = transcode "[, ]")
    ([[a b c]] = transcode "[a, b,, c,]")
]

; Stripping leading dots
[
    ([abc] = transcode ".abc")
    ([abc/def] = transcode ".abc/def")
]

; Stripping leading slashes (if not refinement)
[
    ([abc/def] = transcode "/abc/def")
]

; Keeping trailing slashes
[
    ([a/] = transcode "a/")
    ([a/] = transcode ".a/")
    ([a/b/] = transcode "a.b/")
]

; Turning tuples into paths
[
    ([abc/def] = transcode "abc.def")
    ([abc/def/ghi.txt] = transcode "abc/def/ghi.txt")
]

; Leading slash indicates path desired
[
    ([abc/def:] = transcode "/abc.def:")
]
