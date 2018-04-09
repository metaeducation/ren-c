; datatypes/object.r
(object? make object! [x: 1])
(not object? 1)
(object! = type of make object! [x: 1])
; minimum
(object? make object! [])
; literal form
(object? #[object! [[][]]])
; local words
(
    x: 1
    make object! [x: 2]
    x = 1
)
; BREAK out of make object!
[#846 (
    blank? loop 1 [
        make object! [break]
        2
    ]
)]
; THROW out of make object!
[#847 (
    1 = catch [
        make object! [throw 1]
        2
    ]
)]
; "error out" of make object!
(
    error? try [
        make object! [1 / 0]
        2
    ]
)
; RETURN out of make object!
[#848 (
    f: func [] [
        make object! [return 1]
        2
    ]
    1 = f
)]
; object cloning
[#2045 (
    a: 1
    f: func [] [a]
    g: :f
    o: make object! [a: 2 g: :f]
    p: make o [a: 3]
    1 == p/g
)]
; object cloning
[#2045 (
    a: 1
    b: [a]
    c: b
    o: make object! [a: 2 c: b]
    p: make o [a: 3]
    1 == do p/c
)]
; appending to objects
[#1979 (
    o: make object! []
    append o [b: 1 b: 2]
    1 == length of words of o
)]
(
    o: make object! [b: 0]
    append o [b: 1 b: 2]
    1 == length of words of o
)
(
    o: make object! []
    c: "c"
    append o compose [b: "b" b: (c)]
    same? c o/b
)
(
    o: make object! [b: "a"]
    c: "c"
    append o compose [b: "b" b: (c)]
    same? c o/b
)
(
    o: make object! []
    append o 'self
    true
)
(
    o: make object! []
    ; currently disallowed..."would expose or modify hidden values"
    error? try [append o [self: 1]]
)



(
    o1: make object! [a: 10 b: does [f: does [a] f]]
    o2: make o1 [a: 20]

    o2/b = 20
)

(
    o-big: make object! collect [
        repeat n 256 [
            ;
            ; var-1: 1
            ; var-2: 2
            ; ...
            ; var-256: 256
            ;
            keep compose/only [
                (to-set-word rejoin ["var-" n]) (n)
            ]
        ]
        repeat n 256 [
            ;
            ; fun-1: does [var-1]
            ; fun-2: does [var-1 + var-2]
            ; ...
            ; fun-256: does [var-1 + var-2 ... + var-256]
            ;
            keep compose/only [
                (to-set-word rejoin ["fun-" n]) does (collect [
                    keep 'var-1
                    repeat i n - 1 [
                        keep compose [
                            + (to-word rejoin ["var-" i + 1])
                        ]
                    ]
                ])
            ]
        ]
    ]

    o-big-b: make o-big [var-1: 100001]
    o-big-c: make o-big-b [var-2: 200002]

    did all [
        o-big/fun-255 = 32640
        o-big-b/fun-255 = 132640
        o-big-c/fun-255 = 332640
    ]
)

; object cloning
[#2050 (
    o: make object! [n: 'o b: reduce [func [] [n]]]
    p: make o [n: 'p]
    (o/b)/1 = 'o
)]

[#2076 (
    o: make object! [x: 10]
    e: trap [append o [self: 1]]
    error? e and (e/id = 'hidden)
)]

[#187 (
    o: make object! [self]
    [] = words of o
)]

[#1553 (
    o: make object! [a: _]
    same? context of in o 'self context-of in o 'a
)]
