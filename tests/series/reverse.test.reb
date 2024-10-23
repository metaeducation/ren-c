; functions/series/reverse.r
[#1810 ; REVERSE:PART does not work for tuple!
    (3.2.1.4.5 = reverse:part 1.2.3.4.5 3)
]


[#2326 (
    data: collect [
        keep:line spread [1 2] keep:line spread [3 4 5] keep:line spread [6]
    ]
    ; == [
    ;     1 2
    ;     3 4 5
    ;     6
    ; ]
    before: collect [
        for-next 'pos data [keep boolean new-line? pos]
        keep boolean new-line? tail data
    ]

    reverse data
    ; == [
    ;     6
    ;     5 4 3
    ;     2 1
    ; ]
    after: collect [
        for-next 'pos data [keep boolean new-line? pos]
        keep boolean new-line? tail data
    ]

    all [
        before = [true false true false false true true]
        after = [true true false false true false true]
    ]
)]

; !!! sequences are immutable, so a REVERSE operation that just ignores that
; is not the greatest idea.  But this replaces mutable code that would
; just corrupt memory, so it's better than that.
[
    ('d:c:b:a = reverse 'a:b:c:d)
    (2.1.3.4 = reverse:part 1.2.3.4 2)
]
