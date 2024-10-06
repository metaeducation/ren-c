; datatypes/path.r
(path? 'a/b)
('a/b == first [a/b])
(not path? 1)
(path! = type of 'a/b)
; the minimum
[#1947
    (path? load-value "[a]/1")
]

; ANY-PATH? are no longer positional
;(
;    all [
;        path? a: load-value "[a b c]/2"
;        2 == index? a
;    ]
;)

("a/b" = mold 'a/b)
(
    a-word: 1
    data: #{0201}
    2 = data.(a-word)
)
(
    blk: reduce [^abs 2]
    2 == blk.(^abs)
)
~bad-sequence-item~ !! (
    blk: reduce [charset "a" 3]
    to path! reduce ['blk charset "a"]
)
(
    blk: [[] 3]
    3 == blk.([])
)
(
    blk: [_ 3]
    3 == eval [blk.('_)]
)
(
    blk: [blank 3]
    3 == eval [blk.blank]
)
(
    a-value: 1/Jan/0000
    all [
        1 == a-value.1
        'Jan == a-value.2
        0 == a-value.3
    ]
)
(
    a-value: me@here.com
    #"m" == a-value.1
)
(
    a-value: make error! ""
    null? a-value.type
)
(
    a-value: first ['a/b]
    'a == (noquote a-value).1
)
(
    a-value: make object! [a: 1]
    1 == a-value.a
)
(
    a-value: 2x3
    2 = a-value.1
)
(
    a-value: first [(2)]
    2 == a-value.1
)
(
    a-value: 'a/b
    'a == a-value.1
)
(
    a-value: make port! http://
    null? a-value.data
)
(
    a-value: first [a.b:]
    'a.b == a-value.1
)
(
    a-value: "12"
    #"1" == a-value.1
)
(
    a-value: <tag>
    #"t" == a-value.1
)
(
    a-value: 2:03
    2 == a-value.1
)
(
    a-value: 1.2.3
    1 == a-value.1
)

; Ren-C changed INTEGER! path picking to act as PICK, only ANY-STRING? and
; WORD! actually merge with a slash.
(
    a-value: file://a
    #"f" = a-value.1
)

; calling functions through paths: function in object
(
    obj: make object! [fun: func [] [return 1]]
    1 == obj/fun
)
(
    obj: make object! [fun: func [/ref [integer!]] [return ref]]
    1 == obj/fun:ref 1
)

; calling functions through paths: function in block, positional
(
    blk: reduce [
        noquasi reify func [] [return 10]
        noquasi reify lambda [] [20]
    ]
    10 = run blk.1
)
; calling functions through paths: function in block, "named"
(
    blk: reduce [
        'foo noquasi reify lambda [] [10]
        'bar noquasi reify func [] [return 20]
    ]
    20 = run blk.bar
)

[#26 (
    b: [b 1]
    1 = b.b
)]

; Paths are immutable, but shouldn't raise an error just on MUTABLE
; (would be too annoying for generic code that mutates some things)
(
    'a/a = mutable 'a/a
)

[#71
    ~bad-pick~ !! (
        a: "abcd"
        a.x
    )
]

[#1820 ; Word USER can't be selected with path syntax
    (
    b: [user 1 _user 2]
    1 = b.user
    )
]
[#1977
    ~scan-invalid~ !! (f: func [/r] [1], do load-value "f/r/%")
]

; path evaluation order
; Note: This matches Red but is different from R3-Alpha, which gets b as 1
(
    a: 1x2
    b: a.(a: [3 4] 1)
    all [
        b = 3
        a = [3 4]
    ]
)

; PATH! beginning with an inert item will itself be inert
;
[
    ~bad-sequence-item~ !! (to path! [/ref inement path])
    ~bad-sequence-item~ !! (to path! [/refinement 2])
    ((/refinement).2 = 'refinement)
    (r: /refinement, r.2 = 'refinement)
][
    ("te"/xt/path = to path! ["te" xt path])
    ("text"/3 = to path! ["text" 3])
    (("text").3 = #"x")
    (t: "text", t.3 = #"x")
]

; ISSUE! has internal slashes (like FILE!), and does not load as a path
[
    ("iss/ue/path" = as text! ensure issue! load-value "#iss/ue/path")
]

; https://gitter.im/red/red?at=5b23be5d1ee2d149ecc4c3fd
(
    bl: [a 1 q/w [e/r 42]]
    all [
        1 = bl.a
        [e/r 42] = bl.('q/w)
        [e/r 42] = reduce inside [] to-tuple [bl ('q/w)]
        42 = bl.('q/w).('e/r)
        42 = reduce inside [] to-tuple [bl ('q/w) ('e/r)]
    ]
)

; / was once a length 2 PATH! in Ren-C, but now it's a WORD!
;
(word! = type of the /)

; foo/ is a length 2 PATH! in Ren-C
(path! = type of the foo/ )
(2 = length of the foo/ )
(the foo/ = to path! [foo _])

(
    e: trap [to path! [_ _]]
    all [
        e.id = 'conflated-sequence
        e.arg1 = '/
        word? e.arg1
    ]
)
(
    e: trap [to path! [~ ~]]
    all [
        e.id = 'conflated-sequence
        e.arg1 = '~/~
        quasiform? e.arg1
        '/ = unquasi e.arg1
    ]
)
(
    e: trap [to path! [a _ b]]
    all [
        e.id = 'bad-sequence-blank
    ]
)

; Not currently true, TO BLOCK! is acting like BLOCKIFY, review
;
; ([foo _] = to block! the foo/ )  ; !!! low priority scanner bug on /)

; Voids vanish in GROUP!s, refinements allowed.  /REFINE form is permitted
; because it's easy to support and may make someone happy.
;
([a b c d] = append:(if null ['dup]) [a b c] spread [d])
([a b c d d] = append:(if ok ['dup]) [a b c] spread [d] 2)
([a b c d d] = append:(if ok [/dup]) [a b c] spread [d] 2)

; Made this example in a forum post, tested it working, so why not a test
(
    '+./foo.r+ = as path! reduce [
        as tuple! [+ _]
        as tuple! [foo r+]
    ]
)

[
    (3 = length of 'a/b/c)
    (not empty? 'a/b/c)
    ~type-has-no-index~ !! (index of 'a/b/c)
    (null = try index of 'a/b/c)
]
