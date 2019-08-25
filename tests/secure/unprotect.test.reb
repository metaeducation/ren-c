; functions/secure/unprotect.r
; block
[#1748 (
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? trap [insert value [4]]
)]
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? trap [append value [4]]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? trap [change value [4]]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? trap [poke value 1 4]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? trap [remove/part value 1]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? trap [take value]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? trap [reverse value]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    unprotect value
    not error? trap [clear value]
)
; string
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? trap [insert value 4]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? trap [append value 4]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? trap [change value 4]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? trap [poke value 1 4]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? trap [remove/part value 1]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? trap [take value]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? trap [reverse value]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    unprotect value
    not error? trap [clear value]
)
