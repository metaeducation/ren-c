; extract.test.reb
;
; There were no tests for the EXTRACT function in R3-Alpha
; This adds a couple to be better than nothing.

([a [c] e] = extract [a b [c] [d] e f] 2)
("ace" = extract "abcdef" 2)

([e [c] a] = extract tail [a b [c] [d] e f] -2)
("eca" = extract tail "abcdef" -2)

; There is an /INDEX feature, but not clear why it is exists instead of SKIP
;
([[c] e] = extract/index [a b [c] [d] e f] 2 3)
("ce" = extract/index "abcdef" 2 3)

; Throw in test for MOVE here too, this is all that's used by whitespacers
(
    block: [a b]
    move/part block 1 1
    [b a] = block
)
