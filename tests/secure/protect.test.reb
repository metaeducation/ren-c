; functions/secure/protect.r
; block
[#1748 (
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? trap [insert value 4]
        equal? value original
    ]
)]
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? trap [append value 4]
        equal? value original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? trap [change value 4]
        equal? value original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? trap [poke value 1 4]
        equal? value original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? trap [remove/part value 1]
        equal? value original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? trap [take value]
        equal? value original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? trap [reverse value]
        equal? value original
    ]
)
(
    value: copy original: [1 + 2 + 3]
    protect value
    all [
        error? trap [clear value]
        equal? value original
    ]
)
; string
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? trap [insert value 4]
        equal? value original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? trap [append value 4]
        equal? value original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? trap [change value 4]
        equal? value original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? trap [poke value 1 4]
        equal? value original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? trap [remove/part value 1]
        equal? value original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? trap [take value]
        equal? value original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? trap [reverse value]
        equal? value original
    ]
)
(
    value: copy original: {1 + 2 + 3}
    protect value
    all [
        error? trap [clear value]
        equal? value original
    ]
)
[#1764
    (unset 'blk protect/deep 'blk true)
    (unprotect 'blk true)
]


; TESTS FOR TEMPORARY EVALUATION HOLDS
; These should elaborated on, and possibly be in their own file.  Simple tests
; for now.

('series-held = pick trap [do code: [clear code]] 'id)
(
    obj: make object! [x: 10]
    'series-held = pick trap [do code: [obj.x: (clear code recycle 20)]] 'id
)


; HIDDEN VARIABLES SHOULD STAY HIDDEN
;
; The bit indicating hiddenness lives on the variable slot of the context.
; This puts it at risk of being overwritten by other values...though it is
; supposed to be protected by masking operations.  Make sure changing the
; value doesn't un-hide it...
(
    obj: make object! [x: 10, y: 20]
    word: bind 'y obj
    did all [
        20 = get word
        [x y] = words of obj  ; starts out visible

        elide protect/hide 'obj/y
        [x] = words of obj  ; hidden
        20 = get word  ; but you can still see it

        set word 30  ; and you can still set it
        [x] = words of obj  ; still hidden
    ]
)
