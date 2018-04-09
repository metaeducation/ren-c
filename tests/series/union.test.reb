; functions/series/union.r
([1 2 3] = union [1 2] [2 3])
([[1 2] [2 3] [3 4]] = union [[1 2] [2 3]] [[2 3] [3 4]])
([path/1 path/2 path/3] = union [path/1 path/2] [path/2 path/3])
[#799
    (equal? make typeset! [decimal! integer!] union make typeset! [decimal!] make typeset! [integer!])
]
