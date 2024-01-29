; functions/context/set.r
[#1763
    (
        a: <before>
        '~null~ = [a]: pack quote reduce/predicate [null] :reify
        '~null~ = a
    )
]
(
    a: <a-before>
    b: <b-before>
    2 = [a b]: pack quote reduce/predicate [2 null] :reify
    a = 2
    '~null~ = b
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
(a: 1 b: ~ [b]: pack ^[a] b = 'a)

(
    a: 10
    b: 20
    did all [blank = [a b]: pack ^[_ _], blank? a, blank? b]
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
        null? [a b /c]: pack [null 99]  ; /c marks it optional
        null? a
        b = 99
        c = null
    ]
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
