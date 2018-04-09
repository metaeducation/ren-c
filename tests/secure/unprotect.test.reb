; functions/secure/unprotect.r
; block
[#1748 (
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? try [insert value 4]
)]
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? try [append value 4]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? try [change value 4]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? try [reduce/into [4 + 5] value]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? try [compose/into [(4 + 5)] value]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? try [poke value 1 4]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? try [remove/part value 1]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? try [take value]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? try [reverse value]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? try [clear value]
)
; string
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? try [insert value 4]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? try [append value 4]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? try [change value 4]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? try [poke value 1 4]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? try [remove/part value 1]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? try [take value]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? try [reverse value]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? try [clear value]
)
