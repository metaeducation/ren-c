; functions/series/insert.r
(
    a: make block! 0
    insert a 0
    a == [0]
)
(
    a: [0]
    b: make block! 0
    insert b first a
    a == b
)
(
    a: [0]
    b: make block! 0
    insert b a
    [[0]] == b
)
; paren
(
    a: make group! 0
    insert a 0
    a == first [(0)]
)
(
    a: first [(0)]
    b: make group! 0
    insert b a
    b == '((0))
)
(
    a: first [(0)]
    b: make group! 0
    insert b ^a
    b == '('(0))
)
; text
(
    a: make text! 0
    insert a #"0"
    a == "0"
)
(
    a: "0"
    b: make text! 0
    insert b first a
    a == b
)
(
    a: "0"
    b: make text! 0
    insert b a
    a == b
)
; file
(
    a: make file! 0
    insert a #"0"
    a == %"0"
)
(
    a: %"0"
    b: make file! 0
    insert b first a
    a == b
)
(
    a: %"0"
    b: make file! 0
    insert b a
    a == b
)
; email
(
    a: make email! 0
    insert a #"0"
    a == make email! "0"
)
(
    a: make email! "0"
    b: make email! 0
    insert b first a
    a == b
)
(
    a: make email! "0"
    b: make email! 0
    insert b a
    a == b
)
; tag
(
    a: make tag! 0
    insert a #"0"
    a == <0>
)
[#855 (
    a: #{00}
    b: make binary! 0
    insert b first a
    a == b
)]
(
    a: #{00}
    b: make binary! 0
    insert b a
    a == b
)
; insert/part
(
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a spread b 1
    a == [5]
)
(
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a spread b 5
    a == [5 6 7 8 9]
)
(
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a spread b 6
    a == [5 6 7 8 9]
)
(
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a spread b 2147483647
    a == [5 6 7 8 9]
)
(
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a spread b 0
    empty? a
)

; !!! There has been confusion over what the /PART (added in R3-Alpha) means
; as applied to INSERT, APPEND, and CHANGE:
;
; https://github.com/rebol/rebol-issues/issues/2096
;
; It seems a theory is that it was supposed to be a kind of /LIMIT of how much
; to add to the source, and is not speaking in terms of the source series.
; In this case negative indices should likely be the same as 0.  These tests
; are changed in Ren-C from R3-Alpha to append nothing.
(
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a spread b -1
    a == []
)
(
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a spread b -4
    a == []
)
(
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a spread b -5
    a == []
)
(
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a spread b -2147483648
    a == []
)


(
    a: make block! 0
    b: []
    insert a b
    same? b first a
)
; insert/dup
(
    a: make block! 0
    insert/dup a 0 2
    a == [0 0]
)
(
    a: make block! 0
    insert/dup a 0 0
    a == []
)
(
    a: make block! 0
    insert/dup a 0 -1
    a == []
)
(
    a: make block! 0
    insert/dup a 0 -2147483648
    a == []
)
(
    a: make block! 0
    insert/dup a 0 -2147483648
    empty? a
)

[https://github.com/red/red/issues/5171 (
    blk: copy [1 2 3 4 5 6]
    insert blk spread skip blk -2 + length? blk
    [5 6 1 2 3 4 5 6] = head blk
)]
