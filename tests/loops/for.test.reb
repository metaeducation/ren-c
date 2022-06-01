; %loops/for.test.reb
;
; FOR is a generalization of the idea of something that iterates a loop and
; then returns the last result.  (For a version that collects the results as
; a BLOCK!, see MAP.)  It can operate with an arbitrary generator or dialect.
;
; FOR-EACH is a legacy specialization that will iterate over a block one at
; a time.  Unlike MAP-EACH, FOR X EACH [...] is not semantically different
; from FOR-EACH x [...].  (MAP splices plain blocks, while MAP-EACH does not)

(
    out: copy ""
    str: "abcdef"
    for i each str [append out i]
    out = str
)
(
    blk: [1 2 3 4]
    sum: 0
    for i each blk [sum: sum + i]
    sum = 10
)
; cycle return value
(
    blk: [1 2 3 4]
    true = for i each blk [true]
)
(
    blk: [1 2 3 4]
    false = for i each blk [false]
)
; break cycle
(
    str: "abcdef"
    for i each str [
        num: i
        if i = #"c" [break]
    ]
    num = #"c"
)
; break return value
(
    blk: [1 2 3 4]
    null? for i each blk [break]
)
; continue cycle
(
    success: true
    for i each [1] [continue, success: false]
    success
)
; zero repetition
(
    success: true
    blk: []
    for i each blk [success: false]
    success
)
; Test that return stops the loop
(
    blk: [1]
    f1: func [return: [integer!]] [for i each blk [return 1 2]]
    1 = f1
)
; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    blk: [1 2]
    e: for i each blk [num: i trap [1 / 0]]
    all [error? e num = 2]
)

; "recursive safety", "locality" and "body constantness" test in one
(for-each i [1] b: [not same? 'i b.3])

; recursivity
(
    num: 0
    for-each i [1 2 3 4 5] [
        for i each [1 2] [num: num + 1]
    ]
    num = 10
)
(
    error? trap [for [:x] each [] []]
)

; A LIT-WORD! does not create a new variable or binding, but a WORD! does
(
    x: 10
    sum: 0
    for x each [1 2 3] [sum: sum + x]
    did all [x = 10, sum = 6]
)
(
    x: 10
    sum: 0
    for 'x each [1 2 3] [sum: sum + x]
    did all [x = 3, sum = 6]
)
(
    x: 10
    y: 20
    sum: 0
    for ['x y] each [1 2 3 4] [sum: sum + x + y]
    did all [x = 3, y = 20, sum = 10]
)

; Redundancy is checked for.  LIT-WORD! redundancy is legal because those
; words may have distinct bindings and the same spelling, and they're not
; being fabricated.
;
; !!! Note that because FOR-EACH soft quotes, the COMPOSE would be interpreted
; as the loop variable if you didn't put it in parentheses!
[#2273 (
    x: 10
    obj1: make object! [x: 20]
    obj2: make object! [x: 30]
    sum: 0
    did all [
        error? trap [for [x x] each [1 2 3 4] [sum: sum + x]]
        error? trap [
            for :(compose [  ; see above
                x (bind the 'x obj1)
            ]) each [
                1 2 3 4
            ][
                sum: sum + x
            ]
        ]
        error? trap [
            for :(compose [  ; see above
                (bind the 'x obj2) x
            ]) each [
                1 2 3 4
            ][
                sum: sum + x
            ]
        ]
        not error? trap [
            for :(compose [  ; see above
                (bind the 'x obj1) (bind the 'x obj2)
            ]) each [
                1 2 3 4
            ][
                sum: sum + obj1.x + obj2.x
            ]
        ]
        sum = 10
        obj1.x = 3
        obj2.x = 4
    ]
)]


; Series being enumerated are locked during the FOR-EACH, and this lock
; has to be released on THROWs or FAILs.

(
    block: copy [a b c]
    all [
        <thrown> = catch [
            for item each block [
                throw <thrown>
            ]
        ]
        [a b c 10] = append block 10
    ]
)(
    block: copy [a b c]
    all [
        e: trap [
            for item each block [
                append block <failure>
            ]
        ]
        e.id = 'locked-series
        [a b c 10] = append block 10
    ]
)

; paths are immutable, but for-each is legal on them

(
    [a b c] = collect [for x each 'a/b/c [keep ^x]]
)(
    [_ _] = collect [for x each '/ [keep ^x]]
)

; FOR-EACH X is an alias of FOR X EACH
;
([1 2 3] = collect [for-each x [1 2 3] [keep x]])

; BLANK! is legal for slots you want to opt out of
(
    sum: 0
    for _ each [a b c] [sum: sum + 1]
    sum = 3
)

(5 = for-each x [1 2 3] [5 assert [x < 10]])
