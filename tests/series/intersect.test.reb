; functions/series/intersect.r
[[2] = intersect [1 2] [2 3]]
[[[2 3]] = intersect [[1 2] [2 3]] [[2 3] [3 4]]]
[[path/2] = intersect [path/1 path/2] [path/2 path/3]]
; bug#799
[equal? make typeset! [integer!] intersect make typeset! [decimal! integer!] make typeset! [integer!]]
