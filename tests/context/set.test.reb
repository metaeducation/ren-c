; functions/context/set.r
[#1763
    (a: 1 all [error? sys/util/rescue [set [a] reduce [null]] a = 1])
]
(a: 1 sys/util/rescue [set [a b] reduce [2 null]] a = 1)
(x: make object! [a: 1] all [error? sys/util/rescue [set x reduce [()]] x/a = 1])
(x: make object! [a: 1 b: 2] all [error? sys/util/rescue [set x reduce [3 ()]] x/a = 1])
; set [:get-word] [word]
(a: 1 b: null set [b] [a] b = 'a)

(
    a: 10
    b: 20
    all [blank = set [a b] blank, blank? a,  null? b]
)
(
    a: 10
    b: 20
    all [
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
    all [a = _, b = 99, c = _]
)
(
    a: 10
    b: 20
    c: 30
    set/some [a b c] [_ 99]
    all [a = 10, b = 99, c = 30]
)

; #1745
(
    [1 2 3 4 5 6] = set [a 'b :c d: /e #f] [1 2 3 4 5 6]
)
