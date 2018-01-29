; functions/series/append.r
; bug#75
[
    o: make object! [a: 1]
    p: make o []
    append p [b 2]
    not in o 'b
]

[block? append copy [] ()]


; Slipstream in some tests of MY (there don't seem to be a lot of tests here)
;
[
    data: [1 2 3 4]
    data: my next
    data: my skip 2
    data: my back

    block: copy [a b c d]
    block: my next
    block: my insert data
    block: my head

    block = [a 3 4 b c d]
]
