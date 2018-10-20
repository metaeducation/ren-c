; functions/series/copy.r
(
    blk: []
    all [
        blk == copy blk
        not same? blk copy blk
    ]
)
(
    blk: [1]
    all [
        blk == copy blk
        not same? blk copy blk
    ]
)
([1] == copy/part tail of [1] -1)
([1] == copy/part tail of [1] -2147483647)
[#853 #1118
    ([1] == copy/part tail of [1] -2147483648)
]
([] == copy/part [] 0)
([] == copy/part [] 1)
([] == copy/part [] 2147483647)
(ok? trap [copy blank])
[#877 (
    a: copy []
    insert/only a a
    error? trap [copy/deep a]
    true
)]
[#2043 (
    f: func [] []
    error? trap [copy :f]
    true
)]
[#648
    (["a"] == deline/lines "a")
]
[#1794
    (1 == length of deline/lines "Slovenščina")
]
