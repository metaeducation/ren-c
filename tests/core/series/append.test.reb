; functions/series/append.r
; bug#75
[
    o: make object! [a: 1]
    p: make o []
    append p [b 2]
    not in o 'b
]
[block? append copy [] ()]
