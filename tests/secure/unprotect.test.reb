; functions/secure/unprotect.r
; block
[#1748 (
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? sys/util/rescue [insert value 4]
)]
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? sys/util/rescue [append value 4]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? sys/util/rescue [change value 4]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? sys/util/rescue [poke value 1 4]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? sys/util/rescue [remove/part value 1]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? sys/util/rescue [take value]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? sys/util/rescue [reverse value]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? sys/util/rescue [clear value]
)
; string
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? sys/util/rescue [insert value 4]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? sys/util/rescue [append value 4]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? sys/util/rescue [change value 4]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? sys/util/rescue [poke value 1 4]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? sys/util/rescue [remove/part value 1]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? sys/util/rescue [take value]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? sys/util/rescue [reverse value]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? sys/util/rescue [clear value]
)
