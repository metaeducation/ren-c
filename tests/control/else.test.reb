(
    success: <bad>
    if 1 > 2 [success: false] else [success: true]
    success
)
(
    success: <bad>
    if 1 < 2 [success: true] else [success: false]
    success
)
(
    success: <bad>
    if-not 1 > 2 [success: true] else [success: false]
    success
)
(
    success: <bad>
    if-not 1 < 2 [success: false] else [success: true]
    success
)
(
    success: <bad>
    if true (does [success: true])
    success
)
(
    success: true
    if false (does [success: false])
    success
)

(
    ; https://github.com/metaeducation/ren-c/issues/510

    c: func [i] [
        return if i < 15 [30] else [4]
    ]

    d: func [i] [
        return <- if i < 15 [30] else [4]
    ]

    did all [
        30 = c 10
        4 = c 20
        30 = d 10
        4 = d 20
    ]
)

; Hard quotes need to account for enfix deferral
(
    foo: func [y] [return lit 1 then (x => [x + y])]
    bar: func [y] [return 1 then (x => [x + y])]
    did all [
        foo 10 = 11
        bar 10 = 1
    ]
)
