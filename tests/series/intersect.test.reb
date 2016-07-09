; functions/series/intersect.r
[empty? intersect [1 2] [3 4]]
[[2] = intersect [1 2] [2 3]]
[empty? intersect [[1 2]] [[2 1]]]
[[[1 2]] = intersect [[1 2]] [[1 2]]]
[empty? intersect [path/1] [path/2]]
; bug#799
[equal? make typeset! [integer!] intersect make typeset! [decimal! integer!] make typeset! [integer!]]
