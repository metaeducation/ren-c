; functions/series/difference.r
([1 3] == difference [1 2] [2 3])
([[1 2] [3 4]] == difference [[1 2] [2 3]] [[2 3] [3 4]])
([path/1 path/3] == difference [path/1 path/2] [path/2 path/3])

[#799 (
    strict-equal?
        make typeset! [decimal!]
        difference make typeset! [decimal! integer!] make typeset! [integer!]
)]

[#1822 (
    did all [
        12:00 == difference 13/1/2011/12:00 13/1/2011
        12:00 == difference 13/1/2011/12:00 13/1/2011/0:0
    ]
)]
