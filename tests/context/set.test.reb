; functions/context/set.r
[#1763
    (
        a: <before>
        [_] = set [a] reduce .try [null]
        blank? :a
    )
]
(
    a: <a-before>
    b: <b-before>
    [2 _] = set [a b] reduce .try [2 null]
    a = 2
    blank? :b
)
(x: make object! [a: 1] all [error? trap [set x reduce [()]] x/a = 1])
(x: make object! [a: 1 b: 2] all [error? trap [set x reduce [3 ()]] x/a = 1])
; set [:get-word] [word]
(a: 1 b: _ set [b] [a] b = 'a)

(
    a: 10
    b: 20
    did all [blank = set [a b] blank, blank? a, blank? b]
)
(
    a: 10
    b: 20
    did all [
        [x y] = set/single [a b] [x y]
        a = [x y]
        b = [x y]
    ]
)
(
    a: 10
    b: 20
    c: 30
    set [a b c] [_ 99]
    did all [a = _, b = 99, c = _]
)
(
    a: 10
    b: 20
    c: 30
    set/some [a b c] [_ 99]
    did all [a = 10, b = 99, c = 30]
)

; #1745
(
    [1 2 3 4 5 6] = set [a 'b :c d: /e @f] [1 2 3 4 5 6]
)
