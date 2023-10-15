; functions/series/emptyq.r

(empty? [])
(empty? _)
(not empty? [a])
(empty? next [a])

(
    blk: tail of [1]
    clear head of blk
    not empty? blk  ; !!! currently answers as "not tail?" so not empty
)

[#190
    (x: copy "xx^/" repeat 20 [enline y: join x x] true)
]

; EMPTY? is not by default tolerant of null, but it's a common need, so a
; combined test is available.
[
    ~expect-arg~ !! (empty? null)

    (empty-or-null? [])
    (empty-or-null? _)
    (empty-or-null? null)
]
