; %loops/for.test.r
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
    for 'i each str [append out i]
    out = str
)
(
    blk: [1 2 3 4]
    sum: 0
    for 'i each blk [sum: sum + i]
    sum = 10
)
; cycle return value
(
    blk: [1 2 3 4]
    'true = for 'i each blk ['true]
)
(
    blk: [1 2 3 4]
    'false = for 'i each blk ['false]
)
; break cycle
(
    str: "abcdef"
    num: ~
    for 'i each str [
        num: i
        if i = #"c" [break]
    ]
    num = #"c"
)
; break return value
(
    blk: [1 2 3 4]
    null? for 'i each blk [break]
)
; continue cycle
(
    success: 'true
    all [
        trash? for 'i each [1] [continue, success: 'false]
        true? success
    ]
)
; zero repetition
(
    success: 'true
    blk: []
    all [
        void? for 'i each blk [success: 'false]
        true? success
    ]
)
; Test that return stops the loop
(
    blk: [1]
    f1: func [return: [integer!]] [for 'i each blk [return 1 2]]
    1 = f1
)
; Test that errors do not stop the loop and errors can be returned
(
    num: 0
    blk: [1 2]
    e: for 'i each blk [num: i rescue [1 / 0]]
    all [warning? e num = 2]
)

; "recursive safety", "locality" and "body constantness" test in one
(for-each 'i [1] b: [not same? 'i b.3])

; recursivity
(
    num: 0
    for-each 'i [1 2 3 4 5] [
        for 'i each [1 2] [num: num + 1]
    ]
    num = 10
)
~bad-value~ !! (
    for [:x] each [] []
)

; An @WORD! does not create a new variable or binding, but a WORD! does
(
    x: 10
    sum: 0
    for 'x each [1 2 3] [sum: sum + x]
    all [x = 10, sum = 6]
)
(
    x: 10
    sum: 0
    for @x each [1 2 3] [sum: sum + x]
    all [x = 3, sum = 6]
)
(
    x: 10
    y: 20
    sum: 0
    for [@x y] each [1 2 3 4] [sum: sum + x + y]
    all [x = 3, y = 20, sum = 10]
)

; Redundancy is checked for.  @WORD! redundancy is legal because those
; words may have distinct bindings and the same spelling, and they're not
; being fabricated.
;
[#2273
    (
        x: ~#[shouldn't be seen]#~
        obj1: make object! [x: 20]
        obj2: make object! [x: 30]
        ok
    )
    ~dup-vars~ !! (
        for [x x] each [1 2 3 4] [sum: sum + x]
    )
    (
        sum: 0
        for compose [
            x (bind obj1 '@x)
        ] each [
            1 2 3 4
        ][
            sum: sum + x
        ]
        all [
            sum = 4
            obj1.x = 4
        ]
    )
    (
        sum: 0
        for compose [
            (bind obj2 '@x) x
        ] each [
            1 2 3 4
        ][
            sum: sum + x
        ]
        all [
            sum = 6
            obj2.x = 3
        ]
    )
    (
        sum: 0
        for compose [
            (bind obj1 '@x) (bind obj2 '@x)
        ] each [
            1 2 3 4
        ][
            sum: sum + obj1.x + obj2.x
        ]
        all [
            sum = 10
            obj1.x = 3
            obj2.x = 4
        ]
    )
]


; Series being enumerated are locked during the FOR-EACH, and this lock
; has to be released on THROWs or FAILs.

(
    block: copy [a b c]
    all [
        <thrown> = catch [
            for 'item each block [
                throw <thrown>
            ]
        ]
        [a b c 10] = append block 10
    ]
)(
    block: copy [a b c]
    all [
        let e: sys.util/recover [
            for 'item each block [
                append block <panic>
            ]
        ]
        e.id = 'locked-series
        [a b c 10] = append block 10
    ]
)

; paths are immutable, but for-each is legal on them

(
    [a b c] = collect [for 'x each 'a/b/c [keep x]]
)(
    [_ b] = collect [for 'x each '/b [keep x]]
)(
    [a _] = collect [for 'x each 'a/ [keep x]]
)

(
    [a b c] = collect [for 'x each 'a.b.c [keep x]]
)(
    [_ b] = collect [for 'x each '.b [keep x]]
)(
    [a _] = collect [for 'x each 'a. [keep x]]
)

; FOR-EACH X is an alias of FOR X EACH
;
([1 2 3] = collect [for-each 'x [1 2 3] [keep x]])

; SPACE is legal for slots you want to opt out of
(
    sum: 0
    for _ each [a b c] [sum: sum + 1]
    sum = 3
)

(5 = for-each 'x [1 2 3] [5 assert [x < 10]])

; Abrupt panic (needs more comprehensive tests, but this is an example of
; where the enumeration state is held by the FOR-EACH frame, and the error
; on trying to use 3 variables to enumerate an object originates from the
; C stack when FOR-EACH's dispatcher is on the stack.
;
~???~ !! (for-each [x y z] make object! [key: <value>] [])


; BLANK acts same as empty block, void opts out and generates BREAK signal
[
    (void? for-each 'x [] [panic])
    (void? for-each 'x blank [panic])
    (null? for-each 'x ^void [panic])

    ~expect-arg~ !! (for-each 'x '~ [panic])
    ~unspecified-arg~ !! (for-each 'x ~ [panic])
]
