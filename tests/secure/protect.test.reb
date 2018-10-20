; functions/secure/protect.r
; block
[#1748 (
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? trap [insert value 4]
        value == original
    ]
)]
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? trap [append value 4]
        value == original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? trap [change value 4]
        value == original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? trap [poke value 1 4]
        value == original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? trap [remove/part value 1]
        value == original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? trap [take value]
        value == original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? trap [reverse value]
        value == original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? trap [clear value]
        value == original
    ]
)
; string
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? trap [insert value 4]
        value == original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? trap [append value 4]
        value == original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? trap [change value 4]
        value == original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? trap [poke value 1 4]
        value == original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? trap [remove/part value 1]
        value == original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? trap [take value]
        value == original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? trap [reverse value]
        value == original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? trap [clear value]
        value == original
    ]
)
[#1764
    (unset 'blk protect/deep 'blk true)
]
(unprotect 'blk true)
