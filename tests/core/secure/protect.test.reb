; functions/secure/protect.r
; bug#1748
; block
[
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? try [insert value 4]
        equal? value original
    ]
]
[
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? try [append value 4]
        equal? value original
    ]
]
[
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? try [change value 4]
        equal? value original
    ]
]
[
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? try [reduce/into [4 + 5] value]
        equal? value original
    ]
]
[
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? try [compose/into [(4 + 5)] value]
        equal? value original
    ]
]
[
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? try [poke value 1 4]
        equal? value original
    ]
]
[
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? try [remove/part value 1]
        equal? value original
    ]
]
[
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? try [take value]
        equal? value original
    ]
]
[
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? try [reverse value]
        equal? value original
    ]
]
[
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? try [clear value]
        equal? value original
    ]
]
; string
[
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? try [insert value 4]
        equal? value original
    ]
]
[
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? try [append value 4]
        equal? value original
    ]
]
[
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? try [change value 4]
        equal? value original
    ]
]
[
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? try [poke value 1 4]
        equal? value original
    ]
]
[
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? try [remove/part value 1]
        equal? value original
    ]
]
[
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? try [take value]
        equal? value original
    ]
]
[
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? try [reverse value]
        equal? value original
    ]
]
[
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? try [clear value]
        equal? value original
    ]
]
; bug#1764
[unset 'blk protect/deep 'blk true]
[unprotect 'blk true]
