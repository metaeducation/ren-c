; datatypes/path.r
[path? 'a/b]
['a/b == first [a/b]]
[not path? 1]
[path! = type-of 'a/b]
; the minimum
; bug#1947
[path? load "#[path! [[a] 1]]"]
[
    all [
        path? a: load "#[path! [[a b c] 2]]"
        2 == index? a
    ]
]
["a/b" = mold 'a/b]
[
    a-word: 1
    data: #{0201}
    2 = data/:a-word
]
[
    blk: reduce [:abs 2]
    2 == blk/:abs
]
[
    blk: [#{} 2]
    2 == blk/#{}
]
[
    blk: reduce [charset "a" 3]
    3 == do reduce [to path! reduce ['blk charset "a"]]
]
[
    blk: [[] 3]
    3 == blk/#[block! [[] 1]]
]
[
    blk: [_ 3]
    3 == do [blk/_]
]
[
    blk: [blank 3]
    3 == do [blk/blank]
]
[
    a-value: 1/Jan/0000
    0 == a-value/1
]
[
    a-value: me@here.com
    #"m" == a-value/1
]
[
    a-value: make error! ""
    a-value/type = 'user
]
[
    a-value: make image! 1x1
    0.0.0.255 == a-value/1
]
[
    a-value: first ['a/b]
    'a == a-value/1
]
[
    a-value: make object! [a: 1]
    1 == a-value/a
]
[
    a-value: 2x3
    2 = a-value/1
]
[
    a-value: first [(2)]
    2 == a-value/1
]
[
    a-value: 'a/b
    'a == a-value/1
]
[
    a-value: make port! http://
    blank? a-value/data
]
[
    a-value: first [a/b:]
    'a == a-value/1
]
[
    a-value: "12"
    #"1" == a-value/1
]
[
    a-value: <tag>
    #"t" == a-value/1
]
[
    a-value: 2:03
    2 == a-value/1
]
[
    a-value: 1.2.3
    1 == a-value/1
]

; Ren-C changed INTEGER! path picking to act as PICK, only ANY-STRING! and
; WORD! actually merge with a slash.
[
    a-value: file://a
    #"f" = a-value/1
]

; calling functions through paths: function in object
[
    obj: make object! [fun: func [] [1]]
    1 == obj/fun
]
[
    obj: make object! [fun: func [/ref val] [val]]
    1 == obj/fun/ref 1
]
; calling functions through paths: function in block, positional
[
    blk: reduce [func [] [10]  func [] [20]]
    10 == blk/1
]
; calling functions through paths: function in block, "named"
[
    blk: reduce ['foo func [] [10]  'bar func [] [20]]
    20 == blk/bar
]
; bug#26
[
    b: [b 1]
    1 = b/b
]
; recursive path
[
    a: make object! []
    path: 'a/a
    change/only back tail path path
    error? try [do path]
    true
]

; bug#71
[
    a: "abcd"
    "abcd/x" = a/x
]

; bug#1820: Word USER can't be selected with path syntax
[
    b: [user 1 _user 2]
    1 = b/user
]
; bug#1977
[f: func [/r] [1] error? try [f/r/%]]
