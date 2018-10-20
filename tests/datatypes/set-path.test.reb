; datatypes/set-path.r
(set-path? first [a/b:])
(not set-path? 1)
(set-path! == type of first [a/b:])
; the minimum
[#1947
    (set-path? load "#[set-path! [[a] 1]]")
]
(
    all [
        set-path? a: load "#[set-path! [[a b c] 2]]"
        2 == index? a
    ]
)
("a/b:" == mold first [a/b:])
; set-paths are active
(
    a: make object! [b: _]
    a/b: 5
    5 == a/b
)
[#1 (
    o: make object! [a: 0x0]
    o/a/x: 71830
    o/a/x == 71830.0
)]
; set-path evaluation order
(
    a: 1x2
    a/x: (a: [x 4] 3)
    any [
        a == 3x2
        a == [x 3]
    ]
)
[#64 (
    blk: [1]
    i: 1
    blk/:i: 2
    blk == [2]
)]
