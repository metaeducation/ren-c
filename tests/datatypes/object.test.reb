; datatypes/object.r
[object? make object! [x: 1]]
[not object? 1]
[object! = type of make object! [x: 1]]
; minimum
[object? make object! []]
; literal form
[object? #[object! [[][]]]]
; local words
[
    x: 1
    make object! [x: 2]
    x = 1
]
; BREAK out of make object!
[
    #846
    blank? loop 1 [
        make object! [break]
        2
    ]
]
; THROW out of make object!
; bug#847
[
    1 = catch [
        make object! [throw 1]
        2
    ]
]
; "error out" of make object!
[
    error? try [
        make object! [1 / 0]
        2
    ]
]
; RETURN out of make object!
; bug#848
[
    f: func [] [
        make object! [return 1]
        2
    ]
    1 = f
]
; object cloning
; bug#2045
[
    a: 1
    f: func [] [a]
    g: :f
    o: make object! [a: 2 g: :f]
    p: make o [a: 3]
    1 == p/g
]
; object cloning
; bug#2045
[
    a: 1
    b: [a]
    c: b
    o: make object! [a: 2 c: b]
    p: make o [a: 3]
    1 == do p/c
]
; appending to objects
; bug#1979
[
    o: make object! []
    append o [b: 1 b: 2]
    1 == length of words of o
]
[
    o: make object! [b: 0]
    append o [b: 1 b: 2]
    1 == length of words of o
]
[
    o: make object! []
    c: "c"
    append o compose [b: "b" b: (c)]
    same? c o/b
]
[
    o: make object! [b: "a"]
    c: "c"
    append o compose [b: "b" b: (c)]
    same? c o/b
]
[
    o: make object! []
    append o 'self
    true
]
[
    o: make object! []
    ; currently disallowed..."would expose or modify hidden values"
    error? try [append o [self: 1]]
]
