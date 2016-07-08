; functions/series/exclude.r
[empty? exclude [1 2] [2 1]]
; bug#799
[equal? make typeset! [decimal!] exclude make typeset! [decimal! integer!] make typeset! [integer!]]
