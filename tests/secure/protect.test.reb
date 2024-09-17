; functions/secure/protect.r
; block
[#1748 (
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? sys/util/rescue [insert value 4]
        equal? value original
    ]
)]
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? sys/util/rescue [append value 4]
        equal? value original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? sys/util/rescue [change value 4]
        equal? value original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? sys/util/rescue [poke value 1 4]
        equal? value original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? sys/util/rescue [remove/part value 1]
        equal? value original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? sys/util/rescue [take value]
        equal? value original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? sys/util/rescue [reverse value]
        equal? value original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? sys/util/rescue [clear value]
        equal? value original
    ]
)
; string
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? sys/util/rescue [insert value 4]
        equal? value original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? sys/util/rescue [append value 4]
        equal? value original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? sys/util/rescue [change value 4]
        equal? value original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? sys/util/rescue [poke value 1 4]
        equal? value original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? sys/util/rescue [remove/part value 1]
        equal? value original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? sys/util/rescue [take value]
        equal? value original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? sys/util/rescue [reverse value]
        equal? value original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? sys/util/rescue [clear value]
        equal? value original
    ]
)
[#1764
    (blk: ~ protect/deep 'blk true)
]
(unprotect 'blk true)
