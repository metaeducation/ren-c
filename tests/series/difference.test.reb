; functions/series/difference.r
[[1 3] = difference [1 2] [2 3]]
[[[1 2] [3 4]] = difference [[1 2] [2 3]] [[2 3] [3 4]]]
[[path/1 path/3] = difference [path/1 path/2] [path/2 path/3]]
; bug#799
[equal? make typeset! [decimal!] difference make typeset! [decimal! integer!] make typeset! [integer!]]
