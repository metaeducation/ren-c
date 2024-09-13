; functions/series/emptyq.r

(empty? [])
(empty? _)
(not empty? [a])
(empty? next [a])

~index-out-of-range~ !! (
    blk: tail of [1]
    clear head of blk
    not empty? blk
)

[#190
    (x: copy "xx^/" repeat 20 [enline y: join x x] ok)
]

; EMPTY? is not by default tolerant of null, but it's a common need, so you
; can pass voids via MAYBE.
[
    ~expect-arg~ !! (empty? null)

    (empty? maybe null)
]
