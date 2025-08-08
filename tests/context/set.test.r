; functions/context/set.r
[#1763
    (
        a: <before>
        '~null~ = [a]: pack pin reduce:predicate [null] :reify
        '~null~ = a
    )
]
(
    a: <a-before>
    b: <b-before>
    2 = [a b]: pack pin reduce:predicate [2 null] :reify
    a = 2
    '~null~ = b
)
(
    x: make object! [a: 1]
    all [
        warning? sys.util/recover [set x reduce [()]]
        x.a = 1
    ]
)
(
    x: make object! [a: 1 b: 2]
    all [
        warning? sys.util/recover [set x reduce [3 ()]]
        x.a = 1
    ]
)

; set [:get-word] [word]
(a: 1 b: ~ [b]: pack pin [a] b = 'a)

(
    a: 10
    b: 20
    all [space = [a b]: pack @[_ _], space? a, space? b]
)
~???~ !! (
    a: 10
    b: 20
    c: 30
    [a b c]: pack [null 99]  ; too few values in pack
)
(
    a: 10
    b: 20
    c: 30
    all [
        null? [a b :c]: pack [null 99]  ; /c marks it optional
        null? a
        b = 99
        c = null
    ]
)


(10 = set ^void 10)
(null = get ^void)

(
    ^e: set $x fail 'test
    all [
        quasi? e
        warning? e: unquasi e
        e.id = 'test
    ]
)
