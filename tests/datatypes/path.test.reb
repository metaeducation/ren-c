; datatypes/path.r
(path? 'a/b)
('a/b == first [a/b])
(not path? 1)
(path! = type of 'a/b)
; the minimum
[#1947
    (path? load "#[path! [[a] 1]]")
]

; ANY-PATH! are no longer positional
;(
;    all [
;        path? a: load "#[path! [[a b c] 2]]"
;        2 == index? a
;    ]
;)

("a/b" = mold 'a/b)
(
    a-word: 1
    data: #{0201}
    2 = data/:a-word
)
(
    blk: reduce [:abs 2]
    2 == blk/:abs
)
(
    blk: [#{} 2]
    2 == blk/#{}
)
(
    blk: reduce [charset "a" 3]
    3 == do reduce [to path! reduce ['blk charset "a"]]
)
(
    blk: [[] 3]
    3 == blk/#[block! [[] 1]]
)
(
    blk: [_ 3]
    3 == do [blk/_]
)
(
    blk: [blank 3]
    3 == do [blk/blank]
)
(
    a-value: 1/Jan/0000
    0 == a-value/1
)
(
    a-value: me@here.com
    #"m" == a-value/1
)
(
    a-value: make error! ""
    blank? a-value/type
)
(
    a-value: make image! 1x1
    0.0.0.255 == a-value/1
)
(
    a-value: first ['a/b]
    'a == a-value/1
)
(
    a-value: make object! [a: 1]
    1 == a-value/a
)
(
    a-value: 2x3
    2 = a-value/1
)
(
    a-value: first [(2)]
    2 == a-value/1
)
(
    a-value: 'a/b
    'a == a-value/1
)
(
    a-value: make port! http://
    blank? a-value/data
)
(
    a-value: first [a/b:]
    'a == a-value/1
)
(
    a-value: "12"
    #"1" == a-value/1
)
(
    a-value: <tag>
    #"t" == a-value/1
)
(
    a-value: 2:03
    2 == a-value/1
)
(
    a-value: 1.2.3
    1 == a-value/1
)

; Ren-C changed INTEGER! path picking to act as PICK, only ANY-STRING! and
; WORD! actually merge with a slash.
(
    a-value: file://a
    #"f" = a-value/1
)

; calling functions through paths: function in object
(
    obj: make object! [fun: func [] [1]]
    1 == obj/fun
)
(
    obj: make object! [fun: func [/ref [integer!]] [ref]]
    1 == obj/fun/ref 1
)
; calling functions through paths: function in block, positional
(
    blk: reduce [func [] [10]  func [] [20]]
    10 == blk/1
)
; calling functions through paths: function in block, "named"
(
    blk: reduce ['foo func [] [10]  'bar func [] [20]]
    20 == blk/bar
)
[#26 (
    b: [b 1]
    1 = b/b
)]

; Paths are immutable, but shouldn't raise an error just on MUTABLE
; (would be too annoying for generic code that mutates some things)
(
    'a/a = mutable 'a/a
)

[#71 (
    a: "abcd"
    "abcd/x" = a/x
)]

[#1820 ; Word USER can't be selected with path syntax
    (
    b: [user 1 _user 2]
    1 = b/user
    )
]
[#1977
    (f: func [/r] [1] error? trap [do load "f/r/%"])
]

; path evaluation order
(
    a: 1x2
    did all [
        b: a/(a: [3 4] 1)
        b = 1
        a = [3 4]
    ]
)

; PATH! beginning with an inert item will itself be inert
;
[
    (/ref/inement/path = to path! [/ref inement path])
    (/refinement/2 = to path! [/refinement 2])
    ((/refinement)/2 = 'refinement)
    (r: /refinement | r/2 = 'refinement)
][
    ("te"/xt/path = to path! ["te" xt path])
    ("text"/3 = to path! ["text" 3])
    (("text")/3 = #"x")
    (t: "text" | t/3 = #"x")
]

; ISSUE! has internal slashes (like FILE!), and does not load as a path
[
    ("iss/ue/path" = as text! ensure issue! "#iss/ue/path")
]

; https://gitter.im/red/red?at=5b23be5d1ee2d149ecc4c3fd
(
    bl: [a 1 q/w [e/r 42]]
    all [
        1 = bl/a
        [e/r 42] = bl/('q/w)
        [e/r 42] = reduce to-path [bl ('q/w)]
        42 = bl/('q/w)/('e/r)
        42 = reduce to-path [bl ('q/w) ('e/r)]
    ]
)

; / is a length 2 PATH! in Ren-C
(path! = type of lit /)
(2 = length of lit /)
(lit / = to path! [_ _])

; foo/ is also a length 2 PATH! in Ren-C
(path! = type of lit foo/ )
(2 = length of lit foo/ )
(lit foo/ = to path! [foo _])

; Not currently true, TO BLOCK! is acting like BLOCKIFY, review
; ([_ _] = to block! lit /)
; ([foo _] = to block! lit foo/ )  ; !!! low priority scanner bug on /)
