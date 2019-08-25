; functions/control/map-each.r
; "return bug"
(
    integer? reeval does [map-each v [] [] 1]
)

; PATH! is immutable, but MAP-EACH should work on it

(
     [a 1 b 1 c 1] = map-each x 'a/b/c [reduce [x 1]]
)

(
     [[a 1] [b 1] [c 1]] = map-each x 'a/b/c @[reduce [x 1]]
)

; BLANK! is legal for slots you don't want to name variables for:

(
    [5 11] = map-each [dummy a b] [1 2 3 4 5 6] @[a + b]
)(
    sum: 0
    for-each _ [a b c] [sum: sum + 1]
    sum = 3
)

[
    "Modal splicing control"

    ([1 1 2 2 3 3] = map-each x [1 2 3] [reduce [x x]])

    ([[1 1] [2 2] [3 3]] = map-each/only x [1 2 3] [reduce [x x]])

    ([[1 1] [2 2] [3 3]] = map-each x [1 2 3] @[reduce [x x]])
]
