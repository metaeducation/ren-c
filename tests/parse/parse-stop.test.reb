; %parse-stop.test.reb
;
; PARSE STOP is like BREAK except the parse succeeds.  It can optionally
; match a rule and return a result.

[
    ('~void~ = ^ uparse "a" [while ["a" stop]])
    ("a" = uparse "a" [while [stop "a"]])
]

; You should be able to stop at any depth
[
    (uparse? "aaa" [some ["a" stop] "aa"])
    (uparse? "aaa" [some ["a" [stop]] "aa"])
    (uparse? "aaa" [some ["a" [["a" stop]]] "a"])
]

; STOP optionally takes a rule as an argument
[
    (uparse? "aaa" [some ["a" stop "a"] "a"])
    (uparse? "aaa" [some ["a" [stop "a"]] "a"])
    (uparse? "aaa" [some ["a" [["a" stop "a"]]] ""])
]

(
    uparse? "aaa" [some ["a", elide opt stop "b"]]
)
