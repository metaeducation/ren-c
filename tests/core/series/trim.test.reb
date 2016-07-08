; functions/series/trim.r
; bug#83
; refinement order
[strict-equal? trim/all/with "a" "a" trim/with/all "a" "a"]
; bug#1948
["foo^/" = trim "  foo ^/"]
