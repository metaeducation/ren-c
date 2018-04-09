; functions/series/exclude.r
([1] = exclude [1 2] [2 3])
([[1 2]] = exclude [[1 2] [2 3]] [[2 3] [3 4]])
([path/1] = exclude [path/1 path/2] [path/2 path/3])
[#799
    (equal? make typeset! [decimal!] exclude make typeset! [decimal! integer!] make typeset! [integer!])
]
