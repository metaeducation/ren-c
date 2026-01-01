; %loops/map.test.r
;
; MAP is a new generalized form of loop which implicitly collects the result
; of running its body branch.

; "return bug"
(
    integer? reeval does [map-each 'v [] [] 1]
)

; PATH! is immutable, but MAP should work on it
(
    [a 1 b 1 c 1] = map 'x each 'a/b/c [spread reduce [x 1]]
)

; SPACE is legal for slots you don't want to name variables for:
[(
    [5 11] = map [_ a b] each [1 2 3 4 5 6] [a + b]
)]

; ACTION!s are called repeatedly util an "enumeration exhausted" error returned
; (DONE synthesizes this error antiform)
(
    make-one-thru-five: func [
        return: [error! integer!]
    ] (bind construct [count: 0] [
        if count = 5 [return done]
        return count: count + 1
    ])
    [10 20 30 40 50] = map 'i make-one-thru-five/ [
        i * 10
    ]
)(
    make-one-thru-five: func [
        return: [error! integer!]
    ] (bind construct [count: 0] [
        if count = 5 [return done]
        return count: count + 1
    ])
    [[1 2] [3 4] [5]]  = map [a b] make-one-thru-five/ [
        compose [(a) (opt b)]
    ]
)

; MAP uses APPEND rules, so SPREAD works
[
    ([1 <haha!> 2 <haha!>] = map 'x each [1 2] [spread reduce [x <haha!>]])
    ([1 <haha!> 2 <haha!>] = map 'x each [1 2] [spread :[x <haha!>]])

    ([[1 <haha!>] [2 <haha!>]] = map 'x each [1 2] [:[x <haha!>]])
    (['[1 <haha!>] '[2 <haha!>]] = map 'x each [1 2] ^[:[x <haha!>]])

    ; void opts out, as a twist on APPEND's rule disallowing it.
    ;
    ([[1 <haha!>] [3 <haha!>]] = map 'x each [1 2 3] [
        if x <> 2 [:[x <haha!>]]
    ])
]

[
    (['1 ~null~ '3] = map 'x each [1 2 3] [lift if x <> 2 [x]])
    (['1 ~()~ '3] = map 'x each [1 2 3] [lift if x = 2 [^ghost] else [x]])
]

; MAP-EACH works with ANY-CONTEXT? now
(
    [x 10 y 20] = map-each [key val] make object! [x: 10 y: 20] [
        spread reduce [key val]
    ]
)

; BLANK acts same as empty block, void opts out and generates BREAK signal
[
    ([] = map-each 'x [] [panic])
    ([] = map-each 'x none [panic])
    (null? map-each 'x ^ghost [panic])

    ~expect-arg~ !! (map-each 'x '~ [panic])
    ~unspecified-arg~ !! (map-each 'x ~ [panic])
]

; VOID removes from MAP! if not ^META
(
    m: make map! ['key 1000]
    all [
        m.key = 1000
        elide m.('key): ^ghost
        not try m.key
    ]
)
