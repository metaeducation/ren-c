; functions/series/copy.r
(
    blk: []
    all [
        blk = copy blk
        not same? blk copy blk
    ]
)
(
    blk: [1]
    all [
        blk = copy blk
        not same? blk copy blk
    ]
)
([1] = copy/part tail of [1] -1)
([1] = copy/part tail of [1] -2147483647)
[#853 #1118
    ([1] = copy/part tail of [1] -2147483648)
]
([] = copy/part [] 0)
([] = copy/part [] 1)
([] = copy/part [] 2147483647)
(ok? copy blank)

;[#877
;    ~stack-overflow~ !! (
;        a: copy []
;        insert a a
;        error? trap [copy/deep a]
;        true
;    )
;]

[#2043 (
    f: func [] []
    error? trap [copy :f]
    true
)]


[#138 (
    b: make binary! 100
    #{} = copy/part b 50
)]
