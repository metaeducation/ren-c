; %parse-break.test.reb
;
; The concept behind UPARSE BREAK is that it only break looping constructs, like
; SOME and REPEAT.  (not INTEGER!-based iteration, should it?)
;
; It also counts as a "soft failure", so the looping construct that was broken
; is considered to have not succeeded.  You need to use STOP instead if you
; want it to be seen as successful.

[
    ~parse-mismatch~ !! (parse "a" [some ["a" break]])
    ("a" = parse "a" [try some ["a" break] "a"])
]

; You should be able to break at any depth
[
    ~parse-mismatch~ !! (parse "aaa" [some ["a" break] "aaa"])
    ~parse-mismatch~ !! (parse "aaa" [some ["a" [break]] "aaa"])
    ~parse-mismatch~ !! (parse "aaa" [some ["a" [["a" | break]]] "aaa"])

    ("aaa" == parse "aaa" [try some ["a" break] "aaa"])
    ("aaa" == parse "aaa" [try some ["a" [break]] "aaa"])
    ("aaa" == parse "aaa" [try some ["a" [["a" break]]] "aaa"])
]
