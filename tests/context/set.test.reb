; functions/context/set.r
[#1763
    (
        a: <before>
        '_ = [a]: pack inert reduce/predicate [null] :reify
        '_ = a
    )
]
(
    a: <a-before>
    b: <b-before>
    2 = [a b]: pack inert reduce/predicate [2 null] :reify
    a = 2
    '_ = b
)
(
    x: make object! [a: 1]
    all [
        error? sys.util.rescue [set x reduce [()]]
        x.a = 1
    ]
)
(
    x: make object! [a: 1 b: 2]
    all [
        error? sys.util.rescue [set x reduce [3 ()]]
        x.a = 1
    ]
)

; set [:get-word] [word]
(a: 1 b: _ [b]: pack inert [a] b = 'a)

(
    a: 10
    b: 20
    did all [blank = [a b]: pack @[_ _], blank? a, blank? b]
)
(
    a: 10
    b: 20
    c: 30
    [a b c]: pack [_ 99]
    did all [null? a, b = 99, ^c = '~]
)

(10 = set void 10)
(null = get void)

(
    e: ^ set 'x raise ~test~
    all [
        quasi? e
        error? e: unquasi e
        e.id = 'test
    ]
)
