; functions/series/copy.r
[
    blk: []
    all [
        blk = copy blk
        not same? blk copy blk
    ]
]
[
    blk: [1]
    all [
        blk = copy blk
        not same? blk copy blk
    ]
]
[[1] = copy/part tail [1] -1]
[[1] = copy/part tail [1] -2147483647]
; bug#853
; bug#1118
[[1] = copy/part tail [1] -2147483648]
[[] = copy/part [] 0]
[[] = copy/part [] 1]
[[] = copy/part [] 2147483647]
[ok? try [copy blank]]
; bug#877
[
    a: copy []
    insert/only a a
    error? try [copy/deep a]
    true
]
; bug#2043
[
    f: func [] []
    error? try [copy :f]
    true
]
; bug#648
[["a"] = deline/lines "a"]
; bug#1794
[1 = length? deline/lines "Slovenščina"]
