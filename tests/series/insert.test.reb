; functions/series/insert.r
[
    a: make block! 0
    insert a 0
    a == [0]
]
[
    a: [0]
    b: make block! 0
    insert b first a
    a == b
]
[
    a: [0]
    b: make block! 0
    insert b a
    a == b
]
; paren
[
    a: make group! 0
    insert a 0
    a == first [(0)]
]
[
    a: first [(0)]
    b: make group! 0
    insert b first a
    b == first a
]
[
    a: first [(0)]
    b: make group! 0
    insert b a
    a == b
]
; path
[
    a: make path! 0
    insert a 0
    a == to path! [0]
]

; This test makes a path-in-a-path, which aren't currently handled well.
;[
;    a: to path! [0]
;    b: make path! 0
;    insert b first a
;    a == b
;]

[
    a: to path! [0]
    b: make path! 0
    insert :b a
    a == b
]
; lit-path
[
    a: make lit-path! 0
    insert :a 0
    :a == to lit-path! [0]
]
[
    a: to lit-path! [0]
    b: make lit-path! 0
    insert :b first :a
    :a == :b
]

; This test makes a path-in-a-path, which aren't currently handled well.
;[
;    a: to lit-path! [0]
;    b: make lit-path! 0
;    insert :b :a
;    :a == :b
;]

; set-path
[
    a: make set-path! 0
    insert :a 0
    :a == to set-path! [0]
]
[
    a: to set-path! [0]
    b: make set-path! 0
    insert :b first :a
    :a == :b
]

; This test makes a path-in-a-path, which aren't currently handled well
;[
;    a: to set-path! [0]
;    b: make set-path! 0
;    insert :b :a
;    :a == :b
;]

; string
[
    a: make string! 0
    insert a #"0"
    a == "0"
]
[
    a: "0"
    b: make string! 0
    insert b first a
    a == b
]
[
    a: "0"
    b: make string! 0
    insert b a
    a == b
]
; file
[
    a: make file! 0
    insert a #"0"
    a == %"0"
]
[
    a: %"0"
    b: make file! 0
    insert b first a
    a == b
]
[
    a: %"0"
    b: make file! 0
    insert b a
    a == b
]
; email
[
    a: make email! 0
    insert a #"0"
    a == #[email! "0"]
]
[
    a: #[email! "0"]
    b: make email! 0
    insert b first a
    a == b
]
[
    a: #[email! "0"]
    b: make email! 0
    insert b a
    a == b
]
; url
[
    a: make url! 0
    insert a #"0"
    a == #[url! "0"]
]
[
    a: #[url! "0"]
    b: make url! 0
    insert b first a
    a == b
]
[
    a: #[url! "0"]
    b: make url! 0
    insert b a
    a == b
]
; tag
[
    a: make tag! 0
    insert a #"0"
    a == <0>
]
; bug#855
[
    a: #{00}
    b: make binary! 0
    insert b first a
    a == b
]
[
    a: #{00}
    b: make binary! 0
    insert b a
    a == b
]
; insert/part
[
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a b 1
    a == [5]
]
[
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a b 5
    a == [5 6 7 8 9]
]
[
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a b 6
    a == [5 6 7 8 9]
]
[
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a b 2147483647
    a == [5 6 7 8 9]
]
[
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a b 0
    empty? a
]
[
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a b -1
    a == [4]
]
[
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a b -4
    a == [1 2 3 4]
]
[
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a b -5
    a == [1 2 3 4]
]
[
    a: make block! 0
    b: at [1 2 3 4 5 6 7 8 9] 5
    insert/part a b -2147483648
    a == [1 2 3 4]
]
; insert/only
[
    a: make block! 0
    b: []
    insert/only a b
    same? b first a
]
; insert/dup
[
    a: make block! 0
    insert/dup a 0 2
    a == [0 0]
]
[
    a: make block! 0
    insert/dup a 0 0
    a == []
]
[
    a: make block! 0
    insert/dup a 0 -1
    a == []
]
[
    a: make block! 0
    insert/dup a 0 -2147483648
    a == []
]
[
    a: make block! 0
    insert/dup a 0 -2147483648
    empty? a
]
