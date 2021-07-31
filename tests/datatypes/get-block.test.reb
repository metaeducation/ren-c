; GET-BLOCK! tests
;
; GET-BLOCK! really just calls REDUCE at the moment.  So instead of saying:
;
;     >> repend [a b c] [1 + 2 3 + 4]
;     == [a b c 3 7]
;
; You can write:
;
;     >> append [a b c] :[1 + 2 3 + 4]
;     == [a b c 3 7]
;

(get-block! = type of first [:[a b c]])
(get-path! = type of first [:[a b c]/d])

(
    a: 10 b: 20
    [10 20] = :[a b]
)

([a b c 3 7] = append [a b c] :[1 + 2 3 + 4])
