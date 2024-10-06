; functions/secure/unprotect.r
; block
[#1748 (
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    [1 + 2 + 3] = insert value 4
)]
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    [1 + 2 + 3 4] = append value 4

)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    [+ 2 + 3] = change value 4
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    4 = poke value 1 4
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    [+ 2 + 3] = remove:part value 1
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    1 = take value
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    [3 + 2 + 1] = reverse value
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    []  = clear value
)
; string
(
    value: copy original: "1 + 2 + 3"
    protect value
    unprotect value
    "1 + 2 + 3" = insert value 4
)
(
    value: copy original: "1 + 2 + 3"
    protect value
    unprotect value
    "1 + 2 + 34" = append value 4
)
(
    value: copy original: "1 + 2 + 3"
    protect value
    unprotect value
    " + 2 + 3" = change value 4
)
(
    value: copy original: "1 + 2 + 3"
    protect value
    unprotect value
    4 = poke value 1 4
)
(
    value: copy original: "1 + 2 + 3"
    protect value
    unprotect value
    " + 2 + 3" = remove:part value 1
)
(
    value: copy original: "1 + 2 + 3"
    protect value
    unprotect value
    #1 = take value
)
(
    value: copy original: "1 + 2 + 3"
    protect value
    unprotect value
    "3 + 2 + 1" = reverse value
)
(
    value: copy original: "1 + 2 + 3"
    protect value
    unprotect value
    "" = clear value
)
